#pragma once
#include "rndobj\Mesh.h"
#include "math\Vec.h"
#include "utl/BinStream.h"

struct CompressedVertex_Xbox {
    float mPosX;
    float mPosY;
    float mPosZ;
    int mColor; // 0xc - packed color
    unsigned int mNormal;
    unsigned int mTangent;
    unsigned int mBinormal;
    unsigned int mBoneIndices;
    unsigned int mBoneWeights;
};

// PackVector is defined here, `static`, rather than out-of-line in one of the
// two Mesh.cpp files.  ham_xbox_r.map lists ?PackVector@@YAXAAIABVVector4@@EEEE_N@Z
// TWICE -- 826202e0 from rnddx9:Mesh.obj and 8263a168 from rndobj:Mesh.obj, both
// bare `f` -- which only internal linkage can produce, and the __FILE__ both
// copies bake in is e:\lazer_build_gmc1\system\src\rndobj/MeshVertCompress.h.
// The two shipped bodies are identical word-for-word except for 5 branch
// displacements and each TU's own copy of that file string, which is also why
// /OPT:ICF could not fold them: the string COMDATs sit at different addresses,
// so the bytes differ.
//
// w8-e 2026-09-15, RE-MEASURED AND CONFIRMED UNSCOREABLE.  fn_8263A168 (504 B)
// and fn_8263A360 (556 B) are the two largest 0% rows in rndobj/Mesh and both
// are this.  Evidence, in order:
//   * build/373307D9/asm/system/rndobj/Mesh.s:5332 / :5464 carve them as real
//     functions with .pdata entries (:2853, :2859) -- not EH funclets; 8263A360
//     is called at 8263B250 with (r3=CompressedVertex_Xbox*, r4=Vert*), one
//     instruction before `bl ?SaveCompressedVertex@@...` at 8263B25C;
//   * `strings build/373307D9/src/system/rndobj/Mesh.obj` lists BOTH
//     ?PackVector@@... and ?FillCompressedVertex@@... -- our object emits them;
//   * dtk's apply_symbols_file (jeff src/util/config.rs) PARKS a prior holder
//     when a REAL name collides, so symbols.txt can bind each name exactly
//     once; it binds the rnddx9 addresses (config/373307D9/symbols.txt:142797,
//     :142798) and leaves these two as fn_ placeholders;
//   * scripts/analysis/map_multiplicity_census.py --check-objs files both in
//     its 5-row REAL residue with ours_defines=True.
// Adding symbols.txt lines here is ZERO-SUM: it would park the rnddx9 copies as
// fn_826204D8 / fn_826202E0 and move the 0% rather than remove it.  The lever
// that pays is improving the SHARED bodies below, which scores in rnddx9 and is
// silently identical here: as of 2026-09-15 rnddx9's FillCompressedVertex reads
// 99.96% (8 rows: 6 lfs/stfs offset swaps, one r28<->r29 rlwimi pair) and its
// PackVector reads 96.2% (22 diff_arg / 3 replace, r29<->r30 dominant).
//
// w8-k 2026-09-15: independently re-derived, AGREES, and CLASS = PLACEHOLDER for
// both rows (fn_8263A168 @0x8263A168 and fn_8263A360 @0x8263A360, contributor
// rndobj:Mesh.obj).  Two things w8-e asserted but did not measure, now measured:
//   * the duplicate-name census it never ran comes back EMPTY -- parsing all
//     `NAME = .text:0xADDR;` lines of config/373307D9/symbols.txt gives 211608
//     entries, 211608 distinct names, 0 names at more than one address.  So the
//     "binds each name exactly once" claim is not just dtk's intent, it is the
//     shipped state of the file, and there is no precedent to copy;
//   * the swap is not merely zero-sum, it is a NET LOSS.  Sizes are identical
//     across each pair (504/504, 556/556), and we bank 96.190475% x 504 B +
//     99.95683% x 556 B on the rnddx9 side today.  A rename hands rndobj about
//     those same numbers and hands rnddx9 a hard 0.0.
// w8-p 2026-09-30: THE REBIND WAS ACTUALLY PERFORMED AND MEASURED, because both
// notes above argued it from the source rather than running it, and they
// disagreed about the sign.  Verdict: w8-e's "ZERO-SUM" is CONFIRMED and w8-k's
// "NET LOSS" is REFUTED -- it is an exact wash, to five decimals.
//   Method: rebind both names in config/373307D9/symbols.txt to 0x8263A168 /
//   0x8263A360 and delete the two fn_ lines.  dtk re-derives its own input on
//   the first build (the fixed-point rule) -- it moves the names into sorted
//   position and auto-names the vacated rnddx9 addresses fn_826202E0 /
//   fn_826204D8 -- and the second build is stable, split-guard green both times.
//   ADDRESS-RANGE TEST from config/373307D9/splits.txt: rnddx9/Mesh.cpp .text is
//   [0x82620180, 0x82622AF0) and holds 0x826202E0 + 0x826204D8; rndobj/Mesh.cpp
//   .text is [0x82639738, 0x826465E8) and holds 0x8263A168 + 0x8263A360.
//   Result, whole binary, full ninja each way:
//       matched_functions  31356 -> 31356   (no change)
//       matched_code     5563700 -> 5563700 (no change)
//       canonical          55.59% -> 55.59%
//     0 regressions over 48,367 rows; the only movement is 4 only-in-current /
//     4 only-in-baseline symbol-identity rows, i.e. the names themselves moving.
//   The rndobj copies score EXACTLY what the rnddx9 copies scored: PackVector
//   96.190475 norm / 94.920631 fuzzy, FillCompressedVertex 99.956833 /
//   99.848923 -- identical to five decimals, because it is the same shared body.
//
// ⚠ A PREDICTION OF MINE THAT THE MEASUREMENT KILLED, recorded so nobody rebuilds
// it: I expected the moved side to score LOWER, on the reasoning that rndobj's
// FillCompressedVertex would then call a target-side `fn_8263A168` while our
// object emits `?PackVector@@...`, costing a relocation-name row under
// name_check.  It does not, because BOTH names move together -- the intra-pair
// call is named on whichever side holds the names.  The pair is exactly
// symmetric.
//
// WHAT THIS SETTLES about the "+2 functions / +1060 B" framing: reaching 100 is a
// property of the SHARED BODY, not of which side is named.  Whichever side holds
// the names gets precisely the shared body's score, so the rebind neither
// unlocks the prize nor blocks it.  And on matched_code specifically the parked
// "partial credit" is worth ZERO either way, since report.json banks matched_code
// only at fuzzy == 100 and neither row is at 100 on either side.  The prize is
// unlocked by taking PackVector past 96.190475, after which it lands wherever the
// names already are.  So there is no reason to rebind, in either direction.
// Do not re-open this as a symbols.txt task.  The open work is PackVector's
// 96.190475% in the shared body below, which is a rnddx9/Mesh row, not a
// rndobj/Mesh one.
static const unsigned int kBitsOutput = 32;

