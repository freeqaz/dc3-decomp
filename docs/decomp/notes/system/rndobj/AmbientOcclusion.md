# Lane notes: `src/system/rndobj/AmbientOcclusion.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 88-92; nearest following definition: `RndAmbientOcclusion::BlendVert`):

```text
// w15-r: INIT_REVS precedes every other file-scope const here.  The image's
// .rdata for this TU opens with gRev (0x820A6570) and gAltRev, and only then
// 0.001f, {50, 0.5, 2}, 0.67625f and kQualityLUT (0x820A658C).  Defined below
// kQualityLUT, gRev sat at offset 0x10 and MSVC anchored the ASSERT_REVS pair on
// gAltRev; the image anchors on gRev.
```


## `?BlendVert@RndAmbientOcclusion@@SAXABVVert@RndMesh@@0AAV23@@Z`

`RndAmbientOcclusion::BlendVert` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 111-186):

```text
    // NEGATIVE RESULT 2026-09-14 (w7-ae), 85.7% canonical.  The interleaving
    // below is load-bearing and two variants that tidy it up both REGRESS:
    //   * hoisting `tang.z`/`tang.y` to sit right after `tang.x` (which is where
    //     the image's `lfs 0x54(r30)` / `lfs 0x58(r30)` appear, at target
    //     AmbientOcclusion.obj offsets 0x818 and 0x834, interleaved into the
    //     `out.pos *= 0.5f` block) measures
    //     85.7 -> 78.9.  The image hoists only the LOADS; the adds and the
    //     stores back into the stack copy stay after the colour multiply, so the
    //     current statement order is already the one that produces them.
    //   * replacing the three `out.tangent.<c> = tang.<c>` stores with a single
    //     `(Vector3&)out.tangent = (const Vector3&)tang;` measures 85.7 -> 82.7.
    //     It does fix the one real ordering row (we sink the 0x50 store past the
    //     colour zeroing, the image does not) but costs more elsewhere.
    // What is left after those is ~90 rows of pure FPR renaming with identical
    // opcodes on both sides, plus ~4 rows where MSVC defers the `lfs 0x44(r30)`
    // of `out.tex += v2.tex` past the store to out.tex.x -- i.e. our build proved
    // the two Vert& do not alias and the image's did not.
    // w7-bw (2026-09-15), 85.66912 -> 88.7: the colour zeroing at the end is
    // Color::Set(0.0f) (its chained assignment stores alpha, blue, green, red
    // = 0x3c..0x30 in that order) -- that also stops our build sinking the
    // out.tangent.x store past the zeroing, so rows 121-142 now match.
    // Refuted on top of that: out.tex.Set(x + v2.x, y + v2.y) fixes the tex
    // block's load order but reshuffles the pos add (88.0); Scale(out.pos,
    // 0.5f, out.pos) for the pos scaling is inert (88.7); `out.tex *= 0.5f`
    // ahead of the tangent copy makes the pos add byte-exact but drags the
    // tex scale up with it (75.4); reading v2.tangent.x/y/z into float locals
    // before the scaling (the image holds them in f11/f13 across it) hoists
    // the loads above the tangent copy instead (85.1).
    // w26-k (2026-11-02): mechanism, from the c2 tap.  Every FPR left in the
    // swap set (f8..f13, 12 pairs) is a SECOND-allocator (regasg) selection,
    // not a COLOR candidate: this function's entire FP candidate list is two
    // arena temps, id14 pri 50 tie 58 and id15 pri 25 tie 5, and both are
    // assigned f0 (different live ranges).  regasg is first-fit over the
    // sched3 order, so a pair's assignment is a pure function of when each
    // load's def and each value's last use reach it, and there is no
    // priority/tie key to move; the only source handle is expression shape,
    // which the refutations above already varied six ways.  Behaviour checked
    // instruction-by-instruction against the image: the two sides carry the
    // same multiset of loads, stores, adds and multiplies, every store
    // destination offset is equal on both sides, and there is no branch,
    // call or constant difference (objdiff diff_op: none; unicorn verdict
    // EQUIVALENT at harness h4).  The eight insert/delete rows are the same
    // values emitted in a different order, not different values.
    // w29-d (2026-11-02): still 88.661766 canonical / 85.83088 fuzzy, and the
    // classification above survives a fresh diff -- 70 equal / 57 diff_arg /
    // 7 insert / 7 delete / 2 replace, body 136 vs 136 (identical length, so
    // nothing here is a shortened-body score).  The two `replace` rows are the
    // `lfs f11, 0x54(r30)` and `mr r3, r4` that the image hoists one sched0
    // step earlier; both are single-position moves of an otherwise matching
    // instruction.  Arithmetic re-checked as arithmetic on both sides: the
    // tangent is summed x/z/y in that order on both, the two Normalize calls
    // take identical inputs, and the final `Set(0.0f)` is present on both.
    // Nothing to fix here; the residual is the regasg first-fit selection
    // described above.
    // w34-b (2026-11-04): the strongest equivalence certificate this row has
    // had.  Comparing each side's OWN instruction stream (not the aligned row
    // view, which pairs across a permutation and invents field swaps), the
    // normalised instruction multiset is IDENTICAL: 136 instructions and
    // 544 bytes on both sides, the same opcodes with the same immediates,
    // the same store and load displacements, and the same callee set.  So
    // there is no value, field, immediate, call or loop-bound difference to
    // find -- the residual is order alone.  First divergence is instruction
    // 14: the image issues `lfs f12, 4(r30)` one slot before its `fadds`,
    // i.e. sched0 interleaving, not source order, so no spelling reaches it.
    // Measured this lane and rejected: `const float &tx = v2.tangent.x;`
    // (canonical unchanged 88.661766, fuzzy 85.83088 -> 85.79412) and moving
    // the `Vector4 tang = out.tangent;` copy to the top of the function
    // (75.39706: the position of that copy is load-bearing).
    // w46-a (2026-11-06): the two remaining tangent adds are independent
    // (each reads and writes its OWN component of the local `tang`), so their
    // source order is inert -- and writing x/y/z makes sched0 emit them in the
    // image's order instead of ours: 88.661766 -> 88.676471 canonical,
    // 85.83088 -> 85.84559 fuzzy, one row, body unchanged at 544 B / 136
    // instructions on both sides.  The rest of the residual is instruction
    // order only: every other multiset (mnemonic, branch, relocation,
    // displacement) is identical.
```

