#include "meta_ham/LetterboxPanel.h"
#include "LetterboxPanel.h"
#include "flow/Flow.h"
#include "flow/FlowNode.h"
#include "math/Easing.h"
#include "meta_ham/HamPanel.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Group.h"
#include "rndobj/PropAnim.h"
#include "rndobj/Tex.h"
#include "rndobj/Text.h"
#include "ui/PanelDir.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"
#include "utl/Symbol.h"

#ifdef HX_NATIVE
LetterboxPanel *LetterboxPanel::sInstance;
#endif

LetterboxPanel::LetterboxPanel()
    : mSyncedPanel(0), mLetterboxGroup(0), mIsBlacklightMode(false), mBlacklightActive(false), mBlacklightPhase(0), mBlacklightTimeout(13000) {
    sInstance = this;
}

LetterboxPanel::~LetterboxPanel() { sInstance = nullptr; }

BEGIN_HANDLERS(LetterboxPanel)
    HANDLE_ACTION(resync, SyncToPanel(mSyncedPanel))
    HANDLE_ACTION(sync_to_panel, SyncToPanel(_msg->Obj<UIPanel>(2)))
    HANDLE_SUPERCLASS(HamPanel)
END_HANDLERS

BEGIN_PROPSYNCS(LetterboxPanel)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void LetterboxPanel::Draw() {
    if (mLetterboxGroup) {
        mLetterboxGroup->SetShowing(!ShouldHideLetterbox());
    }
    UIPanel::Draw();
}

void LetterboxPanel::Enter() {
    mLetterboxGroup = DataDir()->Find<RndGroup>("letterbox_main.grp");
    static Symbol blacklight_timeout("blacklight_timeout");
    const DataNode *property = Property(blacklight_timeout, false);
    if (property) {
        mBlacklightTimeout = property->Int();
    }
    HamPanel::Enter();
}

void LetterboxPanel::Poll() {
    HamPanel::Poll();
    if (!ShouldHideLetterbox() && mIsBlacklightMode && mBlacklightPhase != 0) {
        if (mBlacklightPhase == 1) {
            mBlacklightTimer.Restart();
            mBlacklightPhase = 2;
            mBlacklightTimeoutMs = mBlacklightTimeout;
        } else {
            if (mBlacklightTimer.SplitMs() >= mBlacklightTimeoutMs) {
                mBlacklightTimer.Restart();
                if (mBlacklightPhase != 2) {
                    if (mBlacklightPhase == 6)
                        mBlacklightPhase = 0;
                } else {
                    mBlacklightPhase = 6;
                    mBlacklightTimeoutMs = 100.0f;
                    SetBlacklightMode(false);
                }
            }
        }
    }
}

void LetterboxPanel::Unload() { UIPanel::Unload(); }

bool LetterboxPanel::IsBlacklightMode() { return mIsBlacklightMode; }

bool LetterboxPanel::IsLeavingBlacklightMode() {
    Flow *f = DataDir()->Find<Flow>("deactivate_letterbox.flow", false);
    return f && f->IsRunning();
}

bool LetterboxPanel::IsEnteringBlacklightMode() {
    Flow *f = DataDir()->Find<Flow>("activate_letterbox.flow", false);
    return f && f->IsRunning();
}

bool LetterboxPanel::InBlacklightTransition() {
    return IsEnteringBlacklightMode() || IsLeavingBlacklightMode();
}

void LetterboxPanel::VoiceInput(int i, bool b) {
    if (mIsBlacklightMode && b) {
        Flow *f;
        if (i == 0) {
            f = DataDir()->Find<Flow>("voice_command_right.flow", false);
            if (f) {
                f->Activate();
            }
        } else {
            f = DataDir()->Find<Flow>("voice_command_left.flow", false);
            if (f) {
                f->Activate();
            }
        }
    }
}

void LetterboxPanel::EnterBlacklightMode() {
    mIsBlacklightMode = true;
    Flow *f = DataDir()->Find<Flow>("activate_letterbox.flow", false);
    if (f)
        f->Activate();
    if (mBlacklightActive != true) // wont let me do !mBlacklightActive
        mBlacklightActive = true;
    RndText::SetBlacklightModeEnabled(true);
    static Message enter_blacklight_mode("enter_blacklight_mode");
    Handle(enter_blacklight_mode, false);
}

