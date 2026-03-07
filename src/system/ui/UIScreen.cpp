#include "ui/UIScreen.h"
#include "gesture/GestureMgr.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Archive.h"
#include "os/Debug.h"
#include "os/JoypadMsgs.h"
#include "os/Timer.h"
#include "rndobj/Rnd.h"
#include "ui/UI.h"
#include "ui/UILabel.h"
#include "ui/UIPanel.h"
#include "ui/PanelDir.h"
#include "utl/MakeString.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include "utl/TextStream.h"
#include <vector>

void EnterGlitchCB(float, void *);
void UnloadGlitchCB(float, void *);

UIScreen *UIScreen::sUnloadingScreen;

UIScreen::UIScreen()
    : mFocusPanel(nullptr), mBack(nullptr), mClearVram(false), mShowing(true),
      mScreenId(sMaxScreenId++) {
    MILO_ASSERT(sMaxScreenId < 0x8000, 0x20);
}

void UIScreen::SetTypeDef(DataArray *data) {
    Hmx::Object::SetTypeDef(data);
    mFocusPanel = NULL;
    mPanelList.clear();
    static Symbol panels("panels");
    DataArray *panelsArr = data->FindArray(panels, false);
    if (panelsArr != NULL) {
        for (int i = 1; i < panelsArr->Size(); i++) {
            PanelRef pr;
            pr.mActive = true;
            pr.mAlwaysLoad = true;

            if (panelsArr->Node(i).Type() == kDataArray) {
                static Symbol active("active");
                static Symbol always_load("always_load");
                DataArray *panelArray = panelsArr->Array(i);
                pr.mPanel = panelArray->Obj<class UIPanel>(0);
                MILO_ASSERT(pr.mPanel, 0x3a);
                panelArray->FindData(active, pr.mActive, false);
                panelArray->FindData(always_load, pr.mAlwaysLoad, false);
            } else {
                pr.mPanel = panelsArr->Obj<class UIPanel>(i);
                MILO_ASSERT(pr.mPanel, 0x41);
            }
#ifdef HX_NATIVE
            if (!pr.mPanel) {
                MILO_WARN("UIScreen '%s': panel at index %d failed to construct", Name(), i);
                continue;
            }
#endif
            mPanelList.push_back(pr);
        }
    }
    static Symbol focus("focus");
    DataArray *focusArr = data->FindArray(focus, false);
    if (focusArr != NULL) {
        SetFocusPanel(focusArr->Obj<class UIPanel>(1));
    }

    if (mFocusPanel == NULL && !mPanelList.empty()) {
        SetFocusPanel(mPanelList.front().mPanel);
    }

    mBack = data->FindArray("back", false);
    static Symbol clear_vram("clear_vram");
    mClearVram = false;
    data->FindData(clear_vram, mClearVram, false);
}

void UIScreen::LoadPanels() {
    if (Archive::DebugArkOrder())
        MILO_LOG("ArkFile: ;%s\n", Name());

    FOREACH (it, mPanelList) {
#ifdef HX_NATIVE
        // On native, always load all panels. We skip UnloadPanels (ObjRef crash
        // workaround), so mLoadRefs stays >0 for previously loaded panels but new
        // panels with mAlwaysLoad=false and mLoadRefs=0 would never load.
        it->mPanel->CheckLoad();
        it->mLoaded = true;
#else
        if (it->mAlwaysLoad || it->mPanel->IsReferenced()) {
            it->mPanel->CheckLoad();
            it->mLoaded = true;
        } else {
            it->mLoaded = false;
        }
#endif
    }
    static Message load_panels("load_panels");
    HandleType(load_panels);
}

void UIScreen::UnloadPanels() {
    FOREACH_REVERSE(it, mPanelList) {
        if (it->mLoaded) {
            AutoGlitchReport hang(17.0f, UnloadGlitchCB, it->mPanel);
            it->mPanel->CheckUnload();
        }
    }
}

bool UIScreen::CheckIsLoaded() {
    FOREACH (it, mPanelList) {
        if (it->Active() && !it->mPanel->CheckIsLoaded()) {
#ifdef HX_NATIVE
            static int sLoadDiag = 0;
            if (sLoadDiag++ < 5) {
                printf("DC3 UI: Screen '%s' not loaded — panel '%s' (state=%d) blocking\n",
                       Name(), it->mPanel->Name(), (int)it->mPanel->GetState());
            }
#endif
            return false;
        }
    }

    return true;
}

bool UIScreen::IsLoaded() const {
    FOREACH (it, mPanelList) {
        if (it->Active() && it->mPanel->GetState() == UIPanel::kUnloaded) {
            return false;
        }
    }

    // please don't tell me const_cast is what they did lol
    static Message is_loaded("is_loaded");
    DataNode result = const_cast<UIScreen *>(this)->HandleType(is_loaded);
    if (result.Type() != kDataUnhandled) {
        return result.Int();
    }

    return true;
}