Added by lane w-low-rndobj, 2026-11-13:

```text
BEHAVIOUR VERDICT: AGREES (88.68 / 85.85, unchanged).  memcpy 0x60; pos, tex,
color (4 comps), norm += v2; pos/tex/color *= 0.5; tangent.xyz += v2 into a
stack copy; Normalize(norm); Normalize(tang); tangent.xyz stored (w kept from
v1); color zeroed (all 4).  Offsets pos 0/norm 0x10/color 0x30/tex 0x40/
tangent 0x50 all agree.  Residual is FPR renaming and load placement.
```


## `?Load@RndAmbientOcclusion@@UAAXAAVBinStream@@@Z`

`RndAmbientOcclusion::Load` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 354-375):

```text
    // 7-row residual, 97.82%: the documented ASSERT_REVS gRev/gAltRev CSE
    // anchor family (docs/decomp/patterns/relocation-names-are-unmetered.md,
    // which names this function by hand).  `subi r7, r29, 0x4` vs `mr r7, r28`
    // plus the addi reorder and the paired `mr r3, r28/r29`.  Also refuted here
    // (byte-identical, same 7 rows): spelling the superclass call as
    // `Hmx::Object::Load(bs)` instead of LOAD_SUPERCLASS's `d.stream`, the
    // lever that took SynthSample::PreLoad 99.54 -> 100.  It does not
    // generalise to this family -- the stream argument is not what these rows
    // are about.
    //
    // w9-d 2026-09-30, CONFIRMED AT LIMIT, do not spend a build here.  This row
    // is one of ten named by hand in
    // docs/decomp/patterns/relocation-names-are-unmetered.md, which refutes the
    // whole family: 613 of the 645 sibling Loads are already at 100% with this
    // exact spelling, so any change that reaches these ten breaks those 613.
    // The anchor pick also FLIPS between siblings, which settles that it is
    // MSVC-internal.  Here the image anchors gRev (r28) and reaches gAltRev as
    // `addi r7, r28, 0x4` at 0x826DF4FC while we anchor gAltRev and reach gRev
    // as `subi r7, r29, 0x4`; in HamDirector::Load (98.16216, 592 B, also on
    // that list) it is the exact MIRROR -- the image does
    // `subi r7, r29, 0x4` and we do `mr r7, r28`.  Same macro, same two
    // file-statics, opposite pick, nothing in either source distinguishes them.
```


