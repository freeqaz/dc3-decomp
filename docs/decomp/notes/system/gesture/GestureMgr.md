# Lane notes: `src/system/gesture/GestureMgr.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0GestureMgr@@AAA@XZ`

`GestureMgr::GestureMgr` -- moved verbatim from `src/system/gesture/GestureMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 63-67):

```text
    // w14-f: 97.74 -> 100.  A plain indexed loop; MSVC strength-reduces it to
    // the image's four walking pointers itself.  The hand-reduced version
    // (unk30 - 1 / mIdentityInfos - 4 pre-increment pointers) let MSVC CSE
    // `this + 0x2c` with the SkeletonCallback subobject pointer, where the image
    // re-derives it (`addi r24, r30, 0x2c` at 8242C3A4).
```


## `?PostUpdate@GestureMgr@@UAAXPBUSkeletonUpdateData@@@Z`

`GestureMgr::PostUpdate` -- moved verbatim from `src/system/gesture/GestureMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 268-279):

```text
        // The same helper as below, but through `this` (8242C804 `addi r10, r25,
        // 0xacc`), so the inlined search falls through to 8242C824 `li r11, -0x1`.
        // Open-coding the loop with `idx = i; break;` emits a branch there instead.
        // w19-c (99.0 -> 100 modulo register permutation): name the full
        // object once.  PostUpdate is entered through the SkeletonCallback
        // subobject (this+0x2c); the image materialises `subi r29, r25, 0x2c`
        // at the TOP of this block (before the inlined search), where calling
        // GetSkeleton through `this` sinks it to the call.
        // w25-gm: the explicit `<= 0` test (the helper's own guard, made
        // visible) lets the no-ID path branch straight to the scan loop
        // (8242C80C `ble cr6` -> `li r30, 0`); through the helper alone the
        // branch lands on its `li r11, -0x1` instead.  Same predicate.
```

Inside the body (was lines 304-313):

```text
        // leftID / rightID are updated in place (one variable each, two live
        // ranges) rather than copied into separate nextLeft / nextRight
        // locals: with separate locals the copies were their own colour
        // candidates and nextRight (pri -6) took r28 ahead of rightIdx, which
        // rotated this/leftID/rightID/rightIdx/displacedLeft/6 over r23-r28
        // (w25-gm, c2rs cands tap).
        //
        // The image keeps the ID that leftID DISPLACED in its own variable, seeded
        // to -1 (8242C8F0 `li r26, -0x1`) and written only on the found path
        // (8242C968 `mr r26, r24` immediately before `mr r24, r11`).
```


## `?DrawSkeletonKinectData@GestureMgr@@QAAXXZ`

`GestureMgr::DrawSkeletonKinectData` -- moved verbatim from `src/system/gesture/GestureMgr.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 605-664):

```text
                    // NEGATIVE RESULT (w7-ap, 2026-09-14, 96.0 canonical): the
                    // residual 4 rows here (image `addi r3, r3, 0x110` then
                    // `lwz r11, 0x0(r3)` with the int argument loaded first;
                    // ours folds the TextHolder base adjust into
                    // `lwz r11, 0x110(r3)`) are NOT reachable by naming the
                    // adjusted pointer -- `TextHolder *th = idLabel;` at both
                    // SetInt sites is byte-identical.  Likewise swapping the
                    // leftShoulder/rightShoulder declarations to chase the one
                    // remaining PERMUTED slot (image right@0x90, left@0xc0) is
                    // byte-identical.  The rest is the constant-pool lis
                    // shuffle, one register wide, downstream of nothing we can
                    // name from source.
                    // NEGATIVE RESULT (w7-bv, 2026-09-15, still 95.9 canonical /
                    // 46 rows): the image loads the vtable THROUGH the adjusted
                    // pointer after the arguments (0x8242A0F8 `addi r3, r3,
                    // 0x110` / `lwz r4, 0xaac(r27)` / `li r5, 0` / `lwz r11,
                    // 0x0(r3)`), and the epilogue at 0x8242A360 copies mDebugDir
                    // (`clrrwi r11, r11, 0`) before `addi r3, r11, 0x9c`.  Every
                    // way of naming that conversion in the TU is inert or worse:
                    // `TextHolder &th = *idLabel; th.SetInt(..)` and
                    // `static_cast<TextHolder *>(indexLabel)->SetInt(..)` are
                    // byte-identical to the plain call; a `TextHolder *`-typed
                    // local with the null test on IT (and `RndDrawable *` for the
                    // epilogue) makes MSVC emit its own null-preserving adjust
                    // AND the user test (`beq / li r3, 0 / cmplwi cr6 / beq`),
                    // 93.6; `RndDir *dir = mDebugDir;` after the loop is inert.
                    // For the prologue, hoisting `int i;` above textPos so the
                    // counter's init precedes the 0.2f constant is inert too:
                    // the r28/r29 vs r9 assignment of "(active)"/"not tracked"
                    // and the 0.0f/1.0f pool registers do not move.
                    // w21-ag (98.8 canonical, 6 rows, all in the two SetInt
                    // calls below): the image's shape -- `addi r3, r3, 0x110`,
                    // args, then `lwz r11, 0x0(r3)` -- is exactly what MSVC emits
                    // for `Find<UILabel>(..)->SetInt(..)` with NO null test in
                    // between (standalone cl.exe probe, and the image's own
                    // BustAMovePanel/HelpBarPanel Find(..,true)->SetX sites); any
                    // tested pointer folds the vtable load to `lwz r11,
                    // 0x110(r3)`.  Probed and inert/worse: if-init declaration,
                    // `!l goto`, `l && (..)`, TextHolder*/TextHolder& locals, the
                    // argument hoisted into a local, an inline
                    // SetLabelInt(TextHolder*/UILabel*, int) helper (the
                    // TextHolder* one re-creates the null-preserving select).
                    // The epilogue closed (96.0 -> 98.8) by reading mDebugDir
                    // directly -- see the end of the function.
                    // w27-a (98.7516 canonical / 98.6894 fuzzy, 6 rows, 324
                    // instructions, per-object path EXACT for this unit):
                    // re-read both sites against the image -- the null test is
                    // present on BOTH sides (`cmplwi r3, 0x0` / `beq`), the
                    // vtable slot is the same (0x8 of TextHolder's vtable) and
                    // the two arguments are the same values, so this is not an
                    // added-guard or wrong-callee row.  The only delta is the
                    // scheduling of three instructions: the image materialises
                    // the 0x110 sub-object adjust first (`addi r3, r3, 0x110`),
                    // then loads the int argument, then reads the vtable
                    // through the adjusted base (`lwz r11, 0x0(r3)`); we fold
                    // the adjust into the vtable load (`lwz r11, 0x110(r3)`)
                    // AND re-issue it as the addi for the call, and sink the
                    // argument behind the vtable load.  Open question: which
                    // spelling stops c2 re-materialising the adjusted address
                    // after it has folded it into the load.
```

Inside the body (was lines 740-744):

```text
    // w21-ag: the member itself, twice, not the `debugDir` reference: the
    // image's `clrrwi r11, r11, 0` copy before `addi r3, r11, 0x9c` is MSVC
    // CSE-ing a re-read raw member (the w21-u shape), and dropping the
    // reference's last use here also put the prologue's constant-pool
    // registers back on the image's (96.0 -> 98.8).
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: load
scheduling only.
