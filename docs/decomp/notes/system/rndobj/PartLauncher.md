# Lane notes: `src/system/rndobj/PartLauncher.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0RndPartLauncher@@IAA@XZ`

`RndPartLauncher::RndPartLauncher` -- moved verbatim from `src/system/rndobj/PartLauncher.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 7-13):

```text
// w21-bh: 84.93 -> 100.  The w7-bl/w7-br "scheduling" residual (home stores
// of the inlined ObjPtr ctors bunched after every vtable `lis`) was the
// PartOverride ctor's LINKAGE: the retail map lists ??0PartOverride@@QAA@XZ as
// `f i` (inline/COMDAT, kept from Part.obj), so it was an inline header ctor
// that MSVC declines to inline here, not an out-of-line `throw()` ctor in
// Part.cpp.  Defining it in Part.h closes every row; Part.obj's copy and
// ??__EgNoPartOverride stay at 100.
```
