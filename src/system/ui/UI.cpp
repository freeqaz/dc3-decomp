#include "ui/UI.h"
#include "UIComponent.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/MessageTimer.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/JoypadClient.h"
#include "os/Keyboard.h"
#include "os/System.h"
#include "os/UserMgr.h"
#include "rndobj/Cam.h"
#include "ui/CheatProvider.h"
#include "utl/Cheats.h"
#include "ui/InlineHelp.h"
#include "ui/LabelNumberTicker.h"
#include "ui/LabelShrinkWrapper.h"
#include "ui/LocalePanel.h"
#include "ui/PanelDir.h"
#include "ui/Screenshot.h"
#include "ui/UIButton.h"
#include "ui/UIColor.h"
#include "ui/UIFontImporter.h"
#include "ui/UIGuide.h"
#include "ui/UILabel.h"
#include "ui/UIList.h"
#include "ui/UIPicture.h"
#include "ui/UIScreen.h"
#include "ui/UIPanel.h"
#include "ui/UISlider.h"
#include "ui/UITrigger.h"
#include "utl/KnownIssues.h"
#include "utl/Locale.h"
#include "utl/OSCMessenger.h"
#include "utl/Std.h"
#include "utl/Str.h"
#include "utl/Symbol.h"

namespace {
    JoypadAction NavButtonToNavAction(JoypadButton btn) {
        switch (btn) {
        case kPad_DLeft:
            return kAction_Left;
        case kPad_DRight:
            return kAction_Right;
        case kPad_DDown:
            return kAction_Down;
        case kPad_DUp:
            return kAction_Up;
        default:
            return kAction_None;
        }
    }
}

const char *TransitionStateString(UIManager::TransitionState s) {
    switch (s) {
    case UIManager::kTransitionTo:
        return "to";
    case UIManager::kTransitionFrom:
        return "from";
    case UIManager::kTransitionPop:
        return "pop";
    default:
        return "";
    }
}

void TerminateCallback() {
    MILO_ASSERT(TheUI, 0x1CE);
    TheUI->Terminate();
}

void FailAppendCallback(FixedString &str) {
    if ((TheUI && TheUI->CurrentScreen()) || TheUI->TransitionScreen()) {
        str += "\n";
        if (TheUI->CurrentScreen()) {
            str += "Screen: ";
            str += TheUI->CurrentScreen()->Name();
        }
        if (TheUI->InTransition()) {
            str += "Trans: ";
            str += TransitionStateString(TheUI->GetTransitionState());
            str += " ";
            str += TheUI->TransitionScreen()->Name();
        }
    }
}

#pragma region UIManager

UIManager::UIManager()
    : mWentBack(0), mMaxPushDepth(100), mJoyClient(0), mCurrentScreen(0), unk50(0),
      mOverloadHorizontalNav(0), mCancelTransitionNotify(0), mDefaultAllowEditText(1),
      mDisableScreenBlacklight(0), mOverlay(0), mAutomator(0), unkd0(0) {}

UIManager::~UIManager() {}

void UIManager::SetScreenBlacklghtDisabled(bool disable) {
    mDisableScreenBlacklight = disable;
}

bool UIManager::InComponentSelect() {
    if (mCurrentScreen)
        return mCurrentScreen->InComponentSelect();
    else
        return false;
}

UIPanel *UIManager::FocusPanel() {
    if (mCurrentScreen)
        return mCurrentScreen->FocusPanel();
    else
        return nullptr;
}

UIComponent *UIManager::FocusComponent() {
    UIPanel *focusPanel = FocusPanel();
    if (focusPanel)
        return focusPanel->FocusComponent();
    else
        return nullptr;
}

void UIManager::GotoFirstScreen() {
    UIScreen *screen = DataVariable("first_screen").Obj<UIScreen>();
    GotoScreen(screen, false, false);
    mTimer.Restart();
}

void UIManager::ToggleLoadTimes() {
    mOverlay->CurrentLine() = gNullStr;
    mOverlay->SetShowing(!mOverlay->Showing());
}

void UIManager::Draw() {
    for (std::vector<UIScreen *>::iterator it = mPushedScreens.begin();
         it != mPushedScreens.end();
         ++it) {
        (*it)->Draw();
    }
    if (mCurrentScreen)
        mCurrentScreen->Draw();
}

