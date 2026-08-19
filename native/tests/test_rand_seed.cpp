// Rand::Seed MT-state high-word regression tests (Wave-4 Lane B, unicorn
// flip-list object_memory bug `?Seed@Rand@@QAAXH@Z`).
//
// The Xbox 360 target lowers `mRandTable[i] = (s & 0x7FFF0000) | (j >> 16)` with
// a LOGICAL right shift (PPC `srwi`): the shifted draw contributes only its low
// 16 bits, so every table word keeps bit 31 clear (orig words are
// `0x5665xxxx`-class, never `0xFFFFxxxx`). The decompiled source declares `j` as
// a signed `int`, so `j >> 16` is an ARITHMETIC shift (`srawi`): when bit 31 of
// the draw is set it sign-extends to `0xFFFFxxxx` and the OR poisons the high
// word of the Mersenne-Twister-style state table. Unicorn differential execution
// caught this as 20 memory diffs (decomp `0xFFFFxxxx` vs orig `0x5665xxxx`) under
// a stale "equivalent" cert.
//
// The fix (guarded by HX_NATIVE so the PPC byte-match is untouched) masks the
// shifted value to 16 bits, reproducing the logical shift exactly. These tests
// pin (1) the absence of the `0xFFFF` high-word poison directly in the seeded
// table, and (2) the canonical Int() draw sequence for several seeds. They FAIL
// on the pre-fix native code (which produced e.g. table[0]=0xFFFFD3DC for
// seed 12345, and Int() draws diverging in their high 16 bits).
//
// Reference sequences were computed from the corrected algorithm with -fwrapv
// (matching the wrap-on-overflow signed multiply the target also performs).

#include <gtest/gtest.h>

#include "math/Rand.h"

#include <cstdint>

namespace {

// Re-seed a Rand and pull `n` raw Int() draws into a vector.
std::vector<uint32_t> Draws(Rand &r, int seed, int n) {
    r.Seed(seed);
    std::vector<uint32_t> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(static_cast<uint32_t>(r.Int()));
    return out;
}

}  // namespace

// GUARD for the `srawi`-vs-`srwi` sign-extension defect in Rand::Seed.
//
// HISTORY — rewritten 2026-08-19 (toolchain-audit follow-up). The previous
// version of this test was VACUOUS: with `(unsigned int)` deleted from
// Rand::Seed — the exact defect it is named after — it still PASSED. Two
// independent reasons, both worth recording because each defeats an "obvious"
// replacement probe:
//
//  1. It probed the DRAW STREAM. `Int()` returns `table[i1] ^ table[i2]`, and
//     XOR destroys any high-half signature: two poisoned words cancel, and one
//     poisoned against one clean word yields the complement of the clean high
//     half. Across 5 seeds x 16 draws the 0xFFFF pattern never appeared.
//
//  2. The 0xFFFFxxxx signature the old comment described does not occur in the
//     current source at all. Rand::Seed deliberately combines with `+`, not
//     `|` (a disjoint `|` folds to `rlwimi` and costs ~10pp of PPC match). With
//     `+`, sign extension of `j >> 16` adds 0xFFFF0000, which simply *carries*:
//     the word comes out as (correct - 0x10000), e.g. seed 12345 table[0] is
//     0x2704D3DC correct and 0x2703D3DC buggy. Bit 31 stays clear, the high
//     half stays <= 0x7FFF, and the low half is byte-identical. So EVERY
//     structural invariant of the form "no 0xFFFF high word" / "bit 31 clear"
//     is ALSO vacuous against the real defect. Verified by rebuilding with the
//     sabotage in place, not assumed.
//
// What actually discriminates is comparing the production table against an
// independent reference implementation of the target's semantics. The test
// carries its OWN NEGATIVE CONTROL: it computes both the logical-shift
// reference (what PPC `srwi` does, the correct semantics) and the
// arithmetic-shift twin (what `srawi` does, the defect), asserts the two
// DIFFER on the seed set — proving the probe has discriminating power and the
// seeds reach the defect — and only then asserts that production matches the
// logical one. If someone weakens the seed set until the defect is no longer
// reachable, the control assertion fails rather than the test silently
// becoming a no-op.
namespace {

struct SeedTables {
    std::vector<uint32_t> logical;  // PPC `srwi` — correct
    std::vector<uint32_t> arith;    // PPC `srawi` — the defect
};

// Independent reference for `Rand::Seed`, transcribed from the target
// instruction sequence rather than from Rand.cpp. All arithmetic is done in
// uint32_t so the multiply wraps without signed-overflow UB, matching the
// target's `mullw`.
SeedTables ReferenceSeed(int seed, int n) {
    SeedTables t;
    t.logical.reserve(n);
    t.arith.reserve(n);
    uint32_t s = static_cast<uint32_t>(seed);
    for (int i = 0; i < n; ++i) {
        uint32_t j = s * 0x41C64E6Du + 0x3039u;
        s = j * 0x41C64E6Du + 0x3039u;
        uint32_t hi = s & 0x7FFF0000u;
        uint32_t shifted_logical = j >> 16;  // srwi
        // srawi, spelled portably (do not rely on >> of a negative int32_t).
        uint32_t shifted_arith =
            (j & 0x80000000u) ? (shifted_logical | 0xFFFF0000u) : shifted_logical;
        t.logical.push_back(shifted_logical + hi);
        t.arith.push_back(shifted_arith + hi);
    }
    return t;
}

}  // namespace

