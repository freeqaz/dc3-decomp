// Regression tests for the two pre-existing native boot crashes fixed in
// Execution Wave 2 Lane A:
//
//   1. CameraManager::RandomizeCategory (and FlowPickOne::Activate) — Rand::Int(low,
//      high) lowers to a SIGNED modulo on PPC (`divw`), so when the underlying Int()
//      draws a value with the top bit set, the returned index is NEGATIVE / out of
//      [low, high). On the Xbox a small out-of-range index hits tolerated scratch
//      heap (benign); on the host it is an out-of-bounds std::vector / ObjPtrVec
//      access (Fisher-Yates shuffle; random child pick) that corrupts adjacent heap
//      and SIGSEGVs on the next ObjRef ring link. dc3-native crashed in
//      RandomizeCategory during App construction (CameraManager::SyncObjects ->
//      Randomize), and — once that was unblocked — again in FlowPickOne during the
//      first UI poll, before /api/stubs could be served. The ROOT fix lives in
//      Rand::Int(low, high) (Rand.h, HX_NATIVE): fold the same single draw into
//      [low, high) with UNSIGNED modulo, fixing every index-from-random caller. The
//      PPC path is byte-unchanged.
//
//   2. CharBones::ScaleDown — forms one-past-the-end iterator/bound pointers with
//      &mBones[mCounts[TYPE_END]] (index == mBones.size()). The pointer is never
//      dereferenced, but hardened libstdc++ (debug / RelWithDebInfo without NDEBUG)
//      aborts on the out-of-range operator[]. The fix uses mBones.data() + index,
//      which is bounds-check-free and lowers to identical PPC.
//
// These tests pin the ROOT CAUSE (signed-modulo index, one-past-end addressing) so
// the regressions can't return. They need no game assets.

#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include <cstdint>

#include "math/Rand.h"
#include "char/CharBones.h"
#include "utl/Symbol.h"
#include "test_helpers.h"

// ---------------------------------------------------------------------------
// 1. Rand::Int signed-modulo behavior — the root cause of the crash.
// ---------------------------------------------------------------------------
//
// We do NOT "fix" Rand::Int (it matches the Xbox target asm 1:1 and is shared
// everywhere). We pin that it CAN return values outside [low, high) — that is
// the original-game behavior callers must defend against on the host.

namespace {

// Reproduce the exact PPC formula Rand::Int(low, high) compiles to: a SIGNED
// remainder. `int Int()` may return a negative value (its unsigned table draw
// reinterpreted as signed), and `negative % positive` is <= 0 in C++.
int SignedModInt(int rawDraw, int low, int high) {
    return low + rawDraw % (high - low);
}

} // namespace

TEST(RandIntSignedModulo, NegativeDrawProducesOutOfRangeIndex) {
    // A draw with the top bit set is negative when reinterpreted as int.
    int negDraw = (int)0x80000001u; // INT_MIN + 1
    int idx = SignedModInt(negDraw, 0, 9);
    // Signed modulo => idx is <= 0 and can be negative: NOT a valid [0,9) index.
    EXPECT_LT(idx, 9);
    EXPECT_LE(idx, 0) << "signed modulo of a negative draw should be <= low";
    // This is the value that, used as a std::vector index, corrupts the host heap.
    EXPECT_TRUE(idx < 0 || idx == 0);
}

TEST(RandIntSignedModulo, PositiveDrawStaysInRange) {
    int posDraw = 12345; // positive
    int idx = SignedModInt(posDraw, 2, 9);
    EXPECT_GE(idx, 2);
    EXPECT_LT(idx, 9);
}

TEST(RandIntSignedModulo, FixedSeedDrawsAreNeverNegative) {
    // Wave-4 understanding (supersedes the wave-2 "RawSignedModuloLeavesRange"
    // expectation): Seed() masks every table word with 0x7FFF0000|low16, so bit 31
    // is ALWAYS clear, and Int() only XORs table words — a correctly-seeded
    // generator can never produce a negative draw. The wave-2 boot crash happened
    // only because the buggy native Seed (signed `>> 16` sign-extension) poisoned
    // the table with 0xFFFFxxxx words. With Seed fixed, the raw signed modulo
    // stays in range; the Int(low, high) HX_NATIVE guard remains as defense in
    // depth against any other table corruption.
    const int low = 0, high = 4;
    for (unsigned int seed = 0; seed < 16; seed++) {
        Rand r(seed);
        for (int i = 0; i < 100000; i++) {
            int raw = r.Int();
            ASSERT_GE(raw, 0) << "negative draw at seed " << seed << " iter " << i
                              << " — Seed() table-poisoning has regressed";
            int idx = SignedModInt(raw, low, high);
            ASSERT_GE(idx, low);
            ASSERT_LT(idx, high);
        }
    }
}

