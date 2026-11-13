# Lane notes: `src/system/hamobj/HamListRibbon.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawRibbon@HamListRibbon@@AAAXHABVTransform@@0ABUHamListRibbonDrawState@@HHH_N@Z`

`HamListRibbon::DrawRibbon` -- moved verbatim from `src/system/hamobj/HamListRibbon.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 434-444):

```text
        // The image materialises the enumerator with a real BRANCH --
        //   0x82481570 li r4, 0x1 / 0x82481578 lbz r11, 0x14(r24) / cmplwi /
        //   ... the three vtable loads ... / 0x82481594 bne / li r4, 0x0 / bctrl
        // -- which is NOT a select: it is TWO virtual calls, one per arm,
        // tail-merged by MSVC into a single bctrl with only r4 differing.  The
        // value forms are all if-converted or folded instead (w7-as, 2026-09-14):
        //   `state.mSelected ? kFocused : kNormal`        -> subic/subfe, 92.5
        //   `State s = kFocused; if (!sel) s = kNormal;`  -> subfic/subfe/and, 93.4
        //   `(UIComponent::State)(int)state.mSelected`     -> lbz straight into r4, 94.3
        // The duplicated call pairs all four rows and also removes the r24/r27
        // callee-saved rotation those spellings caused (w7-bv: 95.0 -> 96.9).
```

Inside the body (was lines 473-482):

```text
            // BUG FIX (w7-as, 2026-09-14): this wrote the label alpha into
            // `mData` (offset 0x38, an int) via memcpy.  The image stores it as
            // a FLOAT into offset 0x24, which is `mAlpha`:
            //   0x824816A0  lwz  r11, 0x18(r24)
            //   0x824816A4  stfs f1,  0x24(r11)
            // -- a bare `stfs`, so the destination is a float, and 0x24 is the
            // only float at that offset.  We wrote 0x38 and had to round-trip
            // the value through a stack slot (`stfs f1, 0x50(r1)` /
            // `lwz r10, 0x50(r1)` / `stw r10, 0x38(r31)`), which is also where
            // our extra 0x10 of frame came from.
```

Inside the body (was lines 486-493):

```text
            // A ternary of the two statics, tested `!= 0.0f` so that sBigScale is
            // the fall-through arm: 0x824816A8 `mr r11, r29` (sBigScale) sits
            // AFTER the mAlpha store, then `fcmpu / bne / 0x824816B8 mr r11, r30`.
            // The pointer-variable form (`Vector3 *scale = &sBigScale; if (==
            // 0.0f) scale = &sNormalScale;`) hoists its `mr` above the store and
            // hands the two statics r30/r29 instead of r29/r30 (9 rows, w7-bv);
            // `== 0.0f ? sNormalScale : sBigScale` fixes the registers but
            // inverts the branch (beq for bne).
```

Inside the body (was lines 499-506):

```text
    // Initialised, and initialised HERE: the image keeps savedAlpha in f30,
    // the register that already holds the 0.0f literal from 0x824814C8
    // (SetFrame's first argument and the mBigScale compare), so `= 0.0f`
    // costs no instruction -- the else path simply leaves f30 alone.  Left
    // uninitialised, MSVC homes the variable at 0x50(r1), loads it back on
    // the else path (`lfs f31, 0x50(r1)`) and the frame grows to 0x110
    // (94.1 -> 95.0, w7-bv).  Declared at the top of the function the literal
    // is materialised there instead of at 0x824814C8 (95.9).
```

Inside the body (was lines 509-516):

```text
        // Read the FIELD, not the inline accessor.  `Style(0).GetAlpha()`
        // returns through an inline temporary and MSVC sinks the load to the
        // block end: `mr r11, r3 / mr r3, r31 / lwz r30 / lfs f30, 0x24(r11)`
        // (97.8).  The field read is `lfs f30, 0x24(r3)` straight off the
        // const Style() return at 0x8248170C, and the total-alpha `fmr f31, f1`
        // then lands after `mr r3, r30` as at 0x82481724.  A named
        // `const HamLabel *` and a named `const RndText::Style &` are both
        // inert (w7-bv).
```

Inside the body (was lines 519-523):

```text
        // The ObjPtr accessor's rvalue is homed in r30 across the argument
        // call (`lwz r30, 0x31c(r31)` at 0x82481714, `mr r3, r30` after it),
        // exactly as a named `HamLabel *label` is; only a NAMED
        // `float totalAlpha = GetLabelTotalAlpha();` made MSVC re-load 0x31c
        // afterwards (w7-as).
```

Inside the body (was lines 527-532):

```text
    // RESIDUAL (w7-bv, 4 register rows, 100.0 canonical / 99.7 raw): the
    // image gives worldXfm r26 (0x82481468 `mr r26, r6`) and inRange r25
    // (0x824814A0 `clrlwi r25, r11, 24`); we allocate them the other way
    // round.  Both die before the label block (Multiply at 0x82481514, the
    // clrlwi. at 0x82481544) and every other callee-saved assignment matches.
    // Moving `Transform tempXfm;` above inRange is inert.
