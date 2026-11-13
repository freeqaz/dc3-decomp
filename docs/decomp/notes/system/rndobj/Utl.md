# Lane notes: `src/system/rndobj/Utl.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 45-49; nearest following definition: `GroupOwner`):

```text
// w8-a: for ObjPair -- see RndUtlDiscardedInstanceMerge below.  ObjPair is
// MISPLACED in this tree: RB3's decomp has it in obj/Object.h immediately
// before ObjMatchPr, and DC3's obj/Object.h still has ObjMatchPr at that exact
// spot (line 1706).  Moving it there is the real fix; obj/Object.h is
// PCH-reached, so it is a whole-tree rebuild and out of this lane's scope.
```

File-level (was lines 2407-2413; nearest following definition: `RndUtlDiscardedKeyTrim`):

```text
// RndAmbientOcclusion::Edge::operator< is defined INLINE in AmbientOcclusion.h
// (w13-a).  Its out-of-line copy here is the COMDAT the set<Edge> templates
// call; being a COMDAT, MSVC does not propagate its register usage into the
// callers, so _M_find/_M_insert/insert_unique keep the tree pointers in
// callee-saved registers across `bl ??MEdge` exactly as the image does.  The
// old out-of-line definition here let the volatiles survive the call (w8-h's
// 89.6 FLOOR on _M_find).
```


## `?GetDefaultMatShaderOpts@@YA?AUMatShaderOptions@@PBVObject@Hmx@@PAVRndMat@@@Z`

`GetDefaultMatShaderOpts` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 215-224):

```text
            // The NumBones temp is LOAD-BEARING, the HasAOCalc one is not
            // (both measured, w8-m 2026-09-30, 1 dead `mr r10, r9` at idx 33):
            //   named NumBones + inlined HasAOCalc  99.0  <- this
            //   named NumBones + named HasAOCalc    99.0  (inert, uglier)
            //   both inlined                        98.0  (+`clrlwi r11,r11,24`
            //                                       bool mask + r10<->r8 swap)
            //   both named ABOVE the two setters    98.0  (whole arm reshuffles)
            // The residual `mr r10, r9` is a dead copy of the HasAOCalc byte
            // into a register nothing reads (idx 35 uses r9 on BOTH sides); it
            // is regalloc, not the temp -- removing the temp does not move it.
```


## `?BadUV@@YA_NAAVVector2@@@Z`

`BadUV` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 380-386):

```text
// `inline` is load-bearing for the one caller, ComputeFaceTangentBasis: an
// out-of-line same-TU BadUV lets MSVC keep the caller's vertex pointers in
// volatile r7-r9 across the three calls (it knows this body touches only
// r3/r10/r11), where the image saves them in r29-r31.  The image's compiler
// had no usable register summary for BadUV, which is what an inline (pick-any
// COMDAT) definition gives.  The body is still emitted out of line and matches
// 100.0 (46 rows) either way.  w7-bs, 2026-09-15.
```


## `?TransformKeys@@YAXPAVRndTransAnim@@ABVTransform@@@Z`

`TransformKeys` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 741-749):

```text
// w17-a (94.911 canonical, left as is): the ScaleKeys loop below is NOT the
// image's shape -- written like the other two (`it != tanim->ScaleKeys().end()`,
// no hoisted _tmp3) rows 46-78 become 100% equal (begin then end off the owner
// pointer the TransKeys loop already loaded, no reload of 0x58(r29)).  It still
// scores LOWER (94.6) because the remaining diff is the inlined quaternion
// Multiply (rows 79-108: the image evaluates Set()'s arguments w,z,y,x and stores
// 0xc,0x8,0x4,0x0; we interleave and store y first) and the realigned rows weigh
// more there.  Refuted at the call site: a temp Quat result copied back (81.9).
// Fix the quat block first, then restore the plain ScaleKeys loop.
```

Added by lane w-low-rndobj, 2026-11-13:

```text
BEHAVIOUR VERDICT: AGREES (94.91 / 90.80, unchanged).  m3c rows are tf.m rows
times 1/scale (x,y,z each row); q58 = Quat(m3c).  The three loops walk the
KEYS OWNER's (0x58) mTransKeys 0x34 (Multiply by tf), mScaleKeys 0x40
(component-wise times the extracted scale) and mRotKeys 0x28 (q58 * key, all
four components checked against the fmadds/fnmsubs chain).  Residual is the
scale-loop end hoist and quat store order already recorded above.
```


## `?XfmSort@@YA_NAAUInstance@RndMultiMesh@@0@Z`

`XfmSort` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 836-854):

```text
// w26-e (was 99.931 canonical / 97.69 fuzzy): the written term order is a
// SCHEDULING handle, not arithmetic.  Both sides emit an fmuls + two fmadds
// chain for every spelling tried -- /fp:fast re-associates on its own -- so
// what the order selects is the sequence in which the six independent `lfs`
// reach sched3/sched0, and with it their registers.
// A 144-variant sweep ({all 6 term orders} x {flat, grouped} on each side)
// found exactly two spellings that reproduce the image's r4-side load sequence
// (0x34 x, then 0x3c z, then 0x38 y): mesh1 flat y,z,x and mesh2 grouped
// y + (x + z).  Runner-up is 21/29 instruction-text rows (the old spelling).
// TRADE, stated plainly: this spelling matches the load sequence and reaches
// 100.0 canonical, but its mesh1 accumulation groups as x + (y + z) where the
// image has y + (x + z) -- same three squares, same fast-math contract, a
// different rounding order.  The old spelling had the image's grouping and
// instead carried an OFFSET_SWAP of (0x38,0x3c); no spelling in the sweep gave
// both.  Residual now: the f9/f10 pair only.  The image's sched3 defines
// mesh2.z before mesh1.y, ours the other way, so regasg's first-fit (c2rs
// section 8.2: pure first-fit over the sched3 def order, sched0 runs after)
// hands them the opposite registers while the emitted load order is already
// identical.
```


## `?UtilDrawCigar@@YAXABVTransform@@QBM1ABVColor@Hmx@@H@Z`

`UtilDrawCigar` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1042-1115):

