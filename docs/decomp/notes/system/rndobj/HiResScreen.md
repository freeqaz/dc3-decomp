# Lane notes: `src/system/rndobj/HiResScreen.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0BmpCache@HiResScreen@@QAA@II@Z`

`HiResScreen::BmpCache::BmpCache` -- moved verbatim from `src/system/rndobj/HiResScreen.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 23-54):

```text
// RESIDUAL (w9-f, 98.291 canonical / 98.12 raw, 468 B): 6 rows of 118, one
// cause -- WHERE the __FILE__ address materialisation is scheduled.  The string
// is used first by the MILO_ASSERT at 0x3B and again by the MemAlloc at 0x44, so
// both builds keep it in callee-saved r25 across both.  The image splits the
// lis/addi pair and interleaves it into the assert's modulo computation --
// `lis r9, ...@ha` at 826266A0 sits BETWEEN `divwu r8, r26, r11` (82626690) and
// `mullw r11, r8, r11` (826266A4), with the `addi r25, r9, ...@l` at 826266AC
// after the `subf.` -- filling the divide's latency.  We emit the pair back to
// back after the `subf.`.  The three register renames (r8/r9 on the divwu and
// mullw, r11/r10 on the subf., r9/r11 on the addi) are all downstream of that
// one placement.  The member store order already matches (mRowsPerCacheLine at
// 0x10 before mByteSize at 0x20), so this is a scheduler slot fill with no
// source handle found.
// w19-a (still 98.29): no statement order of the independent member stores
// (mPixelsPerRow/mTotalRows, the three initial stores, the two final stores)
// moves the __FILE__ lis -- hill-climbed over every single move, all inert.
// w26-t (still 98.29): the mechanism hypothesis, and 12 more inert spellings.
// The placement and the register are the same decision, and it is circular:
//   image  divwu r8 / mullw r11, r8 / subf. r11   and  lis r9, __FILE__@h at
//          idx 32, filling the divide's latency shadow;
//   ours   divwu r9 / mullw r11, r9 / subf. r10   and  lis r11, __FILE__@h at
//          idx 35.  r11 is the divisor `rowsPer`, whose last use is the subf.,
//          so our lis cannot be scheduled before idx 34 at all.
// The image's allocator gives the quotient/remainder chain r8 and keeps r9 for
// the address half; ours gives that chain r9 and the address half r11.  It is
// a GPR colour choice upstream of any schedule slot, and no statement order
// reaches it.  Measured inert (98.2479): `%` in the assert, `--rowsPer`,
// parenthesising the byte-size product, swapping the two final stores, naming
// the byte budget, naming the per-row byte count, naming totalRows and/or
// pixelsPerRow (those two are worse, 87.6 / 86.1), asserting against `rowsPer`,
// and a named cache budget with a named totalRows.  `%` in the LOOP condition
// is 73.8 -- the expanded `a - a/b*b` is what the image has.
```


## `?Accumulate@HiResScreen@@QAAXXZ`

`HiResScreen::Accumulate` -- moved verbatim from `src/system/rndobj/HiResScreen.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 296-328):

```text
    // BEHAVIOURAL FIX (w9-f).  We were handing Merge() a HORIZONTAL border
    // where the image hands it a vertical one, and vice versa -- and the bug
    // was invisible to the score, because our stack-slot assignment happened to
    // compensate for it exactly.
    //
    // Read the image's two calls by SLOT.  GetBorderForTile receives
    // &p3 = 0x6c, &p4 = 0x60, &p5 = 0x68, &p6 = 0x64 (82627B20-82627B2C), and
    // its body puts the horizontal 480 in p5 or p3 and the vertical 270 in p4
    // or p6.  Merge is then called (82627B48-82627B88) with
    //   srcW = lwz 0x6c = p3,  srcH = lwz 0x60 = p4,
    //   padX = lwz 0x68 = p5 -> stw 0x54(r1),
    //   padY = lwz 0x64 = p6 -> stw 0x5c(r1)
    // (0x54 and 0x5c, not 0x54/0x58, because Xenon's parameter save area uses
    // 8-byte slots and a 32-bit int sits in the high half of one).  So the image
    // passes p3, p4, p5, p6 straight through in order, which pairs srcW with
    // padX (both horizontal, left/right) and srcH with padY (both vertical,
    // top/bottom) -- exactly what Merge's body needs: blendThreshX = dstX - padX
    // is compared against bmX, which starts at srcW, and blendThreshY =
    // dstY - padY against bmY, which starts at srcH.
    //
    // We passed (left, right, ..., top, bottom), i.e. p3, p5, p4, p6 -- so
    // srcW got the left border but padX got the TOP one, and srcH got the right
    // border while padY got the bottom.  Both axes were crossed, so the blend
    // ramp on a tiled hi-res screenshot was divided by the wrong pad and
    // started at the wrong row.
    //
    // It scored 100% on those instructions anyway: our locals were declared
    // `left, right, top, bottom`, which put top at 0x68 and right at 0x60 --
    // the mirror of the image -- so `lwz 0x60` read our `right` where the image
    // reads its `top`.  Identical bytes, different meaning.  The declaration
    // order below is now the image's, which is what the two remaining
    // OFFSET_SWAP rows at idx 63/64 were reporting all along; the earlier note
    // here read those as "not a slot-allocation choice", which was wrong.
```


## `?Merge@HiResScreen@@IAAXABVRndBitmap@@HHHHHHHH@Z`

`HiResScreen::Merge` -- moved verbatim from `src/system/rndobj/HiResScreen.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 381-396):

```text
// RESIDUAL (w9-f, 100.0 canonical / 99.3 raw, 772 B): 19 rows, every one a pure
// register permutation -- no insert, delete, replace or diff_op anywhere.  The
// integer side is a cycle over the SAME callee-saved set both builds pick,
// {r17,r18,r19,r21,r22}: the image homes bm in r19, srcX in r18, dstX in r22,
// xConst in r21 and xBlend in r17, and we use r18/r17/r21/r22/r19 for the same
// five.  Emission ORDER already agrees (idx 17 is xConst and idx 18 xBlend on
// both sides), so this is allocation, not scheduling, and declaration reorder is
// the wrong lever for it.  REFUTED (w9-f): spelling the sqrt as RB3 does,
// `sqrtf(blendX * blendX + blendY * blendY)`, is byte-identical -- the f0<->f13
// pair at idx 102/103 is the known commutative same-register floor, not an
// operand-order bug.
// ⚠ Do NOT port RB3's loop shape wholesale to chase these: RB3's Merge has no
// `(unsigned)yIter >= mAccumHeight` break and no `yIter >= 0` guard, and ours is
// at canonical 100.0 WITH them, so the image has them and RB3's source is a
// different build of this function.  RB3 was the right reference for the blend
// initialiser and the wrong one for the loop.
```


## `?ScreenRect@HiResScreen@@QBA?AVRect@Hmx@@PBVRndCam@@ABV23@@Z`

`HiResScreen::ScreenRect` -- moved verbatim from `src/system/rndobj/HiResScreen.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 559-563):

```text
    // w13-a (96.8 -> 100): `ret`, the copy of r, is what the image hands
    // CurrentTileRect (`addi r4, r1, 0x70`), and the result is accumRect
    // adjusted in place (the stfs at 0x80..0x8c are CurrentTileRect's third
    // out-param slot, copied to the return buffer from there).  With `ret`
    // dead the image's frame was 0x10 smaller than ours.
```
