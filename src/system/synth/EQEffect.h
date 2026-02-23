#pragma once

#include "xdk/xaudio2/xaudio2.h"
class EQEffect {
public:
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

    // Biquad filter state per band (5 bands)
    // Each band: enabled(bool), b0, b1, b2, a1, a2, z1 (floats)
    bool mBand0Enabled;     // 0x38 - band 0 enabled
    float mBand0B0;    // 0x3c
    float mBand0B1;    // 0x40
    float mBand0B2;    // 0x44
    float mBand0A1;    // 0x48
    float mBand0A2;    // 0x4c
    float mBand0Z1;    // 0x50
    bool mBand1Enabled;     // 0x54 - band 1 enabled
    float mBand1B0;    // 0x58
    float mBand1B1;    // 0x5c
    float mBand1B2;    // 0x60
    float mBand1A1;    // 0x64
    float mBand1A2;    // 0x68
    float mBand1Z1;    // 0x6c
    float mBand1Z2;    // 0x70
    bool mBand2Enabled;     // 0x74 - band 2 enabled
    float mBand2B0;    // 0x78
    float mBand2B1;    // 0x7c
    float mBand2B2;    // 0x80
    float mBand2A1;    // 0x84
    float mBand2A2;    // 0x88
    float mBand2Z1;    // 0x8c
    bool mBand3Enabled;     // 0x90 - band 3 enabled
    float mBand3B0;    // 0x94
    float mBand3B1;    // 0x98
    float mBand3B2;    // 0x9c
    float mBand3A1;    // 0xa0
    float mBand3A2;    // 0xa4
    bool mBand4Enabled;     // 0xa8 - band 4 enabled
    float mBand4B0;    // 0xac
    float mBand4B1;    // 0xb0
    float mBand4B2;    // 0xb4
    float mBand4A1;    // 0xb8
    float mBand4A2;    // 0xbc
};
