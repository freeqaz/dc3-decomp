# Lane notes: `src/system/rndobj/Text.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0StyleState@RndText@@QAA@PAV1@M@Z`

`RndText::StyleState::StyleState` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 197-204):

```text
// `inline` matches the map: ??0StyleState@RndText@@QAA@PAV1@M@Z @82690d78 is
// `f i` (a COMDAT), and every other RndText method we define out-of-line here is
// bare `f`.  MEASURED (w8-p 2026-09-30): the change is inert for every caller --
// WrapText stays 95.35407, both SetupCharacter overloads 96.00671 / 97.77953,
// DrawShowing 98.80604, ConstructMeshes 90.94355 -- so it is a linkage-fidelity
// fix, not a lever.  Kept because the map says the image did it; do not read the
// inertness as evidence the flag does not matter (it took PatchVerts::HasVert
// from 81.4 to 100 in rndobj/Mesh.cpp).
```


## `?Load@RndText@@UAAXAAVBinStream@@@Z`

`RndText::Load` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 298-325):

```text
    // NOTE: `style` MUST be declared before `font`. Swapping the two
    // declarations costs 5.6pp (99.4 -> 93.8): it moves the whole frame by
    // 0x10 and de-schedules the TEXT_REV block. The target's slot order is
    // style-then-font.
    // w16-a (99.56): what is left is a frame 0x10 SMALLER than the image's
    // (0x1b0 vs 0x1c0; it predates the SetFixedLength(0) fix below) and the
    // resulting slot shuffle, plus one `cmpwi r21, 0xd` scheduled two rows
    // late.  With SetFixedLength(0) in place, font-before-style is still a
    // loss (94.0).
    // w26-i: re-read every row; the non-offset residual is one instruction
    // placement.  The image issues `cmpwi cr6, r21, 0xd` twice (idx 443 for
    // `d.rev < 13`, idx 446 for `d.rev > 13`); we evaluate the same two
    // compares but schedule the second one after the fdivs (idx 450), so both
    // sides compute the same two predicates on the same value.  Everything
    // else is the liveness-ordered slot map: the target's `vec` (idx 237) sits
    // at 0x68 and ours at 0x70, the 3-int temp block moves with it, and the
    // 0x8c/0xac pair is swapped.  Naming the Vector3 temporary of the d.rev<2
    // branch and splitting `int i, j, k` into three declarations are inert
    // (canonical unchanged), so the frame delta is a slot-shape difference,
    // not a missing local.
    // w32-a: the relocation-symbol multiset is equal once ICF folds are
    // resolved against retail's own map.  Our `operator>>(BinStream&,
    // Vector2&)` and the image's `operator>>(BinStream&, Key<float>&)` are the
    // SAME address (0x826ee638, hamobj:DancerSequence.obj / rndobj:CamAnim.obj),
    // and our `operator>>(BinStream&, Hmx::Color&)` and the image's
    // `operator>>(BinStream&, Hmx::Rect&)` are both 0x82628210 (rndobj:Mesh.obj
    // / rndobj:Cam.obj).  Those two rows are therefore fold artifacts, not
    // wrong callees, and objdiff scores them equal for that reason.
```

Inside the body (was lines 407-411):

```text
            // w16-a: SetFixedLength(0), inlined here (and only here: the
            // constant makes its loop dead).  The image materialises `this`
            // for the store -- `subi r11, r30, 0xd0` / `stw r19, 0x2c(r11)` --
            // which a hand-written `if (mFixedLength) mFixedLength = 0;` does
            // not.  Same behaviour.
```


## `?ConvertTextToWide@RndText@@IAAHPBDAAV?$vector@GV?$StlNodeAlloc@G@stlpmtx_std@@@stlpmtx_std@@@Z`

`RndText::ConvertTextToWide` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1313-1318):

```text
    // w13-a (91.9 -> 100): the temp-heap bracket is a MemDoTempAllocations
    // scope, not bare MemPushTemp()/MemPopTemp() calls.  Its destructor gives
    // the function an EH state, which is what dedicates r31 to a frame
    // pointer (8269A004 `subi r31, r1, 0xb0`) and makes the image save r24-r31.
    // With the scope in place, the count computed before MemPushTemp
    // (8269A030-8269A034) is a named `len`.
```


## `?ReplaceMissingCharacters@RndText@@IAAXAAV?$vector@GV?$StlNodeAlloc@G@stlpmtx_std@@@stlpmtx_std@@@Z`

`RndText::ReplaceMissingCharacters` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1427-1432):

```text
                    // w26-f6: the declaration-order handle above is REFUTED --
                    // swapping count/j so j is minted first rebuilds to the same
                    // 99.71098 / 100.0, so the mint order does not reach the pop
                    // order.  Also measured: declaring p before the StyleState
                    // local is worse (99.147), and taking p from
                    // wideChars.begin() is inert.
```

Inside the body (was lines 1505-1512):

```text
                    // w7-bj: `missing.begin()`, not `mapIt->second.begin()`.
                    // Re-deriving the set from the iterator here made MSVC anchor
                    // a second pointer (`addi r28, r15, 0x10` = &*mapIt) for the
                    // begin load, which cost a callee-saved register and evicted
                    // the function-wide zero register the image keeps in r14
                    // (0x82699818): 97.51 -> 98.27; with `curChar = 0` ahead of
                    // the replacements array, pluralS as a conditional expression
                    // and qp taken inside the size test, 100.0 (regperm only).
```


## `?QueueBlacklightPacket@RndText@@KAXPAVRndMesh@@MH@Z`

`RndText::QueueBlacklightPacket` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 1727-1731):

```text
    // w25-gb: the count is read into a named local BEFORE capacity().  That
    // makes the reloads after resize() come out count-first, then the pool's
    // mStart, as in the image (it was the other way round when capacity() was
    // read first).  The packet count is still re-read for the post-increment,
    // since resize() is an opaque call.
```


## `?FitTextScroll@RndText@@IAAXXZ`

`RndText::FitTextScroll` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 2166-2176):

```text
        // w21-x: 98.276 -> 100 (all 232 rows equal).  This is the same
        // style-font fallback every other RndText site writes
        // (`s->mFont ? s->mFont : mStyles[0].mFont`, see ParseMarkup and
        // UpdateText), here with the style pinned to index 0.  The image keeps
        // both ternary arms: the condition reads 0x40(r11) and the else arm
        // re-evaluates mStyles[0] (its dead `stw r11, 0x54(r31)` home), then
        // the join reads the pointer again -- the "two reads" earlier lanes
        // could not reproduce with a plain `font = mStyles[0].mFont` (98.276),
        // an ObjPtr reference (95.6), `.Ptr()` or a `Style &` binding (inert).
        // Writing both arms as mStyles[0] lets MSVC merge them (98.9).  Same
        // value on every path: both arms name mStyles[0].mFont.
```


## `?ConstructMeshes@RndText@@QAAXABV?$vector@VLine@RndText@@V?$StlNodeAlloc@VLine@RndText@@@stlpmtx_std@@@stlpmtx_std@@ABVRect@Hmx@@M@Z`

`RndText::ConstructMeshes` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 2316-2329):

```text
    // Allocate meshes for each font map
    // w30-c (2026-11-02): the image evaluates this loop's condition TWICE --
    // `lwz r30, 0xa8(r3)` / `lwz r10, 0xac(r3)` / `cmplw cr6, r30, r10` /
    // `beq` in the prologue, then a body that reloads the end each iteration
    // (`lwz r11, 0xac(r31)` / `cmplw` / `bne`).  A branch-to-bottom `for`
    // produces only the second one, and the explicit rotation
    // `it = begin(); if (it != end()) do { ... } while (it != end());` is inert
    // because MSVC un-rotates it (measured, 90.94 unchanged).  What DOES
    // survive is a guard whose condition is a separate evaluation of the
    // container -- `begin() <op> end()` -- because the rotation-cancelling pass
    // cannot merge it with the loop's own `it != end()` test.  That reproduces
    // the image's second compare: 90.94 -> 92.56 canonical.  The same trick is
    // NOT available to the CleanupSyncMeshes loop at the end of this function,
    // where the image is branch-to-bottom on both sides.
```

Inside the body (was lines 2357-2366):

```text
            // w30-c (2026-11-02): the same DOUBLE EVALUATION as the font-map
            // loop above.  The image has `lwz r10, 0x4(r28)` / `cmplw cr6,
            // r30, r10` / `beq` before the body and the usual `cmplw` / `bne`
            // at the bottom.  The guard must be spelled on the RANGE
            // (`mStart`), not on `cur`: `if (cur != line.mEnd) do {...} while
            // (cur != line.mEnd)` is un-rotated straight back into a plain
            // while (measured: 92.56, unchanged), while the `mStart` spelling
            // survives and reproduces it (92.56 -> 94.66 canonical).  This is
            // behaviour-identical -- `cur` is `line.mStart` on entry, and the
            // bottom test runs the body zero or more times exactly as before.
```

Added by lane w-low-rndobj, 2026-11-13:

```text
BEHAVIOUR VERDICT: AGREES (94.66 / 94.06, unchanged).  mConstructScale 0xc8,
mNumLinesRendered 0xc4 = size (stride 0x14), mBounds 0xb4..0xc0 word copy;
AllocateMeshes (vtbl 0x28) with mFixedLength 0x2c; font test mStyles[0] +0x40;
per line yPos 0x10 by value and xStart 0xc by reference; '<' tested before
mMarkup 0x30; ParseMarkup(cur, state, ch&) and the conditional cur--;
SetupCharacter (vtbl 0x30) args ch, &xPos, yPos, state, prevChar, mCircle
0x18, mFitType 0x20, mIndentation 0x64 in the image's register order;
CleanupSyncMeshes (vtbl 0x2c) over every font map.
```


## `?ParseMarkup@RndText@@IAAPBGPBGAAVStyleState@1@AAG@Z`

`RndText::ParseMarkup` -- moved verbatim from `src/system/rndobj/Text.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 2690-2698):

