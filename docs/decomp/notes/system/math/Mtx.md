# Lane notes: `src/system/math/Mtx.h` and `src/system/math/mtx.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 127-132; nearest following definition: `Hmx::Matrix4::Matrix4`):

```text
        // w8-e: defined here, not out-of-line in mtx.cpp.  ham_xbox_r.map lists
        // ??0Matrix4@Hmx@@QAA@ABVTransform@@@Z once, flagged `i` (COMDAT) and
        // contributed by rnddx9:Rnd.obj at 0x82610B38 -- an out-of-line
        // definition in mtx.cpp would have been flagged `f` and contributed by
        // math:mtx.obj, and build/373307D9/asm/system/math/mtx.s does not
        // mention the symbol at all.  Ten target units name it.
```


## `Plane::Plane()`

`Plane::Plane` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `Plane::Plane` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 403-411):

```text
    // w13-o: Character::DrawShadow's residual (the image keeps x*0 + y*0 as
    // two products; we factor them into (x+y)*0) does not move with this body.
    // Measured on DrawShadow + CharCollide::Highlight (the only two callers):
    // `-::Dot(normal, point)` and `-::Dot(point, normal)` 97.32 -> 94.34;
    // terms z,y,x flat: inert; `float dot = ...; d = -dot;` (named or via
    // ::Dot): inert; accumulator statements and explicit parens cost Highlight
    // its 100; delegating to Set(): inert.  Call-site respellings in DrawShadow
    // (worldPos.z += planeD first, named point/normal temps, 0.0f literals) are
    // inert too.
```


## `?Normalize@@YAXABVMatrix3@Hmx@@AAV12@@Z`

`Normalize` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 495-501):

```text
// w13-o: written with Cross(), the spelling og-dc3 and rb3-xenon carry. The
// previous body spelled the two cross products out as `out.x.Set(...)` with
// the identical expressions, and that alone was 95.78 canonical (the w8-i
// residual: the image stores out.x z, x, y and we stored z, y, x, with the
// fmuls operand orders and an FPR permutation following from it). Going
// through the inline Cross() is 100.0 for this COMDAT (char/Character.obj);
// whole-binary A/B, full ninja both sides: 1 up, 0 down.
```


## `?Multiply@@YAXABVVector3@@ABVQuat@Hmx@@AAV1@@Z`

`Multiply` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 535-598):

