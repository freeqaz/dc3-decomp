#include "synth_xbox/FftIpp.h"
#include "types.h"
#include "utl/MemMgr.h"
#include <cstring>
#include <stdarg.h>

extern void merged_827BD118(void *, void *);
extern void CalculateSinCosTable(int, void *);
extern "C" int FFTRealForward(unsigned int *, unsigned int, float *, int, int);
extern "C" int _vsprintf_s_l(void *, char *, unsigned int, const char *, void *, va_list);

void FftIpp::FftRealCcs(
    unsigned int *param1, volatile float &param2, unsigned int *param3, float &param4
) {
    unsigned int *inData = param1;
    float *outData = (float *)&param2;
    unsigned int n = *param3;

    memcpy(&param4, (void *)param1, n * 4);

    int result = FFTRealForward(inData, n, outData, 0, 0);
    (void)result;
}

void FftIpp::FftReal(
    unsigned int *param1, volatile float &param2, unsigned int *param3, float &param4,
    volatile float &param5
) {
    unsigned int *inData = param1;
    float *outData = (float *)&param2;
    unsigned int *tmpBuf = param3;
    unsigned int n = *param3;
    float *outCcs = (float *)&param5;

    memcpy((void *)inData, (void *)outData, n * 4);

    int result = FFTRealForward(inData, n, outData, 0, 0);
    (void)result;

    result = FFTRealForward(inData, n, outCcs, 1, 0);
    (void)result;
}

FftIpp::~FftIpp() {
}

FftIpp::FftIpp()
    : mSize(0), mOrder(0) {}

void FftIpp::SetMode(int mode) {
    mSize = mode;
    mOrder = 1;
    if (mode > 2) {
        do {
            mOrder = mOrder + 1;
        } while ((1 << mOrder) < mSize);
    }

    // First vector - offset 0x8 (begin, end, cap)
    int size1 = (mBuf1.mEnd - mBuf1.mBegin) >> 2;
    float zero = 0.0f;
    if ((unsigned int)mode < (unsigned int)size1) {
        merged_827BD118((void *)(&mBuf1.mBegin + 1), (void *)((mode * 4) + mBuf1.mBegin));
    } else {
        merged_827BD118((void *)(&mBuf1.mBegin + 1), (void *)(&mBuf1.mBegin + 1));
    }

    // Second vector - offset 0x14
    int size2 = (mBuf2.mEnd - mBuf2.mBegin) >> 2;
    if ((unsigned int)mSize < (unsigned int)size2) {
        merged_827BD118((void *)(&mBuf2.mBegin + 1), (void *)((mSize * 4) + mBuf2.mBegin));
    } else {
        merged_827BD118((void *)(&mBuf2.mBegin + 1), (void *)(&mBuf2.mBegin + 1));
    }

    // Third vector - offset 0x20
    int size3 = (mBuf3.mEnd - mBuf3.mBegin) >> 2;
    if ((unsigned int)mSize < (unsigned int)size3) {
        merged_827BD118((void *)(&mBuf3.mBegin + 1), (void *)((mSize * 4) + mBuf3.mBegin));
    } else {
        merged_827BD118((void *)(&mBuf3.mBegin + 1), (void *)(&mBuf3.mBegin + 1));
    }

    // Fourth vector - offset 0x38 (sincos)
    int size4 = (mSinCos.mEnd - mSinCos.mBegin) >> 2;
    if ((unsigned int)mSize < (unsigned int)size4) {
        merged_827BD118((void *)(&mSinCos.mBegin + 1), (void *)((mSize * 4) + mSinCos.mBegin));
    } else {
        merged_827BD118((void *)(&mSinCos.mBegin + 1), (void *)(&mSinCos.mBegin + 1));
    }

    CalculateSinCosTable(mSize / 2, (void *)(&mSinCos.mBegin + 1));
}
