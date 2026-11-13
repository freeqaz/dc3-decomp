# Lane notes: `src/system/rndobj/Cam.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetViewProjectXfms@RndCam@@QBAXAAVTransform@@AAVMatrix4@Hmx@@@Z`

`RndCam::GetViewProjectXfms` -- moved verbatim from `src/system/rndobj/Cam.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 355-369):

```text
    // COPIES, not `const float &` references.  Rewriting these two as
    // references (so the members are read at point of use, which is what the
    // image's load order at 0x82628C80-A0 superficially suggests -- mYFov
    // 0x2c8, mNearPlane 0x2c0, __real@00000000, __real@3f800000, mFarPlane
    // 0x2c4) REGRESSES 83.8 -> 77.4: both plane loads migrate inside the
    // mYFov branch and drag `stfs f31, 0x3c(r30)` with them.  Measured
    // w7-aj; do not retry.
    //
    // NEGATIVE RESULT (w7-ap, 2026-09-14): hoisting the test into a named
    // `bool ortho = mYFov == 0;` between the two copies (to sink the mFarPlane
    // load past the `fcmpu` the way the image does at 0x82628C88/94, where far
    // reuses f0 because yFov is dead) REGRESSES 83.8 -> 79.7: MSVC materialises
    // the bool as `li r11, 1` / `li r11, 0` / `clrlwi.` / `beq` instead of
    // keeping the result in cr6, and that costs 4 inserts plus the 1.0f
    // constant being re-materialised on both sides of the branch.
```

Inside the body (was lines 414-439):

```text
    // NEGATIVE RESULT (w7-ap, 2026-09-14, 83.80 canonical): the residual here
    // is that /fp:fast folds this `-(x) * 2.0f` into `x * -2.0f`, so we
    // materialise a SECOND pooled constant `__real@c0000000` with its own
    // lis/lfs where the image emits `fneg f12, f9` (0x82628D50) and multiplies
    // by the same `__real@40000000` in f0 (0x82628D4C) that projXNum and all
    // four of the l/b/r/t scalings reuse.  The expression SHAPE is already
    // right -- image order is fmuls(h, m.z.y) -> fneg -> fmuls(2.0), which is
    // exactly `-(h * m.z.y) * 2.0f`.  Splitting the fold apart does not help
    // and costs 2.3pp: both `float projY = h*m.z.y; float projYNum = -projY;
    // projYNum *= 2.0f;` and the two-statement `float projYNum = -(h*m.z.y);
    // projYNum *= 2.0f;` measure 81.5 -- identical to each other, so the
    // regression is the statement split resequencing the surrounding loads,
    // not the constant.  Leave the fused spelling.
    // w7-bt (83.80 unchanged), two more negatives on the same residual:
    //   - `(-mScreenRect.h * m.z.y) * 2.0f` (negate the LEAF, not the product)
    //     does kill the second constant -- the build then has the image's
    //     `fneg` and one shared __real@40000000 -- but lands at 81.54, the
    //     same number as w7-ap's statement splits: the `fneg` sinks to after
    //     the last hiRect word copy and both `* 2.0f` multiplies fall behind
    //     the fsels (image: 0x82628D5C / D6C, before the first fsel at
    //     0x82628D74).  The constant was never the cost; the schedule is.
    //   - reading ONLY mFarPlane at point of use (`const float &farPlane`,
    //     near still a copy) to get the image's post-fcmpu far load
    //     (0x82628C94): 78.7 -- the far load still sinks into both arms and
    //     takes `stfs f31, 0x3c(r30)` with it, exactly as the both-planes
    //     variant did at 77.4.  Which plane is referenced is not the trigger.
```

Inside the body (was lines 457-481):

```text
    // (r - l) and (t - b) are spelled OUT at each division rather than held in
    // `width`/`height` locals.  Xenon MSVC defaults to /fp:fast and
    // strength-reduces two divisions by the same NAMED variable into
    // `1.0f/x` plus multiplies -- with the locals we emitted
    // `fdivs f12, f31, f12` / `fdivs f11, f31, f11` against 1.0 and four
    // fmuls.  The image emits four real fdivs (0x82628DCC, DD0, DD8, DE0) all
    // sharing the two CSE'd subtractions in f11 and f8, which is what an
    // expression divisor gives: the backend still CSEs the subtract, but the
    // reciprocal transform keys on source-level variable identity and does not
    // fire.  See docs/decomp/patterns/fixable-fsel-fma.md.
    // w30-c (2026-11-02) re-measured every inherited negative on this tree and
    // they all still hold: (-h * m.z.y) * 2.0f 81.544, -(h*m.z.y) then *=2.0f
    // 81.544, named product + `-projY * 2.0f` 83.800 (byte-identical), named
    // product + separate negation 81.544, mFarPlane read at point of use
    // 78.680, `const float &` bindings of mScreenRect.{h,w} 83.800 (inert).
    // The opcode+operand multiset now says exactly what is left: the image has
    // ONE MORE `fmr` (f9 <- f11, h kept), ONE MORE `fneg` and ONE MORE
    // `lfs 0x2e0(r31)` (mScreenRect.w reloaded), and we have the extra
    // `lis`+`lfs __real@c0000000` pair (/fp:fast folds -(x)*2 into x*-2).  The
    // fmr and the reload are CONSEQUENCES of the image evaluating h*m.z.y
    // AFTER the cy fmadds and of keeping h and w live across it; our scheduler
    // hoists that product above cy, so no copy or reload is needed.  Arithmetic
    // re-derived from the listing: every value, store, divisor and call agrees
    // (cy, cx, projYNum, projXNum, l/b/r/t, the four fdivs and both w.z forms),
    // so this is form/scheduling, not a behaviour defect.
```