// PackVector's 96.190475 residual is 28 rows and the cause IS identified: the
// image's entry sum of the four bit counts is a right-to-left CHAIN over the
// parameter registers,
//     add r11, p(r8), p(r7)  /  add r11, r11, p(r6)  /  add r9, r11, p(r5)
// where ours is a balanced TREE, (p(r5)+p(r6)) + (p(r7)+p(r8)).  The sum's TEXT
// is pinned by MILO_ASSERT's stringification (??_C@_0CP@JCHBHFAK@,
// "(bitsX + bitsY + bitsZ + bitsW) == ...", equal on both sides).
//
// REVERTED, and the reason is worth keeping.  Declaring the parameters W,Z,Y,X
// and passing 2, 10, 10, 10 DOES take this function to 100.0 -- the text
// X+Y+Z+W then maps to r8+r7+r6+r5 and MSVC's left chain is the image's.  It is
// still WRONG, and scripts/analysis/arith_semantics_scan.py proves it from the
// CALLERS: the image's three call sites emit `li r5, 0xa` and `li r8, 0x2`, so
// the image binds r5 to the 10-bit count and r8 to the 2-bit count.  Under a
// W,Z,Y,X declaration that reads bitsW = 10 and bitsX = 2, which cannot be the
// DEC4N/UDEC4N layout this function packs (x, y, z get 10 bits and w gets 2 in
// the high bits, via kOffsetW = bitsZ + bitsY + bitsX = 30).  The reorder was a
// NAME permutation that matched the callee's bytes while putting a wrong
// constant into every caller -- FillCompressedVertex picked up six li-constant
// rows for it and dropped 99.9568 -> 99.9137.  Behaviour was preserved either
// way; faithfulness was not.  Do not re-apply it: the chain-vs-tree difference
// has to be reached without moving which name binds which register.
static void PackVector(
    unsigned int &output,
    const Vector4 &vec,
    unsigned char bitsX,
    unsigned char bitsY,
    unsigned char bitsZ,
    unsigned char bitsW,
    bool normalize
) {
    MILO_ASSERT((bitsX + bitsY + bitsZ + bitsW) == kBitsOutput, 0x39);

    int offsetY = bitsX;
    int offsetZ = bitsY + bitsX;
    int normFactor = normalize ? 1 : 0;
    int kOffsetW = bitsZ + offsetZ;

    int shiftY = bitsY - normFactor;
    int shiftZ = bitsZ - normFactor;
    int shiftX = bitsX - normFactor;
    int shiftW = bitsW - normFactor;

    u32 maskY = (1U << bitsY) - 1;
    u32 maskZ = (1U << bitsZ) - 1;
    u32 maskW = (1U << bitsW) - 1;
    u32 maskX = (1U << bitsX) - 1;
    int maxX = (1 << shiftX) - 1;
    int maxY = (1 << shiftY) - 1;
    int maxZ = (1 << shiftZ) - 1;
    int maxW = (1 << shiftW) - 1;

    MILO_ASSERT(kOffsetW + bitsW == kBitsOutput, 0x4E);

    f32 fy = (f32)(f64)maxY;
    f32 fx = (f32)(f64)maxX;
    f32 fw = (f32)(f64)maxW;
    f32 fz = (f32)(f64)maxZ;

    u32 py = ((u32)(s32)(vec.y * fy)) & maskY;
    u32 px = ((u32)(s32)(vec.x * fx)) & maskX;
    u32 pz = ((u32)(s32)(vec.z * fz)) & maskZ;
    u32 pw = ((u32)(s32)(vec.w * fw)) & maskW;

    output = (pw << kOffsetW) | (pz << offsetZ) | (py << offsetY) | px;
}

