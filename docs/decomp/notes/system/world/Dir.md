# Lane notes: `src/system/world/Dir.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Sync@BitmapOverride@WorldDir@@QAAX_N@Z`

`WorldDir::BitmapOverride::Sync` -- moved verbatim from `src/system/world/Dir.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 110-115):

```text
// w20-k (100 normalized, 6 diff_arg rows in the !b preamble): values checked --
// both sides put &localRing into prev (0x60) then next (0x5c) and compare the
// begin node against replacement+4; only the three scratch registers r8/r9/r11
// are permuted. Branch targets and the RefOwner() null test (present only on
// the b arm in the image too) agree. Measured: declaring `it` before Clear()
// drops to 96.0. Noise class: volatile-register permutation.
```
