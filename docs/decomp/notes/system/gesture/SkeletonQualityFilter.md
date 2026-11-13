# Lane notes: `src/system/gesture/SkeletonQualityFilter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateIsSideways@SkeletonQualityFilter@@AAAXPBUTrackedJoint@@@Z`

`SkeletonQualityFilter::UpdateIsSideways` -- moved verbatim from `src/system/gesture/SkeletonQualityFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 87-115):

```text
// w8-l: 99.914894 normalized, the only function short of 100% in this unit
// (10/11).  27 charged rows out of 94, dominated by one callee-saved pair:
// r28 and r29 are exchanged for the whole body (13 of 24 register rows), which
// drags f0/f13, f30/f13 and f29/f12 along with it, plus 4 slot swaps including
// (0x4, 0xec).  A single liveness/scheduling cause upstream of all of them;
// measured only in this wave, no source lever tried.
// w21-as (still 99.95744 canonical, same rows): a standalone cl.exe probe reproduces
// this listing exactly once Normalize is the Vec.h inline (an extern
// Normalize makes MSVC reload joint[2] after the call).  Probe-inert, all
// keeping this <-> r28 / joint <-> r29 where the image has joint in r28:
// threshold as fabsf(Dot(vDiff, Vector3(0, 0, 1))) (either arg order, or a
// named axis) -- that spelling does reproduce the image's (x + y) * 0 + z;
// `mSideways = threshold > thresh` and if/else forms; the og-dc3 Dot(vDiff,
// vDiff2) final test (both comparison orders); named refs to joint[8]/[4]/[2]
// (all 6 declaration orders, as TrackedJoint& or Vector3&); the cutoff as a
// local, `cutoff * (mSideways ? 0.9f : 1.0f)`, `if (mSideways) thresh *=
// 0.9f`, and `SkeletonQualityFilter *self = this;`.
// w26-c: the this/joint swap is a COLOR priority decision, not a tie.  Tap:
// `this` (sym 2, k05) pri 14 tie 35 -> r28, `joint` (sym 3, k05) pri 22 tie 34
// -> r29; the image has r29 = this, so it needs pri(this) >= pri(joint), and
// the tie key already favours this (35 > 34).  Priority is
// refs_in_block x local pressure (C2RS-BRIDGE 3), so the lever is one more
// reference to `this` (or one fewer to joint) in a block the other does not
// reach; the last block (`mSideways = true`) is the only such block and the
// gap is 8.  Measured inert at 71/94 this wave: `thresh` via if/else, a named
// `dot` local, a named `hit` bool, a flat (unparenthesised) dot, and
// reference bindings of joint[2]/[4]/[8] (all worse: 70, 71, 2-5).
// `mSideways = mSideways || ...` is worse (46); the single-write forms cost
// exactly one row (70).
```

Inside the body (was lines 136-141):

```text
    // w22-a20: the image sums the dot as (y + z) + x (0x8243DECC `fmuls f13,
    // f10, f13` y*y, 0x8243DEE4 `fmadds f13, f10, f12, f13` +z*z, 0x8243DEE8
    // `fmadds f13, f9, f11, f13` +x*x).  The parentheses pin that association
    // for the native build (/fp:fast re-sorted the old flat y + x + z to the
    // same listing, so this is listing-neutral: 99.95744 canonical before and
    // after).
```
