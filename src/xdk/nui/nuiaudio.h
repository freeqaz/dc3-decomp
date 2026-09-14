#pragma once
#include "..\win_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PACKED in the real XDK.  LiveCameraInput::NuiAudioDataCallback reads
 * BeamAngle and Confidence with `lwz` into a GPR, bounces both through the
 * same stack slot and reloads them with `lfs` (0x82430680-A0) -- MSVC's
 * lowering for a float it cannot prove is naturally aligned.  Without the
 * pragma it emits a direct `lfs 0x8(r3)` / `lfs 0xc(r3)` and that function
 * cannot get past 90.8%; with it, 100.0%.  sizeof and every offset are
 * unchanged (4/4/4/4 with no tail padding), so this is an alignment
 * assertion only -- no layout moves. */
#pragma pack(push, 1)
typedef struct _NUIAUDIO_RESULTS { /* Size=0x10 */
    /* 0x0000 */ INT16 *pOutputMic;
    /* 0x0004 */ UINT32 Duration;
    /* 0x0008 */ FLOAT BeamAngle;
    /* 0x000c */ FLOAT Confidence;
} NUIAUDIO_RESULTS;
#pragma pack(pop)

typedef void NUIAUDIO_ERROR_CALLBACK(HRESULT);
typedef void NUIAUDIO_CALLBACK(NUIAUDIO_RESULTS *);

HRESULT NuiAudioCreate(
    UINT32 uHardwareThreadRequested,
    NUIAUDIO_ERROR_CALLBACK *pfnErrorCallback,
    DWORD Flags,
    HANDLE Handle,
    UINT32 *pHardwareThreadUsed
);
void NuiAudioRelease(const HANDLE Handle);
void NuiAudioRegisterCallbacks(
    const HANDLE Handle, DWORD Flags, NUIAUDIO_CALLBACK *pfnProcessingCallback
);
void NuiAudioUnregisterCallbacks(
    const HANDLE Handle, NUIAUDIO_CALLBACK *pfnProcessingCallback
);

HRESULT NuiAudioCreatePrivate(
    UINT32 uHardwareThreadRequested,
    NUIAUDIO_ERROR_CALLBACK *pfnErrorCallback,
    DWORD Flags,
    HANDLE Handle,
    UINT32 *pHardwareThreadUsed
);
void NuiAudioRegisterCallbacksPrivate(
    const HANDLE Handle, DWORD Flags, NUIAUDIO_CALLBACK *pfnProcessingCallback
);
void NuiAudioUnregisterCallbacksPrivate(
    const HANDLE Handle, NUIAUDIO_CALLBACK *pfnProcessingCallback
);

#ifdef __cplusplus
}
#endif
