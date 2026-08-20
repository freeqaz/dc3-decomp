#include "synth\EQEffect.h"
#include "os\Debug.h"
#include "xdk\xaudio2\xaudio2.h"
#include <math.h>
#include <string.h>

#ifdef HX_NATIVE
inline double __fsel(double a, double b, double c) { return a >= 0.0 ? b : c; }
#else
#include "xdk\LIBCMT\ppcintrinsics.h"
#endif

#line 13 "dsp\\EQEffect.cpp"
// Filter design types for crossover computation
enum FilterType { kFilterButterworth = 1 };
enum FilterBand { kFilterLowpass = 0, kFilterHighpass = 1, kFilterBandpass = 2 };

// Must stay layout-compatible with synth/filterdesign.cpp's FILTER, which is
// what createFilter() actually writes through. filterdesign's is 0x1014 bytes
// (xcoeffs[0x200] / ycoeffs[0x200] / gain / gain2 / invgain2 / numpoles /
// numzeros); this declaration names only the fields EQEffect reads, but it must
// still be the same *size* -- copyresults() ends with
// `out->numzeros = zplane.numzeros`, so a 0x1010-byte FILTER here means every
// createFilter() call below writes 4 bytes past the end of the local `filter`.
// The trailing numzeros below exists purely to absorb that store. On PPC this
// is codegen-neutral (measured: frame stays 0x10f0, 573 instructions /
// 213 mismatches either way) because the frame delta is callee-save GPR counts,
// not this struct.
struct FILTER {
    char _pad[0x800];
    float coeffs[0x200];    // 0x800
    float gain;             // 0x1000
    char _pad2[8];          // 0x1004
    // 0x100C is filterdesign's FILTER::numpoles -- the count of entries
    // copyresults() writes into the +0x800 array, which is what `coeffs` aliases.
    int numCoeffs;          // 0x100C -- filterdesign's numpoles
    int numzeros;           // 0x1010 -- written by copyresults(), never read here
};

// NOT extern "C". The target binary's symbol is the C++-mangled
// ?createFilter@@YAXW4FilterType@@W4FilterBand@@IMMPAUFILTER@@H@Z
// (config/373307D9/symbols.txt, .text:0x82E5C228) and synth/filterdesign.cpp
// defines it with C++ linkage too. An `extern "C"` here emitted a reference to
// a plain `createFilter` that nothing defines -- invisible to objdiff, which
// normalizes relocation targets away, but it left the native link with an
// unresolved symbol that EQEffect::SetParameter jumps to.
void createFilter(FilterType, FilterBand, unsigned int, float, float, FILTER *, int);

EQEffect::EQEffect(IXAudioBatchAllocator *) {
    mBand0Enabled = false;
    mBand1Enabled = false;
    mBand2Enabled = false;
    mBand1Freq = 12000.0f;
    mBand3Enabled = false;
    mBand1Gain = 0;
    mBand4Enabled = false;
    mBand1Q = 8000.0f;
    mBand2Freq = 1000.0f;
    mBand2Gain = 0;
    mBand2Q = 2000.0f;
    mBand3Freq = 0;
    mBand3Gain = 20000.0f;
    mBand3Q = 0;
    mBand4Freq = 20.0f;
    mBand4Gain = 0;
    mBand4Q = 0;
    mBand5Freq = 25.0f;
    mBand0B0 = 0;
    mBand0B1 = 0;
    mBand0B2 = 0;
    mBand0A1 = 0;
    mBand0A2 = 0;
    mBand0Z1 = 0;
    mBand1B0 = 0;
    mBand1B1 = 0;
    mBand1B2 = 0;
    mBand1A1 = 0;
    mBand1A2 = 0;
    mBand1Z1 = 0;
    mBand1Z2 = 0;
    mBand2B0 = 0;
    mBand2B1 = 0;
    mBand2B2 = 0;
    mBand2A1 = 0;
    mBand2A2 = 0;
    mBand2Z1 = 0;
    mBand3B0 = 0;
    mBand3B1 = 0;
    mBand3B2 = 0;
    mBand3A1 = 0;
    mBand3A2 = 0;
    mBand4B0 = 0;
    mBand4B1 = 0;
    mBand4B2 = 0;
    mBand4A1 = 0;
    mBand4A2 = 0;
    Reset();
}

