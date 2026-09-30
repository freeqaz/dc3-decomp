#include "math\SHA1.h"
#include "utl/BinStream.h"
#include "utl\Licenses.h"
#include <cstdio>
#include <cstring>

static Licenses sLicense("system/src/math/SHA1.h", Licenses::kRequirementNotification);

// shoutouts to clibs' implementation of sha1: https://github.com/clibs/sha1

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#ifdef HX_NATIVE
// The Xbox 360 target is big-endian: a 32-bit word memcpy'd from the message
// buffer is read in big-endian order, which is the byte order SHA1 expects. The
// little-endian native/web host reads those bytes reversed, so byteswap the raw
// word on first use (blk0) to recover the big-endian view. blk() then operates
// on already-corrected words. Without this every CSHA1 digest is wrong on host.
static inline unsigned int Sha1Bswap32(unsigned int v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}
#define blk0(i) (m_block->l[i] = Sha1Bswap32(m_block->l[i]))
#else
#define blk0(i) m_block->l[i]
#endif
#define blk(i)                                                                           \
    (m_block->l[i & 15] =                                                                \
         rol(m_block->l[i & 15] ^ m_block->l[(i + 2) & 15]                               \
                 ^ m_block->l[(i + 8) & 15] ^ m_block->l[(i + 13) & 15],                 \
             1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v, w, x, y, z, i)                                                             \
    z += ((w & (x ^ y)) ^ y) + blk0(i) + 0x5A827999 + rol(v, 5);                         \
    w = rol(w, 30);
#define R1(v, w, x, y, z, i)                                                             \
    z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + rol(v, 5);                          \
    w = rol(w, 30);
#define R2(v, w, x, y, z, i)                                                             \
    z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + rol(v, 5);                                  \
    w = rol(w, 30);
#define R3(v, w, x, y, z, i)                                                             \
    z += (((w | x) & y) | (w & x)) + blk(i) + 0x8F1BBCDC + rol(v, 5);                    \
    w = rol(w, 30);
#define R4(v, w, x, y, z, i)                                                             \
    z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + rol(v, 5);                                  \
    w = rol(w, 30);

// The PPC arm of this function is behaviourally exact and does not need
// another look. Verified 2026-08-19 by driving both the decompiled .text and
// the original .obj's .text through the unicorn harness with a real fixture
// (m_block and pState pointed inside the compared object region, seeded with
// the padded single block for "abc"): both sides produce
// a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d -- the published SHA-1("abc")
// digest -- and the whole 64KB object region comes out byte-identical.
// The unicorn row for ?Transform@CSHA1@@AAAXPAIPBE@Z is nevertheless
// DIVERGENT/return_value: the comparator checks r3 unconditionally, and this
// is a void function, so the r3 residue is dead by ABI.
//
// The residual ~55.7% objdiff score is instruction scheduling and register
// allocation inside the 80-round unrolled block (both sides prefetch
// m_block->l[] into a long chain of registers and interleave the prefetch
// differently, offset by one register). It is unrelated to the HX_NATIVE
// guards below, which are compile-time: the PPC build sees only the #else
// arms and is byte-identical to what it was before those guards landed
// (55.7% was reached in 979aabcc0, the guards landed later in 97b649d25).
// The "Source accesses 'm_reserved1'/'m_buffer' but target accesses ..."
// notes objdiff prints for this function are false positives -- those loads
// are indexed off m_block, not off `this`.
//
// MEASURED NEGATIVES (2026-09-13, lane w3-m), all read off
// build/373307D9/asm/system/math/SHA1.s and all reverted. Baseline 61.99.
//
// 1. ROUND ACCUMULATION ORDER IS NOT SOURCE-CONTROLLABLE. The target's round 0
//    accumulates `(rol(v,5) + f) + e + w[0] + K` -- it adds `e` at index 22,
//    BEFORE `m_block->l` is even addressed (the `lwz r22, 0xc0(r24)` is index
//    29) -- which is the FIPS-180 grouping, not Reid's `z += f + w + K + rol`.
//    Rewriting all five macros as `z = rol(v,5) + f + z + blk(i) + K` (exact
//    for unsigned 32-bit) emits a BYTE-IDENTICAL round 0: still `add r8,r7,r8`
//    (+w) then `add r8,r8,r29` (+e). MSVC canonicalises the add chain before
//    scheduling, so the 27 `diff_op` rows attributed to the `rol(v,5)` /
//    `w = rol(w,30)` pair are a scheduling artifact, not a source shape. Net
//    61.99 -> 61.1 from unrelated downstream drift.
// 2. PROLOGUE + EPILOGUE ORDER: the target loads pState[0..4] into a,b,c,d,e in
//    index order (r28,r27,r31,r30,r29) and its epilogue loads 0x0,0x4,0x8,0xc,
//    0x10 in order too, so the original source really is `a = pState[0]; ...`
//    and `pState[0] += a; ...` forward, where ours is `c,b,a,d,e` and a reversed
//    epilogue. Making BOTH forward does exactly what it should locally --
//    instructions 0-19 become fully equal and the r27/r28 naming of a and b
//    matches -- and still costs 0.6pp overall (61.99 -> 61.4), because the
//    80-round body reschedules around it. Reordering the five declarations to
//    a,b,c,d,e on top of that is inert (61.4, identical row set). This is worth
//    revisiting ONLY together with a fix for the body's r9/r11 scratch
//    allocation (88 of the 104 swap pairs), which is what actually costs the
//    ~230 extra instructions.
//
// RE-CONFIRMED 2026-09-14 (lane w7-aj) at 62.0 canonical / 55.7 raw, 1698
// instructions: 1721 instructions across 99 REGISTER_SWAP pairs, 47 offset
// swaps, 17 commutative rows. Both leads above still read exactly as recorded,
// so neither was re-derived. The class that remains is the body's scratch
// allocation, which is register permutation and is not source-reachable.
//
// MEASURED NEGATIVES (2026-09-14, lane w7-bm), baseline 62.0 canonical:
// 3. blk() XOR OPERAND ORDER IS INERT. Parsing every blk round of both sides
//    (64 rounds, 8253A4C0..8253B680) shows the target's XOR tree is ALWAYS
//    `(((hi ^ next) ^ next) ^ lo)` by folded index -- round 79 is
//    (l[15]^l[12])^l[7]^l[1] -- while ours is scheduling-dependent whenever an
//    index wraps (round 24 and round 40 order the same four words differently).
//    Respelling the macro in Dominik Reichl's CSHA1 SHABLK order
//    (l[i+13]^l[i+8]^l[i+2]^l[i]) emits the IDENTICAL tree in all 64 rounds
//    (MSVC canonicalises the chain), 62.0 -> 61.0 from downstream drift.
// 4. WORD TYPE IS INERT: reading/writing the block words as `unsigned int&`
//    and declaring a..e `unsigned int` (Reichl's UINT_32) gives 62.0 with the
//    identical 1698-row set (910/27/47/234/291).
// 5. The 57 extra instructions the target carries are 57 `mr` copies of the
//    block pointer, one per blk round (e.g. 8253A4CC `lwz r31,0xc0(r24)` /
//    `mr r30,r31`, then the LAST operand load clobbers r31 and the store goes
//    through r30). That is a live-range split under one more callee-saved
//    register (__savegprlr_17 vs our _18), i.e. allocation, not a source
//    shape: there is exactly one m_block load per round on both sides (66).
//
// FLOOR CERTIFICATE (w8-o 2026-09-30), 61.986 canonical / 55.7 raw.  The
// residual is register-copy insertion and nothing else, and this is now
// measured rather than inferred.  The OPCODE HISTOGRAMS OF THE TWO SIDES ARE
// IDENTICAL IN EVERY ENTRY EXCEPT `mr`:
//
//   lwz 348  xor 312  add 285  rotlwi 144  rotrwi 80  stw 69  and 60
//   subf 40  or 40  ori 4  lis 4  bl 2  stwu 1  mflr 1  li 1  b 1  addi 1
//
//   mr:  target 71   base 14        <- the ONLY difference, and 71-14 = 57
//                                      is exactly the instruction-count gap
//                                      (target 1464, base 1407).
//
// A 1,464-instruction function does not agree on all seventeen other opcode
// counts by accident.  The arithmetic, the round structure, the blk() XOR
// tree, the word type and the 66 m_block reloads are all already correct; the
// image simply splits the m_block pointer's live range once per blk round
// (e.g. 0x8253A4CC `lwz r31, 0xc0(r24)` / 0x8253A520 `mr r29, r31`, the last
// operand load then clobbering r31 while the store goes through r29) under one
// more callee-saved register than MSVC gives us.  There is no source edit left
// to make: what remains is the allocator's choice.
//
// Two further negatives from that lane, so nobody re-derives them:
//
// 6. STATEMENT-SPLITTING THE ROUND MACRO IS WORSE, and it refutes the "e is
//    added before w because the source says so" reading directly.  The image
//    accumulates ((rol(v,5) + f) + z) + blk(i) + K -- `add r7, r9, r29` at
//    0x8253A058 adds e BEFORE the block word is loaded at 0x8253A074 -- where
//    we add w then e.  Splitting each macro into `z += rol(v,5) + f;` then
//    `z += blk(i) + K;` does not produce the image's order; it LOWERS register
//    pressure and moves the prologue the WRONG WAY, to __savegprlr_19 (the
//    image is _17, we are _18).  62.0 -> 60.4, 1708 rows.
// 7. THE COMDAT / `inline`-ON-THE-CALLEE LEVER IS OUT OF REACH HERE BY
//    CONSTRUCTION, and the test is same-TU-ness, not the map class.  That
//    lever (ham_xbox_r.map's COMDAT column: 79,320 bare `f` vs 31,754 `f i`)
//    has closed rows in other units empirically, but it needs a SAME-TU callee
//    for a clobber set to propagate in the first place.  This function calls
//    exactly one thing -- `memcpy`, which the map puts in LIBCMT:memcpyp.obj at
//    0x8299FBB0, bare `f`, a different translation unit -- so it has NO same-TU
//    callee and there is nothing for the lever to act on.  Both sides emit the
//    same `bl memcpy`.  (Do NOT restate this as "a COMDAT callee is link-time
//    replaceable so the caller must spill": that mechanism was retracted
//    2026-09-30.  MSVC/Xenon puts every function it compiles in its own COMDAT,
//    so adding `inline` does not change the map class -- the carrier is the
//    COMDAT SELECTION TYPE in the section symbol's aux record, and our objects
//    emit NODUPLICATES for both classes.  The lever is empirical; the
//    same-TU precondition is what is structural.)
void CSHA1::Transform(unsigned int *pState, const unsigned char *pBuffer) {
#ifdef HX_NATIVE
    // `unsigned long` is 64-bit on the LP64 host, so rol()/blk() would not wrap
    // these round-state words at 32 bits and the digest would be wrong. The Xbox
    // 360 target's `unsigned long` is 32-bit, so pin a 32-bit word type here. The
    // PPC source keeps the original `unsigned long` declarations for matching.
    unsigned int e;
    unsigned int d;
    unsigned int c;
    unsigned int b;
    unsigned int a;
#else
    unsigned long e;
    unsigned long d;
    unsigned long c;
    unsigned long b;
    unsigned long a;
#endif
    c = pState[2];

    b = pState[1];
    a = pState[0];
    d = pState[3];
    e = pState[4];
    memcpy(m_block->c, pBuffer, 0x40);
    R0(a, b, c, d, e, 0);
    R0(e, a, b, c, d, 1);
    R0(d, e, a, b, c, 2);
    R0(c, d, e, a, b, 3);
    R0(b, c, d, e, a, 4);
    R0(a, b, c, d, e, 5);
    R0(e, a, b, c, d, 6);
    R0(d, e, a, b, c, 7);
    R0(c, d, e, a, b, 8);
    R0(b, c, d, e, a, 9);
    R0(a, b, c, d, e, 10);
    R0(e, a, b, c, d, 11);
    R0(d, e, a, b, c, 12);
    R0(c, d, e, a, b, 13);
    R0(b, c, d, e, a, 14);
    R0(a, b, c, d, e, 15);
    R1(e, a, b, c, d, 16);
    R1(d, e, a, b, c, 17);
    R1(c, d, e, a, b, 18);
    R1(b, c, d, e, a, 19);
    R2(a, b, c, d, e, 20);
    R2(e, a, b, c, d, 21);
    R2(d, e, a, b, c, 22);
    R2(c, d, e, a, b, 23);
    R2(b, c, d, e, a, 24);
    R2(a, b, c, d, e, 25);
    R2(e, a, b, c, d, 26);
    R2(d, e, a, b, c, 27);
    R2(c, d, e, a, b, 28);
    R2(b, c, d, e, a, 29);
    R2(a, b, c, d, e, 30);
    R2(e, a, b, c, d, 31);
    R2(d, e, a, b, c, 32);
    R2(c, d, e, a, b, 33);
    R2(b, c, d, e, a, 34);
    R2(a, b, c, d, e, 35);
    R2(e, a, b, c, d, 36);
    R2(d, e, a, b, c, 37);
    R2(c, d, e, a, b, 38);
    R2(b, c, d, e, a, 39);
    R3(a, b, c, d, e, 40);
    R3(e, a, b, c, d, 41);
    R3(d, e, a, b, c, 42);
    R3(c, d, e, a, b, 43);
    R3(b, c, d, e, a, 44);
    R3(a, b, c, d, e, 45);
    R3(e, a, b, c, d, 46);
    R3(d, e, a, b, c, 47);
    R3(c, d, e, a, b, 48);
    R3(b, c, d, e, a, 49);
    R3(a, b, c, d, e, 50);
    R3(e, a, b, c, d, 51);
    R3(d, e, a, b, c, 52);
    R3(c, d, e, a, b, 53);
    R3(b, c, d, e, a, 54);
    R3(a, b, c, d, e, 55);
    R3(e, a, b, c, d, 56);
    R3(d, e, a, b, c, 57);
    R3(c, d, e, a, b, 58);
    R3(b, c, d, e, a, 59);
    R4(a, b, c, d, e, 60);
    R4(e, a, b, c, d, 61);
    R4(d, e, a, b, c, 62);
    R4(c, d, e, a, b, 63);
    R4(b, c, d, e, a, 64);
    R4(a, b, c, d, e, 65);
    R4(e, a, b, c, d, 66);
    R4(d, e, a, b, c, 67);
    R4(c, d, e, a, b, 68);
    R4(b, c, d, e, a, 69);
    R4(a, b, c, d, e, 70);
    R4(e, a, b, c, d, 71);
    R4(d, e, a, b, c, 72);
    R4(c, d, e, a, b, 73);
    R4(b, c, d, e, a, 74);
    R4(a, b, c, d, e, 75);
    R4(e, a, b, c, d, 76);
    R4(d, e, a, b, c, 77);
    R4(c, d, e, a, b, 78);
    R4(b, c, d, e, a, 79);

    pState[4] += e;
    pState[3] += d;
    pState[2] += c;
    pState[1] += b;
    pState[0] += a;
}

void CSHA1::Update(const unsigned char *data, unsigned int len) {
    unsigned int i, j;

    j = (m_count[0] >> 3) % 64;
    m_count[0] += (unsigned long)len << 3;
    if (m_count[0] < (unsigned long)len << 3) {
        m_count[1]++;
    }
    m_count[1] += (len >> 29);

    if ((j + len) > 63) {
        i = 64 - j;
        memcpy(&m_buffer[j], data, i);
        Transform(m_state, m_buffer);
        for (; i + 63 < len; i += 64) {
            Transform(m_state, &data[i]);
        }
        j = 0;
    } else
        i = 0;

    if (len - i != 0)
        memcpy(&m_buffer[j], &data[i], len - i);
}

const CSHA1::Digest &CSHA1::Final() {
    unsigned int i;
    unsigned char finalcount[8];
    unsigned char c;

    for (i = 0; i < 8; i++) {
        finalcount[i] =
            (unsigned char)((m_count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }

    Update((const unsigned char *)"\x80", 1);
    while ((m_count[0] & 504) != 448) {
        Update((const unsigned char *)"\x00", 1);
    }
    Update(finalcount, 8);
    for (i = 0; i < 20; i++) {
        m_digest.digits[i] =
            (unsigned char)((m_state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
    memset(m_buffer, 0, 0x40);
    memset(m_state, 0, 0x14);
    memset(m_count, 0, 8);
    memset(finalcount, 0, 8);
    Transform(m_state, m_buffer);
    return m_digest;
}

void CSHA1::Digest::Copy(unsigned char *c) const { memcpy(c, this, 20); }

void CSHA1::Digest::ReportHash(char *c1, unsigned char uc) const {
    char buf[24];
    unsigned char ui;
    if (c1) {
        if (uc == 0) {
            sprintf(buf, "%02X", digits[0]);
            strcpy(c1, buf);
            for (ui = 1; ui < 0x14; ui++) {
                sprintf(buf, "%02X", digits[ui]);
                strcat(c1, buf);
            }
        } else if (uc == 1) {
            sprintf(buf, "%u", digits[0]);
            strcpy(c1, buf);
            for (ui = 1; ui < 0x14; ui++) {
                sprintf(buf, " %u", digits[ui]);
                strcat(c1, buf);
            }
        } else
            strcpy(c1, "Error: Unknown report type!");
    }
}

BinStream &operator<<(BinStream &bs, const CSHA1::Digest &digest) {
    bs.Write(digest.digits, 20);
    return bs;
}

BinStream &operator>>(BinStream &bs, CSHA1::Digest &digest) {
    bs.Read(digest.digits, 20);
    return bs;
}

CSHA1::CSHA1() {
    m_block = (SHA1_WORKSPACE_BLOCK *)m_workspace;
    Reset();
}

CSHA1::~CSHA1() { Reset(); }
