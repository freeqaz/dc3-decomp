# Lane notes: `src/system/hamobj/MiniGameMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateCascadeMovePool@MiniGameMgr@@QAAXAAVMoveGraph@@AAV?$vector@PBVMoveVariant@@V?$StlNodeAlloc@PBVMoveVariant@@@stlpmtx_std@@@stlpmtx_std@@1@Z`

`MiniGameMgr::UpdateCascadeMovePool` -- moved verbatim from `src/system/hamobj/MiniGameMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 83-115):

```text
// w8-l: 99.900000 normalized, the only function keeping this unit from 100%
// (30/31).  Five charged rows and every one is a register NAME, not a
// different computation:
//   [43]/[44] the two hoisted lis's are emitted r25(kAssertStr) then
//             r24(TheGameData) by the image and the other way round by us --
//             same destination registers, same values, opposite order.
//   [51]/[52]/[55] the image loads mNumMovesNeeded(0x2c) into r9 and
//             mBlockingFactor(0x30) into r10; we use r10 and r9.  The add is
//             `add r10, r9, r10` vs `add r10, r10, r9` -- the same sum in the
//             same order, spelled with the two volatiles exchanged.
// Refuted: writing the sum as `mNumMovesNeeded + mBlockingFactor` is exactly
// inert (99.900000) -- MSVC already canonicalises the two loads to ascending
// field order on both sides.
// Not a bug: objdiff reports four target-only/base-only callees here
// (MakeString<char[19],int,char[5]>, __find<DxTex**>, vector<JoypadClient*>
// _M_erase, vector<UILabel*> erase).  The string literals those MakeStrings
// actually load, rows [40] and [41], MATCH -- these are ICF folds of
// byte-identical instantiations from other TUs, not wrong callees.
// w19-b (99.90): 5 rows, all register numbering -- the hoisted kAssertStr/
// TheGameData lis pair in the other order and the 0x2c/0x30 loads into
// r9/r10 swapped.  Inert: `mNumMovesNeeded + mBlockingFactor` (MSVC
// canonicalises the sum).  Worse: the i == 0 arm first (76.0).
// w21-aw (5 -> 3 rows): `switch (i)` with case 0 / default MILO_ASSERT fixed
// the hoisted lis pair order ([43]/[44]).
// The named `parent` local closes the last 3 rows (the 0x2c/0x30 loads into
// r9/r10).  Those two loads are tie-0 code-band pieces ordered by id; the
// 0x2c load is the 8th code mint.  With 7 negative-benefit frees it got a
// fresh id and was coloured first (r10).  `parent` is one more such candidate
// (a load passed straight to a call), so with 8 frees the 0x2c load recycles
// the lowest id, the 0x30 load is coloured first, and the registers match.
// Inert before: the sum in either order, a `(unsigned int)(...)` cast, the
// size test as an `if (...) break;` at the top of the body.  Worse:
// `end() - begin() >` in place of size() (99.4).
```
