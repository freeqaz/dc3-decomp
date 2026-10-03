#include "math\Trig.h"
#include "math\Utl.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include <cmath>

float gBigSinTable[0x200];

// w19-c (98.72 -> 100): a plain INDEX loop.  The image's signed pointer test
// (`cmpw cr6, r30, r11` against `addi r11, r29, 0x7fc` recomputed in the loop)
// is MSVC's own linear-function-test replacement of `i < 256` onto the
// strength-reduced &gBigSinTable[i * 2 - 1] cursor -- it keeps the signedness
// of the int compare it replaced, which is why no hand-written pointer loop
// (cmplw), cast (a fourth GPR) or j-stepped index loop (wrong cursor) reached
// it.  The final half-iteration (the delta slot 511) is peeled after the loop.
void TrigTableInit() {
    int i;
    for (i = 0; i < 256; i++) {
        float s = std::sin(0.024543693f * i);
        gBigSinTable[i * 2] = s;
        if (i != 0) {
            gBigSinTable[i * 2 - 1] = s - gBigSinTable[i * 2 - 2];
        }
    }
    float sineValue = std::sin(0.024543693f * i);
    // Spelled through two separate bases because that is what the target
    // emits -- `gBigSinTable[i*2-1]` lets MSVC CSE them into one base and
    // costs four rows.
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