void UIManager::GotoScreen(const char *name, bool b2, bool b3) {
    UIScreen *screen = ObjectDir::Main()->Find<UIScreen>(name, true);
    MILO_ASSERT(screen, 0x37E);
    GotoScreen(screen, b2, b3);
}

void UIManager::GotoScreen(UIScreen *scr, bool b1, bool b2) {
    GotoScreenImpl(scr, b1, b2);
}

void UIManager::ResetScreen(UIScreen *screen) {
    if (mTransitionState != kTransitionNone && mTransitionState != kTransitionFrom) {
        bool old = mCancelTransitionNotify;
        mCancelTransitionNotify = false;
        CancelTransition();
        mCancelTransitionNotify = old;
    }
    if (mPushedScreens.empty()) {
        GotoScreen(screen, false, false);
    } else {
        MILO_ASSERT(mPushedScreens.size() == 1, 0x3E5);
        PopScreen(screen);
    }
}

UIScreen *UIManager::BottomScreen() {
    return !mPushedScreens.empty() ? mPushedScreens.front() : mCurrentScreen;
}

int UIManager::PushDepth() const { return mPushedScreens.size(); }

UIScreen *UIManager::ScreenAtDepth(int depth) {
    MILO_ASSERT(depth < mPushedScreens.size(), 0x46F);
    return mPushedScreens[depth];
}

void UIManager::UseJoypad(bool useJoypad, bool enableAutoRepeat) {
    if (useJoypad && !mJoyClient) {
        mJoyClient = new JoypadClient(this);
        mJoyClient->SetVirtualDpad(true);
        if (enableAutoRepeat) {
            mJoyClient->SetRepeatMask(0xf000);
        }
    } else if (!useJoypad) {
        if (mJoyClient) {
            RELEASE(mJoyClient);
        }
    }
}

void UIManager::CancelTransition() {
    if (mCancelTransitionNotify && mTransitionState != kTransitionNone
        && mTransitionState != kTransitionFrom) {
        MILO_NOTIFY("Cancelled transition");
    }
    TransitionState oldState = mTransitionState;
    UIScreen *oldScreen = mTransitionScreen;
    mTransitionState = kTransitionNone;
    mTransitionScreen = nullptr;
    if (oldState == kTransitionTo) {
        if (mCurrentScreen) {
            mCurrentScreen->Enter(oldScreen);
        } else if (oldScreen)
            oldScreen->UnloadPanels();
    } else if (oldState == kTransitionPop && mCurrentScreen) {
        mCurrentScreen->Enter(nullptr);
    }
}

bool UIManager::OverloadHorizontalNav(JoypadAction act, JoypadButton btn, bool b) const {
    return !(!mOverloadHorizontalNav || NavButtonToNavAction(btn) == act && !b);
}

void UIManager::Terminate() {
    CheatProvider::Terminate();
    UILabel::Terminate();
    SetName(0, 0);
    KeyboardUnsubscribe(this);
    RELEASE(mCam);
    RELEASE(mEnv);
    RELEASE(mJoyClient);
    TheDebug.RemoveExitCallback(TerminateCallback);
    RELEASE(mAutomator);
}

bool UIManager::IsGameScreenActive() {
    // Resolve the bottom-most screen (first pushed screen, or current if none pushed)
    UIScreen *screen;
    if (!mPushedScreens.empty()) {
        screen = mPushedScreens[0];
    } else {
        screen = mCurrentScreen;
    }

    bool result;
    if (screen) {
        // Re-resolve bottom screen (required for register allocation to match target)
        UIScreen *bottomScreen;
        if (!mPushedScreens.empty()) {
            bottomScreen = mPushedScreens[0];
        } else {
            bottomScreen = mCurrentScreen;
        }

        // Manual string comparison to check if screen name is "game_screen"
        // (Cannot use strcmp - must match target's inline comparison)
        const char *screenName = bottomScreen->Name();
        const char *gameScreen = "game_screen";
        signed char c1, c2;
        do {
            c1 = *screenName;
            c2 = *gameScreen;
            if (!c1) break;
            screenName++;
            gameScreen++;
        } while (c1 == c2);

        result = true;
        // If strings match, proceed to check if bottom equals current
        if (c1 == c2) goto check_current;
    }
    result = false;

check_current:
    // Verify that bottom screen is the current screen
    screen = mCurrentScreen;
    if (screen) {
        UIScreen *bottomScreen = screen;
        if (!mPushedScreens.empty()) {
            bottomScreen = mPushedScreens[0];
        }
        // Bit manipulation: result &= (bottomScreen == screen)
        // Uses -(bool) to convert true->-1 (all bits set), false->0
        result = -(bottomScreen == screen) & result;
    }

    return result;
}

