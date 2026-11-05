// DC3 Native Port - Cache System (native backend)
// Replaces CacheMgr_Xbox.cpp / Cache_Xbox.cpp / CacheMgr_Xbox.h / Cache_Xbox.h.
//
// The image keeps its caches (the song-info cache and the "global" options
// cache) in an Xbox content package, reached through the XContent* API. On
// native the same tree is a plain directory:
//
//     $DC3_CACHE_DIR/<cache name>/<relative file path>
//
// $DC3_CACHE_DIR defaults to $HOME/.local/share/dc3/cache. A "cache" is a
// subdirectory named by the cache name; a "cache file" is a file inside it.
//
// ASYNC CONTRACT. Every Async entry point is asynchronous by contract: the
// caller issues it, then polls IsDone() and reads GetLastResult(). There are
// no worker threads here, so the I/O is performed inline in the Async call
// and only the *completion* is deferred:
//   * CacheMgrNative operations stay registered (IsDone() == false) until the
//     next Poll(). SystemPoll() calls TheCacheMgr->Poll() every frame, so the
//     caller's state machine sees the image's frame-by-frame timing.
//   * CacheNative operations complete inline (IsDone() == true when the call
//     returns). Nothing in the image ever calls Cache::Poll() on a mounted
//     cache -- SystemPoll polls only TheCacheMgr, and CacheXbox completes its
//     own ops from its worker thread -- so deferring a cache op would leave
//     every caller polling forever.

#include "Cache_Native.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/Cache.h"
#include "utl/CacheMgr.h"
#include "utl/Str.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

// The image uses local storage (the hard drive or a memory unit); native has
// exactly one synthetic storage device, and a cache id's device id is only
// used to prove the device is still attached.
static const unsigned int kNativeCacheDeviceID = 1;

// ============================================================================
// Filesystem helpers
// ============================================================================

// Root of the native cache tree: $DC3_CACHE_DIR, else ~/.local/share/dc3/cache.
static void CacheRootDir(char *buf, size_t bufSize) {
    const char *envDir = getenv("DC3_CACHE_DIR");
    if (envDir && envDir[0]) {
        snprintf(buf, bufSize, "%s", envDir);
        return;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = "/tmp";
    }
    snprintf(buf, bufSize, "%s/.local/share/dc3/cache", home);
}

// <root>/<cache name>
static void MakeCacheDirPath(char *buf, size_t bufSize, const char *name) {
    char root[512];
    CacheRootDir(root, sizeof(root));
    snprintf(buf, bufSize, "%s/%s", root, name);
}

// Normalise an in-cache path to a relative POSIX path ("a\b" -> "a/b",
// leading separators dropped). The image stores Windows-style names.
static void NormalizeRelPath(String &path) {
    path.ReplaceAll('\\', '/');
    while (path.length() != 0 && path[0] == '/') {
        path.erase(0, 1);
    }
    while (path.length() != 0 && path[path.length() - 1] == '/') {
        path.erase(path.length() - 1, 1);
    }
}

// <root>/<cache name>[/<relative path>]
static void MakeCacheFilePath(char *buf, size_t bufSize, const char *name, const char *file) {
    char dir[512];
    MakeCacheDirPath(dir, bufSize < sizeof(dir) ? bufSize : sizeof(dir), name);
    if (!file) {
        snprintf(buf, bufSize, "%s", dir);
        return;
    }
    String rel(file);
    NormalizeRelPath(rel);
    if (rel.empty()) {
        snprintf(buf, bufSize, "%s", dir);
    } else {
        snprintf(buf, bufSize, "%s/%s", dir, rel.c_str());
    }
}

// mkdir -p
static bool EnsureDir(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p != '\0'; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat st;
    return stat(tmp, &st) == 0 && S_ISDIR(st.st_mode);
}

