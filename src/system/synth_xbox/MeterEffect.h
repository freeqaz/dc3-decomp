#pragma once
#include "xdk\xaudio2\xapobase.h"

struct LevelData;

// The consumer hands the effect the LevelData array it wants metered -- one
// entry per channel, sizeof(LevelData) == 0x18, which is the 0x18 stride
// DoProcess walks. FxSendMeterEffect360::InitParams points this at
// FxSendMeterEffect::mChannels.
struct MeterEffectParams {
    LevelData *mLevelData;
};

class __declspec(uuid("b4d4c8aa-a20d-40a1-84a7-64193551a9cc")) MeterEffect : public ATG::CSampleXAPOBase<MeterEffect, MeterEffectParams> {
public:
    MeterEffect();

    virtual void OnSetParameters(const MeterEffectParams &);
    virtual void
    DoProcess(const MeterEffectParams &, float *__restrict, unsigned int, unsigned int);

private:
    float mSumSquares[6]; // 0x60 -- running sum of x^2 per channel
    float mPeak[6]; // 0x78 -- running |x| maximum per channel
    int mFrameCount; // 0x90 -- frames accumulated since the last publish
};
