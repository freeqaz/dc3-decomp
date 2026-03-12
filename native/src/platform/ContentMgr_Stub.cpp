// DC3 Native Port - ContentMgr with root song loading
// Replaces ContentMgr_Xbox.cpp - no DLC, but loads base game content from ark roots.

#include "obj/Data.h"
#include "os/ContentMgr.h"
#include "os/System.h"
#include <vector>

// Native ContentMgr: enumerate configured root content directories and feed them
// through the shared ContentMgr callback/loader pipeline. DLC/LIVE discovery stays
// disabled, but root content such as songs*.dta now loads through the same path the
// Xbox code expects.
class NativeContentMgr : public ContentMgr {
public:
    NativeContentMgr() = default;
    virtual ~NativeContentMgr() { ClearRoots(); }

    virtual void Init() {
        ContentMgr::Init();
        RebuildRoots();
    }

    virtual void Terminate() { ClearRoots(); }

    virtual void StartRefresh() {
        if (!mDirty) {
            return;
        }

        mDirty = false;
        mCallbackFiles.clear();
        RELEASE(mLoader);
        RebuildRoots();

        for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ++it) {
            (*it)->ContentStarted();
        }

        // Skip native DLC enumeration and mount bookkeeping, but enter the shared
        // "loading" phase so ContentMgr::PollRefresh() performs ContentAllMounted,
        // file discovery, loader dispatch, and final ContentDone callbacks.
        mState = kDiscoveryLoading;
    }

private:
    void ClearRoots() {
        for (RootContent *root : mOwnedRoots) {
            delete root;
        }
        mOwnedRoots.clear();
        mContents.clear();
        mRootLoaded = 0;
    }

    void RebuildRoots() {
        ClearRoots();

        DataArray *roots = SystemConfig("content_mgr", "roots");
        for (int i = 1; i < roots->Size(); ++i) {
            RootContent *root = new RootContent(roots->Str(i));
            mOwnedRoots.push_back(root);
            mContents.push_back(root);
            ++mRootLoaded;
        }

        for (auto it = mExtraContents.begin(); it != mExtraContents.end(); ++it) {
            RootContent *root = new RootContent(it->c_str());
            mOwnedRoots.push_back(root);
            mContents.push_back(root);
            ++mRootLoaded;
        }
    }

    std::vector<RootContent *> mOwnedRoots;
};

static NativeContentMgr gContentMgr;
ContentMgr &TheContentMgr = gContentMgr;