// mkdir -p of a file's parent directory.
static bool EnsureParentDir(const char *path) {
    String s(path);
    unsigned int sep = s.find_last_of('/');
    if (sep == FixedString::npos) {
        return true;
    }
    String dir = s.substr(0, sep);
    if (dir.empty()) {
        return true;
    }
    return EnsureDir(dir.c_str());
}

// Recursively remove a file or directory tree. A path that does not exist is
// reported as removed: the caller's end state (nothing there) is satisfied.
static bool RemoveTree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            return false;
        }
        bool ok = true;
        struct dirent *ent;
        while ((ent = readdir(dir)) != nullptr) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
                continue;
            }
            char child[1024];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            if (!RemoveTree(child)) {
                ok = false;
            }
        }
        closedir(dir);
        if (!ok) {
            return false;
        }
        return rmdir(path) == 0;
    }
    return remove(path) == 0;
}

// Remove now-empty directories from `path` upwards, stopping at (and keeping)
// `stopPath`. Mirrors CacheXbox::DeleteParentDirs, which prunes the directory
// chain a deleted cache file leaves behind.
static void RemoveEmptyParents(const char *path, const char *stopPath) {
    String cur(path);
    String stop(stopPath);
    while (cur.length() > stop.length()) {
        unsigned int sep = cur.find_last_of('/');
        if (sep == FixedString::npos) {
            return;
        }
        cur = cur.substr(0, sep);
        if (cur.length() <= stop.length()) {
            return;
        }
        if (rmdir(cur.c_str()) != 0) {
            return;
        }
    }
}

static void FillDateTime(DateTime &dt, time_t t) {
    struct tm local;
    if (!localtime_r(&t, &local)) {
        return;
    }
    dt = DateTime(
        (unsigned short)(local.tm_year + 1900),
        (unsigned char)(local.tm_mon + 1),
        (unsigned char)local.tm_mday,
        (unsigned char)local.tm_hour,
        (unsigned char)local.tm_min,
        (unsigned char)local.tm_sec
    );
}

// Recursive directory walk, matching CacheXbox::ThreadGetDir: entries come
// back with a cache-relative '/' separated name and the files' sizes. A
// missing directory is an empty listing (that is what the image's
// ERROR_FILE_NOT_FOUND arm does), not an error.
static bool CollectDir(
    const char *dirPath, const String &basePath, std::vector<CacheDirEntry> *out
) {
    DIR *dir = opendir(dirPath);
    if (!dir) {
        return errno == ENOENT;
    }
    bool ok = true;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
            continue;
        }
        char child[1024];
        snprintf(child, sizeof(child), "%s/%s", dirPath, ent->d_name);
        struct stat st;
        if (stat(child, &st) != 0) {
            ok = false;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            String childBase(basePath);
            childBase += ent->d_name;
            childBase += '/';
            if (!CollectDir(child, childBase, out)) {
                ok = false;
            }
        } else if (S_ISREG(st.st_mode)) {
            CacheDirEntry entry;
            entry.mName = basePath + ent->d_name;
            entry.mSize = (int)st.st_size;
            FillDateTime(entry.mDateTime, st.st_mtime);
            out->push_back(entry);
        }
    }
    closedir(dir);
    return ok;
}

// ============================================================================
// CacheIDNative
// ============================================================================

class CacheIDNative : public CacheID {
public:
    CacheIDNative() {}
    CacheIDNative(const String &name) : mStrCacheName(name) {}
    virtual ~CacheIDNative() {}
    virtual const char *GetCachePath(const char *);
    virtual const char *GetCacheSearchPath(const char *);
    virtual unsigned int GetDeviceID() const { return kNativeCacheDeviceID; }

    const char *Name() const { return mStrCacheName.c_str(); }
    void SetName(const String &name) { mStrCacheName = name; }

private:
    String mStrCacheName;
    // Backing store for GetCachePath/GetCacheSearchPath. The image returns a
    // MakeString buffer (thread-local, rotating); a per-id buffer keeps the
    // returned pointer valid for the id's lifetime without depending on
    // InitMakeString() having run, which it has not when this backend is
    // driven from a unit test or from early boot.
    char mPathBuf[1024];
};

