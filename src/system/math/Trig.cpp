#include "math/Trig.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include <cmath>

float gBigSinTable[0x200];

// Builds sine lookup table with 256 entries (0x200 floats total)
// Each table entry is 2 floats: [delta from previous, sine value]
// This enables fast interpolated sine lookups in Lookup()
void TrigTableInit() {
    int i = 0;
    float *tablePtr = gBigSinTable - 1;
    do {
        float sineValue = std::sin(0.024543693f * i);
        // Store sine value in odd-indexed slot
        *(tablePtr + 1) = sineValue;
        if (i != 0) {
            // Store delta (current - previous) in even-indexed slot
            *tablePtr = sineValue - *(tablePtr - 1);
        }
        tablePtr += 2;
        i++;
    } while ((long)tablePtr < (long)(gBigSinTable + 0x1FF));
    // Final entry: compute delta for index 256
    float sineValue = std::sin(0.024543693f * i);
    *(tablePtr + 1) = sineValue - *(tablePtr - 1);
}

void TrigTableTerminate() {}

inline float Lookup(float arg8) {
    float x = arg8 * 40.743664f;
    int temp_r5 = (int)x;
    int idx = (temp_r5 & 0xFF) * 2;
    float *offset = &gBigSinTable[idx];
    float res = x - (float)temp_r5;
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
