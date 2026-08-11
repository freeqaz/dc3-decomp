#include "ShellInput.h"
#include "OverlayPanel.h"
#include "flow\PropertyEventProvider.h"
#include "game\GameMode.h"
#include "game\GamePanel.h"
#include "gesture\GestureMgr.h"
#include "gesture\HandInvokeGestureFilter.h"
#include "gesture\HandsUpGestureFilter.h"
#include "gesture\SkeletonExtentTracker.h"
#include "gesture\SkeletonUpdate.h"
#include "gesture\SpeechMgr.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamNavList.h"
#include "hamobj\HamPlayerData.h"
#include "meta_ham\DepthBuffer.h"
#include "meta_ham\HamUI.h"
#include "meta_ham\HelpBarPanel.h"
#include "meta_ham\LetterboxPanel.h"
#include "meta_ham\OverlayPanel.h"
#include "meta_ham\ProfileMgr.h"
#include "meta_ham\UIEventMgr.h"
#include "net_ham\RockCentral.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj\MessageTimer.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os\Debug.h"
#include "os\Joypad.h"
#include "os\JoypadMsgs.h"
#include "rndobj\Anim.h"
#include "synth\Synth.h"
#include "ui\PanelDir.h"
#include "ui\UI.h"
#include "ui\UIPanel.h"
#include "utl\Symbol.h"

ShellInput::ShellInput()
    : mVoiceControlEnabled(0), unk_0x34(this), unk_0x48(0, 15, 0), unk_0x9C(0.2),
      unk_0xA0(0.25), unk_0xA4(0), mWrongHandPosAnim(this), mInputPanel(0),
      mCursorPanel(nullptr), unk_0xC4(0), mDepthBuffer(0), mSkelIdentifier(0),
      mSkelChooser(0), mSkelExtTracker(0) {
    unk_0x31 = 0;
    unk_0x32 = 0;
}

ShellInput::~ShellInput() {
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    handle.RemoveCallback(this);
    delete mDepthBuffer;
    delete mSkelIdentifier;
    delete mSkelChooser;
    delete mSkelExtTracker;
}

BEGIN_HANDLERS(ShellInput)
    HANDLE_ACTION_IF(
        panel_navigated, !TheGestureMgr->InControllerMode(), EnterControllerMode(false)
    )
    HANDLE_EXPR(has_skeleton, HasSkeleton())
    HANDLE_EXPR(num_tracked_skeletons, NumTrackedSkeletons())
    HANDLE_ACTION(
        enter_controller_mode,
        EnterControllerMode(_msg->Size() >= 3 ? _msg->Int(2) : false)
    )
    HANDLE_ACTION(exit_controller_mode, ExitControllerMode(true))
    HANDLE_EXPR(in_controller_mode, TheGestureMgr->InControllerMode())
    HANDLE_ACTION(
        set_last_select_in_controller_mode,
        HamNavList::sLastSelectInControllerMode = _msg->Int(2)
    )
    HANDLE_EXPR(voice_control_enabled, mVoiceControlEnabled)
    HANDLE_MESSAGE(ButtonDownMsg)
    HANDLE_MESSAGE(JoypadConnectionMsg)
    HANDLE_MESSAGE(SpeechRecoMessage)
    HANDLE_MESSAGE(SpeechEnableMsg)
    HANDLE_MESSAGE(LeftHandListEngagementMsg)
    HANDLE_MESSAGE(ResetControllerModeTimeoutMsg)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void ShellInput::PostUpdate(const SkeletonUpdateData *updata) {
    if (updata) {
        Skeleton *skeleton = TheGestureMgr->GetActiveSkeleton();
        if (skeleton) {
            mHandInvokeGestureFilter->Update(*skeleton, skeleton->ElapsedMs());
            mHandsUpGestureFilter->Update(*skeleton, skeleton->ElapsedMs());
        }
    }
}

