#include "synth_xbox\MeterEffect.h"
#include "synth\FxSend.h"
#include "xdk\LIBCMT\math.h"

MeterEffect::MeterEffect() : mFrameCount(0) {
    for (int i = 0; i < 6; i++) {
        mSumSquares[i] = 0;
        mPeak[i] = 0;
    }
    MeterEffectParams p;
    p.mLevelData = 0;
    SetParameters(&p, sizeof(p));
}

void MeterEffect::OnSetParameters(const MeterEffectParams &params) {
    mFrameCount = 0;
    for (int i = 0; i < 6; i++) {
        mSumSquares[i] = 0;
    }
}

// Accumulates a sum-of-squares and a running peak per channel and, whenever the
// consumer has supplied a LevelData array, publishes RMS and peak into it.
//
// Note the buffer indexing: the running base advances by frameCount once per
// channel, so this reads buffer[channel * frameCount + frame] -- a DEINTERLEAVED
// block, not the interleaved layout the rest of this family assumes. That is
// what the target does (`add r8, r8, r6` at the bottom of the outer loop, with
// `lfsx f12, r31, r5` off r31 = (r8 + inner) * 4); reproduced, not corrected.
void MeterEffect::DoProcess(
    const MeterEffectParams &params, float *__restrict buffer, unsigned int frameCount,
    unsigned int channelCount
) {
    for (unsigned int channel = 0; channel < channelCount; channel++) {
        // Through `this->mSumSquares[channel]` / `this->mPeak[channel]` MSVC
        // proves the two accesses are distinct fields and hoists the mPeak load
        // above the mSumSquares store. Through two plain float* it cannot, so
        // the load stays where the target has it -- after the store. That one
        // ordering edge was 9 of this function's 10 mismatched rows; the
        // remaining register numbering falls out of it.
        float *sums = &mSumSquares[channel];
        float *peak = &mPeak[channel];
        for (unsigned int frame = 0; frame < frameCount; frame++) {
            float sample = buffer[channel * frameCount + frame];
            float magnitude = fabsf(sample);
            *sums += sample * sample;
            // Spelled as an explicit subtraction so MSVC emits fsubs/fsel
            // rather than fcmpu/bge/fmr -- the target has no branch here.
            *peak = *peak - magnitude >= 0 ? *peak : magnitude;
        }
        if (params.mLevelData) {
            params.mLevelData[channel].mPeak = mPeak[channel];
            mPeak[channel] = 0;
            if (mFrameCount) {
                params.mLevelData[channel].mRMS =
                    sqrtf(mSumSquares[channel] / (float)mFrameCount);
            }
        }
    }
    mFrameCount = frameCount + mFrameCount;
}