## `?BuildObjectLists@RndAmbientOcclusion@@QAAXXZ`

`RndAmbientOcclusion::BuildObjectLists` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 580-585):

```text
    // BEHAVIOUR FIX (w15-i1): the comparator ranks by position in the USER's
    // tessellate list, not in the vector being sorted.  The image passes
    // `addi r5, r31, 0x68` to sort -- the address of the local tessellateMeshes
    // (the same 0x68(r31) the third GatherObject loop fills) -- where we passed
    // &mObjectsTessellate.  Ranking by index in the vector std::sort is busy
    // permuting is not an ordering at all.
```


## `?TransformNormal@RndAmbientOcclusion@@IBAXABVVector3@@ABVMatrix3@Hmx@@AAV2@@Z`

`RndAmbientOcclusion::TransformNormal` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 602-620):

```text
    // Declaration order z,x,y is the only one of the six that reproduces the
    // image's 46-instruction shape; every other order gains two instructions
    // (a duplicated `lfs`/`fmuls` pair) and reads 91.1-91.2:
    //   z,x,y  97.30  46 instr  <- this
    //   z,y,x 91.07,  x,z,y 91.07,  x,y,z 91.07,  y,z,x 91.20,  y,x,z 91.20
    // Residual at 97.30 (w26-y: 29 spellings measured): 15 diff_arg rows and
    // one 2-row scheduling swap, and they follow from ONE tree difference.
    // `Dot(a,b)` parses left-to-right, ((a.x*b.x) + a.y*b.y) + a.z*b.z; c2 may
    // reassociate that to a.x*b.x + (a.y*b.y + a.z*b.z) before contraction, and
    // it decides PER CALL SITE.  The image reassociates x but not y or z (its y
    // row starts from mtmp.y.x, its z row from mtmp.z.x); we reassociate x and
    // y, so only y's fmuls/fmadds split and its f9/f11 accumulator differ, and
    // the rest of the register permutation plus the `mr r3, r30` hoist follow
    // from it.  Measured inert: all six declaration+term orders (above), all
    // six assignment orders under z,x,y declarations, the operand swap
    // Dot(mtmp.y, vtmp) (byte-identical), and binding mtmp.y to a reference or
    // a by-value Vector3 (86-91).  Inline arithmetic reads 91.0: it takes the
    // association decision away from c2 rather than steering it.
    // Compute dot products with inverted matrix rows
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?IsSerializable@RndAmbientOcclusion@@QBA_NPBVRndMesh@@@Z`

`RndAmbientOcclusion::IsSerializable` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 653-659):

```text
    // w28-e: the image compares the two pointers with a 32-bit `cmpw` because
    // PPC pointers ARE 32 bits (0x826DB5E8 `lwz r3, 0x28(r11)`, 0x826DB5EC
    // `cmpw cr6, r3, r10`). Truncating to `int` on LP64 compares only the low
    // halves, so two DIFFERENT dirs 4 GiB apart compare EQUAL here and a mesh
    // is reported serializable outside its own dir. Same class as
    // DataNode::Equal and TypeProps' `(int)ptr == (int)ptr`. intptr_t is `int`
    // on PPC (src/types.h:76), so the Xbox codegen is unchanged.
```