bool UIManager::BlockHandlerDuringTransition(Symbol s, DataArray *da) { return false; }

void UIManager::GotoScreenImpl(UIScreen *scr, bool b1, bool b2) {
    // Only proceed if:
    // - Force transition (b1), OR
    // - Already in a transition, OR
    // - Changing to a different screen
    // AND we're not already transitioning to this exact screen
    if (((b1 || mTransitionState != kTransitionNone) || mCurrentScreen != scr)
        && ((mTransitionState != kTransitionTo && mTransitionState != kTransitionPop)
            || mTransitionScreen != scr)) {
        CancelTransition();

#ifdef MILO_DEBUG
        // Verify that the new screen doesn't share panels with any pushed screens
        // (panels should be unique per screen to avoid resource conflicts)
        if (scr) {
            for (std::vector<UIScreen *>::iterator it = mPushedScreens.begin();
                 it != mPushedScreens.end();
                 ++it) {
                if (scr->SharesPanels(*it)) {
                    MILO_FAIL("%s shares panels with %s", scr->Name(), (*it)->Name());
                }
            }
        }
#endif

        // Log the transition for debugging
        const char *curName = mCurrentScreen ? mCurrentScreen->Name() : "<none>";
        const char *newName = scr ? scr->Name() : "<none>";
        TheDebug << MakeString("transition from %s to %s\n", curName, newName);

        // Store transition state and notify listeners
        mWentBack = b2;
        UIScreenChangeMsg msg(scr, mCurrentScreen, mWentBack);
        Handle(msg, false);

        // Begin transition to new screen
        mTransitionState = kTransitionTo;
        mTransitionScreen = scr;

        // Exit current screen or load new screen panels
        if (mCurrentScreen) {
            mCurrentScreen->Exit(scr);
        } else if (scr) {
            scr->LoadPanels();
        }

#ifdef MILO_DEBUG
        // Start tracking load time for the new screen
        if (mTransitionScreen) {
            mOverlay->CurrentLine() = gNullStr;
            mLoadTimer.Restart();
        }
#endif
    }
}

void UIManager::PopScreen(UIScreen *screen) {
    if (mPushedScreens.empty()) {
        MILO_NOTIFY("No screen to pop\n");
    } else {
        GotoScreenImpl(nullptr, false, false);
        mTransitionState = kTransitionPop;
        if (screen)
            mTransitionScreen = screen;
        else
            mTransitionScreen = mPushedScreens.back();
    }
}

DataNode UIManager::OnIsResource(DataArray *arr) { return 0; }

DataNode UIManager::OnGotoScreen(DataArray const *arr) {
    Hmx::Object *obj = arr->GetObj(2);
    UIScreen *screen = dynamic_cast<UIScreen *>(obj);
    if (screen == nullptr && obj)
        MILO_FAIL("%s is not a screen", obj->Name());

    if (arr->Size() > 4) {
        GotoScreen(screen, arr->Int(3), arr->Int(4));
    } else if (arr->Size() > 3) {
        GotoScreen(screen, arr->Int(3), false);
    } else {
        GotoScreen(screen, false, false);
    }
    return 0;
}

DataNode UIManager::OnGoBackScreen(DataArray const *arr) {
    Hmx::Object *obj = arr->GetObj(2);
    UIScreen *screen = dynamic_cast<UIScreen *>(obj);
    if (screen == nullptr && obj) {
        MILO_FAIL("%s is not a screen", obj->Name());
    }
    GotoScreen(screen, false, true);
    return DATA_UNHANDLED;
}

void UIManager::ReloadStrings() {
    Message msg(Symbol("reload_strings"));

    if (mCurrentScreen) {
        mCurrentScreen->Handle(msg, true);
    }

    FOREACH (it, mPushedScreens) {
        (*it)->Handle(msg, true);
    }
}

