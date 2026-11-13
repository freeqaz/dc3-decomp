# Lane notes: `src/system/hamobj/MoveMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `Dc3W8bMoveMgrProbe()`

`Dc3W8bMoveMgrProbe` -- moved verbatim from `src/system/hamobj/MoveMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `Dc3W8bMoveMgrProbe` in report.json (unit); filed under the source name

Above the definition (was lines 27-31):

```text
// w8-b: orphan-instantiation anchor.  ham_xbox_r.map line 42162 puts
// ??$MakeString@HPBDPBD@@YAPBDPBDABHABQBD2@Z at 0x8245F500 in hamobj:MoveMgr.obj
// with no surviving caller -- the odr-use was compiled here and then dropped by
// /OPT:REF.  An unreferenced EXTERNAL function reinstates the instantiation;
// `static` does not (MSVC discards it before instantiating).  100% -> 0% without.
```


## `?FindHamMoveFromName@MoveMgr@@QBAPAVHamMove@@VSymbol@@@Z`

`MoveMgr::FindHamMoveFromName` -- moved verbatim from `src/system/hamobj/MoveMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 915-919):

```text
// w25-gi: the two "keep the preferred variant, else the neighbour's" stores
// are ternaries. As if/assign pairs (same code) the extra join blocks moved
// three callee-saved pairs: &mRoutineMeasures[player] r27 / prevPair r26,
// &mMoveParents[player] r25 / prevParent r24 and routinePair r21 / measure*4
// r20, each the other way round in the image.
```