## `?FindSplit_SAH@kdTreeNode@?$kdTree@VTriangle@@@@QAA_NABVBox@@ABV?$list@PAVTriangle@@V?$StlNodeAlloc@PAVTriangle@@@stlpmtx_std@@@stlpmtx_std@@@Z`

`kdTree<Triangle>::kdTreeNode::FindSplit_SAH` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 857-892):

```text
// OPEN (w26-f7): 100 normalized, fuzzy 99.444, 11 diff_arg rows, all one
// three-way register rotation among the last three callee-saved candidates.
// Image : r26 = `this`, r25 = the 16-step counter, r24 = `&box.mMax`.
// Ours  : r26 = the counter, r25 = `&box.mMax`, r24 = `this`.
// (`box` is r30 and `items` is r28 in both, so only these three move.)
//
// Tap (`c2rs stage cands --regasg`, fn 736): all three are the last popped and
// all read pri 0, so the order is the tie key alone --
//   id19 sym21/k03 tie 77 -> r26 (counter)
//   id23 sym25/k03 tie 26 -> r25 (&box.mMax)
//   id2  sym4/k05  tie  8 -> r24 (this, ref 7, ben 12)
// The image needs `this` popped first, i.e. its key above the other two.
//
// MEASURED: the priority and tie keys are a function of the EMITTED tuple
// stream, not of the source spelling. Rewriting the 16-step bound as a value
// local assigned inside the axis loop (`int steps; ... steps = 16;`) renumbers
// every symbol ordinal in the function (arena#24->#24, sym 21->23 etc.) and
// leaves the three ties at 77 / 26 / 8, byte-identical code, fuzzy unchanged.
// Since the image's instruction stream is also identical modulo registers, its
// keys are the ones we compute -- so nothing that preserves the code can
// reorder these three. That makes the open question a modelling one rather
// than a spelling one: which IL input other than the tuple stream reaches the
// tie key, or which symbol the image's r26/r25/r24 actually hold.
//
// 19 spellings measured this pass, all inert (99.444) or worse:
//   countdown `for (int n = 16; n != 0; n--)` 97.64; `n-- > 0` 94.68;
//   `int steps = 16;` bound (folded, inert); `while (axis < 3)` (inert);
//   `axis++` (inert); `(unsigned char)(axis + 1)` (inert);
//   `const Vector3 &boxMax = box.mMax` 97.41 (adds a kept pointer);
//   the same for `boxMin` 97.41; `current` hoisted above `step` 89.91;
//   dropping the `splitAxis` round-trip 92.73; `float span = ...; step =
//   span * invSteps` 97.41; naming the counter outside the axis loop (inert);
//   `kdTreeNode *self = this` for the mData writes (inert);
//   `const Box &bounds = box` / `Box &bounds = const_cast<...>` (inert);
//   `this->EvaluateSplit(...)` (inert); `bestAxis` selection as braced ifs
//   (inert); `it++` in the count loop (inert).
```


## `?CalculateAOAtPoint@RndAmbientOcclusion@@IBAXABVVector3@@0PAM@Z`

`RndAmbientOcclusion::CalculateAOAtPoint` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1077-1094):

```text
            // RESIDUAL (w7-am, 97.5 canonical): the last real row is that MSVC
            // still lowers this affine rescale as `fadd (x, 1.0)` + `fmul 0.5`
            // rather than the image's single `fmadd x, 0.5, 0.5`, and the extra
            // live `__real@3ff0000000000000` (double 1.0) is what shifts the
            // volatile FPRs f9<->f10 / f10<->f11 across the whole k loop.
            // NEGATIVE RESULT (w7-am, 2026-09-14): four spellings compile to
            // BYTE-IDENTICAL code (97.5, 15 diff_arg / 1 replace / 3 insert
            // each time) -- `val * 0.5 + 0.5`, `0.5 + val * 0.5`,
            // `(val + 1.0) * 0.5`, and assigning the clamp back into
            // shAccum[k] first and rescaling that double in place.  MSVC
            // canonicalises the form before contraction, so the image's fmadd
            // is not reachable from source here; the faithful spelling is kept.
            // w16-a: three more spellings byte-identical at 97.48 --
            // `val / 2.0 + 0.5`, a `const double half = 0.5;` local for both
            // the multiplier and the addend, and `shAccum[k] = val * 0.5;`
            // followed by `shAccum[k] += 0.5;`.  The factoring into
            // (val + 1.0) * 0.5 survives a statement boundary and a named
            // constant, so it is not a front-end rewrite of one expression.
