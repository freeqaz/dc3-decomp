# Lane notes: `src/system/math/mtx.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Multiply@@YAXABVTransform@@0AAV1@@Z`

`Multiply` -- moved verbatim from `src/system/math/mtx.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 73-85):

```text
    // w19-c (67.77 -> 100, all 62 rows equal): the translation is the
    // Multiply(const Vector3 &, const Transform &, Vector3 &) shape rb3-xenon
    // carries -- rotate straight into out.v and add t.v when out does not
    // alias b, rotate into a temporary otherwise -- with the add written
    // t.v + out.v (the image's fadds operand order).  Measured spellings:
    //   Multiply(a.v, b.m, out.v) + Add in the fast arm           60.1
    //   a call to the Transform overload (not inlined)             12.5
    //   spelled-out Set() written on a.v / b directly, no refs     2 lfs rows swapped
    //   the else arm ALSO written through v / t                     62.7
    //   this (refs in the fast arm only)                           100
    // So the shared y-component partial the image hoists above the alias
    // test, and the b.v.x / out.v.x load order, both hang on exactly this
    // spelling; do not "tidy" the two arms into one style.
```


## `?FastInvert@@YAXABVMatrix3@Hmx@@AAV12@@Z`

`FastInvert` -- moved verbatim from `src/system/math/mtx.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 106-115):

```text
// w25-gd stop note: one fmuls operand order differs (min.x.y * xdot; the image
// lists the load first, we list xdot first). Not a colour decision: the load is
// a regasg temp and the order is fixed in the IL operand list. Row x's loads are
// CSE'd from Dot(min.x, min.x) into the Set arguments (rows y/z read Dot through
// the reference and are not), and both CSE'd row-x reloads (x.y, x.z) come out
// xdot-first; the image has x.y load-first and x.z xdot-first. Inert (~300
// variants): source operand order, per-row ternary reciprocals, named products
// (x.x, z.y, z.z are code-neutral), Dot(min[k], min[k]), reference aliases.
// Naming the x.y load flips this operand but hoists the load. Open question:
// what key orders a CSE-reload operand against a named local in a commutative op.
```

Inside the body (was lines 118-125):

```text
    // w25-oa: operand order of `min.x.y * xdot` (c2 commutative key, C2RS-BRIDGE
    // 8.7). Row x's Set loads reuse Dot(min.x, min.x)'s address temps, so as
    // `min.x.y` the load is a memory leaf on base temp 192 (192 & 3 == 0: key
    // 0x10008) and sorts below xdot (variable sid 3: 0x10060) -- xdot first. The
    // image lists the load first. Through this reference the base is the
    // variable `xy` (sid 4: key 0x18008 > 0x10060), so the load sorts first;
    // min.x.z stays on base temp 196 (0x10008) and keeps xdot first, as the
    // image does. Same value, same instructions; only the operand list moves.
```


## `?Invert@@YAXABVMatrix4@Hmx@@AAV12@@Z`

