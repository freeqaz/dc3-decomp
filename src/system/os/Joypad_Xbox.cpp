#include "os\Joypad_Xbox.h"
#include "obj\Data.h"
#include "os\CritSec.h"
#include "os\Debug.h"
#include "os\Joypad.h"
#include "os\Joypad_Xinput.h"
#include "os\System.h"
#include "xdk\XAPILIB.h"
#include "xdk\LIBCMT\ppcintrinsics.h"

// The order of this block is load bearing. The original .bss run is
//
//   tRawOutput    0x82f68ac0  0x08
//   tRawPending   0x82f68ac8  0x04 (+4 pad)
//   tUpstreamData 0x82f68ad0  0x40
//   tRawData      0x82f68b10  0x40
//   tInputStates  0x82f68b50  0x40
//   tBreed        0x82f68b90  0x30
//   sThreadData   0x82f68bc0  0x08
//   tButtonStatesCurr 0x82f68bc8 0x10
//   tButtonStatesPrev 0x82f68bd8 0x10
//   tNeedCaps     0x82f68be8  0x04
//   tCritSection  0x82f68bec  0x20
//
// and several functions address one of these through another with a baked-in
// displacement (ReadSingleJoypad reaches tRawData as tRawPending + 0x48,
// InitXinputJoypadThreadData reaches tNeedCaps as tInputStates + 0x98,
// ParseRawData reaches tRawPending as tRawData - 0x48). Reordering these
// declarations changes those immediates.
// MSVC lays .bss out in REVERSE declaration order (with alignment packing),
// so this block is written back-to-front relative to the original data run:
//
//   tRawOutput    0x82f68ac0  0x08   tBreed            0x82f68b90 0x30
//   tRawPending   0x82f68ac8  0x04   sThreadData       0x82f68bc0 0x08
//   tUpstreamData 0x82f68ad0  0x40   tButtonStatesCurr 0x82f68bc8 0x10
//   tRawData      0x82f68b10  0x40   tButtonStatesPrev 0x82f68bd8 0x10
//   tInputStates  0x82f68b50  0x40   tNeedCaps         0x82f68be8 0x04
//                                    tCritSection      0x82f68bec 0x20
//
// The order is load bearing: these are internal-linkage statics, so MSVC
// reaches one through another with a baked-in displacement rather than a
// second relocation (ReadSingleJoypad reads tRawData as tRawPending + 0x48,
// InitXinputJoypadThreadData writes tNeedCaps as tInputStates + 0x98,
// ParseRawData writes tRawPending as tRawData - 0x48). Anonymous-namespace
// variables are external in MSVC and never get folded that way, which is why
// only tBreed and sThreadData -- the two the target names -- live in one.
namespace {
    // Stays in the anonymous namespace: its ??__E/??__F initializer and atexit
    // thunks carry the namespace decoration in the target, and nothing reaches
    // it by displacement, so it does not need internal linkage.
    CriticalSection tCritSection;
}
// Pad needs its XInput capabilities re-queried before it can be read.
// The align(8) is a PLACEMENT WORKAROUND, not something recovered from the
// target: tRawOutput(8) + tRawPending(4) leaves a four byte hole ahead of the
// 8-aligned tUpstreamData, and MSVC packs the only four byte static it can
// find -- this one -- into it, which puts tNeedCaps 0x84 *below* tInputStates
// instead of 0x98 above it and breaks InitXinputJoypadThreadData's baked-in
// displacement. The original TU leaves that hole empty; presumably it had no
// candidate to fill it. Over-aligning is how we say "not there" in source.
__declspec(align(8)) static bool tNeedCaps[kNumJoypads];
static unsigned int tButtonStatesPrev[kNumJoypads];
static unsigned int tButtonStatesCurr[kNumJoypads];
// Thread handle and termination flag grouped for proper codegen.
// File-scope static rather than anonymous-namespace, so RunXinputJoypadLoop
// can reach tNoHandle as tButtonStatesPrev - 0x14 the way the target does.
static struct {
    HANDLE tThread;
    bool tNoHandle;
} sThreadData;
namespace {
    BreedData tBreed[kNumJoypads];
}
static XINPUT_STATE tInputStates[kNumJoypads];
static unsigned char tRawData[kNumJoypads][16];
static unsigned char tUpstreamData[kNumJoypads][16];
// Set when a pad has an unread upstream response waiting in tUpstreamData.
static bool tRawPending[kNumJoypads];
// Downstream packet staged by SendRawData: report id + seven payload bytes.
static unsigned char tRawOutput[8];

