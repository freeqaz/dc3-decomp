#pragma once
#include "xdk\xaudio2\xapobase.h"

class HeadsetXferEffect;

// Empty parameter block: HeadsetPlaybackEffect carries no per-block state, it
// just publishes one zeroed block from its constructor. sizeof == 1 (an empty
// struct), confirmed twice in the target: `li r6, 0x1` (uParameterBlockByteSize)
// in ??0?$CSampleXAPOBase@VHeadsetPlaybackEffect@@... at 0x82E441C8, and the
// single `stb` plus `li r5, 0x1` at the SetParameters call site in
// ??0HeadsetPlaybackEffect@@QAA@PAPAVHeadsetXferEffect@@@Z.
struct HeadsetPlaybackEffectParams {};

// Mixes the four per-player HeadsetXferEffect capture rings into one output
// block: four consecutive 256-frame mono runs, reading whichever half of each
// ring the transfer side is not currently filling. Layout:
//   0x00  CXAPOParametersBase          (0x40)
//   0x40  CSampleXAPOBase::mParams[3]  (3 * 1)
//   0x43  CSampleXAPOBase::mWav        (WAVEFORMATEX, 0x12)
//   0x58  mXfer[4]
//   0x68  mCounter
// Total 0x6c, confirmed by the `li r3, 0x6c` feeding the `operator new` that
// precedes the constructor call in MicManagerXbox::Init at 0x82E41518.
class __declspec(uuid("b4d4c8aa-a20d-40a1-84a7-64193551a9bf")) HeadsetPlaybackEffect
    : public ATG::CSampleXAPOBase<HeadsetPlaybackEffect, HeadsetPlaybackEffectParams> {
public:
    HeadsetPlaybackEffect(HeadsetXferEffect **);

    virtual void DoProcess(
        const HeadsetPlaybackEffectParams &, float *__restrict, unsigned int, unsigned int
    );

private:
    HeadsetXferEffect *mXfer[4]; // 0x58
    int mCounter; // 0x68
};
