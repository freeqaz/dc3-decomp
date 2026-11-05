// Native cache backend (native/src/platform/Cache_Stub.cpp) end-to-end tests.
//
// The image keeps the song-info cache and the global options cache in an Xbox
// content package; on native that is a directory tree under $DC3_CACHE_DIR
// (default ~/.local/share/dc3/cache). These tests drive the real API the way
// SaveLoadManager does -- issue an Async call, poll IsDone(), read
// GetLastResult() -- and assert the bytes that end up on DISK, not just the
// CacheResult the backend reports. A backend that "succeeds" without doing the
// I/O fails here.
//
// The full cycle is the deliverable: create the manager, search, mount, write,
// unmount, re-create the manager through CacheMgr::CreateCacheMgr() (which is
// what the HX_NATIVE branch in src/system/utl/CacheMgr.cpp exists for),
// search again, mount, read the bytes back and compare, then delete the file
// and the cache and assert both are gone from the filesystem.

#include <gtest/gtest.h>

#include "platform/Cache_Native.h"
#include "utl/Cache.h"
#include "utl/CacheMgr.h"
#include "utl/Str.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

// Drives the manager's deferred completion the way SystemPoll() does.
bool Pump(CacheMgr *mgr, int frames = 8) {
    for (int i = 0; i < frames && !mgr->IsDone(); ++i) {
        mgr->Poll();
    }
    return mgr->IsDone();
}

std::string MakeTempRoot() {
    char tmpl[] = "/tmp/dc3-cache-native-XXXXXX";
    char *dir = mkdtemp(tmpl);
    EXPECT_NE(dir, nullptr);
    return dir ? std::string(dir) : std::string();
}

bool PathExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool IsDir(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<unsigned char> ReadRawFile(const std::string &path) {
    std::vector<unsigned char> out;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        return out;
    }
    unsigned char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.insert(out.end(), buf, buf + n);
    }
    fclose(f);
    return out;
}

void RemovePath(const std::string &path) {
    struct stat st;
    if (lstat(path.c_str(), &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path.c_str());
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != nullptr) {
                if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
                    continue;
                }
                RemovePath(path + "/" + ent->d_name);
            }
            closedir(dir);
        }
        rmdir(path.c_str());
        return;
    }
    remove(path.c_str());
}

// Creates a manager through the native init entry point and points the backend
// at `root`.
CacheMgr *MakeManager(const std::string &root) {
    setenv("DC3_CACHE_DIR", root.c_str(), 1);
    TheCacheMgr = nullptr;
    CacheMgrNativeInit();
    return TheCacheMgr;
}

} // namespace

// The deliverable cycle, with real bytes verified on both sides of a remount.
TEST(CacheNative, FullCyclePersistsRealBytes) {
    std::string root = MakeTempRoot();
    ASSERT_FALSE(root.empty());

    CacheMgr *mgr = MakeManager(root);
    ASSERT_NE(mgr, nullptr);

    // 1. Search on an empty cache root: the image reports kCache_ErrorCacheNotFound
    //    (the state machine turns that into "create the cache").
    CacheID *id = nullptr;
    ASSERT_TRUE(mgr->SearchAsync("w38cache", &id));
    ASSERT_TRUE(Pump(mgr));
    EXPECT_EQ(mgr->GetLastResult(), kCache_ErrorCacheNotFound);
    EXPECT_EQ(id, nullptr);

    // 2. Create the cache id the way the state machine does when the search
    //    found nothing, then mount it (which creates the directory).
    ASSERT_TRUE(mgr->ShowUserSelectUIAsync(nullptr, 0x25800, "w38cache", "w38 test cache", &id));
    ASSERT_TRUE(Pump(mgr));
    EXPECT_EQ(mgr->GetLastResult(), kCache_NoError);
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->GetDeviceID(), 1u); // one synthetic local storage device
    EXPECT_EQ(
        std::string(id->GetCachePath("song_info.bin")),
        root + "/w38cache/song_info.bin"
    );

    Cache *cache = nullptr;
    ASSERT_TRUE(mgr->MountAsync(id, &cache, nullptr));
    ASSERT_TRUE(Pump(mgr));
    EXPECT_EQ(mgr->GetLastResult(), kCache_NoError);
    ASSERT_NE(cache, nullptr);
    EXPECT_STREQ(cache->GetCacheName(), "w38cache");
    EXPECT_TRUE(IsDir(root + "/w38cache"));

    // 3. Write a payload with an embedded NUL so a length-truncating write
    //    cannot pass.
    std::vector<unsigned char> payload;
    const char head[] = "w38-cache payload:";
    payload.insert(payload.end(), head, head + sizeof(head) - 1);
    for (int i = 0; i < 256; ++i) {
        payload.push_back((unsigned char)i);
    }
    ASSERT_TRUE(cache->WriteAsync("song_info.bin", payload.data(), (unsigned int)payload.size(), nullptr));
    EXPECT_TRUE(cache->IsDone());
    EXPECT_EQ(cache->GetLastResult(), kCache_NoError);

    // The bytes must exist on disk, at the documented location, before any
    // remount can "read them back".
    std::string filePath = root + "/w38cache/song_info.bin";
    ASSERT_TRUE(PathExists(filePath));
    EXPECT_EQ(ReadRawFile(filePath), payload);

    // 4. Unmount.
    ASSERT_TRUE(mgr->UnmountAsync(&cache, nullptr));
    ASSERT_TRUE(Pump(mgr));
    EXPECT_EQ(mgr->GetLastResult(), kCache_NoError);
    EXPECT_EQ(cache, nullptr);

    // 5. Re-create the manager through the image's factory (this is the
    //    HX_NATIVE branch added to src/system/utl/CacheMgr.cpp), search for
    //    the cache that is now on disk, mount and read the file back.
    delete mgr;
    TheCacheMgr = nullptr;
    CacheMgr *mgr2 = CacheMgr::CreateCacheMgr();
    ASSERT_NE(mgr2, nullptr);
    TheCacheMgr = mgr2;

    id = nullptr;
    ASSERT_TRUE(mgr2->SearchAsync("w38cache", &id));
    ASSERT_TRUE(Pump(mgr2));
    EXPECT_EQ(mgr2->GetLastResult(), kCache_NoError);
    ASSERT_NE(id, nullptr);

    cache = nullptr;
    ASSERT_TRUE(mgr2->MountAsync(id, &cache, nullptr));
    ASSERT_TRUE(Pump(mgr2));
    EXPECT_EQ(mgr2->GetLastResult(), kCache_NoError);
    ASSERT_NE(cache, nullptr);

    unsigned int size = 0;
    ASSERT_TRUE(cache->GetFileSizeAsync("song_info.bin", &size, nullptr));
    EXPECT_EQ(cache->GetLastResult(), kCache_NoError);
    EXPECT_EQ(size, (unsigned int)payload.size());

    std::vector<unsigned char> readback(payload.size() + 16, 0xCD);
    ASSERT_TRUE(cache->ReadAsync("song_info.bin", readback.data(), size, nullptr));
    EXPECT_EQ(cache->GetLastResult(), kCache_NoError);
    EXPECT_EQ(0, memcmp(readback.data(), payload.data(), payload.size()));

    // 6. Delete the file, then the cache; both must be gone from disk.
    ASSERT_TRUE(cache->DeleteSync("song_info.bin"));
    EXPECT_EQ(cache->GetLastResult(), kCache_NoError);
    EXPECT_FALSE(PathExists(filePath));

    ASSERT_TRUE(mgr2->UnmountAsync(&cache, nullptr));
    ASSERT_TRUE(Pump(mgr2));
    EXPECT_EQ(cache, nullptr);

    ASSERT_TRUE(mgr2->DeleteAsync(id));
    ASSERT_TRUE(Pump(mgr2));
    EXPECT_EQ(mgr2->GetLastResult(), kCache_NoError);
    EXPECT_FALSE(PathExists(root + "/w38cache"));

    delete id;
    delete mgr2;
    TheCacheMgr = nullptr;
    RemovePath(root);
}