```text
    // Two behavioural bugs fixed here, both visible in retail's stores:
    //  1. The cap apex sits on the LOCAL X AXIS, not Y.  Retail writes the
    //     computed value to 0x60(r1) -- offset 0 of the temp vector -- and zeroes
    //     0x64/0x68, i.e. Set(value, 0, 0).  That is the same axis the ring
    //     vertices use (v1/v2 take the axial coordinate as their x), so putting
    //     it in y put both caps off the cigar's axis.
    //  2. Retail transforms through a SEPARATE temp (in = 0x60, out = 0x90 /
    //     0xa0); we were transforming in place.
    // Retail's frame is 0x3d0 and ours WAS 0x3e0: retail coalesces the int->float
    // conversion scratch double into the dead `scaledLens` slot (0x50, accessed
    // again at the (float)i conversions), while MSVC gave us a fresh 0x90 and
    // pushed top/bottom/basis/both vertex arrays up by 0x10.  Hoisting `end` out
    // of a nested block recovered 0.1pp of that; swapping the declaration order of
    // `end` and `scaledLens` to give scaledLens the lower slot was byte-identical
    // (measured 2026-09-14, two consecutive neutral variants).
    //
    // w7-bo (2026-09-15): THE 0x10 IS CLOSED, and declaration order was never the
    // lever -- LIFETIME was.  `scaledLens` and the do-loop that fills it now live in
    // their own block, with `sLen0`/`sLen1` declared outside it, so the array is dead
    // the moment the block ends and MSVC reuses 0x50 for the conversion double exactly
    // as retail does.  Frame 0x3e0 -> 0x3d0 (prologue `stwu r1, -0x3d0(r1)` now equal),
    // all 38 +/-0x10 offset rows closed, canonical 91.94 -> 92.31193, mismatch rows
    // 97 -> 72.  See docs/decomp/patterns/lexical-scope-controls-msvc-stack-slots.
    // w7-bo (2026-09-15) measured negatives on the two remaining small clusters,
    // both BYTE-IDENTICAL (canonical 92.31193 unchanged, same 72 rows):
    //  - swapping the declaration order of `top` and `bottom` to chase the 0x90/0xa0
    //    permutation (retail puts `top` at 0x90; we get 0xa0).  MSVC assigns these two
    //    same-sized Vector3 temps by use, not by declaration order.
    //  - writing the three `sin * radii[]` products as `radii[] * sin` to chase
    //    idx 82/88/50 (retail `fmuls f27,f1,f0`, we emit `fmuls f28,f13,f0`; retail
    //    `fadds f0,f24,f0`, we emit `fadds f0,f0,f24`).  MSVC canonicalises fmuls/fadds
    //    operand order from its register assignment, not from the source order, so the
    //    readable order stays.
    // w7-bs (2026-09-15): the fmadds row (retail `fmuls f0,f1,f0` + `fadds
    // f27,f0,f24` at 0x8262D384/D38C) is CLOSED by H2 below -- see the note above
    // the inner loop.  Two further byte-identical negatives (95.3 canonical, same
    // 54 rows both times): constructing `end` with its value (`Vector3 end(sLen0 -
    // radii[0], 0, 0)`) instead of Set(), and declaring `v2` before `v1` in the
    // inner loop.  What is left on this function after H1/H2 is register and
    // slot assignment only: `top`/`bottom` at 0x90/0xa0 vs our 0xa0/0x90 (rows
    // 33-50, which also drags the sLen0 reload and the `fadds f0,f24,f0` operand
    // order), `v1`/`v2` at 0x70/0x80 vs our 0x80/0x70 (rows 107-127), h0 in f27
    // vs our f28 with the matching `fmr` placement (rows 79-93), TheRnd's base in
    // r30 vs our r28 (canonical-forgiven), and the `li` order in the ring-index
    // loop (rows 153-161).  Both slot pairs are same-sized Vector3 temps that
    // MSVC assigns by use, not declaration order, and the documented pinned-region
    // slot order is still unresolved (docs/decomp/patterns/stack-slot-sharing.md).
    // w16-a (95.25 -> 98.02): the scaledLens fill is a PLAIN `for (n < 2)`
    // loop -- MSVC itself strength-reduces lengths[n] off the dst pointer,
    // which is the odd `lengths + (dst - scaledLens)` address the old
    // hand-stepped do/while spelled out.  Left: the top/bottom (0x90/0xa0) and
    // v1/v2 (0x80/0x70) slot pairs and the h0 `fmr` placement listed above.
    // Passing v1/v2 as unnamed Vector3 temporaries to Multiply is inert.
    // w24-slot (98.02, unchanged): both pairs run AGAINST the slot rule's
    // last-reference tie-break.  top and bottom have equal counts and bottom's
    // last reference comes first, yet the image puts top lower; the same holds
    // for v1/v2.  A standalone cl.exe skeleton shows the int->float conversion
    // temps (`tv`, 8 bytes, liveness-shared) reorder same-size Vector3 slots, and
    // even break the size-ascending order.  Remove the conversions and the
    // skeleton follows the rule again.  So this layout depends on the conversion
    // temps, not on v1/v2/top/bottom alone.  Measured inert: `Vector3 v2;
    // v2.Set(...)` instead of the ctor (98.0, same 9 permuted slots).
    //
    // w27-c (2026-11-02) measured 98.01835 / 97.21560 and 46 rows, and pinned
    // the remainder down with a register-and-branch-insensitive multiset
    // comparison: the target has 218 instructions and we have 219, and the
    // ONLY unmatched entry in either direction is one extra `fmr` on our side
    // (idx 76/78, the `fmr f0,f1` / `fmr f1,f28` pair that the image places
    // around the `fmuls f27,f1,f0` instead).  Everything else is the two
    // same-sized Vector3 slot pairs (top/bottom at 0x90/0xa0, v1/v2 at
    // 0x70/0x80), the h0-in-f27-vs-f28 colouring, the TheRnd base in r30 vs
    // r28, and three commutative operand-order rows at idx 47/85/89.  So no
    // load, store, call or constant differs -- the body is behaviour-
    // equivalent and the whole remainder is the slot allocation above.
```

Inside the body (was lines 1131-1144):

```text
    // w7-bs (2026-09-15), H1: the index is spelled `iIdx * 6 + iLon` and the outer
    // loop is bounded by `iIdx < 3`.  MSVC strength-reduces `iIdx * 6` into its own
    // induction variable (retail's r28, stepping by 6, LFTR test `cmpwi r28,0x12`
    // at 0x8262D410) but does NOT strength-reduce the derived-of-derived `idx`, so
    // `add r10,r28,r31` / `slwi r29,r10,4` (0x8262D3D0/D3DC) are recomputed per
    // iteration exactly as retail does.  The earlier `iLatSum` source-level IV was
    // what let MSVC fuse both loops into one byte cursor (`addi r30,r30,0x10` /
    // `cmpwi r30,0x120`) -- an IV that already IS the sum has nothing left to
    // reduce, so it becomes the cursor.  92.31193 -> 95.1 canonical, 72 -> 53 rows.
    // H2: the apex offsets `sLen0 - h0` / `sLen1 + h1` are written INSIDE the
    // inner loop (in the Vector3 ctor call).  LICM hoists them to the outer loop
    // as stand-alone `fsubs`/`fadds` (retail 0x8262D38C `fadds f27,f0,f24`),
    // which is why retail has no fmadds there; a separate `h1 = h1raw + sLen1`
    // statement in the outer loop is contracted under /fp:fast.  95.1 -> 95.3.
```


## `?UtilDrawPlane@@YAXABVPlane@@ABVVector3@@ABVColor@Hmx@@HM_N@Z`

`UtilDrawPlane` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1224-1246):

```text
    // STOP (w21-bi) at 99.98 normalized, 6 rows (0x8262F624..0x8262F648):
    // the inlined Dot(mb0[idx], m.y) below. The image's fma chain is
    // ((z*z') + x*x') + y*y' off the row pointer biased to .z; ours is
    // ((y*y') + z*z') + x*x' off the same pointer. Probed (standalone cl.exe,
    // all inert at 6 rows): Dot argument order, a named float for the dot,
    // block-scoped idx / ++idx, (&mb0.x)[idx] and a const Matrix3 view instead
    // of operator[], a const Matrix3 ctor instead of Identity(), Transform
    // declared before mb0, a const& cast on m.y. All 24 explicit
    // order/association/operand spellings of the sum get the image's chain at
    // best by moving the row-pointer bias to .x and swapping a Cross operand
    // (157 vs 156 equal rows) -- not shipped.
    // OPEN (w25-mc), mechanism read from the c2rs tap (C2RS-BRIDGE 8.7):
    // the three products are sorted by key and the list's second entry is
    // emitted innermost. Each product is (row-load temp) * (m.y field), and
    // its key's hash is 64 * (temp sid + field sid) mod 0x10000, so the order
    // is that of (t + s) mod 1024: x 729+26 = 755, y 727+274 = 1001,
    // z 746+273 = 1019 (the m.y y/z fields are renamed copies, x keeps its
    // field symbol -- the same pattern as MakeNormals' e1/e2). Sorted
    // [z, y, x] gives our y-innermost chain; the image needs [x, z, y]. A
    // uniform shift of the row-load temp sids by +23..+267 wraps y and z past
    // 1024 but not x and gives exactly that order. Open question: a spelling
    // that mints that many more lowering temps ahead of this loop without
    // changing the code around it.
```

Inside the body (was lines 1259-1267):

```text
        // w21-bi: the quad's four corners are ONE array local. The image
        // gives them 0x90/0xa0/0xb0/0xc0 -- tf88's own slots, tf88 being dead
        // in memory once its fields are hoisted into f23-f31 -- and four
        // separate Vector3 locals never share that slot (frame 0x190 vs the
        // image's 0x150, the whole +0x40 delta the w7-x/w7-bx notes chased).
        // Measured in a standalone cl.exe probe and in the build: `Vector3
        // pts[4]` packs onto tf88 exactly. `-scalar` written at each call
        // instead of a named `negscalar` local gives the image's
        // `fmadds f0, f13(-s), f25(m.x), f26(v)` operand order (lever p).
```

Inside the body (was lines 1274-1278):

```text
        // BUG FIX (w7-bx): the sixth parameter is forwarded to every DrawLine
        // -- `mr r28, r8` in the prologue (0x8262F550) and `mr r7, r28` at
        // each call (0x8262F70C, 0x8262F7AC, 0x8262F7D0, 0x8262F7F4); a
        // literal false dropped it and cost the callee-saved register
        // (93.39474 -> 99.7).
