# Lane notes: `src/system/hamobj/RhythmBattle.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `JustEnteredZone()`

`JustEnteredZone` -- moved verbatim from `src/system/hamobj/RhythmBattle.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Attribution: no function named `JustEnteredZone` in report.json (unit); filed under the source name

Above the definition (was lines 696-700):

```text
// w20-b: the helper and its name are ours. Spelled inline in the two zone
// tests, `mPlayerX->ZoneValue() && !mPlayerX->GetPrevInTheZone()` kept the
// operator-> result in a $T home slot (four dead `stw rN, 0x64/0x68(r31)` per
// test); passing the player as a parameter to an if/return helper gives the
// image's code exactly (a `return a && !b` helper materialises the bool).
```


## `?OnBeat@RhythmBattle@@AAAXXZ`

`RhythmBattle::OnBeat` -- moved verbatim from `src/system/hamobj/RhythmBattle.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 971-1016):

```text
                // w20-b/w21-ax/w26-n1: the last residual, re-adjudicated.
                //
                // Only idx 1316-1320 are charged at 99.95106 normalized. They are
                // three independent instructions of the SECOND call's argument
                // setup, emitted in a different order; both sides put the same
                // values in r4/r5/r6 and call the same two functions, so there is
                // no behaviour difference. Image: L1, L2, mr r6, mr r3, lwz state.
                // Ours: L1, mr r6, lwz state, mr r3, L2. (L1/L2 are the two loads
                // of the UIPanel* -> Hmx::Object* vbase conversion of focusPanel.)
                //
                // w26-n1 mechanism, from the c2rs regasg tap: the order already
                // diverges in the pre-sched0 tuple stream (RT block 234 -- T1548
                // lwz L1, T1549 mr r6, T1550 lwz state, T1551 mr r3, T1552 lwz L2),
                // so sched0 is not the decider; c2 sinks L2 to its use in the
                // `add`, and the image keeps it next to L1. Every other panel
                // conversion in this function keeps the L1 ... L2 split on both
                // sides, so this call is the outlier.
                //
                // 46 behaviour-identical spellings measured: byte-identical or
                // worse. Named state/panel/jacked/receiver locals, reference
                // bindings, (*p). form, nested block, i6b4 moved, ternary and
                // 5 + !b36 forms of the state store, declaration order, cast
                // forms, and 15 pairs. The other 8 charged rows are only
                // function-local-static scope ordinals (?NC@ 210, ?PC@ 242,
                // ?BEG@ 326, ?BJK@ 410 in the image). They do NOT need an
                // invented static: the ordinal counts scopes, so braces move it
                // (docs/decomp/patterns/fixable-scope-index.md).
                //
                // w-rb-ordinal (2026-11-13), against the map's 31 ??__F helpers:
                // every region now has the image's scope count EXCEPT the one
                // this note sits in -- bars_between_vo_suggestion (?HE@ 116,
                // matches) to the first switch msg (?LM@ 188): the image opens
                // 13 more scopes than we do here, so every static after it reads
                // 13 low (first msg 175 vs 188, worked 313 vs 326). Intra-switch msg
                // spacing, and every later region, match the image exactly.
                // Instruction-neutral (OnBeat code+reloc hash unchanged) spellings
                // measured in this region: nesting the first->InTheZone() && test
                // (+3); `} else { if (i27) .. else .. }` for the side select (+1);
                // and a provably-true MILO_ASSERT_IF(focusPanel) (+3) -- the
                // optimizer deletes it, since focusPanel was null-checked above.
                // NOT neutral: i6b4 as nested if/else (+15; regalloc cascade,
                // 4132 rows), i16/i19 as `= -1; if (pN) ...` (+4; 21552 B). The
                // image's string pool gives no assert-condition string between
                // bars_between_vo_suggestion and the swag jack, and the count
                // alone does not say which constructs carry the 13, so nothing
                // is committed here: a guessed distribution would only buy names.
```

## `??__Fgame_outro_finale@?BJK@??OnBeat@RhythmBattle@@AAAXXZ@YAXXZ`

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 97.857, 3 non-equal row(s): the 3 charged
rows are the static's scope ordinal, image `?BJK@` (410) vs ours `?BIN@` (397). All four
OnBeat statics the image names are off by the same 13 (msg 210/197, msg 242/229,
worked_it_progress 326/313, game_outro_finale 410/397), so our OnBeat numbers 13 fewer
scopes before scope 197. Per-function counter; OnBeat is 99.95 canonical, so the missing
scopes carry no code. NOT A BUG.

## `??__Fworked_it_progress@?BEG@??OnBeat@RhythmBattle@@AAAXXZ@YAXXZ`

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 97.857, 3 non-equal row(s): the 3 charged
rows are the static's scope ordinal, image `?BEG@` (326) vs ours `?BDJ@` (313). All four
OnBeat statics the image names are off by the same 13 (msg 210/197, msg 242/229,
worked_it_progress 326/313, game_outro_finale 410/397), so our OnBeat numbers 13 fewer
scopes before scope 197. Per-function counter; OnBeat is 99.95 canonical, so the missing
scopes carry no code. NOT A BUG.