void ShellInput::Init() {
    SetName("shell_input", ObjectDir::Main());
#ifdef HX_NATIVE
    // On native, skip Xbox-specific Kinect init (SkeletonUpdate thread, DepthBuffer,
    // SkeletonIdentifier, speech) but create SkeletonChooser so player assignment
    // logic works. Without it, GetSkeletonChooser() returns null and functions like
    // GetPlayerIndex/UpdateNavLists bail out with fallback values.
    mCursorPanel = ObjectDir::Main()->Find<UIPanel>("cursor_panel");
    if (mCursorPanel && mCursorPanel->CheckIsLoaded() && mCursorPanel->LoadedDir()) {
        mCursorPanel->Enter();
    }
    mSkelChooser = new SkeletonChooser;
    static Symbol reset_controller_mode_timeout("reset_controller_mode_timeout");
    TheHamUI.AddSink(this, reset_controller_mode_timeout);
    // Primary boot hook: runs after HamInit() (TheGestureMgr exists, in controller
    // mode) and after UIManager::Init() (helpbar dir loaded), so this normally wins
    // the one-shot and activates controller_mode.flow before the first screen shows.
    NativeBootControllerModeOnce();
#else
    SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
    handle.AddCallback(this);
    mCursorPanel = ObjectDir::Main()->Find<UIPanel>("cursor_panel");
    MILO_ASSERT(mCursorPanel->CheckIsLoaded(), 95);
    MILO_ASSERT(mCursorPanel->LoadedDir(), 96);
    mDepthBuffer = new DepthBuffer();
    mDepthBuffer->Init(mCursorPanel);
    mWrongHandPosAnim =
        mCursorPanel->DataDir()->Find<RndAnimatable>("wrong_hand_position.anim", true);
    MILO_ASSERT(TheGameData, 102);
    mSkelIdentifier = new SkeletonIdentifier;
    mSkelIdentifier->Init();
    mSkelChooser = new SkeletonChooser;
    mHandInvokeGestureFilter = new HandInvokeGestureFilter;
    mHandsUpGestureFilter = Hmx::Object::New<HandsUpGestureFilter>();
    mHandsUpGestureFilter->SetRequiredMs(1200);
    mCursorPanel->Enter();
    TheSpeechMgr->AddSink(TheUI);
    mSkelExtTracker = new SkeletonExtentTracker;

    static Symbol reset_controller_mode_timeout("reset_controller_mode_timeout");
    TheHamUI.AddSink(this, reset_controller_mode_timeout);
#endif
}

void ShellInput::Draw() { mCursorPanel->Draw(); }

