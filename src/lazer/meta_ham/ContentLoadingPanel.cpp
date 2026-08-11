#include "meta_ham\ContentLoadingPanel.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os\ContentMgr.h"
#include "os\Debug.h"
#include "rndobj\Group.h"
#include "ui\PanelDir.h"
#include "ui\UIPanel.h"

ContentLoadingPanel::ContentLoadingPanel() : mAllowedToShow(false), mContentCount(0), mMountedCount(0) {
    TheContentMgr.RegisterCallback(this, false);
    mShowing = false;
}

ContentLoadingPanel::~ContentLoadingPanel() {
    TheContentMgr.UnregisterCallback(this, false);
}

void ContentLoadingPanel::AllowedToShow(bool b) {
    mAllowedToShow = b;
    if (b) {
        ShowIfPossible();
    }
}

void ContentLoadingPanel::ContentMountBegun(int i) {
    mContentCount = i;
    mMountedCount = 0;
    RndGroup *r = LoadedDir()->Find<RndGroup>("progress.grp", true);
    r->SetFrame(0, 1.0f);
    ShowIfPossible();
}

void ContentLoadingPanel::ContentFailed(const char *) {
    mMountedCount++;
    ShowIfPossible();
}

void ContentLoadingPanel::ShowIfPossible() {
    if (!mShowing) {
        if (mAllowedToShow && (bool)(mContentCount > 1)) {
            MILO_ASSERT(CheckIsLoaded(), 0x84);
            Enter();
            mShowing = true;
        }
    }
}

void ContentLoadingPanel::ContentDone() {
    mContentCount = 0;
    mMountedCount = 0;
    mAllowedToShow = false;
    if (mState == 1 && mShowing) {
        Exit();
        mShowing = false;
    }
}

void ContentLoadingPanel::Poll() {
    ShowIfPossible();
    if (mShowing) {
        UIPanel::Poll();
        RndGroup *progressGroup = LoadedDir()->Find<RndGroup>("progress.grp", true);
        f32 currentFrame = progressGroup->GetFrame();
        static const f32 kHundred = 100.0f;
        f32 target;
        if (mContentCount > 0) {
            target = ((f32)mMountedCount * 110.0f) / (f32)mContentCount;
        } else {
            target = kHundred;
        }
        if (target > kHundred) {
            target = kHundred;
        }
        f32 delta = TheTaskMgr.DeltaSeconds();
        if (delta < 0.0f) {
            delta = 0.0f;
        } else if (delta > 1.0f) {
            delta = 1.0f;
        }
        f32 newFrame = (target - currentFrame) * delta + currentFrame;
        if (mMountedCount == mContentCount) {
            newFrame = kHundred;
        }
        progressGroup->SetFrame(newFrame, 1.0f);
    }
}

void ContentLoadingPanel::ContentMounted(char const *c1, char const *c2) {
    mMountedCount++;
    ShowIfPossible();
}

BEGIN_HANDLERS(ContentLoadingPanel)
    HANDLE_ACTION(allowed_to_show, AllowedToShow(_msg->Int(2)))
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS
