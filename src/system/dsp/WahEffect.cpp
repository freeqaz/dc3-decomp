#include "synth\WahEffect.h"
#include "os\Debug.h"
#include "math\Utl.h"
#include <cmath>

#line 6 "dsp\\WahEffect.cpp"
WahEffect::WahEffect(IXAudioBatchAllocator *) {
    mSampleRate = 96000;
    mGain = 7;
    mFreqLo = 1000.0f;
    mFreqHi = 5000.0f;
    mResonance = 1.35f;
    mBandwidth = 0.3f;
    mSweepRate = -1.0f;
    mSweepRange = 0.5f;
    mEnvAmount = 1.0f;
    mStaticSweep = 0.5f;
    mCurrentSweep = 0;
    mPrevEnv = 1e+30;
    mPhase = 0;
    mLastInput = 0;
    mLastOutput = 0;
    mFilterState3 = 0;
    mFilterState2 = 0;
    mFilterState1 = 0;
    mFilterState0 = 0;
}

void WahEffect::Reset() {
    mPhase = 0;
    mFilterState1 = 0;
    mFilterState0 = 0;
    mFilterState3 = 0;
    mFilterState2 = 0;
}

void WahEffect::SetParameters(WahEffect::Params const &params) {
    mGain = params.mGain;
    mFreqHi = params.mFreqHi;
    mFreqLo = params.mFreqLo;
    mResonance = params.mResonance;
    mBandwidth = params.mBandwidth;
    mSweepRate = params.mSweepRate;
    mSweepRange = params.mSweepRange;
    mEnvAmount = params.mEnvAmount;
    mStaticSweep = params.mStaticSweep;
}

