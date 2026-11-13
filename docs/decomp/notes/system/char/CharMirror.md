# Lane notes: `src/system/char/CharMirror.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@CharMirror@@UAAXXZ`

`CharMirror::Poll` -- moved verbatim from `src/system/char/CharMirror.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 62-66):

```text
    // w21-q: 98.61 -> 100 with rb3-xenon's shape -- the section bounds read
    // through CharBones' Start()/ScaleOffset()/... accessors.  The image starts
    // the POS loop at mStart itself (`extsw r10, r11`, no mOffsets[TYPE_POS]
    // load; the ctor, ClearBones and RecomputeSizes all keep that offset 0 and
    // AddBoneInternal only bumps later sections, so this is the same address).
```