// Macros to access thread data - required for matching symbol offsets
#define tThread sThreadData.tThread
#define tNoHandle sThreadData.tNoHandle

// Retrieves XInput state and button changes since last frame
// Combines translated buttons with accumulated button state, then resets current state
void GetXinputSinceLastFrame(int pad, XINPUT_STATE *state, unsigned int *buttons) {
    CritSecTracker tracker(&tCritSection);
    unsigned int translatedButtons;
    *state = tInputStates[pad];
    TranslateButtons(&translatedButtons, tInputStates[pad].Gamepad.wButtons);
    *buttons = tButtonStatesCurr[pad] | translatedButtons;
    tButtonStatesPrev[pad] = tButtonStatesCurr[pad];
    tButtonStatesCurr[pad] = 0;
}

// Cleanly terminates the XInput polling thread
void XinputJoypadThreadDestruction() {
    tNoHandle = true;
    WaitForSingleObject(tThread, INFINITE);
    CloseHandle(tThread);
    tThread = 0;
}

void JoypadReset() { JoypadResetXboxPC(4); }

void JoypadTerminate() {
    XinputJoypadThreadDestruction();
    JoypadTerminateCommon();
}

void JoypadPoll() { JoypadPollCommon(); }

// Declared extern "C" in Joypad.h, so this gets C linkage and matches the
// target's unmangled `JoypadSendKeepAlive` at .text:0x825EC908. The target
// body is a single instruction -- `b XamInputSendStayAliveRequest` -- i.e. a
// tail call that forwards the pad bitmask untouched and discards the result.
void JoypadSendKeepAlive(int pad_mask) { XamInputSendStayAliveRequest(pad_mask); }

// Hands the caller the raw HID report last parked by ParseRawData and then
// defers to the shared XInput reader for everything else. Declared extern "C"
// in Joypad.h, so this is the unmangled `ReadSingleJoypad`.
int ReadSingleJoypad(
    int pad,
    unsigned int *buttons,
    char *lx,
    char *ly,
    char *rx,
    char *ry,
    char *lt,
    char *rt,
    float *sensors,
    float *pressures,
    unsigned char *pro_guitar
) {
    if (pad >= kNumJoypads)
        return kJoypadNone;
    for (int i = 0; i < 16; i++) {
        pro_guitar[i] = tRawData[pad][i];
    }
    if (tRawPending[pad]) {
        tRawPending[pad] = false;
    }
    return ReadSingleXinputJoypad(
        pad, pad, buttons, lx, ly, rx, ry, lt, rt, sensors, pressures, pro_guitar
    );
}

JoypadType SetupHXKeytar(int, const XINPUT_CAPABILITIES &c) {
    if ((c.Gamepad.sThumbLY & 0xFFF0U) == 0x1730) {
        return kJoypadXboxMidiBoxKeyboard;
    } else
        return kJoypadXboxKeytar;
}

void ReceiveUpstreamLowPriorityOutputResponse(int pad, unsigned char *data) {
    MILO_LOG("Low Priority Output Report for controller %d:\n", pad);
    MILO_LOG("0x%02x 0x%02x 0x%02x\n", data[1], data[2], data[3]);
}