```


## `?CacheResource@@YAPBDPBDAAW4CacheResourceResult@@@Z`

`CacheResource` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 1358-1459):

```text
// NEGATIVE RESULT (2026-08-22, name_check, objdiff 4.2.8) -- do not retry blindly.
//
// The image differs from this function in two known ways and NEITHER is reachable by
// restructuring the source:
//
//  1. Block placement.  The image sinks the whole MovieExtension arm to the end of the
//     function (bne past the main body to 0x228) where we emit it inline right after
//     the stricmp pair.  Rewriting this as
//         if (stricmp(ext,"bmp") == 0 || stricmp(ext,"png") == 0) { ...main...; return ret; }
//         else { ...movie... }
//     does NOT move it: MSVC still emitted the movie arm inline and idx 28 stayed
//     bne-vs-beq.  Measured 71.537 -> 67.9, a 3.6pp REGRESSION, and reverted.
//
//  2. Destructor merging.  The image destroys qualifiedPath ONCE (target names
//     ??1String@@UAA@XZ once, we name it twice), holding the return value in r29 across
//     the single exit.  Hoisting that into a `const char *ret = cacheFile;` with one
//     `return ret;` was part of the same reverted experiment and added instructions
//     rather than merging the two destructor calls -- objdiff classifies this as
//     DEAD_STORE_ELIMINATION / destructor-merging, RarelyHandFixable.
//
//     RE-MEASURED 2026-08-31 WITHOUT THE CONFOUND.  The note above described one
//     experiment that changed BOTH things at once, so the destructor-merge had never
//     actually been measured on its own.  It has been now: the `ret` hoist ALONE, with
//     the block placement left exactly as it is, moves 71.537 -> 67.9 -- the same 3.6pp
//     regression the combined experiment produced.
//
// w7-bo (2026-09-15) -- BOTH DISMISSALS ABOVE ARE NOW SUPERSEDED, with the NEW mechanism
// they asked for: the rb3 sibling (../rb3/src/system/rndobj/Utl.cpp:1117), which neither
// earlier pass consulted.
//
//  2'. THE DESTRUCTOR MERGE IS OBTAINABLE, and the lever is not a `ret` hoist -- it is
//      DUPLICATING `return cacheFile;` INSIDE the String's scope (rb3 does exactly this).
//      Target 8262E6C4..8262E6D8:
//          ble .L_8262E6DC   <- cacheRes <= 0
//          li  r29, 0x0      <- cacheRes  > 0: return value := nullptr
//        .L_8262E6CC:
//          addi r3, r31, 0x60
//          bl   ??1String@@UAA@XZ   <- the ONE destructor, on both paths
//          mr   r3, r29
//      .L_8262E6DC is a bare `b .L_8262E6CC`, the tail-merge seam of the second return.
//      With the duplicated return we now emit idx 155-158 EXACTLY, ??1String goes 2 -> 1,
//      equal instructions 123 -> 127 of 185, mismatch rows 62 -> 58, and base size
//      656 -> 648 == target size.  The `ret`-hoist spelling could never do this because
//      it moves the store OUT of the scope instead of moving the return INTO it.
//      ⚠ Canonical went 71.55556 -> 71.49383, a 0.062pp LOSS, and that is an aligner
//      artifact, not a regression in the code: diff_score is 4643/16200 on both sides
//      (normalized/fuzzy 71.33951 unchanged).  Closing the destructor turned three
//      partially-credited `replace` rows inside the STILL-MISPLACED movie block into two
//      fully-charged `delete` rows.  Kept on faithful-over-score: the emitted bytes are
//      strictly closer to the image and the size is now exact.  When 1 is closed this
//      becomes a strict win.
//
//  1'. THE ARM INVERSION IS INERT, not a 3.6pp regression.  The 2026-08-22 experiment
//      changed the arm order AND hoisted `ret`; the 2026-08-31 re-measure de-confounded
//      only the hoist.  Measured on its own, on top of 2', rewriting the head as
//          if (stricmp(ext,"bmp") == 0 || stricmp(ext,"png") == 0) { ...main...; return; }
//          const char *movieExt = MovieExtension(ext, thisPlatform);   // trailing, no else
//      -- i.e. the movie arm LAST IN SOURCE ORDER rather than in an `else` -- produces
//      BYTE-IDENTICAL code: same 58 mismatch rows, same canonical 71.49383, idx 28 still
//      `beq` vs the image's `bne`.  MSVC normalises the two arms to "smaller arm first"
//      and no source ordering of them reaches it.  The whole 3.6pp belonged to the hoist.
//      44 of the 58 residual rows are this one block placement (idx 29-51 inserted,
//      idx 160-182 deleted); it is the only thing left between this function and ~100.
//
// w7-bs (2026-09-15) -- three more spellings of the head, each BYTE-IDENTICAL to the
// current one (58 mismatch rows, canonical 71.49383, idx 28 `beq` vs the image's `bne`
// at 0x8262E528, movie block still inline at idx 29-51 instead of at 0x8262E6E0-0x8262E734
// where the image's unknown-extension arm `li r11,1; li r3,0; stw r11,0(r26)` FALLS INTO
// the shared epilogue at 0x8262E738):
//   (a) full arm inversion `if (bmp==0 || png==0) { ...main... } else { ...movie... }`;
//   (b) the Block-Placement Lever 2 goto split
//       `if (stricmp(ext,"bmp")==0) goto cached; if (stricmp(ext,"png")!=0) goto movie;`
//       with the movie block spelled last in the function;
//   (c) Lever 1, the condition materialised into a named int
//       `int isMovie = !!(stricmp(ext,"bmp") != 0 && stricmp(ext,"png") != 0);`.
// Together with w7-bo's trailing-arm spelling that is four source orders of the two
// arms and both documented placement levers, all folding to one layout.  The home-slot
// permutation (image: movieExt 0x54 / base 0x50 / path 0x58 in the movie arm, ext 0x58 /
// base 0x50 / path 0x54 in the main arm; ours 0x58/0x54/0x50 and 0x50/0x54/0x58) is
// allocated in block order and moves with it, so it is the same single residual, not a
// second one.  FLOOR 71.49383 canonical until a placement heuristic is found that ranks
// a two-return arm as the out-of-line one; nothing in docs/decomp/patterns/
// fixable-control-flow.md "Block Placement" reaches it.
//
// The census WRONG_CALLEE charge here (target MovieExtension vs base ~String) is a
// consequence of 1: both sides call MovieExtension exactly once, at different points in
// the function, so the aligner pairs our ~String against it.  It is not a wrong callee.
// (With 2' landed the charge is gone from the pattern list anyway -- our ~String no
// longer sits where the aligner would pair it against MovieExtension.)
//
// w21-c (2026-10-03): still 71.49383 canonical / 71.33951 fuzzy, the same single
// residual (movie arm inline at idx 29-51 vs the image's 0x8262E6E0 tail block).
// Five more spellings measured, none moves the block:
//   - strieq() for the two tests, `!a && !b` and `!(a || b)`: byte-identical;
//   - main arm first with duplicated returns + trailing movie arm, and the lever-2
//     goto split (`bmp==0 goto cached; png!=0 goto movie;`, scoped blocks, movie
//     last): byte-identical, so MSVC is reordering these blocks itself;
//   - a single `ret` local assigned on every path, one `return ret;`: 67.30 (worse);
//   - `const char *ret = nullptr` inside the movie arm: byte-identical;
//   - `if (!movieExt) { res = ...; return nullptr; }` first: 72.11, but only because
//     it flips the INNER arm order away from the image (image: beq to the unknown
//     arm at the very end), so it is an aligner artifact, not kept.
```


## `?ComputeFaceTangentBasis@@YAXPAVRndMesh@@HAAVMatrix3@Hmx@@@Z`

`ComputeFaceTangentBasis` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1789-1817):

```text
    // 100.0 canonical / 100.0 raw, 230 of 230 rows (w7-bs, 2026-09-15; was 94.13
    // with 114 rows).  Four levers, in the order they were found -- none of them
    // was the "scheduling" the w7-bo residual note blamed:
    //   1. BadUV() is `inline`.  Our caller kept the three vertex pointers in
    //      r7/r8/r9 ACROSS the three `bl BadUV` because MSVC uses a same-TU
    //      callee's register summary; the image holds them in r29/r30/r31
    //      (0x8262CE7C / 0x8262CE90 / 0x8262CE94), i.e. its compiler did not
    //      trust BadUV's summary, which is what a pick-any COMDAT (inline)
    //      callee gets.  That relabelled every GPR downstream: 100 -> 70 arg rows.
    //   2. The transpose is a nine-argument `edgeMat.Set(x.x, y.x, z.x, ...)`
    //      (RB3's inline Transpose(Matrix3) body): six loads then six stores in
    //      exactly the image's order (0x8262D08C-D0E8).  Three swap temps read
    //      and wrote 0x84/0x98 and 0x90/0xa4 in the other order: 4 offset rows.
    //   3. edge21/edge31 come from `Subtract(vert2.pos, vert1.pos, edge21)`, and
    //      the zero tests read the vectors; six scalar diffs feeding a Vector3
    //      constructor gave a different FPR assignment for the whole block
    //      (0x8262CED8-CF54, 29 arg rows + one insert/delete).
    //   4. `outBasis.Identity()`: the image threads `mulli r11, r31, 6` and the
    //      `add` through the nine identity stores (0x8262CE14 / 0x8262CE30);
    //      nine explicit component stores hoisted both above them.  This is the
    //      ninth callee-saved register too: with Identity() the Matrix3 copy is
    //      scheduled loads-first (0x8262D008-D084) and takes r23.
    // Inert, measured on the way: `m->Faces(faceIdx)` for `Faces()[faceIdx]`;
    // `edgeMat.Set(edge21, edge31, faceNormal)` for the three-vector ctor;
    // faceNormal as three component assignments (fmsubs order is FPR-driven,
    // as w7-bo found); `Cross(edge21, edge31, faceNormal)` for the hand-written
    // cross product (kept: same three fmsubs, and it cannot re-flip the y sign).
    // Declaring `face` AFTER the identity fill costs 0.6pp (w7-aq, 2026-09-14),
    // and sinking edge21/edge31 into the innermost block costs 10.5pp.