void UIScreen::Poll() {
    static Message poll_msg("poll_msg");
    HandleType(poll_msg);

    FOREACH (it, mPanelList) {
        if (it->Active() && !it->mPanel->Paused()) {
            it->mPanel->Poll();
        }
    }
}

void UIScreen::Draw() {
    if (!mShowing) {
        return;
    }

    for (std::list<PanelRef>::iterator it = mPanelList.begin(); it != mPanelList.end();
         it++) {
        if (it->Active() && it->mPanel->Showing()) {
            if (TheRnd.ShouldDrawPanel(it->mPanel)) {
                static Symbol suppress_blacklight_text("suppress_blacklight_text");
                const DataNode *prop = Property(suppress_blacklight_text, false);
                if (prop) {
                    int val = prop->Int();
                    if (val)
                        TheUI->SetScreenBlacklghtDisabled(true);
                    else
                        TheUI->SetScreenBlacklghtDisabled(false);
                }
                it->mPanel->Draw();
            }
        }
    }
}

bool UIScreen::InComponentSelect() const {
    UIComponent *component = TheUI->FocusComponent();
    if (component != nullptr) {
        return component->GetState() == UIComponent::kSelecting;
    }

    return false;
}

void UIScreen::Enter(UIScreen *from) {
#ifdef HX_NATIVE
    printf("DC3 UI: Screen '%s' Enter (from '%s')\n", Name(), from ? from->Name() : "<null>");
#endif
    if (from != NULL) {
        sUnloadingScreen = from;
#ifdef HX_NATIVE
        // Skip panel unload on native — ObjRef lifecycle issues cause SIGSEGV
        // during bulk object deletion (ObjPtrList::Unlink on freed nodes).
        // Instead, hide the old screen so it stops drawing.
        from->mShowing = false;
#else
        from->UnloadPanels();
#endif
    }

    Rnd::sPostProcPanelCount = 0;
    std::vector<char *> panelNames;
    int lastCount = 0;

    FOREACH (it, mPanelList) {
        if (it->Active() && it->mPanel->GetState() == UIPanel::kDown) {
            AutoGlitchReport hang(17.0f, EnterGlitchCB, it->mPanel);
            it->mPanel->Enter();
            if (Rnd::sPostProcPanelCount != lastCount) {
                panelNames.push_back((char *)it->mPanel->Name());
                lastCount = Rnd::sPostProcPanelCount;
            }
        }
    }

    if (Rnd::sPostProcPanelCount != 1) {
        if (Rnd::sPostProcPanelCount == 0) {
            TheDebug << MakeString(
                "[POSTPROC WARNING] UIScreen '%s' doesn't have any panels that set the PostProc\n",
                (char *)Name()
            );
        } else {
            TheDebug << MakeString(
                "[POSTPROC WARNING] UIScreen '%s' has %d panels that attempt to set the PostProc\n",
                Name(),
                Rnd::sPostProcPanelCount
            );
            for (int i = 0; i < Rnd::sPostProcPanelCount; i++) {
                TheDebug << MakeString(
                    "[POSTPROC WARNING]    panel = '%s'\n",
                    panelNames[i]
                );
            }
        }
        Rnd::sPostProcPanelCount = 0;
    }

    static Message msg("enter", 0);
    msg[0] = from;
    HandleType(msg);
    Poll();

#ifdef HX_NATIVE
    // Dump screen typeDef handlers for debugging
    {
        DataArray *td = TypeDef();
        if (td) {
            printf("DC3 Native: Screen '%s' typeDef:", Name());
            DataArray *nsArr = td->FindArray("next_screen", false);
            if (nsArr && nsArr->Size() > 1) {
                printf(" next_screen='%s'", nsArr->ForceSym(1).Str());
            }
            printf(" handlers:");
            for (int _i = 0; _i < td->Size(); _i++) {
                if (td->Type(_i) == kDataArray) {
                    DataArray *sub = td->Array(_i);
                    if (sub->Size() > 0 && sub->Type(0) == kDataSymbol) {
                        printf(" %s", sub->Sym(0).Str());
                    }
                }
            }
            printf("\n");
        }
    }
    // Auto-skip screens that wait for movie playback or Kinect input.
    // Without these subsystems, the screen would be stuck forever.
    {
        DataArray *td = TypeDef();
        if (td) {
            // We're called during the current transition, so save and check for NEW transitions
            bool wasInTransition = TheUI->InTransition();
            bool didNavigate = false;

            // Try skip_selected first (attract screen uses this)
            if (!didNavigate) {
                DataArray *skipHandler = td->FindArray("skip_selected", false);
                if (skipHandler) {
                    static Message skipMsg("skip_selected");
                    HandleType(skipMsg);
                    didNavigate = TheUI->TransitionScreen() != this;
                    printf("DC3 Native: Auto-skip '%s' via skip_selected (navigated=%d)\n",
                           Name(), didNavigate);
                }
            }

            // If screen has next_screen property, use it directly (more reliable than exit_screen)
            if (!didNavigate) {
                DataArray *nextArr = td->FindArray("next_screen", false);
                if (nextArr && nextArr->Size() > 1) {
                    Symbol nextName = nextArr->ForceSym(1);
                    printf("DC3 Native: Auto-skip '%s' -> '%s' via next_screen\n",
                           Name(), nextName.Str());
                    TheUI->GotoScreen(nextName.Str(), false, false);
                    didNavigate = true;
                }
            }
        }
    }
#endif
}

