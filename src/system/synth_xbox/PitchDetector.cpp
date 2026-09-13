#include "PitchDetector.h"
#include "utl\MemMgr.h"
#include "IPP_basicmath_xbox.h"
#include <math.h>

namespace DSP {

// The target parks SpectralAnalysis's ctor/dtor in PitchDetector.obj (Analyze
// and SetMode live in SpectralAnalysis.obj).  Both are implicit: the ctor only
// default-constructs the two FftIpp members and the six float vectors, and the
// dtor only runs their destructors in reverse.
SpectralAnalysis::SpectralAnalysis() {
}

SpectralAnalysis::~SpectralAnalysis() {
}

namespace Synapse {

PitchDetector::PitchDetector(const stlpmtx_std::vector<float, stlpmtx_std::StlNodeAlloc<float> > &input,
                             unsigned int windowSize, unsigned int hop)
    : mInput(&input), mWindowSize(windowSize), mHop(hop), mFrequency((float)windowSize),
      mConfidence(0.0f), mClarity(0.0f) {
    mSpectral.SetMode((unsigned int)((float)mHop * 1.7999999523162842f), mHop);

    mSpectrum.resize(mSpectral.mWindowSize, 0.0f);
    mWindow.resize(mSpectral.mWindowSize, 0.0f);

    // Hann analysis window.
    for (unsigned int i = 0; i < mWindow.size(); i++) {
        float size = (float)mWindow.size();
        float angle = ((float)i + 0.5f) * 6.2831854820251465f;
        float c = 1.0f - (float)cos((double)(angle / size));
        mWindow[i] = c;
    }

    mWeight.resize(mHop + 1, 0.0f);

    // Per-harmonic weighting curve.
    for (unsigned int i = 0; i < mWeight.size(); i++) {
        float size = (float)mWindow.size();
        float angle = (float)i * 1.5707963705062866f;
        float c = 1.0f - (float)cos((double)(angle / size));
        mWeight[i] = c * 4.0f + 1.0f;
    }
}

PitchDetector::~PitchDetector() {
}

void PitchDetector::Detect(unsigned int frame) {
    unsigned int span = mSpectral.mWindowSize;

    // Locate the analysis window inside the circular input buffer.  The target
    // re-derives the buffer length at each of its three uses rather than
    // caching it, and divides unsigned.
    unsigned int pos = (unsigned int)(mInput->end() - mInput->begin() - span + frame + 1)
        % (unsigned int)(mInput->end() - mInput->begin());
    unsigned int start = (unsigned int)(mInput->end() - mInput->begin()) - pos;
    // Bound as a const reference, not a copy: both arms are unmodified lvalues of
    // the same type, so this aliases span/start rather than materialising a new
    // slot.  Measured -- the plain `unsigned int firstLen = ...` copy scores 81.5
    // against this spelling's 82.5, so the alias is load-bearing, not incidental.
    const unsigned int &firstLen = (start >= span) ? span : start;

    IPP::Mul(firstLen, &mInput->begin()[pos], &mWindow[0], &mSpectrum[0]);
    if (firstLen != span) {
        IPP::Mul(span - firstLen, &mWindow[firstLen], mInput->begin(), &mSpectrum[firstLen]);
    }

    mSpectral.Analyze(&mSpectrum[0], &mSpectrum[0]);
    IPP::Mul_InPlace(mHop + 1, &mWeight[0], &mSpectrum[0]);

    // Skip the initial monotonically-decreasing region of the spectrum.
    unsigned int lo = 0;
    unsigned int i = 1;
    if (((mWindowSize + mHop) & ~1u) > 2) {
        while (mSpectrum[i] < mSpectrum[i - 1]) {
            lo = i;
            i++;
            if (i >= ((mHop + mWindowSize) >> 1)) break;
        }
    }
    if (lo < mWindowSize) {
        lo = mWindowSize;
    }

    // Weighted peak search across the candidate band.
    unsigned int best = lo;
    float bestScore = 0.0f;
    if (lo <= mHop) {
        for (unsigned int j = lo; j <= mHop; j++) {
            float score = mSpectrum[j] * 1.5f + (mSpectrum[j - 1] + mSpectrum[j + 1]);
            if (bestScore < score) {
                bestScore = score;
                best = j;
            }
        }
    }

    // Parabolic interpolation around the peak bin.
    float left = mSpectrum[best - 1];
    float center = mSpectrum[best];
    float right = mSpectrum[best + 1];
    float curvature = center * 2.0f - right - left;
    float freq;
    if (best > mWindowSize && best < mHop && curvature != 0.0f) {
        float fbest = (float)best;
        float delta = (right - left) / (curvature * 2.0f);
        freq = delta + fbest;
        float lowClamp = fbest - 1.0f;
        float highClamp = fbest + 1.0f;
        if (freq < lowClamp) {
            freq = lowClamp;
        } else if (freq > highClamp) {
            freq = highClamp;
        }
    } else {
        freq = (float)best;
    }

    mFrequency = freq;
    mClarity = mSpectrum[0];
    if (mSpectrum[0] != 0.0f) {
        mConfidence = mSpectrum[best] / mSpectrum[0];
    } else {
        mConfidence = 1.0f;
    }
}

} // namespace Synapse

} // namespace DSP
