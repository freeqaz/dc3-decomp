# Lane notes: `src/lazer/game/PartyModeMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DetermineSubModePlayers@PartyModeMgr@@AAAXVSymbol@@PAH1PAV?$vector@HV?$StlNodeAlloc@H@stlpmtx_std@@@stlpmtx_std@@@Z`

`PartyModeMgr::DetermineSubModePlayers` -- moved verbatim from `src/lazer/game/PartyModeMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 935-965):

```text
// RESIDUAL (w12-b, 98.84 canonical): register-only plus one `mr r30, r24`.
// The image keeps `this` in r30 and reuses mode's r29 for maxplayers, which it
// then counts down in place; we copy maxplayers into a fresh register for the
// second loop. Rewriting both loops as `for (; n != 0; n--)` is byte-identical.
// w18-e (98.84): dropping the `&& maxplayers != 0` guard (it is the rotated
// while's own entry test) is byte-identical.
// w21-e (98.84, 1 inserted `mr r30, r24` + register-only rows): two more
// byte-identical spellings -- `if (0 < total) while (max != 0 && total != 0)`
// and `for (; max != 0; max--)` with the in-loop return.  Image allocation:
// this=r30, mode->max share r29, &showdown->min share r28, `1` in r24; ours
// merges the second loop's counter into min's web (r30) instead of max's.
// Behaviour checked row by row against the image: identical.
// w21-m (98.84, same 28 rows; behaviour re-read vs 0x82871308..0x8287145C,
// identical): a standalone cl.exe probe reproduces our 28 rows exactly, and
// ~25 spellings leave them unchanged -- early `return` on showdown, `int
// player` hoisted to function/if scope, inline AddPlayer helpers (by value
// and by reference), `(*pNumPlayers)++`, do/while(--n) for either loop, loop 2
// as `for (int i = maxplayers; ...)`, `!(mode == showdown)`, `break` for the
// in-loop return.  Worse: subtracting minplayers outside the `if` (50 rows),
// counting loop 1 up with `for (i < minplayers)` (33), driving loop 2 on
// totalplayers with an inner `--maxplayers` break (35/51).  Root cause as
// measured: loop 2's counter is a separate web that coalesces with min's, so
// that web outranks `this` for r30; nothing tried merges it into max's.
// w21-ah (98.84 -> 100): the fix is on LOOP 1, not loop 2 -- count loop 1
// down on its own copy `for (int i = minplayers; i != 0; i--)`.  With min
// itself as loop 1's counter, loop 2's counter coalesces into min's web and
// splits off from max (`mr r30, r24`); with a separate loop-1 counter, max
// stays one web, takes mode's r29 and is counted down in place, exactly as
// at 0x82871444 `subic. r29, r29, 0x1`.  ~400 other combinations (decl order,
// loop/increment/flag spellings, early return) were all inert.  minplayers is
// dead after the loop, so behaviour is unchanged.
```


## `?ClearTeam@PartyModeMgr@@QAAXH@Z`

`PartyModeMgr::ClearTeam` -- moved verbatim from `src/lazer/game/PartyModeMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1059-1079):

```text
// RESIDUAL (w12-b, 99.92 canonical) -- same shape as FinalizeTeam: MSVC lays
// the case-2 block before case 1, the image the reverse, so objdiff pairs the
// two blocks against each other (every row +-12). Measured inert: swapping the
// case order in source, and moving `default:` first. Worse: an if/else-if
// chain (54.4), a `for` countdown loop (94.4). See
// docs/decomp/patterns/fixable-control-flow.md (arm transposition is a
// layout residual, not a polarity one).
// w20-o (99.92, same 8 rows): name_check's callee differences are ALL ICF
// folds, not wrong callees -- RadFree@?A0x6dfd0232 (BinkMovieSys) and our
// operator delete are both 0x82E21268 in ham_xbox_r.map (with OggFree,
// JsonFree, operator delete[]); the MakeString<char[19],int,char[5]> /
// <char[17],int,char[23]> pair are both 0x824D1870, and erase<UILabel*> /
// erase<PartyModePlayer*> are both 0x827BD118.  Branch landings agree:
// team==1 -> the 0x68 (mTeam1Players) block, team==2 -> the 0x74 block, on
// both sides.
// w21-az (99.92 -> 100): the assert comes FIRST and an if / else-if follows
// (no switch).  MSVC jump-threads the assert's team==1 / team==2 exits
// straight into the arms, and the failed-assert path (team known to be
// neither) straight to the epilogue, which lays the team-1 arm first as in
// the image.  Same behaviour natively: a non-fatal assert falls into an
// if / else-if that matches neither value.
```


