# Lane notes: `src/system/rndobj/Spline.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/Spline.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 16-20; nearest following definition: `RndSpline::CtrlPoint::CtrlPoint`):

```text
// w15-r: NOTE lbl_8209F850 is in .RDATA, so the shipped variable was const; the
// image's TU .rdata runs [10.0f x3 (0x8209F84C-54), 4.0f, 1/30.0f (0x8209F85C),
// gRev, gAltRev].  Spelled `const float`, MSVC folds the two SyncPristineCtrlPoints
// uses into __real@41200000 instead of loading the named slot the image loads
// (measured), so the mutable static stays until a spelling reproduces both.
```


## `?Load@RndSpline@@UAAXAAVBinStream@@@Z`

`RndSpline::Load` -- moved verbatim from `src/system/rndobj/Spline.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Above the definition (was lines 120-125):

```text
// w15-r: the w9-f RESIDUAL that sat here (97.970, nine rows, "the anchor pick is a
// function-local allocation decision") is CLOSED.  The pick is not
// function-local: MSVC anchors the ASSERT_REVS pair on gRev exactly when gRev is
// at offset 0 of the TU's non-COMDAT .rdata, and on gAltRev otherwise (measured
// over all 248 INIT_REVS sites in the image).  kPulseStep above restores the
// image's .rdata ahead of gRev, and Load is 100.
```


## `?SyncDeformedDummyCtrlPoints@RndSpline@@ABAXHH@Z`

`RndSpline::SyncDeformedDummyCtrlPoints` -- moved verbatim from `src/system/rndobj/Spline.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 251-270):

```text
        // NOTE (matching): the dummy points live in `this`, so every store below
        // may alias the vector's own begin pointer as far as the compiler is
        // concerned. The original re-subscripts `mDeformedCtrlPoints` after each
        // group of stores rather than holding one cached element pointer; the
        // compiler then does the caching within each store-free run. Keeping a
        // pointer alive across the stores loses those reloads.
        // w21-be (95.56 -> 100): both extrapolations are the Vec.h
        // Subtract/Add pair (w7-bt had it for the After groups; the
        // Before group's scalar spelling had flipped all three fadds), and
        // each ROLL copy is stored through a `CtrlPoint &` to the dummy while
        // the position goes through Add's Vector3& (the image's dead `addi
        // r8, r31, 0x94` at 826B28B0 is that reference).  A this-relative
        // roll store lets MSVC hoist the following begin reload above it;
        // through the reference it stays after the store, exactly where the
        // image reloads (826B28F8, and before the mDirtyConstants store in
        // the Before group).  The AfterEnd Subtract re-subscripts the last
        // point after the mDummyAfter stores rather than reusing `last`.
        // Probes: Add into `after.mPos` (the whole CtrlPoint& form) loses the
        // dead addi and is worse (94.7 raw); w7-bt's other probes are moot.
        // Behaviour unchanged: the same values land in the same fields.
```
