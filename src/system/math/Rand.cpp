#include "math/Rand.h"
#include "os\Debug.h"
#include "os\OSFuncs.h"
#include <cmath>

Rand Rand::sRand(0x29A);

Rand::Rand(int i)
    : mRandIndex1(0), mRandIndex2(0), mRandTable(), mSpareGaussianAvailable(0) {
    Seed(i);
}

void Rand::Seed(int seed) {
    int s = seed;
    for (int i = 0; i < 0x100; i++) {
        int j = s * 0x41C64E6D + 0x3039;
        s = j * 0x41C64E6D + 0x3039;
#ifdef HX_NATIVE
        // ROOT FIX (Wave 4 Lane B, unicorn flip-list object_memory bug): the target
        // lowers `j >> 16` with a LOGICAL right shift (PPC `srwi`), so the high 16
        // bits of the shifted draw are zero and the OR with `(s & 0x7FFF0000)` keeps
        // bit 31 clear (orig table words are `0x5665xxxx`-class, never `0xFFFFxxxx`).
        // Our `int j >> 16` is a SIGNED shift (`srawi`): when bit 31 of the draw is
        // set it sign-extends to `0xFFFFxxxx` and poisons the high word of the MT
        // state table (unicorn: 20 memory diffs, decomp `0xFFFFxxxx` vs orig
        // `0x5665xxxx`). Masking the shifted value to 16 bits reproduces the logical
        // shift exactly. The PPC build path keeps the original `int` shift verbatim
        // (byte-identical, guarded) — the host compiler will not emit `srwi` for any
        // unsigned spelling here (it fuses to `rlwimi`), so the fix is host-only.
        mRandTable[i] = (s & 0x7FFF0000) | ((j >> 16) & 0xFFFF);
#else
        mRandTable[i] = (s & 0x7FFF0000) | (j >> 16);
#endif
    }
    mRandIndex1 = 0;
    mRandIndex2 = 0x67;
}

float Rand::Float() { return ((Int() & 0xFFFF) / 65536.0f); }
float Rand::Float(float f1, float f2) { return ((f2 - f1) * Float() + f1); }

int RandomInt() {
    MILO_ASSERT(MainThread(), 0x5C);
    return Rand::sRand.Int();
}

int RandomInt(int i1, int i2) {
    MILO_ASSERT(MainThread(), 0x63);
    return Rand::sRand.Int(i1, i2);
}

float RandomFloat() {
    MILO_ASSERT(MainThread(), 0x69);
    return Rand::sRand.Float();
}

float RandomFloat(float f1, float f2) {
    MILO_ASSERT(MainThread(), 0x6F);
    return Rand::sRand.Float(f1, f2);
}

float Rand::Gaussian() {
    float f2, f3, f5;

    if (mSpareGaussianAvailable) {
        mSpareGaussianAvailable = false;
        return mSpareGaussianValue;
    } else {
        do {
            do {
                f2 = Float(-1.0f, 1.0f);
                f3 = Float(-1.0f, 1.0f);
                f5 = f2 * f2 + f3 * f3;
            } while (f5 >= 1.0f);
        } while (0 == f5);
        f5 = sqrtf(-2.0f * (logf(f5) / f5));
        mSpareGaussianValue = f2 * f5;
        mSpareGaussianAvailable = true;
        return f3 * f5;
    }
}

void SeedRand(int seed) { Rand::sRand.Seed(seed); }