void LetterboxPanel::ExitBlacklightMode(bool b) {
    mIsBlacklightMode = false;
    Flow *f;
    if (!b) {
        f = DataDir()->Find<Flow>("deactivate_letterbox.flow", false);
    } else {
        f = DataDir()->Find<Flow>("deactivate_letterbox_immediate.flow", false);
    }
    if (f)
        f->Activate();
    if (mBlacklightActive != false)
        mBlacklightActive = false;
    RndText::SetBlacklightModeEnabled(false);
    DataNode handle;
    if (b) {
        static Message cExitBlacklightModeImmediateMsg("exit_blacklight_mode", 1);
        handle = Handle(cExitBlacklightModeImmediateMsg, false);
    } else {
        static Message cExitBlacklightModeMsg("exit_blacklight_mode", 0);
        handle = Handle(cExitBlacklightModeMsg, false);
    }
}

bool LetterboxPanel::ShouldHideLetterbox() const {
    static Symbol hide_letterbox("hide_letterbox");
    if (mSyncedPanel) {
        const DataNode *prop = mSyncedPanel->Property(hide_letterbox, false);
        if (prop && prop->Int()) {
            return true;
        }
    }
    return false;
}

bool LetterboxPanel::ShouldShowHandHelp() const {
    static Symbol show_letterbox_hand_help("show_letterbox_hand_help");
    if (mSyncedPanel) {
        const DataNode *prop = mSyncedPanel->Property(show_letterbox_hand_help, false);
        if (prop && prop->Int()) {
            return true;
        }
    }
    return false;
}

void LetterboxPanel::ToggleBlacklightMode(bool toggle) {
    if (toggle) {
        SetBlacklightModeImmediately(mIsBlacklightMode == false);
        return;
    }
    SetBlacklightMode(mIsBlacklightMode == false);
}

void LetterboxPanel::SetBlacklightMode(bool b) {
    if (b != mIsBlacklightMode) {
        if (!ShouldHideLetterbox() || !b) {
            if (!InBlacklightTransition()) {
                HandleType(Message("on_toggle_blacklight", b));
                mIsBlacklightMode = b;
                if (b) {
                    EnterBlacklightMode();
                    mBlacklightPhase = 1;
                    mBlacklightTimer.Restart();
                    int temp = mBlacklightTimeout;
                    mBlacklightTimeoutMs = temp;
                } else
                    ExitBlacklightMode(false);
            }
        }
    }
    if (mBlacklightPhase == 2) {
        mBlacklightTimer.Restart();
        mBlacklightTimeoutMs = mBlacklightTimeout;
    }
}

void LetterboxPanel::SetBlacklightModeImmediately(bool b) {
    if (b != mIsBlacklightMode) {
        if (!ShouldHideLetterbox() || !b) {
            HandleType(Message("on_toggle_blacklight", b));
            mIsBlacklightMode = b;
            if (b) {
                EnterBlacklightMode();
                mBlacklightPhase = 1;
                mBlacklightTimer.Restart();
                int temp = mBlacklightTimeout;
                mBlacklightTimeoutMs = temp;
            } else
                ExitBlacklightMode(true);
        }
    }
    if (mBlacklightPhase == 2) {
        mBlacklightTimer.Restart();
        mBlacklightTimeoutMs = mBlacklightTimeout;
    }
}

void LetterboxPanel::SyncToPanel(UIPanel *panel) {
    mSyncedPanel = panel;
    if (mLetterboxGroup) {
        mLetterboxGroup->SetShowing(!ShouldHideLetterbox());
        if (ShouldShowHandHelp()) {
            RndPropAnim *anim = LoadedDir()->Find<RndPropAnim>("handDown_icon_show.anim");
            if (anim) {
                float cur = anim->GetFrame();
                float end = anim->EndFrame();
                anim->Animate(
                    cur, end, kTaskUISeconds, 0, 0, nullptr, kEaseLinear, 0, false
                );
            }
        } else {
            RndPropAnim *anim = LoadedDir()->Find<RndPropAnim>("handDown_icon_show.anim");
            if (anim) {
                anim->Animate(
                    anim->GetFrame(), 0, kTaskUISeconds, 0, 0, nullptr, kEaseLinear, 0, false
                );
            }
        }
    }
    static Symbol letterbox_commands("letterbox_commands");
    DataArray *cfg = SystemConfig(letterbox_commands);
    DataArray *arr = cfg->FindArray(TheUI->CurrentScreen()->Name(), false);
    if (!arr) {
        static Symbol missing_data("missing_data");
        arr = cfg->FindArray(missing_data);
    }
    static Message tickerCommandsMsg("set_ticker_commands", 0);
    tickerCommandsMsg[0] = arr;
    HandleType(tickerCommandsMsg);
}
