# Lane notes: `src/system/gesture/DepthBuffer3D.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?JointToVertexData@?A0xa87adb66@@YAXAAVVector3@@ABVSkeleton@@W4SkeletonJoint@@ABVVector4@@@Z`

`JointToVertexData` -- moved verbatim from `src/system/gesture/DepthBuffer3D.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 34-42):

```text
        // w21-bb: 99.83 -> 100.  The image computes both normalised components
        // first, x before y (bounds loads x-pair first at 0x82DED7F0..), then
        // forms the output.  Writing the two normalised values back into
        // the local gives MSVC the image's x-before-z evaluation order; the old
        // single out.Set(...) expression (and named x/z/dx/dy locals in either
        // order) evaluated the z formula first.  Same arithmetic and
        // association; screenPos is a local, so the stores into it fold away.
        // Set() still writes out last, after every bounds load (out may alias
        // bounds).
```


## `?UpdateAttachment@DepthBuffer3D@@IAAXAAUDepthBuffer3DAttachment@@ABVVector4@@1@Z`

`DepthBuffer3D::UpdateAttachment` -- moved verbatim from `src/system/gesture/DepthBuffer3D.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 142-147):

```text
    // w14-f: 93.47 -> 100.  The image tests skelIdx + 1 and passes
    // (skelIdx + 1) - 1 to GetSkeleton (`addic. r11, r3, 0x1` / `subi r4, r11,
    // 0x1`), and accumulates into newPos in place: newPos is seeded with
    // localXfm.v (its 0x50 slot is the localXfm.v copy) and `+= pos`.  The old
    // NEGATIVE RESULT note (separate localPos/pos locals, Add into newPos) is
    // superseded.  b5 declared after skelNum keeps it out of a callee-saved reg.
```


## `?AddAttachment@DepthBuffer3D@@QAAXABUDepthBuffer3DAttachment@@@Z`

`DepthBuffer3D::AddAttachment` -- moved verbatim from `src/system/gesture/DepthBuffer3D.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 187-193):

```text
    // w21-z: 97.714 -> 100.  The image's `li r10, 0x0` / `cmplwi cr6, r10, 0x0`
    // pair (0x82DF14F8/0x82DF14FC) is the `return nullptr` of an inlined
    // pointer-returning search, as w8-r inferred; what w8-r's helper got wrong
    // was its signature.  Passing begin()/end() BY VALUE (not the vector by
    // reference) keeps begin() live in r11 past the loop for the size()
    // computation and leaves `this`/0x24 in r30/r29, as the image does.  The
    // helper name is ours.
```


## `?Load@DepthBuffer3D@@UAAXAAVBinStream@@@Z`