void EQEffect::SetParameters(EQEffect::Params const &params) {
    SetParameter(0, params.mBand1Freq);
    SetParameter(1, params.mBand1Gain);
    SetParameter(2, params.mBand1Q);
    SetParameter(3, params.mBand2Freq);
    SetParameter(4, params.mBand2Gain);
    SetParameter(5, params.mBand2Q);
    SetParameter(6, params.mBand3Freq);
    SetParameter(7, params.mBand3Gain);
    SetParameter(8, params.mBand3Q);
    SetParameter(9, params.mBand4Freq);
    SetParameter(10, params.mBand4Gain);
    SetParameter(11, params.mBand4Q);
    SetParameter(12, params.mBand5Freq);
}

// The smoothing base is the float 0.368f widened to double
// (0x3fd78d4fe0000000 == 0.36800000071525574) -- i.e. ~1/e, a one-pole
// time constant. The target inlines it; it is not a named static.
#define kSmoothBase 0.368f

void EQEffect::Reset() {
    // Zero every per-channel filter delay line.  The crossover delay lines are
    // walked with a single flat index that keeps counting across channels
    // (30 floats per channel), which is what the target does.
    int xover = 0;
    for (int chan = 0; chan < EQ_MAX_CHANS; chan++) {
        mBand1DelayXn[chan] = 0;
        mBand1DelayXn1[chan] = 0;
        mBand1DelayZ[chan] = 0;
        mBand1DelayZ1[chan] = 0;
        mBand0DelayZ1[chan] = 0;
        mBand2DelayZ1[chan] = 0;

        for (int tap = 0; tap < 2; tap++) {
            mBand3DelayX[chan][tap] = 0;
            mBand3DelayZ[chan][tap] = 0;
            mBand4DelayX[chan][tap] = 0;
            mBand4DelayZ[chan][tap] = 0;
        }

        for (int stage = 0; stage < EQ_NUM_XOVER_STAGES; stage++) {
            for (int pass = 0; pass < EQ_NUM_XOVER_PASSES; pass++) {
                for (int tap = 0; tap < EQ_NUM_XOVER_TAPS; tap++) {
                    mXoverOutputDelay[0][xover + tap] = 0;
                    mXoverInputDelay[0][xover + tap] = 0;
                }
                xover += EQ_NUM_XOVER_TAPS;
            }
        }
    }

    // Copy target to current for smoothed parameters
    mBand0B2 = mBand0B1;  // band0 gain current = target
    mBand1A1 = mBand1B2;  // band1 gain current = target
    mBand2B2 = mBand2B1;  // band2 gain current = target
    mBand0A2 = mBand0A1;  // band0 shelf current = target
    mBand1Z1 = mBand1A2;  // band1 shelf current = target
    mBand2A2 = mBand2A1;  // band2 shelf current = target

    // Compute smoothing coefficient from crossover frequency
    if (mBand5Freq != 0.0f) {
        mSmoothCoeff = (float)pow(kSmoothBase, (double)(1.0f / (mBand5Freq * 48.0f)));
    } else {
        mSmoothCoeff = 1.0f;
    }
}

