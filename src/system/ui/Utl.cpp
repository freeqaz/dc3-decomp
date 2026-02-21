#include "ui/Utl.h"
#include "os/Joypad.h"
#include "ui/UI.h"

int PageDirection(JoypadAction act) {
    if (act == kAction_PageDown)
        return 1;
    if (act == kAction_PageUp)
        return -1;
    return 0;
}

bool IsNavAction(JoypadAction act) {
    return act == kAction_Up || act == kAction_Down || act == kAction_Left
        || act == kAction_Right;
}

int ScrollDirection(const ButtonDownMsg &msg, bool b1, bool b2, int i) {
    int button;
    int action;
    bool overload;

    action = msg.mData->Int(4);

    if (!b2) {
        button = msg.mData->Int(3);
        overload = TheUI->OverloadHorizontalNav((JoypadAction)action, (JoypadButton)button, b1);
        if (overload) {
            if (action == 6) {
                action = 9;
            } else if (action == 8) {
                action = 7;
            }
        }
    }

    int result;
    if (action == 9) {
        result = -i;
    } else if (action == 7) {
        result = i;
    } else if (action == 6 && i > 1) {
        result = -1;
    } else {
        result = 0;
    }
    return result;
}