void UIManager::FakeKeyboardAction(JoypadButton btn, JoypadAction action) {
    static ButtonDownMsg msg(nullptr, btn, action, 0x18);
    msg[0] = TheUserMgr->GetLocalUserFromPadNum(0);
    msg[1] = btn;
    msg[2] = action;
    msg[3] = 0;
    mCurrentScreen->Handle(msg, false);
}

void UIManager::Poll() {}

void UIManager::PushScreen(UIScreen *screen) {
    // Function prologue
    UIScreen *current = mCurrentScreen;

    CancelTransition();
    MILO_ASSERT(current, 0x358);
    MILO_ASSERT(screen, 0x359);

    // Check if screen is already in stack
    UIScreen *existing = nullptr;
    for (std::vector<UIScreen *>::iterator it = mPushedScreens.begin();
         it != mPushedScreens.end();
         ++it) {
        existing = *it;
        if (screen == existing) {
            MILO_WARN("Don't push %s, it is already there!\n", screen->Name());
            break;
        }
    }

    // Push current screen to stack
    mPushedScreens.push_back(current);

    // Check max depth (use > instead of >= for branch matching)
    if (mPushedScreens.size() > (mMaxPushDepth - 1)) {
        MILO_WARN(
            "Exceeded max push depth of %i, pushing %s", mMaxPushDepth, screen->Name()
        );
        MILO_LOG("mPushedScreens:\n");

        UIScreen *stacked = nullptr;
        for (std::vector<UIScreen *>::iterator it = mPushedScreens.begin();
             it != mPushedScreens.end();
             ++it) {
            stacked = *it;
            if (stacked) {
                MILO_LOG("%s\n", stacked->Name());
            }
        }
    }

    // Clear current screen before transition
    mCurrentScreen = nullptr;
    GotoScreenImpl(screen, false, false);
}

DataNode UIManager::OnForeachCurrentScreen(DataArray const *) { return NULL_OBJ; }

void UITerminateCallback() { TheUI->Terminate(); }

void UIManager::Init() {
    MILO_ASSERT(TheUI, 0x1f3);
    mAutomator = new Automator(*this);
    SetName("ui", ObjectDir::Main());
    DataArray *cfg = SystemConfig("ui");
    SetTypeDef(SystemConfig("ui"));
    UseJoypad(cfg->FindInt("use_joypad"), cfg->FindInt("enable_auto_repeat"));
    KeyboardSubscribe(this);
    mCurrentScreen = nullptr;
    mTransitionState = kTransitionNone;
    mTransitionScreen = nullptr;
    mWentBack = false;
    mCam = ObjectDir::Main()->New<RndCam>("[ui.cam]");
    DataArray *camCfg = cfg->FindArray("cam");
    auto _tmp9 = camCfg->FindFloat("fov");
    auto _tmp5 = camCfg->FindFloat("near");
    mCam->SetFrustum(
        _tmp5,
        camCfg->FindFloat("far"),
        _tmp9 * DEG2RAD,
        1.0f
    );
    // mCam->SetLocalPos(0, camCfg->FindFloat("y"), 0);
    DataArray *zArr = camCfg->FindArray("z-range");
    mCam->SetZRange(zArr->Float(1), zArr->Float(2));
    mEnv = Hmx::Object::New<RndEnviron>();
    Hmx::Color envAmbientColor;
    cfg->FindArray("env")->FindData("ambient", envAmbientColor, true);
    mEnv->SetAmbientColor(envAmbientColor);
    cfg->FindData("max_push_depth", mMaxPushDepth, false);
    cfg->FindData("cancel_transition_notify", mCancelTransitionNotify, false);
    cfg->FindData("default_allow_edit_text", mDefaultAllowEditText, false);
    bool notify = false;
    cfg->FindData("verbose_locale_notifies", notify, false);
    Locale::SetLocaleVerboseNotify(false);
    REGISTER_OBJ_FACTORY(UIScreen)
    REGISTER_OBJ_FACTORY(UIPanel)
    REGISTER_OBJ_FACTORY(PanelDir)
    UIComponent::Init();
    UIButton::Init();
    REGISTER_OBJ_FACTORY(UIColor)
    UILabel::Init();
    UIList::Init();
    REGISTER_OBJ_FACTORY(UIPicture)
    UISlider::Init();
    REGISTER_OBJ_FACTORY(UITrigger)
    InlineHelp::Init();
    REGISTER_OBJ_FACTORY(UIFontImporter)
    REGISTER_OBJ_FACTORY(UIGuide)
    REGISTER_OBJ_FACTORY(Screenshot)
    LabelNumberTicker::Init();
    LabelShrinkWrapper::Init();
    TheDebug.AddExitCallback(UITerminateCallback);

    std::vector<ObjDirPtr<ObjectDir> > dirPtrs;
    DataArray *frontloadArr = cfg->FindArray("frontload_subdirs", false);
    if (frontloadArr) {
        dirPtrs.resize(frontloadArr->Size() - 1);
        for (int i = 1; i < frontloadArr->Size(); i++) {
            String curStr = frontloadArr->Str(i);
            dirPtrs[i - 1].LoadFile(curStr.c_str(), false, true, kLoadFront, false);
        }
    }
    CheatProvider::Init();
    REGISTER_OBJ_FACTORY(LocalePanel)
    static Message cheat_init("cheat_init");
    Hmx::Object::Handle(cheat_init, false);
    mOverlay = RndOverlay::Find("ui", true);
    mOverlay->SetShowing(false);
    TheOSCMessenger.Connect();
    PreloadSharedSubdirs("ui");
    // FailAppendCallback(FixedString &str); unsure
    UILabel::sRequireFixedLength = true;
    static Message init("init");
    Hmx::Object::Handle(init, false);
    mTimer.Restart();
    cfg->FindData("overload_horizontal_nav", mOverloadHorizontalNav, false);
    TheKnownIssues.Init();
}

