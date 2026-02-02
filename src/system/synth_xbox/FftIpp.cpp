#include "synth_xbox/FftIpp.h"
#include "types.h"
#include "utl/MemMgr.h"
#include <cstdlib>

extern void merged_827BD118(void *, void *);
extern void CalculateSinCosTable(int, void *);

void FftIpp::FftRealCcs(unsigned int *, volatile float &, unsigned int *, float &) {}

void FftIpp::FftReal(
    unsigned int *param1, volatile float &param2, unsigned int *, float &, volatile float &
) {}

FftIpp::~FftIpp() {
}

FftIpp::FftIpp()
    : unk0(0), unk4(0) {}

void FftIpp::SetMode(int mode) {
    unk0 = mode;
    unk4 = 1;
    if (mode > 2) {
        do {
            unk4 = unk4 + 1;
        } while ((1 << unk4) < unk0);
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
    if ((unsigned int)unk0 < (unsigned int)size2) {
        merged_827BD118((void *)(&mBuf2.mBegin + 1), (void *)((unk0 * 4) + mBuf2.mBegin));
    } else {
        merged_827BD118((void *)(&mBuf2.mBegin + 1), (void *)(&mBuf2.mBegin + 1));
    }

    // Third vector - offset 0x20
    int size3 = (mBuf3.mEnd - mBuf3.mBegin) >> 2;
    if ((unsigned int)unk0 < (unsigned int)size3) {
        merged_827BD118((void *)(&mBuf3.mBegin + 1), (void *)((unk0 * 4) + mBuf3.mBegin));
    } else {
        merged_827BD118((void *)(&mBuf3.mBegin + 1), (void *)(&mBuf3.mBegin + 1));
    }

    // Fourth vector - offset 0x38 (sincos)
    int size4 = (mSinCos.mEnd - mSinCos.mBegin) >> 2;
    if ((unsigned int)unk0 < (unsigned int)size4) {
        merged_827BD118((void *)(&mSinCos.mBegin + 1), (void *)((unk0 * 4) + mSinCos.mBegin));
    } else {
        merged_827BD118((void *)(&mSinCos.mBegin + 1), (void *)(&mSinCos.mBegin + 1));
    }

    CalculateSinCosTable(unk0 / 2, (void *)(&mSinCos.mBegin + 1));
}
