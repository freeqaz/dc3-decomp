#include "synth\FlangerEffect.h"
#include "synth\Common_Xbox.h"
#include "math\Rot.h"
#include "os\Debug.h"
#include "types.h"
#include "xdk\xaudio2\xaudio2.h"

#line 8 "dsp\\FlangerEffect.cpp"
FlangerEffect::FlangerEffect(IXAudioBatchAllocator *ix)
    : mWritePos(0), mDelaySamples(100), mDepthFrac(0), unk1c(0), mFeedbackFrac(0.5f), unk24(0), mRateRadians(0), unk2c(0),
      mWetFrac(0.1f) {
    for (int i = 0; i < 2; i++) {
        DspAllocate(mDelayBuffers[i], 0x2580, ix);
        DspAllocate(mDelayBuffers[i + 2], 0x2580, ix);
    }
}

FlangerEffect::~FlangerEffect() {
    for (int i = 0; i < 2; i++) {
        DspFree(mDelayBuffers[i]);
        DspFree(mDelayBuffers[i + 2]);
    }
}

void FlangerEffect::Reset() {
    mWritePos = 0;
    unk1c = 0;
    unk24 = 0;
    unk2c = 0;
    for (int i = 0; i < 2; i++) {
        DspClearBuffer(mDelayBuffers[i], 0x2580);
        DspClearBuffer(mDelayBuffers[i + 2], 0x2580);
    }
}

static float kSampleRate = 48000.0f;

void FlangerEffect::SetParameters(FlangerEffect::Params const &params) {
    mDelaySamples = (int)(params.mDelayMs * 48.0f);
    mRateRadians = (params.mRate / kSampleRate) * 6.2831853f;
    mDepthFrac = params.mDepth / 100.0f;
    mFeedbackFrac = params.mFeedback / 100.0f;
    mWetFrac = params.mWet / 100.0f;
}

/** RESIDUAL (w7-ba, 90.78 canonical, 182/215 rows equal; was 85.0 under
 *  w7-aj/w7-ap).  Semantics settled by w7-aj: every arithmetic instruction in
 *  0x82E588F8-0x82E58C2C is accounted for, `bl sin`+`frsp` is `sinf`, both
 *  wrap moduli and the Clamp() fsel pairs match.  The ??$MakeString@ name
 *  difference (`$$BY0BD@` vs `$$BY0BG@`) is an ICF fold of byte-equal
 *  literals, not a wrong callee.
 *
 *  What moved it (each measured on its own; all seven needed together):
 *   - the image's f23=2.0 / f24=4799 / f25=rate hoists come from FP
 *     LITERALS inside the loop, not from named locals outside it;
 *   - the per-frame values (`delay`, `writeIdx`, `center`, `swing`) are
 *     named locals in the OUTER loop body -- that lands the divw at
 *     0x82E58A34 BEFORE the cmpwi at 0x82E58A38, which w7-aj's variant (3)
 *     tried to reach by hoisting above the `numSamples > 0` guard (74.7);
 *   - `buf[frame * numChans + chan]` keeps the sample index in r22 (`add
 *     r22, r22, r24`); a `sampleIdx` local + `buf[sampleIdx + chan]` is
 *     strength-reduced once more into a walking pointer (76.2);
 *   - two separate `(float)(numSamples * 20)` temps: one variable makes
 *     /fp:fast lower both divisions to a reciprocal + two fmuls;
 *   - `mDepthFrac - unk1c` (member re-read, not the `depth` local) is the
 *     0x1c-then-0x18 load order w7-ap called unreachable;
 *   - storing through the MEMBER (`mDelayBuffers[chan][writeIdx] = in`)
 *     gives the image's base-first `stfsx f13, r9, r25` at 0x82E58A94; any
 *     store through the `delayBuf` local comes out index-first;
 *   - `Clamp(1.0f, 4799.0f, x)` from math/Utl.h is the fsel pair.
 *
 *  Measured INERT (byte-identical or only reorders the tail): division
 *  statement order, swapping the divisor temps, `unsigned` writeIdx,
 *  `*(p + idx)`, `buf[i] += ...` for stmt 1, a `wet` local, hoisted
 *  step declarations, the rate subtraction inside its division, the depth
 *  subtraction inside its division.  Measured WORSE: the index expression
 *  spelled out at every site (swaps r30/r31); reading
 *  `mDelayBuffers[chan][j]` or `buf[frame * numChans + chan]` in stmt 2 --
 *  both read 92.2-93.7 canonical but only by register-permutation
 *  forgiveness: they add a callee-saved GPR (`__savegprlr_19`, frame 0x70
 *  vs 0x68) and drop to 157 equal rows.
 *
 *  Remaining 33 rows: (a) the two ramp fdivs come out rate-first in every
 *  spelling where the image is depth-first (`fdivs f22, f13, f0` at
 *  0x82E589E0), so f21/f22 stay swapped through the tail (0x82E58BCC..D8);
 *  (b) MSVC forwards stmt 1's store of buf[i] into stmt 2's read where the
 *  image reloads it (`lfsx f10, r11, r31` at 0x82E58B6C), which reshuffles
 *  ~15 rows of the wrap arithmetic between 0x82E58B00 and 0x82E58BB0;
 *  (c) the image's inner-loop `cmpw cr6, r28, r24` is at 0x82E58BB4, ours
 *  is hoisted to just after the index add.  None of the spellings above
 *  touched (a) or (b); they are the next lane's problem, not a floor. */
