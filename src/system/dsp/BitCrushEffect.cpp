#include "synth\BitCrushEffect.h"
#include "os\Debug.h"
#include "xdk\xaudio2\xaudio2.h"

BitCrushEffect::BitCrushEffect(IXAudioBatchAllocator *)
    : mHoldPeriod(0), mHoldCounter(0), mHeldLeft(0), mHeldRight(0) {}

void BitCrushEffect::SetParameters(BitCrushEffect::Params const &params) {
    mHoldPeriod = params.unk4;
}

void BitCrushEffect::Process(float *f, int numSamples, int numChans) {
    do {
        if (!(numChans <= 2)) {
            TheDebugFailer << MakeString(kAssertStr, "dsp\\BitCrushEffect.cpp", 0x1e, "numChans <= 2");
        }
    } while (0);

    if (numSamples > 0) {
        float *left;
        float *right;
        int stride;
        int ctr;

        ctr = numSamples;
        stride = numChans << 2;
        left = f;
        right = f + 1;

        do {
            if (mHoldCounter > 0) {
                *left = mHeldLeft;
                if (numChans == 2) {
                    *right = mHeldRight;
                }
                mHoldCounter--;
            } else {
                mHoldCounter = (int)mHoldPeriod;
                mHeldLeft = *left;
                if (numChans == 2) {
                    mHeldRight = *right;
                }
            }

            left = (float*)((char*)left + stride);
            right = (float*)((char*)right + 8);
        } while (--ctr);
    }
}

void BitCrushEffect::Reset() {}