const char *CacheIDNative::GetCachePath(const char *c) {
    if (mStrCacheName.empty()) {
        MILO_FAIL("CacheID::GetCachePath() - mStrCacheName is empty.\n");
    }
    char dir[512];
    MakeCacheDirPath(dir, sizeof(dir), mStrCacheName.c_str());
    if (!c) {
        snprintf(mPathBuf, sizeof(mPathBuf), "%s/", dir);
        return mPathBuf;
    }
    String rel(c);
    NormalizeRelPath(rel);
    if (rel.empty()) {
        snprintf(mPathBuf, sizeof(mPathBuf), "%s/", dir);
    } else {
        snprintf(mPathBuf, sizeof(mPathBuf), "%s/%s", dir, rel.c_str());
    }
    return mPathBuf;
}

const char *CacheIDNative::GetCacheSearchPath(const char *c) {
    if (mStrCacheName.empty()) {
        MILO_FAIL("CacheID::GetCacheSearchPath() - mStrCacheName is empty.\n");
    }
    if (!c) {
        char dir[512];
        MakeCacheDirPath(dir, sizeof(dir), mStrCacheName.c_str());
        snprintf(mPathBuf, sizeof(mPathBuf), "%s", dir);
        return mPathBuf;
    }
    return GetCachePath(c);
}

// ============================================================================
// CacheNative
// ============================================================================

class CacheNative : public Cache {
public:
    CacheNative(const CacheIDNative &id) : mCacheID(id) {}
    virtual ~CacheNative() {}
    virtual const char *GetCacheName() { return mCacheID.Name(); }
    virtual void Poll() {}
    virtual bool IsConnectedSync();
    virtual bool GetFreeSpaceSync(u64 *);
    virtual bool DeleteSync(const char *);
    virtual bool
    GetDirectoryAsync(const char *, std::vector<CacheDirEntry> *, Hmx::Object *);
    virtual bool GetFileSizeAsync(const char *, unsigned int *, Hmx::Object *);
    virtual bool ReadAsync(const char *, void *, unsigned int, Hmx::Object *);
    virtual bool WriteAsync(const char *, void *, unsigned int, Hmx::Object *);
    virtual bool DeleteAsync(const char *, Hmx::Object *);

private:
    void NotifyWriteResult(Hmx::Object *obj);

    CacheIDNative mCacheID;
};

// The image posts this message to the caller's object when a write fails
// immediately and when the async write completes (CacheXbox::WriteAsync /
// CacheXbox::ThreadDone). We complete the write inline, so both cases post
// here.
void CacheNative::NotifyWriteResult(Hmx::Object *obj) {
    if (!obj) {
        return;
    }
    static Message msg("cache_write_result", GetLastResult());
    msg[0] = GetLastResult();
    obj->Handle(msg, true);
}

bool CacheNative::IsConnectedSync() {
    char root[512];
    CacheRootDir(root, sizeof(root));
    return EnsureDir(root);
}

bool CacheNative::GetFreeSpaceSync(u64 *out) {
    if (!out) {
        mLastResult = kCache_ErrorBadParam;
        return false;
    }
    char root[512];
    CacheRootDir(root, sizeof(root));
    struct statvfs vfs;
    if (statvfs(root, &vfs) != 0) {
        MILO_NOTIFY("CacheNative::GetFreeSpaceSync(): statvfs('%s') failed: %s\n", root, strerror(errno));
        mLastResult = kCache_ErrorStorageDeviceMissing;
        return false;
    }
    *out = (u64)vfs.f_bavail * (u64)vfs.f_frsize;
    mLastResult = kCache_NoError;
    return true;
}

