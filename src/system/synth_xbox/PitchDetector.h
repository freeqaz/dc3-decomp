#pragma once

#include "system/synth_xbox/FftIpp.h"

namespace DSP {

class SpectralAnalysis {
public:
    ~SpectralAnalysis();
    SpectralAnalysis();

    FftIpp mFft1;           // offset 0x0c
    char pad1[0x44];        // offset 0x50 - padding
    FftIpp mFft2;           // offset 0x50
    char pad2[0x50];        // offset 0xa0 - padding
    void *mData1;           // offset 0xa0
    void *mData2;           // offset 0xac
    void *mData3;           // offset 0xb8
    void *mData4;           // offset 0xc4
    void *mData5;           // offset 0xd0
};

} // namespace DSP
