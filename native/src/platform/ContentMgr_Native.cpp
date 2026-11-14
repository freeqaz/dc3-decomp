// DC3 Native Port - ContentMgr: base-game DTA providers + downloadable content.
// Replaces ContentMgr_Xbox.cpp.
//
// Two halves:
//
// 1. ROOT CONTENT (the disc). The image adds a RootContent per `content_mgr
//    roots` entry and lets ContentMgr::PollRefresh enumerate `<root>/<dir>/
//    <pattern>` for every callback. Native lists `orig-assets/extracted/...`
//    instead and queues the matching ark paths, because the shared engine's
//    FileEnumerate always answers from the ark (PLAN D-4). Unchanged here.
//
// 2. DOWNLOADABLE CONTENT. A port of XboxContentMgr's discovery/mount state
//    machine (src/system/os/ContentMgr_Xbox.cpp) with the XContent* calls
//    replaced by host directories. Each package is one directory -- the
//    extracted STFS package, `songs/songs.dta` and all -- under a DLC root
//    named by DC3_DLC_DIR (scripts/dlc/import.sh makes them). For each one:
//
//      StartRefresh   ContentDiscovered(name) on every callback; a package no
//                     callback already knows (SongMgr: not in the song cache)
//                     is marked kNeedsMounting, as XboxContent's ctor does.
//      PollRefresh    NativeContent::Poll mounts it (1 -> 2 -> 4). The base
//                     sees 2 -> 4 and calls NotifyMounted ->
//                     ContentMounted(name, root), which is where SongMgr learns
//                     the root it later prefixes onto the song's paths
//                     (HamSongMgr::SongAudioData). Then its `<root>/songs/
//                     songs*.dta` is queued with the package name, so
//                     SongMgr::ContentLoaded caches it (CacheSongData) rather
//                     than adding it as disc content. Packages are mounted at
//                     most six at a time and unmounted after their DTAs load,
//                     exactly as on the 360.
//      MountContent   play time: PreloadPanel / TexLoadPanel / SongPreview
//                     mount the song's package and wait for ContentMounted.
//
//    A package's root is the 360's: the drive `cntNNNNNNNN:` (XboxContent's
//    kContentRootFormat, numbered in discovery order), mapped to the package's
//    host directory while it is mounted (NativeMountDrive in the engine's
//    File_Native.cpp, the counterpart of XContentCreate / XContentClose).
//    The drive is not cosmetic: the `song` ObjectDir type's change_subdir
//    (world/world_objects.dta) sends a downloaded song's
//    `../../world/shared/director.milo` subdir -- which holds the HamDirector
//    every song.anim key targets -- back to the disc's copy only when the path
//    has a ':' in it. With a drive-less root the subdir resolved inside the
//    package, did not load, and HamDirector::SetupRoutineBuilderAnims crashed
//    on the unbound keys.
//    FileMakePathBuf lowercases every path, so the files inside a package must
//    be lowercase on disk; scripts/dlc/import.sh extracts them that way.

#include "obj/Data.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include "utl/Std.h"
#include <algorithm>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef __EMSCRIPTEN__
// milo-native-engine src/platform/File_Native.cpp: content drives.
extern bool NativeMountDrive(const char *drive, const char *hostDir);
extern void NativeUnmountDrive(const char *drive);
#endif

namespace {

bool IsDirectory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// XboxContent's kContentRootFormat.
const char *const kContentRootFormat = "cnt%08x";

#ifndef __EMSCRIPTEN__
bool MountDrive(const char *drive, const char *hostDir) {
    return NativeMountDrive(drive, hostDir);
}
void UnmountDrive(const char *drive) { NativeUnmountDrive(drive); }
#else
// No DLC on web yet (PLAN D-7): DiscoverPackages finds none, so these never run.
bool MountDrive(const char *, const char *) { return false; }
void UnmountDrive(const char *) {}
#endif

} // namespace

