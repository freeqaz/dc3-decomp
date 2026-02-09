#include "meta_ham/ContentLoadingPanel.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "rndobj/Group.h"
#include "ui/PanelDir.h"
#include "ui/UIPanel.h"

ContentLoadingPanel::ContentLoadingPanel() : unk3c(false), unk40(0), unk44(0) {
    TheContentMgr.RegisterCallback(this, false);
    mShowing = false;
}

ContentLoadingPanel::~ContentLoadingPanel() {
    TheContentMgr.UnregisterCallback(this, false);
}

void ContentLoadingPanel::AllowedToShow(bool b) {
    unk3c = b;
    if (b) {
        ShowIfPossible();
    }
}

void ContentLoadingPanel::ContentMountBegun(int i) {
    unk40 = i;
    unk44 = 0;
    RndGroup *r = LoadedDir()->Find<RndGroup>("progress.grp", true);
    r->SetFrame(0, 1.0f);
    ShowIfPossible();
}

void ContentLoadingPanel::ShowIfPossible() {
    if (!mShowing) {
        if (unk3c && unk40 != 1) { // theres an extra check here
            MILO_ASSERT(CheckIsLoaded(), 0x84);
            Enter();
            mShowing = true;
        }
    }
}

void ContentLoadingPanel::ContentDone() {
    unk40 = 0;
    unk44 = 0;
    unk3c = false;
    if (mState == 1 && mShowing) {
        Exit();
        mShowing = false;
    }
}

void ContentLoadingPanel::Poll() {
    ShowIfPossible();
    if (mShowing) {
        UIPanel::Poll();
        RndGroup *progressGroup = ObjectDir::Main()->Find<RndGroup>("progress.grp", true);
        if (unk40 > 0 && progressGroup) {
            // Animate progress bar smoothly toward target percentage
            f32 currentFrame = progressGroup->GetFrame();
            int total = unk44;
            int current = unk40;

            // Calculate target frame (110% of progress, capped at 100)
            f32 target = ((f32)total * 110.0f) / (f32)current;
            if (target > 100.0f) {
                target = 100.0f;
            }

            // Clamp delta time to [0, 1] range
            f32 delta = TheTaskMgr.DeltaSeconds();
            if (delta < 0.0f) {
                delta = 0.0f;
            } else if (delta > 1.0f) {
                delta = 1.0f;
            }

            // Interpolate toward target, snap to 100% when fully loaded
            f32 newFrame = (target - currentFrame) * delta + currentFrame;
            if (current == total) {
                newFrame = 100.0f;
            }
            progressGroup->SetFrame(newFrame, 1.0f);
        }
    }
}

BEGIN_HANDLERS(ContentLoadingPanel)
    HANDLE_ACTION(allowed_to_show, AllowedToShow(_msg->Int(2)))
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS
