# Lane notes: `src/system/ui/UIList.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawShowing@UIList@@UAAXXZ`

`UIList::DrawShowing` -- moved verbatim from `src/system/ui/UIList.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 877-884):

```text
// w21-z: 98.15 -> 100.  The image's double home store of mParent to
// 0x50(r31) and its cr0 `cmplwi r29, 0` are the return temp of an inlined
// ParentList() accessor (RB3's spelling, rb3 UIList.cpp:249), called three
// times; offset is built as `offset = spacing; offset *= (float)n;` (the
// CalcBoundingBox shape), which gives the image's `fmuls f31, f1, f0` operand
// order -- `spacing * (float)n` and `n * spacing` both give f0, f1.  w11-b's
// residual note (mParent && ..., parent local, header-inline ChildList,
// (float)n * spacing all inert) is superseded.
```