```

Inside the body (was lines 1096-1101):

```text
            // w26-y: naming the product as a double is what lets c2 contract
            // `scaled + 0.5` into the image's single `fmadd f0, f0, f12, f12`
            // (f12 = double 0.5).  Written as one expression, `val * 0.5 + 0.5`
            // is rewritten by c1 to `(val + 1.0) * 0.5` before contraction, which
            // costs a second `lfd` of double 1.0, an extra `fmul`, and shifts the
            // volatile FPRs f9/f10/f11 across the whole k loop.
```


## `?SmoothResults@RndAmbientOcclusion@@IBAXPAVRndMesh@@@Z`

`RndAmbientOcclusion::SmoothResults` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1117-1127):

```text
// w27-h stop note (96.10052 canonical / 94.24637 fuzzy).  The 8 inserts and 8
// deletes pair up one-for-one and every one of them is ORDER, not content: the
// two `addi r3/r4, r31, 0x90` that we issue at 41-42 are the same two the image
// issues at 131-132 (one Normalize argument pair, materialised earlier here);
// the image's `lfs f8, 0x10(r9)` + `fadds` at 127/129 reappear in ours a slot
// earlier; the rest are the fadds of the face centre and the `mulli r, r, 0x60`
// face-stride multiplies sliding one slot against the loads.  Same operands,
// same stores, same calls.  The other 128 diff_arg rows are one large
// nonvolatile-GPR cascade (r23..r27) plus a uniform stack-frame shift; the
// two accessor-CSE facts recorded below are still the upstream cause.  Left at
// its measured number rather than moved with invented code.
```

Inside the body (was lines 1131-1189):

```text
    // Phase 1: Compute AO at each face center
    // SURVEY 2026-09-14 (w7-ae), 87.1% canonical, 209 mismatch rows, no edit made.
    // The residual is NOT arithmetic -- every fadds chain below already matches the
    // image term for term.  Two measured structural facts, both about the accessor
    // calls, are what is left:
    //   (a) the image emits SIX `mulli rX, rIdx, 0x60` for one face (v1,v2,v3 for
    //       .pos and again for .norm, at Geo-relative .L_82694628/4648/4694/46b0/
    //       46d4 and the `mr r9, r22` at .L_826946f8), where MSVC CSEs ours down to
    //       three.  The image also homes each index with `sth rX, 0x50(r31)` --
    //       six dead halfword stores into the SAME slot that also carries the
    //       inlined accessors' `this` (mixed-width slot sharing, see
    //       docs/decomp/patterns/stack-slot-sharing.md).  Caching the Vert
    //       references, as this code does, is what lets MSVC fold them.
    //   (b) our whole frame is shifted: the image's shared home slot is 0x50 and
    //       ours is 0x54, which alone accounts for ~40 diff_arg rows.
    // Both are reachable only by finding the accessor spelling whose address the
    // image hands to an inlined callee; guessing at it (unrolled index temps,
    // per-component Verts() calls) was not attempted here.
    //
    // w29-d (2026-11-02, 96.10048 canonical / 94.246414 fuzzy): the arithmetic was
    // re-adjudicated term by term against the listing and is still identical on
    // both sides -- every fadds/fmuls/fsubs in the dot/angle/weight chains
    // carries the same operands, the braces are the same, and the four colour
    // accumulators are the same four fields.  Body is 418 vs 418, so nothing is
    // being bought by a shortened body.  Class counts: 282 equal / 128 diff_arg
    // / 8 insert / 8 delete / 0 replace, and every one of the 144 is placement:
    //   * r24/r25/r26 and r20..r24 rotate as a block through the whole body
    //     (7 of them are the `lwz r11, 0x148(r25)` reload of the same member).
    //   * f26..f31 rotate the same way (idx 247-257: four `fmr` copies of the
    //     same constant land in f26..f29 in a different order).
    //   * ONE real structural fact, and it is a slot assignment, not a value:
    //     the three Vector3 temporaries are the SAME three objects in a
    //     different ORDER.  image  worldCenter=0xA0, worldNorm=0x70,
    //     aoResult=0xB0;  ours  worldCenter=0xB0, worldNorm=0xA0,
    //     aoResult=0x70.  Both sides keep worldNorm below worldCenter, so the
    //     only disagreement is where `aoResult` sits -- ours allocates it at the
    //     BOTTOM of the three even though it is declared first and its live
    //     range starts at the `faceAO(n, aoResult)` copy above this loop; the
    //     image puts it at the top.  That single rotation is what swaps the
    //     `addi r4/r5/r6, r31, 0x{a0,b0,70}` triples at idx 135-149, and it is
    //     the whole reason Multiply/TransformNormal/CalculateAOAtPoint receive
    //     their outputs in a different order.
    // FIXED in the same pass.  `aoResult`'s first live use is already above
    // worldCenter/worldNorm, so liveness and declaration order AGREE and a decl
    // reorder cannot move it -- which is the tell that the low slot was never
    // aoResult's own.  `std::vector<Hmx::Color> faceAO(n, aoResult)` takes its
    // fill value BY CONST REFERENCE, so MSVC spilled a COPY of aoResult for the
    // call and gave THAT copy the slot it would otherwise have given aoResult;
    // aoResult then inherited the low slot when the copy's live range ended.
    // Passing a value-initialised temporary instead removes the spill: the
    // image's three slots (worldCenter 0xA0, worldNorm 0x70, aoResult 0xB0) now
    // come out exactly, `addi r4/r5/r6, r31, 0x{a0,70,b0}` is equal on all six
    // affected rows (idx 135/139/144/145/146/149) and 8 diff_arg rows cross to
    // equal.  96.10048 -> 96.117226 canonical, 94.246414 -> 94.28708 fuzzy,
    // body 418 vs 418 unchanged.  Also removes a read of an uninitialised
    // Hmx::Color from the fill -- every element is overwritten by
    // `faceAO[f] = aoResult` in the loop below before Phase 3 reads it, so
    // zero-fill and garbage-fill are indistinguishable here, but only one of
    // them is free of undefined behaviour.