void ShellInput::Poll() {
#ifdef HX_NATIVE
    // Native poll: skip gesture filters (HandInvoke, HandsUp) and Kinect-specific
    // subsystems (DepthBuffer, SkeletonIdentifier, SkeletonExtentTracker) that
    // aren't initialized. But poll the SkeletonChooser and cursor panel.

    // No hands-up gesture filter on native — keep NavList disengage off
    HamNavList::sForceDisengage = false;

    if (mCursorPanel)
        mCursorPanel->Poll();

    // Poll SkeletonChooser when there are tracked skeletons (native pose server).
    // With default properties now set in HamInit, SkeletonChooser code paths
    // that read ui_nav_mode etc. are safe.
    if (mSkelChooser && NumTrackedSkeletons() > 0)
        mSkelChooser->Poll();

    // Track skeleton presence changes — drives "has_skeleton" property on
    // hamprovider which UI scripts use for Kinect-vs-controller mode switching.
    {
        static bool lastHasSkeleton = false;
        bool hasSkel = HasSkeleton();
        if (hasSkel != lastHasSkeleton) {
            static Symbol has_skeleton_sym("has_skeleton");
            static Message updateSkeletonStatus("update_skeleton_status");
            Handle(updateSkeletonStatus, false);
            TheHamProvider->SetProperty(has_skeleton_sym, hasSkel);
        }
        lastHasSkeleton = hasSkel;
    }

    if (TheGestureMgr->InControllerMode() && unk_0x68.SplitMs() >= unk_0x98) {
        ExitControllerMode(true);
    }
    if (mHandsUpGestureFilter && mHandsUpGestureFilter->GetHandsUp()
        && TheHamUI.EventDialogPanel()
        && TheHamUI.EventDialogPanel()->GetState() != UIPanel::kUp
        && !TheHamUI.InTransition()) {
        static Symbol ui_nav_mode("ui_nav_mode");
        static Symbol movie("movie");
        const DataNode *pNavModeNode = TheHamProvider->Property(ui_nav_mode);
        MILO_ASSERT(pNavModeNode, 0xd2);
        Symbol navmodeSym = pNavModeNode->Sym();
        if (TheGestureMgr->IDEnabled() && navmodeSym != movie) {
            mHandsUpGestureFilter->Clear();
            if (TheHamUI.GetOverlayPanel()) {
                TheHamUI.GetOverlayPanel()->Dismiss();
            }
            OverlayPanel *pCorrectPanel =
                ObjectDir::Main()->Find<OverlayPanel>("correct_identity_panel");
            MILO_ASSERT(pCorrectPanel->CheckIsLoaded(), 0xdc);
            MILO_ASSERT(pCorrectPanel->LoadedDir(), 0xdd);
            TheHamUI.SetOverlayPanel(pCorrectPanel);
        }
    }

    OverlayPanel *panel = TheHamUI.GetOverlayPanel();
    if (panel) {
        mHandsUpGestureFilter->Clear();
        if (TheHamUI.GetTransitionState() != 0) {
            panel->Dismiss();
        }
    }
    bool raised = mHandsUpGestureFilter && mHandsUpGestureFilter->GetRaisedMs() > 0;
    HamNavList::sForceDisengage = raised != false;
    // Headless native builds don't initialize Kinect: cursor/depth/skeleton
    // subsystems' Poll() bodies call into DataNode lookups that crash without
    // Kinect state. Skip them entirely under HX_NATIVE so we can run automated
    // gameplay tests. (If/when Kinect is available, gate this on a runtime flag.)
#ifndef HX_NATIVE
    if (mCursorPanel) mCursorPanel->Poll();
    if (mDepthBuffer) mDepthBuffer->Poll();
    if (mSkelIdentifier) mSkelIdentifier->Poll();
    if (mSkelChooser) mSkelChooser->Poll();
    if (mSkelExtTracker) mSkelExtTracker->Poll();
#endif

    static bool sHasSkeleton = false;
    bool hasSkel = HasSkeleton();
    if (hasSkel != sHasSkeleton) {
        static Symbol has_skeleton("has_skeleton");
        static Message updateSkeletonStatus("update_skeleton_status");
        Handle(updateSkeletonStatus, false);
        TheHamProvider->SetProperty(has_skeleton, hasSkel);
    }
    sHasSkeleton = hasSkel;

    if (TheUI->InTransition()) {
        SetCursorAlpha(0);
    }
#else // !HX_NATIVE
    static Symbol is_in_shell_pause("is_in_shell_pause");
    static Symbol is_in_party_mode("is_in_party_mode");
    static Symbol is_in_infinite_party_mode("is_in_infinite_party_mode");
    static bool sPracticeOptionsInvokedPartyMode = false;
    static bool sPracticeOptionsInvoked = false;
    static bool sHasSkeleton = false;

    if (TheUI->FocusPanel() == TheGamePanel) {
        if (mWrongHandPosAnim->GetFrame() > 0.0f) {
            mWrongHandPosAnim->SetFrame(0.0f, 1.0f);
            unk_0xA4 = false;
        }

        if (TheGestureMgr->InControllerMode() && !TheUIEventMgr->HasActiveDialogEvent()) {
            ExitControllerMode(true);
        }
        if (!TheHamUI.InTransition()) {
            TheGestureMgr->SetIdentificationEnabled(false);
        }
        static Symbol practice("practice");
        static Symbol gameplay_mode("gameplay_mode");
        static Symbol suppress_practice_options("suppress_practice_options");
        if (TheGameMode->Property(gameplay_mode)->Sym() == practice) {
            if (mHandInvokeGestureFilter->GetInvokeDetected() && !sPracticeOptionsInvoked) {
                if (TheHamProvider->Property(suppress_practice_options)->Int() == 0) {
                    TheHamProvider->Export(Message("invoke_practice_options"), true);
                    sPracticeOptionsInvoked = true;
                }
            }
            if (!mHandInvokeGestureFilter->GetInvokeDetected()
                || TheHamProvider->Property(suppress_practice_options)->Int()) {
                if (sPracticeOptionsInvoked) {
                    TheHamProvider->Export(Message("deinvoke_practice_options"), true);
                    sPracticeOptionsInvoked = false;
                }
            }
        }
    } else if (TheHamProvider->Property(is_in_infinite_party_mode)->Int()
               || TheHamProvider->Property(is_in_party_mode)->Int()) {
        TheGestureMgr->SetIdentificationEnabled(false);
        if (TheHamProvider->Property(is_in_shell_pause)->Int() == 0) {
            if (mHandInvokeGestureFilter->GetInvokeDetected()
                && !sPracticeOptionsInvokedPartyMode) {
                TheHamProvider->Export(Message("invoke_practice_options"), true);
                sPracticeOptionsInvokedPartyMode = true;
            }
            if (!mHandInvokeGestureFilter->GetInvokeDetected()
                && sPracticeOptionsInvokedPartyMode) {
                TheHamProvider->Export(Message("deinvoke_practice_options"), true);
                sPracticeOptionsInvokedPartyMode = false;
            }
        }
    }

    if (TheGestureMgr->InControllerMode() && unk_0x68.SplitMs() >= unk_0x98) {
        ExitControllerMode(true);
    }
    if (mHandsUpGestureFilter->GetHandsUp()
        && TheHamUI.EventDialogPanel()
        && TheHamUI.EventDialogPanel()->GetState() != UIPanel::kUp
        && !TheHamUI.InTransition()) {
        static Symbol ui_nav_mode("ui_nav_mode");
        static Symbol movie("movie");
        const DataNode *pNavModeNode = TheHamProvider->Property(ui_nav_mode);
        MILO_ASSERT(pNavModeNode, 0xd2);
        Symbol navmodeSym = pNavModeNode->Sym();
        if (TheGestureMgr->IDEnabled() && navmodeSym != movie) {
            mHandsUpGestureFilter->Clear();
            if (TheHamUI.GetOverlayPanel()) {
                TheHamUI.GetOverlayPanel()->Dismiss();
            }
            OverlayPanel *pCorrectPanel =
                ObjectDir::Main()->Find<OverlayPanel>("correct_identity_panel");
            MILO_ASSERT(pCorrectPanel->CheckIsLoaded(), 0xdc);
            MILO_ASSERT(pCorrectPanel->LoadedDir(), 0xdd);
            TheHamUI.SetOverlayPanel(pCorrectPanel);
        }
    }

    OverlayPanel *panel = TheHamUI.GetOverlayPanel();
    if (panel) {
        mHandsUpGestureFilter->Clear();
        if (TheHamUI.GetTransitionState() != 0) {
            panel->Dismiss();
        }
    }
    bool raised = mHandsUpGestureFilter->GetRaisedMs() > 0.0f;
    HamNavList::sForceDisengage = raised != false;
    mCursorPanel->Poll();
    mDepthBuffer->Poll();
    mSkelIdentifier->Poll();
    mSkelChooser->Poll();
    mSkelExtTracker->Poll();

    bool hasSkel = HasSkeleton();
    if (hasSkel != sHasSkeleton) {
        static Symbol has_skeleton("has_skeleton");
        static Message updateSkeletonStatus("update_skeleton_status");
        Handle(updateSkeletonStatus, false);
        TheHamProvider->SetProperty(has_skeleton, hasSkel);
    }
    sHasSkeleton = hasSkel;

    if (TheUI->InTransition()) {
        SetCursorAlpha(0);
    }
#endif // HX_NATIVE
}

