# Lane notes: `src/system/math/Vec.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/math/Vec.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

File-level (was lines 176-183; nearest following definition: `PaddedJointPos::PaddedJointPos`):

```text
// 16-byte padded Vector3 for structs that need XMVECTOR-compatible stride
// w24-pch: do NOT make this `struct PaddedJointPos : Vector3` (Vector3 is
// already 16 bytes with its PAD).  A/B'd with a full ninja: it fixes the two
// DirectionGestureFilter readers the conversion operator's inline level hurt,
// but takes StandingStillGestureFilter::Update 100 -> 90.16 -- the image holds
// joint x/y/z across Normalize(), which MSVC stops doing once they ARE
// Vector3::x/y/z.  The original type is not Vector3-derived; readers that need
// no inline level bind `*(const Vector3 *)&joint.mJointPos[0]` instead.
```


## `LengthSquared()`

`LengthSquared` -- moved verbatim from `src/system/math/Vec.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `LengthSquared` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 268-293):

```text
// w13-o (2026-10-02): the "image computes y, z, x where we compute x, y, z"
// residuals are NOT a term-order property of these helper bodies.  A flat sum
// is canonicalised under /fp:fast -- writing Dot's terms y, z, x is
// byte-identical -- and the canonical order MSVC picks depends on the
// surrounding function (measured in an isolated TU: Length alone -> y,x,z;
// Dot alone -> y,z,x; Subtract-then-Length -> z,x,y; and in one function the
// order of a Dot changes when a later ScaleAdd consumes its operands).  Two
// natural-source witnesses pin the current bodies: RndFlare::CalcScale (RB3's
// source verbatim) is 100 with this Length, and MultiplyTranspose /
// ClosestPoint / ComputeAngle / FastInvert are 100 with this Dot.
// Measured LOSSES, do not retry (per-target probe of ~34 functions, plus a
// full-ninja whole-binary A/B where noted):
//   Length = sqrt(LengthSquared(v))        whole-binary 2 up / 7 down, -2 matched
//     (fixes CharEyes::EnforceMinimumTargetDistance and ScaleBone, but
//     MakeScale 99.78->87.49, CalcScale 100->97.30, SimulateInternal -4.7)
//   Length with x/y/z temps, = sqrt(Dot(v,v)), right-assoc, ((x+y)+z): same
//     profile or worse (4 functions fall off 100)
//   LengthSquared = Dot(v, v): probe-inert
//   Dot right-assoc / explicit left parens / named products: 5 fall off 100
//     (explicit parens are honoured by MSVC: `(a+b)+c` is NOT `a+b+c`)
//   Subtract / Add / Scale as statements, ScaleAdd via Set: all lose
//   Distance/DistanceSquared via a 3-float-ctor diff: 1 up / 5 down
//   Distance = sqrt(DistanceSquared(...)): probe-inert
//   Vector3(f,f,f) ctor via Set(): loses; with assignments: inert
// The lever that DID fix EnforceMinimumTargetDistance is at its call site:
// build the difference with Vector3(a.x - b.x, ...) instead of Subtract().
```


## `Dot()`

`Dot` -- moved verbatim from `src/system/math/Vec.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `Dot` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 302-315):

```text
// Do NOT rewrite this as a per-component accumulator. The lever that pins
// /fp:fast association in Hmx::Dot4 (math/Mtx.h) is measured HARMFUL here.
// Measured (w7-m, whole-binary A/B, full ninja both sides):
//   float d = v1.z * v2.z; d += v1.y * v2.y; d += v1.x * v2.x; return d;
// gives 4 functions improved / 20 regressed, 6 leave the matched set
// (MakeScale 99.78->83.45, MakeRotQuat 90.31->77.31, CharGuitarString::Poll
// 98.65->91.46, RndFlare::CalcScale and MultiplyTranspose and ClosestPoint
// and FastInvert and ComputeAngle and RotateTowards all fall off 100.0),
// headline 47.74566 -> 47.72928, matched functions 30974 -> 30968.
// It does not even fix the function it was tried for: RndMesh::SetVolume's
// 4-row residual (the inlined Dot there loads y/z into the opposite pair of
// FPRs from the target) is byte-for-byte unchanged by it. MSVC's /fp:fast
// reassociation of this expression is decided per call site, so a fixed
// association in the inline cannot be right everywhere.
```
