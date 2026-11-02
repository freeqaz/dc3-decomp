#include "os\UsbMidiKeyboard.h"
#include "decomp.h"
#include "os\Debug.h"
#include "os\Joypad.h"
#include "os\UsbMidiKeyboardMsgs.h"

UsbMidiKeyboard *TheKeyboard;
bool UsbMidiKeyboard::mUsbMidiKeyboardExists = false;

namespace {
    bool gUseMidiPort = false;
    bool gForceDetectKeytar = false;
}

bool UsbMidiKeyboard::GetSustain(int pad) {
    return mSustain[pad];
}

int UsbMidiKeyboard::GetSlottedKeyVelocityFromExtended(int i, unsigned char *uc) {
    if (gUseMidiPort)
        return 0;
    if (i >= 1 && i <= 5) {
        switch (i) {
        case 1:
            return uc[3] & 0x7F;
        case 2:
            return uc[4] & 0x7F;
        case 3:
            return uc[5] & 0x7F;
        case 4:
            return uc[6] & 0x7F;
        case 5:
            return uc[7] & 0x7F;
        }
    }
    return 0;
}

void UsbMidiKeyboard::Poll() {
    if (gUseMidiPort)
        return;
    if (!TheKeyboard)
        return;

    for (int i = 0; i < 4; i++) {
        JoypadType ty = JoypadGetPadData(i)->mType;
        if (ty == kJoypadXboxMidiBoxKeyboard || ty == kJoypadPs3MidiBoxKeyboard
            || ty == kJoypadWiiMidiBoxKeyboard || ty == kJoypadXboxKeytar
            || ty == kJoypadPs3Keytar || ty == kJoypadWiiKeytar
            || gForceDetectKeytar) {
            JoypadData *padData = JoypadGetPadData(i);
            ProKeysData *proData = (ProKeysData *)&padData->mProGuitarData;
            // RESOLVED (w27-b, 98.900 -> 100.0): the image emits `li r26, 1`
            // (slotCounter) BETWEEN `lwz TheKeyboard` and `addi r29, r3, 0x34`
            // (proData), i.e. one slot before the addi.  Declaring slotCounter
            // first really does hoist the `li` to before the call (w7-r, idx 38
            // vs the image's 40), which is why order alone never worked.  The
            // addi's source position moves when the JoypadData pointer is named:
            // with `padData` in a local the addi is materialised one slot later
            // and the li lands in 40 from its natural place at the end.
            int slotCounter = 1;

            for (int note = 0x30; note - 0x30 < 25; note++) {
                bool pressed = (proData->unk0[(note - 0x30) / 8]
                                >> (7 - (note - 0x30) % 8))
                    & 1;

                bool storedPressed = TheKeyboard->GetKeyPressed(i, note);

                if (pressed != storedPressed) {
                    if (pressed) {
                        int extVel = TheKeyboard->GetSlottedKeyVelocityFromExtended(
                            slotCounter, proData->unk0
                        );
                        TheKeyboard->SetKeyVelocity(i, note, extVel);
                        slotCounter++;
                        KeyboardKeyPressedMsg msg(
                            note, TheKeyboard->GetKeyVelocity(i, note), i
                        );
                        SendMessage(msg);
                    } else {
                        TheKeyboard->SetKeyVelocity(i, note, 0);
                        KeyboardKeyReleasedMsg msg(note, i);
                        SendMessage(msg);
                    }
                    TheKeyboard->SetKeyPressed(i, note, pressed);
                } else {
                    if (pressed)
                        slotCounter++;
                }
            }

            bool sus = proData->mSustain;
            if (sus != TheKeyboard->GetSustain(i)) {
                TheKeyboard->SetSustain(i, sus);
                KeyboardSustainMsg msg(sus, i);
                SendMessage(msg);
            }

            bool stomped = proData->mStompPedal;
            if (stomped != TheKeyboard->GetStompPedal(i)) {
                TheKeyboard->SetStompPedal(i, stomped);
                KeyboardStompBoxMsg msg(stomped, i);
                SendMessage(msg);
            }

            int mod = proData->unkachar;
            if (mod != TheKeyboard->GetModVal(i)) {
                TheKeyboard->SetModVal(i, mod);
                KeyboardModMsg msg(mod, i);
                SendMessage(msg);
            }

            int exp = proData->mExpressionPedal;
            if (exp != TheKeyboard->GetExpressionPedal(i)) {
                TheKeyboard->SetExpressionPedal(i, exp);
                KeyboardExpressionPedalMsg msg(exp, i);
                SendMessage(msg);
            }

            int conn = proData->mConnectedAccessories;
            if (conn != TheKeyboard->GetConnectedAccessory(i)) {
                TheKeyboard->SetConnectedAccessories(i, conn);
                KeyboardConnectedAccessoriesMsg msg(conn, i);
                SendMessage(msg);
            }

            int lowhand = proData->mLowHandPlacement;
            if (lowhand != TheKeyboard->GetLowHandPlacement(i)) {
                TheKeyboard->SetLowHandPlacement(i, lowhand);
                KeyboardLowHandPlacementMsg msg(lowhand, i);
                SendMessage(msg);
            }

            // The image emits four separate fused rlwinm extractions and a flat
            // add chain (d<<2) + (c<<1) + (e<<3) + b.  The declaration order
            // below is the one that interleaves the loads and shifts the way the
            // image does: lbz 0xd, lbz 0xc, rlwinm 0xd, lbz 0xe, rlwinm 0xc,
            // lbz 0xb, rlwinm 0xe.  Measured declaration orders (w27-b, canonical):
            // B,E,C,D 100.0 | B,D,C,E 99.581 | D,B,C,E 99.165 | D,C,B,E 99.162 |
            // B,D,E,C 98.897 | B,C,D,E 98.687 | B,C,E,D 98.684 | E,B,D,C 98.485.
            // The sum keeps the image's term order (d<<2) + (c<<1) + (e<<3) + b.
            int hhB = proData->unkbbool;
            int hhE = proData->unkemiddle << 3;
            int hhC = proData->unkcbool << 1;
            int hhD = proData->unkdbool << 2;
            int highhand = hhD + hhC + hhE + hhB;
            if (highhand != TheKeyboard->GetHighHandPlacement(i)) {
                TheKeyboard->SetHighHandPlacement(i, highhand);
                KeyboardHighHandPlacementMsg msg(highhand, i);
                SendMessage(msg);
            }

            int accelAxisVal0 = proData->unkachar;
            int accelAxisVal1 = proData->unkbchar;
            int accelAxisVal2 = proData->unkcchar;
            if (accelAxisVal0 != TheKeyboard->GetAccelAxisVal(i, 0)
                || accelAxisVal1 != TheKeyboard->GetAccelAxisVal(i, 1)
                || accelAxisVal2 != TheKeyboard->GetAccelAxisVal(i, 2)) {
                TheKeyboard->SetAccelerometer(i, accelAxisVal0, accelAxisVal1, accelAxisVal2);
                KeysAccelerometerMsg msg(
                    accelAxisVal0, accelAxisVal1, accelAxisVal2, i
                );
                SendMessage(msg);
            }
        }
    }
}
