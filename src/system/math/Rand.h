#pragma once
#include "os\Debug.h"
#include "utl\MemMgr.h"

class Rand {
public:
    Rand(int);
    void Seed(int);

    int Int() {
        unsigned int ret = mRandTable[mRandIndex1] ^ mRandTable[mRandIndex2];
        mRandTable[mRandIndex1] = ret;
        mRandIndex1++;
        mRandIndex2++;
        if (0xF9 <= mRandIndex1) {
            mRandIndex1 = 0;
        }
        if (0xF9 <= mRandIndex2) {
            mRandIndex2 = 0;
        }
        return ret;
    }

    int Int(int low, int high) {
        MILO_ASSERT(high > low, 0x3A);
#ifdef HX_NATIVE
        // ROOT FIX (Wave 2 Lane A): the target lowers `Int() % (high - low)` with a
        // SIGNED divide (PPC `divw`; see the Rand::Int target asm). `Int()` returns
        // its unsigned table draw reinterpreted as a signed int, so when the top
        // bit is set the remainder is NEGATIVE and the result falls outside
        // [low, high). On the Xbox a small out-of-range index hits tolerated scratch
        // heap (benign); on the host, callers that use the result to index a
        // std::vector / ObjPtrVec (e.g. Fisher-Yates shuffles, FlowPickOne's random
        // child pick) get an out-of-bounds access that corrupts the heap and crashes
        // (it took down dc3-native's boot twice). Fold the same single draw into
        // [low, high) with UNSIGNED modulo so every index-from-random caller is
        // safe. Consumes exactly one Int() draw, matching the Xbox call count. The
        // PPC build path below is byte-identical to the original (guarded).
        return low + (int)((unsigned)Int() % (unsigned)(high - low));
#else
        return (Int() % (high - low)) + low;
#endif
    }

    int FastInt(int low, int high) {
        MILO_ASSERT(high > low, 0x33);
        // og's masked form is bounds-safe (Int() masked to 16 bits before the
        // multiply, so no 32-bit overflow corruption) — matches target and is
        // correct on native. Native sign-off pending; if native semantics must
        // differ, re-add an #ifdef HX_NATIVE modulo variant here.
        return ((Int() & 0xFFFF) * (high - low) >> 16) + low;
    }

    float Float();
    float Float(float, float);
    float Gaussian();

    static Rand sRand;

#ifdef HX_NATIVE
    // Native-only test hook. Seed() builds the whole 256-entry table that every
    // RandomInt/RandomFloat draw reads, and the sign-extension defect this class
    // has a regression test for (`srawi` vs `srwi`) is a property OF THE TABLE,
    // not of the draw stream: Int() returns `table[i] ^ table[j]`, and XOR
    // destroys the `0xFFFFxxxx` signature (two poisoned words cancel to
    // `0x0000xxxx`), which is why the original draw-level probe could never fire.
    // Expose the raw words so the guard can assert the invariant directly.
    // Guarded by HX_NATIVE: the PPC build never sees this and its codegen is
    // byte-identical.
    unsigned int TableWordForTest(int i) const { return mRandTable[i]; }
    static int TableSizeForTest() { return 256; }
#endif

    MEM_OVERLOAD(Rand, 0x16)

private:
    unsigned int mRandIndex1;
    unsigned int mRandIndex2;
    unsigned int mRandTable[256];
    float mSpareGaussianValue;
    bool mSpareGaussianAvailable;
};

void SeedRand(int);
int RandomInt();
int RandomInt(int, int);
float RandomFloat();
float RandomFloat(float, float);

// std::random_shuffle was removed in C++17 (Emscripten/Clang).
// This wrapper uses std::shuffle on native, std::random_shuffle on PPC.
#ifdef HX_NATIVE
#include <random>
#include <algorithm>
template <typename Iter>
inline void RandomShuffle(Iter first, Iter last) {
    std::shuffle(first, last, std::default_random_engine(RandomInt()));
}
#else
#include <algorithm>
template <typename Iter>
inline void RandomShuffle(Iter first, Iter last) {
    std::random_shuffle(first, last);
}
#endif
