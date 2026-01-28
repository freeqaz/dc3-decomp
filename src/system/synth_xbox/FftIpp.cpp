#include "synth_xbox/FftIpp.h"
#include "types.h"
#include "utl/MemMgr.h"

extern void merged_827BD118(void *, void *);
extern void CalculateSinCosTable(int, void *);

void FftIpp::FftRealCcs(unsigned int *, volatile float &, unsigned int *, float &) {}

void FftIpp::FftReal(
    unsigned int *param1, volatile float &param2, unsigned int *, float &, volatile float &
) {}

FftIpp::~FftIpp() {
    if (unk38 != 0) {
        MemFree((void *)unk38);
    }

    if (unk2c != 0) {
        MemFree((void *)unk2c);
    }

    if (unk20 != 0) {
        MemFree((void *)unk20);
    }

    if (unk14 != 0) {
        MemFree((void *)unk14);
    }

    if (unk8 != 0) {
        MemFree((void *)unk8);
    }
}

FftIpp::FftIpp()
    : unk0(0), unk4(0), unk8(0), unkc(0), unk10(0), unk14(0), unk18(0), unk1c(0),
      unk20(0), unk24(0), unk28(0), unk2c(0), unk30(0), unk34(0), unk38(0), unk3c(0),
      unk40(0) {}

void FftIpp::SetMode(int mode) {
    unk0 = mode;
    unk4 = 1;
    if (mode > 2) {
        do {
            unk4 = unk4 + 1;
        } while ((1 << unk4) < unk0);
    }

    // First vector - offset 0x8 (begin=unk8, end=unkc, cap=unk10)
    int size1 = (unkc - unk8) >> 2;
    float zero = 0.0f;
    if ((unsigned int)mode < (unsigned int)size1) {
        merged_827BD118((void *)(&unk8 + 1), (void *)((mode * 4) + unk8));
    } else {
        merged_827BD118((void *)(&unk8 + 1), (void *)(&unk8 + 1));
    }

    // Second vector - offset 0x14 (begin=unk14, end=unk18, cap=unk1c)
    int size2 = (unk18 - unk14) >> 2;
    if ((unsigned int)unk0 < (unsigned int)size2) {
        merged_827BD118((void *)(&unk14 + 1), (void *)((unk0 * 4) + unk14));
    } else {
        merged_827BD118((void *)(&unk14 + 1), (void *)(&unk14 + 1));
    }

    // Third vector - offset 0x20 (begin=unk20, end=unk24, cap=unk28)
    int size3 = (unk24 - unk20) >> 2;
    if ((unsigned int)unk0 < (unsigned int)size3) {
        merged_827BD118((void *)(&unk20 + 1), (void *)((unk0 * 4) + unk20));
    } else {
        merged_827BD118((void *)(&unk20 + 1), (void *)(&unk20 + 1));
    }

    // Fourth vector - offset 0x38 (begin=unk38, end=unk3c, cap=unk40)
    int size4 = (unk3c - unk38) >> 2;
    if ((unsigned int)unk0 < (unsigned int)size4) {
        merged_827BD118((void *)(&unk38 + 1), (void *)((unk0 * 4) + unk38));
    } else {
        merged_827BD118((void *)(&unk38 + 1), (void *)(&unk38 + 1));
    }

    CalculateSinCosTable(unk0 / 2, (void *)(&unk38 + 1));
}
