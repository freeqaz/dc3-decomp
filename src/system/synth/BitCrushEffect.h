#pragma once

#include "xdk\xaudio2\xaudio2.h"
class __declspec(uuid("d794c77c-d14d-470c-9346-b9be9ac4860b")) BitCrushEffect {
public:
    struct Params {
        Params() : unk0(false) {}
        bool unk0; // 0x0 (bypass)
        float unk4; // 0x4 (amount)
    };

    BitCrushEffect(IXAudioBatchAllocator *);
    void Process(float *, int, int);
    void SetParameters(BitCrushEffect::Params const &);
    void Reset();

    float mHoldPeriod;
    int mHoldCounter;
    float mHeldLeft;
    float mHeldRight;
};