void EQEffect::Process(float *samples, int numSamples, int numChans) {
    if (mBand4Q != 0.0f) {
        // Crossover filter path.  Three Butterworth sections (low / band /
        // high), each run twice for 4th order.  Per channel the six delay
        // sub-lines live at fixed slots inside the 30-float input and output
        // delay lines:
        //   0..4   stage 0 pass 1 (lowpass,  3 taps used)
        //   5..9   stage 0 pass 2
        //   10..14 stage 1 pass 1 (bandpass, 5 taps)
        //   15..19 stage 1 pass 2
        //   20..24 stage 2 pass 1 (highpass, 3 taps used)
        //   25..29 stage 2 pass 2
        MILO_ASSERT(numChans <= 2, 0x78);
        if (numChans > 0) {
            for (int chan = 0; chan < numChans; chan++) {
                if (numSamples > 0) {
                    float *c1 = mXoverCoeffs[1];
                    float *yd2b = &mXoverOutputDelay[chan][25];
                    float *c2 = mXoverCoeffs[2];
                    float *s = &samples[chan];
                    for (int i = 0; i < numSamples; i++) {
                        float *xd = &mXoverInputDelay[chan][0];

                        // Stage 0 (lowpass), pass 1
                        xd[0] = xd[1];
                        xd[1] = xd[2];
                        xd[2] = *s / mXoverGain[0];
                        mXoverOutputDelay[chan][0] = mXoverOutputDelay[chan][1];
                        mXoverOutputDelay[chan][1] = mXoverOutputDelay[chan][2];
                        float fVar2 = mXoverCoeffs[0][0] * mXoverOutputDelay[chan][0] + mXoverCoeffs[0][1] * mXoverOutputDelay[chan][1]
                            + (xd[0] + xd[2]) + xd[1] * 2.0f;
                        mXoverOutputDelay[chan][2] = fVar2;

                        // Stage 0, pass 2
                        xd[5] = xd[6];
                        xd[6] = xd[7];
                        xd[7] = fVar2 / mXoverGain[0];
                        mXoverOutputDelay[chan][5] = mXoverOutputDelay[chan][6];
                        mXoverOutputDelay[chan][6] = mXoverOutputDelay[chan][7];
                        float fVar3 = mXoverCoeffs[0][0] * mXoverOutputDelay[chan][5] + mXoverCoeffs[0][1] * mXoverOutputDelay[chan][6]
                            + (xd[5] + xd[7]) + xd[6] * 2.0f;
                        mXoverOutputDelay[chan][7] = fVar3;

                        // Stage 1 (bandpass), pass 1
                        xd[10] = xd[11];
                        xd[11] = xd[12];
                        xd[12] = xd[13];
                        xd[13] = xd[14];
                        xd[14] = *s / mXoverGain[1];
                        mXoverOutputDelay[chan][10] = mXoverOutputDelay[chan][11];
                        mXoverOutputDelay[chan][11] = mXoverOutputDelay[chan][12];
                        mXoverOutputDelay[chan][12] = mXoverOutputDelay[chan][13];
                        mXoverOutputDelay[chan][13] = mXoverOutputDelay[chan][14];
                        fVar2 = (xd[14] + xd[10]) - xd[12] * 2.0f
                            + c1[3] * mXoverOutputDelay[chan][14] + c1[2] * mXoverOutputDelay[chan][12]
                            + c1[1] * mXoverOutputDelay[chan][11] + c1[0] * mXoverOutputDelay[chan][10];
                        mXoverOutputDelay[chan][14] = fVar2;

                        // Stage 1, pass 2
                        xd[15] = xd[16];
                        xd[16] = xd[17];
                        xd[17] = xd[18];
                        xd[18] = xd[19];
                        xd[19] = fVar2 / mXoverGain[1];
                        mXoverOutputDelay[chan][15] = mXoverOutputDelay[chan][16];
                        mXoverOutputDelay[chan][16] = mXoverOutputDelay[chan][17];
                        mXoverOutputDelay[chan][17] = mXoverOutputDelay[chan][18];
                        mXoverOutputDelay[chan][18] = mXoverOutputDelay[chan][19];
                        float fVar4 = (xd[19] + xd[15]) - xd[17] * 2.0f
                            + c1[3] * mXoverOutputDelay[chan][19] + c1[2] * mXoverOutputDelay[chan][17]
                            + c1[1] * mXoverOutputDelay[chan][16] + c1[0] * mXoverOutputDelay[chan][15];
                        mXoverOutputDelay[chan][19] = fVar4;

                        // Stage 2 (highpass), pass 1
                        xd[20] = xd[21];
                        xd[21] = xd[22];
                        xd[22] = *s / mXoverGain[2];
                        mXoverOutputDelay[chan][20] = mXoverOutputDelay[chan][21];
                        mXoverOutputDelay[chan][21] = mXoverOutputDelay[chan][22];
                        fVar2 = (xd[20] + xd[22]) - xd[21] * 2.0f
                            + c2[1] * mXoverOutputDelay[chan][22] + c2[0] * mXoverOutputDelay[chan][20];
                        mXoverOutputDelay[chan][22] = fVar2;

                        // Stage 2, pass 2
                        xd[25] = xd[26];
                        xd[26] = xd[27];
                        xd[27] = fVar2 / mXoverGain[2];
                        yd2b[0] = yd2b[1];
                        yd2b[1] = yd2b[2];
                        fVar2 = (xd[25] + xd[27]) - xd[26] * 2.0f
                            + c2[1] * yd2b[2] + c2[0] * yd2b[0];
                        yd2b[2] = fVar2;

                        // Mix crossover bands using smoothed gains
                        *s = mBand2B2 * fVar3 + fVar2 * mBand0B2 + fVar4 * mBand1A1;

                        // Smooth interpolated output mix coefficients
                        mBand0B2 = (mBand0B2 - mBand0B1) * mSmoothCoeff + mBand0B1;
                        mBand2B2 = (mBand2B2 - mBand2B1) * mSmoothCoeff + mBand2B1;
                        mBand1A1 = (mBand1A1 - mBand1B2) * mSmoothCoeff + mBand1B2;
                        s += numChans;
                    }
                }
            }
        }
    } else {
        // Biquad filter path
        MILO_ASSERT(numChans <= 2, 0xd9);
        if (numChans > 0) {
            for (int chan = 0; chan < numChans; chan++) {
                if (numSamples > 0) {
                    float *s = &samples[chan];
                    for (int i = 0; i < numSamples; i++) {
                        if (mBand0Enabled) {
                            // Band 0: low shelf, one-pole allpass
                            float x = *s;
                            float z1 = mBand0DelayZ1[chan];
                            float coeff = mBand0Z1;
                            float y = x - z1 * coeff;
                            *s = (x - (coeff * y + z1)) * mBand0A2 + x;
                            mBand0DelayZ1[chan] = y;
                        }
                        if (mBand1Enabled) {
                            // Band 1: bell/peaking, two-pole allpass
                            float x = *s;
                            float b0 = mBand1B0;
                            float cosCoeff = mBand1Z2;
                            float xn = mBand1DelayXn[chan];
                            float xn2 = mBand1DelayXn1[chan];
                            float gainCur = mBand1Z1;
                            float zn = mBand1DelayZ[chan];
                            mBand1DelayXn1[chan] = xn;
                            float zn1 = mBand1DelayZ1[chan];
                            mBand1DelayXn[chan] = *s;
                            mBand1DelayZ1[chan] = zn;
                            float y = zn1 * b0 + -(zn * cosCoeff - (-(x * b0) + xn * cosCoeff + xn2));
                            mBand1DelayZ[chan] = y;
                            *s = (x - y) * gainCur + x;
                        }
                        if (mBand2Enabled) {
                            // Band 2: high shelf, one-pole allpass
                            float x = *s;
                            float z1 = mBand2DelayZ1[chan];
                            float coeff = mBand2Z1;
                            float y = x - z1 * coeff;
                            *s = (coeff * y + z1 + x) * mBand2A2 + x;
                            mBand2DelayZ1[chan] = y;
                        }
                        if (mBand3Enabled) {
                            // Band 3: bandpass biquad
                            float b1 = mBand3B1;
                            float xn = mBand3DelayX[chan][0];
                            float b0 = mBand3B0;
                            float x = *s;
                            float b2 = mBand3B2;
                            float xn1 = mBand3DelayX[chan][1];
                            float a1 = mBand3A1;
                            float a2 = mBand3A2;
                            mBand3DelayX[chan][1] = xn;
                            float zn = mBand3DelayZ[chan][0];
                            float zn1 = mBand3DelayZ[chan][1];
                            mBand3DelayX[chan][0] = *s;
                            mBand3DelayZ[chan][1] = zn;
                            float out = -(a2 * zn1 - -(a1 * zn - (b1 * xn + b0 * x + b2 * xn1)));
                            mBand3DelayZ[chan][0] = out;
                            *s = out;
                        }
                        if (mBand4Enabled) {
                            // Band 4: bandpass biquad
                            float x = *s;
                            float b0 = mBand4B0;
                            float b1 = mBand4B1;
                            float xn = mBand4DelayX[chan][0];
                            float b2 = mBand4B2;
                            float xn1 = mBand4DelayX[chan][1];
                            float a1 = mBand4A1;
                            float zn = mBand4DelayZ[chan][0];
                            float a2 = mBand4A2;
                            float zn1 = mBand4DelayZ[chan][1];
                            mBand4DelayX[chan][1] = xn;
                            mBand4DelayX[chan][0] = *s;
                            mBand4DelayZ[chan][1] = zn;
                            float out = -(a2 * zn1 - -(a1 * zn - (b0 * x + b1 * xn + b2 * xn1)));
                            mBand4DelayZ[chan][0] = out;
                            *s = out;
                        }
                        // Smooth interpolated coefficients
                        mBand0A2 = (mBand0A2 - mBand0A1) * mSmoothCoeff + mBand0A1;
                        mBand1Z1 = (mBand1Z1 - mBand1A2) * mSmoothCoeff + mBand1A2;
                        mBand2A2 = (mBand2A2 - mBand2A1) * mSmoothCoeff + mBand2A1;
                        s += numChans;
                    }
                }
            }
        }
    }
}

