# Lane notes: `src/system/hamobj/HamNavList.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@HamNavList@@UAAXXZ`

`HamNavList::Poll` -- moved verbatim from `src/system/hamobj/HamNavList.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 487-492):

```text
            // w16-b: bind the MEMBER, not the SlideSoundAnim() accessor's
            // result. The accessor was one inline level too many: it homed its
            // computed `this` (`addi r11, r3, 0x384` / `stw r11, 0x50(r31)`)
            // on top of the ObjPtr conversion temp the image does home
            // (`stw r3, 0x50(r31)`). Same at all four sites (Poll x3,
            // SetSelecting x2 -- both functions now 100).
```


## `?GetDisabledCount@HamNavList@@ABAHH@Z`

`HamNavList::GetDisabledCount` -- moved verbatim from `src/system/hamobj/HamNavList.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 949-953):

```text
// w20-d BEHAVIOUR FIX: the 0x313 assert tests the DISABLED counter, not the
// display index.  Image: `cmpwi cr6, r28, 0x0` where r28 is the counter that
// `li r28, 0x0` seeds and the two `addi r28, r28, 0x1` advance, and that is
// returned via `mr r3, r28`; we compared the parameter (r31) instead.  The
// assert text "count == 0" names the counter `count`.
```


## `?GetTargetSwellAmount@HamNavList@@AAAMH@Z`

`HamNavList::GetTargetSwellAmount` -- moved verbatim from `src/system/hamobj/HamNavList.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 1015-1021):

```text
        // w21-k (98.07 -> 100): early-out on the non-selected row, then the
        // positive kRibbonSwell test.  w7-av certified a 6-row block-order
        // floor here with the nested `if (display == selDisplay) { if (mode !=
        // kRibbonSwell) return 1.0f; return GetFrame(); } return 0.0f;`; the
        // positive test alone was 92.30 because the shared 1.0f/0.0f tails moved.
        // With the 0.0f return written first MSVC lays out GetFrame, then the
        // shared 1.0f block, as the image does.  Same results on every path.
```

Inside the body (was lines 1352-1356):

```text
        // The ObjPtr member itself (friend access), not the SlideSoundAnim()
        // accessor: see Poll (w16-b). The `if` runs ObjPtr's inlined
        // conversion, which is what puts the result in the 0x50 temp slot at
        // 824487C4; the accessor added a second home store (w7-ag's "one inline
        // level more than the image").
