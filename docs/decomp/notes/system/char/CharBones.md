# Lane notes: `src/system/char/CharBones.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?RecomputeSizes@CharBones@@IAAXXZ`

`CharBones::RecomputeSizes` -- moved verbatim from `src/system/char/CharBones.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 141-177):

```text
        // offset[-7] = mCounts[i], offset[-6] = mCounts[i+1]
        // (mCounts is 7 ints (0x1C bytes) before mOffsets)
        // RESIDUAL (w8-i, 99.926 canonical / 99.556 fuzzy, 3 rows of 27): the image
        // loads the SUBTRAHEND first -- `lwz r11, -0x1c(r10)` then `lwz r7, -0x18(r10)`
        // then `subf r7, r11, r7` -- while we load -0x18 first and flip the subf
        // operands to compensate.  Both compute offset[-6] - offset[-7]; only the
        // load order differs.  REFUTED: splitting into two named locals declared in
        // the image's load order (`int prev = offset[-7]; int next = offset[-6];`)
        // leaves all 3 rows bit-identical -- MSVC schedules the two loads
        // independently of statement order.
        // w21-as (still 99.926, same 3 rows): ~30 standalone cl.exe probe
        // spellings (TypeSize defined in-TU so the call clobbers only volatiles,
        // reproducing this exact listing) -- `-(a - b)`, `a + -b`, `-b + a`,
        // `x = a; x -= b`, unsigned diff, named TypeSize result, `++i < 7`
        // latch, end-pointer latch, `++offset; *offset =`, `offset[-1]` source,
        // a separate counts pointer (stays a second induction register), an
        // mCounts[i] index (adds lwzu on r9), `offset[1] = ...; offset++` and the
        // og-dc3/RB3 plain loop (both load -0x1c first like the image, but sink
        // the subf below the bl).  Only `*++offset = ...` keeps subf above the
        // call, and every such spelling loads the minuend (-0x18) first.
        //
        // w26-f VERDICT: equivalent -- both sides compute offset[-6] - offset[-7];
        // only the two loads' order and the matching subf operand pair differ.
        // MECHANISM (measured, `c2rs stage cands --regasg`): this is the sched3
        // order, not colour and not regasg.  regasg sees the two loads as
        // `RT 6 lwz d{s16}` (sid 198 = the -0x18 minuend) then `RT 8 lwz d{s17}`
        // (sid 197 = the -0x1c subtrahend), then `RT 11 subf u{s17 s16}`, and
        // first-fit hands them r11 then r7 -- the image's registers exactly.
        // The symbol ids say the IL minted the SUBTRAHEND first (sid 197 < 198),
        // which is also the image's emitted order, so the two tuples are already
        // right in the IL and sched3 reverses them.  Nothing in COLOR decides it
        // (both loads are `CDEMOTE`d to regasg temps; C 3, F 4), so the §8.4/§8.6
        // free-list levers cannot reach it either.  Tried and inert in w26-f:
        // `-offset[-7] + offset[-6]` (24/27, bit-identical) and the negated
        // single-statement form `cur_offset - TypeSize(i) * (offset[-8] -
        // offset[-7])` (5/28, worse).  Open question: what makes sched3 keep the
        // IL order for two independent same-base loads.
```


## `RotateByMultiply()`

`RotateByMultiply` -- moved verbatim from `src/system/char/CharBones.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Attribution: no function named `RotateByMultiply` in report.json (unit); filed under the source name

Above the definition (was lines 1092-1158):

