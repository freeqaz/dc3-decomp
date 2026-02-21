#pragma once
#include "xdk/xaudio2/xaudio2.h"

class CompressionEffect {
public:
    struct Params {
        bool unk0;
        float unk4;
        float unk8;
        float unkc;
        float unk10;
        float unk14;
        float unk18;
        float unk1c;
        float unk20;
        float unk24;
    };

    CompressionEffect(IXAudioBatchAllocator *);
    void Reset();
    void Process(float *, int, int);
    void SetParameters(CompressionEffect::Params const &);

    float mThresholdRatio;
    float mThresholdDb;
    float mMakeupGainRatio;
    float mRatio;
    float mOutputGainRatio;
    float mAttackCoeff;
    float mReleaseCoeff;
    float mPostGain;
    float mPeakAttackCoeff;
    float mPeakReleaseCoeff;
    float mGateThreshDb;
    float mGateMin;
    float mGateMax;
    float mDCBlock;
    float mEnvelope;
    float mEnvelope2;
};