```

Inside the body (was lines 1882-1889):

```text
            // Only a BadUV() failure notifies.  The four degenerate-edge /
            // degenerate-UV tests above exit silently with outBasis still the
            // identity: every one of their four `bne` rows targets the epilogue
            // at 0x8262D128, and the notify block at 0x8262D0F4 is reached only
            // from the three BadUV() `bne` at 0x8262CEB4 / 0x8262CEC4 / 0x8262CED4.
            // Until 2026-09-15 (w7-bs) this notify sat after the nested ifs, so a
            // zero-area face or a zero UV delta printed "has bad UVs" -- a
            // decompilation-introduced message the image never emits.
```


## `?MakeNormals@@YAXPAVRndMesh@@@Z`

`MakeNormals` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1961-1972):

```text
                            // w25-mc: built in place rather than through
                            // Scale(crossProd, angle, weighted). Same three
                            // products, but fewer symbols ahead of the Add()
                            // below, which fixes its x row ([265]) to the
                            // image's norm.x + weighted.x. c2 orders those two
                            // operands by key (c2rs W-STAGETAP-11): weighted.x
                            // is a variable (0x10000 | sid<<5), norm.x a memory
                            // operand whose key carries its base variable's
                            // sid mod 8 in bits 13-15. With Scale() the norm
                            // base was sid 592 (0 mod 8, key 0x10008, below
                            // weighted.x 0x149e0); built in place it is 564
                            // (4 mod 8, key 0x18008, above weighted.x 0x14600).
```

Inside the body (was lines 1976-2003):

```text
                            // OPEN (w25-mc, 8 rows), two mechanisms:
                            //  * [222]/[223], the crossProd fmuls/fmsubs operand
                            //    order. c2's commutative key (C2RS-BRIDGE 8.7)
                            //    puts the higher-sid variable first. At cqil1
                            //    every product reads the field symbols in
                            //    declaration order (e1 21/23/25, e2 27/29/31),
                            //    but by cqlo0 the y and z fields have been
                            //    renamed to fresh symbols (e1.z 315, e1.y 576,
                            //    e2.z 578, e2.y 579) while both x fields keep
                            //    theirs. So e2.x (27) sorts below e1.z and e1.y
                            //    and those two products come out e1-first; the
                            //    image has e2 first in all six. Open question:
                            //    which pass renames y/z and spares x (offset 0,
                            //    the struct's own symbol?), and what spelling
                            //    makes e2.x a renamed symbol too.
                            //  * [249]/[250] + [267]..[270], the y/z halves of
                            //    the Add(). Operand order is right on all three
                            //    rows; the image just runs the y chain (crossProd.y
                            //    load, scale, add, store 0x14) before z, and we
                            //    run z first. regasg hands out FPRs first-fit in
                            //    sched3 order (RT 271/272), so this is the
                            //    scheduler's y/z tie, not a key.
                            // Measured inert earlier: Add(weighted, norm, norm)
                            // (w7-m); expanding Add by hand through a Vector3&
                            // collapses the function (w9-f, 94.5). w25-mc also
                            // measured Cross(e1, e2, ...), Subtract() for e1/e2,
                            // IsZero()/operator== for the tests (fixes [265] but
                            // breaks the acos dot) and four Add spellings.
```


## `?ResetNormals@@YAXPAVRndMesh@@@Z`

`ResetNormals` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 2019-2046):

```text
// 52-row residual (99.03636 canonical / 98.4808 fuzzy) is four mechanisms:
//   * 39 rows of FPR permutation, dominated by f0<->f12 (13) and f0<->f13 (10);
//   * 7 offset swaps, (0x14,0x18) x2 -- the y/z component pairs of the two
//     Vector3 temps;
//   * 4 commutative operand-order rows (fmadds at idx 80, fmuls/fmsubs at
//     297/298, fadds at 368/373). The cqlo3 dump shows their operands as
//     folded single-use loads, i.e. M6{0102xxxx} subexpression-class, whose
//     key is the defining tuple's own (SU<<24|nodes<<16|hash) -- so they are
//     not keyed on a base at all and C2RS-BRIDGE 8.8's base rebinding has no
//     lever arm here;
//   * 10 rows of the image's two-address multiply (`mr r9,r7 / mulli r9,r9,0x60`
//     where we emit `mulli r9,r7,0x60` directly).
// w26-q measured ~45 variants on this row. The 8.8 base rebinding is worse,
// measured exactly (report.json's own entry point, post-compile patchers
// applied): binding `RndMesh::Vert &vert = m->Verts()[i]` for `vert.norm`
// and `&vert.tangent` reads 90.52525 canonical / 88.66666 fuzzy against this
// source's 99.03636 / 98.48080 -- the Vert reference pins a different live
// range and the split of the 0x60 stride disappears. Only the pTangent-half
// binding is shape-inert (99.0364) and it still costs 0.02 fuzzy. Naming the six d1/d2 components, or the
// six cross-product products, as float locals takes them from the
// subexpression class to variable leaves and REGRESSES to 97.58/98.23 --
// which confirms the class change happens, just not toward the image's order.
// Naming the acos dot as `double`, splitting the handedness `w` into `wDot`,
// binding `angle` through `const float &`, and rewriting the three
// `pTangent->k += ft.k` statements as `ft.k + pTangent->k` are all inert.
// Open handle: which pass renames the y/z field symbols in the cross-product
// loads (see the w25-mc note inside the loop) -- that, not a base, is what
// would move these keys.
```

Inside the body (was lines 2095-2146):

```text
                // 54-row residual, 99.032326 canonical (lane w7-j, 2026-09-14).
                // The image is 1980 B against our 1972: the shipped build spends two
                // extra instructions turning the index multiplies into the two-address
                // form, `mr r9, r7 / mulli r9, r9, 0x60` and `mr r7, r4 / mulli
                // r7, r7, 0x60`, where we emit `mulli r9, r7, 0x60` directly.  The
                // rest is one scheduling inversion and its register cascade: we
                // compute &v2 and v2.pos.z (rows 246/249) BEFORE &v1 and v1.pos.z,
                // so d2's components land before d1's and d1 is stored x,y,z where
                // the image stores x,z,y (d2 is stored x,z,y on both sides already).
                // 39 of the 54 rows are the resulting FPR permutation, dominated by
                // f0<->f12 x13 and f0<->f13 x10.
                //
                // REFUTED (lane w7-j, both measured):
                //   1. Naming the three vertex indices --
                //        int i0 = face[k % 3]; ... m->Verts()[i0]
                //      -- REGRESSES to 97.4%.  It does not produce the image's
                //      two-address multiplies; it gives the three ints their own
                //      stack slots and perturbs the whole surrounding schedule.
                // w21-bf (still 99.032326, 54 rows) -- all measured, none kept:
                //   * the angle dot: flat (y,z,x), Dot(d1,d2), Dot(d2,d1) give one
                //     schedule; explicit parens y+(x+z), y+(z+x), (z+x)+y, x+(z+y),
                //     z+(x+y) never give the image's z-then-x inner pair: MSVC always
                //     emits the inner pair's LOWER-offset component as the fmuls.
                //   * `pTangent->x = ft.x + pTangent->x` (image adds ft first): inert.
                //   * the handedness sign as Cross(basis.z, basis.x, cross) + Dot or
                //     explicit parens: 99.0 with 4-5 more rows; swapping the
                //     `crossZ * basis.y.x` operands: inert.
                //   2. Sinking the `v2` declaration below d1's construction, so that
                //      d1 is fully built before v2's address is formed, is exactly
                //      SCORE-NEUTRAL: 54 rows -> 53 rows but canonical 99.032326
                //      before and after, to all six digits.  Kept the symmetric
                //      three-declarations-together spelling.
                // w27-l (2026-11-02, 99.03636 canonical / 98.48080 fuzzy, 496
                // rows): the two-instruction gap is the IMAGE's two-address
                // multiply, and the three `Verts()` spellings measured this
                // round do not produce it.  Re-read from the listing: the image
                // emits `mr r9, r7 / mulli r9, r9, 0x60` and `mr r7, r4 /
                // mulli r7, r7, 0x60` at idx 229-237 where we emit
                // `mulli r9, r7, 0x60` / `mulli r7, r4, 0x60`; both compute the
                // same byte offset (the index register is dead on both sides at
                // the point of the copy, so this is not a liveness difference --
                // it is c2 choosing the dst==src form of the scale).  Nothing
                // observable follows: the remaining 50 rows are the documented
                // schedule inversion, its f0/f12/f13 cascade and two `lfs`
                // offset swaps, and objdiff reports diff_op none.
                // MEASURED, all keep 1972 B and change nothing: `m->Verts(i)`
                // instead of `m->Verts()[i]` for one, two or all three of
                // v0/v1/v2 (99.03636 / 98.48080 every combination -- the
                // accessor inlines to the same tree).  `face[k]` for
                // `face[k % 3]` REGRESSES to 93.06263 / 91.92121, so the
                // redundant `% 3` is load-bearing for the surrounding schedule;
                // do not "simplify" it.