// One downloadable package: a directory standing in for a mounted STFS
// container. Mirrors XboxContent's state machine; the overlapped XContent
// calls complete on the next Poll, so the base sees every 1->2->4 and 4->3->0
// transition it keys its notifications on.
class NativeContent : public Content {
public:
    NativeContent(const char *name, int index, const char *hostDir, bool discovered)
        : mName(name), mRoot(MakeString(kContentRootFormat, index)),
          mContentPath(MakeString("%s:", mRoot.c_str())), mHostDir(hostDir),
          mState(discovered ? kNeedsMounting : kUnmounted), mLRM(0) {}
    virtual ~NativeContent() {
        if (mState == kMounting || mState == kMounted) UnmountDrive(mRoot.c_str());
    }
    virtual const char *Root() { return mContentPath.c_str(); }
    virtual bool OnMemcard() { return false; }
    virtual ContentLocT Location() { return kLocationHDD; }
    virtual State GetState() { return mState; }
    virtual void Poll() {
        if (mState == kNeedsMounting) {
            MILO_LOG("Mounting content '%s'\n", mName.Str());
            // XContentCreate: a package that has gone away fails here.
            if (IsDirectory(mHostDir.c_str()) && MountDrive(mRoot.c_str(), mHostDir.c_str())) {
                mState = kMounting;
            } else {
                mState = kContentDeleting;
            }
        } else if (mState == kMounting) {
            mState = kMounted;
        } else if (mState == kUnmounting) {
            UnmountDrive(mRoot.c_str()); // XContentClose
            mState = kUnmounted;
        }
    }
    virtual void Mount() {
        if (mState == kUnmounted) {
            mState = kNeedsMounting;
            static unsigned int count = 0;
            mLRM = ++count;
        }
    }
    virtual void Unmount() {
        if (mState == kMounted) {
            mState = kUnmounting;
        } else if (mState == kNeedsMounting || mState == kContentDeleting) {
            mState = kUnmounted;
        }
    }
    virtual Symbol FileName() { return mName; }
    virtual const char *DisplayName() { return mName.Str(); }
    virtual unsigned int GetLRM() { return mLRM; }

    const char *HostDir() const { return mHostDir.c_str(); }

private:
    Symbol mName;
    String mRoot; // cntNNNNNNNN
    String mContentPath; // cntNNNNNNNN:, what Root() reports
    String mHostDir;
    State mState;
    unsigned int mLRM;
};

class NativeContentMgr : public ContentMgr {
public:
    NativeContentMgr() : mNextRoot(0) {}
    virtual ~NativeContentMgr() = default;

    virtual void Init() {
        ContentMgr::Init();
        DataArray *cfg = SystemConfig("content_mgr");
        DataArray *ignored = cfg ? cfg->FindArray("ignored_content", false) : nullptr;
        if (ignored) {
            for (int i = 1; i < ignored->Size(); i++) {
                mIgnored.push_back(ignored->Str(i));
            }
        }
    }

    virtual void Terminate() {}