```text
// w21-x: 98.7952 -> 100 (all 166 rows equal).  The earlier 31-row residual
// (this in r29 instead of r30, and the fontName home stored before the
// vbptr load of the inlined Name()) came from spelling the font check as a
// three-armed if/else that assigned a `fontName` local.  The original is one
// combined test with the "NULL" fallback as a ternary inside the
// MILO_NOTIFY arguments; MSVC jump-threads the second `font` test away, so
// the block shape is identical and only the allocation order changes.
// Same behaviour on every path: null font -> "NULL"; a non-RndFont font ->
// its Name(); an RndFont -> next style.
```

Inside the body (was lines 2762-2772):

```text
            // Whole-Color copy, alpha included: the image moves all four words
            // with lwz/stw (0x826992F0..0x82699330) and leaves one dead
            // `addi r11, r3, 0x2c` -- the CSE'd &GetColor() -- at 0x82699308.
            // NEGATIVE RESULT (w7-ax): spelling it as four per-field float
            // assignments turns those lwz/stw into lfs/stfs and costs 0.1pp;
            // the image's copy is a struct assignment.  The only residual here
            // is that MSVC INTERLEAVES the four load/store pairs through one
            // temp register (r11) while we batch four loads into r11/r8/r7/r10
            // first -- a scheduling choice, not a spelling one, and it survived
            // both spellings.  The RESTORE loop below is deliberately
            // asymmetric and puts back only red/green/blue.
```

