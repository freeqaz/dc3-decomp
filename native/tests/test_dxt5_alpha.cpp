// DXT5 / BC3 alpha-block decode regression tests (Wave-3 Lane C, roadmap N.4).
//
// `DecodeDxt5Alpha` (src/system/rndobj/Bitmap.cpp) was a corrupted Ghidra
// transcription with three behavioral bugs vs the original Xbox 360 binary
// (confirmed against the shared-engine RB3 reference and the BC3 spec):
//
//   1. code 0 / code 1 mapped to the WRONG endpoint: code 0 must return
//      alpha0 and code 1 alpha1 (which physical byte each is: see below).
//   2. The 6-vs-8 value interpolation selector was inverted: `!(a1 > a0)`
//      (== a1 <= a0 == a0 >= a1) instead of the correct `a0 <= a1`.
//   3. Spurious code==6 -> 0 / code==7 -> 0xFF special cases were added to the
//      8-value (a0 > a1) branch, where codes 6 and 7 are normal interpolants.
//
// These tests pin the ORIGINAL (correct) decode behavior so the bug cannot
// silently regress. The Xbox 360 stores the whole 8-byte block with a 16-bit
// word byteswap (byte index XOR 1), so the reference encoder applies the same
// swizzle when laying the block out -- to the index bytes AND to the endpoint
// word. The endpoint half of that was wrong here until 2026-09-14: this file
// swizzled bytes 2..7 and left a0/a1 at physical bytes 0/1, which pinned a
// decoder that read a0 from uc[0]. The target listing reads a0 from uc[1]:
// 82671ED8 `lbz r28, 0x1(r3)` is what code 0 stores (82671F84 `stb r28`),
// 82671EDC `lbz r27, 0x0(r3)` is what code 1 stores (82671F94 `stb r27`), and
// 82671FA4 `cmplw cr6, r9(=uc[1]), r10(=uc[0])` / `bgt` selects the 8-value
// mode when uc[1] > uc[0], i.e. a0 > a1. Wave 7 (lane w7-am, DecodeDxt5Alpha
// 81.9 -> 94.6) corrected the decoder; this file now models the block the way
// the image does.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// File-local in Bitmap.cpp but external linkage; declare it here.
void DecodeDxt5Alpha(unsigned char *uc, int i, int j, unsigned char &alpha);

namespace {

// Build an 8-byte DXT5 alpha block: a0,a1 endpoints + 16 3-bit indices.
// Index n (0..15) occupies bits [3n, 3n+2] of the 48-bit index field, which
// lives in bytes 2..7 of the block. The Xbox decoder reads each index byte
// with its even/odd partner swapped (XOR 1), so to make DecodeDxt5Alpha see
// index `idx[n]` at logical position n we must write into the byte-swapped
// position. We model the 6 index bytes as a 48-bit little-endian value, then
// emit those 6 bytes in byteswapped (XOR-1) order into block[2..7].
std::vector<unsigned char> MakeBlock(uint8_t a0, uint8_t a1,
                                     const uint8_t idx[16]) {
    uint64_t packed = 0;
    for (int n = 0; n < 16; ++n) {
        packed |= (uint64_t)(idx[n] & 7) << (3 * n);
    }
    unsigned char idxBytes[6];
    for (int b = 0; b < 6; ++b) {
        idxBytes[b] = (unsigned char)((packed >> (8 * b)) & 0xFF);
    }
    std::vector<unsigned char> block(8, 0);
    // The endpoint word gets the same 16-bit byteswap as the index bytes:
    // logical a0 (byte 0) lives at physical byte 1 and a1 at physical byte 0.
    block[0 ^ 1] = a0;
    block[1 ^ 1] = a1;
    // Apply the Xbox byteswap: the value the decoder reads at logical byte b
    // is what we store at physical byte (b ^ 1). The decoder indexes block+2,
    // so write idxBytes[b] to block[2 + (b ^ 1)].
    for (int b = 0; b < 6; ++b) {
        block[2 + (b ^ 1)] = idxBytes[b];
    }
    return block;
}

// Decode one of the 16 texels (i = x-in-block 0..3, j = y-in-block 0..3).
uint8_t Decode(std::vector<unsigned char> &block, int i, int j) {
    unsigned char out = 0xAA;  // poison
    DecodeDxt5Alpha(block.data(), i, j, out);
    return out;
}

// Texel (i,j) maps to linear index n = i + (j << 2).
int Lin(int i, int j) { return i + (j << 2); }

}  // namespace

