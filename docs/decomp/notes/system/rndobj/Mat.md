# Lane notes: `src/system/rndobj/Mat.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `MatShaderOptions::SetHasAOCalc()`

`MatShaderOptions::SetHasAOCalc` -- moved verbatim from `src/system/rndobj/Mat.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Attribution: no function named `MatShaderOptions::SetHasAOCalc` in report.json (whole report (header)); filed under the source name

Above the definition (was lines 50-55):

```text
    // w26-r: the `shader_struct.mHasAOCalc = 0;` line that used to precede the
    // assignment was decompilation-added.  It is dead on a 1-bit bitfield
    // (assigning 0 then the bool is the same value), but it cost a live
    // `mr r10, r9` in Utl.cpp's GetDefaultMatShaderOpts arm -- the zero store
    // forced the byte into the inlined parameter's register.  Same setter, one
    // statement, and the call sites compile to the image's code.
```