void ShellInput::UpdateInputPanel(UIPanel *panel) { mInputPanel = panel; }

bool ShellInput::IsGameplayPanel() const {
    static Symbol is_gameplay_panel("is_gameplay_panel");
    if (TheUI->FocusPanel() != nullptr) {
        const DataNode *gamepanel =
            TheUI->FocusPanel()->Property(is_gameplay_panel, false);
        if (gamepanel != nullptr && gamepanel->Int() == 1)
            return true;
    }
    return false;
}

bool ShellInput::HasSkeleton() const {
    Skeleton *skel = TheGestureMgr->GetActiveSkeleton();
    return skel != nullptr && skel->IsValid();
}

int ShellInput::NumTrackedSkeletons() const {
    int count = 0;
    for (int i = 0; i < 6; i++) {
        if (TheGestureMgr->GetSkeleton(i).IsTracked())
            count++;
    }
    return count;
}

int ShellInput::CycleDrawCursor() {
    unk_0xC4 = !unk_0xC4;
    return unk_0xC4;
}

void ShellInput::SyncVoiceControl() { // almost done
#ifdef HX_NATIVE
    // Speech/Kinect voice control is unavailable on native. We still need to
    // drive the "hide" side of the shell/helpbar state so the voice-tip
    // overlay does not stay latched on and cover the menu.
    if (!TheSpeechMgr) {
        mVoiceControlEnabled = false;
        static Symbol hide_microphone_icon("hide_microphone_icon");
        static Message hide_microphone_msg(hide_microphone_icon);
        TheHamProvider->Handle(hide_microphone_msg, false);
        static Symbol voice_commander_help_hide("voice_commander_help_hide");
        static Message voice_commander_help_hide_msg(voice_commander_help_hide);
        TheHamProvider->Handle(voice_commander_help_hide_msg, false);
        static Symbol voice_commander_tip_temporary("voice_commander_tip_temporary");
        TheHamProvider->SetProperty(voice_commander_tip_temporary, false);
        return;
    }
#endif
    static Symbol allow_voice_control("allow_voice_control");
    const DataNode *prop;
    if (mInputPanel) {
        prop = mInputPanel->Property(allow_voice_control, false);
    } else {
        prop = nullptr;
    }
    if (!(!mInputPanel || !prop || 1 != prop->Int() || TheProfileMgr.DisableVoice()
        || TheUIEventMgr->HasActiveDialogEvent() || !TheSpeechMgr->SpeechSupported())) {
        TheSpeechMgr->SetRecognizing(true);
        mVoiceControlEnabled = true;

        static Symbol show_microphone_icon("show_microphone_icon");
        static Message show_microphone_msg(show_microphone_icon);
        TheHamProvider->Handle(show_microphone_msg, false);
        if (TheProfileMgr.GetShowVoiceTip()) {
            static Symbol voice_commander_help("voice_commander_help");
            static Message voice_commander_help_msg(voice_commander_help);
            TheHamProvider->Handle(voice_commander_help_msg, false);
        }
        static Symbol voice_commander_tip_temporary("voice_commander_tip_temporary");
        const DataNode *voiceProp =
            mInputPanel->Property(voice_commander_tip_temporary, false);
        if (!TheProfileMgr.GetShowVoiceTip() || (voiceProp && voiceProp->Int() == 1)) {
            TheHamProvider->SetProperty(voice_commander_tip_temporary, true);
        } else {
            TheHamProvider->SetProperty(voice_commander_tip_temporary, false);
        }
    } else {
        TheSpeechMgr->SetRecognizing(false);
        mVoiceControlEnabled = false;
        static Symbol hide_microphone_icon("hide_microphone_icon");
        static Message hide_microphone_msg(hide_microphone_icon);
        TheHamProvider->Handle(hide_microphone_msg, false);
        static Symbol voice_commander_help_hide("voice_commander_help_hide");
        static Message voice_commander_help_hide_msg(voice_commander_help_hide);
        TheHamProvider->Handle(voice_commander_help_hide_msg, false);
    }
}