bool CacheNative::DeleteSync(const char *cc) {
    if (!IsDone()) {
        mLastResult = kCache_ErrorBusy;
        return false;
    }
    if (!cc) {
        mLastResult = kCache_ErrorBadParam;
        return false;
    }
    char path[1024];
    MakeCacheFilePath(path, sizeof(path), mCacheID.Name(), cc);
    if (remove(path) != 0) {
        mLastResult = (errno == ENOENT) ? kCache_ErrorCacheNotFound : kCache_ErrorUnknown;
        if (mLastResult != kCache_ErrorCacheNotFound) {
            MILO_NOTIFY("CacheNative::DeleteSync(): failed to delete '%s': %s\n", path, strerror(errno));
        }
        return false;
    }
    char dir[512];
    MakeCacheDirPath(dir, sizeof(dir), mCacheID.Name());
    RemoveEmptyParents(path, dir);
    mLastResult = kCache_NoError;
    return true;
}

bool CacheNative::GetDirectoryAsync(
    const char *cc, std::vector<CacheDirEntry> *entries, Hmx::Object *
) {
    if (!IsDone()) {
        mLastResult = kCache_ErrorBusy;
        return false;
    }
    if (!entries) {
        mLastResult = kCache_ErrorBadParam;
        return false;
    }
    char dir[1024];
    MakeCacheFilePath(dir, sizeof(dir), mCacheID.Name(), cc);
    String base;
    mOpCur = kOpDirectory;
    bool ok = CollectDir(dir, base, entries);
    mLastResult = ok ? kCache_NoError : kCache_ErrorUnknown;
    if (!ok) {
        MILO_NOTIFY("CacheNative::GetDirectoryAsync(): failed to read '%s': %s\n", dir, strerror(errno));
    }
    mOpCur = kOpNone;
    return true;
}

bool CacheNative::GetFileSizeAsync(const char *cc, unsigned int *out, Hmx::Object *) {
    if (!IsDone()) {
        mLastResult = kCache_ErrorBusy;
        return false;
    }
    if (!out) {
        mLastResult = kCache_ErrorBadParam;
        return false;
    }
    if (!cc) {
        mLastResult = kCache_ErrorBadParam;
        return false;
    }
    char path[1024];
    MakeCacheFilePath(path, sizeof(path), mCacheID.Name(), cc);
    struct stat st;
    mOpCur = kOpFileSize;
    if (stat(path, &st) != 0) {
        // CacheXbox::ThreadGetFileSize maps ERROR_FILE_NOT_FOUND to
        // kCache_ErrorCacheNotFound and leaves the output untouched.
        mLastResult = (errno == ENOENT) ? kCache_ErrorCacheNotFound : kCache_ErrorUnknown;
        if (mLastResult != kCache_ErrorCacheNotFound) {
            MILO_NOTIFY("CacheNative::GetFileSizeAsync(): stat('%s') failed: %s\n", path, strerror(errno));
        }
    } else {
        *out = (unsigned int)st.st_size;
        mLastResult = kCache_NoError;
    }
    mOpCur = kOpNone;
    return true;
}

bool CacheNative::ReadAsync(const char *cc, void *data, unsigned int size, Hmx::Object *) {
    if (!IsDone()) {
        mLastResult = kCache_ErrorBusy;
        return false;
    }
    if (!cc || !data || size == 0) {
        mLastResult = kCache_ErrorBadParam;
        return false;
    }
    char path[1024];
    MakeCacheFilePath(path, sizeof(path), mCacheID.Name(), cc);
    mOpCur = kOpRead;
    FILE *f = fopen(path, "rb");
    if (!f) {
        // CacheXbox::ThreadRead maps FILE_NOT_FOUND / PATH_NOT_FOUND /
        // NOT_READY to kCache_ErrorStorageDeviceMissing.
        int err = errno;
        mLastResult = (err == ENOENT || err == ENOTDIR) ? kCache_ErrorStorageDeviceMissing
                                                        : kCache_ErrorUnknown;
        if (mLastResult != kCache_ErrorStorageDeviceMissing) {
            MILO_NOTIFY("CacheNative::ReadAsync(): failed to open '%s': %s\n", path, strerror(err));
        }
    } else {
        size_t read = fread(data, 1, size, f);
        bool ok = (read == size);
        if (!ok) {
            // ReadFile succeeds on a short read; mirror that but leave a
            // trace, because a short read means the caller's expectation
            // (size from GetFileSizeAsync) no longer holds.
            MILO_NOTIFY("CacheNative::ReadAsync(): read %zu of %u bytes from '%s'\n", read, size, path);
        }
        mLastResult = kCache_NoError;
        fclose(f);
    }
    mOpCur = kOpNone;
    return true;
}