`Invert` -- moved verbatim from `src/system/math/mtx.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 176-270):

```text
// w21-ac (2026-10-03): 70.67 -> 71.25 canonical (fuzzy 67.27 -> 67.49).
// CORRECTNESS, checked term by term: a symbolic evaluator run over the
// target listing and over ours gives all 16 outputs as expression trees;
// expanded as polynomials, all 16 were already equal (no wrong cofactor
// term, no wrong sign).  Rounding was not: the c10 acc chain had terms 4
// and 5 swapped, and 12 of the y/z/w cofactors were associated differently
// on PPC (and 10 natively).  Both are fixed (see the two w21-ac notes below);
// PPC and native association now equal the image's for all 16.  Measured
// negatives: the twelve y/z/w cofactors as locals with all four Sets at the
// end (the image's store placement) 31.2 even with the parenthesised trees
// (MSVC still hoists the operator[] block); a local Matrix4 copied to out at
// the end 62.7 (not scalar-replaced, stores to the stack); writing every
// element through row refs / operator[] without the inline_depth pragma
// inlines all 88 calls (22.5) -- so the image's out-of-line operator[] calls
// are not an inline-budget effect.
//
// AT_LIMIT as of 2026-09-01, 70.67% normalized, with codegen evidence:
// 759 instruction rows, `diff_op: none`, and ALL 245 diff_arg rows are
// accounted for by register renaming (477 swaps over 207 pairs, e.g.
// f0<->f13 x40, f20<->f28 x23) plus stack-offset shift -- 0 unexplained.
// The 41 `replace` rows are spill placement, not different work: they pair
// `stfs f?, 0x??(r1)` against `lfs`/`fmuls` at the same index, i.e. the two
// sides spill the same values at different points in the same expression.
//
// The DECOMPOSITION STRUCTURE already matches the target and is not the
// residual: the target calls Det(Matrix4) first (that function is at 100%),
// tests `fabs` against __real@38d1b717 (= 0.0001f), forms the reciprocal as a
// plain `fdivs` (one division, so no /fp:fast reciprocal folding to undo), and
// materialises row base pointers `addi r29,r31,0x20 / r28,r31,0x10 /
// r30,r31,0x30` matching the row1/row2/row3 references below.
// The residual is register allocation and spill scheduling inside expressions
// that exceed the FPR file. Do not permute operand order here.
//
// MEASURED NEGATIVE (2026-09-13, lane w3-m), read off
// build/373307D9/asm/system/math/mtx.s directly. The target really does hold all
// twelve column-1/2/3 cofactors live across the ~250 `bl ??AVector4@@QBAABMH@Z`
// calls and write ALL SIXTEEN results to `out` only at the very end: eight
// survive in callee-saved FPRs (f22-f30), the four `w` values are spilled to
// 0x5c/0x60/0xb8/0xbc(r1), and the sixteen `stfs ..(r27)` sit at 82539B4C-82539B9C
// after the last call. That accounts for the -0x30 frame delta and the twelve
// TGT_ONLY stack slots. Hoisting the three `out.?.Set(...)` calls out of the
// middle into twelve named `float` locals and Set-ing at the end -- which is
// exactly that shape -- makes MSVC do the OPPOSITE: with no store between them,
// it schedules the whole operator[] call block FIRST (first `bl` at index 53 of
// 647) and the cofactor math after it, because that keeps 4 values live across
// the calls instead of 12. 70.67 -> 32.8. Reordering the four Sets so `out.x`
// comes last (use order y,z,w,x) does not change that: 33.0. The early
// `out.y/z/w.Set` stores are what pins the region order in our build; there is
// no construct found so far that keeps the target's order without them, so the
// twelve mid-function stores are the price of the correct region order.
// Do not retry either variant without a new idea for pinning the schedule.
//
// w29-a (2026-11-02) BEHAVIOUR VERDICT: the RESULT is equal to the image's.
// Both sides call Det(Matrix4) once, test `fabs(det)` against __real@38d1b717
// (0.0001f), form the reciprocal with ONE `fdivs` (f0 = 1.0f in both -- no
// /fp:fast reciprocal-multiply to undo), multiply the same sixteen cofactor
// expressions by it, and write all sixteen results only at the end (image:
// the sixteen `stfs ..(r27)` at 0x82539B4C-0x82539B9C, four of them reloaded
// from 0x5c/0x60/0xb8/0xbc(r1)). The only structural difference is WHERE the
// y/z/w rows are stored: ours writes them mid-function, which is what pins
// the region order (w3-m measured the store-at-end shape at 32.8).
//
// THE ENGINE'S Matrix4 INVERSE IS NOT A TRUE INVERSE -- INHERITED, NOT A
// DECOMP DEFECT, and deliberately left alone. Det(const Matrix3&) returns
// 1/det, not det (target 0x82538B70: `fcmpu`/`beqlr` on det==0, then
// `lfs f0, __real@3f800000` / `fdivs f1, f0, f1`; it is at 100.0 normalized
// AND fuzzy, and og-dc3-decomp and rb3-xenon independently decompiled it the
// same way -- rb3-xenon comments it "returns its reciprocal (1/det)").
// Det(const Matrix4&) then forms det = +-Sum_j Det3(minor_j) * m.x[j], i.e.
// Sum_j (-1)^j m0j / det3(minor_j), which is neither the determinant nor its
// reciprocal. Worked example -- 45 deg rotation about z + translation:
// Det4 = 2.0 where the true det = 1.0, so out * in = 0.5 * I. This does not
// break the title: the split listing contains exactly ONE direct `bl` to
// ?Invert@@YAXABVMatrix4 in the whole binary, in RndCam::SetViewProj.
// Everything else calls Invert(const Transform&) or Invert(const Matrix3&),
// which compute 1/det inline and are correct. Both Det overloads are outside
// this lane's targets and sit at 100.0; changing either would break them.
// Correction to the w21-ac note above: its symbolic check compared the ADJUGATE
// against the inverse and was right about that, but `Det(minor)` was modelled
// there as det(minor) rather than as the 1/det the function actually returns,
// so the SCALING was never in the checked tree.
//
// MEASURED w29-a: the residual is not reachable from source. Ours 642
// instructions / 2568 B vs the image's 700 / 2800 B -- 58 SHORT. Census
// image -> ours: lfs 138->118, stfs 67->45, mr 136->135, fmuls 100->92,
// fmsubs 12->42, fnmsubs 36->15, fmadds 32->22, fneg 0->7, bl 73->73. The
// image holds twelve cofactors live across the ~69 out-of-line operator[]
// calls. Two form levers tested and rejected: (a) the fmsub/fnmsub split is
// not source-controlled -- MSVC canonicalises `acc = -(X - acc)` and
// `acc = acc - X` to the same opcode, and rewriting all twelve acc-chain
// negations moved fmsubs 42->43 / fnmsubs 15->14 and scored 71.166 (worse);
// (b) flipping the five negated y/z/w cofactors IS a clean 1:1 shift
// (42/15 -> 37/20 after all five) but drops to 70.199 and SHORTENS the body
// to 2560 B (C2RS-BRIDGE section 14 red flag). 71.25 is the best of the
// spellings tried; the remainder is the spill/schedule class w3-m named.
```

Inside the body (was lines 286-298):

```text
    // Cofactors for columns 1,2,3 stored directly to output rows y,z,w.
    // w21-ac: every term, product and sum here is the image's own expression
    // tree (0x825390C0), read off its fmuls/fmadds/fmsubs/fnmsubs chain by a
    // symbolic evaluator and checked as a polynomial against the inverse --
    // all 16 outputs were already mathematically right, but 12 rounded
    // differently from the image: MSVC's /fp:fast re-sorts a flat sum and a
    // flat product, and only an explicitly PARENTHESISED grouping survives.
    // The redundant-looking parentheses are therefore load-bearing: with
    // them the PPC build's association equals the image's for all 16 outputs
    // (it was 8 of 16), and the native build evaluates the same trees (it was
    // 6 of 16) -- a fidelity fix for both.  Commutative operand order and the
    // `x + -y` / `x - y` spellings were then hill-climbed for match with the
    // association held exact: 70.68 -> see the function comment.
```
