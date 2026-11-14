# Lane notes: `src/system/rndobj/Flare.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawShowing@RndFlare@@UAAXXZ`

`RndFlare::DrawShowing` -- moved verbatim from `src/system/rndobj/Flare.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 136-165):

```text
            // Semantically this is `useOccResult = !mOcclusionPending && mOcclusionReady`,
            // and the image agrees.  Decoded straight off the listing
            // (8270BC98-8270BCBC): test 0x89 (= mOcclusionPending) first and take 0 when
            // it is set, otherwise test 0x88 (= mOcclusionReady) and take 1, otherwise 0;
            // the result is narrowed by `clrlwi r28,r11,24` and lives in r28.  That also
            // fixes the field order: the two stores after it are `stb r26,0x88` (ready =
            // false) then `stb r10,0x89` (pending = the old ready), so 0x88 is
            // mOcclusionReady and 0x89 is mOcclusionPending -- no behaviour difference.
            // Spelling the `&&` DOES reproduce that clrlwi and the r28 home, but it
            // stops MSVC sharing slot 0x50 between `scale` and the int->float conversion
            // scratch: the scratch takes 0x60 and 0x90, `dir` moves 0x60 -> 0x70 and the
            // frame grows to 0x140, re-opening about twenty displacement rows.  Net
            // 98.94 -> 97.75 canonical (w7-n 2026-09-14, w15-a, and w27-c 2026-11-02
            // re-measured it on this tree).  The older note here claimed the `&&` form
            // kept the image's 0x130 frame; it does not.
            // Keep the comma-operator form until a spelling is found that gets the
            // clrlwi without moving the slots.
            //
            // w27-c (2026-11-02): the 24 residual rows are ONE register exchange plus
            // that clrlwi.  The image keeps the `this-0xc0` interior pointer (13 uses:
            // WorldXfm_Force, CalcRect, RectOffscreen) in r27 and `useOccResult` in r28;
            // we take the opposite pair, which is 15 rows on its own.  Register-
            // insensitive comparison of the two instruction multisets leaves exactly one
            // unmatched instruction, the `clrlwi R,R,24` -- i.e. both bodies are the same
            // computation and this row set is not hiding a missing call or a wrong
            // field.  Two other rows are pure order: `fmadds f0,f13,f12` vs
            // `fmadds f0,f12,f13` (idx 118) and the two `stfs` to 0x30/0x34 (idx 214/215).
            // Dropping the `oldReady` temp below (RB3's literal `mOcclusionPending =
            // mOcclusionReady; mOcclusionReady = false;`) is byte-inert: same 24 rows,
            // same 98.94405 / 98.55944.
```

Inside the body (was lines 241-246):

```text
                // w26-r: the local is a Transform, not a Matrix3.  The 0x40-byte
                // memcpy above and below is the image's own (a Matrix3 local is
                // overflowed by 16 bytes there); sizing the local to the copy
                // keeps every memcpy byte-for-byte identical on the X360 arm and
                // restores the image's 0x130 frame -- the Matrix3 version ran a
                // 0x120 one, 16 bytes short.  Same statements, same order.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `?CalcScale@RndFlare@@IAAXXZ`

`RndFlare::CalcScale` -- moved verbatim from `src/system/rndobj/Flare.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 333-346):

```text
// w25-oa: 75/76. The one row is the z term of the inlined Dot(v28, mMatrix.z)
// (c2's commutative sort key, C2RS-BRIDGE 8.7). mMatrix.z.z is a memory leaf on
// base temp 377 (377 & 3 == 1, key 0x14008), above the cross z, v28.z (V18,
// 0x10240), so the load goes first. The image has cross z first, so it needs that
// base == 0 mod 4 (key 0x10008), i.e. a temp shift of +3 or -1 before it. The
// shift must also keep z.x's base 386 != 0 mod 4; the y term (z.y temp 396
// over V14) holds for any shift. Nothing reachable without new code: all 18
// code-identical spellings in a 96-way paren/order grid keep 377. Parens on
// `-len`, Length(mMatrix.x) and Length(mMatrix.z) (grid k21100) move every temp by +3
// (380/389/399) and flip the z term as predicted, but reschedule the block (56/76).
// Named refs or an explicit Dot body change code, because the inline's
// reference params are what keep the loads off a separate base.
// Also inert (w25-gd, ~110 variants): Dot argument order, v28 declaration
// position, Cross before/after Length, named scaleY/scaleX/negLen, `0 < dot`.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.868, 1 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/rndobj/Flare.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