// Bug #1: code 0 returns a0 (uc[0]); code 1 returns a1 (uc[1]).
TEST(Dxt5Alpha, EndpointCodesMapToCorrectEndpoint) {
    uint8_t idx[16] = {0};
    idx[Lin(0, 0)] = 0;  // texel (0,0) -> code 0 -> a0
    idx[Lin(1, 0)] = 1;  // texel (1,0) -> code 1 -> a1
    auto block = MakeBlock(/*a0=*/200, /*a1=*/50, idx);

    EXPECT_EQ(Decode(block, 0, 0), 200) << "code 0 must return alpha0 (uc[1] on Xbox)";
    EXPECT_EQ(Decode(block, 1, 0), 50) << "code 1 must return alpha1 (uc[0] on Xbox)";
}

// Bug #2: a0 > a1 selects the 8-value interpolation (no transparent/opaque
// endpoints from codes 6/7). With a0=200,a1=50, codes 2..7 interpolate.
TEST(Dxt5Alpha, EightValueModeInterpolatesAllCodes) {
    uint8_t idx[16] = {0};
    for (int c = 2; c <= 7; ++c) idx[Lin(c % 4, c / 4)] = (uint8_t)c;
    auto block = MakeBlock(/*a0=*/200, /*a1=*/50, idx);  // a0 > a1 -> 8-value

    // Reference 8-value formula: ((8-code)*a0 + (code-1)*a1 + 3) / 7.
    for (int c = 2; c <= 7; ++c) {
        unsigned exp = ((8u - c) * 200u + (c - 1u) * 50u + 3u) / 7u;
        EXPECT_EQ(Decode(block, c % 4, c / 4), (uint8_t)exp)
            << "8-value mode, code " << c
            << " must interpolate, not snap to 0/0xFF";
    }
    // Specifically: code 6 and 7 must NOT be 0 / 0xFF here.
    EXPECT_NE(Decode(block, 6 % 4, 6 / 4), 0)
        << "code 6 in 8-value mode must not be transparent";
    EXPECT_NE(Decode(block, 7 % 4, 7 / 4), 0xFF)
        << "code 7 in 8-value mode must not be fully opaque";
}

// Bug #2/#3: a0 <= a1 selects the 6-value mode where code 6 -> 0, code 7 ->
// 0xFF, and codes 2..5 use the 6-value interpolation formula.
TEST(Dxt5Alpha, SixValueModeUsesTransparentOpaqueEndpoints) {
    uint8_t idx[16] = {0};
    for (int c = 2; c <= 7; ++c) idx[Lin(c % 4, c / 4)] = (uint8_t)c;
    auto block = MakeBlock(/*a0=*/50, /*a1=*/200, idx);  // a0 < a1 -> 6-value

    for (int c = 2; c <= 5; ++c) {
        unsigned exp = ((6u - c) * 50u + (c - 1u) * 200u + 2u) / 5u;
        EXPECT_EQ(Decode(block, c % 4, c / 4), (uint8_t)exp)
            << "6-value mode, code " << c << " interpolation";
    }
    EXPECT_EQ(Decode(block, 6 % 4, 6 / 4), 0)
        << "6-value mode code 6 is transparent";
    EXPECT_EQ(Decode(block, 7 % 4, 7 / 4), 0xFF)
        << "6-value mode code 7 is fully opaque";
}

// Endpoint codes are mode-independent: code 0/1 always return a0/a1 even in
// 6-value mode.
TEST(Dxt5Alpha, EndpointCodesAreModeIndependent) {
    uint8_t idx[16] = {0};
    idx[Lin(0, 0)] = 0;
    idx[Lin(1, 0)] = 1;
    auto block = MakeBlock(/*a0=*/30, /*a1=*/220, idx);  // 6-value mode

    EXPECT_EQ(Decode(block, 0, 0), 30) << "code 0 -> a0 in 6-value mode";
    EXPECT_EQ(Decode(block, 1, 0), 220) << "code 1 -> a1 in 6-value mode";
}