```

Inside the body (was lines 2172-2177):

```text
                // w22-a31: the image sums the angle dot as (z + x) + y --
                // `fmuls z*z'; fmadds x*x' + that; fmadds y*y' + that` -- so the
                // explicit parens give native (no FMA, no reassociation) the
                // image's association; the old flat x+y+z summed in another
                // order.  Xbox: 99.032326 -> 99.03636 (MSVC still fuses the
                // inner pair the other way round, see w21-bf above).
```


## `?ConvertBonesToTranses@@YAXPAVObjectDir@@_N@Z`

`ConvertBonesToTranses` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 2222-2227):

```text
                // w24-pch (99.454 -> 100): a raw ObjRef* ring walk.  With
                // ObjRef::iterator (w8-m) MSVC computed `it->Refs().end()`
                // eagerly into volatile r9 and left that copy dead (one
                // `addi r9, r10, 0x8`); re-spellings of the iterator loop were
                // inert or worse (rotated shape 90.7, swapped && 91.6).  The
                // `!foundBoneRef &&` test stays FIRST.
```


## `?SetBloomBlurWeights@@YAX_NMM@Z`

`SetBloomBlurWeights` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 2265-2284):

```text
// CONST, and both arrays live in .rdata -- established from the ORIGINAL's own
// linker map, not inferred: build/373307D9/default.map attributes
//   lbl_8208B100  .rdata  size 0x3C  <- sBloomWeights
//   lbl_8208B13C  .rdata  size 0x3C  <- sBloomOffsets
// to Utl.obj, the same TU, and config/373307D9/symbols.txt records both as
// `.rdata:...; // type:object size:0x3C`.  They were `static float` here, i.e.
// emitted to .data, which is a faithfulness defect independent of score.
// Adding `const` is MEASURED SCORE-NEUTRAL (88.66129 canonical, same 16 rows,
// w8-m 2026-09-30); it is landed for the section, not the number.
//
// w16-a: CLOSED, 88.66129 -> 100.  The two tables are FUNCTION-LOCAL
// `static const` arrays.  As file-scope statics MSVC knew their relative
// layout and folded the offsets table into `addi r11, r27, 0x3c` off the
// weights base (the w8-m residual: one hoisted base instead of the image's two,
// __savegprlr_26 instead of _25, the 8-byte frame delta and the idx 8-25
// shuffle).  Function-local statics are separate symbols to it, so it hoists
// both bases (r26/r27) exactly as the image does.  The pointer-local and
// early-weight-read attacks w8-m and w9-f recorded here were inert or worse
// because they kept the file-scope pair.  The loop is also the plain
// `for (i < 15)` form now (byte-identical to the hand-stepped one).
```


## `RndUtlDiscardedKeyTrim()`

`RndUtlDiscardedKeyTrim` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: no function named `RndUtlDiscardedKeyTrim` in report.json (unit); filed under the source name

Above the definition (was lines 2418-2435):

```text
// w8-a: ORPHAN INSTANTIATION -- reconstruction of a never-called external
// function that /OPT:REF discarded.  ham_xbox_r.map attributes these COMDATs to
// rndobj:Utl.obj, but nothing that survives in
// build/373307D9/asm/system/rndobj/Utl.s calls any of them:
//
//   ?_M_erase@vector<Key<vector<Color> > >   0x82638C20   (no in-unit caller)
//   ?_M_erase@vector<Key<vector<Vector3> > > 0x82638DC8   (no in-unit caller)
//   ?_M_erase@vector<Key<vector<Vector2> > > 0x82638E38   (no in-unit caller)
//   ?_M_erase@vector<Key<RndMatAnim::TexPtr> > 0x826381D0 (no in-unit caller)
//   ?_M_erase@vector<RndMesh::Face>(pos, __false_type) 0x82630938 -- note the
//        SINGLE-element overload (mangled `PAV34@ABU__false_type@2@`), not the
//        range overload our TessellateMesh already emits
//   ??$?0H@vector<int>(int, int, alloc)      0x82631178   (no in-unit caller)
//
// The name of the original function is not recoverable from the binary.  The
// erase()/Remove() calls below reproduce the exact instantiation set; the
// bodies are deliberately inert.  A `static` stand-in does NOT work -- MSVC
// drops an unreferenced static function before instantiating through it.
```


## `RndUtlDiscardedKeyAppend()`

`RndUtlDiscardedKeyAppend` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: no function named `RndUtlDiscardedKeyAppend` in report.json (unit); filed under the source name

Above the definition (was lines 2448-2455):

```text
// w8-a: the same discarded function also appended one key range onto another
// from a CONST source.  ConvertBonesToTranses' own appends go through
// _M_range_insert<Key<T>*> (non-const, 0x82638A10/0x82638A38), which lowers to
// _M_range_insert_realloc<Key<T>*>; the image additionally holds
// _M_range_insert_realloc<const Key<T>*> at 0x82633348 (Vector3) and
// 0x826334C0 (Quat).  Those two are ICF-folded with the non-const bodies -- the
// retail map lists both names at each address -- so only a const-iterator
// source range produces the spelling dtk carved the unit under.
```


## `RndUtlDiscardedInstanceMerge()`

`RndUtlDiscardedInstanceMerge` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: no function named `RndUtlDiscardedInstanceMerge` in report.json (unit); filed under the source name

Above the definition (was lines 2466-2473):

```text
// w8-a: the std::list<ObjPair> COMDATs are the same orphan class.  The retail
// map attributes four of the six to rndobj:Utl.obj (insert 0x826309A0,
// _M_create_node 0x8262EF48, StlNodeAlloc<_List_node<ObjPair> >::allocate
// 0x8262C4E0 and ::deallocate 0x8262C550) and the other two to
// world:Instance.obj, yet nothing in rndobj/Utl.s mentions ObjPair outside
// those four and their RTTI descriptor at 0x82F13ADC.  The surviving user of
// the type is WorldInstance (src/system/world/Instance.cpp:318-360), which is
// where the shape below comes from.
```


## `?RndScaleObject@@YAXPAVObject@Hmx@@MM@Z`

`RndScaleObject` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 2614-2630):

```text
        // /fp:fast folds every division by fovScale into a single reciprocal
        // (`fdivs f0, f0, f30` from __real@3f800000 at 0x82630018), so the ONLY
        // thing the target's instruction order can still tell us is the
        // association. It says: speed is (x / fovScale) * scale (fmuls by the
        // reciprocal first, by scale second), and the force-dir factor is
        // (1/fovScale * 1/fovScale) * scale -- fmuls f12,f0,f0 then fmuls f13,f12,f31
        // at 0x82630064 / 0x82630088 -- not (scale/fovScale)/fovScale, which would
        // multiply by scale first.  Spelling the reciprocal as a named local,
        // as `X * invFov`, or as `X / fovScale` is the same code (w7-bx).
        //
        // w7-bx (2026-09-15, 94.06421 -> 97.333336): the whole branch is one
        // basic block, so MSVC's list scheduler decides the emitted order and
        // the source order is only its tie-break.  Swept all 24 orders of the
        // four Vector2 setters: Life LAST is what the image wants (its Life.y
        // store 0x15c at 0x82630080 sits below EmitRate's 0x19c at 0x82630070),
        // 95.83 with the tail below vs 93.9 for Life third; BubbleSize and
        // BubblePeriod are interchangeable.