DataNode UIManager::OnSetSink(DataArray *arr) { return 0; }
DataNode UIManager::OnUseJoypad(DataArray *arr) {
    int val = arr->Int(2);
    UseJoypad(val != 0, true);
    return 0;
}
DataNode UIManager::OnSetVirtualDpad(DataArray *arr) {
    int val = arr->Int(2);
    mJoyClient->SetVirtualDpad(val != 0);
    return 0;
}
DataNode UIManager::OnPushScreen(DataArray *arr) {
    return 0;
}
DataNode UIManager::OnPopScreen(DataArray *arr) {
    return 0;
}
DataNode UIManager::OnCurrentScreen(DataArray *arr) {
    return DataNode(mCurrentScreen);
}
DataNode UIManager::OnTransitionScreen(DataArray *arr) {
    return DataNode(mTransitionScreen);
}
DataNode UIManager::OnBottomScreen(DataArray *arr) {
    UIScreen *screen;
    if (mPushedScreens.size() != (size_t)mPushedScreens.capacity()) {
        screen = *mPushedScreens.begin();
    } else {
        screen = mCurrentScreen;
    }
    return DataNode(screen);
}
DataNode UIManager::OnInTransition(DataArray *arr) {
    return DataNode((bool)(mTransitionState != kTransitionNone));
}
DataNode UIManager::OnFocusPanel(DataArray *arr) {
    return 0;
}
DataNode UIManager::OnWentBack(DataArray *arr) {
    return DataNode((bool)mWentBack);
}
DataNode UIManager::OnIsGameScreenActive(DataArray *arr) {
    return DataNode((bool)IsGameScreenActive());
}
DataNode UIManager::OnToggleLoadTimes(DataArray *arr) {
    ToggleLoadTimes();
    return 0;
}
DataNode UIManager::OnShowingLoadTimes(DataArray *arr) {
    u8 result = 0;
    return DataNode(result);
}
DataNode UIManager::OnToggleDevMenu(DataArray *arr) {
    return 0;
}
DataNode UIManager::OnShowDevMenu(DataArray *arr) {
    u8 result = 0;
    return DataNode(result);
}
DataNode UIManager::OnResetScreen(DataArray *arr) {
    return 0;
}
DataNode UIManager::OnFakeKeyboardAction(DataArray *arr) {
    int button = arr->Int(3);
    int action = arr->Int(2);
    FakeKeyboardAction((JoypadButton)action, (JoypadAction)button);
    return 0;
}