static inline unsigned short FloatToHalf(float value) {
    unsigned int raw = *(unsigned int *)&value;
    unsigned int iValue = raw & 0x7FFFFFFF;
    unsigned int sign = (raw >> 16) & 0x8000;
    if (iValue > 0x47FFEFFF) {
        return (unsigned short)(sign | 0x7FFF);
    }
    if (iValue < 0x38800000) {
        unsigned int shift = 113 - (iValue >> 23);
        iValue = (0x800000 | (iValue & 0x7FFFFF)) >> shift;
    } else {
        iValue -= 0x38000000;
    }
    return (unsigned short)(sign | ((((iValue >> 13) & 1) + iValue + 0xFFF) >> 13));
}

// Same story as PackVector above, and the same evidence.  ham_xbox_r.map lists
// ?FillCompressedVertex@@YAXAAUCompressedVertex_Xbox@@ABVVert@RndMesh@@_N@Z
// TWICE -- 826204d8 from rnddx9:Mesh.obj and 8263a360 from rndobj:Mesh.obj,
// both bare `f`, both 0x22C bytes -- so it was one definition in this header,
// not two out-of-line definitions.  Decoding both shipped bodies: 139
// instructions each, differing only in 8 branch displacements and the
// PackVector callee each TU resolves to its own copy of.  config/symbols.txt
// can only carry one symbol per name, so it names 826204d8 and leaves the
// rndobj copy as the placeholder `fn_8263A360`.
// RESIDUAL 99.95683 / 8 rows (w8-p 2026-09-30), and the 8 rows are ONE decision
// twice over: which member of a pair is converted 3rd vs 4th, after which the
// register and stack-slot assignment follows mechanically.
//   * colour group, idx 7/9/34/35: the image assigns f13,f12,f11,f10 <-
//     green,blue,ALPHA,RED (`lfs f11,0x3c(r4)` / `lfs f10,0x30(r4)` at
//     0x826204F4 / 0x826204FC) and we assign green,blue,RED,ALPHA.  The first
//     two agree, so it is only the last pair.  Downstream, the image converts
//     red 4th into the SECOND scratch slot (`fctidz f0,f0` + `stfd f0,0x60(r1)`
//     at 0x82620550/54, read back `lwz r28,0x64(r1)`) while alpha goes through
//     0x50/0x54 into r29; we route alpha through the second slot instead, which
//     is the whole of the `rlwimi r28,r29` <-> `rlwimi r29,r28` row pair at
//     0x82620560/64.  Both spellings compute (alpha<<8)|(red&0xFF) correctly.
//   * normal group, idx 102/103/105/113: the image loads norm.z (0x18) BEFORE
//     norm.y (0x14) -- `lfs f11,0x18(r31)` / `lfs f13,0x14(r31)` at
//     0x82620670 / 0x82620674 -- then stores y to 0x64(r1) and z to 0x68(r1);
//     we load y first and store the same two values to the same two slots.
//     Semantically identical, f11<->f13 exchanged.
// MEASURED NEGATIVES (w8-p, each a full post-compile build, each EXACTLY inert
// -- identical 8-row table, identical 99.84892 fuzzy, not merely the same
// rounded canonical):
//   1. swapping the `alpha`/`red` declarations, and separately the
//      `normZ`/`normY` declarations, so the pair is declared the other way up;
//   2. deleting the `normZ`/`normY` locals altogether and writing
//      `Vector4 normVec(vert.norm.x, vert.norm.y, vert.norm.z, 0.0f)` -- the
//      right-to-left argument evaluation that fixed RndText::SetColor (w7-bx)
//      does NOT reach these loads, because the locals were never what pinned
//      them;
//   3. commuting the innermost `|` to `(red & 0xFF) | (alpha << 8)`, which under
//      right-to-left evaluation should have converted alpha first.
// The scheduler, not the source, picks which conversion gets the second scratch
// slot; all three source-visible orderings produce the same bytes.  Do not retry
// declaration order, argument inlining, or commuting this `|`.
static void FillCompressedVertex(
    CompressedVertex_Xbox &compressed, const RndMesh::Vert &vert, bool normalize
) {
    // Pack color (ARGB D3DCOLOR format)
    u32 green = (u32)(vert.color.green * 255.0f);
    u32 blue = (u32)(vert.color.blue * 255.0f);
    // ADJUDICATED, NOT A BUG (w9-e 2026-09-30).
    // arith_semantics_scan.py reports an [operand-source] row here --
    //     target  rlwimi r28, r29, 8, 0, 23
    //     ours    rlwimi r29, r28, 8, 0, 23
    // with r29 = [0x54(r1)] and r28 = [0x64(r1)] loaded by rows that are EQUAL
    // on both sides, so the two values really are exchanged at that one
    // instruction.  It is compensated two instructions earlier: the image loads
    // `lfs f11, 0x3c(r4)` / `lfs f10, 0x30(r4)` (alpha then red) where we load
    // `lfs f11, 0x30(r4)` / `lfs f10, 0x3c(r4)` (red then alpha).  Composing
    // both exchanges, BOTH sides store
    //     alpha<<24 | red<<16 | green<<8 | blue
    // i.e. D3DCOLOR ARGB, which is also what this vertex element must be.  The
    // scanner row is a true operand-source difference and a false bug.
    // REFUTED as a scoring lever: swapping these two declarations (so MSVC's
    // own reordering of the last two would land on the image's order) is
    // byte-inert -- 99.9568 / 99.8489 either way, same 8 rows.  4 of those 8
    // rows are this compensating pair.
    u32 alpha = (u32)(vert.color.alpha * 255.0f);
    u32 red = (u32)(vert.color.red * 255.0f);
    compressed.mColor = ((((alpha << 8) | (red & 0xFF)) << 8) | (green & 0xFF))
            << 8
        | (blue & 0xFF);

    // Pack bone weights as UDEC4N
    PackVector(
        (unsigned int &)compressed.mBoneIndices, vert.boneWeights, 10, 10, 10, 2, false
    );

    // Copy position as float bit patterns
    *(f32 *)(&compressed.mPosX) = vert.pos.x;
    *(f32 *)(&compressed.mPosY) = vert.pos.y;
    *(f32 *)(&compressed.mPosZ) = vert.pos.z;

    // Pack UV as float16_2
    unsigned short halfU = FloatToHalf(vert.tex.x);
    unsigned short halfV = FloatToHalf(vert.tex.y);
    compressed.mNormal = (halfU << 16) | halfV;

    // Pack normal as DEC4N
    float normZ = vert.norm.z;
    float normY = vert.norm.y;
    Vector4 normVec(vert.norm.x, normY, normZ, 0.0f);
    PackVector((unsigned int &)compressed.mTangent, normVec, 10, 10, 10, 2, true);

    // Pack tangent as DEC4N
    PackVector((unsigned int &)compressed.mBinormal, vert.tangent, 10, 10, 10, 2, true);

    // Pack bone indices as UBYTE4
    compressed.mBoneWeights = (((int)vert.boneIndices[3] * 0x100
        + (int)vert.boneIndices[2]) * 0x100
        + (int)vert.boneIndices[1]) * 0x100
        + (int)vert.boneIndices[0];
}

void SaveCompressedVertex(const CompressedVertex_Xbox &, BinStream &);
