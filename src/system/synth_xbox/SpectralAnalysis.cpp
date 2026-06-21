// Decompiled from assembly
#include "PitchDetector.h"
#include <math.h>

namespace DSP {

void SpectralAnalysis::SetMode(unsigned int windowSize, unsigned int hop) {
    mWindowSize = windowSize;
    mFftSize = 8;
    if (hop == (unsigned int)-1) {
        hop = windowSize;
    }

    // Grow the FFT size (power of two) until it spans the window plus hop.
    if (windowSize + hop > 8) {
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
    mData1.resize(((unsigned int)mFftSize >> 1) + 2, 0.0f);
    mData4.resize(((unsigned int)mFftSize >> 1) + 1, 0.0f);
    mData5.resize(((unsigned int)mFftSize >> 1) + 1, 0.0f);
    mSinTable.resize((unsigned int)mFftSize >> 1, 0.0f);
    mCosTable.resize((unsigned int)mFftSize >> 1, 0.0f);

    // Precompute the analysis-window sin/cos table over [0, pi).
    for (unsigned int i = 0; i < ((unsigned int)mFftSize >> 1); i++) {
        double angle = (i * 3.141592653589793) / (double)((unsigned int)mFftSize >> 1);
        mSinTable[i] = (float)sin(angle);
        mCosTable[i] = (float)cos(angle);
    }
}

} // namespace DSP
