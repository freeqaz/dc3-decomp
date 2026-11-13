# Lane notes: `src/system/dsp/BitCrushEffect.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Process@BitCrushEffect@@QAAXPAMHH@Z`

`BitCrushEffect::Process` -- moved verbatim from `src/system/dsp/BitCrushEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 12-16):

```text
// w19-d: fuzzy 98.45 -> 100 (byte-identical).  A plain indexed loop; the
// image's `addi r10, r10, 0x8` right-channel walker is MSVC folding
// numChans == 2 into `f[i * numChans + 1]` inside the stereo test, not a
// hand-written pointer pair (the old two-pointer do/while rotated the
// callee-saved homes of this/f/numSamples).
```