```

Inside the body (was lines 1280-1286):

```text
                            // Weight by the angle subtended at this vertex
                            // w22-a13: 826DFC1C..C34 the image sums z*z, then
                            // `fmadds f0, f11(e2.x), f13(e1.x), f0`, then
                            // `fmadds f1, f10(e2.y), f12(e1.y), f0` -- i.e.
                            // ((zz + xx) + yy).  The old flat y,x,z spelling made
                            // native round as ((yy + xx) + zz); the parentheses
                            // pin the image's association on both builds.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?CalculateAO@RndAmbientOcclusion@@QAAXPAM@Z`

`RndAmbientOcclusion::CalculateAO` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1350-1359):

```text
    // RESIDUAL (w7-am, 99.98 canonical): 7 rows remain, all one register
    // naming swap r28<->r29 between the two strength-reduced induction
    // variables of the vertex loop (the 0x60 byte offset into mVerts and the
    // 0x64-stepped progress*100), together with the order in which their three
    // `addi`s are emitted at the bottom of the loop.  The instruction sequence
    // is otherwise identical.
    // w16-a: closed (99.98 -> 100) by advancing progress in the for-increment
    // alongside v (`v++, progress++`): the image steps the vertex offset, then
    // progress, then progress*100.  Same semantics -- the body has no
    // `continue`, so progress still advances once per vertex, after use.
```


