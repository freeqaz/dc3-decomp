#pragma once
#include "..\win_types.h"
#include "minwinbase.h"
#include "wtypesbase.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _XINPUT_BATTERY_INFORMATION {
    BYTE BatteryType;
    BYTE BatteryLevel;
} XINPUT_BATTERY_INFORMATION, *PXINPUT_BATTERY_INFORMATION;

typedef struct _XINPUT_GAMEPAD {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD, *PXINPUT_GAMEPAD;

typedef struct _XINPUT_KEYSTROKE {
    WORD VirtualKey;
    WCHAR Unicode;
    WORD Flags;
    BYTE UserIndex;
    BYTE HidCode;
} XINPUT_KEYSTROKE, *PXINPUT_KEYSTROKE;

typedef struct _XINPUT_STATE {
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE, *PXINPUT_STATE;

typedef struct _XINPUT_VIBRATION {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION, *PXINPUT_VIBRATION;

typedef struct _XINPUT_CAPABILITIES {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XINPUT_GAMEPAD Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES, *PXINPUT_CAPABILITIES;

void XInputEnable(BOOL enable);
DWORD XInputGetAudioDeviceIds(
    DWORD dwUserIndex,
    LPWSTR pRenderDeviceId,
    UINT *pRenderCount,
    LPWSTR pCaptureDeviceId,
    UINT *pCaptureCount
);
DWORD XInputGetBatteryInformation(
    DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION *pBatteryInformation
);
DWORD XInputGetCapabilities(
    DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES *pCapabilities
);
DWORD XInputGetKeystroke(
    DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke
);
DWORD XInputGetKeystrokeEx(
    PDWORD pdwUserIndex, DWORD dwFlags, PXINPUT_KEYSTROKE pKeystroke
);
DWORD XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState);
DWORD XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration);

// Xam-side input entry point (import __imp_XamInputSendStayAliveRequest,
// .rdata:0x82000704; thunk .text:0x82EE5A84). Not part of the public XInput
// surface, but the title imports and calls it: JoypadSendKeepAlive
// (.text:0x825EC908) is a four-byte `b XamInputSendStayAliveRequest` tail
// call, which fixes the arity at one argument -- a second argument would
// have needed an `li r4` ahead of the branch and the function would not be
// one instruction long.
DWORD XamInputSendStayAliveRequest(DWORD dwUserMask);

// XInput2 ("sample") surface. A sample is a snapshot handle opened over one
// pad; fields are addressed with 16-byte semantic ids passed BY VALUE -- the
// target loads them with two `ld`s into r4/r5, which fixes the layout at two
// 64-bit words.
typedef void *XINPUT2_HANDLE;

typedef struct _XINPUT2_ID { /* Size=0x10 */
    /* 0x0000 */ ULONGLONG Lo;
    /* 0x0008 */ ULONGLONG Hi;
} XINPUT2_ID;

typedef struct _XINPUT2_DEVICE_ID { /* Size=0x10 */
    /* 0x0000 */ BYTE Id[16];
} XINPUT2_DEVICE_ID;

BOOL XInput2Sample(DWORD dwUserIndex, XINPUT2_HANDLE *phSample, DWORD *pdwFlags);
BOOL XInput2BeginUpdate(XINPUT2_HANDLE hSample);
BOOL XInput2EndUpdate(XINPUT2_HANDLE hSample, BOOL fCancel);
BOOL XInput2SetDWord(XINPUT2_HANDLE hSample, XINPUT2_ID Id, DWORD dwValue);
BOOL XInput2GetDWord(XINPUT2_HANDLE hSample, XINPUT2_ID Id, DWORD *pdwValue);
BOOL XInput2GetDeviceId(XINPUT2_HANDLE hSample, XINPUT2_DEVICE_ID *pDeviceId);

extern const XINPUT2_ID XINPUTID_OUT_UNSPECIFIED_DWORD_0;
extern const XINPUT2_ID XINPUTID_OUT_UNSPECIFIED_DWORD_1;
extern const XINPUT2_ID XINPUTID_UNSPECIFIED_DWORD_0;
extern const XINPUT2_ID XINPUTID_UNSPECIFIED_DWORD_1;
extern const XINPUT2_ID XINPUTID_UNSPECIFIED_DWORD_2;
extern const XINPUT2_ID XINPUTID_UNSPECIFIED_DWORD_3;
extern const XINPUT2_DEVICE_ID XINPUTID_0F_CONTROLLER;
extern const XINPUT2_DEVICE_ID XINPUTID_19_CONTROLLER;

#ifdef __cplusplus
}
#endif
