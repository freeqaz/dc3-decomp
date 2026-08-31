#pragma once
#include "xdk\xaudio2\xapobase.h"

class __declspec(uuid("b4d4c8aa-a20d-40a1-84a7-64193551a9be")) HeadsetXferEffect;

// The parameter block is a single pointer: the ctor publishes `this` through
// SetParameters so the consumer side can reach the ring buffer below.
// sizeof == 4, confirmed by the `li r6, 0x4` in
// ??0?$CSampleXAPOBase@VHeadsetXferEffect@@... and the `li r5, 0x4` at the
// SetParameters call site in ??0HeadsetXferEffect@@QAA@XZ.
struct HeadsetXferEffectParams {
    HeadsetXferEffect *effect;
};

// Headset voice-transfer XAPO. Layout:
//   0x000  CXAPOParametersBase          (0x40)
//   0x040  CSampleXAPOBase::mParams[3]  (3 * 4)
//   0x04c  CSampleXAPOBase::mWav        (WAVEFORMATEX, 0x12 padded to 0x14)
//   0x060  mBufferIndex
//   0x064  mBuffer                      (two 0x400 halves)
// Total 0x864.
class HeadsetXferEffect : public ATG::CSampleXAPOBase<HeadsetXferEffect, HeadsetXferEffectParams> {
public:
    HeadsetXferEffect();

    virtual void DoProcess(
        const HeadsetXferEffectParams &, float *__restrict, unsigned int, unsigned int
    );

private:
    int mBufferIndex; // 0x60
    unsigned char mBuffer[0x800]; // 0x64
};
