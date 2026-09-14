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

/** SURVEYED w7-aj, 83.8% canonical, 824 B.  The SEMANTICS are settled: every
 *  arithmetic instruction in the image (0x82E588F8-0x82E58C2C) is accounted
 *  for by the statements below, the `bl sin` + `frsp` pair is what `sinf`
 *  lowers to here, and both loop bounds, both wrap moduli (0x2580 = 9600 and
 *  0x257F) and the two Min() fsel pairs match one for one.  The
 *  ??$MakeString@ callee name differs (image `$$BY0BD@`/`$$BY04@`, ours
 *  `$$BY0BG@`/`$$BY0O@`) but the string literals it is handed are byte-equal
 *  -- `dsp?2FlangerEffect?4cpp` and `numChans?5?$DM?$DN?52` -- so that is an
 *  ICF fold of two identically-lowered instantiations, not a wrong callee.
 *
 *  The residual is FPR/GPR allocation plus scheduling.  Ours holds var_f25 in
 *  f23 and the two ramp steps in f22/f21 where the image uses f25 and f21/f22,
 *  which rotates f23/f24/f25 across the whole body; and our scheduler emits
 *  the `(float)mDelaySamples` conversion and the `% 9600` wrap on the other
 *  side of the `numChans > 0` guard from the image.
 *
 *  Five variants measured, ALL non-improving -- do not re-derive:
 *    (1) index `mDelayBuffers[i]` / `[i+2]` instead of walking a `float **`,
 *        to reproduce the image's single induction register (`mr r29, r30`,
 *        `subf r23, r30, r10`, `lfsx f0, r23, r29`): 83.4, and it grows the
 *        frame by 0x10 (extra callee-saved GPR).
 *    (2) spell the second term `(temp_f31 - var_f30 * temp_f29)` so MSVC
 *        folds it to the image's single `fnmsubs f0, f30, f29, f31`
 *        (0x82E58A14) instead of our fmsubs+fneg: the fnmsubs DOES appear,
 *        but the int->float conversion then re-schedules across the guard and
 *        the net is 80.7.
 *    (3) (2) plus hoisting `temp_r25` above the `if (numChans > 0)`, which is
 *        where the image computes it (divw at 0x82E58A34, cmpwi only at
 *        0x82E58A38): 74.7.
 *    (4) swap the two ramp divisions so the depth division is emitted first
 *        (the image's `fdivs f22, f13, f0` is depth, ours is rate): 82.8 --
 *        it fixes the fdivs pair and breaks the two fsubs above it.
 *    (5) commutative operand order on `sampleIdx + var_r28` and on
 *        `temp_f0_3 * (temp_f13 * var_f30 * temp_f29)`: exactly neutral,
 *        MSVC normalises both.
 *  If this is picked up again the lever is whatever makes MSVC allocate f25
 *  for var_f25, not any re-spelling of the expressions. */