void ShellInput::EnterControllerMode(bool b) {
#ifdef HX_NATIVE
    // Native stays permanently in controller mode — skip the RockCentral/profile/
    // skeleton-chooser Xbox body. Route helpbar controller_mode.flow activation
    // through the shared one-shot guard so the boot hook, HamScreen::Enter's
    // sControllerModeForced path, and repeat enter_controller_mode messages all
    // share the single sActivated latch — the flow activates exactly once.
    TheGestureMgr->SetInControllerMode(true);
    NativeBootControllerModeOnce();
    return;
#endif
    HelpBarPanel *pHelpbarPanel = TheHamUI.GetHelpBarPanel();
    MILO_ASSERT(pHelpbarPanel, 0x230);
    if (pHelpbarPanel->AllowController() || b) {
        pHelpbarPanel->EnterControllerMode();
        TheGestureMgr->SetInControllerMode(true);
        static Message controller_mode_entered("controller_mode_entered");
        TheUI->Handle(controller_mode_entered, false);
        unk_0xA4 = false;
        static Symbol in_controller_mode("in_controller_mode");
        TheHamProvider->SetProperty(in_controller_mode, true);
        TheRockCentral.SetControllerModeEnterCount(TheRockCentral.GetControllerModeEnterCount() + 1);
        unk_0x68.Restart();
        int hamUIPadNum = TheHamUI.GetPadNum();
        if (!TheProfileMgr.CriticalProfile()) {
            for (int i = 0; i < 2; i++) {
                HamPlayerData *pPlayer = TheGameData->Player(i);
                MILO_ASSERT(pPlayer, 0x251);
                if (pPlayer->PadNum() == hamUIPadNum) {
                    mSkelChooser->SetActivePlayer(i);
                    return;
                }
            }
        }
    } else {
        TheSynth->RunFlow("invalid_select.flow");
    }
}