bool CacheNative::WriteAsync(const char *cc, void *data, unsigned int size, Hmx::Object *obj) {
    if (!IsDone()) {
        mLastResult = kCache_ErrorBusy;
        NotifyWriteResult(obj);
        return false;
    }
    if (!cc || !data || size == 0) {
        mLastResult = kCache_ErrorBadParam;
        NotifyWriteResult(obj);
        return false;
    }
    char path[1024];
    MakeCacheFilePath(path, sizeof(path), mCacheID.Name(), cc);
    mOpCur = kOpWrite;
    // CacheXbox::ThreadWrite creates every directory on the path first.
    if (!EnsureParentDir(path)) {
        mLastResult = kCache_ErrorStorageDeviceMissing;
        MILO_NOTIFY("CacheNative::WriteAsync(): failed to create parent directories for '%s'\n", path);
        NotifyWriteResult(obj);
        mOpCur = kOpNone;
        return true;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        int err = errno;
        mLastResult = (err == ENOSPC) ? kCache_ErrorNotEnoughSpace
                     : (err == ENOENT || err == ENOTDIR || err == EACCES) ? kCache_ErrorStorageDeviceMissing
                                                                          : kCache_ErrorUnknown;
        MILO_NOTIFY("CacheNative::WriteAsync(): failed to open '%s' for writing: %s\n", path, strerror(err));
    } else {
        size_t written = fwrite(data, 1, size, f);
        int closeErr = fclose(f);
        if (written != size || closeErr != 0) {
            mLastResult = (errno == ENOSPC) ? kCache_ErrorNotEnoughSpace : kCache_ErrorUnknown;
            MILO_NOTIFY("CacheNative::WriteAsync(): wrote %zu of %u bytes to '%s'\n", written, size, path);
        } else {
            mLastResult = kCache_NoError;
        }
    }
    NotifyWriteResult(obj);
    mOpCur = kOpNone;
    return true;
}

bool CacheNative::DeleteAsync(const char *cc, Hmx::Object *) {
    if (!IsDone()) {
        mLastResult = kCache_ErrorBusy;
        return false;
    }
    if (!cc) {
        mLastResult = kCache_ErrorBadParam;
        return false;
    }
    char path[1024];
    MakeCacheFilePath(path, sizeof(path), mCacheID.Name(), cc);
    mOpCur = kOpDelete;
    if (remove(path) != 0) {
        mLastResult = (errno == ENOENT) ? kCache_ErrorCacheNotFound : kCache_ErrorUnknown;
        if (mLastResult != kCache_ErrorCacheNotFound) {
            MILO_NOTIFY("CacheNative::DeleteAsync(): failed to delete '%s': %s\n", path, strerror(errno));
        }
    } else {
        char dir[512];
        MakeCacheDirPath(dir, sizeof(dir), mCacheID.Name());
        RemoveEmptyParents(path, dir);
        mLastResult = kCache_NoError;
    }
    mOpCur = kOpNone;
    return true;
}

// CacheDirEntry's copy constructor is defined in Cache_Xbox.cpp in the image,
// which is not part of the native build; std::vector<CacheDirEntry>::push_back
// needs it.
CacheDirEntry::CacheDirEntry(const CacheDirEntry &o)
    : mName(o.mName), mDateTime(o.mDateTime), mSize(o.mSize) {}

// ============================================================================
// CacheMgrNative
// ============================================================================

