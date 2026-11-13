# Lane notes: `src/lazer/meta_ham/MetaPerformer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CalcCharacters@MetaPerformer@@QAAXPBVHamSongMetadata@@_NW4PlayerFlag@@AAPAVHamPlayerData@@AAVSymbol@@443444@Z`

`MetaPerformer::CalcCharacters` -- moved verbatim from `src/lazer/meta_ham/MetaPerformer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1242-1261):

```text
// Character selection priority:
// 1. If both players have valid, non-conflicting characters -> use player preferences
// 2. If skeleton tracking differs -> prioritize active skeleton
// 3. If one has preference -> use that player as primary
// 4. Otherwise -> use tracking age (newer player gets priority)
// w21-ar: 99.988 normalized / 99.498 fuzzy, 22 rows, all register naming or
// argument-load order, no value difference (adjudicated row by row):
//   image r22 = primaryChar (from r9), r24 = primaryOutfit (from r10),
//   r23 = the hoisted gNullStr `lis`; ours r23 / r22 / r24 -- a three-way
//   rotation that accounts for rows 13/14/24/32/41/45/80/91/150/152/159/175/
//   210/214/221/226/227/232/233.  Rows 237-240 are the same four argument
//   loads for CalcSecondarySongCharacter (r6 = *primaryOutfit, r7/r8 from the
//   0x13c/0x144 stack params) issued in a different order.  No source lever
//   tried here; recorded as the allocator tie it appears to be.
// w21-bn (99.988, same 22 rows; re-adjudicated: r22/r23/r24 carry
// primaryChar / gNullStr-hi / primaryOutfit consistently on every row, and
// the final CalcSecondarySongCharacter loads read the same four values).
// Inert: `Symbol primaryPlayerChar, secondaryPlayerChar;` hoisted to function
// scope; the conflict-free branch ending in `return;` with the rest at
// function scope (no else).
```


## `?SaveAndUploadScores@MetaPerformer@@AAAXVSymbol@@HH@Z`

`MetaPerformer::SaveAndUploadScores` -- moved verbatim from `src/lazer/meta_ham/MetaPerformer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1424-1431):

```text
// w20-i: normalized 100, fuzzy 99.75. Checked every permuted row against the image:
// the 20 remaining rows (177-206) are the hoisted lis/addi materialisation of the
// alert_unlockedhard/move_perfect/move_awesome/expert static and string addresses in
// different VOLATILE registers; each address lands in the same callee-saved register
// (r17/r21/r16/r18) and each string in the same frame slot (0xd4/0xd0/0xcc/0xc8) as the
// image, and the .bss order reproduces the image's (expert lowest .. guard highest).
// The 4 `sym` rows are lbl_83118E24..30 vs our per-variable .bss names. No value or
// branch divergence; the bl rows at 281/367/423 are ICF folds (82545670, 82e2ab00).
```