TEST(RandIntNativeFix, RandIntStaysInRangeOnHost) {
    // The fix: Rand::Int(low, high) is guaranteed to return a value in [low, high)
    // on the host (HX_NATIVE unsigned-modulo fold in Rand.h). Hammer it across many
    // draws and several spans; not a single result may escape the range.
    Rand r(0);
    const int spans[][2] = {{0, 2}, {0, 4}, {3, 9}, {-5, 5}, {0, 1}, {100, 137}};
    for (auto &s : spans) {
        for (int i = 0; i < 200000; i++) {
            int v = r.Int(s[0], s[1]);
            ASSERT_GE(v, s[0]) << "Rand::Int(" << s[0] << "," << s[1] << ") < low";
            ASSERT_LT(v, s[1]) << "Rand::Int(" << s[0] << "," << s[1] << ") >= high";
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Fisher-Yates shuffle index safety — RandomizeCategory's exact loop.
// ---------------------------------------------------------------------------
//
// This is the EXACT shuffle CameraManager::RandomizeCategory runs, now that
// Rand::Int(i, size) is root-safe: every swap index must stay in [i, size) and the
// element multiset must be preserved (no corruption / no loss). This is the
// scenario that crashed the boot before the Rand::Int fix.

TEST(RandomizeShuffleSafety, RandomizeCategoryShuffleStaysInBounds) {
    Rand &r = Rand::sRand;
    r.Seed(12345);
    for (int trial = 0; trial < 2000; trial++) {
        int n = 1 + (int)((unsigned)r.Int() % 16u); // list size 1..16
        std::vector<int> v(n);
        for (int i = 0; i < n; i++)
            v[i] = i;
        std::vector<int> original = v;

        // EXACT body of CameraManager::RandomizeCategory's shuffle loop.
        for (int i = 0; i < (int)v.size(); i++) {
            int randIdx = r.Int(i, (int)v.size());
            ASSERT_GE(randIdx, i) << "shuffle index below i (n=" << n << ")";
            ASSERT_LT(randIdx, (int)v.size())
                << "shuffle index past end (n=" << n << ")";
            std::swap(v[i], v[randIdx]);
        }

        // A permutation: same multiset, nothing lost or duplicated.
        std::vector<int> sortedV = v;
        std::sort(sortedV.begin(), sortedV.end());
        EXPECT_EQ(sortedV, original)
            << "shuffle dropped/duplicated an element (corruption) at n=" << n;
    }
}

TEST(RandomizeShuffleSafety, FlowPickOneRandomIndexStaysInBounds) {
    // The same root fix also protects FlowPickOne::Activate, which does
    // mChildNodes[RandomInt(0, numChildren)] — an out-of-range index there picked a
    // garbage FlowNode and crashed the next ObjRef ring link during boot. Pin that
    // RandomInt(0, n)-style picks index validly for every child count.
    Rand &r = Rand::sRand;
    r.Seed(7);
    for (int numChildren = 1; numChildren <= 20; numChildren++) {
        for (int k = 0; k < 20000; k++) {
            int idx = r.Int(0, numChildren); // RandomInt(0, numChildren) under the hood
            ASSERT_GE(idx, 0) << "child index < 0 (numChildren=" << numChildren << ")";
            ASSERT_LT(idx, numChildren)
                << "child index past end (numChildren=" << numChildren << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// 3. CharBones::ScaleDown one-past-end addressing — the fix's invariant.
// ---------------------------------------------------------------------------
//
// ScaleDown (and Blend/ScaleAdd) form iterator/bound pointers at index ==
// mBones.size() (mCounts[TYPE_END]). The fix replaces &mBones[index] with
// mBones.data() + index. The safety property: for a non-empty contiguous vector,
// data() + index is the SAME address as &v[index] would compute for every index
// in [0, size], including the one-past-the-end index — but data() + index forms
// it WITHOUT the hardened operator[] bounds assert that aborts the process.
//
// (The end-to-end no-crash behavior of ScaleDown itself is covered by the
// asset-backed CharClip::ScaleDown tests in test_asset_loading.cpp, which pose
// real character bones under RelWithDebInfo.)

TEST(ScaleDownEndPointer, DataPlusIndexEqualsAddressOfForEveryIndexIncludingEnd) {
    for (int n = 1; n <= 32; n++) {
        std::vector<CharBones::Bone> v;
        v.reserve(n);
        for (int i = 0; i < n; i++)
            v.push_back(CharBones::Bone(Symbol(), (float)i));

        // For every in-range index, data()+i must equal &v[i].
        for (int i = 0; i < n; i++) {
            ASSERT_EQ(v.data() + i, &v[i])
                << "data()+i != &v[i] at i=" << i << " n=" << n;
        }
        // The one-past-the-end pointer the ScaleDown loops form as a bound:
        // data()+n is well-defined. Forming it must not abort. (Doing &v[n] here
        // under hardened libstdc++ WOULD abort — that is exactly the crash the fix
        // removes; we therefore never form &v[n] or deref end().)
        const CharBones::Bone *endPtr = v.data() + n;
        // The bound equals data() advanced by the element count and is strictly
        // past the last element.
        ASSERT_EQ((endPtr - v.data()), (ptrdiff_t)n) << "n=" << n;
        ASSERT_GT(endPtr, v.data() + (n - 1)) << "n=" << n;
    }
}
