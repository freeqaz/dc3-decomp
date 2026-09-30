#include "math\Trig.h"
#include "math\Utl.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include <cmath>

float gBigSinTable[0x200];

void TrigTableInit() {
    float *tablePtr = gBigSinTable - 1;
    int i = 0;
    do {
        float sineValue = std::sin(0.024543693f * i);
        tablePtr[1] = sineValue;
        if (i != 0) {
            tablePtr[0] = sineValue - tablePtr[-1];
        }
        tablePtr += 2;
        i++;
        // FLOOR, 98.72: the image's loop test is `cmpw cr6, r30, r11` --
        // SIGNED -- and a plain pointer `<` gives us `cmplw`.  Four spellings
        // measured, none better than this one:
        //   tablePtr < &gBigSinTable[511]      98.72  (1 row: cmpw vs cmplw)
        //   tablePtr < gBigSinTable + 511      98.72  inert, same row
        //   (int)tablePtr < (int)&gBigSinTable[511]        95.6
        //   (int)tablePtr < (int)gBigSinTable + 0x7fc      95.6
        //   i < 256                                        96.6
        // Both casts do buy the signed compare, and both cost a fourth
        // callee-saved GPR: the limit becomes a loop-invariant int in r28, the
        // prologue goes r29-r31 -> r28-r31 and the frame grows 0x10, where the
        // image recomputes `addi r11, r29, 0x7fc` inside the loop.  The index
        // form just compares i (`cmpwi cr6, r31, 0x100`) and drops the pointer
        // test altogether.
        // w9-b 2026-09-30: floor re-confirmed.  Two more spellings and a permuter
        // sweep, none better:
        //   int tablePtr = (int)gBigSinTable - 4, accesses through
        //     *(float *)(tablePtr + 4), test tablePtr < (int)gBigSinTable + 0x7fc
        //     -- 95.574.  Making the CURSOR an int rather than casting it at the
        //     comparison costs exactly the same fourth GPR.
        //   float *table = gBigSinTable; ... (int)tablePtr < (int)table + 0x7fc
        //     -- 87.043.  Hoisting the base into a named local so the limit would
        //     have to be rematerialised from an already-live register -- the
        //     two-axis idea that closed UILabelDir::GetStateColor -- is much worse.
        //   decomp-synth beam search, 3 depths, 86 viable variants, with
        //     signed_unsigned and comparison_flip both in the selected pattern
        //     set: 0 improved over the 98.72 baseline.
        // Still unexplained: how MSVC reaches a SIGNED compare of two pointers at
        // all.  Every spelling that buys `cmpw` also turns the limit into a
        // loop-invariant int, where the image recomputes `addi r11, r29, 0x7fc`
        // inside the loop.
    } while (tablePtr < &gBigSinTable[511]);
    float sineValue = std::sin(0.024543693f * i);
    // Peeled last half-iteration: writes the odd (delta) slot 2i-1 and reads the
    // even (sine) slot 2i-2, exactly as the loop body does.  Spelled through two
    // separate bases because that is what the target emits -- `gBigSinTable[i*2-1]`
    // lets MSVC CSE them into one base and costs four rows.
    (gBigSinTable + 1)[i * 2 - 2] = sineValue - gBigSinTable[i * 2 - 2];
}

void TrigTableTerminate() {}

inline float Lookup(float arg8) {
    float scaledArg = arg8 * 40.743664f;
    int index = (int)scaledArg;
    int idx = (index & 0xFF) * 2;
    float *offset = &gBigSinTable[idx];
    float res = scaledArg - (float)index;
    return (res * offset[1]) + offset[0];
}

float Sine(float arg8) {
    if (arg8 < 0.0f) {
        return -Lookup(-arg8);
    } else
        return Lookup(arg8);
}

float FastSin(float f) {
    if (f < 0.0f) {
        return -gBigSinTable[((int)(-40.743664f * f + 0.49999f) & 0xFF) * 2];
    } else
        return gBigSinTable[((int)(40.743664f * f + 0.49999f) & 0xFF) * 2];
}

DataNode DataSin(DataArray *a) { return (float)sin(DegreesToRadians(a->Float(1))); }
DataNode DataCos(DataArray *da) { return std::cos(DegreesToRadians(da->Float(1))); }
DataNode DataTan(DataArray *da) { return std::tan(DegreesToRadians(da->Float(1))); }

DataNode DataASin(DataArray *da) {
    float f = da->Float(1);
    if (IsNaN(f))
        return 0.0f;
    else
        return RadiansToDegrees(std::asin(f));
}

DataNode DataACos(DataArray *da) {
    float f = da->Float(1);
    if (IsNaN(f))
        return 0.0f;
    else
        return RadiansToDegrees(std::acos(f));
}

DataNode DataATan(DataArray *da) {
    float f = da->Float(1);
    if (IsNaN(f))
        return 0.0f;
    else
        return RadiansToDegrees(std::atan(f));
}

void TrigInit() {
    DataRegisterFunc("sin", DataSin);
    DataRegisterFunc("cos", DataCos);
    DataRegisterFunc("tan", DataTan);
    DataRegisterFunc("asin", DataASin);
    DataRegisterFunc("acos", DataACos);
    DataRegisterFunc("atan", DataATan);
}