```text
// ⚠ Do NOT right-associate these three sums as `a + (b + c)`.  Measured
// whole-binary A/B in worktree w5-f (2026-09-14, full ninja both sides):
// 5 functions up, 14 down, matched_functions 30962 -> 30958, matched_code
// -800 B, headline 47.6415 -> 47.6345.  The five small functions whose whole
// body IS one inlined copy -- ?Multiply@@YAXABVTransform@@ABVMatrix3@Hmx@@AAV1@@Z
// (100 B), MultiplyInverse (180 B), CharServoBone::MoveToDeltaFacing (216 B),
// GetLightPosition (256 B), DrawBounds (264 B) -- are all at 100% with the
// left-associated form and all fall to 91-96% with parentheses, so the
// left-associated spelling is the original's.  The two functions that GAIN
// from the parentheses (Spotlight::UpdateTransforms 91.956 -> 100.000 exactly,
// RndShadowMap::PrepShadow 93.388 -> 99.985) both call this with a vector whose
// x and z are literal 0.0f; under /fp:fast MSVC factors `(m.x.c + m.z.c) * 0`
// out of the unparenthesised form and the target does not.  Whatever those two
// sites called in the original, it was not this overload spelled this way --
// chase it there, not here.
//
// RESOLVED 2026-09-14 (lane w6-i, same worktree).  Both of those sites are now
// at 100.0 canonical, fixed AT THE CALL SITE, with this overload untouched:
// Spotlight::UpdateTransforms 91.956 -> 100.0 (1456 B) and
// RndShadowMap::PrepShadow 93.388 -> 100.0 (1072 B).  Whole-binary check after
// both: 0 regressions, 2 improvements, 48371 unchanged.
//
//   * The control that settles WHERE the lever lives: spelling this overload's
//     own left-associated tree out at the Spotlight call site scores 91.95605,
//     the baseline to five decimals.  Call-site context is irrelevant; only the
//     expression TREE shape matters.  So a site that needs a different tree can
//     have one locally, and the four callers that are already at 100% are safe.
//
//   * The defect is not the association, it is MSVC's `a*K + b*K -> (a+b)*K`
//     factoring.  Read the target of either function: it emits the two zero
//     terms as plain `fmuls fN,fN,f31` and folds them in with `fmadds`, with no
//     leading `fadds` of two matrix elements.  PrepShadow's target is the proof
//     that association is a red herring -- its y and z components are LEFT
//     associated (they seed from m.x.c at 0x94/0x98) and are still not factored.
//     Right-association merely happens to break the factoring pattern.
//
//   * What actually works is the ACCUMULATOR form -- `float a = ...; a += ...;
//     a += ...;` -- because statement order pins the association and MSVC does
//     not reassociate across the `+=`.  That lets each component seed from the
//     element the target seeds from.  Same shape as Hmx::Dot4 above, which a
//     previous lane also had to write as a reversed accumulator.  On PrepShadow:
//     uniform right-association 99.98508 (4 charged seed-offset rows left),
//     left-associated y/z as one expression 94.66418 (factoring comes back),
//     per-component accumulators seeded like the target 100.0.
//
//   * REFUTED here, one full ninja, 7 probe functions: swapping the multiply
//     operand order in this body to v-first (`v.x * m.x.x + ...`).  All seven
//     scored byte-identical -- MSVC canonicalises the commutative multiply, so
//     operand order in the SOURCE is not a lever on this overload at all.
//
//   * ⚠ The call-site accumulator lever does NOT generalise to every site, and
//     the failure mode is not local.  Lane w7-e (2026-09-14) applied it to
//     RndParticleSys::InitParticle's aliasing `Multiply(particle->Vel3(),
//     xfm->m, particle->Vel3())`, seeded from the Y term exactly as that
//     target does, and the function fell 99.2943 -> 98.5 canonical.  The
//     rows it was aiming at did move, but the five extra named floats and the
//     `Vector3 &`/`Matrix3 &` aliases changed the whole function's register
//     assignment: a fresh r25<->r26 swap appears at index 49, ~600
//     instructions BEFORE the call site, and the charged row count goes 73 ->
//     106.  Spotlight::UpdateTransforms and RndShadowMap::PrepShadow are short
//     functions whose body is essentially the one call; in a 2,700-byte
//     function with 30 live floats the accumulators cost more than the tree
//     shape buys.  Check the function's size and register pressure before
//     reaching for this, and measure the WHOLE function, not the cluster.
```


## `?Multiply@@YAXABVVector3@@ABVTransform@@AAV1@@Z`

`Multiply` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 607-616):

```text
// w13-o: the aliasing arm rotates into a temporary and adds the translation on
// the way out, the shape rb3-xenon recovered for RB3 retail. The fused 4-term
// `out.Set(... + t.v.x, ...)` it replaces scheduled the components z, y, x and
// tail-merged with the fast arm; the image (char/Character.obj COMDAT) evaluates
// x, y, z, reads t.v through the `&t.v` it already computed for the compare,
// and returns from each arm separately. 94.53 -> 100.0; whole-binary A/B (full
// ninja both sides): 1 up, 0 down -- every inlined copy at a call site where
// the compiler can prove &t.v != &out is unaffected.  Both arms then written
// through Multiply(v, t.m, .) (og-dc3's shape; it needs that overload's body
// above it): whole-binary byte-inert, 0 up / 0 down.
```


## `?Multiply@@YAXABVMatrix3@Hmx@@0AAV12@@Z`

`Multiply` -- moved verbatim from `src/system/math/Mtx.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Above the definition (was lines 631-682):

