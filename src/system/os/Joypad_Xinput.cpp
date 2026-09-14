#include "os\Joypad_Xinput.h"
#include "os\Joypad_Xbox.h"
#include "obj\Data.h"
#include "os\CritSec.h"
#include "os\Joypad.h"
#include "os\UserMgr.h"
#include "os\UsbMidiKeyboard.h"
#include "xdk\XAPILIB.h"
#include "xdk\xapilibi\winerror.h"

namespace {
    XINPUT_CAPABILITIES gCaps[kNumJoypads];
    float gXboxDeadzone;
    bool gCapsValid[kNumJoypads];
    CriticalSection gCritSection;
}

void JoypadInitXboxPCDeadzone(DataArray *arr) {
    arr->FindData("deadzone", gXboxDeadzone);
    gXboxDeadzone /= 256.0f;
}

void TranslateStick(char *keys, short s, bool param_a, bool param_b) {
    float var1 = (s + 0.5f) * 0.000030518044f; // this should be / 32768

    if (param_b) {
        if (var1 > gXboxDeadzone) {
            var1 = (var1 - gXboxDeadzone) / (1 - gXboxDeadzone);
        } else if (var1 < -gXboxDeadzone) {
            var1 = (var1 + gXboxDeadzone) / (1 - gXboxDeadzone);
        } else {
            var1 = 0;
        }
    }
    char c = (var1 * 127);
    *keys = c;

    if (param_a) {
        *keys = -c;
    }
}

void TranslateButtons(unsigned int *buttons, unsigned short s) {
    static int var2[16] = { 0xC, 0xE, 0xF, 0xD, 0xB, 8, 9, 0xA, 2, 3, 0, 0, 6, 5, 7, 4 };
    *buttons = 0;

    for (int i = 0; i < 16; i++) {
        if (s & 1 << i) {
            *buttons = 1 << var2[i] | *buttons;
        }
    }
}

bool JoypadGetCachedXInputCaps(int pad, XINPUT_CAPABILITIES *caps, bool b3) {
    if (gCapsValid[pad] && !b3) {
        *caps = gCaps[pad];
    } else {
        CritSecTracker tracker(&gCritSection);
        if (XInputGetCapabilities(pad, 0, caps) == ERROR_SUCCESS) {
            gCaps[pad] = *caps;
            gCapsValid[pad] = true;
        } else
            return false;
    }
    return true;
}

void JoypadResetXboxPC(int pad) {
    ResetAllUsersPads();
    if (TheUserMgr && TheUserMgr->GetBool()) {
        std::vector<LocalUser *> users;
        TheUserMgr->GetLocalUsers(users);
        for (int i = 0; i < pad; i++) {
            if (i >= users.size())
                break;
            AssociateUserAndPad(users[i], i);
        }
    }
}

// The drum-pedal curve constants, read straight out of the image's literal
// pool: __real@41d80000 = 27.0f, __real@42f40000 = 122.0f,
// __real@3c2c7692 = 0.010526316f (= 1/95, the width of the 27..122 window)
// and __real@c6cf5600 = -26539.0f.  All four are loaded ONCE, before the
// drums test at 0x825FCF1C, and shared by both clamp blocks.
static const float kPedalLo = 27.0f;
static const float kPedalHi = 122.0f;
static const float kPedalScale = 0.010526316f;
static const float kPedalRange = -26539.0f;

