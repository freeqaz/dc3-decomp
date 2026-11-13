# Lane notes: `src/system/char/CharClipDisplay.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetStartEnd@CharClipDisplay@@QAAXMM_N@Z`

`CharClipDisplay::SetStartEnd` -- moved verbatim from `src/system/char/CharClipDisplay.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 67-72):

```text
        // w21-bb: 98.75 -> 100.  Storing through a reference and reading the
        // member back by name stops MSVC forwarding the stored value, so it
        // re-reads mStartBeat (`lfs f12, 0xc(r3)` after the second lwa) as the
        // image does.  The reference name is ours; the same object is written
        // and read, so behaviour is unchanged.  Reading through the same
        // reference forwards again (93.5).
```


## `?DrawBeatString@CharClipDisplay@@QAAXPBDMABVColor@Hmx@@@Z`

`CharClipDisplay::DrawBeatString` -- moved verbatim from `src/system/char/CharClipDisplay.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 104-108):

```text
    // BUG FIX (w19-x): x is the beat's column minus 4, y is the row minus 18.
    // Image 0x823DF25C `fsubs f0, f12, f0` (f12 = mDrawPosY, 0x18; f0 = 18.0f)
    // stored to the Vector2's .y (0x54(r1)), then 0x823DF264 `fsubs f0, f1, f13` (f1 =
    // GetX(), f13 = 4.0f) stored to .x (0x50(r1)).  We had both the axes and
    // the offsets crossed (x = mDrawPosY - 4, y = GetX - 18).  RB3 agrees.
```


## `?DrawTrack@CharClipDisplay@@QAAXXZ`

`CharClipDisplay::DrawTrack` -- moved verbatim from `src/system/char/CharClipDisplay.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 164-229):

```text
    // Open residual (w7-r, 2026-09-14): the image never keeps 3.0f in a
    // register. It pins the literal-pool ANCHOR in a callee-saved GPR
    // (`lis r25, __real@40400000@ha`) and re-issues `lfs f, __real@40400000@l(r25)`
    // at all four use sites (823DF544 / 823DF5A0 / 823DF9E0 / 823DFA40:
    // trackRect.h, markerY, and the two `sEm * 3.0f` label offsets). We CSE the
    // value into callee-saved f21 instead, which is exactly the
    // GPR -1 / FPR +1 prologue delta (TGT 9/12 vs ours 8/13) and drags the
    // f-register numbering with it. REFUTED: moving `beat`/`markerRect` ahead of
    // markerY/markerH so the sub is scheduled after the loop-entry `bgt` (the
    // image's order) is exactly neutral -- 97.31, same 11 insert/delete rows.
    // The separate `lbl_82020B54` vs `__real@40000000` row is benign: that
    // .rdata word IS 2.0f, it is just pooled in CharacterTest's object.
    // w24-c3 (97.31 -> 97.8 instr-level): the marker rect's y/h are written
    // INSIDE the loop as `drawY - 3.0f` / `9.0f`, not through markerY/markerH
    // locals declared above it.  MSVC hoists the invariant subtraction to the
    // preheader AFTER the `bgt` loop guard (image 823DF5A0: `lfs f0,
    // __real@40400000@l(r25)` / `fsubs f27, f29, f0`), and no longer keeps
    // 3.0f in a callee-saved FPR there.
    // w26-v (97.817, 42 rows) -- c2rs-bridge 8.8 does NOT reach this row.
    // The lever was tried in both forms and both are byte-inert (identical
    // score and identical rows): `const float &three = 3.0f;` bound once and
    // substituted at all four sites, and `float three = 3.0f;` as the control.
    // A reference to a FLOATING-POINT LITERAL is folded straight back to the
    // literal, unlike a reference to a member expression (that is the case
    // 8.8 was established on), so there is no distinct load to keep.  Also
    // inert here: the commutative-key spelling `3.0f * sEm` on both label
    // sites, binding `sEm` to a value or a reference local, and binding `em`
    // through `halfEm`.  Worse: reading the marker height back through
    // `trackRect.h` (95.99) -- we then load 0xdc(r1) instead of the pool.
    // Not a scoring row either way: the assert's MakeString instantiation
    // differs between the objects (`<char[14],int,char[5]>` in the image vs
    // `<char[15],int,char[42]>` in ours) but objdiff does not charge it.
    // w24-c3 STOP at 97.817 (38 rows): still the 3.0f anchor/remat residual
    // above (r25 anchor + 4 reloads vs our f21), plus its f19/f20/f21 and
    // r23/r24/r25 renumbering, the label `0x64/0x14` load order, and the
    // nameColor r-channel store sunk below namePos.x (0x120 at 823DFAC8).
    // Measured inert: markerRect declared inside the loop; `if (mClip) {...}`
    // instead of the goto.  Worse: Rect ctor for trackRect (96.2).
    // decomp-synth hill_climb (2 rounds x 58 variants) found nothing.
    // w27-f (2026-11-02): multiset re-verified -- target 415 instructions, ours
    // 412, and the ENTIRE delta is the 3.0f rematerialisation: the image
    // issues four `lfs f, __real@40400000@l(r25)` where we hold the value once
    // in f21, which is also the +1 saved callee-saved FPR (__savefpr_19 vs
    // __savefpr_20) and the source of the f19/f20/f21 renumbering.  Every
    // other T-only / B-only row is register renaming or an equal-valued name
    // pair (`lbl_82020B54` IS 2.0f; `?SUBSAMPLED_HALF_WIDTH` IS 40.0f).  One
    // new spelling MEASURED better and was REJECTED: a C2RS-BRIDGE 8.8
    // scalar-product binding on the label site with `const float &labelOff =
    // sEm * 3.0f;` takes
    // canonical 97.81686 -> 98.23856 and fuzzy 97.35904 -> 97.76868 but turns
    // the image's `fmadds f13, f0, f13, f11` into a separate `fmuls`+`fadds`,
    // i.e. it buys the rows by rounding differently from the image.  Also
    // measured byte-inert: binding the two ternary members
    // (mStartBeat/mViewStartBeat), the end pair, and mDrawPosY/sEm.
    // Mechanism hypothesis (c2 priority colouring, c2-rs P_REGALLOC.md): the
    // 3.0f CSE temp is a candidate whose priority (sum of weight x n_live,
    // minus n_live where it is live but unused) comes out <= 0 in the image
    // -- live across the whole event/IK region with no use -- so it is left
    // in memory and rematerialised from the r25 anchor; ours stays > 0 and
    // gets f21.  Open question: what shortens or splits that range in the
    // image.  Related lead, not chased: every 2.0f in this TU (LineSpacing,
    // DrawBlend, the start label here) loads lbl_82020B54, a NON-COMDAT
    // .rdata word just below this object's split start (0x82020B58), never
    // __real@40000000 (0x820E6390) -- so the original likely had a TU-local
    // const float object for 2.0, and maybe for other constants too.
    // Draw integer beat markers
```

Inside the body (was lines 297-302):

```text
            // w22-a07 BUG FIX: the right-foot-only case used to declare its own
            // block-local `data` and `goto` past the left arm's initializer, so
            // the IK readout read an uninitialized stack slot (ours:
            // `lwz r11, 0x50(r1)`).  The image loads rightIk->GetData() there:
            // `lwz r11, 0xd4(r28)` at 0x823DF880 (r28 = rightIk), reached from
            // `bne cr6, 0xb88` right after the right.ikfoot Find.
```