    virtual void StartRefresh() {
        if (!mDirty) {
            return;
        }
        mDirty = false;
        mCallbackFiles.clear();
        RELEASE(mLoader);
        FOREACH (it, mContents) {
            if ((*it)->GetState() == Content::kMounted) {
                NotifyUnmounted(*it);
            }
        }
        DeleteAll(mContents);
        for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ++it) {
            (*it)->ContentStarted();
        }
        // Root content first, as the image's mContents lists the roots before
        // any package; each package's files follow as it mounts.
        QueueRootCallbackFiles();
        int toMount = DiscoverPackages();
        for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ++it) {
            (*it)->ContentMountBegun(toMount);
        }
        // XboxContentMgr's kDiscoveryMounting is the enumeration, which ran
        // synchronously above; go straight to polling the mounts. The base's
        // state-3 pass broadcasts ContentAllMounted and moves on to 4.
        mState = kDiscoveryLoading;
    }

    virtual void PollRefresh() {
        // XboxContentMgr::PollRefresh's own states, minus the enumeration.
        if (mState == kDiscoveryLoading) {
            int mountedCount = 0;
            FOREACH (it, mContents) {
                if ((*it)->GetState() == Content::kMounted) mountedCount++;
            }
            if (mountedCount >= 6) {
                mState = kDiscoveryCheckIfDone;
            }
        } else if (mState == kMounting) {
            bool allDone = true;
            FOREACH (it, mContents) {
                Content::State state = (*it)->GetState();
                if (state == Content::kMounted) {
                    (*it)->Unmount();
                    allDone = false;
                } else {
                    allDone = (state != Content::kNeedsMounting) & allDone;
                }
            }
            if (!allDone) {
                mState = kDiscoveryLoading;
            }
        }
        ContentMgr::PollRefresh();
    }

    virtual bool MountContent(Symbol name) {
        // XboxContentMgr::MountContent: mount `name`, and keep at most six
        // packages mounted by unmounting the least recently mounted.
        bool alreadyMounted = false;
        bool found = false;
        FOREACH (it, mContents) {
            if (name == (*it)->FileName()) {
                found = true;
                (*it)->Mount();
                mState = kContentMgrState7;
                if ((*it)->GetState() == Content::kMounted) {
                    alreadyMounted = true;
                }
                break;
            }
        }
        if (!found) {
            MILO_NOTIFY("\"%s\" not found to mount.", name.Str());
        }
        int mountingCount = 0;
        int prevCount = 0;
        bool done = false;
        do {
            Content *oldest = nullptr;
            unsigned int oldestLRM = 0xFFFFFFFF;
            FOREACH (it, mContents) {
                Content::State state = (*it)->GetState();
                if (state == Content::kMounted || state == Content::kMounting
                    || state == Content::kNeedsMounting) {
                    mountingCount++;
                    if (name != (*it)->FileName() && (*it)->GetLRM() < oldestLRM
                        && (*it)->GetState() != Content::kMounting) {
                        oldest = *it;
                        oldestLRM = oldest->GetLRM();
                    }
                }
            }
            if (mountingCount == prevCount) {
                done = true;
            } else if (mountingCount > 6 && oldest) {
                oldest->Unmount();
                mState = kContentMgrState7;
            }
            prevCount = mountingCount;
            mountingCount = 0;
        } while (!done);
        return alreadyMounted;
    }

    virtual bool IsMounted(Symbol name) {
        FOREACH (it, mContents) {
            if (name == (*it)->FileName()) {
                return (*it)->GetState() == Content::kMounted;
            }
        }
        return false;
    }

    virtual bool IsCorrupt(Symbol, const char *&) { return false; }
    // Deleting a package would delete the user's files; not on native.
    virtual bool DeleteContent(Symbol) { return false; }

protected:
    virtual void NotifyMounted(Content *c) {
        FOREACH (it, mCallbacks) {
            (*it)->ContentMounted(c->FileName().Str(), c->Root());
        }
        // A refresh-time mount: queue what ContentMgr::PollRefresh's state-4
        // pass would FileEnumerate for it (`<root>/<dir>/<pattern>`). The
        // engine's FileEnumerate reads only the ark (PLAN D-4), so the base's
        // own pass would find nothing; with the queue non-empty when state 4
        // begins, the base goes straight to loading these. A play-time mount
        // (MountContent, state 7) loads nothing, as on the 360.
        if (mState == kDiscoveryLoading) {
            QueuePackageFiles(static_cast<NativeContent *>(c));
        }
    }
    virtual void NotifyUnmounted(Content *c) {
        FOREACH (it, mCallbacks) {
            (*it)->ContentUnmounted(c->FileName().Str());
        }
    }
    virtual void NotifyFailed(Content *c) {
        MILO_LOG("DC3 Native: content '%s' failed to mount (%s)\n",
                 c->FileName().Str(),
                 static_cast<NativeContent *>(c)->HostDir());
        FOREACH (it, mCallbacks) {
            (*it)->ContentFailed(c->FileName().Str());
        }
    }