void ShellInput::ExitControllerMode(bool b) {
#ifdef HX_NATIVE
    // Native: no Kinect — never exit controller mode.
    // DTA scripts fire exit_controller_mode during screen transitions,
    // but without gesture input there's no way to re-enter.
    return;
#endif
    if (TheHamUI.GetHelpBarPanel())
        TheHamUI.GetHelpBarPanel()->ExitControllerMode(b);
    TheGestureMgr->SetInControllerMode(false);
    static Message controllerModeExited("controller_mode_exited");
    TheUI->Handle(controllerModeExited, false);
    static Symbol in_controller_mode("in_controller_mode");
    TheHamProvider->SetProperty(in_controller_mode, 0);
    TheRockCentral.SetControllerModeExitCount(TheRockCentral.GetControllerModeExitCount() + 1);
}

void ShellInput::DrawDebug() {
    if (mInputPanel) {
        HamNavList *list =
            mInputPanel->DataDir()->Find<HamNavList>("right_hand.hnl", false);
        if (list) {
            list->DrawDebug();
        }
    }
#ifdef HX_NATIVE
    // mSkelIdentifier is null on native (Kinect-only subsystem)
    if (mSkelChooser)
        mSkelChooser->DrawDebug();
#else
    mSkelIdentifier->DrawDebug();
    mSkelChooser->DrawDebug();
#endif
}

void ShellInput::SetCursorAlpha(float f1) const {
    if (TheHamUI.GetHelpBarPanel()) {
        ObjectDir *hbpDataDir = TheHamUI.GetHelpBarPanel()->DataDir();
        PanelDir *dir = dynamic_cast<PanelDir *>(hbpDataDir);
        if (dir) {
            RndAnimatable *anim =
                dir->DataDir()->Find<RndAnimatable>("cursor_alpha.anim");
            float frame = anim->GetFrame();
            frame = TheTaskMgr.DeltaUISeconds() * (f1 - frame) * 10.0f + frame;
            anim->SetFrame(frame, 1);
        }
    }
}

void ShellInput::SyncToCurrentScreen() {
    if (TheUIEventMgr->HasActiveDialogEvent()
        && TheHamUI.EventDialogPanel()->GetState() == UIPanel::kUp) {
        mInputPanel = TheHamUI.EventDialogPanel();
    } else if (TheHamUI.GetOverlayPanel()) {
        mInputPanel = TheHamUI.GetOverlayPanel();
    } else {
        mInputPanel = TheHamUI.FocusPanel();
    }
    TheGestureMgr->SetInShellMode(!IsGameplayPanel());
#ifdef HX_NATIVE
    if (TheHamUI.GetHelpBarPanel())
#endif
    TheHamUI.GetHelpBarPanel()->SyncToPanel(mInputPanel);
    unk_0x98 = 5000;
    if (TheHamUI.GetHelpBarPanel()) {
        static Symbol controller_mode_timeout("controller_mode_timeout");
        const DataNode *prop =
            TheHamUI.GetHelpBarPanel()->Property(controller_mode_timeout, false);
        if (prop) {
            unk_0x98 = prop->Int();
        }
    }
    LetterboxPanel *lbp = TheHamUI.GetLetterboxPanel();
    if (lbp) {
        lbp->SyncToPanel(mInputPanel);
    }
    static Symbol use_gamertag_bg("use_gamertag_bg");
    bool use = false;
    if (mInputPanel) {
        SyncVoiceControl();
        const DataNode *prop = mInputPanel->Property(use_gamertag_bg, false);

        if (prop) {
            use = prop->Int();
        }
        static Symbol allow_doubleuser_swipe("allow_doubleuser_swipe");
        const DataNode *userProp = mInputPanel->Property(allow_doubleuser_swipe, false);
        if (userProp && userProp->Int() == 1) {
            TheGestureMgr->SetInDoubleUserMode(true);
        } else {
            TheGestureMgr->SetInDoubleUserMode(false);
        }
    }
    TheHamProvider->SetProperty(use_gamertag_bg, use);
}

