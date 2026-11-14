# Lane notes: `src/lazer/meta_ham/OptionsPanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?OnMsg@OptionsPanel@@IAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z`

`OptionsPanel::OnMsg` -- moved verbatim from `src/lazer/meta_ham/OptionsPanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 79-106):

```text
// Open (register order only; every value, store, branch and call matches the
// image): two COLOR pop orders differ (c2 candidate tap).
// - The zero constant (id 1, sym 0/k0d, pri -144) pops after `this` (id 5,
//   pri -139), so they take r20/r21 the other way round. The image needs the
//   zero first, i.e. +6 for it relative to `this`.
// - The hoisted static-Symbol addresses: token_redemption_too_late (id 65,
//   pri -5 tie 131) pops before leaderboard_no_net (id 63, pri -7 tie 146);
//   the image pops no_net first (r30, then too_late r29, too_early r27), which
//   needs no_net +2 so the tie decides.
// ~22 respellings (switch case order, default-arm forms, success/error
// placement) were inert or changed the emitted code (cross-jumped
// `success = false` tails, the guard order).
// w26-n: no behaviour difference -- the call-name differences objdiff reports
// (`GetDepthRT`, `SetSuccess@MultipleItemsEnumCompleteMsg` against
// `GetResponseString`, `SetLinkingCode`, `SetSuccess@LinkingCodeRetrievedMsg`,
// and the `SetOfferString` count) are ICF aliases at one address: name_check
// charges exactly the same 22 register rows and nothing else.  The prologue
// shows the first swap directly -- ours `mr 21,4` / `li 20,0`, image
// `mr 20,4` / `li 21,0`, so it is the incoming-message pointer and the zero
// constant trading r20/r21.  The base-reference lever has no lever arm on
// either contested pop order: they are two pri/tie comparisons between
// values that are both live across the whole function and neither of which
// is a memory operand, so the remaining handle is the §6 priority budget
// (refs per block, or an extra merge point), not operand binding.
// w26-f6: `bool success = false;` (an extra zero use in the entry block) is
// inert at 99.66102/100.0, and folding the default arm into
// `error = online ? token_redemption_error : leaderboard_no_net;` is worse
// (98.814).  Both measured on a full ninja build.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.661, 22 non-equal row(s): NOT A BUG.
Every row is register-only; `scripts/analysis/regperm_equivalence.py` raised 1 lead(s),
each read by hand against the listing: pure register permutation, no operand swap. The
one JOIN? lead (idx 140, 0xc28) is the case-2 arm loading the token_redemption_too_late
static through the same renamed register (r29 vs r30) the rest of the function uses -- a
linear-scan join artifact.