JoypadType ReadSingleXinputJoypad(
    int pad,
    int user_idx,
    unsigned int *buttons,
    char *stick_lx,
    char *stick_ly,
    char *stick_rx,
    char *stick_ry,
    char *ltrigger,
    char *rtrigger,
    float *const pad_float_a,
    float *const pad_float_b,
    unsigned char *const out_char_a
) {
    XINPUT_STATE state;
    XINPUT_CAPABILITIES caps;
    JoypadType joypad_type = kJoypadAnalog;

    // The third argument is `buttons` itself, not a scratch local -- this is
    // where the button word gets filled in.  The image never materialises an
    // address for it: r5 arrives holding `buttons`, is copied to the
    // callee-saved r24 at 0x825FCDF4 `mr r24, r5`, and is left UNTOUCHED
    // through the `bl` at 0x825FCE10, so it is still the third argument.  Ours
    // passed `&unused` and threw the result away.
    GetXinputSinceLastFrame(user_idx, &state, buttons);

    if (-1 == state.dwPacketNumber) {
        return kJoypadNone;
    }
    unsigned char setup_flag = 0;

    // A caps lookup that fails, and a SubType the switch does not name, both
    // fall THROUGH to the stick handling at .L_825FCEE4 with joypad_type still
    // kJoypadAnalog -- they are not early returns. 0x825FCE40 `beq .L_825FCEE4`
    // is the caps-failure edge and 0x825FCE7C `bne cr6, .L_825FCEE4` is the
    // switch default; the only `return kJoypadNone` inside the switch is
    // SetupHXGuitar's zero result at 0x825FCED4, which branches back to
    // .L_825FCE20. There is also no `caps_type != 0` test in the image: 0 is
    // simply an unnamed SubType and takes the default edge.
    if (JoypadGetCachedXInputCaps(user_idx, &caps, false)) {
        unsigned char caps_type = ((unsigned char *)&caps)[1];
        switch (caps_type) {
        case 6:
        case 11:
            joypad_type = SetupHXGuitar(pad, caps);
            if (joypad_type == kJoypadNone) {
                return kJoypadNone;
            }
            if (joypad_type != kJoypadXboxButtonGuitar) {
                setup_flag = 1;
            }
            break;
        case 7:
            joypad_type = (JoypadType)7;
            setup_flag = 1;
            break;
        case 8:
            joypad_type = SetupHXDrums(pad, caps);
            break;
        case 9:
            joypad_type = (JoypadType)11;
            break;
        case 15:
            joypad_type = SetupHXKeytar(pad, caps);
            break;
        case 25:
            joypad_type = SetupHXRealGuitar(pad, caps);
            break;
        }
    }

    short lx = state.Gamepad.sThumbLX;
    unsigned char deadzone_apply = (setup_flag == 0) ? 1 : 0;
    TranslateStick(stick_lx, lx, 0, deadzone_apply);

    // The drum-pedal remap is applied to sThumbLY and sThumbRX, and stick_ry
    // gets the plain call -- we had LY and RY swapped.  In
    // build/373307D9/asm/system/os/Joypad_Xinput.s the four TranslateStick
    // calls are, in order: 0x825FCF00 r3=r29 (arg 6, stick_lx) with
    // `lhz r4, 0x68(r1)` = sThumbLX; 0x825FCF90/0x825FCFA4 r3=r28 (arg 7,
    // stick_ly) with `lhz r4, 0x6a(r1)` = sThumbLY and the drums clamp;
    // 0x825FD024 r3=r27 (arg 8, stick_rx) with `lhz r7, 0x6c(r1)` =
    // sThumbRX and the same clamp; 0x825FD04C r3=r25 (arg 9, stick_ry)
    // with `lhz r4, 0x6e(r1)` = sThumbRY and the setup_flag||drums deadzone.
    short ly = state.Gamepad.sThumbLY;
    if ((joypad_type == kJoypadXboxDrums) && (ly > 0) && (ly < 0x100)) {
        float f = (float)ly;
        float f2 = (kPedalLo - f >= 0.0f) ? kPedalLo : f;
        float f3 = (f2 - kPedalHi >= 0.0f) ? kPedalHi : f2;
        int scaled = (int)((f3 - kPedalLo) * kPedalScale * kPedalRange);
        short result = (short)(-0x8000 - scaled);
        TranslateStick(stick_ly, result, 1, 0);
    } else {
        TranslateStick(stick_ly, ly, 1, deadzone_apply);
    }

    short rx = state.Gamepad.sThumbRX;
    if (joypad_type == kJoypadXboxDrums && (rx > 0) && (rx < 0x100)) {
        float f = (float)rx;
        float f2 = (kPedalLo - f >= 0.0f) ? kPedalLo : f;
        float f3 = (f2 - kPedalHi >= 0.0f) ? kPedalHi : f2;
        int scaled = (int)((f3 - kPedalLo) * kPedalScale * kPedalRange);
        short result = (short)(-0x8000 - scaled);
        TranslateStick(stick_rx, result, 1, 0);
    } else {
        TranslateStick(stick_rx, rx, 1, 0);
    }

    unsigned char deadzone_apply2;
    if ((setup_flag != 0 || joypad_type == kJoypadXboxDrums)) {
        deadzone_apply2 = 0;
    } else {
        deadzone_apply2 = 1;
    }
    short ry = state.Gamepad.sThumbRY;
    TranslateStick(stick_ry, ry, 1, deadzone_apply2);

    if ((joypad_type == kJoypadXboxMidiBoxKeyboard) || (joypad_type == kJoypadXboxMidiBoxDrums)) {
        // A NAMED global and a DIRECT call, not a raw address and a vtable
        // slot: 0x825FD064 `lwz r3, "?TheKeyboard@@3PAVUsbMidiKeyboard@@A"@l(r11)`
        // and 0x825FD074 `bl "?GetSustain@UsbMidiKeyboard@@QAA_NH@Z"`.  The
        // null case falls into the SAME test with a materialised 0
        // (0x825FD07C `li r3, 0x0`), and `*buttons` is loaded once for both
        // arms at 0x825FD084.
        bool sustain = TheKeyboard != nullptr ? TheKeyboard->GetSustain(pad) : false;
        if (sustain) {
            *buttons |= 4;
        } else {
            *buttons &= 0xFFFFFFFB;
        }
    }

    if (joypad_type == kJoypadAnalog) {
        unsigned char lt = state.Gamepad.bLeftTrigger;
        unsigned char rt = state.Gamepad.bRightTrigger;
        unsigned char threshold = *(unsigned char *)0x83099C7C;

        if (lt > threshold) {
            *buttons |= 1;
        } else {
            *buttons &= 0xFFFFFFFE;
        }

        if (rt > threshold) {
            *buttons |= 2;
        } else {
            *buttons &= 0xFFFFFFFD;
        }
    }

    unsigned char lt = state.Gamepad.bLeftTrigger;
    unsigned char rt = state.Gamepad.bRightTrigger;
    *ltrigger = (lt >> 1) & 0x7F;
    *rtrigger = (rt >> 1) & 0x7F;

    return joypad_type;
}