```


## `?Draw@HamListRibbon@@QAAXABVTransform@@ABV?$vector@UHamListRibbonDrawState@@V?$StlNodeAlloc@UHamListRibbonDrawState@@@stlpmtx_std@@@stlpmtx_std@@_N2@Z`

`HamListRibbon::Draw` -- moved verbatim from `src/system/hamobj/HamListRibbon.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 600-605):

```text
    // Indexed through a reference to the vector: c2 sorts `add r4, r29, r11`'s
    // operands by key, and indexing drawStates directly leaves begin() as a
    // variable leaf (sid 397, key 0x131a0) that outranks the strength-reduced
    // i*40 temp (key 0x12a40), giving `add r4, r11, r29`.  Through `states`
    // the begin() variable is minted earlier (sid 334, key 0x129c0) and the
    // offset temp sorts first, as in the image (w25-gu).
```

Inside the body (was lines 614-623):

```text
    // BUG FIX (w7-as, 2026-09-14): the two arms were swapped.  The image tests
    // `scrollable` and takes the half-2 / clamp-mTestSelectedIndex path when it
    // is TRUE, and only resets the scroll animation when it is FALSE:
    //   0x82483738  cmplwi cr6, r25, 0x0      ; r25 = (numItems > 6)
    //   0x8248373C  beq    cr6, .L_8248376C   ; NOT scrollable -> mScrollAnim
    //   0x82483740  srawi  r11, r23, 1        ; scrollable -> numItems/2 - 2
    //   ...
    //   0x8248376C  lwz    r3, 0x208(r28)     ; mScrollAnims.mScrollAnim
    // We clamped the selected index for short lists and reset the scroll anim
    // for long ones -- exactly backwards.
```

Inside the body (was lines 663-681):

```text
    // (CLOSED by w21-t, see the if/else advance in the loop; the history below
    // is kept for the refuted spellings.)
    // RESIDUAL (w7-as, 96.2 canonical; w7-bm 96.8, see the loop): what is left is one block-placement
    // difference plus its register knock-on.  The image parks the
    // `inRange ? mSpacing : mPaddedSpacing` select at 0x82483890, i.e. BELOW the
    // `if (!mSelected)` body at 0x824838A0, and branches back up to it from all
    // three predecessors (`b 0x360` at 0x8248389C and 0x824838C4); it also keeps
    // `ribbonXfm.v.z` cached in f30 across the loop, reloading it only after the
    // DrawRibbon call (`lfs f30, 0xb8(r31)`).  We lay the select out after the
    // body and reload v.z into f13 at the subtraction.  The `stb r26, 0x74(r31)`
    // at index 70 is a base-only home store for `scrollable` (no target
    // instruction references 0x74(r31) at all); `int scrollable` instead of
    // `bool` removes it but costs 5.6pp of register allocation -- 96.2 -> 90.6.
    // w16-b (97.48 -> ~97.6, 13 -> 12 rows): dropping the named `scrollable`
    // and spelling `numItems > 6` at both uses is 87.0 (refuted). Polarity
    // flips of the loop body (`if (==) {body; tail} else {tail}`, and an empty
    // then-arm with one shared tail) measured 95.8 and 89.0: the image's
    // single select copy at the compare's fall-through, with both active arms
    // `b`-ing back up to it, is still not reproduced.
```

Inside the body (was lines 687-705):

```text
        // 96.2 -> 97.5 canonical (w7-bm, 2026-09-15). Two levers: (1) writing
        // the spacing advance on the inactive path as well (this `continue`
        // block) lets MSVC cross-jump the two copies and drops the r26/r27
        // knock-on (8 rows): 96.8. It keeps the copy AFTER the arms as the
        // holder, so the image's placement (select at 0x824838B0, both arms
        // `b` back up to it) is still not reproduced: 1 diff_op + 3/4
        // insert/delete remain. Same 96.8 as an explicit if/else with the
        // tail in both arms; three single-predecessor copies (tail in each of
        // the three arms) is 94.6; a `goto`-labelled tail before the arms with
        // the arms jumping back is canonicalised straight back to the 96.2
        // layout. (2) `selectedIdx = i` BEFORE the Transform copy matches the
        // image's `mr r25, r30` ahead of `bl memcpy` (0x82483908): 97.5.
        // `int scrollable` re-measured on this state: 91.9, still refuted.
        // w21-t (97.68 -> 100): the advance is an if/else on inRange with the
        // subtraction in each arm, written on BOTH paths.  Cross-jumping then
        // keeps the inactive path's test + mSpacing arm at the compare's
        // fall-through (0x824838B0) and merges the mPaddedSpacing arm and the
        // fsubs tail below the body, which every ternary `step` spelling
        // (w7-as, w7-bm, w16-b) could not reproduce.
```
