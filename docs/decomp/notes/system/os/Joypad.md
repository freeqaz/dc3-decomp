# Lane notes: `src/system/os/Joypad.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `JoypadPollCommon()`

`JoypadPollCommon` -- moved verbatim from `src/system/os/Joypad.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Attribution: no function named `JoypadPollCommon` in report.json (unit); filed under the source name

Above the definition (was lines 503-514):

```text
// Per-frame joypad update. For every non-disabled pad this
//   - hands the platform back end (ReadSingleJoypad) the previous stick
//     positions re-quantised to signed bytes and gets back the raw state,
//   - dequantises the returned axes, tracks whether anything moved,
//     and drives the connect/disconnect edges,
//   - pumps the EEPROM ("breed data") write state machine,
//   - turns the button delta into button_up / button_down messages, with
//     velocity-bucketed drum hits sorted loudest-first,
// then polls the USB MIDI peripherals and the controller keep-alive timer.
// w20-p branch-landing row 152 (image `stwx r30` / `b` to loop tail vs ours
// `li r11, 0x2` / `b` to the shared `stwx r11` store): ARTIFACT -- r30 holds the
// constant 2 (`li r30, 0x2` at the pad-loop top), so both store state = 2.
```

Inside the body (was lines 657-757):

```text
            // w21-bg: 96.699 -> 97.003 normalized.  The "one cause" below was
            // not the only one: the sensors copy further down was a memcpy,
            // the image copies element by element (unrolled lwz/stw through
            // &sensors with a dead `addi r11, r23, 0x24` dest pointer), and
            // that alone moved the 0xb0..0x108 slot block (sensors 0xf8,
            // pro_guitar 0xd0, Symbol temp 0xe8) to the image's layout.
            // Still open: the third FPR (0.0f hoisted to entry; frame 0x12e0
            // vs 0x12d0) and the r23/r24 + r18/r19 renumbering it drags
            // (~165 rows, mostly register-only).  Inert this wave (standalone
            // cl.exe probe, FPR count + frame): dequant else-arm as `x = 0`,
            // default-then-override, ternary, `k < n`, direct member stores,
            // 1/127 multiply; y computed/declared before x; pressures reset
            // as `= 0`; sensors `= { 0 }` declared in the loop (frame 0x12d0
            // but padData moves to 0x120, wrong); a nested 2-iteration stick
            // quantise loop (not unrolled like the image).
            // RESIDUAL ROOT CAUSE (w7-bf, 96.72617 canonical, 125 rows -- ONE
            // cause).  We hold the literal 0.0f in a THIRD callee-saved FPR for
            // the whole function; the image does not, and every other row falls
            // out of that.  Prologue: image `stfd f30,-0xa8 / stfd f31,-0xa0`
            // (two FPRs), ours adds `stfd f31,-0xa0` for a third, so our frame
            // is 0x12e0 against the image's 0x12d0 (^/* 825E6B94) and ~90 rows
            // are the resulting +-0x10 stack-offset shift plus the f29/f30/f31
            // and r18/r19 renumbering.
            // The image materialises __real@00000000 THREE separate times and
            // lets each copy die immediately -- ^/* 825E6BEC into f0 (the
            // `pressures[] = {0}` initialiser), ^/* 825E6D1C into f0 (the
            // `pressures[p] = 0.0f` loop) and ^/* 825E7030 into f12, a VOLATILE
            // register hoisted only into this loop's preheader, right after
            // `mtctr` -- while 127.0f (f30) and 1/127 (f31) are the only two
            // values it parks in callee-saved FPRs.  Our build CSEs all four
            // 0.0f uses into one value spanning the outer loop, which crosses
            // calls, so it has to be callee-saved.
            // REFUTED (measured 2026-09-14, bit-identical to this spelling):
            // hoisting `float zero = 0.0f;` above this loop and writing
            // `x = zero; y = zero;` -- MSVC CSEs the local straight back into
            // the same function-wide literal.  The image's f12 copy is a
            // rematerialisation decision, not a second source variable.
            // w7-bn (96.73 -> 96.73, 125 -> 123 rows via the `== true` below):
            // the hoist is NOT a CSE of several 0.0f sites.  Measured, full
            // ninja each: with the entry `= { 0 }` removed and the pressures
            // loop copying sensors[] (else-arm the ONLY 0.0f use) our build
            // still parks it in a callee-saved FPR loaded at entry; with the
            // else-arm reading data.mSticks (pressures store the ONLY use,
            // one stfs per outer iteration) likewise.  Our MSVC hoists any
            // loop-invariant float literal out of BOTH loops; the image
            // hoists 127.0f and 1/127 that far but leaves 0.0f one level
            // down (f12 after `mtctr`, 825E7034) and in place at the two
            // stores.  Inert on the FPR: `x = 0` / `x = 0.0` / `x = y = 0.0f`
            // (the last costs an lbz swap), `pressures[0] = 0.0f` without the
            // loop, inverted `if (k >= mNumAnalogSticks)` (96.1), locals
            // declared in the image's slot order (96.1), a shared outer-body
            // `int k`, a while-form first stick loop.  Two more image facts
            // no spelling here reproduces: `2` lives in callee-saved r30 from
            // the first stick loop's count (825E6C78, with a dead `mr r11,
            // r30` at 825E6C8C) through `mEepromWriteState = 2` (825E6DD0
            // `stwx r30`), where we `li r11, 2` at the store -- so the image
            // and our build each keep ONE extra constant across the calls,
            // the image an int, ours the float.  CharClipSet::SetFrame (100%)
            // shows this compiler materialising __real@00000000 per sibling
            // arm (f29 twice), so the merge is loop hoisting, not global CSE.
            //
            // OPEN (w27-b, 2026-11-02): 97.00 canonical / 95.82 fuzzy, 165 of
            // 669 rows.  The whole cascade still hangs on the same entry: our
            // prologue saves THREE callee-saved FPRs (stfd f29/f30/f31) where
            // the image saves two (f30 = 127.0f, f31 = 1/127.0f), which is the
            // +0x10 frame and the r23<->r24 (75 rows) / r18<->r19 (24 rows)
            // renumbering that follows it.  The third is 0.0f, held live across
            // the whole outer loop's calls.  What is missing is a source form
            // that makes c2 REMATERIALISE 0.0f at the two pressure stores and
            // the stick else-arm the way the image does (825E6BEC, 825E6D1C,
            // 825E7030) instead of hoisting one copy out of both loops; the
            // 127.0f / 1/127.0f pair it must keep hoisting, so the budget is
            // exactly one register.  Everything above this paragraph was
            // re-confirmed against the current tree; no new spelling landed.
            //
            // w31-b (2026-11-03): LOCALISED to an address, and the third FPR is
            // NOT the entry initialiser.  Prologue: image `stfd f30,-0xa8` /
            // `stfd f31,-0xa0` and frame 0x12d0; ours adds `stfd f29,-0xb0` and
            // frame 0x12e0.  f31 = __real@42480000 (127.0f), f30/f29 =
            // __real@3c010204 (1/127.0f) on the two sides -- and our EXTRA
            // register is f30, loaded at 0x70/0x74 (`lfs f30,
            // __real@00000000@l(r11)` / `stfs f30,0x7c(r1)`) where the image
            // loads the same constant into volatile f0.  The two sites that
            // share it are the `pressures[] = { 0 }` initialiser and the
            // in-loop `pressures[p] = 0.0f` reset (image 0x6c and 0x198, each
            // `lis __real@00000000@h` + `lfs f0,@l`; ours has ONE `lis` and one
            // load reused).  So c2 CSEs the two zero sites and hoists the load
            // to entry, making it live across the loop's calls -> callee-saved.
            // DIAGNOSTIC, not a candidate: deleting the pre-loop initialiser
            // (which the reset loop makes redundant) does NOT remove the third
            // FPR -- f30 survives, the frame stays 0x12e0, and it scores worse
            // (95.43 vs 96.81 canonical), so the value being kept alive is the
            // loop's, not the initialiser's.  Also inert on the register:
            // `pressures[p] = 0` (int), unrolled `pressures[0] = 0.0f`, `= {}`
            // value-init, `= { 0.0f }`, a named `const float zeroF`, a named
            // `float *pr = pressures`, `*(pressures + p)`, `0.0f * 1.0f`, and
            // declaring `pressures` after `pro_guitar` (which also costs 1.5).
            // `x = y = 0.0f` in the stick else-arm costs 0.27.  Both sides are
            // 661 vs 658 instructions: the image materialises 0.0f twice where
            // we materialise it once, which is exactly the 2-instruction gap
            // plus our extra stfd/lfd pair.
```

Inside the body (was lines 762-766):

```text
                    // w22-a11: the image multiplies by the folded reciprocal
                    // __real@3c010204 (= 1.0f/127.0f) under /fp:fast, e.g.
                    // ^/* row `fmuls f13, f13, f31` with f31 = __real@3c010204;
                    // a true `/ 127.0f` rounds differently natively for 16 of
                    // the 256 byte values (+-9, 13, 18, 26, 36, 52, 72, 104).
```

## `JoypadPollCommon`

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: the
0x55/0xAA bytes swap registers but still store packet[j+4]=0x55, [j+5]=0xAA; the stick-
byte conversion and state-2 stwx are equivalent.