// w7-bh: the one remaining Function Call Diff row here is a benign ICF fold,
// not a wrong callee.  The image calls
// `MakeString<char[19], int, char[5]>` and we emit
// `MakeString<char[18], int, char[14]>`; both names resolve to 0x824D1870 in
// build/373307D9/icf_aliases.map, because MakeString's array-bound template
// parameters never reach the generated code and every instantiation of that
// shape is byte-identical.  The name objdiff shows is just whichever
// instantiation won the fold.  (w7-bh's "indexed-vs-auto-update addressing
// floor" at 93.29% was the `sampleIdx += numChans` spelling; `i * numChans + ch`
// reaches the indexed form -- see w7-bx notes in the body.  RESIDUAL at 99.0:
// callee-saved homing of the three parameters (image r28/r30/r27 = buf /
// numSamples / numChans, ours r29/r27/r28) and of the sample base (r29 vs
// r30), the f12/f13 colouring of `1 - newFreq` vs mGain at 0x82E5A1A0-1C0,
// and the `fmuls f29, f17, f17` slot after `bl cos`; declaring i above the
// guard, hoisting mGain into a local, and `out * gain` are all byte-identical.)
void WahEffect::Process(float *buf, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x34);

    // Load parameters in target order
    float f0_unk18 = mSweepRange;
    float f10 = mFreqLo;
    float f9 = mFreqHi;
    float f8 = mBandwidth;

    // Constants
    float f26 = 2.0f;
    float f31 = 1.0f;
    float f7 = f0_unk18 * f26;        // sweepRange * 2
    float f6 = f31 - f0_unk18;        // 1 - sweepRange
    float f0_norm = 4.1666666e-5f;    // 1/24000
    float f12_twopi = 6.2831853f;     // 2*PI (kept alive for end-of-function phase comparison)
    float f25 = f10 * f0_norm;        // freqLo normalized
    float f24 = f9 * f0_norm;         // freqHi normalized
    float f23 = f8 * 0.1f;            // bandwidth scaled by 0.1

    // Load state variables BEFORE the comparison
    float f10_state = mFilterState0;
    float f0_state = mFilterState1;
    float f12_state = mFilterState2;
    float f11_state = mFilterState3;
    float f27 = mPhase;
    float f30 = 0.5f;

    // Copy state to stack arrays
    float stack50[2];
    float stack58[2];
    stack50[0] = f10_state;
    stack50[1] = f0_state;

    // Compute phase rate
    float f21 = f7 / f6;

    stack58[0] = f12_state;
    stack58[1] = f11_state;

    // Compute sweep value based on mSweepRate - comparison happens AFTER state loading
    float f13_unk14 = mSweepRate;
    float sweepVal;
    if (f13_unk14 < 0.0f) {
        sweepVal = f31;
    } else {
        // Phase modulation with Mod
        float f0_invtwopi = 0.15915494f;  // 1/(2*PI)
        float f2 = f31;
        float f0_neghalf = -0.5f;
        float tmp = f27 * f0_invtwopi - f13_unk14;
        tmp = tmp + f30;
        float modPhase = Mod(tmp - f0_neghalf, f2);
        modPhase = modPhase - f30;

        // fsel clamp
        float f0_lo = 0.9f;
        float f13_hi = 1.1f;
        sweepVal = (modPhase >= 0.0f) ? f0_lo : f13_hi;
    }

    // Apply resonance scaling
    float f13_scaled = mResonance * sweepVal;
    float f0_unk0 = mGain;
    float f0_scale = 1.3089970e-4f;   // 0x3909421f
    float f18 = f13_scaled * f0_scale;

    // Clamp mGain to >= 1.0
    if (f0_unk0 < f31) {
        mGain = f31;
    }

    // Process samples
    if (numSamples > 0) {
        float f19 = 0.99999f;         // 0x3f7fff58
        float f20 = -4.2704245e-9f;   // 0xb192bb0d (negative)
        float f22 = 0.99958f;         // 0x3f7fe47a

        // w7-bx: the image's inner loop is INDEXED -- `add r8, r29, r10` /
        // `slwi r7, r8, 2` / `lfsx f12, r7, r28` (0x82E5A214-0x82E5A230) and
        // `stfsx f12, r7, r28` (0x82E5A270) -- with r29 advanced by
        // `add r29, r29, r27` (0x82E5A280) each sample.  That is
        // `buf[i * numChans + ch]`: MSVC strength-reduces i*numChans to the
        // r29 += numChans walker but will not fold the second-level
        // `(i*numChans) + ch` into a pointer walk.  Spelling the index as a
        // running `sampleIdx += numChans` (w7-bh's form) makes the whole
        // subscript a reducible IV and yields the `stfsu f12, 0x4(r10)` /
        // `add r30, r28, r30` pointer walk (93.3); `unsigned` sampleIdx is
        // identical.  The countdown on numSamples (`subic. r30, r30, 0x1`,
        // 0x82E5A278) is kept as a separate decrement: a counting
        // `i < numSamples` loop keeps i live and costs r25 (94.1).
        int i = 0;

        do {
            // Compute sin of phase
            float sinVal = sin(f27);
            sinVal = (float)sinVal;
            float f13_unk1c = mEnvAmount;
            float f12_coef = f22;

            // Compute sweep
            float f0_sweep = (sinVal + f31) * f30;

            // Check mEnvAmount
            if (f13_unk1c < f30) {
                f0_sweep = mStaticSweep;
            } else {
                float f11_unk28 = mPrevEnv;
                if (f11_unk28 != f13_unk1c) {
                    mSampleRate = 0;
                }
            }

            // Counter interpolation
            int counter = mSampleRate;
            if (counter <= 96000) {
                int nextCounter = counter + 1;
                float counterF = (float)counter;
                float prod = counterF * f20;
                f12_coef = prod + f19;
                mSampleRate = nextCounter;
            }

            // Frequency tracking
            float f11_unk24 = mCurrentSweep;
            float f11_diff = f11_unk24 - f0_sweep;
            mPrevEnv = f13_unk1c;
            float newFreq = f11_diff * f12_coef + f0_sweep;
            mCurrentSweep = newFreq;

            // Filter coefficients
            float f12_inv = f31 - newFreq;
            float blend = newFreq * f25;
            blend = f12_inv * f24 + blend;
            float filterFreq = blend * f30 + f23;
            float f28 = blend;
            float filterDiv = filterFreq / mGain;
            float f17 = f31 - filterDiv;

            // Compute cos/sin for filter.  The target squares f17 AFTER the
            // cos call; MSVC schedules it before regardless, so moving this
            // statement either side of the call is byte-identical (measured).
            float cosVal = cos(f28);
            cosVal = (float)cosVal;
            float f29 = f17 * f17;
            float cosMod = cosVal * f17;
            float f28_scaled = cosMod * f26;

            float sinVal2 = sin(f28);
            float feedback = (f31 - f17) * mGain;
            sinVal2 = (float)sinVal2;
            feedback = feedback * sinVal2;

            // Process channels
            int ch = 0;
            if (numChans > 0) {
                float f13_gain = f21 + f31;

                for (; ch < numChans; ch++) {
                    // w7-bx: `ch` is declared above the `numChans > 0` guard
                    // because the image sets `li r10, 0x0` (0x82E5A1F4) BEFORE
                    // `cmpwi cr6, r27, 0x0`.  (w7-bh's byte-offset
                    // `*(float *)((char *)stack50 + ch * 4)` spelling of the
                    // state arrays was a no-op; plain subscripts are kept.)
                    float sample = buf[i * numChans + ch];
                    mLastInput = sample;
                    float state1 = stack50[ch];
                    float state2 = stack58[ch];

                    stack58[ch] = state1;

                    // Biquad filter.  Under /fp:fast the source order of the
                    // two products is the OPPOSITE of the image's: the image
                    // does `fmuls f11, f12(sample), f0(feedback)` at
                    // 0x82E5A238 and then `fmadds f11, f10(state1), f28, f11`
                    // at 0x82E5A24C; writing sample*feedback first emits
                    // state1*f28 first and hoists the state1 load above the
                    // buf load (w7-bx).
                    float tmp1 = state1 * f28_scaled;
                    tmp1 = sample * feedback + tmp1;
                    tmp1 = tmp1 - state2 * f29;
                    stack50[ch] = tmp1;

                    // Soft clip
                    float out = sample * f30 + tmp1;
                    float absOut = fabs(out);
                    out = f13_gain * out;
                    absOut = absOut * f21 + f31;
                    out = out / absOut;

                    mLastOutput = out;
                    buf[i * numChans + ch] = out;
                }
            }

            // Update phase
            numSamples--;
            f27 = f18 + f27;
            i++;
        } while (numSamples != 0);
    }

    // Store phase - compare f27 with 2*PI, subtract if greater
    mPhase = f27;
    if (f27 > f12_twopi) {
        mPhase = f27 - f12_twopi;
    }

    // Copy state back from stack.  The two halves are NOT adjacent in the
    // object: stack50 was loaded from mFilterState0/mFilterState1 (0x34/0x38)
    // and stack58 from mFilterState2/mFilterState3 (0x3c/0x40), so the
    // write-back walks one pointer and reaches TWO floats back for the stack50
    // half.  The image does exactly that -- `subi r10, r26, 0x4` at 82E5A2A8
    // with `addi r26, r31, 0x3c`, then `stfs f0, -0x4(r10)` / `stfsu f13,
    // 0x4(r10)` at 82E5A2C8/CC, i.e. 0x34 then 0x3c, then 0x38 then 0x40.
    float *dest = &mFilterState2;
    for (int i = 0; i < 2; i++) {
        float s1 = stack50[i];
        float s2 = stack58[i];
        dest[-2] = s1;  // mFilterState0, then mFilterState1
        *dest = s2;     // mFilterState2, then mFilterState3
        dest++;
    }
}
