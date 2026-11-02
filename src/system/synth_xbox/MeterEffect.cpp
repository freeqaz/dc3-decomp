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
    // w26-f10 stop note, fuzzy 99.818 / canonical 100.0, one row left: the
    // final `add r11,r6,r11` (image) vs `add r11,r11,r6` (ours), i.e. the image
    // puts frameCount first and the mFrameCount load second.  Source order does
    // not reach it -- c2 orders each commutative use list by its own key
    // (C2RS-BRIDGE 8.7), whose last run is after lowering:
    //   mFrameCount += frameCount;                      99.818 (inert)
    //   mFrameCount = mFrameCount + frameCount;         99.818 (inert)
    //   unsigned int frames = mFrameCount; ...          99.818 (inert)
    //   unsigned int frames = mFrameCount + frameCount; 99.818 (inert)
    //   const unsigned int &frames = mFrameCount;       99.818 (inert)
    // Both operands are already leaves, so the "name a single-use subexpression"
    // lever has no target here; `frameCount` is a parameter and cannot be
    // respelled into a subexpression.  The tap's pre-sort list for the tuple
    // carrying the 0x90 member is `V1{0x10020} k07[0x90..]`, and every spelling
    // above reproduces it unchanged.
    mFrameCount = frameCount + mFrameCount;
}