```

Inside the body (was lines 2637-2645):

```text
        // w22-a16: the image computes ONE reciprocal (`fdivs f0, f0, f30` at
        // 0x82630018) and multiplies by it everywhere -- EmitRate
        // (`fmuls f13, f6, f0` / `fmuls f7, f7, f0`), the force-dir factor
        // (`fmuls f12, f0, f0` then `fmuls f13, f12, f31`) and Speed
        // (`fmuls f13, f13, f0` then `fmuls f13, f13, f31`).  `/ fovScale`
        // compiled the same on Xbox only because /fp:fast rewrites it; the
        // native build has no /fp:fast and divided for real, rounding
        // differently.  Naming the reciprocal is byte-identical here and gives
        // native the image's arithmetic.
```

Inside the body (was lines 2653-2770):

```text
        // Retail coalesces box2 into vb's dead stack slot (0x50) and gives box1
        // its own (0x70); declaring box2 first does NOT reproduce that (measured
        // 2026-09-14, byte-identical diff), so the readable order stays.
        // w7-bj (2026-09-14, 94.06): the image also sinks the eight box stw's
        // (0x160-0x17c) below the Speed/StartSize/DeltaSize float work and
        // multiplies StartSize/DeltaSize scale-first. Refuted: SetBoxExtent
        // called after SetDeltaSize (93.36, 82 diff_arg rows vs 56); spelling
        // `scale * X` on BubbleSize/BubblePeriod/StartSize/DeltaSize
        // (byte-identical -- MSVC canonicalises the commutative fmuls).
        // w7-bx: the image loads all four StartSize/DeltaSize floats
        // (0x1a0-0x1ac, 0x82630134-0x8263014C) and Speed's reciprocal products
        // (0x82630120/28) BEFORE any of the tail's stores, then stores
        // StartSize, Speed, DeltaSize (0x82630164-0x82630188).  Only the
        // named-local spelling below reproduces that grouping: with the
        // products inline in the setter calls every one of the 24 orders of
        // {SetBoxExtent, SetSpeed, SetStartSize, SetDeltaSize} interleaves
        // load/mul/store per setter (best 95.83, SetBoxExtent/DeltaSize/Speed/
        // StartSize).  SetBoxExtent must stay FIRST: any later position drops
        // to 90.5-92.1 because the copy's source pointers (`addi r10, r1, 0x70`
        // at 0x82630048, `addi r9, r1, 0x50` at 0x826300C0) stop being hoisted.
        // RESIDUAL (97.33): our eight stw's still land before the StartSize
        // store, the image's after DeltaSize.x (0x82630184-0x826301A4), and
        // ~25 f11/f13-class register-colouring rows in the top block.
        // w26-y: the four "offset" rows the diff reports at idx 592-610
        // (0x160-0x16c vs 0x170-0x17c) are NOT a wrong member -- both sides
        // store box1 to 0x160..0x16c and box2 to 0x170..0x17c, and the row
        // alignment slips only because the image sinks the box1 group below
        // the StartSize stores.  Likewise idx 581/583 (`lfs ... 0x1a0` vs
        // `0x1a4`) is which register holds StartSize.x, not which field.
        // The six COMMUTATIVE_OP_ORDER rows are `fmuls f31, f12` vs
        // `fmuls f12, f31`: the image puts the `scale` param first and the
        // StartSize/DeltaSize/Speed memory leaf second, i.e. the memory leaf's
        // §8.7 key must fall BELOW the param's 0x10000|sid<<5 (scale is sid 2).
        // §8.8's base rebinding raises a memory leaf's key, so it moves this
        // the wrong way.  Also
        // inert: `Vector2` temporaries instead of the four floats (+1 equal
        // row, same canonical), Scale(vb, k, vb) for the `vb *=`, and moving
        // the box Scale() calls above the vb work (90.3).  `*=` on the members
        // through a friend declaration is refuted outright (89.8): it emits an
        // `addi r11, r3, <member>` per Vector2 that the image lacks.
        //
        // w27-c (2026-11-02): the 48 residual rows are a PERMUTATION, proven
        // rather than assumed.  With every register name replaced by a
        // placeholder and every branch target masked, the two instruction
        // multisets are IDENTICAL -- 732 instructions, 257 distinct, zero
        // unmatched entries on either side.  So the whole of rows 480-613 is
        // the same computation in a different order and different registers:
        // there is no wrong member, no missing call and no wrong constant left
        // in this branch.  The six commutative rows cannot be flipped from the
        // source for the reason above (the leaf's §8.7 key has to fall below
        // the param's 0x10040 and nothing in the expression language lowers
        // it), and the box1/box2 store sinking is the list scheduler's, since
        // SetBoxExtent is already first in the source and moving it later was
        // measured at 90.5-92.1 by w7-bx.
        //
        // w31-a (2026-11-03) strengthened that to the two checks a masked
        // instruction multiset cannot make.  Over each side's own byte range
        // (2928 B / 732 instructions on BOTH) the mnemonics are identical AND
        // the compare/branch multiset is identical -- cmplw, cmpw, cmplwi,
        // fcmpu and every branch form at equal multiplicity, so no condition
        // is evaluated a different number of times.  The RELOCATION-SYMBOL
        // multiset is 109 on both sides and IDENTICAL, so no constant, global
        // or callee differs either.  What is left is placement: the list
        // scheduler's store sinking plus the §8.7 commutative key on the six
        // `fmuls f31, f12` / `fmuls f12, f31` rows.
        //
        // w47-x (2026-11-09): seven source levers measured against the two
        // surviving clusters (idx 499-538, 581-612); every one refuted.
        // Baseline for each A/B: 739 instructions, 691 equal / 25 diff_arg /
        // 9 replace / 7 insert / 7 delete, canonical 97.333336,
        // fuzzy 97.13525.
        //  - SetBoxExtent moved to the very end (after SetDeltaSize), the
        //    Scale() calls left first: 91.8 canonical, 759 instructions,
        //    623 equal.  The call's SOURCE POSITION is what hoists the two
        //    `addi r10/r9, r1, 0x70/0x50` pointer materialisations, so there
        //    is no "just put the box store where the image puts its stw's".
        //    (w7-bx measured 90.5-92.1 at this lever with the inline
        //    spelling; the named-local spelling below is the same 91.8.)
        //  - SetBoxExtent between SetStartSize and SetSpeed: 91.6, 760
        //    instructions, 627 equal.  No intermediate position exists.
        //  - The four head Vector2s restructured into named locals
        //    (bsX/bsY, bpX/bpY, erX/erY, lifeX/lifeY -- the tail's spelling
        //    applied to the head): canonical 97.333336 UNCHANGED, fuzzy
        //    97.12158, 690 equal, the SAME 25 rows at 499-538 plus one new
        //    register diff_arg at 541.  This is the strongest evidence the
        //    head is not a spelling problem: two structurally different
        //    spellings emit the same sequence for the whole of 499-538.
        //  - `scale * partsys->StartSize().x` on startX/startY/deltaX/deltaY:
        //    byte-identical (739/25/9/7/7, canonical 97.333336).  Confirms
        //    w7-bj and closes the last "flip the commutative operands from the
        //    source" route -- MSVC canonicalises the tree, so the six
        //    `f31,X` vs `X,f31` rows belong to the FPR allocator, not us.
        //  - `float invFov` hoisted above SetBubbleSize: 688 equal, 51
        //    mismatched, and the COMMUTATIVE count RISES 6 -> 9, because it
        //    flips `fmuls X, f31` to `fmuls f31, X` on the BubbleSize /
        //    BubblePeriod / Life rows, which the image wants value-first.
        //    So declaration position does steer operand order -- but in the
        //    wrong direction everywhere except StartSize/DeltaSize, and no
        //    single position buys both.
        //  - `float invFov` between SetBubbleSize and SetBubblePeriod:
        //    689 equal, commutative 8.  The current position (between
        //    SetBubblePeriod and SetEmitRate) is the best of the three probed.
        //  - deltaX/deltaY scoped after SetSpeed: 95.1 canonical, 749
        //    instructions, 685 equal -- but the commutative count falls
        //    6 -> 2, i.e. that one move fixes exactly the four rows the image
        //    wants param-first (589/591/593/595) and costs ~34 elsewhere by
        //    displacing the box stores.  The two clusters are coupled through
        //    one allocator state; nothing found pays for both.
        // Mechanism, named rather than waved at: the head's divergence is
        // base HOISTING `lfs f3, 0x158` (Life.x) to 499 and SINKING its store
        // to 534 -- a 35-slot live range the image never has (image: load
        // 511, mul 513, store 515) -- which is also why base needs f3/f4/f5
        // here while the image stays inside f7-f13.  The tail's is base
        // emitting the eight box `stw`'s at 592-606 (ASAP after the stack
        // reloads at 577-590) where the image emits them at 602-610, after
        // all six float stores.  Both are list-scheduling/colouring choices;
        // the instruction, compare/branch and relocation multisets are equal
        // (w27-c, w31-a), so nothing else is left to change from the source.
