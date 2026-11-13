# Lane notes: `src/system/math/Trig.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?TrigTableInit@@YAXXZ`

`TrigTableInit` -- moved verbatim from `src/system/math/Trig.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 9-15):

```text
// w19-c (98.72 -> 100): a plain INDEX loop.  The image's signed pointer test
// (`cmpw cr6, r30, r11` against `addi r11, r29, 0x7fc` recomputed in the loop)
// is MSVC's own linear-function-test replacement of `i < 256` onto the
// strength-reduced &gBigSinTable[i * 2 - 1] cursor -- it keeps the signedness
// of the int compare it replaced, which is why no hand-written pointer loop
// (cmplw), cast (a fourth GPR) or j-stepped index loop (wrong cursor) reached
// it.  The final half-iteration (the delta slot 511) is peeled after the loop.
```
