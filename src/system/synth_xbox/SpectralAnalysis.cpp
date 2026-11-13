// Decompiled from assembly
#include "PitchDetector.h"
#include <cstring>
#include <math.h>

namespace DSP {

// The image reloads pi from an unnamed .rdata double (lbl_82263898) on EVERY
// iteration of the sin/cos table loop rather than pinning it in a callee-saved
// FPR, and the symbol is NOT one of MSVC's `__real@...` literal-pool entries --
// so it is a file-scope constant in the original, not a literal in the
// expression.  Spelling it as a literal costs f31 to the hoisted constant and
// pushes the angle into f30.
static const double kPi = 3.141592653589793;

void SpectralAnalysis::Analyze(const float *in, float *out) {
    // Copy the input window into the analysis buffer and zero-pad the rest.
    if ((unsigned int)mWindowSize != 0) {
        memcpy(&mData0[0], in, mWindowSize * 4);
    }
    if (mFftSize - mWindowSize != 0) {
        memset(&mData0[0] + mWindowSize, 0, (mFftSize - mWindowSize) * 4);
    }

    // Forward real FFT -> real parts in mData4, imag parts in mData5.
    mFft1.FftReal(&mData0[0], &mData4[0], &mData5[0]);

    // Lane history (15 lines moved): docs/decomp/notes/system/synth_xbox/SpectralAnalysis.md
    unsigned int bins = (unsigned int)mHalfPlusOne;
    float *mag = &mData0[0];
    float *im = &mData5[0];
    float *re = &mData4[0];
    for (unsigned int k = 0; k < bins; k++) {
        float acc = re[k] * re[k];
        acc = im[k] * im[k] + acc;
        mag[k] = sqrtf(acc);
    }

    // Lane history (23 lines moved): docs/decomp/notes/system/synth_xbox/SpectralAnalysis.md
    // > Spectral window recombination over the first half, using the sin/cos table,
    // > accumulating the cosine term into mAccum.
    float *data = &mData0[0];
    int half = (unsigned int)mFftSize >> 1;
    float *sinT = &mSinTable[0];
    float *cosT = &mCosTable[0];
    int i = 1;
    float a0 = data[0];
    float aN = data[half];
    float diff0 = a0 - aN;
    float sum0 = aN + a0;
    mAccum = (double)(diff0 * 0.5f);
    data[0] = sum0 * 0.5f;

    unsigned int quarter = (unsigned int)half >> 1;
    if (quarter > 1) {
        do {
            float a = data[i];
            float b = data[half - i];
            float diff = a - b;
            float c = cosT[i];
            float sum = b + a;
            float s = sinT[i];
            double acc = mAccum;
            float pc = c * diff;
            sum = sum * 0.5f;
            float ps = s * diff;
            data[i] = sum - ps;
            data[half - i] = ps + sum;
            mAccum = (double)pc + acc;
            ++i;
        } while (i < quarter);
    }

    // Inverse-CCS transform of the recombined spectrum into mData1.
    // 0x82E4D584/0x82E4D588 emit only `addi r3, r31, 0x50` and
    // `lwz r5, 0xa0(r31)`: r4 still holds &mData0[0] from 0x82E4D4DC and is
    // never clobbered, so the second argument is the only pointer reloaded.
    mFft2.FftRealCcs(data, &mData1[0]);

    // Emit the result: real parts directly, imaginary derivative from mAccum.
    int j = 0;
    for (unsigned int k = 0; k < (unsigned int)mWindowSize; k += 2) {
        out[j] = mData1[j];
        double acc = mAccum;
        float imag = mData1[j + 1];
        mAccum = acc - (double)imag;
        if (k + 1 < (unsigned int)mWindowSize) {
            out[j + 1] = (float)mAccum;
        }
        j += 2;
    }
}

void SpectralAnalysis::SetMode(unsigned int windowSize, unsigned int hop) {
    mWindowSize = windowSize;
    mFftSize = 8;
    if (hop == (unsigned int)-1) {
        hop = windowSize;
    }

    // Lane history (5 lines moved): docs/decomp/notes/system/synth_xbox/SpectralAnalysis.md
    // > Grow the FFT size (power of two) until it spans the window plus hop.
    while ((unsigned int)mFftSize < (unsigned int)mWindowSize + hop) {
        mFftSize = (unsigned int)mFftSize * 2;
    }

    mHalfPlusOne = ((unsigned int)mFftSize >> 1) + 1;
    mFft1.SetMode(mFftSize);
    mFft2.SetMode((unsigned int)mFftSize >> 1);

    mData0.assign(mFftSize, 0.0f);
    mData1.resize((unsigned int)mFftSize + 2, 0.0f);
    mData4.resize(((unsigned int)mFftSize >> 1) + 1, 0.0f);
    mData5.resize(((unsigned int)mFftSize >> 1) + 1, 0.0f);
    mSinTable.resize((unsigned int)mFftSize >> 1, 0.0f);
    mCosTable.resize((unsigned int)mFftSize >> 1, 0.0f);

    // Precompute the analysis-window sin/cos table over [0, pi).
    for (unsigned int i = 0; i < ((unsigned int)mFftSize >> 1); i++) {
        double angle = (i * kPi) / (double)((unsigned int)mFftSize >> 1);
        double s = sin(angle);
        mSinTable[i] = (float)s;
        double c = cos(angle);
        mCosTable[i] = (float)c;
    }
}

} // namespace DSP
