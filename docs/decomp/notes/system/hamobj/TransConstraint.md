# Lane notes: `src/system/hamobj/TransConstraint.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@TransConstraint@@UAAXXZ`

`TransConstraint::Poll` -- moved verbatim from `src/system/hamobj/TransConstraint.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 173-186):

```text
// Residual (w25-fb, fuzzy 99.92, 3 rows): three /fp:fast commutative operand
// orders.  (1) `hi = parentPos[i] + halfCube`: both are regasg temps, the image
// lists halfCube first; regasg's u{} is ordered by the higher sid first (the
// load is sid 730, halfCube's fmuls 623).  (2) `scaleRatio * mSpeed`: the
// image lists the scaleRatio candidate before the mSpeed load.  (3)
// `scaleDir[i] * scaleSpeed`: the image lists the scaleSpeed candidate first.
// The order is fixed in the IL before regasg; swapping the source operands, or
// any `x op= y` respelling, is inert.  A ~500-variant sweep of neutral named
// locals (cube, dirComponent x2, scaleLen, dist, maxDist, localScale, speed),
// declaration moves and an if/else vs ternary `dt` never moved rows 1 and 2.
// Row 3 and the first loop's `step * dt` (which matches now) move together: a
// ternary `dt` fixes row 3 and flips that one, a named dir[i] local flips the
// first-loop row alone.  No combination fixes all three.  Open question: the
// key that orders a candidate against a temp (C2RS-BRIDGE 8.3).
```