DataNode ShellInput::OnMsg(const SpeechEnableMsg &msg) {
    if (msg->Int(2))
        SyncVoiceControl();
    return 0;
}

DataNode ShellInput::OnMsg(const ResetControllerModeTimeoutMsg &msg) {
    unk_0x68.Restart();
    return DATA_UNHANDLED;
}

DataNode ShellInput::OnMsg(const ButtonDownMsg &msg) {
    if (msg.GetButton() == kPad_RStickUp || msg.GetButton() == kPad_RStickDown
        || msg.GetButton() == kPad_RStickLeft || msg.GetButton() == kPad_RStickRight
        || msg.GetButton() == kPad_Xbox_LT || msg.GetButton() == kPad_Xbox_RT) {
        TheSynth->RunFlow("invalid_select.flow");
        return DATA_UNHANDLED;
    } else if (TheGestureMgr->InControllerMode() && msg.GetButton() == kPad_Start) {
        ExitControllerMode(false);
        return 0;
    } else if (mInputPanel) {
        bool b2 = false;
        static Symbol is_gameplay_panel("is_gameplay_panel");
        const DataNode *prop = mInputPanel->Property(is_gameplay_panel, false);
        if (prop && prop->Int() == 1) {
            b2 = true;
        }
        if (!b2 && !TheGestureMgr->InControllerMode()) {
            EnterControllerMode(false);
            if (msg.GetButton() != kPad_Xbox_B) {
                if (!TheHamUI.GetHelpBarPanel()->AllowController()) {
                    TheSynth->RunFlow("invalid_select.flow");
                }
                return 0;
            }
        }
        return DATA_UNHANDLED;
    } else {
        return DATA_UNHANDLED;
    }
}

DataNode ShellInput::OnMsg(const JoypadConnectionMsg &msg) {
    if (TheGestureMgr->InControllerMode() && !msg.Connected()) {
        if (msg->Int(5) == TheHamUI.GetPadNum())
            ExitControllerMode(false);
    }
    return DATA_UNHANDLED;
}

DataNode ShellInput::OnMsg(const SpeechRecoMessage &msg) { return 0; }

DataNode ShellInput::OnMsg(const LeftHandListEngagementMsg &msg) {
    if (msg.Success()) {
        static Symbol voice_commander_help_hide("voice_commander_help_hide");
        static Message voice_commander_help_hide_msg(voice_commander_help_hide);
        TheHamProvider->Handle(voice_commander_help_hide_msg, false);

    } else {
        if (!mInputPanel) {
            return DataNode(kDataInt, 0);
        }
        static Symbol allow_voice_control("allow_voice_control");
        const DataNode *voiceProp = mInputPanel->Property(allow_voice_control, false);
        if (!voiceProp || voiceProp->Int() != 1
            || TheProfileMgr.GetDisableVoiceCommander()
            || !TheProfileMgr.GetShowVoiceTip()) {
            return DataNode(kDataInt, 0);
        }
        static Symbol voice_commander_tip_temporary("voice_commander_tip_temporary");
        const DataNode *voiceHelpProp =
            mInputPanel->Property(voice_commander_tip_temporary, false);
        if (voiceHelpProp && voiceHelpProp->Int() != 0) {
            return DataNode(kDataInt, 0);
        }
        static Symbol voice_commander_help("voice_commander_help");
        static Message voiceCommanderHelp(voice_commander_help);
        TheHamProvider->Handle(voiceCommanderHelp, false);
    }
    return DataNode(kDataInt, 0);
}