```


## `?BurnXfm@@YAXPAVRndMesh@@_N@Z`

`BurnXfm` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 2908-2933):

```text
    // Transpose order is inert here: retail saves the three transposed-away
    // components as y.z, x.z, x.y (lfs f0,0x68 / f13,0x58 / f12,0x54 at
    // 0x8262E17C) and reversing the three swaps to match moves three offset
    // rows around without changing the 46-row total (measured 2026-09-14), so
    // this keeps the same spelling ComputeFaceTangentBasis uses.
    // w17-a: an in-place `normalMat.Set(x.x, y.x, z.x, x.y, ...)` (rb3's
    // Transpose(Matrix3, Matrix3) shape) makes rows 21-33 match the image
    // EXACTLY, but the loop's inlined Multiply(Vector3, Matrix3) then schedules
    // its loads differently (4 insert / 4 delete) and the function drops
    // 99.887 -> 95.1.  Reverted; the loop is where the remaining lever is.
    // w26-g canonical decomposition: report.json charges 19 units of 16800
    // (99.8869), and all 19 are IMMEDIATE diffs -- one point each, all of them
    // stack-slot NUMBERING, not a field or a constant: 4 in the transpose
    // (rows 21/24/31/33) and 15 inside the two inlined
    // Multiply(Vector3, Matrix3, Vector3) bodies (51/61 and 73-98), where both
    // sides load/store the SAME multiset of slots in a different order. The
    // instruction multiset, every call and every frame slot is identical, so
    // this is c2 scheduling inside the inlined body and nothing else. All 46
    // register/operand-order rows the fuzzy diff prints are forgiven by the
    // canonical ruler (a register diff costs 5 and is folded into
    // `arg_diff_score`). Refuted this lane, all measured with the c2rs
    // scratch-object loop: single shared transpose temp (96.12, 169 insns),
    // three-temp order yz/xz/xy (97.71) and xy/yz/xz (97.68) -- order changes
    // WHICH four transpose rows are charged, never how many -- and the three
    // Set(...) spellings (in-place 92.99, saved-temps 92.96, per-column 89.38,
    // all 172 insns instead of 168), which lose the loop outright.
```


## `?TessellateMesh@@YAXPAVRndMesh@@@Z`

`TessellateMesh` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 3003-3086):

```text
    // Residual 95.46% canonical / 94.89% raw, 67 rows of 298.  ROOT CAUSE is a
    // FOUR-BYTE SHIFT of the whole small-temp block, and it is an STL temp, not
    // anything declared here.  Read the target prologue:
    //   82637AB4  lbz  r10, 0x50(r31)      <- reads an UNINITIALISED byte
    //   82637B0C  stb  r10, 0xa4(r31)      <- into the std::set's comparator
    // i.e. `std::set<Edge> edges;` copy-constructs its stateless less<Edge>
    // from a garbage stack temp.  The image parks that temp at 0x50, SHARING
    // the slot MSVC also uses as the dead home slot (0x50 takes four dead
    // `stw r8` and three dead `sth` of face.v1/v2/v3 that are never read back);
    // we give it its own slot at 0x54.  Everything after it shifts by 4:
    //   image   e23 @ 0x54, e12 @ 0x5c, e31 @ 0x64   (6-byte Edge, 8-byte pitch)
    //   ours    e12 @ 0x58, e31 @ 0x60, e23 @ 0x68   (+ a 4th midpoint slot 0x6c)
    // The Face locals inherit the same shaping: f4 @ 0xa8 / f2 @ 0xc0 in the
    // image, f2 @ 0xa8 / f4 @ 0xc0 in ours, with 0xd0 <-> 0xd8 likewise.
    // The big objects all agree already (newFaces @ 0x70, newVerts @ 0x80,
    // edges @ 0x90).
    //
    // REFUTED, both byte-inert (identical 67 rows, identical 95.5/94.9):
    //   - reordering this declaration to `Edge e23, e31, e12;`
    //   - moving the declaration INSIDE the loop body
    // MSVC does not take slot order for these from declaration order or from
    // scope, so there is no spelling of THIS function that moves them; the
    // allocation is decided by the STL temp above.
    //
    // w7-bo (2026-09-15) re-measured: 95.5 canonical / 94.9 raw, 298 rows,
    // 231 equal / 54 diff_arg / 9 delete / 4 insert -- unchanged, and regions
    // 6-62, 100-116, 125-135, 151-159, 175-183, 195-208, 227-239, 241-259 and
    // 265-297 are all already 100%.  One observation the note above does not
    // carry, and the best remaining lead for whoever takes this next:
    //
    //   THE DEAD-HOME-STORE COUNT DIFFERS, 4 vs 2.  At the top of the loop the
    //   image loads the sub-object at 0x148(r27) ONCE and then homes it FOUR
    //   times into the shared slot:
    //     82637BB4  stw r8, 0x50(r31)
    //     82637BBC  stw r8, 0x50(r31)
    //     82637BC0  stw r8, 0x50(r31)
    //     82637BC8  stw r8, 0x50(r31)
    //   We emit exactly TWO (idx 69 and 74) -- the loop condition's
    //   `mesh->Faces().size()` and the `mesh->Faces()[i]` subscript.  Under
    //   the repeated-call-expression rule (docs/decomp/patterns, "a call
    //   written twice is CSE'd but still homes `this` once per occurrence"),
    //   four homes means the image's source names that same inlined accessor
    //   FOUR times in this block where ours names it twice.  The image also
    //   carries three dead `sth` of face.v1/v2/v3 into the same 0x50
    //   (82637BF4/BF8/C00) plus four `mr` register copies (82637BD0/D8/E0/E4)
    //   that we do not emit -- the same family.  Finding the two missing
    //   mentions is what would make MSVC coalesce the comparator temp onto
    //   0x50 and unwind the whole 4-byte shift; guessing extra mentions in
    //   order to manufacture stores was deliberately NOT done here.
    //   (r27+0x148 is not plain `mesh->Faces()`: the image reads 0x110 off it
    //   as a byte OFFSET added to r30 for the face cursor AND 0x100 off it as
    //   the verts base for the *0x60 index math, so the accessor being homed
    //   covers both of those uses.)
    //
    // w7-bs (2026-09-15): the two missing mentions were the vertex lookups.
    // `&mesh->Verts(face.vN)` (or `Verts()[face.vN]`, byte-identical) instead
    // of the hand-written `face.vN * 0x60 + vertsBase` gives the image's FOUR
    // `stw r8, 0x50` homes and its two `mr` copies of the mVerts base --
    // 95.46 -> 95.9 canonical, 67 -> 66 rows, and the HX_NATIVE fork goes away.
    // What is STILL missing is the family of three dead `sth` of v2/v3/v1 into
    // 0x50 (82637BF4/BF8/C00) and their `mr` copies; the same three dead
    // `sth` sit in RndAmbientOcclusion::Tessellate (826E14A0/B4/C8) and six of
    // them in RndAmbientOcclusion::SmoothResults (826DF7D0-E8, one per
    // `mesh->Verts(face.vN)` mention there), and our builds of BOTH of those
    // lack them too, while MakeNormals' `m->Verts()[face[k]]` has none in the
    // image either.  So a 16-bit temp is homed once per `Verts(face.vN)` mention
    // in the original and never in ours.  REFUTED as the cause: a by-value
    // `unsigned short` parameter (a TU-local `static inline Vert &At(RndMesh *,
    // unsigned short)` wrapper is byte-identical to `Verts(int)`), pointer vs
    // reference binding, `Verts(int)` vs `Verts()[]`.  Until that temp is found
    // the comparator byte stays at 0x54 (image 0x50, row 5) and the 4-byte
    // shift of the small-temp block stays with it -- the remaining 66 rows are
    // all that shift plus its register renames.
    //
    // w27-c (2026-11-02) measured the residual at 62 rows / 96.61224 and
    // confirmed the shape with a register-and-branch-insensitive multiset
    // comparison: every unmatched entry on either side is that 4-byte stack
    // shift (`lbz R,0x50` vs 0x54, `sth R,0x58/0x5c/0x60` vs 0x5c/0x60/0x64,
    // ...) plus the mention-count family above -- the target has 294
    // instructions and we have 291, and the three extra are one more
    // `lwz`/`mr` pair off 0x100 of the sub-object and one more 16-bit home.
    // Nothing in the loop reads a different member or uses a different
    // constant, so the body is behaviour-equivalent as written and the whole
    // remainder hangs on the comparator temp above sharing 0x50.
