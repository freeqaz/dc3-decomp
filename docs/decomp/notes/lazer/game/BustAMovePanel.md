# Lane notes: `src/lazer/game/BustAMovePanel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetFlashcardName@BustAMovePanel@@AAAXHHH@Z`

`BustAMovePanel::SetFlashcardName` -- moved verbatim from `src/lazer/game/BustAMovePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 240-272):

```text
// w14-e (99.93, 5 stack rows): the image homes the `index` PARAMETER
// (`stw r5, 0xb4(r1)`, &index passed to both MakeStrings) and keeps the
// moveName Symbol (0x54) and the else-arm Symbol(gNullStr) temp (0x58) in
// DISTINCT slots, frame 0x90.  Passing `index` directly homes it correctly
// but MSVC then shares 0x54 between moveName and the temp, frame 0x80
// (99.92, 6 rows); a plain `s = ...->Sym(1)` temp, a const-ref moveName, an
// early return instead of the else, and a ternary argument were all equal or
// worse.  flashCardIdx kept.
// w19-e: index direct + plain `s = ...->Sym(1)` is 99.9 (frame 0x80, the
// Sym temp shares 0x54); index direct + a `cond ? s : Symbol(gNullStr)`
// argument is 93.0 (the image's two SetTextToken arms are separate calls
// tail-merged at the bctrl, not one call on a selected value).
// w21-az (99.93, same 5 stack rows): index direct + `switch (mState)` with
// fallthrough cases is 93.4 (compare order flips, frame 0x80); index direct +
// a NAMED else-arm `Symbol nullSym(gNullStr)` is 93.0 (breaks the image's
// bctrl tail merge, frame still 0x80).  The image needs three distinct word
// slots (s 0x50, Sym temp 0x54, else temp 0x58) with &index homed at 0xb4;
// no spelling found makes MSVC stop sharing the two Symbol temps.  Also
// measured with index direct: `const Symbol &moveName = ...->Sym(1)` (the
// lever that closed HamSongMgr::InitializePlaylists) and the inverted
// `!= && !=` test with the gNullStr arm first -- both 99.9, frame 0x80,
// 6 rows (the two Symbol temps still share 0x54).
// w24-slot (99.93, unchanged): by the slot rule, the image's three distinct
// word slots mean moveName's scope overlaps the else-arm Symbol(gNullStr)
// temp's.  With block-scoped moveName and the temp in the else arm, the two
// are disjoint and MSVC reuses the slot (our frame 0x80).  No spelling with
// overlapping scopes that also keeps the image's code is known.
// w26-e (99.931, unchanged): four more spellings, all worse or equal --
// `(_D3DFORMAT)index` in both MakeStrings 75 instrs / 13 rows (the cast is
// materialised, so the call is a different shape), `SetTextToken(Symbol(
// gNullStr))` 74 / 48, `const int &flashCardIdx = index` behaves exactly as
// index-direct (frame 0x80), and a nested scope around the whole body 74 / 5.
// The three-slot frame stays out of reach from the source side.
```


## `?RepsToNextPhrase@BustAMovePanel@@AAAHXZ`

`BustAMovePanel::RepsToNextPhrase` -- moved verbatim from `src/lazer/game/BustAMovePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 568-572):

```text
// w15-b (91.57 -> 100): two plain for-loops over mSongStructure.  w7-ar's
// hand-rotated goto version with a cached `data` pointer gave MSVC a walking
// pointer for the first loop and an extra induction variable for the wrap in
// the second; written naturally, MSVC's own rotation produces the image's byte
// offset (`lwzx r8, r11, r7`) and in-loop `subf`/`addi` wrap exactly.
```


## `?Poll@BustAMovePanel@@UAAXXZ`