```text
// NOTE for whoever is chasing Spotlight::UpdateTransforms: the lane that moved
// this function into the header disclosed a -2.34% regression there and expected
// it back "with the real two-path body". It does not come back, and it cannot.
// Landing the two-path body left ?UpdateTransforms@Spotlight@@IAAXXZ
// byte-identical -- same 1456 bytes, same 91.96% normalized in report.json, not
// in either the regressed or the improved list of a whole-build diff. A body
// this different (one out.Set(9) became a branch plus three per-row Sets) could
// not leave an inlined copy unchanged, so MSVC is not inlining this into
// UpdateTransforms at all; its three Multiply calls are out-of-line. The -2.34%
// is a cost of the PLACEMENT change (mtx.cpp definition -> header COMDAT, so the
// call now binds to Spotlight.obj's own folded copy), not of the body, and it
// has to be attacked as such.
//
// Two paths behind an aliasing test.  The target opens with
//   cmplw cr6, r4, r5 ; beq cr6, <buffered>
// i.e. it compares &b against &out and falls through to the fast path, so the
// source shape is "if (&b != &out) { fast } else { buffered }".  The fast path
// stores each output row the moment it is computed (stfs to 0x0/0x4/0x8 of the
// row, three floats -- a Vector3::Set), which is only alias-safe when b and out
// are distinct.  The buffered path computes each row into a stack Vector3 and
// copies it out with Vector3's implicit operator=, which is a 16-byte copy
// because of the trailing PAD word -- that is why the target's tail moves four
// words per row (lwz/stw at +0x0/+0x4/+0x8/+0xc) instead of three, and why it
// writes garbage into out's pad slots.
//
// Floor 80.7 canonical (lane w7-ay, 2026-09-14), consistent with the notes
// above: the residual is FPR scheduling of the 27 products (the target keeps
// f24-f31 and r30-r31 live, we keep f25-f31 and r31) and the store order.
// Read from the target without building: vy and vz share one stack slot
// (target -0x70, ours -0x60) so a `Matrix3 tmp` is out, and the +0xc pad copy
// rules out `Set()` copies.  Built and measured, each a full ninja:
// `Vector3 vz, vy, vx;` is inert (80.7, same rows); copy-out order z,y,x is
// 77.8 and y,x,z is 80.0 against x,y,z's 80.7.  One real lever surfaced: the copy
// order decides which b element MSVC hoists above the alias branch -- z,y,x
// hoists b.y.x, x,y,z hoists nothing, and the target hoists b.y.y
// (`lfs f7, 0x14(r4)` at 0x8236E970 in CharLookAt.s).  w13-o built the
// remaining three: x,z,y 81.05 (kept -- whole-binary A/B, full ninja both
// sides: this row 80.706 -> 81.053, nothing else moved), z,x,y 78.3, y,z,x
// 79.9.  None of the six reaches the image's b.y.y hoist, so the copy order is
// not the whole story.
// w21-q (81.05, unchanged): read the image's 18 output trees symbolically
// (fast arm row a.x: x=((Y+Z)+X), y=((Z+Y)+X), z=((Y+X)+Z); the alias arm and
// the other rows use other associations) -- the image's association is NOT
// uniform per row, so MSVC chose it, and this flat-sum body already produces
// 10 of the 18 trees.  Standalone cl.exe probe (reproduces this build
// exactly), each scored on raw registers and on tree identity: all 216
// per-component term orders of the Vector3 x Matrix3 row helper as
// accumulators (best 105/170 raw rows but only 5/18 trees), parenthesised
// (89 raw, fewer trees), flat (canonicalised: every order identical),
// per-component float temps in all 6 statement orders; inverted branch,
// early return, separate Vector3 decls, a Matrix3 temp (direct or recursive).
// None beats this body on trees; same 80.7 floor in rb3-xenon's mtx.cpp copy.
```

Inside the body (was lines 689-695):

```text
        // The three products are independent and each row is copied into a
        // distinct row of a distinct object, so this order is a free choice.
        // It is measured: all 6x6 product/copy orderings were ranked with the
        // c2rs replay, and this one is 84.42 canonical against 81.05 for the
        // previous x,y,z / x,z,y spelling; the winner was confirmed by a
        // whole-report A/B (UP 1, DOWN 0). The three Vector3 declarations'
        // order is inert. w26-w.
```

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

w-low-ham-math (2026-11-13), lane D: **latent aliasing divergence, not fixed.**

```text
The image is ALIAS-SAFE and ours is not. Target (0x825390C0): every one of
the 16 output stores (stfs ..(r27), 0x82539B4C..0x82539B9C) comes AFTER the
last ??AVector4@@QBAABMH@Z read of m. Ours stores out.y/out.z/out.w (12 stfs,
base idx 192-243) BEFORE the 64 operator[] reads that compute column 0, so
Invert(m, m) would compute out.x from an already-overwritten m.y/m.z/m.w.
Today it is unexercised: the only Matrix4 caller in the image is RndCam
(0x826287D4, out = this+0x340, in = this+0x300), and no source caller passes
the same Matrix4 twice. Alias-safe spellings measured, name_check:
  - column-0 operator[] block moved above the three Sets:  29.5 (from 71.25)
  - (prior lane) twelve y/z/w cofactors as locals, Sets last: 31.2
  - (prior lane) local Matrix4, copied to out at the end:      62.7
None was landed. A fix needs a spelling that keeps the twelve cofactors
live in FPRs across the operator[] calls (the image's 14 saved FPRs) AND
defers the stores; if one is ever found, it is a behaviour fix.
```