void FlangerEffect::Process(float *buf, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x3f);

    float phaseOffset[2];
    float curRate;
    float var_f30;
    float var_f26;
    float var_f25;
    float temp_f22;
    float temp_f21;

    var_f26 = unk24;
    if (numChans == 1) {
        phaseOffset[0] = 0.0f;
        phaseOffset[1] = 0.0f;
    } else {
        phaseOffset[0] = mWetFrac * -1.5707964f;
        phaseOffset[1] = mWetFrac * 1.5707964f;
    }
    curRate = unk2c;
    var_f25 = curRate;
    var_f30 = unk1c;
    // Two separate divisor temps: naming one variable twice makes MSVC's
    // /fp:fast default strength-reduce both divisions into 1.0f/x + two
    // multiplies, which the target does not do.
    float rampSteps1 = (float)(numSamples * 20);
    float rampSteps2 = (float)(numSamples * 20);
    temp_f22 = (mDepthFrac - var_f30) / rampSteps1;
    temp_f21 = (mRateRadians - curRate) / rampSteps2;

    int frame = 0;
    if (numSamples > 0) {
        // Retail keeps TWO outer-loop variables: `frame` counts frames (+1 per
        // iteration, and is what the loop bound and the mWritePos offsets use)
        // and `sampleIdx` walks buf in units of numChans.  Folding them into
        // one counter incremented by 1 + numChans is wrong for both.
        int sampleIdx = 0;
        float temp_f23 = 2.0f;
        float temp_f24 = 4799.0f;
        float temp_f31 = 1.0f;
        float temp_f29 = 0.5f;

        do {
            float temp_f13 = (float)mDelaySamples;
            int var_r28 = 0;
            if (numChans > 0) {
                int temp_r25 = ((mWritePos + frame) % 9600) * 4;
                float **var_r29 = mDelayBuffers;
                do {
                    float temp_f0_3 = sinf(phaseOffset[var_r28] + var_f26);
                    int temp_r11 = sampleIdx + var_r28;
                    intptr_t temp_r9 = (intptr_t)*var_r29;
                    var_r28++;
                    int temp_r11_2 = temp_r11 * 4;
                    int temp_r8 = mWritePos;
                    intptr_t temp_r7 = (intptr_t)var_r29[2];
                    var_r29++;
                    float temp_f13_2 = *(float *)((intptr_t)buf + temp_r11_2);
                    *(float *)(temp_r9 + temp_r25) = temp_f13_2;
                    float temp_f0_4 = (temp_f0_3 * (temp_f13 * var_f30 * temp_f29)) + (-((var_f30 * temp_f29) - temp_f31) * temp_f13);
                    float temp_f0_5;
                    if (temp_f31 - temp_f0_4 >= 0.0f) {
                        temp_f0_5 = temp_f31;
                    } else {
                        temp_f0_5 = temp_f0_4;
                    }
                    float temp_f0_6;
                    if (temp_f0_5 - temp_f24 >= 0.0f) {
                        temp_f0_6 = temp_f24;
                    } else {
                        temp_f0_6 = temp_f0_5;
                    }
                    int temp_r10 = (int)temp_f0_6;
                    float temp_f10 = temp_f0_6 * temp_f23;
                    int temp_r10_2 = (temp_r8 - temp_r10) + frame;
                    int temp_r3 = (int)temp_f10;
                    float temp_f0_7 = temp_f0_6 - (float)temp_r10;
                    float temp_f11 = temp_f10 - (float)temp_r3;
                    int temp_r10_3 = (temp_r8 - temp_r3) + frame;
                    *(float *)((intptr_t)buf + temp_r11_2) =
                        *(float *)((((temp_r10_2 + 0x2580) % 9600) * 4) + temp_r9) * (temp_f31 - temp_f0_7) + *(float *)((intptr_t)buf + temp_r11_2);
                    float temp_f0_8 =
                        (*(float *)((((temp_r10_2 + 0x257F) % 9600) * 4) + temp_r9) * temp_f0_7 + *(float *)((intptr_t)buf + temp_r11_2)) * temp_f29;
                    *(float *)((intptr_t)buf + temp_r11_2) = temp_f0_8;
                    *(float *)((intptr_t)buf + temp_r11_2) =
                        *(float *)((((temp_r10_3 + 0x2580) % 9600) * 4) + temp_r7) * (temp_f31 - temp_f11) * mFeedbackFrac + temp_f0_8;
                    float temp_f0_9 =
                        *(float *)((((temp_r10_3 + 0x257F) % 9600) * 4) + temp_r7) * temp_f11 * mFeedbackFrac + *(float *)((intptr_t)buf + temp_r11_2);
                    *(float *)((intptr_t)buf + temp_r11_2) = temp_f0_9;
                    *(float *)(temp_r7 + temp_r25) = temp_f0_9;
                    *(float *)((intptr_t)buf + temp_r11_2) = *(float *)((intptr_t)buf + temp_r11_2) * temp_f23 - temp_f13_2;
                } while (var_r28 < numChans);
            }
            frame++;
            var_f26 += var_f25;
            var_f25 += temp_f21;
            sampleIdx += numChans;
            var_f30 += temp_f22;
        } while (frame < numSamples);
    }
    unk1c = var_f30;
    unk2c = var_f25;
    unk24 = var_f26;
    mWritePos = (mWritePos + numSamples) % 9600;
    if (var_f26 > 6.2831854820251465f) {
        unk24 = var_f26 - 6.2831854820251465f;
    }
}
