# Lane notes: `src/lazer/meta_ham/FitnessGoalMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?OnMsg@FitnessGoalMgr@@AAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z`

`FitnessGoalMgr::OnMsg` -- moved verbatim from `src/lazer/meta_ham/FitnessGoalMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 284-289):

```text
// w25-rc: the failure arm returns on its own.  With one shared `return 1` the
// hidden DataNode return pointer (image r28) had colour priority -24 against
// the hoisted 0's -14 (image r27), so the 0 took r28.  The extra return
// references the return pointer in a second block: both reach -9 and the
// return pointer wins the tie (tie key 15 > 0), as in the image.  The RadFree
// callee row is ICF (82E21268 = operator delete).
```
