// DC3 Native Port - ContentMgr with extracted DTA loading
// Replaces ContentMgr_Xbox.cpp - no DLC, but loads base-game content metadata.

#include "obj/Data.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/System.h"
#include <dirent.h>
#include <sys/stat.h>

// Native ContentMgr: keep the shared refresh lifecycle, but source callback files
// from `orig-assets/extracted/...` instead of Xbox content devices. This is enough
// for root song metadata and other static DTA-backed providers.
class NativeContentMgr : public ContentMgr {
public:
    NativeContentMgr() = default;
    virtual ~NativeContentMgr() = default;

    virtual void Init() {
        ContentMgr::Init();
    }

    virtual void Terminate() {}

    virtual void StartRefresh() {
        if (!mDirty) {
            return;
        }

        mDirty = false;
        mCallbackFiles.clear();
        RELEASE(mLoader);

        for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ++it) {
            (*it)->ContentStarted();
        }
        for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ++it) {
            (*it)->ContentAllMounted();
        }

        QueueCallbackFiles();
        if (mCallbackFiles.empty()) {
            mState = kDiscoveryEnumerating;
            for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ++it) {
                (*it)->ContentDone();
            }
            return;
        }

        // Reuse the shared loader dispatch in ContentMgr::PollRefresh(), but skip
        // Xbox discovery by starting directly in the callback-file phase.
        mState = kDiscoveryCheckIfDone;
    }

private:
    void QueueCallbackFiles() {
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

            // DLC directory: DC3_DLC_DIR env var points to additional content
            const char *dlcDir = getenv("DC3_DLC_DIR");
            if (dlcDir && dlcDir[0]) {
                mLocation = kLocationHDD;
                QueueDlcDir(dlcDir, cb->ContentDir(), pattern);
            }
        }
    }

    void QueueDlcDir(const char *dlcRoot, const char *contentSubdir, const char *pattern) {
        // Scan DLC root for subdirectories (each is a DLC pack)
        DIR *root = opendir(dlcRoot);
        if (!root) return;

        struct dirent *pack;
        while ((pack = readdir(root)) != nullptr) {
            if (pack->d_name[0] == '.') continue;

            char packDir[512];
            snprintf(packDir, sizeof(packDir), "%s/%s", dlcRoot, pack->d_name);

            struct stat st;
            if (stat(packDir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            // Look for content files matching the pattern inside each DLC pack
            char contentPath[512];
            if (contentSubdir && *contentSubdir && strcmp(contentSubdir, ".") != 0) {
                snprintf(contentPath, sizeof(contentPath), "%s/%s", packDir, contentSubdir);
            } else {
                snprintf(contentPath, sizeof(contentPath), "%s", packDir);
            }

            DIR *dir = opendir(contentPath);
            if (!dir) continue;

            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_name[0] == '.') continue;

                char fullPath[512];
                snprintf(fullPath, sizeof(fullPath), "%s/%s", contentPath, entry->d_name);

                struct stat fst;
                if (stat(fullPath, &fst) != 0 || S_ISDIR(fst.st_mode)) continue;

                if (FileMatch(entry->d_name, pattern)) {
                    mName = pack->d_name;
                    AddCallbackFile(
                        (contentSubdir && *contentSubdir && strcmp(contentSubdir, ".") != 0)
                            ? contentSubdir : ".",
                        entry->d_name
                    );
                }
            }
            closedir(dir);
        }
        closedir(root);
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
};

static NativeContentMgr gContentMgr;
ContentMgr &TheContentMgr = gContentMgr;