class CacheMgrNative : public CacheMgr {
public:
    CacheMgrNative();
    virtual ~CacheMgrNative() {}
    virtual void Poll();
    virtual bool SearchAsync(const char *, CacheID **);
    virtual bool
    ShowUserSelectUIAsync(LocalUser *, u64, const char *, const char *, CacheID **);
    virtual bool
    CreateCacheIDFromDeviceID(unsigned int, const char *, const char *, CacheID **);
    virtual bool MountAsync(CacheID *, Cache **, Hmx::Object *);
    virtual bool UnmountAsync(Cache **, Hmx::Object *);
    virtual bool DeleteAsync(CacheID *);

private:
    void EndOp(OpType op, CacheResult result);

    Hmx::Object *mCallback;
};

CacheMgrNative::CacheMgrNative() : mCallback(nullptr) {}

void CacheMgrNative::EndOp(OpType op, CacheResult result) {
    SetLastResult(result);
    SetOp(op);
}

// Completes the operation registered by the last Async call. The I/O itself
// already happened inline; this is the deferred completion the caller's
// polling loop waits for.
void CacheMgrNative::Poll() {
    OpType op = GetOp();
    if (op == kOpNone) {
        return;
    }
    switch (op) {
    case kOpMount:
        if (mCallback) {
            static Message msg("cache_mgr_mount_result", GetLastResult());
            msg[0] = GetLastResult();
            mCallback->Handle(msg, true);
        }
        break;
    case kOpUnmount:
        if (mCallback) {
            static Message msg("cache_mgr_unmount_result", GetLastResult());
            msg[0] = GetLastResult();
            mCallback->Handle(msg, true);
        }
        break;
    default:
        break;
    }
    mCallback = nullptr;
    SetOp(kOpNone);
}

