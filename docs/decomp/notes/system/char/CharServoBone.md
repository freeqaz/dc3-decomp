# Lane notes: `src/system/char/CharServoBone.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?MoveToDeltaFacing@CharServoBone@@QAAXAAVTransform@@@Z`

`CharServoBone::MoveToDeltaFacing` -- moved verbatim from `src/system/char/CharServoBone.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 203-219):

```text
// w25-oa: 52/54. The two rows left are the y and z fadds operand order of
// `tf.v += v18` (c2's commutative sort key, C2RS-BRIDGE 8.7). Keys: for x, the
// load M[operator+= this, variable sid 4] is 0x18008, above v18.x (V11, 0x10160),
// so the load goes first, which matches. For y, v18.y (V5, 0x100a0) is above
// M[base temp 248] (0x10008); for z, v18.z (V7, 0x100e0) is above M[base temp
// 252] (0x10008). So the value goes first in y and z, and the image has the
// load first in both. Needs base temps 248 and 252 to be != 0 mod 4 (key >=
// 0x14008), i.e. a shift of 1-3 temps before them. Nothing reachable: the bases
// stay at 248/252 under 25 code-neutral spellings (refs/pointers to tf.v, delta
// or matrix, Transform alias, scopes, explicit Set() expansion of Multiply,
// named components or partial sums, a copy of v18, the early-return if). They
// move only with code-changing ones: a Vector3 copy of the delta gives 241/245
// and the predicted y/z flip, but adds 9 instructions.
// Also changes the code (w25-gc): Add(tf.v, v18, tf.v) or Add(v18, ...),
// per-component += or explicit sums (they drop the image's dead
// `addi r11, r4, 48`, so the image does call operator+=), and an in-place
// Multiply on a copy.
```
