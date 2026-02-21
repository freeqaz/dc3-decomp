#include "synth/BitCrushEffect.h"
#include "os/Debug.h"
#include "xdk/xaudio2/xaudio2.h"

BitCrushEffect::BitCrushEffect(IXAudioBatchAllocator *)
    : mHoldPeriod(0), mHoldCounter(0), mHeldLeft(0), mHeldRight(0) {}

void BitCrushEffect::SetParameters(BitCrushEffect::Params const &params) {
    mHoldPeriod = params.unk4;
}

void BitCrushEffect::Process(float *f, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x1e);

    if (numSamples > 0) {
        int ctr;
        float *left;
        float *right;
        int stride;

        ctr = numSamples;
        stride = numChans << 2;  // stride in bytes: 4 bytes per sample * numChans
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
                float temp;
                temp = *left;
                temp = (float)(int)temp;
                *left = temp;
                mHeldLeft = temp;

                if (numChans == 2) {
                    temp = *right;
                    temp = (float)(int)temp;
                    *right = temp;
                    mHeldRight = temp;
                }
            }

            left = (float*)((char*)left + stride);
            right = (float*)((char*)right + 8);
            ctr--;
        } while (ctr != 0);
    }
}
