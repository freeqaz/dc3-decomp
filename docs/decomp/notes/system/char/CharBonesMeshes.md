# Lane notes: `src/system/char/CharBonesMeshes.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/char/CharBonesMeshes.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

File-level (was lines 199-209; no definition follows it):

```text
// w8-c: the image's CharBonesMeshes.obj carries an out-of-line
// `PropSync<RndTransformable>(RndTransformable *&, ...)` COMDAT (264 B, at
// 0x8234D0F8 in build/373307D9/asm/system/char/CharBonesMeshes.s) that NOTHING
// in the TU calls -- retail's ObjPtrVec PropSync inlines the two call sites
// (the asserts at line 0x66 appear inline inside
// `??$PropSync@VRndTransformable@@@@YA_NAAV?$ObjPtrVec@...`), and MSVC emits the
// instantiation anyway.  We build at /O1 (=> /Ob1), where MSVC declines to
// expand it even when the template is marked `inline`, so writing the call
// instead of the hand-inlined body costs the enclosing function 100.0 -> 68.0
// while gaining this row: measured, net -4,344 B on the headline.  An explicit
// instantiation buys the COMDAT with no code change at all.
```
