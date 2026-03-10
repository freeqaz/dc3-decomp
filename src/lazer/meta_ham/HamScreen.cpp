#include "HamScreen.h"
#include "HamUI.h"
#include "meta_ham/BlacklightPanel.h"
#include "meta_ham/HelpBarPanel.h"
#include "meta_ham/LetterboxPanel.h"
#include "meta_ham/ShellInput.h"
#include "meta_ham/UIEventMgr.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "os/Debug.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"

BEGIN_CUSTOM_HANDLERS(HamScreen)
    HANDLE(get, OnGet)
    _HANDLE_CHECKED(OnEventMsgCommon(_msg)) // lol
    HANDLE_SUPERCLASS(UIScreen)
END_CUSTOM_HANDLERS

void HamScreen::Enter(UIScreen *screen) {
    TheHamUI.InitPanels();
    HelpBarPanel *hbp = TheHamUI.GetHelpBarPanel();
    if (hbp->GetState() != UIPanel::kUp) {
        hbp->Enter();
    }
    LetterboxPanel *lbp = TheHamUI.GetLetterboxPanel();
    if (lbp && lbp->GetState() != UIPanel::kUp) {
        lbp->Enter();
    }
    BlacklightPanel *blp = TheHamUI.GetBlacklightPanel();
    if (blp && blp->GetState() != UIPanel::kUp) {
        blp->Enter();
    }
#ifdef HX_NATIVE
    // Force controller mode on first screen enter. This fires:
    //   - HelpBarPanel::EnterControllerMode() → activates controller_mode.flow
    //   - controller_mode_entered message → DTA hides gesture tutorial UI
    //   - in_controller_mode property → DTA reads this for flow decisions
    // Must happen after HelpBarPanel is up, before UIScreen::Enter DTA handlers.
    {
        static bool sControllerModeForced = false;
        if (!sControllerModeForced) {
            sControllerModeForced = true;
            ShellInput *si = TheHamUI.GetShellInput();
            if (si) {
                printf("DC3 Native: Forcing controller mode on first HamScreen enter\n");
                si->EnterControllerMode(true); // true = force past AllowController check
            }
        }
    }
#endif
    TheHamUI.GetShellInput()->UpdateInputPanel(mFocusPanel);
    UIScreen::Enter(screen);
}

void HamScreen::Exit(UIScreen *screen) { UIScreen::Exit(screen); }

bool HamScreen::Exiting() const { return UIScreen::Exiting(); }

bool HamScreen::IsEventDialogOnTop() const {
    UIPanel *event_dialog = TheHamUI.EventDialogPanel();
    MILO_ASSERT(event_dialog, 0x5f);
    return TheUI->CurrentScreen() == this && event_dialog->GetState() == UIPanel::kUp;
}

bool HamScreen::InComponentSelect() const {
    if (IsEventDialogOnTop()) {
        UIComponent *component = TheHamUI.EventDialogPanel()->FocusComponent();
        if (component) {
            return component->GetState() == UIComponent::kSelecting;
        }
    }
    if (TheHamUI.GetOverlayPanel()) {
        UIComponent *component = TheHamUI.GetOverlayPanel()->FocusComponent();
        if (component) {
            return component->GetState() == UIComponent::kSelecting;
        }
    }
    return UIScreen::InComponentSelect();
}

DataNode HamScreen::OnEventMsgCommon(const Message &msg) {
    if (TheUIEventMgr && TheUIEventMgr->HasActiveDialogEvent()) {
        DataNode handled = TheHamUI.GetShellInput()->Handle(msg, false);
        if (handled != DATA_UNHANDLED) {
            return handled;
        } else if (IsEventDialogOnTop()) {
            UIPanel *event_dialog = TheHamUI.EventDialogPanel();
            MILO_ASSERT(event_dialog, 0x4D);
            return event_dialog->Handle(msg, false);
        }
    }
    if (TheHamUI.GetOverlayPanel()) {
        DataNode handled = TheHamUI.GetShellInput()->Handle(msg, false);
        if (handled != DATA_UNHANDLED) {
            return handled;
        } else {
            return TheHamUI.GetOverlayPanel()->Handle(msg, false);
        }
    } else {
        return DATA_UNHANDLED;
    }
}