```

Inside the body (was lines 1565-1591):

```text
// w21-r (98.36 canonical, 2 rows): behaviour checked against the image --
// every branch lands on the same block (loop exits, ScrollUp/ScrollDown and
// the confirm path all reach the one `stw r26, 0x4(ret)` DataNode(0) block,
// kDataUnhandled stores type 6).  Reading FirstShowing() at both compares
// (no local) fixed the image's `add r11, r11, r10` operand order.  Remaining:
// the shared DataNode(0) block sits after SetHighlight in the image and after
// ScrollUp here (1 insert + 1 delete), plus an r27/r28/r29 rotation (this /
// dir / return slot) that normalized forgives.  Measured, all no better:
// early returns in the scroll arms (98.4, regs worse); `if
// (!ScrollPastMinDisplay())` first (byte-identical); a while(true)/break
// loop (96.7); no bool locals (95.1, loses the image's 0/1 materialisation
// of the gesturing test); a static inline gesturing helper (identical); one
// shared `return DataNode(0)` after an `else if (Confirm)` chain
// (identical); a combined `ScrollPastMinDisplay() && (< || >=)` test (90.1).
// A `goto` to one shared SetHighlight fixes the whole register rotation
// (fuzzy 97.4 -> 98.1) but not the block, so it was not kept.
// w21-ak (98.36 -> 100.0 canonical, fuzzy 99.26): the block WAS a spelling --
// the two loop exits are one `selected < 0 || selected >= NumShowing()` test.
// With two separate `return DataNode(0)`s MSVC kept the merged DataNode(0)
// block after ScrollUp; with one it sits after SetHighlight as in the image.
// w25-gi (fuzzy 99.26 -> 100): the r27 this / r28 dir / r29 return-slot
// rotation was the guard spelling. Each early `return DataNode(kDataUnhandled,
// 0)` is one more reference to the hidden return-slot pointer (the copies are
// cross-jumped back into one block after colouring, so the code is the same):
// with five guard clauses its colour priority goes -22 -> 8, above dir (-1),
// while `this` stays at -4. Early returns in the scroll arms (tried by w21-ak)
// also raise `this` above dir, and they move the DataNode(0) block.
```

Inside the body (was lines 1621-1646):

```text
                // NOT `- 1`.  The image computes the threshold with a
                // single add and no subtract -- `lwz r10,
                // sNumListSelectable` / `add r11, r11, r10` (0x5824) /
                // `cmpw cr6, r31, r11` (0x5828) / `blt` -- so the
                // ScrollDown edge is firstShowing + sNumListSelectable,
                // not one item earlier.  Our `- 1` was an invention and
                // it showed up as a `subi r11, r11, 0x1` the image does
                // not have; it made the list scroll down one item too
                // soon on the controller path.
                // The commutative row (target `add r11, r11, r10`) was
                // closed by w21-r: read FirstShowing() at both compare
                // sites instead of through a local (see the note above
                // the function).  Swapping the sum's operands alone is
                // byte-inert.
                // Landing this once broke 8 DtaFlow tests and it was
                // reverted (54365d0c3) on the theory that the `- 1`
                // compensated for a native scroll divergence.  There was
                // none: instrumented per press, native computes exactly
                // what this listing does.  The flows were counting on
                // the bug -- song select enters on index 2 (the
                // song_tier_0 header; headers are active rows), four
                // downs reach index 6 (the song_tier_1 header), and only
                // the early `- 1` edge made the fourth press ScrollDown
                // and hop the cursor over it to starships.  The input
                // scripts now use one down (-> ymca, index 3), and
                // DtaFlowSongSelectScrollTest pins this edge.
```

Inside the body (was lines 1785-1813):

```text
    // ILP32 (Xbox 360) version
    // NumShowing() is passed straight to IsScrollable, with no named local:
    // that local was one more negative-benefit candidate freed before the
    // code band, so the mElements-begin reload piece (11th code-band mint)
    // took a recycled low id and was coloured after the ribbonStates-begin
    // piece, giving the r10/r11 pair on the mElemDrawState store in reverse.
    // With 10 frees it gets a fresh id, pops first and takes r11 as the image.
    // w7-by (85.72059 -> 100.0).  Four levers, each reproducing a listing site:
    //  - `resize(n, HamListRibbonDrawState())`, not a named `defaultState`:
    //    8244E0C8 `mr r5, r3` takes the ctor's return, a named local re-derives
    //    `addi r5, r1, 0x60`.
    //  - `mListState.` read directly, not through a function-scope reference:
    //    8244E0E4 `addi r24, r27, 0x70` sits AFTER the `ble` at 8244E0E0, and
    //    8244E154 loads mFirstShowing as `lwz r26, 0xa0(r27)` off `this`; a
    //    hoisted `auto &` binds the address before the resize and reads it as
    //    0x30(r24).
    //  - if/else with the mActive store in BOTH arms, not a ternary: the image
    //    has two `bctrl` IsHeader sites (8244E174, 8244E198), each followed by
    //    its own `add r11, r31, rN` before the shared `stb r3, 0x24(r11)` at
    //    8244E1A0; the ternary merges them into one bctrl.
    //  - ONE `mElementState = kUIListWidgetActive` store site (8244E278) with the
    //    two rejecting tests written as `continue`: two store sites are two zero
    //    uses inside the loop, and MSVC then hoists the constant into a callee-
    //    saved register (`li r20,0` + `mr r28/r31/r23, r20`, `mr r11, r20`,
    //    `stw r20`) and saves r16; the image rematerialises `li r28/r31/r23, 0`
    //    at 8244E0D8/8244E0EC/8244E0F0, `li r11, 0` at 8244E25C and `li r10, 0`
    //    at 8244E26C, saving r17.  Inert: signed `widgetElemCount`; inlining the
    //    controller-mode test drops the bool diamond the image has at
    //    8244E254..8244E260 (91.7).
```
