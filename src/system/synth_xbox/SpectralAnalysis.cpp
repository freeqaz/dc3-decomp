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

    // Magnitude spectrum back into mData0.
    // NEGATIVE RESULT (w7-ap, 2026-09-14, 85.9 canonical): the image's
    // magnitude loop is ONE induction pointer plus two byte biases --
    // 0x82E4D4AC `subf r10, r11, r8` / 0x82E4D4B0 `subf r9, r11, r9` off the
    // mData5 walker, then `lfs f0, 0x0(r11)` / `lfsx f13, r10, r11` /
    // `stfsx f0, r9, r11` / `addi r11, r11, 0x4` -- exactly the idiom the
    // recombination loop below uses.  Writing it that way (with the biases as
    // char* differences so no srawi/slwi rescale appears, in either
    // declaration order) costs 4.9pp: 85.9 -> 81.0.  The image emits the
    // zero-trip `beq cr6` and the `mtctr` BEFORE the two `subf`s; MSVC puts
    // the biases first whenever they are named locals, which reorders the
    // whole preheader and re-colours r7/r8/r9/r11.  Three walking pointers
    // and the lfsu/lfsu/stfsu update forms remain the better source.
    unsigned int bins = (unsigned int)mHalfPlusOne;
    float *mag = &mData0[0];
    float *im = &mData5[0];
    float *re = &mData4[0];
    if (bins != 0) {
        do {
            float acc = im[0] * im[0];
            acc = re[0] * re[0] + acc;
            mag[0] = sqrtf(acc);
            im++;
            re++;
            mag++;
        } while (--bins != 0);
    }

    // Spectral window recombination over the first half, using the sin/cos
    // table, accumulating the cosine term into mAccum.
    float *data = &mData0[0];
    int half = (unsigned int)mFftSize >> 1;
    float a0 = data[0];
    float aN = data[half];
    float diff0 = a0 - aN;
    float sum0 = aN + a0;
    mAccum = (double)(diff0 * 0.5f);
    data[0] = sum0 * 0.5f;

    unsigned int quarter = (unsigned int)half >> 1;
    // Both tables and the counter are materialised ABOVE the guard in the
    // image: 0x82E4D4E0 `li r9, 0x1`, 0x82E4D4E8 `lwz r8, 0xac(r31)` and
    // 0x82E4D4EC `lwz r7, 0xb8(r31)` all sit before 0x82E4D524 `ble cr6`.
    // The biases are BYTE differences fed straight to `lfsx` (0x82E4D52C /
    // 0x82E4D530 `subf`); a float-element difference makes MSVC emit a
    // srawi/slwi pair to scale it back.
    float *sinT = &mSinTable[0];
    float *cosT = &mCosTable[0];
    unsigned int i = 1;
    if (quarter > 1) {
        long sinBias = (const char *)sinT - (const char *)data;
        long cosBias = (const char *)cosT - (const char *)data;
        float *lo = data + 1;
        float *hi = data + half;
        do {
            float a = lo[0];
            float b = hi[-1];
            float diff = a - b;
            float s = *(const float *)((const char *)lo + sinBias);
            float sum = b + a;
            float c = *(const float *)((const char *)lo + cosBias);
            double acc = mAccum;
            float ps = s * diff;
            sum = sum * 0.5f;
            float pc = c * diff;
            lo[0] = sum - ps;
            // fused decrement-and-store: 0x82E4D570 `stfsu f13, -0x4(r10)`
            *--hi = ps + sum;
            mAccum = (double)pc + acc;
            ++lo;
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

    // Grow the FFT size (power of two) until it spans the window plus hop.
    if (windowSize + hop > 8) {
        // RESIDUAL (w7-ak, 94.2 canonical): the image copies the doubled value into
        // a second register with a no-op `clrrwi r10, r11, 0` and RE-LOADS
        // mWindowSize from 0x0(r31) inside the loop; we fold both away. NEGATIVE
        // RESULTS: `mFftSize = mFftSize * 2;` with the compare on
        // `(unsigned int)mFftSize`, and the same with a trailing
        // `doubled = mFftSize;`, both compile to the SAME worse code (93.6) --
        // MSVC re-derives the shift instead of copying the stored value.
        unsigned int doubled;
        do {
            doubled = (unsigned int)mFftSize * 2;
            mFftSize = doubled;
        } while (doubled < (unsigned int)mWindowSize + hop);
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
    // RESIDUAL (w7-ak, 94.2 canonical): 7 of the remaining 22 rows are the
    // schedule inside this loop.  The image loads mSinTable._M_start AFTER sin()
    // returns (`fmr f0, f1` to park the result, then `lwz r11, 0x0(r27)` and
    // `fmr f1, f31` to set up cos, then `frsp`/`stfsx`), where we pin the base in
    // r25 before the call and store before setting up cos.  Dropping the explicit
    // `(float)` casts is exactly inert (same 22 rows).  The other structural row
    // is a frame 0x10 larger than the image's 0xa0: the image reuses the one
    // 0x50(r1) temp for the `const float&` 0.0f argument of all six
    // assign/resize calls AND for the two int64->double converts in this loop.
    for (unsigned int i = 0; i < ((unsigned int)mFftSize >> 1); i++) {
        double angle = (i * kPi) / (double)((unsigned int)mFftSize >> 1);
        double s = sin(angle);
        mSinTable[i] = (float)s;
        double c = cos(angle);
        mCosTable[i] = (float)c;
    }
}

} // namespace DSP
