#include "os\Joypad_Xinput.h"
#include "os\Joypad_Xbox.h"
#include "obj\Data.h"
#include "os\CritSec.h"
#include "os\Joypad.h"
#include "os\UserMgr.h"
#include "os\UsbMidiKeyboard.h"
#include "xdk\XAPILIB.h"
#include "xdk\xapilibi\winerror.h"

// This TU's anonymous namespace is a measured FLOOR for two functions, and it
// is not a source problem.  config/373307D9/symbols.txt spells gXboxDeadzone
// `?gXboxDeadzone@?A@@3MA` -- retail's HASHLESS anonymous-namespace form, used
// by only 2 of the 526 anon-namespace entries in the whole config (the other
// is ?sDepthRectVerts in rnddx9/Rnd).  MSVC emits `?A0xf503845b@@` here, and
// obj_anon_ns_patcher.py cannot reconcile the two: it rewrites hashes in place
// over exactly 8 hex characters so nothing in the object moves, and `?A0x<h>@@`
// is 12 bytes against `?A@@`'s 4.  The patcher documents the case and reports
// it rather than dropping it silently.
//
// Consequence: JoypadInitXboxPCDeadzone (99.29) and TranslateStick (99.767)
// are INSTRUCTION-IDENTICAL to the image -- 28/28 and 43/43 equal -- and every
// charged row is a relocation NAME on this one symbol.  No source spelling
// closes them; a named namespace or a file `static` produces a third
// mangling, not `?A@@`.
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

