#include "nativeui_host/GameBindings.h"

#include "obj/Task.h"
#include "os/Joypad.h"
#include "rndobj/Rnd.h"

namespace nativeui_host {

    // InputBridge.h mirrors the game's numbers so it needs no game header;
    // these hold the mirrors to the real enums.
    static_assert(kPadL2 == kPad_L2 && kPadR2 == kPad_R2, "pad button mirror");
    static_assert(kPadL1 == kPad_L1 && kPadR1 == kPad_R1, "pad button mirror");
    static_assert(kPadTri == kPad_Tri && kPadCircle == kPad_Circle, "pad button mirror");
    static_assert(kPadX == kPad_X && kPadSquare == kPad_Square, "pad button mirror");
    static_assert(kPadSelect == kPad_Select && kPadStart == kPad_Start, "pad button mirror");
    static_assert(kPadDUp == kPad_DUp && kPadDRight == kPad_DRight, "pad button mirror");
    static_assert(kPadDDown == kPad_DDown && kPadDLeft == kPad_DLeft, "pad button mirror");
    static_assert(
        kPadLStickUp == kPad_LStickUp && kPadLStickRight == kPad_LStickRight,
        "pad button mirror"
    );
    static_assert(
        kPadLStickDown == kPad_LStickDown && kPadLStickLeft == kPad_LStickLeft,
        "pad button mirror"
    );
    static_assert(
        kPadActionConfirm == kAction_Confirm && kPadActionCancel == kAction_Cancel,
        "pad action mirror"
    );
    static_assert(
        kPadActionUp == kAction_Up && kPadActionRight == kAction_Right, "pad action mirror"
    );
    static_assert(
        kPadActionDown == kAction_Down && kPadActionLeft == kAction_Left, "pad action mirror"
    );
    static_assert(
        kPadActionPageUp == kAction_PageUp && kPadActionPageDown == kAction_PageDown,
        "pad action mirror"
    );

    double GameUiSeconds() { return (double)TheTaskMgr.UISeconds(); }

    bool OnButtonDown(NativeUiHost &host, const ButtonDownMsg &msg) {
        return host.OnButtonDown((int)msg.GetButton(), (int)msg.GetAction());
    }

    void OnButtonUp(NativeUiHost &host, const ButtonUpMsg &msg) {
        host.OnButtonUp((int)msg.GetButton(), (int)msg.GetAction());
    }

    void SetOutputSize(NativeUiHost &host) { host.SetOutputSize(TheRnd.Width(), TheRnd.Height()); }

}
