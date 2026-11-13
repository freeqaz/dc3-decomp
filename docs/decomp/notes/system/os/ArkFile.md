# Lane notes: `src/system/os/ArkFile.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ReadAsync@ArkFile@@UAA_NPAXH@Z`

`ArkFile::ReadAsync` -- moved verbatim from `src/system/os/ArkFile.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 96-102):

```text
// w8-l: 99.934690 normalized, the only function short of 100% in this unit
// (14/15).  60 charged rows out of 245, and 49 of them are register names
// across NINE callee-saved pairs (r21/r22 leading with 10) -- the whole
// callee-saved bank is rotated by one relative to the image, which then shows
// up again as 2 slot swaps of (0x50, 0x54) and a handful of +/-4..24
// displacements.  One upstream liveness decision, not 60 independent rows.
// Measured only in this wave.
```