Inside the body (was lines 2789-2795):

```text
                    // NEGATIVE (w7-bx): the image interleaves the mDirty RMW
                    // inside this 4-word copy (lwz 0x228 at 0x8269940C between
                    // the blue load and store, stw 0x228 at 0x8269941C before
                    // the alpha store); ours keeps copy-then-RMW.  Writing
                    // MarkDirty(1) first keeps RMW-then-copy instead (97.3):
                    // MSVC preserves the source order of these memory ops, so
                    // the image's copy is not an opaque block move.
```

Inside the body (was lines 2846-2873):

```text
                // mLineHeight / mScrollCopies, NOT mStyles[0].mSize / 0.  The
                // image loads `lfs f1, -0xb4(r28)` and `lwz r5, -0xb0(r28)`
                // straight off `this` at 0x826994E4 and 0x826994F4 (object
                // offsets 0x58 and 0x5c, since the body's `this` is
                // object+0x10c).  Those are the marquee repeat spacing and copy
                // count that FitTextScroll sets -- mLineHeight is a misnomer,
                // it is assigned mTotalWidth for a wrapping marquee and 0.0f
                // otherwise, right beside mScrollCopies.  Passing a literal 0
                // copy count meant DrawMesh's repeat loop never ran, so a
                // marquee drew exactly one copy and left a gap instead of
                // tiling across the label.
                // The image hoists `lis sBlacklightModeEnabled` into r27 before
                // `lis TheUI` into r26 (0x82699470/74); ours hoists them the
                // other way round, and the positive form of this condition
                // (A && B && !C) does not change that (w7-bx).
                // Mechanism (c2-rs §8.4, measured at w26-b): this is a tie-0
                // hoisted-address pair, so the pop order is descending
                // candidate id, and the ids come from the free list the ben
                // pass leaves.  The tap reads C=6 code-band mints and F=15
                // demotions for our body; the six mints recycle the fifteen
                // freed ids from the top down (44,43,42,41,40,39), so mint
                // order IS pop order, and the two globals land on id41 -> r27
                // (TheUI) and id40 -> r26 (sBlacklight).  The image needs the
                // pair the other way, i.e. sBlacklight minted first.  Since
                // every code mint is recycled here (C < F), adding or removing
                // one demotion shifts all six ids together and cannot swap the
                // pair; the handle is the order the two address computations
                // reach the code band, which sched1 sets, not the source.
```