void ReceiveUpstreamBreedDataResponse(int pad, unsigned char *data) {
    if (JoypadGetPadData(pad)->mConnected) {
        MILO_LOG("Breed Data Response for controller %d\n", pad);
        MILO_LOG(
            "Vendor:      0x%02x\nProject:     0x%02x\nPeriph Type: 0x%02x\nPlatform:    0x%02x\nFactory:     0x%02x\nDesign Iter: 0x%02x\nManu Date(1):0x%02x\nManu Date(2):0x%02x\nIdent. v(1): 0x%02x\nIdent. v(2): 0x%02x\n",
            data[1],
            data[2],
            data[3],
            data[4],
            data[5],
            data[6],
            data[7],
            data[8],
            data[9],
            data[10]
        );
    }
    tBreed[pad].mVendor = data[1];
    tBreed[pad].mProject = data[2];
    tBreed[pad].mPeripheralType = data[3];
    tBreed[pad].mPlatform = data[4];
    tBreed[pad].mFactory = data[5];
    tBreed[pad].mDesignIter = data[6];
    tBreed[pad].mManuDate = data[8] * 0x100 + data[7];
    tBreed[pad].mIdent = data[10] * 0x100 + data[9];
    tBreed[pad].mPending = 0;
    JoypadHandleBreedDataResponse(pad);
}

void ReceiveUpstreamCalbertResponse(int pad, unsigned char *data) {
    MILO_LOG("Calbert Response for controller %d\n", pad);
    MILO_LOG("Sensor Output Mode: 0x%02x\n", data[1]);
}

void ReceiveUpstreamAccelerometerResponse(int pad, unsigned char *data) {
    MILO_LOG("Accelerometer Mode Response for controller %d\n", pad);
    MILO_LOG(
        "Accelerometer Output Mode: 0x%02x\nX axis resolution:         0x%02x\nY axis resolution:         0x%02x\nZ axis resolution:         0x%02x\n",
        data[1],
        data[2],
        data[3],
        data[4]
    );
}

void ReceiveUpstreamOutputModeResponse(int pad, unsigned char *data) {
    MILO_LOG("Output Mode Switch Response for controller %d\n", pad);
    MILO_LOG("Output Mode: 0x%02x\n", data[1]);
}

void ReceiveUpstreamDeviceStateResponse(int pad, unsigned char *data) {
    MILO_LOG("Device State Response for controller %d\n", pad);
    MILO_LOG("Battery Level: 0x%02x\nOutput Mode:   0x%02x\n", data[1], data[2]);
}

void ReceiveUpstreamEEPROMReadResponse(int pad, unsigned char *data) {
    MILO_LOG("EEPROM Read Response for controller %d\n", pad);
    MILO_LOG(
        "Offset (low):      0x%02x\nOffset (high):     0x%02x\nData Length:       0x%02x\n",
        data[1],
        data[2],
        data[3]
    );
    MILO_LOG(
        "Packet Payload Len:0x%02x\nEEPROM Data(1):    0x%02x\nEEPROM Data(2):    0x%02x\nEEPROM Data(3):    0x%02x\nEEPROM Data(4):    0x%02x\nEEPROM Data(5):    0x%02x\nEEPROM Data(6):    0x%02x\nEEPROM Data(7):    0x%02x\nEEPROM Data(8):    0x%02x\n",
        data[5],
        data[6],
        data[7],
        data[8],
        data[9],
        data[10],
        data[11],
        data[12],
        data[13]
    );
}