private:
    // DC3_DLC_DIR: one or more DLC roots (':'-separated), each holding one
    // directory per package. Returns how many packages need mounting.
    int DiscoverPackages() {
        const char *env = getenv("DC3_DLC_DIR");
        if (!env || !*env) return 0;
        int toMount = 0;
        std::string roots(env);
        size_t start = 0;
        while (start <= roots.size()) {
            size_t end = roots.find(':', start);
            if (end == std::string::npos) end = roots.size();
            std::string dlcRoot = roots.substr(start, end - start);
            start = end + 1;
            if (dlcRoot.empty()) continue;
            DIR *dir = opendir(dlcRoot.c_str());
            if (!dir) {
                MILO_LOG("DC3 Native: DC3_DLC_DIR entry '%s' is not a directory\n",
                         dlcRoot.c_str());
                continue;
            }
            std::vector<std::string> names;
            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_name[0] == '.') continue;
                std::string packDir = dlcRoot + "/" + entry->d_name;
                if (IsDirectory(packDir.c_str())) names.push_back(entry->d_name);
            }
            closedir(dir);
            // readdir order is filesystem order; the 360's enumeration order
            // is not knowable either, but a stable one keeps runs comparable.
            std::sort(names.begin(), names.end());
            for (size_t i = 0; i < names.size(); i++) {
                toMount += AddPackage(dlcRoot + "/" + names[i], names[i]);
            }
        }
        return toMount;
    }

    int AddPackage(const std::string &packDir, const std::string &name) {
        if (std::find(mIgnored.begin(), mIgnored.end(), String(name.c_str()))
            != mIgnored.end()) {
            return 0;
        }
        FOREACH (it, mContents) {
            if ((*it)->FileName() == Symbol(name.c_str())) {
                MILO_LOG("DC3 Native: DLC package '%s' found twice; using %s\n",
                         name.c_str(),
                         static_cast<NativeContent *>(*it)->HostDir());
                return 0;
            }
        }
        char hostDir[PATH_MAX];
        if (!realpath(packDir.c_str(), hostDir)) {
            MILO_LOG("DC3 Native: DLC package '%s' skipped: cannot resolve path\n",
                     packDir.c_str());
            return 0;
        }
        // ContentDiscovered returns true when the callback already has the
        // package (SongMgr: in the song cache); any false means it must be
        // mounted and read. Same fold as XboxContentMgr::PollRefresh.
        bool discovered = false;
        FOREACH (it, mCallbacks) {
            discovered = !(*it)->ContentDiscovered(Symbol(name.c_str())) || discovered;
        }
        NativeContent *content =
            new NativeContent(name.c_str(), mNextRoot++, hostDir, discovered);
        MILO_LOG("DC3 Native: DLC package '%s' at %s -> %s%s\n", name.c_str(),
                 content->Root(), hostDir, discovered ? "" : " (cached)");
        mContents.push_back(content);
        return discovered ? 1 : 0;
    }

    // For one mounted package, what ContentMgr::PollRefresh's state-4 pass
    // enumerates: `<root>/<ContentDir>/<ContentPattern>` and the first two
    // alt dirs (all of them when $extra_songs), per callback.
    void QueuePackageFiles(NativeContent *content) {
        FOREACH (cit, mCallbacks) {
            Callback *cb = *cit;
            const char *pattern = cb->ContentPattern();
            if (!pattern || !*pattern) continue;
            mCallback = cb;
            mLocation = content->Location();
            mName = content->FileName().Str();
            if (cb->ContentDir()) {
                QueuePackageDir(content, cb->ContentDir(), pattern);
            }
            if (cb->HasContentAltDirs()) {
                std::vector<String> *altDirs = cb->ContentAltDirs();
                static DataNode &n = DataVariable("extra_songs");
                int num = n.Int() ? altDirs->size() : 2;
                auto altDir = altDirs->begin();
                for (int i = 0; altDir != altDirs->end() && i < num; i++, altDir++) {
                    QueuePackageDir(content, altDir->c_str(), pattern);
                }
            }
        }
    }

    void QueuePackageDir(NativeContent *content, const char *subdir, const char *pattern) {
        std::string hostDir = content->HostDir();
        // What FileMakePath(Root(), subdir) gives the base: `cntNNNNNNNN:/songs`.
        std::string virtualDir = FileMakePath(content->Root(), subdir ? subdir : ".");
        if (subdir && *subdir && strcmp(subdir, ".") != 0) {
            hostDir += "/";
            hostDir += subdir;
        }
        DIR *dir = opendir(hostDir.c_str());
        if (!dir) return;
        std::vector<std::string> files;
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            std::string full = hostDir + "/" + entry->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
            if (FileMatch(entry->d_name, pattern)) files.push_back(entry->d_name);
        }
        closedir(dir);
        std::sort(files.begin(), files.end());
        for (size_t i = 0; i < files.size(); i++) {
            AddCallbackFile(virtualDir.c_str(), files[i].c_str());
        }
    }

    // The disc: see the header comment.
    void QueueRootCallbackFiles() {
        for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ++it) {
            Callback *cb = *it;
            const char *pattern = cb->ContentPattern();
            if (!pattern || !*pattern) {
                continue;
            }

            mCallback = cb;
            mLocation = kLocationRoot;
            mName = ".";

            QueueCallbackDir(cb->ContentDir(), pattern);
            if (cb->HasContentAltDirs()) {
                std::vector<String> *altDirs = cb->ContentAltDirs();
                if (altDirs) {
                    for (auto altIt = altDirs->begin(); altIt != altDirs->end(); ++altIt) {
                        QueueCallbackDir(altIt->c_str(), pattern);
                    }
                }
            }
        }
    }

    void QueueCallbackDir(const char *virtualDir, const char *pattern) {
        char extractedDir[256];
#ifdef __EMSCRIPTEN__
        // MEMFS: assets are directly under /data/, no "extracted/" prefix
        if (virtualDir && *virtualDir && strcmp(virtualDir, ".") != 0) {
            snprintf(extractedDir, sizeof(extractedDir), "%s", virtualDir);
        } else {
            snprintf(extractedDir, sizeof(extractedDir), ".");
        }
#else
        if (virtualDir && *virtualDir && strcmp(virtualDir, ".") != 0) {
            snprintf(extractedDir, sizeof(extractedDir), "extracted/%s", virtualDir);
        } else {
            snprintf(extractedDir, sizeof(extractedDir), "extracted");
        }
#endif

        char qualifiedDir[256];
        FileQualifiedFilename(qualifiedDir, sizeof(qualifiedDir), extractedDir);

        DIR *dir = opendir(qualifiedDir);
        if (!dir) {
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] == '.') {
                continue;
            }

            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", qualifiedDir, entry->d_name);

            struct stat st;
            if (stat(fullPath, &st) != 0 || S_ISDIR(st.st_mode)) {
                continue;
            }

            // Match just the filename against the pattern (e.g. "songs*.dta")
            if (FileMatch(entry->d_name, pattern)) {
                AddCallbackFile(
                    (virtualDir && *virtualDir && strcmp(virtualDir, ".") != 0) ? virtualDir
                                                                                 : ".",
                    entry->d_name
                );
            }
        }

        closedir(dir);
    }

    std::vector<String> mIgnored;
    int mNextRoot; // XboxContentMgr's unk934: the next cntNNNNNNNN
};

static NativeContentMgr gContentMgr;
ContentMgr &TheContentMgr = gContentMgr;
