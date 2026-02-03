#include "meta/HeldButtonPanel.h"
#include "meta/ButtonHolder.h"
#include "obj/Object.h"
#include "os/JoypadMsgs.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"

HeldButtonPanel::HeldButtonPanel()
    : mHolder(new ButtonHolder(this, nullptr)), mHandling(false) {}

HeldButtonPanel::~HeldButtonPanel() { delete mHolder; }

BEGIN_HANDLERS(HeldButtonPanel)
    HANDLE_MESSAGE(ProcessedButtonDownMsg)
    if (!mHandling)
        HANDLE_MEMBER_PTR(mHolder)
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS

void HeldButtonPanel::Enter() {
    std::vector<ActionRec> recs;
    static Symbol held_buttons("held_buttons");
    DataArray *heldButtonsArr = TypeDef()->FindArray(held_buttons, false);
    if (heldButtonsArr) {
        for (int i = 1; i < heldButtonsArr->Size(); i++) {
            DataArray *el = heldButtonsArr->Array(i);
            MILO_ASSERT(el, 0x27);
            float duration = el->Float(1);
            if (duration > 0) {
                ActionRec rec((JoypadAction)el->Int(0), duration, TheUserMgr);
                recs.push_back(rec);
            }
        }
    }
    mHolder->SetHoldActions(recs);
    UIPanel::Enter();
}

void HeldButtonPanel::Exit() {
    std::vector<ActionRec> recs;
    mHolder->SetHoldActions(recs);
    UIPanel::Exit();
}

void HeldButtonPanel::Poll() {
    if (TheUI->FocusPanel() == this)
        mHolder->Poll();
    else
        mHolder->ClearHeldButtons();
    UIPanel::Poll();
}

DataNode HeldButtonPanel::OnMsg(const ProcessedButtonDownMsg &msg) {
    if (msg.IsHeldDown()) {
        // Button held for longer than threshold; forward as held button message
        static Symbol on_button_held("on_button_held");
        static Message heldMsg(on_button_held, 0, 0, 0, 0);
        heldMsg[0] = msg.GetUser();
        heldMsg[1] = msg.GetButton();
        heldMsg[2] = msg.GetAction();
        heldMsg[3] = msg.GetPadNum();
        Handle(heldMsg, false);
    } else {
        static ButtonDownMsg downMsg(0, kPad_L2, kAction_None, 0);
        downMsg[0] = msg.GetUser();
        downMsg[1] = msg.GetButton();
        downMsg[2] = msg.GetAction();
        downMsg[3] = msg.GetPadNum();
        mHandling = true;
        Handle(downMsg, false);
        mHandling = false;
    }
    return 1;
}