bool UIScreen::Entering() const {
    FOREACH (it, mPanelList) {
        if (it->Active() && it->mPanel->Entering()) {
            return true;
        }
    }

    if (sUnloadingScreen != nullptr && sUnloadingScreen->Unloading()) {
        return true;
    }

    sUnloadingScreen = nullptr;
    return false;
}

void UIScreen::Exit(UIScreen *to) {
#ifdef HX_NATIVE
    printf("DC3 UI: Screen '%s' Exit (to '%s')\n", Name(), to ? to->Name() : "<null>");
#endif
    TheGestureMgr->SetInVoiceMode(false);
    static Message msg("exit", 0);
    msg[0] = to;
    HandleType(msg);

    if (to != NULL) {
        to->LoadPanels();
    }

    FOREACH (it, mPanelList) {
        if (!it->mLoaded) {
            continue;
        }

        if ((it->mPanel->ForceExit() || to == NULL || !to->HasPanel(it->mPanel))
            && it->mPanel->GetState() == UIPanel::kUp) {
            it->mPanel->Exit();
        }
    }
}

bool UIScreen::Exiting() const {
    FOREACH (it, mPanelList) {
        if (it->Active() && it->mPanel->Exiting()) {
#ifdef HX_NATIVE
            static int sExitDiag = 0;
            if (sExitDiag++ < 5) {
                printf("DC3 UI: Screen '%s' still exiting — panel '%s' (state=%d) blocking\n",
                       Name(), it->mPanel->Name(), (int)it->mPanel->GetState());
            }
#endif
            return true;
        }
    }

    return false;
}

void UIScreen::Print(TextStream &s) {
    static Symbol file("file");

    s << "{UIScreen " << Name() << "\n";

    if (mPanelList.size() != 0) {
        s << "   Panels:\n";
        FOREACH (it, mPanelList) {
            s << "      " << it->mPanel->Name() << " ";
            if (!it->mActive) {
                s << "(active " << it->mActive << ") ";
            }
            if (!it->mAlwaysLoad) {
                s << "(always_load " << it->mAlwaysLoad << ") ";
            }

            const DataArray *typeDef = it->mPanel->TypeDef();
            if (typeDef != nullptr) {
                DataArray *fileArray = typeDef->FindArray(file, false);
                if (fileArray != nullptr) {
                    DataNode type = fileArray->Node(1);
                    if (type.Type() == kDataString || type.Type() == kDataSymbol) {
                        s << "(" << type.LiteralStr() << ") ";
                    } else {
                        s << "(dynamic) ";
                    }
                }
            } else {
                s << " ";
            }

            if (it->mPanel == mFocusPanel) {
                s << "(focus)";
            }

            s << "\n";
        }
    }

    s << "}\n";
}

bool UIScreen::Unloading() const {
    FOREACH (it, mPanelList) {
        if (it->mLoaded && it->mPanel->Unloading()) {
            return true;
        }
    }

    return false;
}

void UIScreen::SetFocusPanel(UIPanel *panel) {
    if (panel == mFocusPanel)
        return;

    if (mFocusPanel != nullptr)
        mFocusPanel->FocusIn();

    mFocusPanel = panel;

    if (mFocusPanel != nullptr)
        mFocusPanel->FocusOut();
}

void UIScreen::SetShowing(bool show) { mShowing = show; }

bool UIScreen::HasPanel(UIPanel *panel) {
    FOREACH (it, mPanelList) {
        if (it->mPanel == panel && it->mActive) {
            return true;
        }
    }

    return false;
}

// Exits all active panels, then re-enters them.
// Used to reset panel state without fully unloading.
void UIScreen::ReenterScreen() {
    AutoGlitchReport hang(50.0f, "UIScreen::ReenterScreen");

    // Exit all active panels
    FOREACH_POST (it, mPanelList) {
        if (it->Active()) {
            it->mPanel->Exit();
        }
    }

    // Re-enter all active panels
    FOREACH_POST (it, mPanelList) {
        if (it->Active()) {
            it->mPanel->Enter();
        }
    }
}

