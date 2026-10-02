#include "synth\BitCrushEffect.h"
#include "os\Debug.h"
#include "xdk\xaudio2\xaudio2.h"

BitCrushEffect::BitCrushEffect(IXAudioBatchAllocator *)
    : mHoldPeriod(0), mHoldCounter(0), mHeldLeft(0), mHeldRight(0) {}

void BitCrushEffect::SetParameters(BitCrushEffect::Params const &params) {
    mHoldPeriod = params.unk4;
}

// w19-d: fuzzy 98.45 -> 100 (byte-identical).  A plain indexed loop; the
// image's `addi r10, r10, 0x8` right-channel walker is MSVC folding
// numChans == 2 into `f[i * numChans + 1]` inside the stereo test, not a
// hand-written pointer pair (the old two-pointer do/while rotated the
// callee-saved homes of this/f/numSamples).
void BitCrushEffect::Process(float *f, int numSamples, int numChans) {
    do {
        if (!(numChans <= 2)) {
            TheDebugFailer << MakeString(kAssertStr, "dsp\\BitCrushEffect.cpp", 0x1e, "numChans <= 2");
        }
    } while (0);

    for (int i = 0; i < numSamples; i++) {
        if (mHoldCounter > 0) {
            f[i * numChans] = mHeldLeft;
            if (numChans == 2) {
                f[i * numChans + 1] = mHeldRight;
            }
            mHoldCounter--;
        } else {
            mHoldCounter = (int)mHoldPeriod;
            mHeldLeft = f[i * numChans];
            if (numChans == 2) {
                mHeldRight = f[i * numChans + 1];
            }
        }
    }
}

void BitCrushEffect::Reset() {}
