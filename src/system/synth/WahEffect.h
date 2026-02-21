#pragma once

#include "xdk/xaudio2/xaudio2.h"
class WahEffect {
public:
    struct Params {
        u32 unk0;
        float unk4;
        float unk8;
        float unkc;
        float unk10;
        float unk14;
        float unk18;
        float unk1c;
        bool unk20;
        float unk24;
    };

    WahEffect(IXAudioBatchAllocator *);
    void Reset();
    void Process(float *, int, int);
    void SetParameters(WahEffect::Params const &);

    float mGain;
    float mFreqLo;
    float mFreqHi;
    float mResonance;
    float mBandwidth;
    float mSweepRate;
    float mSweepRange;
    float mEnvAmount;
    float mStaticSweep;
    float mCurrentSweep;
    float mPrevEnv;
    int mSampleRate;
    float mPhase;
    float unk34;
    float unk38;
    float unk3c;
    float unk40;
    float mLastInput;
    float mLastOutput;
};
