# Lane notes: `src/system/os/JoypadClient.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@JoypadClient@@AAAXXZ`

`JoypadClient::Poll` -- moved verbatim from `src/system/os/JoypadClient.cpp` on 2026-11-13 (lane w-five-clean; line numbers are as of the commit before the move)

Above the definition (was lines 186-199):

```text
// w18-d (94.45, 5 rows): the plain indexed `for (i) mRepeats[i]...` loop makes
// &mRepeats[i].mRepeatTimer the IV (`subi r3, r31, 0x30` / `subi r3, r31, 0x40`,
// 94.2), with or without a `Timer &` local -- reverted.
// w-this-adjust (2026-11-13): POINTER AUDIT, NOT A BUG.  `subi r3, r28, 0x30`
// is not a this-adjustment: r28 = &mRepeats[i].mRepeatTimer and the result is
// &mHoldTimer (Timer is 0x30 B), the same address we form as r31+0x10.  The
// image's JoypadRepeat::Start keeps its first timer address in a callee-saved
// GPR the same way.  JoypadRepeat::Reset(JoypadButton) has no out-of-line copy
// in the image, so it is inlined; `pRepeat->Reset(pRepeat->mLastBtn)` (self-
// compare folds) is byte-identical at 94.45 -- inert, not shipped.
// w-five-clean (2026-11-13): MEASURED NEGATIVE, readability probe --
// `JoypadRepeat *repeat = mRepeats; for (int pad = 0; pad < 4; pad++,
// repeat++)` is 90.3 (the `li r29, 0` / `addi r31, r3, 0x40` init pair swaps
// order: equal 22 -> 20, 3 replaces).  Kept the do/while; renamed i/pRepeat.
```

Inside the body (was lines 205-254):

```text
            // MECHANISM (w7-av; w33-c): 94.45 is an address-anchor divergence.  The image forms
            // `addi r28, r31, 0x40` (the repeat timer) into a CALLEE-SAVED
            // register before the first call and derives the hold timer from it
            // as `subi r3, r28, 0x30`, which is why it saves one GPR more than
            // we do (__savegprlr_27 vs _28).  We form each address from r31 at
            // its use.  Refuted, both 94.45 with identical rows: binding
            // `Timer &repeatTimer = pRepeat->mRepeatTimer;` before the first
            // Reset, and even writing the hold timer as explicit pointer
            // arithmetic off the repeat timer -- MSVC folds both back to
            // `r31 + constant` at the use site.
            //
            // CROSS-BINARY CONFIRMATION (w9-a): rb3-xenon's os/JoypadClient.cpp carries a
            // byte-identical body and its ?Poll@JoypadClient@@AAAXXZ reads the
            // SAME 94.44827 -- two binaries, two lanes, one number.  Three more
            // spellings refuted here, all 94.44827 with the same five rows,
            // exploiting that Timer is 0x30 bytes so &mHoldTimer == &mRepeatTimer
            // - 1: `Timer *t = &pRepeat->mRepeatTimer; t[-1].Reset(); t->Reset();`,
            // the same with a named `holdTimer = t - 1`, and the reference form
            // `(&repeatTimer)[-1]`.  MSVC re-folds every one of them to
            // `addi r3, r31, 0x10`.  The residue is that the image spends one
            // extra callee-saved register (__savegprlr_27) to hold
            // &mRepeatTimer across the first Reset and then derives the hold
            // timer as `subi r3, r28, 0x30`; two of the five rows are only the
            // ThePlatformMgr anchor landing in r27 instead of r28 as a result.
            //
            // w31-b (2026-11-03): the mechanism is now named exactly, and it is
            // one instruction, not five. The image computes the SECOND call's
            // receiver first -- `addi r28, r31, 0x40` (&mRepeatTimer, into a
            // callee-saved GPR because it must survive the first bl) -- and
            // derives the first call's receiver from it as `subi r3, r28, 0x30`
            // (= &mHoldTimer; Timer is 0x30 bytes, mHoldTimer 0x10 and
            // mRepeatTimer 0x40, so this is faithful). We materialise each
            // address at its own use site, so the image is 29 instructions and
            // we are 28 -- one SHORT, which is why no score on this row may be
            // banked until that instruction exists. Call order is NOT the lever:
            // swapping the two Reset statements scores 94.48276 (+0.0345, an
            // alignment coincidence -- our 0x40 addi then lines up with the
            // image's) while emitting the calls in the opposite order to the
            // image, and it is reverted. Every binding form that should force
            // the callee-saved materialisation -- `Timer &` for one and for
            // both, `Timer *`, `t[-1]`, pointer arithmetic off the repeat timer
            // -- is folded back to `r31 + constant` at each use site, and a
            // `for` loop instead of the do/while costs 0.07.
            // w33-c behaviour verdict: EQUIVALENT.  Both sides Reset
            // mHoldTimer then mRepeatTimer (r31+0x10, then r31+0x40); the image
            // derives the first address as (r31+0x40)-0x30, which is the same
            // address -- it is the address materialisation, not the calls, that
            // differs.  Probe: a named `Timer *repeatTimer = &pRepeat->mRepeatTimer`
            // used for the second Reset is inert (94.44827), i.e. c1 folds it
            // back to r31+constant exactly as the earlier forms did.
```

Added after the move (lane w-five-clean):

```text
// w-five-clean (2026-11-13): BEHAVIOUR VERIFIED.  Listing: GuideShowing()
// inlined (`lbz 0x58` off ThePlatformMgr); guide up -> Timer::Reset at
// repeat+0x10 (mHoldTimer) then repeat+0x40 (mRepeatTimer); else
// JoypadRepeat::Poll(f1 = 0x38 mHoldMs, f2 = 0x3c mRepeatMs, r6 = 0x30 mSink,
// r7 = pad); stride 0x70, `cmpwi 4` -- all four pads.  No argument swap.
// Unicorn h4, fresh: EQUIVALENT/high, but both sides fault identically before
// the first call, so it is not evidence.  Native:
// CopyPollSemanticsTest.Joypad* (2 tests; sabotages -- hold/repeat thresholds
// swapped, hold-timer reset dropped, 3 pads -- all fail them).  Renamed
// i/pRepeat -> pad/repeat: byte-identical.
```
