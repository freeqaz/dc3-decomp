# Lane notes: `src/lazer/meta_ham/AccomplishmentOneShot.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?AreOneShotConditionsMet@AccomplishmentOneShot@@QAA_NPAVHamPlayerData@@PAVHamProfile@@VSymbol@@W4Difficulty@@@Z`

`AccomplishmentOneShot::AreOneShotConditionsMet` -- moved verbatim from `src/lazer/meta_ham/AccomplishmentOneShot.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 21-52):

```text
// w20-m (normalized 100, 0 mismatched instructions, fuzzy 99.93): behaviour
// verified against the image -- every arm's branch target (Beginner/difficulty
// gate, cross-jumped flawless/nices calls, NumDays 0x114 / NumWeekends 0x11c,
// s == omg gate, shared Int() >= val tail, notify-and-return-false default).
// The charged relocations are names only: NumVerts@RndMesh vs
// GetFlawlessMoveCount and the two MakeString instantiations are ICF folds
// (same address in icf_aliases.map), and the second stars_earned / omg statics
// carry scope ordinal ?CP@/?CM@ (47/44) in the image vs ours ?EF@/?EC@ (69/66):
// the original opened 22 fewer scopes between the stars and hardest_stars
// arms. A shared `int count` + `||`-merged arms reproduced the ordinals exactly
// (44/47) but broke the cross-jumped block layout (85.0), so the separate-arm
// spelling stays; the original arm spelling is still unknown.
// w25-gs: not a register decision -- the ordinal is c1's per-function scope
// counter, so the c2 tap has nothing to say about it. Measured costs: each
// `else if (c) { if (x >= val) return true; }` arm opens 6 scopes, the inner
// if 2, MILO_ASSERT 5. Writing the arms as independent `if (c) { ...;
// continue; }` blocks is code-identical (245/245 rows) and gives ?DL@/?DO@
// (59/62); the image needs 44/47, 15 fewer. Open question: which arm shape
// opens about 2.5 scopes per arm and still cross-jumps like the image.
//
// w26-f15, measured on the EXACT path (`ninja post-compile`, because the
// per-object harness is inexact for this unit -- it reads 99.440/99.440 where
// the real row is 99.925/100.000): the current row set is the four static
// relocations only, image ?CP@ (stars_earned) / ?CM@ (omg) vs ours ?EF@ /
// ?EC@, i.e. the same 26-letter step apart. The two cheap scope reducers both
// lose the cross-jump and with it 13+ instructions: merging the flawless pair
// (`condition == flawless_a || condition == flawless_b`) or the nices pair --
// which produce IDENTICAL code to each other, since MSVC already cross-jumps
// the duplicate arms -- gives 281 insns and 89.907, and the
// independent-if-with-`continue` shape gives 287 insns and 89.537. So the arms
// have to stay separate `else if`s. The remaining lever has to lower the scope
// counter without adding a block; not found this round.
```