`BustAMovePanel::Poll` -- moved verbatim from `src/lazer/game/BustAMovePanel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 1555-1585):

```text
        // MEASURED, 2026-09-14 (lane w7-aa).  The image's shape at THIS site is
        // a single short-circuit initialiser, not the if-guard below:
        //     li r10, 0x1            <- the `true` is materialised AFTER the
        //     bne .L_82884BC8           Symbol compare, in a VOLATILE register
        //   .L_82884BC4: mr r10, r21   <- both false arms join here (r21 == 0)
        //   .L_82884BC8: clrlwi r28, r10, 24
        // Spelling it
        //     bool isPlayer0Pink = TheGameData->Player(0)->Side() == kSkeletonLeft
        //                          && GetPlayerColor(0) == "pink";
        // closes all four of those rows (idx 319/332/334/336) exactly -- and
        // costs 24 NEW rows ~250 instructions downstream, in the
        // `static DebugGraph scoreGraph(...)` argument block at L1583-1589,
        // whose two Hmx::Color temporaries rotate between the 0xb0/0xc0 slot
        // pair (stack reported as "10 PERMUTED -- same slot SET").  Net
        // 99.60107 -> 97.642.  Adding a nested lexical scope back around the
        // initialiser is EXACTLY inert, so the coupling is not scope count.
        // Whatever third spelling gives both, it is not one of these three;
        // the if-guard below is kept only because it scores higher.
        // w16-e: `!(Side() != kSkeletonLeft || GetPlayerColor(0) != "pink")`
        // as the initialiser behaves exactly like the && form (97.78), and the
        // scoreGraph white Color as the 3-arg ctor is inert.
        // w19-e: re-measured the && form: the downstream cost is exactly the
        // two scoreGraph Color temps swapping slots (image: white 0xb0, black
        // 0xc0; && form: black 0xb0, white 0xc0) plus one 0x8c reload.  `bool
        // = false; if (A && B) = true;` is 99.5 (r21/r22 swap + polarity).
        // w21-ba (still 99.60107): `Side() == kSkeletonLeft ? GetPlayerColor(0)
        // == "pink" : false` is 99.2 (r26/r28 swap + a branch); `&& !(color !=
        // "pink")` (an extra inline operator!= level) is exactly the && form
        // (97.8, same 10 PERMUTED slots), so inline depth is not the coupling;
        // the && form plus `const Symbol &color = GetPlayerColor(activePlayer)`
        // in the else-if arm is 97.0.
```

Inside the body (was lines 1673-1703):

```text
        // w26-e (99.60107, unchanged): this add is a COMMUTATIVE use list, so
        // c2rs section 8.7 says its order comes from the post-lowering sort and
        // is keyed per operand -- but in this TU the tuple never surfaces as an
        // M/T pair in the cqlo3 dump (the begin() load is folded into a
        // single-use temp, printed `T<sid>{00000000}`), so the key pair that
        // decides it could not be read here.  What is measured: the bool-mask
        // rows at L1544 and this row cannot both be closed -- the `&&`
        // initialiser that closes the former costs 24 rows downstream -- so
        // 100 is not reachable from this row alone either way.
        // RESIDUAL (lane w7-bb, 2026-09-14), 99.60107.  The FIFTH and last row of
        // Poll -- the four others are the bool-mask block noted at L1544 -- is
        // idx 729, a commutative operand swap in the address of the element
        // below: the image has `add r4, r11, r28` at 0x828851F0 (base first,
        // r11 = mSongStructure.begin() reloaded each iteration, r28 = 4*i), we
        // emit `add r4, r28, r11`.  INERT: spelling the argument as
        // `*(mSongStructure.begin() + i)`, which puts the pointer on the left of
        // the source-level addition, is byte-identical -- same 753 instructions,
        // same 5 rows, still 99.60107.  Reverted to `mSongStructure[i]` (the
        // native-safe spelling; begin() is a raw int* under stlport but an
        // iterator class under libstdc++).
        // NOT a wrong callee: objdiff's Function Call Diff pairs the target's
        // `??$MakeString@W4_D3DFORMAT@@@@YAPBDPBDABW4_D3DFORMAT@@@Z` against our
        // `??$MakeString@H@@YAPBDPBDABH@Z`, but icf_aliases.map has BOTH at
        // 0x82610090 -- one ICF fold of every 4-byte-scalar instantiation, so
        // the name is the linker's pick, not evidence that mSongStructure holds
        // an enum.  The row is not charged.
        // w21-ba: also inert (in-tree and in a standalone cl.exe probe that
        // reproduces `add r4, <4*i>, <begin>`): `mSongStructure.begin()[i]`,
        // `*(i + mSongStructure.begin())`, a `std::vector<int> &` local, the raw
        // `_M_start[i]`, MakeString hoisted to a local, the Color select hoisted
        // to a local, an unsigned loop index.
```
