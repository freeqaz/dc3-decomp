#pragma once

#include "utl/MemMgr.h"

struct IppBuf {
    unsigned int mBegin;
    unsigned int mEnd;
    unsigned int mCap;
    IppBuf() : mBegin(0), mEnd(0), mCap(0) {}
    ~IppBuf() {
        void *&p = (void *&)mBegin;
        if (p) {
            void *temp = p;
            MemFree(temp);
        }
    }
};

class FftIpp {
public:
    void FftRealCcs(unsigned int *, volatile float &, unsigned int *, float &);
    void
    FftReal(unsigned int *, volatile float &, unsigned int *, float &, volatile float &);
    ~FftIpp();
    FftIpp();
    void SetMode(int);

    int unk0;
    int unk4;
    IppBuf mBuf1;   // 0x08
    IppBuf mBuf2;   // 0x14
    IppBuf mBuf3;   // 0x20
    IppBuf mBuf4;   // 0x2C
    IppBuf mSinCos; // 0x38
};