```


## `?BuildVisit@@YAXPAVBSPNode@@@Z`

`BuildVisit` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 3186-3247):

```text
    // Retail's prologue reserves one more callee-saved GPR than ours
    // (__savegprlr_23 vs __savegprlr_24), which is why every register in the
    // body reads one number off, and it emits eleven dead home stores of
    // &m.x/&m.y/&m.z into the first local-temp slot (0x60(r31), the slot
    // `origin` and insert()'s returned iterator also share) that we do not.
    // Binding `Vector3 &axisX/&axisY/&axisZ` to the three rows and using those
    // throughout does NOT reproduce either: it materialises &m.x at
    // `addi r26,r29,0x14` right after the node load, where retail computes it
    // only at the first Cross, and costs 2.4pp (95.0 -> 92.6, measured
    // 2026-09-14).  Member access through the iterator stays.
    //
    // w7-bo (2026-09-15): the CAUSAL ORDER of those two symptoms, which the note
    // above had backwards.  The home stores are not a by-product of the extra
    // register -- they are what CREATES it.  All eleven land on 0x60(r31) and they
    // are the three reference parameters of the two inlined Cross() calls plus the
    // Set()/operator= ahead of them, homed once each:
    //     40 &m.z   41 &m.y   42 &m.y      (m.z = plane; m.y.Set(0,1,0))
    //     82 &m.y                          (m.y.Set(1,0,0), inside the |dot| > 0.9 arm)
    //     88 &m.z   98 &m.x   99 &m.y      (Cross(m.y, m.z, m.x))
    //    117 &m.y  119 &m.x  121 &m.z      (Cross(m.z, m.x, m.y))
    // Because &m.x is stored at 98 and again at 119 -- across the intervening
    // `bl Normalize` -- retail must keep it in a CALLEE-SAVED register (r26,
    // materialised at idx 86 `addi r26,r28,0x14`).  That is the ninth callee-saved
    // GPR, hence __savegprlr_23 against our __savegprlr_24, and every register in
    // the body then reads one number off: 103 of the 121 residual rows are that
    // single relabelling, and 13 more are the home stores themselves.  We already
    // compute the same address at the same instruction index (idx 86
    // `addi r4,r30,0x14`) -- into a VOLATILE register, because nothing in our
    // source keeps it live past the call.
    // Measured negative (2026-09-15): binding ONLY `Vector3 &axisX` immediately
    // before the first Cross -- the surgical form the 2026-09-14 experiment did not
    // try -- does not force the callee-saved either.  Prologue stayed r24-r31,
    // canonical stayed 94.97, and mismatch rows went 121 -> 123.
    // So the open question is narrow and concrete: what source spelling makes MSVC
    // home an inlined Cross()'s reference parameters?  Nothing tried so far does.
    // w7-bs (2026-09-15): nor does routing every one of those eleven mentions
    // through the inlined `Matrix3::operator[]` (`m[2] = plane`, `m[1].Set(..)`,
    // `Cross(m[1], m[2], m[0])`, `Cross(m[2], m[0], m[1])`) -- byte-identical,
    // 121 rows, 94.97.  The homes elsewhere in this wave that DID answer to a
    // spelling were all return values of an inlined accessor
    // (`mGeomOwner->` in TessellateMesh/UpdateGeometryBuffers), so an extra
    // inlined layer per mention is not by itself what creates them.
    // w16-a: `(*lastIt).mTransform` (operator* returning a reference) instead
    // of `lastIt->` for all nine mentions here is byte-identical (94.97, 121
    // rows) -- operator-> vs operator* is not what homes them either.
    // w27-m (2026-11-02): the census row (STRUCTURAL:opcode, deficit
    // lwz:1/stw:10) is exactly these dead home stores and the extra
    // callee-saved GPR -- confirmed, not a new structural difference.  Retested
    // the remaining spelling, `Transform &xfm = lastIt->mTransform;` bound
    // once after the iterator and used for all 18 mentions: it does NOT home
    // the Cross() reference parameters either, and costs 0.6pp
    // (94.97 -> 94.4), so the inlined-Cross homing question stays open.
    // w43-a (2026-11-06): the sentence above -- "nothing tried so far does" -- is
    // now answered.  MSVC DOES home an inlined callee's reference arguments, and
    // the lever is WHERE the binding is written: a `Vector3 &` introduced at the
    // use site is materialised into a callee-saved register and homed, while the
    // same binding placed at the top of the block is folded into the enclosing
    // expression (see the refused spellings above, all of which hoisted).  The
    // image homes &m.y/&m.z twice -- once for the assignment/Set ahead of the
    // first cross product and again for the two Cross calls -- so the binding is
    // repeated per site rather than hoisted.  94.97321 -> 96.29762 canonical,
    // 1300 -> 1320 B against the image's 1344.
```


## `?BuildFromBSP@@YAXPAVRndMesh@@@Z`

`BuildFromBSP` -- moved verbatim from `src/system/rndobj/Utl.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 3426-3434):

```text
            // vertIdx++ folded into the index (w25-rc): a separate `vertIdx++;`
            // statement gave faceIdx colour priority 4 against mesh's 4, and
            // faceIdx's later tie key (131 > 15) took r26.  Folded, mesh is 5
            // and faceIdx 3, so mesh gets r26 / faceIdx r25 as in the image.
            // vertIdx * 0x60 spelled here, not hoisted to a `vertOffset` local:
            // MSVC strength-reduces it into an induction variable whose seed
            // `mulli r29, r30, 0x60` lands in the LOOP PREHEADER, after the
            // zero-trip guard (target idx 90).  A precomputed local puts the
            // multiply before the guard instead.
```

Inside the body (was lines 3442-3449):

```text
        // w16-a (97.81 -> 100 canonical, modulo register permutation): the
        // fan is a PLAIN loop over v calling Face::Set.  The hand-stepped
        // do/while it replaces (byte-offset facePtr, separate v1/v2 counters,
        // `faceIdx += triCount` up front) is what kept the w7-aq residual
        // alive -- with the plain loop MSVC itself strength-reduces it to the
        // image's CTR loop, hoists `clrlwi firstVert`, seeds v-1 with the biased
        // `addis r10, r11, 0x1 / subi r10, r10, 0x1`, and the size() load-order
        // rows close too.
```