BEGIN_HANDLERS(UIManager)
    HANDLE(set_sink, OnSetSink)
    HANDLE(use_joypad, OnUseJoypad)
    HANDLE(set_virtual_dpad, OnSetVirtualDpad)
    HANDLE(push_screen, OnPushScreen)
    HANDLE(pop_screen, OnPopScreen)
    HANDLE(goto_screen, OnGotoScreen)
    HANDLE(go_back_screen, OnGoBackScreen)
    HANDLE(reset_screen, OnResetScreen)
    HANDLE(focus_panel, OnFocusPanel)
    HANDLE(current_screen, OnCurrentScreen)
    HANDLE(transition_screen, OnTransitionScreen)
    HANDLE(bottom_screen, OnBottomScreen)
    HANDLE(in_transition, OnInTransition)
    HANDLE(is_resource, OnIsResource)
    HANDLE(foreach_current_screen, OnForeachCurrentScreen)
    HANDLE(went_back, OnWentBack)
    HANDLE(is_game_screen_active, OnIsGameScreenActive)
    HANDLE(toggle_load_times, OnToggleLoadTimes)
    HANDLE(showing_load_times, OnShowingLoadTimes)
    HANDLE(toggle_dev_menu, OnToggleDevMenu)
    HANDLE(show_dev_menu, OnShowDevMenu)
    HANDLE(fake_keyboard_action, OnFakeKeyboardAction)
END_HANDLERS

#pragma endregion UIManager
#pragma region AutoMator

const char *Automator::ToggleAuto() {
    mCurScript = 0;
    if (mScreenScripts) {
        mScreenScripts->Release();
        mScreenScripts = 0;
    } else {
        Loader *ldr = TheLoadMgr.AddLoader(mAutoPath.c_str(), kLoadFront);
        DataLoader *dl = dynamic_cast<DataLoader *>(ldr);
        MILO_ASSERT(dl, 0x90);
        TheLoadMgr.PollUntilLoaded(dl, 0);
        mScreenScripts = dl->Data();
        mCurScreenIndex = 0;
        if (mScreenScripts) {
            StartAuto(mUIManager.CurrentScreen());
        }
    }
    return AutoScript();
}

void Automator::StartAuto(UIScreen *screen) {
    MILO_ASSERT(mScreenScripts, 0xC0);
    mCurScript = nullptr;
    if (screen) {
        mCurMsgIndex = 1;
        for (int i = mCurScreenIndex; i < mScreenScripts->Size(); i++) {
            DataArray *arr = mScreenScripts->Array(i);
            if (arr->Sym(0) == screen->Name()) {
                mCurScript = arr;
                mCurScreenIndex++;
                break;
            }
        }
    }
}

Symbol Automator::CurRecordScreen() {
    DataArray *recordArr = mRecord;
    if (recordArr->Size() > 0) {
        return recordArr->Array(recordArr->Size() - 1)->Sym(0);
    } else
        return gNullStr;
}

void Automator::AddRecord(Symbol s, DataArray *arr) {
    MILO_ASSERT(mRecord, 0x14F);
    int recordSize = mRecord->Size();
    DataArray *addArr;
    if (CurRecordScreen() == s) {
        addArr = mRecord->Array(recordSize - 1);
    } else {
        addArr = new DataArray(1);
        addArr->Node(0) = s;
        mRecord->Insert(recordSize, addArr);
    }
    addArr->Insert(addArr->Size(), arr);
}

void Automator::FinishRecord() {
    if (mRecord) {
        MILO_ASSERT(!mRecordPath.empty(), 0x162);
        DataWriteFile(mRecordPath.c_str(), mRecord, 0);
    }
    if (mRecord) {
        mRecord->Release();
        mRecord = nullptr;
    }
}

Automator::Automator(UIManager &mgr)
    : mUIManager(mgr), mScreenScripts(0), mRecord(0), mAutoPath("automator.dta"),
      mRecordPath("automator.dta"), mCurScript(0), mSkipNextQuickCheat(0) {}

Automator::~Automator() {
    if (mScreenScripts) {
        mScreenScripts->Release();
        mScreenScripts = 0;
    }
    FinishRecord();
}

