#include "synth/EQEffect.h"
#include "os/Debug.h"
#include "xdk/xaudio2/xaudio2.h"
#include <math.h>

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

// kSmoothBase = 0x3fd78d4fe0000000 as double
static const double kSmoothBase = 4.6414757e-01;

void EQEffect::Reset() {
    // Zero all per-channel filter delay state
    // Uses flat indexing to match target's loop structure
    int lVar5 = 0;
    int iVar10 = 0x40;
    float *puVar11 = (float *)this + 0xC8 / 4;
    do {
        // Band 0/1/2 allpass and bell delay lines (6 floats per channel, stride 8 bytes)
        puVar11[-2] = 0;
        puVar11[0] = 0;
        int iVar12 = 0;
        puVar11[2] = 0;
        puVar11[4] = 0;
        puVar11[6] = 0;
        puVar11[8] = 0;

        // Band 3/4 bandpass delay lines
        for (int tap = 0; tap < 2; tap++) {
            int a = iVar10 - 4 + iVar12;
            int b = iVar10 + iVar12;
            int c = iVar10 + 4 + iVar12;
            int d = iVar10 + 8 + iVar12;
            iVar12 = iVar12 + 1;
            ((float *)this)[a] = 0;
            ((float *)this)[b] = 0;
            ((float *)this)[c] = 0;
            ((float *)this)[d] = 0;
        }

        // Crossover delay lines - 3 stages, 2 passes, 5 taps each
        for (int stage = 0; stage < 3; stage++) {
            for (int pass = 0; pass < 2; pass++) {
                for (int tap = 0; tap < 5; tap++) {
                    ((float *)this)[lVar5 + tap + 0x97] = 0;
                    ((float *)this)[lVar5 + tap + 0x5b] = 0;
                }
                lVar5 += 5;
            }
        }

        iVar10 += 2;
        puVar11++;
    } while (iVar10 < 0x44);

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
        // Crossover filter path
        MILO_ASSERT(numChans <= 2, 0x78);
        if (numChans > 0) {
            float *chanBase = (float *)this;
            for (int chan = 0; chan < numChans; chan++) {
                if (numSamples > 0) {
                    for (int i = 0; i < numSamples; i++) {
                        float *s = &samples[i * numChans + chan];

                        // Crossover filter stage 0 (lowpass): 2nd-order applied twice
                        // Pass 1
                        float *xd0 = &mXoverInputDelay[chan][0]; // 5-tap input delay
                        float *yd0 = &mXoverOutputDelay[chan][0]; // 5-tap output delay
                        xd0[0] = xd0[1];
                        xd0[1] = xd0[2];
                        xd0[2] = *s / mXoverGain[0];
                        float oldY0 = yd0[0];
                        yd0[0] = yd0[1];
                        float fVar2 = xd0[1] * 2.0f + mXoverCoeffs[0][1] * yd0[1]
                            + mXoverCoeffs[0][0] * oldY0 + xd0[0] + xd0[2];
                        yd0[1] = fVar2;

                        // Pass 2
                        float *xd0b = &mXoverInputDelay[chan][5];
                        float *yd0b = &mXoverOutputDelay[chan][5];
                        float oldX0b = xd0b[1];
                        xd0b[1] = xd0b[2];
                        xd0b[0] = oldX0b;
                        xd0b[2] = fVar2 / mXoverGain[0];
                        yd0b[0] = yd0b[1];
                        yd0b[1] = yd0b[2];
                        float fVar3 = xd0b[1] * 2.0f + mXoverCoeffs[0][1] * yd0b[1]
                            + mXoverCoeffs[0][0] * yd0b[0] + xd0b[0] + xd0b[2];
                        yd0b[2] = fVar3;

                        // Crossover filter stage 1 (bandpass): 4th-order
                        // Pass 1
                        float *xd1 = &mXoverInputDelay[chan][10];
                        float *yd1 = &mXoverOutputDelay[chan][10];
                        float oldX1_1 = xd1[1];
                        xd1[1] = xd1[2];
                        xd1[2] = xd1[3];
                        xd1[0] = oldX1_1;
                        xd1[3] = xd1[4];
                        xd1[4] = *s / mXoverGain[1];
                        float oldY1_1 = yd1[1];
                        yd1[1] = yd1[2];
                        yd1[0] = oldY1_1;
                        yd1[2] = yd1[3];
                        yd1[3] = yd1[4];
                        fVar2 = mXoverCoeffs[1][0] * yd1[0] + mXoverCoeffs[1][1] * yd1[1]
                            + mXoverCoeffs[1][2] * yd1[2] + mXoverCoeffs[1][3] * yd1[4]
                            + -(xd1[2] * 2.0f - (xd1[4] + xd1[0]));
                        yd1[4] = fVar2;

                        // Pass 2
                        float *xd1b = &mXoverInputDelay[chan][15];
                        float *yd1b = &mXoverOutputDelay[chan][15];
                        xd1b[0] = xd1b[1];
                        float oldX1b_2 = xd1b[2];
                        xd1b[2] = xd1b[3];
                        xd1b[1] = oldX1b_2;
                        xd1b[3] = xd1b[4];
                        xd1b[4] = fVar2 / mXoverGain[1];
                        yd1b[0] = yd1b[1];
                        float oldY1b_2 = yd1b[2];
                        yd1b[2] = yd1b[3];
                        yd1b[3] = yd1b[4];
                        yd1b[1] = oldY1b_2;
                        float fVar4 = mXoverCoeffs[1][0] * yd1b[0] + mXoverCoeffs[1][1] * yd1b[1]
                            + mXoverCoeffs[1][2] * yd1b[2] + mXoverCoeffs[1][3] * yd1b[4]
                            + -(xd1b[2] * 2.0f - (xd1b[4] + xd1b[0]));
                        yd1b[4] = fVar4;

                        // Crossover filter stage 2 (highpass): 2nd-order applied twice
                        // Pass 1
                        float *xd2 = &mXoverInputDelay[chan][20];
                        float *yd2 = &mXoverOutputDelay[chan][20];
                        xd2[0] = xd2[1];
                        xd2[1] = xd2[2];
                        xd2[2] = *s / mXoverGain[2];
                        float oldY2 = yd2[1];
                        yd2[1] = yd2[2];
                        yd2[0] = oldY2;
                        fVar2 = mXoverCoeffs[2][0] * yd2[0] + mXoverCoeffs[2][1] * yd2[2]
                            + -(xd2[1] * 2.0f - (xd2[0] + xd2[2]));
                        yd2[2] = fVar2;

                        // Pass 2
                        float *xd2b = &mXoverInputDelay[chan][25];
                        float *yd2b = &mXoverOutputDelay[chan][25];
                        xd2b[0] = xd2b[1];
                        xd2b[1] = xd2b[2];
                        xd2b[2] = fVar2 / mXoverGain[2];
                        float oldY2b = yd2b[1];
                        yd2b[0] = oldY2b;
                        yd2b[1] = yd2b[2];
                        fVar2 = mXoverCoeffs[2][0] * oldY2b + mXoverCoeffs[2][1] * yd2b[2]
                            + -(xd2b[1] * 2.0f - (xd2b[0] + xd2b[2]));
                        yd2b[2] = fVar2;

                        // Mix crossover bands using smoothed gains
                        *s = fVar4 * mBand1A1 + fVar2 * mBand0B2 + mBand2B2 * fVar3;

                        // Smooth interpolated output mix coefficients
                        float k = mSmoothCoeff;
                        mBand0B2 = (mBand0B2 - mBand0B1) * k + mBand0B1;
                        mBand2B2 = (mBand2B2 - mBand2B1) * k + mBand2B1;
                        mBand1A1 = (mBand1A1 - mBand1B2) * k + mBand1B2;
                    }
                }
            }
        }
    } else {
        // Biquad filter path
        MILO_ASSERT(numChans <= 2, 0xd9);
        if (numChans > 0) {
            float *z1Band0 = &mBand0DelayZ1[0];
            float *z1Band2 = &mBand2DelayZ1[0];
            for (int chan = 0; chan < numChans; chan++) {
                if (numSamples > 0) {
                    for (int i = 0; i < numSamples; i++) {
                        float *s = &samples[i * numChans + chan];
                        if (mBand0Enabled) {
                            float x = *s;
                            float z1 = *z1Band0;
                            float coeff = mBand0Z1;
                            float y = -(coeff * z1 - x);
                            *s = (x - (coeff * y + z1)) * mBand0A2 + x;
                            *z1Band0 = y;
                        }
                        if (mBand1Enabled) {
                            float x = *s;
                            float b0 = mBand1B0;
                            float cosCoeff = mBand1Z2;
                            float gainCur = mBand1Z1;
                            float xn1 = mBand1DelayXn[chan];
                            float xn2 = mBand1DelayXn1[chan];
                            mBand1DelayXn1[chan] = xn1;
                            float zn1 = mBand1DelayZ1[chan];
                            mBand1DelayXn[chan] = x;
                            mBand1DelayZ1[chan] = mBand1DelayZ[chan];
                            float y = zn1 * b0 + -(mBand1DelayZ[chan] * cosCoeff - (xn1 * cosCoeff + -(x * b0) + xn2));
                            mBand1DelayZ[chan] = y;
                            *s = (x - y) * gainCur + x;
                        }
                        if (mBand2Enabled) {
                            float x = *s;
                            float z1 = z1Band2[chan];
                            float coeff = mBand2Z1;
                            float y = -(z1 * coeff - x);
                            *s = (coeff * y + z1 + x) * mBand2A2 + x;
                            z1Band2[chan] = y;
                        }
                        if (mBand3Enabled) {
                            float x = *s;
                            float b0 = mBand3B0;
                            float b1 = mBand3B1;
                            float b2 = mBand3B2;
                            float a1 = mBand3A1;
                            float a2 = mBand3A2;
                            float xn = mBand3DelayX[chan][0];
                            float xn1 = mBand3DelayX[chan][1];
                            float zn = mBand3DelayZ[chan][0];
                            float zn1 = mBand3DelayZ[chan][1];
                            mBand3DelayX[chan][1] = xn;
                            mBand3DelayX[chan][0] = x;
                            mBand3DelayZ[chan][1] = zn;
                            float out = -(a2 * zn1 - -(a1 * zn - (b2 * xn1 + b0 * x + b1 * xn)));
                            mBand3DelayZ[chan][0] = out;
                            *s = out;
                        }
                        if (mBand4Enabled) {
                            float x = *s;
                            float b0 = mBand4B0;
                            float b1 = mBand4B1;
                            float b2 = mBand4B2;
                            float a1 = mBand4A1;
                            float a2 = mBand4A2;
                            float xn = mBand4DelayX[chan][0];
                            float xn1 = mBand4DelayX[chan][1];
                            float zn = mBand4DelayZ[chan][0];
                            float zn1 = mBand4DelayZ[chan][1];
                            mBand4DelayX[chan][1] = xn;
                            mBand4DelayX[chan][0] = x;
                            mBand4DelayZ[chan][1] = zn;
                            float out = -(a2 * zn1 - -(a1 * zn - (b2 * xn1 + b0 * x + b1 * xn)));
                            mBand4DelayZ[chan][0] = out;
                            *s = out;
                        }
                        // Smooth interpolated coefficients
                        float k = mSmoothCoeff;
                        mBand0A2 = (mBand0A2 - mBand0A1) * k + mBand0A1;
                        mBand1Z1 = (mBand1Z1 - mBand1A2) * k + mBand1A2;
                        mBand2A2 = (mBand2A2 - mBand2A1) * k + mBand2A1;
                    }
                }
                z1Band0++;
                z1Band2++;
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
    double zero = 0.0;
    double one = 1.0;
    double half = 0.5;
    double val = (double)value;

    switch (param) {
    case 0: {
        // mBand1Freq (0x00): shelf frequency, 0..24000 Hz
        double clamped = val;
        if ((float)(24000.0 - val) < 0.0f) clamped = 24000.0;
        double result = zero;
        if (-clamped < 0.0) result = clamped;
        if (result == (double)mBand1Freq) break;
        mBand1Freq = (float)result;
        updateCrossover = true;
        updateBand0 = true;
        break;
    }
    case 1: {
        // mBand1Gain (0x04): shelf gain, ±42 dB
        double clamped = val;
        if ((float)(42.0 - val) < 0.0f) clamped = 42.0;
        double result = -42.0;
        if ((float)(-42.0 - clamped) < 0.0f) result = clamped;
        if (result == (double)mBand1Gain) break;
        mBand1Gain = (float)result;
        updateBand0 = true;
        break;
    }
    case 2: {
        // mBand1Q (0x08): Q/bandwidth, 0..24000
        double clamped = val;
        if ((float)(24000.0 - val) < 0.0f) clamped = 24000.0;
        double result = zero;
        if (-clamped < 0.0) result = clamped;
        if (result == (double)mBand1Q) break;
        mBand1Q = (float)result;
        updateCrossover = true;
        updateBand1 = true;
        break;
    }
    case 3: {
        // mBand2Freq (0x0C): bell frequency, 0..24000 Hz
        double clamped = val;
        if ((float)(24000.0 - val) < 0.0f) clamped = 24000.0;
        double result = zero;
        if (-clamped < 0.0) result = clamped;
        if (result == (double)mBand2Freq) break;
        mBand2Freq = (float)result;
        updateBand1 = true;
        break;
    }
    case 4: {
        // mBand2Gain (0x10): bell gain, ±42 dB
        double clamped = val;
        if ((float)(42.0 - val) < 0.0f) clamped = 42.0;
        double result = -42.0;
        if ((float)(-42.0 - clamped) < 0.0f) result = clamped;
        if (result == (double)mBand2Gain) break;
        mBand2Gain = (float)result;
        updateBand1 = true;
        break;
    }
    case 5: {
        // mBand2Q (0x14): high shelf frequency, 0..24000 Hz
        double clamped = val;
        if ((float)(24000.0 - val) < 0.0f) clamped = 24000.0;
        double result = zero;
        if (-clamped < 0.0) result = clamped;
        if (result == (double)mBand2Q) break;
        mBand2Q = (float)result;
        updateCrossover = true;
        updateBand2 = true;
        break;
    }
    case 6: {
        // mBand3Freq (0x18): high shelf gain, ±42 dB (name vs semantics mismatch in source)
        double clamped = val;
        if ((float)(42.0 - val) < 0.0f) clamped = 42.0;
        double result = -42.0;
        if ((float)(-42.0 - clamped) < 0.0f) result = clamped;
        if (result == (double)mBand3Freq) break;
        mBand3Freq = (float)result;
        updateBand2 = true;
        break;
    }
    case 7: {
        // mBand3Gain (0x1C): bandpass frequency, 20..20000 Hz (name vs semantics mismatch)
        double clamped = val;
        if ((float)(20000.0 - val) < 0.0f) clamped = 20000.0;
        double result = 20.0;
        if ((float)(20.0 - clamped) < 0.0f) result = clamped;
        if (result == (double)mBand3Gain) break;
        mBand3Gain = (float)result;
        updateBand3 = true;
        break;
    }
    case 8: {
        // mBand3Q (0x20): bandpass Q, ±25 dB
        double clamped = val;
        if ((float)(25.0 - val) < 0.0f) clamped = 25.0;
        double result = -25.0;
        if ((float)(-25.0 - clamped) < 0.0f) result = clamped;
        if (result == (double)mBand3Q) break;
        mBand3Q = (float)result;
        updateBand3 = true;
        break;
    }
    case 9: {
        // mBand4Freq (0x24): bandpass frequency, 20..20000 Hz
        double clamped = val;
        if ((float)(20000.0 - val) < 0.0f) clamped = 20000.0;
        double result = 20.0;
        if ((float)(20.0 - clamped) < 0.0f) result = clamped;
        if (result == (double)mBand4Freq) break;
        mBand4Freq = (float)result;
        updateBand4 = true;
        break;
    }
    case 10: {
        // mBand4Gain (0x28): bandpass gain, ±25 dB
        double clamped = val;
        if ((float)(25.0 - val) < 0.0f) clamped = 25.0;
        double result = -25.0;
        if ((float)(-25.0 - clamped) < 0.0f) result = clamped;
        if (result == (double)mBand4Gain) break;
        mBand4Gain = (float)result;
        updateBand4 = true;
        break;
    }
    case 11:
        // mBand4Q (0x2C): crossover enable (float bool: > 0.5)
        mBand4Q = (float)(0.5 < val);
        break;
    case 12: {
        // mBand5Freq (0x30): crossover frequency, 25..5000 Hz
        double clamped = val;
        if ((float)(5000.0 - val) < 0.0f) clamped = 5000.0;
        double result = 25.0;
        if ((float)(25.0 - clamped) < 0.0f) result = clamped;
        mBand5Freq = (float)result;
        double smoothCoeff = one;
        if (result != 0.0) {
            smoothCoeff = (double)(float)pow(kSmoothBase, (double)(1.0f / (float)(result * 48.0)));
        }
        mSmoothCoeff = (float)smoothCoeff;
        break;
    }
    default:
        MILO_ASSERT(false, 0);
        break;
    }

    if (updateBand0) {
        // Low shelf filter (band 0)
        double tanFreq = tan((double)(mBand1Freq * 6.544985e-05f));
        mBand0B0 = (float)tanFreq;
        double gainTarget = pow(10.0, (double)(mBand1Gain * 0.05f));
        double gainF = (double)(float)gainTarget;
        mBand0B1 = (float)gainTarget;
        float shelfTarget = (float)((gainF - one) * half);
        mBand0A1 = shelfTarget;
        if (mBand0A2 != zero || (double)shelfTarget != zero) {
            mBand0Enabled = true;
        } else {
            mBand0Enabled = false;
        }
        float coeff;
        if ((double)mBand1Gain <= zero) {
            coeff = (float)((gainF * (double)mBand0B0 - one) / (gainF * (double)mBand0B0 + one));
        } else {
            coeff = (float)((double)mBand0B0 - one) / (float)((double)mBand0B0 + one);
        }
        mBand0Z1 = coeff;
    } else if (updateBand1) {
        // Bell/peaking filter (band 1)
        double tanFreq = tan((double)(mBand2Freq * 6.544985e-05f));
        mBand1B1 = (float)tanFreq;
        double gainTarget = pow(10.0, (double)(mBand2Gain * 0.05f));
        double gainF = (double)(float)gainTarget;
        mBand1B2 = (float)gainTarget;
        mBand1A2 = (float)((gainF - one) * half);
        double cosQ = cos((double)(mBand1Q * 0.0001308997f));
        mBand1Z2 = -(float)cosQ;
        if ((double)mBand1Z1 != zero || (double)mBand1A2 != zero) {
            mBand1Enabled = true;
        } else {
            mBand1Enabled = false;
        }
        float coeff1;
        if ((double)mBand2Gain <= zero) {
            coeff1 = (float)((tanFreq - gainF) / (tanFreq + gainF));
        } else {
            coeff1 = (float)(tanFreq - one) / (float)(tanFreq + one);
        }
        mBand1B0 = coeff1;
        mBand1Z2 = (float)(one - coeff1) * (-(float)cosQ);
    } else if (updateBand2) {
        // High shelf filter (band 2) — reads mBand3Freq (0x18) for gain
        double tanFreq = tan((double)(mBand2Q * 6.544985e-05f));
        mBand2B0 = (float)tanFreq;
        double gainTarget = pow(10.0, (double)(mBand3Freq * 0.05f));  // 0x18 = band3 gain semantically
        double gainF = (double)(float)gainTarget;
        mBand2B1 = (float)gainTarget;
        float shelfTarget = (float)((gainF - one) * half);
        mBand2A1 = shelfTarget;
        if ((double)mBand2A2 != zero || (double)shelfTarget != zero) {
            mBand2Enabled = true;
        } else {
            mBand2Enabled = false;
        }
        float coeff;
        if ((double)mBand3Freq <= zero) {  // 0x18 holds band3 gain
            coeff = (float)(((double)mBand2B0 - gainF) / ((double)mBand2B0 + gainF));
        } else {
            coeff = (float)((double)mBand2B0 - one) / (float)((double)mBand2B0 + one);
        }
        mBand2Z1 = coeff;
    } else if (updateBand3) {
        // Bandpass filter 1 (band 3) — reads mBand3Gain (0x1C) for freq, mBand3Q for gain
        mBand3Enabled = (mBand3Gain < 19999.0f);  // 0x1C holds band3 freq
        double wc = (double)(mBand3Gain * 4.1666666e-05f);
        double invGain = pow(10.0, (double)(mBand3Q * -0.05f));
        double invGainF = (double)(float)invGain;
        double wcPi = (double)(float)(wc * 3.1415927410125732);
        double sinWc = sin(wcPi);
        double alpha = (double)(float)((double)(float)(sinWc * invGainF) * half);
        double k = (double)((float)((double)(float)(one - alpha) * half) / (float)(alpha + one));
        double cosWc = cos(wcPi);
        mBand3A2 = (float)(k * 2.0);
        mBand3A1 = (float)((double)(float)((double)(float)cosWc * (float)(k + half)) * -2.0);
        float fk4 = (float)((k + half) - (double)(float)((double)(float)cosWc * (float)(k + half)));
        float fk2 = fk4 * 2.0f;
        mBand3B0 = fk2;
        mBand3B1 = fk4 * 4.0f;
        mBand3B2 = fk2;
    } else if (updateBand4) {
        // Bandpass filter 2 (band 4)
        mBand4Enabled = (mBand4Freq > 21.0f);
        double wc = (double)(mBand4Freq * 4.1666666e-05f);
        double invGain = pow(10.0, (double)(mBand4Gain * -0.05f));
        double invGainF = (double)(float)invGain;
        double wcPi = (double)(float)(wc * 3.1415927410125732);
        double sinWc = sin(wcPi);
        double alpha = (double)(float)((double)(float)(sinWc * invGainF) * half);
        double k = (double)((float)((double)(float)(one - alpha) * half) / (float)(alpha + one));
        double cosWc = cos(wcPi);
        mBand4A2 = (float)(k * 2.0);
        double cosK = (double)((float)cosWc * (float)(k + half));
        mBand4A1 = (float)(cosK * -2.0);
        float fk4 = (float)((double)(float)(cosK + k) + half) * 0.25f;
        float fk2 = fk4 * 2.0f;
        mBand4B0 = fk2;
        mBand4B1 = fk4 * -4.0f;
        mBand4B2 = fk2;
    }

    if (updateCrossover && mBand4Q != 0.0f) {
        Reset();
    }
}