void FlangerEffect::Process(float *buf, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x3f);

    float phase = unk24;
    float phaseOffset[2];
    if (numChans == 1) {
        phaseOffset[0] = 0.0f;
        phaseOffset[1] = 0.0f;
    } else {
        phaseOffset[0] = mWetFrac * -1.5707964f;
        phaseOffset[1] = mWetFrac * 1.5707964f;
    }

    // The three LFO states ramp from their stored values toward the current
    // parameters over 20 frames' worth of samples.
    float curRate = unk2c;
    float rate = curRate;
    float rateDelta = mRateRadians - curRate;
    float depth = unk1c;
    // Re-read the member rather than `depth`: that is what gives the image's
    // 0x1c-then-0x18 load order.
    float depthDelta = mDepthFrac - unk1c;
    // Two separate divisor temps: naming one variable twice makes MSVC's
    // /fp:fast default strength-reduce both divisions into 1.0f/x + two
    // multiplies, which the target does not do.
    float rampSteps1 = (float)(numSamples * 20);
    float rampSteps2 = (float)(numSamples * 20);
    float depthStep = depthDelta / rampSteps1;
    float rateStep = rateDelta / rampSteps2;

    int frame = 0;
    if (numSamples > 0) {
        do {
            float delay = (float)mDelaySamples;
            int writeIdx = (mWritePos + frame) % 9600;
            float center = (1.0f - depth * 0.5f) * delay;
            float swing = delay * depth * 0.5f;
            for (int chan = 0; chan < numChans; chan++) {
                float lfo = sinf(phaseOffset[chan] + phase);
                int i = frame * numChans + chan;
                float *delayBuf = mDelayBuffers[chan];
                float *feedbackBuf = mDelayBuffers[chan + 2];
                int writePos = mWritePos;
                float in = buf[i];
                mDelayBuffers[chan][writeIdx] = in;

                // Modulated delay in samples, clamped to the line, read twice:
                // once at the delay and once at double it.
                float delayPos = Clamp(1.0f, 4799.0f, lfo * swing + center);
                int intDelay = (int)delayPos;
                float delayPos2 = delayPos * 2.0f;
                int readBase = writePos - intDelay + frame;
                int intDelay2 = (int)delayPos2;
                float frac = delayPos - (float)intDelay;
                float frac2 = delayPos2 - (float)intDelay2;
                int readBase2 = writePos - intDelay2 + frame;

                buf[i] = delayBuf[(readBase + 9600) % 9600] * (1.0f - frac) + buf[i];
                float mixed = (buf[i] + delayBuf[(readBase + 9599) % 9600] * frac) * 0.5f;
                buf[i] = mixed;
                buf[i] = feedbackBuf[(readBase2 + 9600) % 9600] * (1.0f - frac2) * mFeedbackFrac + mixed;
                float out = feedbackBuf[(readBase2 + 9599) % 9600] * frac2 * mFeedbackFrac + buf[i];
                buf[i] = out;
                mDelayBuffers[chan + 2][writeIdx] = out;
                buf[i] = buf[i] * 2.0f - in;
            }
            frame++;
            phase += rate;
            rate += rateStep;
            depth += depthStep;
        } while (frame < numSamples);
    }
    unk1c = depth;
    unk2c = rate;
    unk24 = phase;
    mWritePos = (mWritePos + numSamples) % 9600;
    if (phase > 6.2831854820251465f) {
        unk24 = phase - 6.2831854820251465f;
    }
}
