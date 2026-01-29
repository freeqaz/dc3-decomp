#include "system/synth_xbox/PitchDetector.h"
#include "system/utl/MemMgr.h"

namespace DSP {

SpectralAnalysis::~SpectralAnalysis() {
    void *temp;

    if (mData5) {
        temp = mData5;
        MemFree(temp, "unknown", 0);
    }

    if (mData4) {
        temp = mData4;
        MemFree(temp, "unknown", 0);
    }

    if (mData3) {
        temp = mData3;
        MemFree(temp, "unknown", 0);
    }

    if (mData2) {
        temp = mData2;
        MemFree(temp, "unknown", 0);
    }

    if (mData1) {
        temp = mData1;
        MemFree(temp, "unknown", 0);
    }

    mFft2.~FftIpp();
    mFft1.~FftIpp();
}

} // namespace DSP