void ReceiveUpstreamEEPROMWriteResponse(int pad, unsigned char *data) {
    MILO_LOG("EEPROM Write Response for controller %d\n", pad);
    MILO_LOG(
        "Offset (low):       0x%02x\nOffset (high):      0x%02x\nData Length:        0x%02x\nStatus:             0x%02x\n",
        data[1],
        data[2],
        data[3],
        data[4]
    );
    MILO_LOG(
        "Packet Payload Len: 0x%02x\nEEPROM Data Echo(1):0x%02x\nEEPROM Data Echo(2):0x%02x\nEEPROM Data Echo(3):0x%02x\nEEPROM Data Echo(4):0x%02x\nEEPROM Data Echo(5):0x%02x\nEEPROM Data Echo(6):0x%02x\nEEPROM Data Echo(7):0x%02x\nEEPROM Data Echo(8):0x%02x\n",
        data[5],
        data[6],
        data[7],
        data[8],
        data[9],
        data[10],
        data[11],
        data[12],
        data[13]
    );
    JoypadHandleEepromWriteResponse(pad, (JoypadBreedDataStatus)(data[4] != 0));
}

// Stages an eight byte downstream HID report (report id 0x11 plus seven
// payload bytes) and pushes it to the pad as two DWORD writes.
void SendRawData(
    int pad,
    unsigned char b1,
    unsigned char b2,
    unsigned char b3,
    unsigned char b4,
    unsigned char b5,
    unsigned char b6,
    unsigned char b7
) {
    XINPUT2_HANDLE sample;
    DWORD flags;
    if (!XInput2Sample(pad, &sample, &flags)) {
        MILO_LOG(
            "No sample available in SendRawData, error 0x%08\n",
            (unsigned int)GetLastError()
        );
        return;
    }
    tRawOutput[0] = 0x11;
    tRawOutput[1] = b1;
    tRawOutput[2] = b2;
    tRawOutput[3] = b3;
    tRawOutput[4] = b4;
    tRawOutput[5] = b5;
    tRawOutput[6] = b6;
    tRawOutput[7] = b7;
    XInput2BeginUpdate(sample);
    if (!XInput2SetDWord(
            sample, XINPUTID_OUT_UNSPECIFIED_DWORD_0, ((DWORD *)tRawOutput)[0]
        )) {
        MILO_LOG("Error 0x%08x writing data 0\n", (unsigned int)GetLastError());
    } else if (!XInput2SetDWord(
                   sample, XINPUTID_OUT_UNSPECIFIED_DWORD_1, ((DWORD *)tRawOutput)[1]
               )) {
        MILO_LOG("Error 0x%08x writing data 1\n", (unsigned int)GetLastError());
    }
    XInput2EndUpdate(sample, 0);
}

BreedData *GetBreedData(int pad) {
    if (tBreed[pad].mPending) {
        SendRawData(pad, 0x81, 0, 0, 0, 0, 0, 0);
        return nullptr;
    } else {
        return &tBreed[pad];
    }
}

bool requestBreedWrite(int pad, unsigned char *pBreedWritePacket) {
    MILO_ASSERT(pBreedWritePacket, 0x301);
    SendRawData(
        pad,
        0xF3,
        pBreedWritePacket[0],
        pBreedWritePacket[1],
        pBreedWritePacket[2],
        pBreedWritePacket[3],
        pBreedWritePacket[4],
        pBreedWritePacket[5]
    );
    return true;
}

JoypadType SetupHXRealGuitar(int pad, const XINPUT_CAPABILITIES &c) {
    unsigned short us = (unsigned short)c.Gamepad.sThumbLY & 0xFFF0;
    bool u1 = us == 0x1530;
    bool u2 = us == 0x1430;
    if (!u1 && !u2)
        u2 = true;
    if (u1) {
        return kJoypadXboxRealGuitar22Fret;
    } else if (u2) {
        return kJoypadXboxButtonGuitar;
    } else {
        MILO_LOG("sThymbLY = %d does not correspond to subtype x19\n", c.Gamepad.sThumbLY);
        return kJoypadAnalog;
    }
}

JoypadType SetupHXGuitar(int pad, const XINPUT_CAPABILITIES &c) {
    bool u5 = c.Flags & 0x2;
    bool u1 = c.Flags & 1;
    bool u4 = u5 && (u1 || c.Gamepad.sThumbRX >= 0x100);
    JoypadGetPadData(pad)->mIsWireless = u5; // wireless?
    JoypadGetPadData(pad)->mHasCapFlag1 = u1;
    if (c.Gamepad.sThumbLX == 0x1BAD) {
        GetBreedData(pad);
        return kJoypadXboxCoreGuitar;
    } else
        return u4 ? kJoypadXboxHxGuitarRb2 : kJoypadXboxHxGuitar;
}

