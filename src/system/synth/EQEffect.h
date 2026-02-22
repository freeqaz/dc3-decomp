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
    bool unk38;     // 0x38 - band 0 enabled
    float unk3c;    // 0x3c
    float unk40;    // 0x40
    float unk44;    // 0x44
    float unk48;    // 0x48
    float unk4c;    // 0x4c
    float unk50;    // 0x50
    bool unk54;     // 0x54 - band 1 enabled
    float unk58;    // 0x58
    float unk5c;    // 0x5c
    float unk60;    // 0x60
    float unk64;    // 0x64
    float unk68;    // 0x68
    float unk6c;    // 0x6c
    float unk70;    // 0x70
    bool unk74;     // 0x74 - band 2 enabled
    float unk78;    // 0x78
    float unk7c;    // 0x7c
    float unk80;    // 0x80
    float unk84;    // 0x84
    float unk88;    // 0x88
    float unk8c;    // 0x8c
    bool unk90;     // 0x90 - band 3 enabled
    float unk94;    // 0x94
    float unk98;    // 0x98
    float unk9c;    // 0x9c
    float unka0;    // 0xa0
    float unka4;    // 0xa4
    bool unka8;     // 0xa8 - band 4 enabled
    float unkac;    // 0xac
    float unkb0;    // 0xb0
    float unkb4;    // 0xb4
    float unkb8;    // 0xb8
    float unkbc;    // 0xbc
};
