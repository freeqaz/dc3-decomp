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
#include "utl/Std.h"
#include "utl/Symbol.h"
#include "utl/TextStream.h"

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
        if (it->mAlwaysLoad || it->mPanel->IsReferenced()) {
            it->mPanel->CheckLoad();
            it->mLoaded = true;
        } else {
            it->mLoaded = false;
        }
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
                    TheUI->SetScreenBlacklghtDisabled(val ? true : false);
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
    if (from != NULL) {
        sUnloadingScreen = from;
        from->UnloadPanels();
    }

    FOREACH_POST (it, mPanelList) {
        if (it->Active() && it->mPanel->GetState() == UIPanel::kDown) {
            it->mPanel->Enter();
        }
    }

    static Message msg("enter", 0);
    msg[0] = from;
    HandleType(msg);
    Poll();
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
        if (!it->mPanel) {
            continue;
        }
        PanelDir *panelDir = it->mPanel->LoadedDir();
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