File-level (was lines 371-422; no definition follows it):

```text
// ---------------------------------------------------------------------------
// w48-b (2026-11-06) EQUIVALENCE CERTIFICATE -- RndFlare::DrawShowing,
// canonical 98.94405 / fuzzy 98.55944.  TARGET 286 instructions, OURS 285
// (1144 B vs 1140 B): this row is ONE INSTRUCTION SHORT on our side.
// Placed at END OF FILE deliberately.  MILO_ASSERT takes an explicit line
// LITERAL rather than __LINE__ (src/system/os/Debug.h:93), so a comment could
// not have moved an assert's message, but nothing above this line is touched
// at all and the certificate therefore cannot perturb the row it describes.
//
// BEHAVIOUR: no divergence.  All 24 non-equal rows (263 equal / 21 diff_arg /
// 2 delete / 1 insert) fall into four groups.
//   (a) 15 rows are ONE register exchange.  The interior pointer `this-0xc0`
//       (13-14 uses: four inlined WorldXfm calls, CalcRect, RectOffscreen and
//       the two 0xbd(rN) drawable loads) is r27 in the image and r28 here;
//       `useOccResult` takes whichever is left.  Same value, same instruction.
//   (b) The instruction we do not emit is the image's `clrlwi r28, r11, 24`.
//       It narrows a merge value that is provably 0/1 on every path -- the
//       arms are `li r11,1` and `mr r11,r26` with r26 = 0 -- so the
//       narrowing is a no-op and the row is not hiding a missing call or a
//       wrong field.
//   (c) idx 118, one commutative `fmadds`: `dir.x = offset * dir.x + v.x`.
//       Only the x term differs; y and z (idx 122/126) already take the
//       image's operand order from the same source spelling, which is what
//       makes this a scheduling/key artifact rather than a form difference.
//   (d) idx 214/215, the order of two `stfs` inside RndMat::SetColor.  The
//       multiset is identical -- r, g and b all land on 0x2c/0x30/0x34.
//
// MECHANISM (c2rs tap: `cands.sh src/system/rndobj/Flare.cpp
// DrawShowing@RndFlare`; stage-tap-obj-differs 0, replay-vs-capture-differs 0,
// cands-errors 0, 40 candidates):
//   id 50  pri -15  tie 15  ben 17 ref 14  sym 48/k03 org arena#51 -> r28
//   id  5  pri -18  tie 69  ben  4 ref  4  sym  5/k04 org arena#6  -> r27
// They are the LAST TWO POPS of the worklist.  The image's assignment is the
// same two values with their priorities EQUAL, after which tie 69 beats tie 15
// and `useOccResult` takes r28 first.  So the lever is THREE priority units on
// one of the bottom two candidates.
// 22 spellings measured this wave, none reached it.  Byte-identical to
// baseline: an if/else with both arms assigning, and the comma form with
// `mOcclusionReady == false`.  Everything else loses: `useOccResult =
// !mOcclusionPending && mOcclusionReady` 97.75175, unbraced if/else 98.15734,
// named `occOn` 98.88112, `(...) == false` 98.03147, ternary 95.91608,
// default-false plus a single `= true` arm 98.59441, a named pending/ready
// pair 97.75175, operands reversed 98.88811, `!(a || !b)` 97.75175, nested if
// 98.59441, `= mOcclusionReady` 98.24476.
// The `&&` family is REFUSED and the reason is MEASURED, not inherited: every
// form that supplies the missing `clrlwi` ALSO grows the frame 0x130 -> 0x140
// (`stwu r1,-0x140` at idx 5; every stack slot shifts +0x10 -- 0x50->0x60,
// 0x58->0x68, 0x80->0x70, 0x90->0xa0), re-opening about twenty displacement
// rows and landing the row at 97.75175.  One instruction is not worth twenty
// rows.  Also byte-identical: deleting the never-reassigned `visibility` local
// and comparing the literal, and `ratio > visibility`.
// ---------------------------------------------------------------------------
```
