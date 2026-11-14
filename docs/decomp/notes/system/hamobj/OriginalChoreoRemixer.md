# Lane notes: `src/system/hamobj/OriginalChoreoRemixer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SelectMove@OriginalChoreoRemixer@@MAAXHH@Z`

`OriginalChoreoRemixer::SelectMove` -- moved verbatim from `src/system/hamobj/OriginalChoreoRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 89-96):

```text
// w20-k (100 normalized, 1 diff_arg row): values checked against the image --
// unkec[player][idx] vs mDesiredDiffs[player] (0xe4 + player*4), HasVariantPair
// on slot 0x7c's vector, mp_next/mv_next via slots 0x7c/0x80 indexed by
// measure*4, `bl OnlyReturns` = ICF fold of the empty DanceRemixer::SelectMove.
// The one row is `lwzx r27, r10, r29` (image) vs `lwzx r27, r29, r10`: same
// data pointer and byte index, operands swapped. Measured: `*(v.begin() +
// measure)` on mp_next leaves it and flips the mv_next load too; on mv_next it
// is inert. Noise class: commutative operand order of an indexed load.
```


## `?BridgeGapsInMoveParents@OriginalChoreoRemixer@@IAAXH@Z`

`OriginalChoreoRemixer::BridgeGapsInMoveParents` -- moved verbatim from `src/system/hamobj/OriginalChoreoRemixer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 255-269):

```text
            // Open (w25-rk): fuzzy 99.896, normalized 100. The only residual is
            // r10/r11 on the two per-iteration loads below (setVec's begin
            // pointer, moveParentsByDiff's begin pointer); the image puts the
            // moveParentsByDiff load in r11. Hypothesis from the c2rs tap: both
            // are code-band candidates at pri 16, tie 0 (code#81 id 13, code#82
            // id 3), so COLOR pops them in descending id. The benefit pass frees
            // 15 candidates and the code band mints 15, so every code mint
            // recycles an id and mint order equals pop order (setVec first).
            // Predicted fix: exactly one negative-benefit candidate fewer (or
            // one more code mint before this pair), which gives code#82 a fresh,
            // highest id. Open question: which of the 15 frees the original
            // lacks. Inert (all 15 frees): curSet/prevMoveParent order,
            // curSet as a pointer, the i12 init spelling, `== nullptr` test;
            // dropping the candidate local, per-arm stores and reordered arms
            // change the emitted code.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.896, 4 non-equal row(s): NOT A BUG.
Every row is register-only; `scripts/analysis/regperm_equivalence.py` raised 1 lead(s),
each read by hand against the listing: pure register permutation, no operand swap.
(CALLARG? leads read by hand: HasPrevAdjacent takes r3/r4; the r10 lead is a dead
leftover.)