void EQEffect::SetParameter(int param, float value) {
    bool updateBand0 = false;
    bool updateBand1 = false;
    bool updateBand2 = false;
    bool updateBand3 = false;
    bool updateBand4 = false;
    bool updateCrossover = false;
    float zero = 0.0f;
    float one = 1.0f;
    float half = 0.5f;

    switch (param) {
    case 0: {
        float clamped = (float)__fsel((float)(24000.0f - value), value, 24000.0f);
        float result = (float)__fsel(-clamped, zero, clamped);
        if (result == (double)mBand1Freq) break;
        mBand1Freq = (float)result;
        updateCrossover = true;
        updateBand0 = true;
        break;
    }
    case 1: {
        float clamped = (float)__fsel((float)(42.0f - value), value, 42.0f);
        float result = (float)__fsel((float)(-42.0f - clamped), -42.0f, clamped);
        if (result == (double)mBand1Gain) break;
        mBand1Gain = (float)result;
        updateBand0 = true;
        break;
    }
    case 2: {
        float clamped = (float)__fsel((float)(24000.0f - value), value, 24000.0f);
        float result = (float)__fsel(-clamped, zero, clamped);
        if (result == (double)mBand1Q) break;
        mBand1Q = (float)result;
        updateCrossover = true;
        updateBand1 = true;
        break;
    }
    case 3: {
        float clamped = (float)__fsel((float)(24000.0f - value), value, 24000.0f);
        float result = (float)__fsel(-clamped, zero, clamped);
        if (result == (double)mBand2Freq) break;
        mBand2Freq = (float)result;
        updateBand1 = true;
        break;
    }
    case 4: {
        float clamped = (float)__fsel((float)(42.0f - value), value, 42.0f);
        float result = (float)__fsel((float)(-42.0f - clamped), -42.0f, clamped);
        if (result == (double)mBand2Gain) break;
        mBand2Gain = (float)result;
        updateBand1 = true;
        break;
    }
    case 5: {
        float clamped = (float)__fsel((float)(24000.0f - value), value, 24000.0f);
        float result = (float)__fsel(-clamped, zero, clamped);
        if (result == (double)mBand2Q) break;
        mBand2Q = (float)result;
        updateCrossover = true;
        updateBand2 = true;
        break;
    }
    case 6: {
        float clamped = (float)__fsel((float)(42.0f - value), value, 42.0f);
        float result = (float)__fsel((float)(-42.0f - clamped), -42.0f, clamped);
        if (result == (double)mBand3Freq) break;
        mBand3Freq = (float)result;
        updateBand2 = true;
        break;
    }
    case 7: {
        float clamped = (float)__fsel((float)(20000.0f - value), value, 20000.0f);
        float result = (float)__fsel((float)(20.0f - clamped), 20.0f, clamped);
        if (result == (double)mBand3Gain) break;
        mBand3Gain = (float)result;
        updateBand3 = true;
        break;
    }
    case 8: {
        float clamped = (float)__fsel((float)(25.0f - value), value, 25.0f);
        float result = (float)__fsel((float)(-25.0f - clamped), -25.0f, clamped);
        if (result == (double)mBand3Q) break;
        mBand3Q = (float)result;
        updateBand3 = true;
        break;
    }
    case 9: {
        float clamped = (float)__fsel((float)(20000.0f - value), value, 20000.0f);
        float result = (float)__fsel((float)(20.0f - clamped), 20.0f, clamped);
        if (result == (double)mBand4Freq) break;
        mBand4Freq = (float)result;
        updateBand4 = true;
        break;
    }
    case 10: {
        float clamped = (float)__fsel((float)(25.0f - value), value, 25.0f);
        float result = (float)__fsel((float)(-25.0f - clamped), -25.0f, clamped);
        if (result == (double)mBand4Gain) break;
        mBand4Gain = (float)result;
        updateBand4 = true;
        break;
    }
    case 11: {
        bool crossoverOn = value > half;
        mBand4Q = (float)crossoverOn;
        break;
    }
    case 12: {
        float clamped = (float)__fsel((float)(5000.0f - value), value, 5000.0f);
        float result = (float)__fsel((float)(25.0f - clamped), 25.0f, clamped);
        mBand5Freq = (float)result;
        float smoothCoeff;
        if (result != 0.0f) {
            smoothCoeff = (float)pow(kSmoothBase, (double)(1.0f / (float)(result * 48.0f)));
        } else {
            smoothCoeff = one;
        }
        mSmoothCoeff = smoothCoeff;
        break;
    }
    default:
        MILO_FAIL("bad parameter %i\n", param);
        break;
    }

    if (updateBand0) {
        // Low shelf filter (band 0)
        double tanFreq = tan((double)(mBand1Freq * 6.544985e-05f));
        mBand0B0 = (float)tanFreq;
        double gainTarget = pow(10.0, (double)(mBand1Gain * 0.05f));
        float gainFf = (float)gainTarget;
        mBand0B1 = (float)gainTarget;
        float shelfTarget = (gainFf - one) * half;
        mBand0A1 = shelfTarget;
        mBand0Enabled = (mBand0A2 != zero || shelfTarget != zero);
        // Both arms share the (t - 1) / (t + 1) tail in the target; only the
        // numerator base differs, so it must be one division, not two.
        float allpassBase;
        if (mBand1Gain > zero) {
            allpassBase = mBand0B0;
        } else {
            allpassBase = gainFf * mBand0B0;
        }
        mBand0Z1 = (allpassBase - one) / (allpassBase + one);
    } else if (updateBand1) {
        // Bell/peaking filter (band 1)
        double tanFreq = tan((double)(mBand2Freq * 6.544985e-05f));
        mBand1B1 = (float)tanFreq;
        double gainTarget = pow(10.0, (double)(mBand2Gain * 0.05f));
        float gainFf = (float)gainTarget;
        mBand1B2 = (float)gainTarget;
        mBand1A2 = (gainFf - one) * half;
        double cosQ = cos((double)(mBand1Q * 0.0001308997f));
        mBand1Z2 = -(float)cosQ;
        mBand1Enabled = (mBand1Z1 != zero || mBand1A2 != zero);
        float coeff1;
        if (mBand2Gain > zero) {
            coeff1 = ((float)tanFreq - one) / ((float)tanFreq + one);
        } else {
            coeff1 = ((float)tanFreq - mBand1B2) / ((float)tanFreq + mBand1B2);
        }
        mBand1B0 = coeff1;
        mBand1Z2 = (one - coeff1) * (-(float)cosQ);
    } else if (updateBand2) {
        // High shelf filter (band 2)
        double tanFreq = tan((double)(mBand2Q * 6.544985e-05f));
        mBand2B0 = (float)tanFreq;
        double gainTarget = pow(10.0, (double)(mBand3Freq * 0.05f));
        float gainFf = (float)gainTarget;
        mBand2B1 = (float)gainTarget;
        float shelfTarget = (gainFf - one) * half;
        mBand2A1 = shelfTarget;
        mBand2Enabled = (mBand2A2 != zero || shelfTarget != zero);
        float coeff;
        if (mBand3Freq > zero) {
            coeff = (mBand2B0 - one) / (mBand2B0 + one);
        } else {
            coeff = (mBand2B0 - gainFf) / (mBand2B0 + gainFf);
        }
        mBand2Z1 = coeff;
    } else if (updateBand3) {
        // Bandpass filter 1 (band 3)
        mBand3Enabled = (mBand3Gain < 19999.0f);
        float wcF = mBand3Gain * 4.1666666e-05f;
        double invGain = pow(10.0, (double)(mBand3Q * -0.05f));
        float invGainF = (float)invGain;
        float wcPi = wcF * 3.1415927f;
        double sinWc = sin((double)wcPi);
        float alpha = (float)((float)sinWc * invGainF) * half;
        float k = (one - alpha) * half / (alpha + one);
        double cosWc = cos((double)wcPi);
        mBand3A2 = k * 2.0f;
        float cosKhalf = (float)cosWc * (k + half);
        mBand3A1 = cosKhalf * -2.0f;
        float fk4 = (k + half) - cosKhalf;
        float fk2 = fk4 * 2.0f;
        mBand3B0 = fk2;
        mBand3B1 = fk4 * 4.0f;
        mBand3B2 = fk2;
    } else if (updateBand4) {
        // Bandpass filter 2 (band 4)
        mBand4Enabled = (mBand4Freq > 21.0f);
        float wcF = mBand4Freq * 4.1666666e-05f;
        double invGain = pow(10.0, (double)(mBand4Gain * -0.05f));
        float invGainF = (float)invGain;
        float wcPi = wcF * 3.1415927f;
        double sinWc = sin((double)wcPi);
        float alpha = (float)((float)sinWc * invGainF) * half;
        float k = (one - alpha) * half / (alpha + one);
        double cosWc = cos((double)wcPi);
        mBand4A2 = k * 2.0f;
        float cosKhalf = (float)cosWc * (k + half);
        mBand4A1 = cosKhalf * -2.0f;
        float fk4 = (float)((double)(cosKhalf + k) + (double)half) * 0.25f;
        float fk2 = fk4 * 2.0f;
        mBand4B0 = fk2;
        mBand4B1 = fk4 * -4.0f;
        mBand4B2 = fk2;
    }

    if (updateCrossover && mBand4Q != 0.0f) {
        float freqScale = 2.0833333e-05f;
        FILTER filter;

        // Lowpass crossover filter (band 0)
        float f1 = mBand2Q * freqScale;
        createFilter(kFilterButterworth, kFilterLowpass, 0, f1, f1, &filter, 2);
        mXoverGain[0] = filter.gain;
        if (filter.numCoeffs > 0) {
            memcpy(&mXoverCoeffs[0], &filter.coeffs[0], filter.numCoeffs * 4);
        }

        // Bandpass crossover filter (band 2)
        createFilter(kFilterButterworth, kFilterBandpass, 0, mBand2Q * freqScale, mBand1Freq * freqScale, &filter, 2);
        mXoverGain[1] = filter.gain;
        if (filter.numCoeffs > 0) {
            memcpy(&mXoverCoeffs[1], &filter.coeffs[0], filter.numCoeffs * 4);
        }

        // Highpass crossover filter (band 1)
        float f3 = mBand1Freq * freqScale;
        createFilter(kFilterButterworth, kFilterHighpass, 0, f3, f3, &filter, 2);
        mXoverGain[2] = filter.gain;
        if (filter.numCoeffs > 0) {
            memcpy(&mXoverCoeffs[2], &filter.coeffs[0], filter.numCoeffs * 4);
        }

        Reset();
    }
}