`DepthBuffer3D::Load` -- moved verbatim from `src/system/gesture/DepthBuffer3D.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Above the definition (was lines 290-330):

```text
// COMDAT-SELECTION LEVER: MEASURED, nothing to fix in this unit (w8-r).
// The mechanism is NOT "add `inline` to make the callee a COMDAT" -- that was
// retracted.  MSVC/Xenon puts every function we compile into its own COMDAT, so
// the map's flag column reports the COMDAT *selection type* in the section
// symbol's aux record at offset 14 (`f i` = IMAGE_COMDAT_SELECT_ANY, bare `f` =
// NODUPLICATES), and the dtk-carved target objects carry no selection byte at
// all, so the map is its only carrier.  Reasoning from where our source puts a
// definition is therefore not evidence; run
// scripts/analysis/comdat_selection_audit.py.  Measured over
// build/373307D9/src/system/gesture/DepthBuffer3D.obj: ZERO selection
// mismatches in either direction.
//
// DrawShowing does carry this lane's one real save-set difference -- the image
// calls __savefpr_18 / __restfpr_18 (f18-f31, 14 FPRs, at 0x82DEF8BC and
// 0x82DF0CEC) where we call __savefpr_17 / __restfpr_17 (f17-f31, 15), i.e. WE
// hold one extra callee-saved FPR, alongside a +0x10 frame delta -- but do NOT
// read that as a lead for the selection lever.  Measured elsewhere: a sibling
// lane's RndSpotlight::BuildBeam is the same textbook symptom (image
// `bl __savegprlr_14`, ours `_16`) and stayed BYTE-IDENTICAL at 85.3415 after
// its same-TU callee's selection class was matched, and all four selection
// closures verified anywhere so far were fidelity-only with zero score
// movement.  The extra FPR is the "double literal masquerades as an FPR floor"
// question instead: we hold one value in a callee-saved FPR that the image
// spills with stfd.
//
// Worth knowing regardless: both funclets fn_82DF0D1C and fn_82DF0D6C are
// DrawShowing's (they immediately follow it and its __unwind$ in
// build/373307D9/asm/system/gesture/DepthBuffer3D.s), and their single charged
// row IS that frame delta -- `subi r31, r12, 0x250` against our 0x260.  So
// closing DrawShowing's frame banks 80 B and 2 functions with it.
//
// w18-c: Load 77.336 -> 100.  The reads are CHAINED `>>` expressions.  The image
// feeds each call's returned reference straight into the next one -- `mr r29, r3`
// after operator>>(BinStream &, Color &) and r29 as the stream for the three
// reads after it; BinStreamRev::operator>>(bool &)'s r3 passed unchanged as the
// next `this`; `lwz r4, 0x8(r3)` (the returned BinStreamRev's .stream) for
// mMesh.  Because the chained calls hand &d to code that returns an opaque
// reference, MSVC can no longer keep d.rev / d.stream in registers, which is
// the "reloaded at every use" storage the w8-r survey saw (0x60/0x68(r1), frame
// 0xa0).  Same reads, same order.  (The lbl_82251220-vs-gRev row is the
// INIT_REVS .rdata pair's target-side name, not a defect.)
```


## `?DrawShowing@DepthBuffer3D@@UAAXXZ`

`DepthBuffer3D::DrawShowing` -- moved verbatim from `src/system/gesture/DepthBuffer3D.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 402-439):

```text
// STOP POINT (w21-f, 80.3 normalized, was 72.54; ~730 rows, 449 of them
// arg-only regalloc).  Levers that landed, each measured on its own:
//   - one `valid` flag cleared in each reset arm (image: li r29,1 / mr r29,r19)
//     instead of three has1/has2/has3 bools                       72.54 -> 74.2
//   - mat / depthTex as if/else assignments, not ternaries          -> 75.4
//   - `s0 ? (s0->SkeletonIndex() + 1) : -1` polarity (beq -> li -1)  -> 76.4
//   - percentile indices via Round() (fctiwz inside each arm)        -> 80.3
// Inert: a ForceValue(float) inline in DoubleExponentialSmoother.h for the
// image's dead `stw &smoother, 0x50(r31)` in the SetParams arm.  Remaining:
// image keeps 60/80 (d38/d44) in memory, not FPRs (we save from f17, image
// __savefpr_18); vector slots p1Cols/rows/depths/p2Cols at 0x70/0x80/0x90/0xb0
// vs ours 0x70/0x90/0xb0/0xc0; mat slot 0x60 vs 0x80; our extra
// `stfs Level, 0x50(r31)` homes in the zoom-clamp section.
//
// w26-j behaviour verdict (2026-11-01), from the name_check listing:
//   - NO wrong field.  objdiff examined 38 offset mismatches and excluded all
//     38 as non-field (35 frame slots, 3 different base register).  There is
//     no `this`-relative displacement that differs.
//   - NO wrong constant.  Every `__real@` that pairs differently is a slot
//     shift (the image's `lfs f21,__real@42a00000` sits one row off ours).
//   - ONE real transcription difference: the image divides with `divwu`
//     (rows 231/232, 267/272) where `int n` gives `divw`, i.e. the original's
//     `n` is a size_type.  w26-j measured the unsigned spelling at 79.30 and
//     reverted it; w29-e re-measured at 83.80 -> 84.216 with the same tree
//     shape, so that 79.30 does not reproduce and the unsigned spelling is
//     LANDED.  It is codegen-only (n is a pixel count, >= 0).
//   - unicorn has no verdict recorded for this symbol, so the listing above
//     is the whole behaviour evidence.
//
// w29-e (80.267 -> 84.877 canonical, 78.355 -> 82.765 fuzzy): three of the
// four levers below are SOURCE SHAPE that the image's own instructions name,
// not register-assignment noise -- the twelve `fsel` (Min/Max), the `divwu`
// (unsigned `n`), and the six address spills in the SetParams arm.  Only then
// is the remainder a register-assignment cascade: c2rs `stage cands` on this TU
// reports 30+ FP candidates with priorities 33..-46 over a ~1300-instruction
// body; the f0<->f13 pair alone is 38 instructions.  Frame is still TGT 0x250
// vs BASE 0x260 (one 16-byte local too many) -- see the SetCamera note in
// SkeletonViz.cpp for the same +0x10 shape.
```