// The drum-pedal curve constants are LITERALS, not file-scope statics: the
// image loads them out of the literal pool (__real@41d80000 = 27.0f,
// __real@42f40000 = 122.0f, __real@3c2c7692 = 0.010526316f = 1/95, the width
// of the 27..122 window, and __real@c6cf5600 = -26539.0f), all four hoisted
// above the drums test at 0x825FCF1C and shared by both clamp blocks.  Named
// `static const float`s made us reference the data symbols instead
// (`lfs f10, 0x4(r9)` is literally kPedalLo + 4), which is five [sym]
// relocation-name rows.
//
// NEGATIVE RESULT (w7-ap, 2026-09-14, 92.5 canonical): writing the scale as
// `/ 95.0f * -26539.0f` does NOT split the multiply.  The image keeps two
// (0x825FCF78 `fmuls f0, f0, f9` then 0x825FCF7C `fmuls f0, f0, f8`); our
// /fp:fast build rewrites the division to a reciprocal and then folds it into
// the following constant, giving one `fmuls` against -279.3578.  Two rows per
// clamp block remain on that account.
//
// NEGATIVE RESULT (w7-bl, 94.8 canonical): spelling the reciprocal as the
// image's own literal -- `* 0.010526316f * -26539.0f`, i.e. exactly
// __real@3c2c7692 followed by __real@c6cf5600 -- folds the same way, to the
// same single `fmuls` against __real@c38badcf.  Identical 212-row
// 33/3/5/4 split.  So it is not the DIVISION that MSVC is folding; it
// reassociates two adjacent float literal multiplies under /fp:fast whatever
// the first one is spelled as.  This is the whole of the missing 4th literal
// (`lis r6, __real@41d80000` / `lfs f8, __real@c6cf5600`) and the r6/r9/r10
// renumbering of the other three pool bases -- 5 rows, plus 1 per clamp block.
//
// RESIDUAL (w7-bl, 94.8 canonical, 45 of 212 rows) -- the rest, all measured:
//  * the two `-0x8000 - (unsigned short)(int)` arguments (6 rows: 108-110 and
//    140-142).  The image narrows the INPUT (`lhz r11, 0x56(r1)`) and passes
//    the subtraction straight to r4; we load the whole fctiwz word
//    (`lwz r11, 0x54(r1)`) and narrow the OUTPUT with a trailing `extsh`.
//    Both are correct mod 2^16 and TranslateStick sign-extends its own
//    argument at 0x825FCBE0, so the caller-side narrowing is optional and
//    MSVC picks the other end.  Three spellings measured inert at 94.8 with
//    an identical row split: a named `unsigned short` local for the fctiwz
//    result; casting the whole argument `(unsigned short)(-0x8000 - u)`; and
//    (w7-ap, earlier) a named `short` local, which is worse.
//  * r24 <-> r25 (9 rows): the image copies `buttons` to r24 and `stick_ry`
//    to r25, we do the reverse.  Both are plain `mr` saves of incoming
//    argument registers in the prologue; nothing in the source orders them.
//  * r9 <-> r10 (11 rows): the image colours `rt` r9 and `lt` r10 in the
//    trigger tail, we do the reverse.  Swapping the two declarations in BOTH
//    blocks (the kJoypadAnalog one and the tail one) is byte-inert.
//  * r31 <-> r7 (5 rows): `rx` is held in the callee-saved r31 for us and in
//    the volatile r7 in the image, which is the same one liveness decision
//    seen twice.

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
            // 0x825FCED8 `cmpwi cr6, r30, 0x20` -- the image compares against
            // 32, NOT kJoypadXboxButtonGuitar (30).  SetupHXGuitar only ever
            // returns 5, 6 or 0x1d, so neither spelling is ever true at
            // runtime; the constant is written as the image has it.
            if (joypad_type != (JoypadType)32) {
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
    // bool, not unsigned char: 0x825FCEF0 `cntlzw r11, r31` /
    // 0x825FCEF8 `extrwi r8, r11, 1, 26` feeds TranslateStick's bool argument
    // DIRECTLY.  An unsigned char makes MSVC re-normalise it at every call
    // site with the `subic`/`subfe` pair.
    bool deadzone_apply = (setup_flag == 0);
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
        float f2 = (27.0f - f >= 0.0f) ? 27.0f : f;
        float f3 = (f2 - 122.0f >= 0.0f) ? 122.0f : f2;
        // A DIVISION by 95.0f, not a multiply by 0.010526316f: /fp:fast
        // rewrites the division in the code generator, AFTER constant folding,
        // so the reciprocal can never merge with the -26539.0f that follows.
        // The image keeps both multiplies (0x825FCF78 `fmuls f0, f0, f9` then
        // 0x825FCF7C `fmuls f0, f0, f8`); a folded literal gives one.
        // 95 is exactly 122 - 27, the width of the clamp window.
        //
        // The result is narrowed to an UNSIGNED SHORT and fed straight to the
        // call: 0x825FD000 `lhz r11, 0x56(r1)` takes only the low halfword of
        // the fctiwz word at 0x54(r1), zero-extended, and 0x825FD004
        // `subfic r4, r11, -0x8000` is the argument.  A named `short result`
        // local adds the `extsh` we used to emit.
        TranslateStick(
            stick_ly,
            -0x8000 - (unsigned short)(int)((f3 - 27.0f) / 95.0f * -26539.0f),
            1,
            0
        );
    } else {
        TranslateStick(stick_ly, ly, 1, deadzone_apply);
    }

    short rx = state.Gamepad.sThumbRX;
    if (joypad_type == kJoypadXboxDrums && (rx > 0) && (rx < 0x100)) {
        float f = (float)rx;
        float f2 = (27.0f - f >= 0.0f) ? 27.0f : f;
        float f3 = (f2 - 122.0f >= 0.0f) ? 122.0f : f2;
        TranslateStick(
            stick_rx,
            -0x8000 - (unsigned short)(int)((f3 - 27.0f) / 95.0f * -26539.0f),
            1,
            0
        );
    } else {
        // The image's fallback passes param_b = deadzone_apply (0x825FD01C
        // `mr r6, r8`), not 1 and 0 -- so a plain analog pad had its
        // right-stick X deadzone SKIPPED and an unwanted flag set.
        //
        // param_a is NOT a constant 0 here (w7-bl: the earlier note calling
        // that edge "not a sane source expression" is refuted).  Three edges
        // reach this call and the image distinguishes them:
        //   0x825FCFAC `bne cr6, .L_825FD014` -- not drums: r5 = 0;
        //   0x825FCFB8 `ble .L_825FD00C` / 0x825FCFBC `bge cr6, .L_825FD00C`
        //     -- drums but rx out of (0, 0x100): fall into
        //   0x825FD00C `li r5, 0x1` / 0x825FD010 `bgt cr6, .L_825FD018`,
        //     where cr6 still holds `rx vs 0x100` from 0x825FCFB4, so r5
        //     survives as 1 only when rx > 0x100.
        // i.e. param_a == (drums && rx > 0x100).  Written that way, as ONE
        // call, MSVC reproduces the whole shape including the hoisted
        // `cmpwi cr6, r11, 0x100` above the `ble` (92.46 -> 94.8 canonical).
        // Splitting it into a nested if/else with two calls instead is much
        // worse -- MSVC duplicates the tail rather than merging it (88.9).
        TranslateStick(
            stick_rx, rx, joypad_type == kJoypadXboxDrums && rx > 0x100, deadzone_apply
        );
    }

    unsigned char deadzone_apply2;
    if ((setup_flag != 0 || joypad_type == kJoypadXboxDrums)) {
        deadzone_apply2 = 0;
    } else {
        deadzone_apply2 = 1;
    }
    short ry = state.Gamepad.sThumbRY;
    TranslateStick(stick_ry, ry, 1, deadzone_apply2);

    // 0x825FD058 `cmpwi cr6, r30, 0x22` -- 34, kJoypadXboxKeytar, not
    // kJoypadXboxMidiBoxDrums (33).  UsbMidiKeyboard.cpp:48 groups the same
    // two types.
    if ((joypad_type == kJoypadXboxMidiBoxKeyboard) || (joypad_type == kJoypadXboxKeytar)) {
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
