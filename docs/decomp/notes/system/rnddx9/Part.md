# Lane notes: `src/system/rnddx9/Part.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawShowing@DxParticleSys@@UAAXXZ`

`DxParticleSys::DrawShowing` -- moved verbatim from `src/system/rnddx9/Part.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 115-119):

```text
// w15-i1 (98.62 canonical): the image computes `aligned`/`stretched` straight into
// callee-saved r27/r29 (`clrlwi. r27, r11, 24`) with fancy in r28; we compute in
// r11 and `mr` (fancy r27, aligned r28), plus one extra clrlwi on constantArea.
// Tried: folding constantArea into the ternary (`stretched && mConstantArea ?
// 1.0f : 0.0f`) -- 98.62 -> 93.62, reverted.
```
