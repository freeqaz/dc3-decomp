#line 1 "synapse_apo\\FftIpp.cpp"
#include "synth_xbox\FftIpp.h"
#include "types.h"
#include "os\Debug.h"
#include <cstring>
#include <stdarg.h>

extern int CalculateSinCosTable(long, float *);
int FFTRealForward(float *data, unsigned long size, float *context);
extern "C" int _vsprintf_s_l(void *, char *, unsigned int, const char *, void *, va_list);

void FftIpp::FftRealCcs(const float *__restrict in, float *__restrict out) {
    if ((unsigned int)mSize != 0) {
        memcpy(&mBuf3[0], in, mSize * 4);
    }

    int iRetVal = FFTRealForward(&mBuf3[0], (unsigned long)mSize, &mSinCos[0]);
    MILO_ASSERT(iRetVal == 0, 0x65);

    unsigned int n = (unsigned int)mSize;
    if (n != 0) {
        memcpy(out, &mBuf3[0], n * 4);
    }

    out[n] = out[1];
    out[n + 1] = 0.0f;
    out[1] = 0.0f;
}

void FftIpp::FftReal(
    const float *__restrict in, float *__restrict outRe, float *__restrict outIm
) {
    if ((unsigned int)mSize != 0) {
        memcpy(&mBuf3[0], in, mSize * 4);
    }

    FFTRealForward(&mBuf3[0], (unsigned long)mSize, &mSinCos[0]);

    int n = mSize;
    int half = n >> 1;
    if ((unsigned int)half > 1) {
        float *packed = &mBuf3[0];
        int byteOff = 8;
        float *im = outIm + 1;
        // BYTE bias, not an element count: the target computes outRe - outIm
        // with a single subf and feeds it straight to stfsx.  Spelling it as a
        // float* difference costs an extra srawi/slwi pair that cancel out.
        long reBias = (char *)outRe - (char *)outIm;
        int i = 1;
        do {
            // Even slot -> real out, odd slot -> imag out.  Both reads are
            // written as (packed + byteOff) rather than through a `cur`
            // pointer: naming the intermediate lets MSVC strength-reduce
            // byteOff away into a single running pointer, which costs the
            // target's lfsx/add pair.
            *(float *)((char *)im + reBias) = *(float *)((char *)packed + byteOff);
            ++i;
            byteOff += 8;
            im[0] = *(float *)((char *)packed + byteOff - 4);
            im += 1;
        } while ((unsigned int)i < (unsigned int)half);
    }

    outIm[0] = 0.0f;
    outRe[0] = mBuf3[0];
    outRe[n] = mBuf3[1];
    outIm[n] = 0.0f;
}

FftIpp::~FftIpp() {
}

FftIpp::FftIpp()
    : mSize(0), mOrder(0) {}

void FftIpp::SetMode(int mode) {
    mSize = mode;
    mOrder = 1;
    if (mSize > 2) {
        // Semantically `do { mOrder++; } while ((1 << mOrder) < mSize);` --
        // smallest order whose power of two covers mSize.  The target reloads
        // both members from memory on every iteration and evaluates mSize
        // BEFORE mOrder, so the volatile spellings are load-order scaffolding,
        // not semantics.  The volatile *store* is what pins the mSize read
        // below the increment; without it MSVC hoists the read out of the loop.
        do {
            *(volatile int *)&mOrder = mOrder + 1;
            int s = *(volatile int *)&mSize;
            int o = *(volatile int *)&mOrder;
            if ((1 << o) < s) continue;
            break;
        } while (true);
    }

    mBuf1.resize(mSize);
    mBuf2.resize(mSize);
    mBuf3.resize(mSize);
    mSinCos.resize(mSize);

    CalculateSinCosTable(mSize / 2, &mSinCos[0]);
}