// Identifies drum controller type based on XInput capabilities
// Rock Band 2 drums have enhanced features when flag 2 is set,
// but require either flag 1 or a high sThumbRX value for identification
JoypadType SetupHXDrums(int pad, const XINPUT_CAPABILITIES &c) {
    bool hasFlag1 = c.Flags & 1;
    bool hasFlag2 = c.Flags & 0x2;
    bool isRb2Drums = hasFlag1 || c.Gamepad.sThumbRX >= 0x100;
    bool isRockOfAgesDrums = hasFlag2 && !hasFlag1;
    JoypadGetPadData(pad)->mIsWireless = hasFlag2;
    JoypadGetPadData(pad)->mHasCapFlag1 = hasFlag1;
    if (c.Gamepad.sThumbLX == 0x1BAD) {
        GetBreedData(pad);
        return kJoypadXboxMidiBoxDrums;
    }
    if (isRb2Drums) {
        return kJoypadXboxDrumsRb2;
    }
    if (isRockOfAgesDrums) {
        return kJoypadXboxRoDrums;
    }
    return kJoypadXboxDrums;
}

bool ReceiveUpstreamResponse(int pad, unsigned char *data) {
    switch (data[0]) {
    case 0x80:
        ReceiveUpstreamLowPriorityOutputResponse(pad, data);
        break;
    case 0x82:
        ReceiveUpstreamBreedDataResponse(pad, data);
        break;
    case 0x84:
        ReceiveUpstreamCalbertResponse(pad, data);
        break;
    case 0x86:
        ReceiveUpstreamAccelerometerResponse(pad, data);
        break;
    case 0x8A:
        ReceiveUpstreamOutputModeResponse(pad, data);
        break;
    case 0xC4:
        ReceiveUpstreamDeviceStateResponse(pad, data);
        break;
    case 0xF2:
        ReceiveUpstreamEEPROMReadResponse(pad, data);
        break;
    case 0xF4:
        ReceiveUpstreamEEPROMWriteResponse(pad, data);
        break;
    default:
        return false;
    }
    return true;
}

// Stashes the 16-byte HID report the pad just sent. Reports with bit 7 of
// byte 14 set are upstream responses to a downstream command: those go to the
// upstream mailbox (and are dispatched immediately), everything else is the
// ordinary per-frame raw state ReadSingleJoypad hands back. Returns true when
// the report was consumed as an upstream response.
bool ParseRawData(int pad, unsigned char *data) {
    if ((data[14] & 0x80) == 0x80) {
        for (int i = 0; i < 16; i++) {
            tUpstreamData[pad][i] = data[i];
        }
        __lwsync();
        tRawPending[pad] = true;
        if (ReceiveUpstreamResponse(pad, data))
            return true;
    }
    for (int i = 0; i < 16; i++) {
        tRawData[pad][i] = data[i];
    }
    return false;
}

namespace {
    void InitXinputJoypadThreadData();

    void RunXinputJoypadLoop();

    DWORD XinputJoypadThreadEntry(HANDLE) {
        InitXinputJoypadThreadData();
        RunXinputJoypadLoop();
        return 0;
    }