## `?Tessellate@RndAmbientOcclusion@@QAAXPAM0@Z`

`RndAmbientOcclusion::Tessellate` -- moved verbatim from `src/system/rndobj/AmbientOcclusion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1405-1481):

```text
// w26-k (2026-11-02): adjudication of the 12.5% residual (4492 B, 94.43
// canonical / 93.35 fuzzy, 1153 instructions).  NOT a behaviour difference:
// objdiff reports diff_op none (no comparison, branch-polarity or test
// mismatch), and the one entry under Function Call Diff is an ICF alias --
// the image's `vector<UILabel*>::erase` and our `vector<RndMesh*>::erase` are
// the same pointer-vector body (build/373307D9/icf_aliases.map carries 84
// UILabel aliases), and this TU cannot instantiate a UILabel erase at all
// (UILabel appears 0 times in AmbientOcclusion.cpp).  Unicorn verdict for this
// symbol is EQUIVALENT at harness h4.  The residual is structural: our frame
// is 0x4D0 against the image's 0x4E0, and 404 of the 1153 rows are r1-relative
// arg diffs riding on that (-16 dominant, 83 rows), with the two home stores
// of `this + 0x98` landing at 0xA0/0x64 (ours) vs 0xE8/0x78 (image), i.e. a
// different set of 16 bytes being kept in the frame rather than the same
// locals shifted by the frame delta.  Remaining: 56 GPR swap pairs, dominated
// by r10/r11 (32) and r10/r9 (22), plus 53 insert/delete rows in 25 clusters
// of which the largest are two 16-byte copy word orders.  Re-deriving the
// frame (which local the image keeps where) is the next step, not another
// spelling of this body.
//
// w27-l (2026-11-02, 94.43366 canonical / 93.34728 fuzzy): w26-k stands --
// re-adjudicated against the listing, still not a behaviour difference.  No
// comparison, immediate, callee or branch polarity differs; every `diff_arg`
// row is a register or an r1-relative offset, and both sides read and write
// the same fields in the same order.  The clearest single statement of the
// mechanism is the cluster at idx 380-395, at `i0/i1/i2`:
//     image: sth r5,0x50(r31) / sth r6,0x50(r31) / sth r4,0x50(r31)
//     ours : (nothing there)
// The image homes each of the three u16 face indices into ONE 2-byte scratch
// slot, 0x50(r31), which it then writes six times (idx 380/386/391/674/676/
// 678) and NEVER reads as a u16 -- a dead home store.  Reproducing it would
// need a statement whose only effect is a store nothing reads, i.e. invented
// code; not landed.  The frame gap is the same story at whole-local scale:
// image 0x4E0 vs ours 0x4D0, and the three `Vector3` results are the same
// objects in a different order (image wp2=0x1F0, wp0=0x200, wp1=0x210; ours
// wp1=0x1E0, wp2=0x1F0, wp0=0x200), which is why 39 u16 user slots ride a
// uniform -0x4 shift.  Register side: 56 GPR swap pairs, r10/r11 (32) and
// r10/r9 (22) dominant, all downstream of where those three Vector3s and the
// two `mObjectsTessellate` iterator homes land.
//
// w29-d (2026-11-02, 94.43366 canonical / 93.34728 fuzzy): w27-l's verdict --
// not a behaviour difference -- is re-confirmed independently, but its
// *label* ("the mechanism is a dead home store") does NOT describe this row
// and should not be inherited.  Measured decomposition of the 1123/1130
// instruction pair (target/base, so our body is 7 LONGER, not shortened):
//     equal 685 | diff_arg 404 | insert 30 | delete 23 | replace 11
// Of the 404 diff_arg rows, 221 are textually identical once register NUMBERS
// are masked -- same opcode, same displacement, same immediate -- i.e. pure
// allocator output.  The other 183 carry a different displacement, and those
// reduce to two placement facts, not to any dead store:
//   (a) the frame itself: image 0x4E0, ours 0x4D0.  Every `r31`-relative slot
//       in 0xC0..0x1E0 rides that, plus a compensatory -0x4 shift on the
//       `sth` user slots (image 0xEE/0xF6/0xFE/..., ours 0xF2/0xFA/0x102/...)
//       and a clean +0x10 on the one 0x10-byte local the image keeps at 0xB4
//       where we keep it at 0xC4.  A 0x10 frame gap and a two-region slot
//       reshuffle is what 183 rows look like; a dead store is what 3 look like.
//   (b) the three Vector3 results are the same objects in a different order
//       (image wp2/wp0/wp1, ours wp1/wp2/wp0) -- already recorded by w27-l.
// The dead home store w27-l found really is there -- `sth r5/r6/r4, 0x50(r31)`
// at idx 380/386/391 and again at 674/676/678, never read as u16 -- but it is
// 3 of the 23 delete rows, 3 instructions out of 1130.  It cannot carry a
// 94.4 row, and reproducing it would still need a store nothing reads.
//
// The one STRUCTURAL difference that is not a dead store, and that w27-l did
// not name, is a self-cancelling pointer re-derivation in both memcpy loops
// (idx 601/604 for Phase 2 and 1010/1013 for Phase 3):
//     image: mulli r28,rN,0x60 ; mr r27,r30        -> src = savedVerts
//     ours : mulli r11,rN,0x60 ; subf r10,r11,r27
//                                add r28,r10,r11   -> src = (p - off) + off
// `subf r10,r11,r27` followed by `add r28,r10,r11` is the identity r28 = r27,
// i.e. c1 built the source pointer as `savedVerts - offset + offset` where the
// image built it as a plain copy.  Two instructions per site, four total, and
// both are `insert` rows.  The loop shape is right (`subic.`/`bne` matches on
// both sides) and the destination side is right (both keep a byte offset and
// re-derive `mVerts` data each iteration), so the handle would have to be in
// how `src` is initialised -- `savedVerts` cannot be mutated here because the
// erase() below still needs the un-advanced pointer, and every non-mutating
// respelling tried writes the same `(p - off) + off` tree.
```

Inside the body (was lines 1605-1617):

```text
                    // w36-a (2026-11-05): read the face at each use site.  The
                    // image emits one `lhz 0x0(r30)` per use of v1 and one
                    // `sth` per Edge field; naming `unsigned short i0/i1/i2`
                    // let c1 keep the value in a register and ELIDE three of
                    // the Edge stores (ours 121 vs the image's 124).  Spelling
                    // the read as FaceVert(face, k) -- i.e. `(&face.v1)[k]`,
                    // the pointer-index form, NOT `face.v1` -- restores them:
                    // 93.34728 -> 94.53161 fuzzy, and the `lhz` and `sth`
                    // multisets both become equal to the image's.  (The member
                    // spelling `face.v1` is byte-inert; the old note's "direct
                    // Verts(FaceVert(face, k)) ... 91.0" no longer reproduces
                    // on this tree.)  Still open: hoisting the midpoint =
                    // 0xffff stores above this lookup regresses to 90.2.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: r31-frame
layout shifted by 0x10. The DistanceSH arguments (c0,n0,c1,n1 and the two rotations) match.
The perimeter sqrt terms match too, but the target squares y with fmuls and fuses x into the
fmadds, and we do the reverse. Only the fusion order differs.