// The image's directory listing is recursive and reports cache-relative names
// (CacheXbox::ThreadGetDir). Sizes are what GetFileSizeAsync will hand back.
TEST(CacheNative, DirectoryListingReportsRelativeNamesAndSizes) {
    std::string root = MakeTempRoot();
    ASSERT_FALSE(root.empty());
    CacheMgr *mgr = MakeManager(root);
    ASSERT_NE(mgr, nullptr);

    CacheID *id = nullptr;
    ASSERT_TRUE(mgr->ShowUserSelectUIAsync(nullptr, 0x1000, "listing", "listing test", &id));
    ASSERT_TRUE(Pump(mgr));
    ASSERT_NE(id, nullptr);

    Cache *cache = nullptr;
    ASSERT_TRUE(mgr->MountAsync(id, &cache, nullptr));
    ASSERT_TRUE(Pump(mgr));
    ASSERT_NE(cache, nullptr);

    char a[] = "abc";
    char b[] = "0123456789";
    ASSERT_TRUE(cache->WriteAsync("sub/a.bin", a, sizeof(a) - 1, nullptr));
    ASSERT_EQ(cache->GetLastResult(), kCache_NoError);
    ASSERT_TRUE(cache->WriteAsync("b.bin", b, sizeof(b) - 1, nullptr));
    ASSERT_EQ(cache->GetLastResult(), kCache_NoError);

    std::vector<CacheDirEntry> entries;
    ASSERT_TRUE(cache->GetDirectoryAsync(nullptr, &entries, nullptr));
    EXPECT_EQ(cache->GetLastResult(), kCache_NoError);
    ASSERT_EQ(entries.size(), 2u);

    int found = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        std::string name(entries[i].mName.c_str());
        if (name == "b.bin") {
            EXPECT_EQ(entries[i].mSize, (int)(sizeof(b) - 1));
            ++found;
        } else if (name == "sub/a.bin") {
            EXPECT_EQ(entries[i].mSize, (int)(sizeof(a) - 1));
            ++found;
        }
    }
    EXPECT_EQ(found, 2);

    u64 freeSpace = 0;
    EXPECT_TRUE(cache->GetFreeSpaceSync(&freeSpace));
    EXPECT_EQ(cache->GetLastResult(), kCache_NoError);
    EXPECT_TRUE(cache->IsConnectedSync());

    ASSERT_TRUE(mgr->UnmountAsync(&cache, nullptr));
    ASSERT_TRUE(Pump(mgr));
    ASSERT_TRUE(mgr->DeleteAsync(id));
    ASSERT_TRUE(Pump(mgr));
    delete id;
    delete mgr;
    TheCacheMgr = nullptr;
    RemovePath(root);
}

// The init entry point is idempotent: it must not replace an existing manager.
TEST(CacheNative, InitIsIdempotent) {
    std::string root = MakeTempRoot();
    ASSERT_FALSE(root.empty());
    CacheMgr *mgr = MakeManager(root);
    ASSERT_NE(mgr, nullptr);
    CacheMgrNativeInit();
    EXPECT_EQ(TheCacheMgr, mgr);
    delete mgr;
    TheCacheMgr = nullptr;
    RemovePath(root);
}