TEST(RandSeed, NoSignExtensionPoison) {
    Rand r(0);
    const int n = Rand::TableSizeForTest();
    for (int seed : {0x29A, 1, 12345, -1, 7777, 0, 0x7FFFFFFF}) {
        SeedTables ref = ReferenceSeed(seed, n);

        // --- in-test negative control -------------------------------------
        // The two reference variants must disagree on this seed, or the
        // assertion below cannot distinguish a fixed build from a broken one.
        int differing = 0;
        for (int i = 0; i < n; ++i)
            if (ref.logical[i] != ref.arith[i])
                ++differing;
        ASSERT_GE(differing, n / 4)
            << "seed " << seed << ": only " << differing << " of " << n
            << " reference entries differ between srwi and srawi, so this seed "
               "does not exercise the sign-extension path and the check below "
               "would be vacuous for it. Fix the seed list, not the engine.";

        // --- the actual guard ---------------------------------------------
        r.Seed(seed);
        std::vector<uint32_t> actual;
        actual.reserve(n);
        for (int i = 0; i < n; ++i)
            actual.push_back(r.TableWordForTest(i));

        for (int i = 0; i < n; ++i) {
            ASSERT_EQ(actual[i], ref.logical[i])
                << "seed " << seed << " table[" << i << "]: got 0x" << std::hex
                << actual[i] << ", srwi reference 0x" << ref.logical[i]
                << ", srawi (bug) would give 0x" << ref.arith[i]
                << (actual[i] == ref.arith[i]
                        ? "  <-- MATCHES THE SIGN-EXTENSION BUG"
                        : "");
        }
    }
}

// Golden table words for seed 12345 — the entry the unicorn differential named.
// Independent of the reference implementation above (these are literals), so a
// mistake shared between Rand::Seed and ReferenceSeed still fails here.
TEST(RandSeed, Seed12345TableWords) {
    Rand r(0);
    r.Seed(12345);
    std::vector<uint32_t> first8;
    for (int i = 0; i < 8; ++i)
        first8.push_back(r.TableWordForTest(i));
    // buggy (srawi) first word is 0x2703D3DC — one less in the high half.
    std::vector<uint32_t> expected = {0x2704D3DCu, 0x0DAAD665u, 0x3EADC21Fu,
                                      0x2F5ACD1Du, 0x2FE520DAu, 0x161B69ACu,
                                      0x525F261Eu, 0x7E706513u};
    EXPECT_EQ(first8, expected);
}

// Canonical first-8 Int() sequence for seed 0x29A (the default sRand seed).
// FIXED  : 0x7DBB9925 0x771BC747 0x50141BB2 0x3808C3E5 ...
// (buggy : 0xD11E9925 0x9BAFC747 0x50141BB2 0xEBABC3E5 ... — diverges at draw 0)
TEST(RandSeed, DefaultSeedSequence) {
    Rand r(0);
    std::vector<uint32_t> expected = {0x7DBB9925u, 0x771BC747u, 0x50141BB2u,
                                      0x3808C3E5u, 0x472373FAu, 0x00F5BF16u,
                                      0x7FF07333u, 0x5A7FDB5Du};
    EXPECT_EQ(Draws(r, 0x29A, 8), expected);
}

// Seed 12345 — the table[0] poison case (fixed 0x2704D3DC vs buggy 0xFFFFD3DC).
TEST(RandSeed, Seed12345Sequence) {
    Rand r(0);
    std::vector<uint32_t> expected = {0x531BDFDEu, 0x2E91BB60u, 0x5B345E33u,
                                      0x7FA6F907u, 0x57F0B131u, 0x1BD47972u,
                                      0x75E5FB76u, 0x430C08F3u};
    EXPECT_EQ(Draws(r, 12345, 8), expected);
}

// Seed 1 and seed -1 (the bit-31-set seed) sequences.
TEST(RandSeed, Seed1Sequence) {
    Rand r(0);
    std::vector<uint32_t> expected = {0x00CEE059u, 0x74C29AECu, 0x55A139D5u,
                                      0x5FBDF4E9u, 0x4D76B024u, 0x6C10470Bu,
                                      0x0310E43Bu, 0x0F221B47u};
    EXPECT_EQ(Draws(r, 1, 8), expected);
}

TEST(RandSeed, SeedMinusOneSequence) {
    Rand r(0);
    std::vector<uint32_t> expected = {0x106C72EBu, 0x413C0C57u, 0x19BFA7D0u,
                                      0x59D13D5Fu, 0x50EB0DE7u, 0x6295F778u,
                                      0x13E45CECu, 0x1585CE13u};
    EXPECT_EQ(Draws(r, -1, 8), expected);
}

// Re-seeding resets the generator deterministically (the table is fully rebuilt,
// indices reset to 0/0x67) — pin that two identical seeds give identical draws.
TEST(RandSeed, Deterministic) {
    Rand r(0);
    EXPECT_EQ(Draws(r, 424242, 12), Draws(r, 424242, 12));
    // Self-consistency alone is unfalsifiable — it passes on any wrong-but-stable
    // algorithm, and the 2026-08-19 audit confirmed it passes with the srawi bug
    // reintroduced. Pin the value too, so "deterministic" means "deterministically
    // correct". Goldens from the srwi reference (see ReferenceSeed above).
    std::vector<uint32_t> expected = {0x40912B19u, 0x217419D6u, 0x49F26827u,
                                      0x0F9D3B5Fu, 0x070A6FC6u, 0x06F712EAu,
                                      0x4E027E16u, 0x18A15B1Du, 0x049EABFDu,
                                      0x0DEFDA7Au, 0x23935C96u, 0x29D1C310u};
    EXPECT_EQ(Draws(r, 424242, 12), expected);
}