Inside the body (was lines 450-456):

```text
    // w18-c (DrawShowing 72.6 canonical): the image keeps `mat` in a frame slot
    // (stw to 0x60(r31) on both arms, `lwz r22, 0x60(r31)` where it is next
    // needed) and we keep it in r22.  `mMinimalMat ? mMinimalMat.Ptr() :
    // SetUpWorkingMat()` reproduces the image's two-store arm shape but scores
    // 72.1 (the value is still register-held).  Other open items: d38/d44 (60.0f,
    // 80.0f) are loaded into FPRs up front on our side only, and most of the
    // remaining rows are the per-pixel loop's register assignment.
```

Inside the body (was lines 464-470):

```text
    // BEHAVIOUR FIX (w18-c): the depth texture defaults to mPlayerPaletteTex, not
    // to null.  The image loads mPlayerPaletteTex (`lwz r11, 0x198(r26)`), tests
    // it, and on the non-null path does `mr r21, r11` then branches straight to
    // the SetDiffuseTex(r21) / SetVertShaderTex(r21) block at 0x82DF04xx; r21 is
    // the camera's depth stream texture only on the null path (`mr. r21, r3`
    // after GetStreamTex).  We used to hand a null texture to the material and
    // the vertex shader whenever a palette texture was set.
```

Inside the body (was lines 559-565):

```text
                // UNSIGNED, because the image divides with `divwu` (0x82DF0184 and
                // 0x82DF0188) where a signed `n` gives `divw`.  `n` is a pixel
                // count and `size()` is a size_type, so this is also the faithful
                // type.  Worth +0.42 canonical on this row (83.800 -> 84.216).
                // w26-j recorded the opposite result (79.30, measured on a tree
                // where the four vectors still sat at the wrong frame slots); it
                // does NOT reproduce here and the divwu rows are gone.
```

Inside the body (was lines 663-676):

```text
            // The six smoothers are reached through named pointer locals, one per
            // call.  The image emits `addi rN, r26, <offset>` followed by a spill of
            // that address (`stw rN, 0x50(r31)`) for each of the six SetParams
            // sites; writing `unk270.SetParams(...)` directly takes the address as
            // an inline operand and emits neither.  83.800 -> 84.877 canonical.
            // Measured inert/refuted on this block: a shared single pointer reused
            // across the six calls (84.827 -- one address, so five of the six
            // spills disappear), `DoubleExponentialSmoother &` references instead of
            // pointers, a per-call `{ }` scope holding one reference, and hoisting
            // all six initialisers above the calls (all byte-identical to the
            // pointer form).  A file-scope helper taking the smoother by reference
            // measured byte-inert earlier (w21-f).  The stack-layout shift that
            // comes with these locals is benign: both EH funclets keep their
            // `subi r31, r12, 0x250` displacement.
```

Inside the body (was lines 807-812):

```text
    // w18-c: written straight into slotParams, as the image does (stfs to its
    // 0xa0/0xa4 slots inside each arm, with the else arm's y = -1.0f sharing the
    // -999 arm's store), and z/w computed in FLOAT: the image converts modf's
    // double result first (`frsp f0, f1`) and then does `fmsubs f0, f0, f18,
    // f19` -- (float)modf(...) * 2.0f - 1.0f.  We used to multiply the double
    // (`fmsub` against 2.0 / 1.0 double literals) and round once at the end.
```
