#pragma once

#include "xdk/xaudio2/xaudio2.h"
class EQEffect {
public:
    struct BiquadBand {
        bool enabled;    // 0x00
        char _pad[3];    // 0x01
        float b0;        // 0x04
        float b1;        // 0x08
        float b2;        // 0x0C
        float a1;        // 0x10
        float a2;        // 0x14
        float z1;        // 0x18 - filter state
    };  // size = 0x1C (28 bytes)

    struct Params {
        u32 mActiveBands;
        float mBand1Freq;
        float mBand1Gain;
        float mBand1Q;
        float mBand2Freq;
        float mBand2Gain;
        float mBand2Q;
        float mBand3Freq;
        float mBand3Gain;
        float mBand3Q;
        float mBand4Freq;
        float mBand4Gain;
        float mBand4Q;
        float mBand5Freq;
        float mBand5Q;
    };

    EQEffect(IXAudioBatchAllocator *);
    void Reset();
    void Process(float *, int, int);
    void SetParameter(int, float);
    void SetParameters(EQEffect::Params const &);

    // Band parameters (fed from SetParameter)
    float mBand1Freq;       // 0x00
    float mBand1Gain;       // 0x04
    float mBand1Q;          // 0x08
    float mBand2Freq;       // 0x0C
    float mBand2Gain;       // 0x10
    float mBand2Q;          // 0x14
    float mBand3Freq;       // 0x18
    float mBand3Gain;       // 0x1C
    float mBand3Q;          // 0x20
    float mBand4Freq;       // 0x24
    float mBand4Gain;       // 0x28
    float mBand4Q;          // 0x2C
    float mBand5Freq;       // 0x30

    u32 mActiveBands;       // 0x34
    
    // Biquad filter coefficients for each band
    BiquadBand mBands[5];   // 0x38-0xC3: [0]=0x38-0x53, [1]=0x54-0x6F, [2]=0x70-0x8B, [3]=0x8C-0xA7, [4]=0xA8-0xC3
    // Total size: 0xC4 bytes
};
