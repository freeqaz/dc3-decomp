#pragma once

#include "system/synth_xbox/FftIpp.h"

namespace DSP {

class SpectralAnalysis {
public:
    ~SpectralAnalysis();
    SpectralAnalysis();

    int unk0;               // 0x00
    int unk4;               // 0x04
    int unk8;               // 0x08
    FftIpp mFft1;           // 0x0C
    FftIpp mFft2;           // 0x50
    IppBuf mData0;          // 0x94
    IppBuf mData1;          // 0xA0
    IppBuf mData2;          // 0xAC
    IppBuf mData3;          // 0xB8
    IppBuf mData4;          // 0xC4
    IppBuf mData5;          // 0xD0
};

} // namespace DSP