Inside the body (was lines 2890-2898):

```text
                // Restore (w7-bx): SetColor(r, g, b) -- right-to-left argument
                // evaluation loads blue, green, red with the lfsu on red
                // (0x82699588-0x82699598) and its mDirty |= 1 is the RMW that
                // follows (0x826995A8).  The image then stores green, blue,
                // red; our Set body stores red, green, blue (3 rows).
                // NEGATIVE: per-field green/blue/red assignments + MarkDirty
                // reorder the loads too (96.8); hoisting mFontMaps.end() into
                // a local turns the rotated `b`-to-bottom loop into a top
                // test and steals r26 (92.0).
```

Inside the body (was lines 2907-2913):

```text
// Residual 2 rows (98.5714 on BOTH rulers, so its 560 B are parked behind them):
// the argument setup for CalcScreenHeight is scheduled one slot apart --
//     target   lfs f0, 0x70(r1) / addi r5, r1, 0x50 / mr r4, r27 / fmuls f1, f1, f0
//     base     lfs f0, 0x70(r1) / mr r4, r27 / addi r5, r1, 0x50 / fmuls f1, f1, f0
// Two independent argument registers, same instructions, adjacent positions.
// Diagnosed only (w9-e 2026-09-30), nothing attempted: there is no source
// operator between `mesh` and `&screenHeight` to reorder.
```

Inside the body (was lines 2915-2926):

```text
    // w22-ng: native runs the image's body too.  It used to replace it with
    // `if (!mWrapEnabled) UpdateText();` -- a re-layout of every non-scrolling
    // label on every draw -- added in the 2026-03 bring-up on the belief that
    // the original "checks mDirtyFlags ... on native we always rebuild".  It
    // does not: this is a diagnostics-only hook (the 0x82691498 body only
    // measures CalcScreenHeight and MILO_NOTIFYs "oversized"), and the image
    // lays text out only from Load, Copy, the update_text handler and
    // UILabel::LabelUpdate -- all of which native now reaches.  Measured
    // before/after with a headless screenshot tour: choose mode, song select
    // (scrolled), multiuser x3 are pixel-identical; main_screen differs only
    // in the time-dependent MOTD ticker offset; the gameplay score label
    // still counts up.
```

Inside the body (was lines 2938-2946):

```text
                    // w26-t: naming the aspect ratio and then the product each
                    // move one step of this call's argument setup.  With the
                    // product written inline, c2 materialises the `mesh` copy
                    // one slot BEFORE the `&screenHeight` address; the image
                    // does the address first.  Naming the product gives it a
                    // variable leaf instead of a folded subexpression, which
                    // both reorders the two argument registers (image: addi r5,
                    // mr r4) and drops the `fmuls f0,f1` / `fmuls f1,f0`
                    // operand swap.  140/140 instruction text.
```

Inside the body (was lines 3022-3026):

```text
            // w26-f12: naming the loaded scroll offset turns it from a CSE temp
            // leaf into a variable leaf, and c2's commutative key (C2RS-BRIDGE
            // 8.7) then emits `fadds f0,f13,f0` -- maxWidth first -- where the
            // temp leaf used to sort above maxWidth and come first.  Same
            // instruction stream; the image's operand order.
```

Inside the body (was lines 3194-3216):