## `?PickNextPlayer@PartyModeMgr@@AAAHXZ`

`PartyModeMgr::PickNextPlayer` -- moved verbatim from `src/lazer/game/PartyModeMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1123-1152):

```text
// RESIDUAL (w12-b, 98.0 canonical): (1) both DataArray::Node calls in the
// image use the Array() return still in r3; we re-copy it from r30 (`mr r3,
// r30` / `mr r3, r29`). (2) team-2 index: the image does `li r4, 1` / ble /
// `li r4, 0`, we emit the 0-then-1 form. Measured: `idx = 1; if (t1 > t2) idx
// = 0;` turns branchless (95.2); passing the bool comparison straight into
// Int() is worse (86.6); an empty-then/else spelling is identical to now.
// w17-e: still 98.0.  Two more spellings, both worse: the index as a
// `t1 > t2 ? 1 : 0` / `? 0 : 1` argument straight into Int() (86.6), and the
// Array() call folded into the Int() expression with idx computed first (69.9
// -- the size compare must come AFTER the Array() call, as the image has it).
// w20-s (98.0, branch-landing adjudication): rows 43/93 are NOT a behaviour
// difference.  Row 93 inverts consistently: image `li r4,1; ble -> Node; li
// r4,0` = ours `li r4,0; bgt -> Node; li r4,1`, both idx = (team1 <= team2).
// Both landings differ only by our extra `mr r3, rN` (image reuses Array()'s r3).
// w21-e (98.01 -> ~98.9): team 1 written as two whole `ret = arr->Int(k)`
// arms, then-arm Int(0) under `t1 <= t2`: MSVC tail-merges the two calls into
// one Node/Int pair with r4 picked by the branch and r3 = Array()'s return
// still live -- exactly the image's `li r4,0 / ble / li r4,1 / bl Node` with
// no `mr r3, r30`.  Same idx semantics (1 iff t1 > t2).  The same spelling on
// team 2 (if/else arms, `?:` arms, `ret = ..; ret += size`, sum in each arm)
// also closes rows 88-95 there, but every one of them re-schedules the
// unrelated playtest modulo block above (divwu/twllei/mullw order, rows
// 61-70, 95.67 overall), so team 2 keeps the idx form: 4 rows left
// (li r4 order + the `mr r3, r29` reload).
// w21-ah (98.97 -> 100): team 2 as two whole arms with the call FIRST,
// `ret = arr->Int(k) + mTeam1Players.size()`.  The size-first spelling
// (`size + Int(k)`, tried by w21-e) and every if/else/?: form with a
// separate teamCount re-allocated the playtest modulo block (divisor r8 ->
// r11, twllei hoisted: 10 rows); call-first keeps it.  Same semantics: the
// image reads mTeam1Players' size after Int() returns (0x8287003C `lwz r11, 0x4(r30)`).
```


## `?FinalizeTeam@PartyModeMgr@@QAAXH@Z`

`PartyModeMgr::FinalizeTeam` -- moved verbatim from `src/lazer/game/PartyModeMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 1532-1538):

```text
// w17-e (99.95, 6 rows: case-1/case-2 block order).  Measured byte-identical:
// `case 2:` written before `case 1:`, and `default:` written first.  An
// if / else-if / else chain is 84.3.  (ClearTeam has the same 8-row shape.)
// w21-az (99.95 -> 100): same lever as ClearTeam -- MILO_ASSERT first, then
// an if / else-if with no else.  The 84.3 chain above had the assert in the
// trailing else; first, MSVC threads its exits into the arms in image order.
// Behaviour unchanged: players/picker stay unset on a bad team either way.
```