bool CacheMgrNative::SearchAsync(const char *cc, CacheID **ppCacheID) {
    if (!IsDone()) {
        SetLastResult(kCache_ErrorBusy);
        return false;
    }
    if (!ppCacheID || *ppCacheID) {
        MILO_LOG("SearchAsync BAD PARAM: ppCacheID = 0x%X\n", ppCacheID);
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    String cacheName(cc ? cc : "");
    if (cacheName.empty()) {
        MILO_LOG("SearchAsync BAD PARAM: cache name is empty\n");
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    char dir[1024];
    MakeCacheDirPath(dir, sizeof(dir), cacheName.c_str());
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        *ppCacheID = new CacheIDNative(cacheName);
        EndOp(kOpSearch, kCache_NoError);
    } else {
        // The image enumerates the storage device and reports
        // kCache_ErrorCacheNotFound when nothing matches the name. The state
        // machine responds by creating the cache through the select UI, so
        // this must stay a completed op, not a failed call.
        EndOp(kOpSearch, kCache_ErrorCacheNotFound);
    }
    return true;
}

bool CacheMgrNative::ShowUserSelectUIAsync(
    LocalUser *, u64, const char *cc1, const char *cc2, CacheID **ppCacheID
) {
    if (!IsDone()) {
        SetLastResult(kCache_ErrorBusy);
        return false;
    }
    if (!ppCacheID || *ppCacheID) {
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    String cacheName(cc1 ? cc1 : "");
    String description(cc2 ? cc2 : "");
    if (cacheName.empty() || description.empty()) {
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    // Native has no device-selector UI (and only one storage device), so the
    // user's choice is always the local cache directory.
    *ppCacheID = new CacheIDNative(cacheName);
    EndOp(kOpChoose, kCache_NoError);
    return true;
}

bool CacheMgrNative::CreateCacheIDFromDeviceID(
    unsigned int, const char *cc1, const char *cc2, CacheID **ppCacheID
) {
    if (!IsDone()) {
        SetLastResult(kCache_ErrorBusy);
        return false;
    }
    if (!ppCacheID || *ppCacheID) {
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    String cacheName(cc1 ? cc1 : "");
    String description(cc2 ? cc2 : "");
    if (cacheName.empty() || description.empty()) {
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    *ppCacheID = new CacheIDNative(cacheName);
    // CacheMgrXbox::CreateCacheIDFromDeviceID completes synchronously.
    SetLastResult(kCache_NoError);
    SetOp(kOpNone);
    return true;
}

bool CacheMgrNative::MountAsync(CacheID *id, Cache **ppCache, Hmx::Object *cb) {
    if (!IsDone()) {
        MILO_NOTIFY("MountAsync: !IsDone() current op is %i\n", (int)GetOp());
        SetLastResult(kCache_ErrorBusy);
        return false;
    }
    if (!ppCache) {
        MILO_NOTIFY("ppCache == NULL\n");
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    if (*ppCache) {
        MILO_NOTIFY("*ppCache != NULL\n");
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    CacheIDNative *nativeID = dynamic_cast<CacheIDNative *>(id);
    if (!nativeID) {
        MILO_NOTIFY("MountAsync: cache id is not a native cache id\n");
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    // XContentCreateEx creates the package when it does not exist yet; that is
    // how the image's "cache not found" -> mount-for-write path works.
    char dir[1024];
    MakeCacheDirPath(dir, sizeof(dir), nativeID->Name());
    if (!EnsureDir(dir)) {
        MILO_NOTIFY("CacheMgrNative::MountAsync(): failed to create '%s': %s\n", dir, strerror(errno));
        SetLastResult(kCache_ErrorStorageDeviceMissing);
        return false;
    }
    *ppCache = new CacheNative(*nativeID);
    mCallback = cb;
    EndOp(kOpMount, kCache_NoError);
    return true;
}

bool CacheMgrNative::UnmountAsync(Cache **ppCache, Hmx::Object *cb) {
    if (!IsDone()) {
        MILO_NOTIFY("UnmountAsync: !IsDone() current op is %i\n", (int)GetOp());
        SetLastResult(kCache_ErrorBusy);
        return false;
    }
    if (!ppCache || !*ppCache) {
        MILO_LOG("UnmountAsync: ppCache is NULL (MU pull?)\n");
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    // CacheMgrXbox::PollUnmount RELEASEs *mppCache.
    delete *ppCache;
    *ppCache = nullptr;
    mCallback = cb;
    EndOp(kOpUnmount, kCache_NoError);
    return true;
}

bool CacheMgrNative::DeleteAsync(CacheID *id) {
    if (!IsDone()) {
        SetLastResult(kCache_ErrorBusy);
        return false;
    }
    CacheIDNative *nativeID = dynamic_cast<CacheIDNative *>(id);
    if (!nativeID) {
        SetLastResult(kCache_ErrorBadParam);
        return false;
    }
    // XContentDelete removes the whole content package.
    char dir[1024];
    MakeCacheDirPath(dir, sizeof(dir), nativeID->Name());
    bool ok = RemoveTree(dir);
    if (!ok) {
        MILO_NOTIFY("CacheMgrNative::DeleteAsync(): failed to remove '%s'\n", dir);
    }
    EndOp(kOpDelete, ok ? kCache_NoError : kCache_ErrorUnknown);
    return true;
}

// ============================================================================
// Entry points
// ============================================================================

// CacheMgr::CreateCacheMgr()'s HX_NATIVE branch calls this (the class is
// defined only in this native-only translation unit).
CacheMgr *CacheMgrCreateNativeBackend() {
    return new CacheMgrNative();
}

// Constructs TheCacheMgr if it is still null. Deliberately NOT called from
// src/system/os/System.cpp yet: wiring the cache into the boot path is a
// separate step, and SaveLoadManager::Init() is still gated.
void CacheMgrNativeInit() {
    if (!TheCacheMgr) {
        TheCacheMgr = CacheMgrCreateNativeBackend();
    }
}

void CacheMgrNativeTerminate() {
    RELEASE(TheCacheMgr);
}