DataNode Automator::OnCustomMsg(const Message &msg) {
    Symbol key = msg.Type();
    // ain't no way this is how hmx wrote it
    std::list<Symbol>::iterator it = mCustomMsgs.begin();
    if (it != mCustomMsgs.end()) {
        for (; it != mCustomMsgs.end() && *it != key; ++it)
            ;
        if (it != mCustomMsgs.end())
            HandleMessage(key);
    }
    return DATA_UNHANDLED;
}

DataNode Automator::OnMsg(const UITransitionCompleteMsg &msg) {
    if (mScreenScripts && !mRecord)
        StartAuto(msg.GetNewScreen());
    return DATA_UNHANDLED;
}

void Automator::FillButtonMsg(ButtonDownMsg &msg, int idx) {
    MILO_ASSERT(mCurScript, 0x141);
    DataArray *b = mCurScript->Array(idx);
    static Symbol button_down("button_down");
    MILO_ASSERT(b->Sym(0) == button_down, 0x144);
    int padnum = b->Int(3);
    msg[0] = TheUserMgr->GetLocalUserFromPadNum(padnum);
    msg[1] = b->Int(1);
    msg[2] = b->Int(2);
    msg[3] = padnum;
}

void Automator::AdvanceScript(Symbol msg) {
    if (mCurScript) {
        if (mCurScript->Array(mCurMsgIndex)->Sym(0) == msg) {
            mFramesSinceAdvance = 0;
            mCurMsgIndex++;
            if (mCurMsgIndex >= mCurScript->Size()) {
                mCurScript = 0;
                if (mScreenScripts->Size() == mCurScreenIndex) {
                    static Message auto_script_done("auto_script_done");
                    mUIManager.Handle(auto_script_done, false);
                }
            }
        }
    }
}

char const *Automator::ToggleRecord() {
    if (mRecord != nullptr) {
        FinishRecord();

    } else {
        mSkipNextQuickCheat = true;
        mRecord = new DataArray(0);
    }

    if (mRecord != nullptr)
        return mRecordPath.c_str();
    else
        return "OFF";
}

Symbol Automator::CurScreenName() {
    UIScreen *curScreen = mUIManager.CurrentScreen();
    if (curScreen) {
        static Message msg(Symbol("is_system_cheat"));
        DataNode handled = curScreen->Handle(msg, false);
        bool unhandled = handled.Equal(DataNode(kDataUnhandled, 0), 0, 1) || handled.Int() == 0;
        if (unhandled) {
            return curScreen->Name();
        }
    }
    return gNullStr;
}

void Automator::Poll() {}

DataNode Automator::OnMsg(ButtonDownMsg const &msg) {
    Symbol screenName = CurScreenName();
    if (mRecord && !screenName.Null()) {
        static Symbol button_down("button_down");
        DataArrayPtr ptr(
            button_down,
            DataGetMacroByInt(msg.GetPadNum(), "kPad_"),
            DataGetMacroByInt(msg.GetAction(), "kAction_"),
            msg.GetButton()
        );
        AddRecord(screenName, ptr);
    }
    return DATA_UNHANDLED;
}

DataNode Automator::OnCheatInvoked(DataArray const *arr) { return DATA_UNHANDLED; }

void Automator::HandleMessage(Symbol msgType) {
    if (!mUIManager.InTransition()) {
        if (mRecord) {
            Symbol screenName = CurScreenName();
            if (!screenName.Null()) {
                DataArrayPtr ptr(msgType);
                AddRecord(screenName, ptr);
            }
        } else if (mScreenScripts) {
            AdvanceScript(msgType);
        }
    }
}

BEGIN_HANDLERS(Automator)
    HANDLE_EXPR("toggle_auto", DataNode(ToggleAuto()))
    HANDLE_EXPR("toggle_record", DataNode(ToggleRecord()))
    HANDLE_EXPR("auto_script", DataNode((mScreenScripts && !mRecord) ? mAutoPath.c_str() : "OFF"))
    HANDLE_EXPR("record_script", DataNode(mRecord ? mRecordPath.c_str() : "OFF"))
    HANDLE_ACTION("set_auto_script", (mAutoPath = _msg->Str(2)))
    HANDLE_ACTION("set_record_script", (mRecordPath = _msg->Str(2)))
    HANDLE_ACTION("add_message_type", (AddMessageType(_msg->GetObj(2), _msg->Sym(3))))
END_HANDLERS

#pragma endregion Automator
