# Lane notes: `src/system/char/CharLipSyncDriver.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetLipSync@CharLipSyncDriver@@QAA_NPAVCharLipSync@@@Z`

`CharLipSyncDriver::SetLipSync` -- moved verbatim from `src/system/char/CharLipSyncDriver.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 179-183):

```text
        // w14-b: a guarded delete, not RELEASE(). The image clears the member
        // only inside the taken arm (beq skips the `stw r29, 0x94(r30)` at
        // 0x823B9158) and homes the old pointer at 0x50(r31) for the deleting
        // dtor; RELEASE's unconditional `x = null` merges the store below the
        // test. Same behaviour either way. 98.22 -> 100.
```


## `?UpdatePlayback@CharLipSyncDriver@@IAAXPAVPlayBack@CharLipSync@@MM@Z`

`CharLipSyncDriver::UpdatePlayback` -- moved verbatim from `src/system/char/CharLipSyncDriver.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 318-354):

```text
                // w19-c (96.505 -> 100 canonical): the image re-tests after the
                // fail and zeroes on a BRANCH (fcmpu f31,f30 / bge / fmr f31,f30).
                // Written with the constant on the LEFT, MSVC no longer threads
                // this test into the fail test above (w13-c measured
                // `if (curWeight < 0) curWeight = 0;` at 95.1 for exactly that).
                // One branch-target row remains: our first `bge` skips straight
                // past the re-test, the image's lands on it.  Same instructions
                // on the value path, so NaN behaves as the image does.  Max()
                // (one fsel) was 96.505; std::max 96.8 with a +0x10 frame.
                // w25-gh: the remaining row is jump threading.  og-dc3's
                // `MILO_ASSERT_FMT(curWeight >= 0, ...); if (curWeight < 0)
                // curWeight = 0;` matches 103/103: the fail test then carries a
                // GE condition in the IL, which c2 does not identify with the LT
                // re-test, so the fail-skip lands on the re-test.  Rejected:
                // on native (no fast-math) `>= 0` fails on a NaN weight, where
                // the image's `bge` (fp:fast) skips the fail.  Every NaN-safe
                // spelling probed is an LT/GT test and gets threaded (102/103):
                // `!(w < 0)` assert, `0 > w` fail test with `w < 0` re-test,
                // a 0.0 double literal, a `w <= 0 && w != 0` re-test; `w >= 0 ||
                // w != w` and a nested GE/LT test add code (70/105).
                // DECIDED 2026-11-05, owner delegated: do NOT take the GE form.
                // Native correctness outranks one fuzzy row, so a NaN weight must
                // not print FAIL on the port where the image ignores it. What is
                // still open is a NaN-safe source form whose IL test is GE; none
                // of the eight probed is one.
                // w26-f14 (measured in this tree, exact): the og-dc3 form
                // `MILO_ASSERT_FMT(curWeight >= 0.0f, "weight = %f", curWeight)`
                // alone closes the row -- fuzzy 99.951454 -> 100.000000 with
                // canonical 100.0 unchanged.  Not landed: GE is the only IL test
                // that lands the fail-skip on the re-test, and under clang (no
                // fast-math) `>=` fails on a NaN weight where the image's `bge`
                // (fp:fast) skips the fail, so native would print FAIL: (and abort
                // under MILO_FATAL_FAILS=1) on an input the image ignores.
                // `!(curWeight < 0.0f)` is NaN-safe and reproduces 99.951454
                // (LT in the IL, threaded).  This is a behaviour trade settled in
                // favour of native correctness, not a compiler limit -- do not
                // re-open it for the score without a NaN-safe GE form.
```


## `?ScaleAddViseme@CharLipSyncDriver@@QAAXPAVCharClip@@M@Z`

`CharLipSyncDriver::ScaleAddViseme` -- moved verbatim from `src/system/char/CharLipSyncDriver.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 571-576):

```text
    // BUG FIX (w19-x): the WEIGHT is the second argument and the trailing
    // dframe is 0 -- image 0x823B6618/0x823B661C `fmr f3, f31` (f31 = 0.0f,
    // loaded at 0x823B65C0) / `fmr f1, f30` (f30 = the weight, `fmr f30, f1`
    // at 0x823B65AC) before the slot-1 ScaleAdd(CharClip *, float weight,
    // float frame, float dframe) bctrl.  We passed (0, beat, weight), so every
    // viseme was blended at weight zero.  RB3's ScaleAddViseme agrees.
```