```text
// RESIDUAL (w7-ba, 99.66 canonical, 330/357 rows; was 93.99): the two
// compressed arms are row-for-row equal.  The uncompressed arm keeps the
// image's seeds, fmadds sequence and store order, but the image loads a.z
// before b.w and slots the hoisted `cmplw cr6, r26, r24` at 0x823C6EF8 before
// the first fmuls, where ours issues the y seed as soon as its two operands
// are loaded (25 register rows + the two-instruction cmplw/fmuls transposition).
// Measured on that arm: source operand order inside the seeds is normalised
// (byte-identical); declaring z before y re-orders the emission to y, w, x, z
// (99.08, worse).  Not a source-visible knob that was found.
// w21-ap: 99.66 -> 100 normalized.  The knob is the BLOCK order in the
// uncompressed helper: computing w, then z, then y, then x (results still
// stored x, y, z, w by Set) gives the image's a.y, a.z, b.w load order and
// the cmplw slot.  Found with a standalone cl.exe probe of the uncompressed
// loop over all 24 block orders x seed-term swaps: exactly four orders (wzyx,
// wzxy, wxzy, xwzy) reproduce the image's load/op sequence, none with a seed
// swap.  Each component's expression is unchanged, so native results are
// bit-identical.  Remaining (fuzzy only, register operand order of a
// commutative op): six fmuls/fmadds/fnmsubs from 0x823C6F08 and the two
// Vector3 += fadds in the vector arms; swapping the source operands of the
// six affected products is byte-identical (MSVC canonicalises them).
// w25-gw (c2rs tap, C2RS-BRIDGE 8.7): the six products are ordered by c2's
// commutative key, and all eight uncompressed loads are TEMP leaves, key
// 0x10000|(sid<<6 & 0xffff), which wraps every 1024 sids.  Ours mints them
// aw 937 < bw/bx 939/942 < ax/ay 944/950 < by 952 < az 958 < bz 960.  The
// image's six orders are exactly that sequence with az and bz past the wrap
// (by > ax,ay > bx,bw > aw > bz > az), i.e. the image mints 66..71 more temp
// sids before this loop.  Confirmed: a dead inlined RotateByMultiply in the
// ByteQuat arm (+64 sids, code unchanged) wraps bz and drops 16 -> 12 diff
// lines.  Open question: which source construct is worth ~70 temp sids
// upstream without changing code.  Inert (sids or rows): block order (24),
// Multiply() from Mtx.h, out.x=... instead of Set, named a/b locals (the
// schedule changes).  The two Vector3 += fadds are the same key (temp vs
// named-local leaf) on other sids.  The load-temp sids are assigned after a
// whole-function pass (dead code in the rot loop shifts them as much as dead
// code before the loop).  One code-neutral handle measured: `*otherVecItr +=
// v` spelled Add(*otherVecItr, v, *otherVecItr) is +32 (937 -> 969); a
// second Add() adds nothing.  .begin() for .data(), !(a == b), and an
// explicit `a = a + b` in the rot loop are +0.  The sid shift is quantised
// (+8, +32, +34, +64 measured; extra named refs do not stack), so the
// remaining question is which construct is worth +66..71 by itself.
// The uncompressed Vector3 += z row is two late temps (my.z 1409 below
// other.z 1415); field-wise, named-ref and Add() spellings leave it.
// w26-n: the CQS dump confirms both pairs -- the compressed arm's z row is
//   CQS V1415/k02s03{000161c0} V1409/k02s03{00016040}
// and the six product rows sit on the sid-937..960 load temps above.  The
// base-reference lever was tried on the vector arm (`Vector3 &dstV =
// *otherVecItr;`, `const Vector3 &srcV = v;`): re-keying the destination's
// base is byte-inert (350/357 either way) and re-keying the source costs one
// more row (the compressed x add at 0xf8, 349/357).  A dead `RotateByMultiply`
// call placed in the ByteQuat loop is NOT eliminated before minting and moves
// the late loads by about +156 sids (1409/1415 -> 1565/1571), so the shift is
// real and measurable but does not land on the +66..71 the six products need;
// `RotateByMultiplyUncompressed` there leaves all seven rows unchanged.
// w26-f10: the 8.8 reference-binding lever DOES reach these products, and the
// wrap is not the only route.  Binding `const float &az = a.z;` in the
// uncompressed helper re-keys that operand from a load TEMP leaf
// (0x10000|(sid<<6 & 0xffff)) to a named-base memory leaf, which lands it
// below the other loads instead of above them: four of the seven rows go
// (fuzzy 99.804 -> 99.916, canonical 100.0, instruction count unchanged).
// The three that remain are all the products carrying `b.z` -- `a.w*b.z`,
// `a.y*b.z`, `a.x*b.z` -- where the image puts b.z SECOND while its fourth
// b.z product (`a.z*b.z`, the rw term) keeps it first.  A uniform re-key of
// b.z cannot satisfy both: `const float &bz = b.z;` scores 99.398 (and
// 99.146 on the three charged sites alone), taking canonical to 99.72 /
// 99.44 -- so on this row that binding changes the emitted code, not just
// the sort key.  Binding az AND bz together: 99.482.  Binding aw/bw: 96.19.
// Nothing tried moves the last three without a canonical regression.
```


## `?RotateTo@CharBones@@QBAXAAV1@M@Z`

`CharBones::RotateTo` -- moved verbatim from `src/system/char/CharBones.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 1480-1503):

```text
// w26-f4 (fuzzy lane, 99.75787 fuzzy / 100.0 normalized): 15 charged rows, no
// relocation-name mismatch, in three groups.
//   249/254/255  fmsubs/fmadds with f0<->f6, f11<->f6, f12<->f6: the first two
//                operand slots of the RotateToMultiply products.
//   292/294      image `fmuls f13, f31, f13` and `fmuls f11, f31, f11`; we
//                emit `fmuls f13, f13, f31` / `fmuls f11, f11, f31`. Rows 288
//                (f8*f31) and 293 (f12*f31) MATCH, so inside one q.Set the
//                image's order for the four q components is not uniform.
//   305-324      an f2<->f4 permutation across the RotateToMultiply expansion:
//                otherQuatItr->z (0x8) sits in f2 and ->y (0x4) in f4 in the
//                image, reversed here.
//
// Measured on the 292/294 group (normalized held at 100.0):
//   `q.Set(myQuatItr->x * f2, ...)` -> per-field `q.x = ...` assignments:
//     byte-identical, fuzzy unchanged -- so Set vs. field writes is NOT the lever.
//   swapping the source operand order to `f2 * myQuatItr->x` etc.:
//     byte-identical, fuzzy unchanged -- consistent with C2RS-BRIDGE 8.7: the
//     emitted order is the commutative key, not the order written in the source.
//   reordering the four components inside Set: 97.74/98.06 canon, rejected.
//
// Open: the alternation is not monotonic in the memory displacement -- the
// loads at 0xc and 0x4 sort one way and those at 0x0 and 0x8 the other, while
// all four share base r10 -- so displacement alone does not explain it. Needs
// the commutative-key dump (CQT), which the deployed c2rs does not carry.
```
