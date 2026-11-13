# Lane notes: `src/system/gesture/DirectionGestureFilter.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Draw@DirectionGestureFilterSingleUser@@UAAXABVSkeleton@@AAVSkeletonViz@@@Z`

`DirectionGestureFilterSingleUser::Draw` -- moved verbatim from `src/system/gesture/DirectionGestureFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 71-95):

```text
// RESIDUAL (w11-d, 99.77 canonical): the image pins 0.1f in f29 and 0.2f in
// f28 and sets up the first DrawPoint3D's arguments this/radius/color/alpha
// (`mr r3` before `mr r6`, f1 before f2); we get the constants the other way
// round.  Inert: named radius/alpha locals in either order; the second call
// without the `pos` reference is worse (92.25); a decomp-synth beam run
// (6 rounds) found nothing.
// w21-bk: ROOT CAUSE FOUND, not fixable in this file. The constant order is
// decided by PaddedJointPos's `operator const Vector3 &()` (math/Vec.h): that
// inline conversion is one call level the original did not have.  Standalone
// cl.exe probe (this function, real flags): mJointPos as a plain Vector3, or
// `struct PaddedJointPos : Vector3 { float _pad; }` (derived-to-base, no call),
// gives the image's exact order in BOTH calls (f1<-f29 then f2<-f28; second
// call f2 first); the conversion-operator struct reproduces our rows.  Same
// result compiling the real TU with the PCH and only that type swapped.  The fix
// belongs in math/Vec.h (PCH-reached, out of scope for this lane) -- flagged to
// the coordinator; it may move every TrackedJoint reader in gesture/.
// w24-pch: FIXED at the call site, 99.773 -> 100.  The header fix was A/B'd
// first (full ninja, whole-binary compare): `struct PaddedJointPos : Vector3`
// takes this and IsValidSwipePosition to 100 but drops
// StandingStillGestureFilter::Update 100 -> 90.16 (the image holds the knee
// xyz in f29-f31 across Normalize(); once the joint's x/y/z ARE Vector3::x/y/z
// MSVC reloads them after the call) and HandInvokeGestureFilter::CalcInPose
// 99.037 -> 99.025, so the joint type is NOT Vector3-derived in the original.
// Binding the first argument through a pointer cast removes the conversion
// operator's inline level without changing the type (same address).
```


## `?IsValidSwipePosition@DirectionGestureFilterSingleUser@@ABA_NABVSkeleton@@@Z`

`DirectionGestureFilterSingleUser::IsValidSwipePosition` -- moved verbatim from `src/system/gesture/DirectionGestureFilter.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 192-200):

```text
    // w21-as: written with Vector3 helpers over skeleton.TrackedJoints() (no
    // `joints` local), the closest-point delta and the hip direction as
    // Subtract()s, and the ellipse sum as two statements: 10 -> 3 rows
    // (normalized 99.979866 unchanged, fuzzy 99.409 -> 99.879).  The
    // value-numbering order MSVC uses for its /fp:fast operand
    // canonicalisation followed the old over-named locals (hipX,
    // deltaX, closestDeltaX, ...) and emitted every commutative pair the
    // other way round: `hipX + deltaX` (target 0x82DEA580 fadds f12, f11, f9),
    // the rotation products (0x82DEA660..70) and the ellipse sum.
```

Inside the body (was lines 221-226):

```text
    // w21-as: the image sets up ClosestPoint's address arguments r4 (corner2),
    // r3 (corner1), r6 (&closest); with the hand joint passed through
    // PaddedJointPos's `operator const Vector3 &` we emitted r6 first (3 rows;
    // inert: named refs, declaration orders, &closest through a pointer).
    // w24-pch: the pointer cast (no inline conversion call, same address) gives
    // the image's order -- 99.980 -> 100.  See the note above Draw().
```
