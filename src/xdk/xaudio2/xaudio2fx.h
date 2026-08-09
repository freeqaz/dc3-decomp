#pragma once
#include "xdk\win_types.h"
#include "xdk\unknwn.h"

// XAudio2FX built-in audio effects (reverb / volume meter) — public XAPOFX C API.
// Only the reverb declarations the engine references are mirrored here; the
// implementations live in the XDK (xdk/xaudio2/reverb).

#pragma pack(push, 1)

// I3DL2 environmental reverb parameters (size 0x34). Matches the DirectX SDK layout.
struct XAUDIO2FX_REVERB_I3DL2_PARAMETERS { /* Size=0x34 */
    /* 0x0000 */ float WetDryMix;
    /* 0x0004 */ INT32 Room;
    /* 0x0008 */ INT32 RoomHF;
    /* 0x000c */ float RoomRolloffFactor;
    /* 0x0010 */ float DecayTime;
    /* 0x0014 */ float DecayHFRatio;
    /* 0x0018 */ INT32 Reflections;
    /* 0x001c */ float ReflectionsDelay;
    /* 0x0020 */ INT32 Reverb;
    /* 0x0024 */ float ReverbDelay;
    /* 0x0028 */ float Diffusion;
    /* 0x002c */ float Density;
    /* 0x0030 */ float HFReference;
};

// Native reverb parameters (size 0x38) consumed by SetEffectParameters.
struct XAUDIO2FX_REVERB_PARAMETERS { /* Size=0x38 */
    /* 0x0000 */ float WetDryMix;
    /* 0x0004 */ UINT32 ReflectionsDelay;
    /* 0x0008 */ BYTE ReverbDelay;
    /* 0x0009 */ BYTE RearDelay;
    /* 0x000a */ BYTE PositionLeft;
    /* 0x000b */ BYTE PositionRight;
    /* 0x000c */ BYTE PositionMatrixLeft;
    /* 0x000d */ BYTE PositionMatrixRight;
    /* 0x000e */ BYTE EarlyDiffusion;
    /* 0x000f */ BYTE LateDiffusion;
    /* 0x0010 */ BYTE LowEQGain;
    /* 0x0011 */ BYTE LowEQCutoff;
    /* 0x0012 */ BYTE HighEQGain;
    /* 0x0013 */ BYTE HighEQCutoff;
    /* 0x0014 */ float RoomFilterFreq;
    /* 0x0018 */ float RoomFilterMain;
    /* 0x001c */ float RoomFilterHF;
    /* 0x0020 */ float ReflectionsGain;
    /* 0x0024 */ float ReverbGain;
    /* 0x0028 */ float DecayTime;
    /* 0x002c */ float Density;
    /* 0x0030 */ float RoomSize;
    /* 0x0034 */ UINT32 WetDryMixPct;
};

#pragma pack(pop)

// Creates the XAudio2 reverb XAPO effect (returns its IUnknown*). XDK-provided.
// Undecorated (extern "C") symbol — matches the XDK leapfxlib export.
extern "C" HRESULT CreateAudioReverb(IUnknown **ppApo);

// Converts I3DL2 environmental parameters into native reverb parameters. XDK-provided.
void ReverbConvertI3DL2ToNative(
    const XAUDIO2FX_REVERB_I3DL2_PARAMETERS *pI3DL2,
    XAUDIO2FX_REVERB_PARAMETERS *pNative
);