```text
    // The image parks a SECOND copy of state.mSize in a callee-saved FPR
    // across the virtual AspectRatio() call (`fmr f26, f0` at 0x25f4).  This
    // local does NOT reproduce it (w7-bs): wherever it is declared it merges
    // into the single `lfs f30, 0x0(r30)`; it is kept for readability only.
    // w31-a (2026-11-03): the one placement w7-bs did not try -- hoisting the
    // declaration ABOVE the `z0` statement and using it there, which is where
    // the image's `fmr` sits (between the mZOffset load and the z0 `fmadds`)
    // -- is byte-inert too: 98.40945 canonical / 97.54330 fuzzy, 38 diff_arg
    // + 2 insert + 2 delete, identical to the current spelling.  c1 folds the
    // copy away wherever it is written, so the image's extra `fmr` is a c2
    // live-range split across the AspectRatio() call, not a source value.
    //
    // w31-a behaviour certificate for the whole function: the
    // RELOCATION-SYMBOL multiset over each side's own byte range is 23 on
    // both sides and IDENTICAL, so no constant, callee, global or string in
    // this body is wrong.  The 42 residual swap instructions are ONE FPR
    // rotation, not 8 independent faults: volatile side, the image holds
    // sum/charW/advW in f13/f12/f0 and we hold them in f0/f13/f12; saved
    // side, the image holds size/z0/circle/0.5/scaledCenter in
    // f26/f27/f28/f29/f30 and we hold z0/circle/0.5/scaledCenter/mSize in
    // the same five.  Both follow the regasg first-fit list (f0 f13 f12 ...,
    // then f31..f14) over the sched3 def stream -- C2RS-BRIDGE §8.2 -- which
    // is why the `size` copy cannot be re-keyed from source.
```

Inside the body (was lines 3220-3245):

```text
    // NOTE (bug 1B fix): keep glyph height CONSTANT (z0 - z1 == aspect*size)
    // regardless of yPos. Permuter sweep f5f704d6 flipped this subtraction to
    // `_tmp1*state.mSize - z0`, which reflects the quad about aspect*size/2 and
    // COLLAPSES it to zero height for vertically-centered text (yPos == th/2 ==
    // aspect*size/2 for a single centered line) — making all menu/HUD text
    // invisible. Subtraction is not commutative; the swap was match-neutral
    // (objdiff 83.8% either way) but behaviorally wrong. Reverted to og form.
    // The image keeps `_tmp1 * size` and the subtraction apart
    // (`fmuls f12, f1, f26` at 0x261c, `fsubs f12, f27, f12` at 0x2638) where we
    // contract to one fnmsubs.  That is a ROUNDING difference: the image rounds
    // the product before subtracting, we keep one extra rounding step.
    // Mechanism: c2's fp contraction peephole on `a - b * c`, which re-fires
    // across statement boundaries.  Measured inert at w26-b (one c2 compile
    // each): a named `aspectH` temporary, `z0 + -aspectH`, swapping the factors,
    // an unrelated statement between product and subtraction, and reading
    // `state.mSize` directly instead of the `size` copy -- the last one does
    // split the pair but also drops the `fmr` copy of mSize the image keeps and
    // shrinks the frame by 0x10, so it measures worse overall (95.2 vs 97.8).
    // w13-a: moving this below the vert[0] Set() (the FontMap3d fusion lever)
    // is inert here.
    // w27-d: the lever that DOES split the pair is c2-rs §8.8's reference
    // binding.  Bound to a `const float &`, the product survives copy
    // propagation as its own rounding step, so we emit the image's
    // `fmuls` + `fsubs` instead of one contracted `fnmsubs`
    // (96.95276 -> 97.54330 fuzzy, 97.77953 -> 98.40945 canonical).  A plain
    // named `float aspectH` is inert -- c1 folds it back into the tree.
```

Inside the body (was lines 3379-3386):

```text
        // w13-a: an identity scaled by cellHeight.  The image writes the three
        // diagonal slots (0x60, 0x74, 0x88) first and materialises every zero
        // as `fmuls f0, f0, f31` -- cellHeight times the 0.0f it holds in a
        // callee-saved FPR (0x82690064) -- which is what scaling Identity()'s
        // rows gives under /fp:fast (x*1 folds, x*0 does not).  The 0.0f is a
        // literal throughout this function: a `static const float` zero was
        // addressed through a hoisted r27 instead of living in f31, which cost
        // the image's fourth saved FPR and its 0x150 frame.
```