void UIScreen::SetPanelActive(UIPanel *panel, bool active) {
    bool found = false;
    FOREACH (it, mPanelList) {
        if (it->mPanel == panel) {
            it->mActive = active;
            found = true;
        }
    }
    MILO_ASSERT(found, 0x164);
}

bool UIScreen::AllPanelsDown() {
    FOREACH (it, mPanelList) {
        if (it->Active() && it->mPanel->GetState() != UIPanel::kDown) {
            return false;
        }
    }

    return true;
}

bool UIScreen::SharesPanels(UIScreen *screen) {
    FOREACH (it, mPanelList) {
        if (screen->HasPanel(it->mPanel)) {
            return true;
        }
    }

    return false;
}

DataNode UIScreen::OnMsg(ButtonDownMsg const &msg) {
    if (mBack != nullptr && msg.GetAction() == kAction_Cancel) {
        DataNode n = mBack->Evaluate(1);
        if (n.Type() != kDataUnhandled) {
            static Symbol go_back_screen("go_back_screen");
            Message m(go_back_screen, n.Str(), msg.GetUser());
            TheUI->Handle(m, false);
        }
    }

    return DATA_UNHANDLED;
}

DataNode UIScreen::ForeachPanel(const DataArray *da) {
    // {$screen foreach_panel $panel ...}

    DataNode *var = da->Var(2);
    DataNode tmp = *var;

    FOREACH_POST (it, mPanelList) {
        if (!it->mActive) {
            continue;
        }

        *var = it->mPanel;
        for (int i = 3; i < da->Size(); i++) {
            da->Command(i)->Execute();
        }
    }

    *var = tmp;
    return DataNode(0);
}

void UIScreen::ReloadStrings() {
    Message msg(Symbol("reload_string"));
    FOREACH (it, mPanelList) {
#ifdef HX_NATIVE
        if (!it->mPanel) continue;
#endif
        ObjectDir *panelDir = it->mPanel->DataDir();
        if (!panelDir) {
            continue;
        }
        for (ObjDirItr<UILabel> labelIt(panelDir, true); labelIt; ++labelIt) {
            labelIt->Handle(msg, true);
        }
    }
}

BEGIN_HANDLERS(UIScreen)
    HANDLE_EXPR(focus_panel, mFocusPanel)
    HANDLE_ACTION(set_focus_panel, SetFocusPanel(_msg->Obj<class UIPanel>(2)))
    HANDLE_ACTION(print, Print(TheDebug))
    HANDLE_ACTION(reenter_screen, ReenterScreen())
    HANDLE_ACTION(
        set_panel_active, SetPanelActive(_msg->Obj<class UIPanel>(2), _msg->Int(3))
    )
    HANDLE_ACTION(set_showing, SetShowing(_msg->Int(2)))
    HANDLE_EXPR(has_panel, HasPanel(_msg->Obj<class UIPanel>(2)))
    HANDLE_ACTION(foreach_panel, ForeachPanel(_msg))
    HANDLE_EXPR(exiting, Exiting())
    HANDLE_ACTION(reload_strings, ReloadStrings())
    HANDLE_SUPERCLASS(Hmx::Object)
    HANDLE_MEMBER_PTR(FocusPanel())
    HANDLE_MESSAGE(ButtonDownMsg)
END_HANDLERS

#ifdef HX_NATIVE
void EnterGlitchCB(float fElapsed, void *data) {
    // Glitch detection callbacks use hardcoded ILP32 struct offsets — stub on native
    TheDebug << MakeString("Enter took %.2f ms\n", fElapsed);
}

void UnloadGlitchCB(float f, void *data) {
    TheDebug << MakeString("CheckUnload took %.2f ms\n", f);
}
#else
void EnterGlitchCB(float fElapsed, void *data) {
    int sp54;
    const char *sp50;

    char *obj = (char *)(((int **)data)[1][1] + (int)data);
    sp50 = *(const char **)((char *)obj + 0x24);
    void *(*func)(void *, char *) = *(void *(**)(void *, char *))(*((int *)(obj + 4)) + 0xC);
    TheDebug << MakeString("%s %s Enter took %.2f ms\n", *(Symbol *)func(&sp54, obj + 4), sp50, fElapsed);
}

void UnloadGlitchCB(float f, void *data) {
    int checkTime;
    char *obj = (char *)((char **)((char **)data)[1])[1] + (int)data;
    checkTime = *(int *)((char *)obj + 0x24);
    int result = (*(int (**)(char *, char *, int))((char *)obj + 0xC))((char *)obj + 4, obj, 0);
    TheDebug << MakeString("CheckUnload took %2.f ms\n", result, &checkTime, &f);
}
#endif
