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
    // Target instruction [9] is `srwi r7, r10, 16` -- a LOGICAL right shift, so
    // the shifted draw contributes only bits 0..15 and the combine with
    // `(s & 0x7FFF0000)` leaves bit 31 clear. Original table words are
    // `0x5665xxxx`-class, never `0xFFFFxxxx`.
    //
    // A bare `int j >> 16` is a SIGNED shift (`srawi`): whenever bit 31 of the
    // draw is set it sign-extends to `0xFFFFxxxx` and poisons the high word of
    // that table entry. Since Seed() builds the whole 256-entry table that every
    // RandomInt/RandomFloat draws from, this corrupted roughly one entry in two.
    // Unicorn showed 20 poisoned words (decomp `0xFFFFA704` vs orig
    // `0x5665A704` -- note the low halves agree exactly, which is the tell).
    //
    // `+` rather than `|` is deliberate. The two fields are disjoint (the mask
    // keeps bits 16..30, the shift yields bits 0..15) so the two are exactly
    // equivalent, but MSVC folds a disjoint `|` into a single `rlwimi` and the
    // target keeps a separate `rlwinm`/`or` pair. Written with `+` we get the
    // target's shape everywhere except the final opcode; written with `|` the
    // fusion costs ~10pp. Every `|` spelling tried (mask to 0xFFFF, unsigned
    // `j`, hoisting the shift to a local, both term orders) fused.
    int s = seed;
    for (int i = 0; i < 0x100; i++) {
        int j = s * 0x41C64E6D + 0x3039;
        s = j * 0x41C64E6D + 0x3039;
        mRandTable[i] = ((unsigned int)j >> 16) + (s & 0x7FFF0000);
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