    // Polls every pad until XinputJoypadThreadDestruction sets tNoHandle.
    // Runs on its own thread, so the shared per-pad state is taken under
    // tCritSection for the whole sweep.
    void RunXinputJoypadLoop() {
        while (!tNoHandle) {
            {
                CritSecTracker tracker(&tCritSection);
                for (int pad = 0; pad < kNumJoypads; pad++) {
                    XINPUT_STATE state;
                    if (XInputGetState(pad, &state) != 0) {
                        // Nothing plugged in: force a capability re-query and a
                        // breed re-read for when it comes back.
                        tBreed[pad].mPending = true;
                        tInputStates[pad].dwPacketNumber = -1;
                        tNeedCaps[pad] = true;
                        continue;
                    }
                    if (state.dwPacketNumber == tInputStates[pad].dwPacketNumber
                        && tInputStates[pad].dwPacketNumber != -1
                        && JoypadGetPadData(pad)->mConnected) {
                        continue;
                    }
                    bool consumed = false;
                    XINPUT_CAPABILITIES caps;
                    if (JoypadGetCachedXInputCaps(pad, &caps, tNeedCaps[pad])) {
                        tNeedCaps[pad] = false;
                        XINPUT2_HANDLE sample;
                        DWORD flags;
                        if (!XInput2Sample(pad, &sample, &flags)) {
                            MILO_LOG("No sample available in RunXinputJoypadLoop\n");
                            continue;
                        }
                        XINPUT2_DEVICE_ID deviceId;
                        if (!XInput2GetDeviceId(sample, &deviceId)) {
                            MILO_LOG("Error getting device ID\n");
                            continue;
                        }
                        // Only the two Harmonix peripheral classes carry a raw
                        // HID payload worth reading.
                        if (memcmp(&deviceId, &XINPUTID_0F_CONTROLLER, 16) == 0
                            || memcmp(&deviceId, &XINPUTID_19_CONTROLLER, 16) == 0) {
                            unsigned char raw[16];
                            if (XInput2GetDWord(
                                    sample, XINPUTID_UNSPECIFIED_DWORD_0, (DWORD *)&raw[0]
                                )
                                && XInput2GetDWord(
                                    sample, XINPUTID_UNSPECIFIED_DWORD_1, (DWORD *)&raw[4]
                                )
                                && XInput2GetDWord(
                                    sample, XINPUTID_UNSPECIFIED_DWORD_2, (DWORD *)&raw[8]
                                )
                                && XInput2GetDWord(
                                    sample, XINPUTID_UNSPECIFIED_DWORD_3, (DWORD *)&raw[12]
                                )) {
                                consumed = ParseRawData(pad, raw);
                            } else {
                                MILO_LOG("Error reading data\n");
                            }
                        }
                    }
                    if (consumed) {
                        // The report was an upstream response, not pad state.
                        tInputStates[pad].dwPacketNumber = state.dwPacketNumber;
                        continue;
                    }
                    unsigned int translated;
                    TranslateButtons(&translated, state.Gamepad.wButtons);
                    tInputStates[pad] = state;
                    tButtonStatesCurr[pad] |=
                        (tButtonStatesPrev[pad] ^ translated) & translated;
                }
            }
            Sleep(4);
        }
    }

    // Puts every pad into the "nothing known yet" state the polling loop
    // expects: capabilities must be re-queried, the breed data is stale, and
    // no XInput packet has been seen.
    void InitXinputJoypadThreadData() {
        for (int i = 0; i < kNumJoypads; i++) {
            tNeedCaps[i] = true;
        }
        for (int i = 0; i < kNumJoypads; i++) {
            tBreed[i].mPending = true;
            tInputStates[i].dwPacketNumber = 0;
        }
    }
}

void XinputJoypadThreadStart() {
    tThread = CreateThread(nullptr, 0, XinputJoypadThreadEntry, nullptr, 4, nullptr);
    MILO_ASSERT(tThread, 0x266);
    SetThreadPriority(tThread, 2);
    XSetThreadProcessor(tThread, 1);
    ResumeThread(tThread);
}

void JoypadSetActuatorsImp(int, int, int) {}

void JoypadInit() {
    DataArray *cfg = SystemConfig("joypad");
    JoypadInitCommon(cfg);
    JoypadInitXboxPCDeadzone(cfg);
    JoypadReset();
    XinputJoypadThreadStart();
}
