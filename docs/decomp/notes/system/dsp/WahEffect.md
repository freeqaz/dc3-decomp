# Lane notes: `src/system/dsp/WahEffect.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Process@WahEffect@@QAAXPAMHH@Z`

`WahEffect::Process` -- moved verbatim from `src/system/dsp/WahEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Above the definition (was lines 49-64):

```text
// w7-bh: the one remaining Function Call Diff row here is a benign ICF fold,
// not a wrong callee.  The image calls
// `MakeString<char[19], int, char[5]>` and we emit
// `MakeString<char[18], int, char[14]>`; both names resolve to 0x824D1870 in
// build/373307D9/icf_aliases.map, because MakeString's array-bound template
// parameters never reach the generated code and every instantiation of that
// shape is byte-identical.  The name objdiff shows is just whichever
// instantiation won the fold.  (w7-bh's "indexed-vs-auto-update addressing
// floor" at 93.29% was the `sampleIdx += numChans` spelling; `i * numChans + ch`
// reaches the indexed form -- see w7-bx notes in the body.  RESIDUAL at 99.0
// (the parameter-homing part closed by w20-e, see the body):
// callee-saved homing of the three parameters (image r28/r30/r27 = buf /
// numSamples / numChans, ours r29/r27/r28) and of the sample base (r29 vs
// r30), the f12/f13 colouring of `1 - newFreq` vs mGain at 0x82E5A1A0-1C0,
// and the `fmuls f29, f17, f17` slot after `bl cos`; declaring i above the
// guard, hoisting mGain into a local, and `out * gain` are all byte-identical.)
```

Inside the body (was lines 143-183):

```text
        // w7-bx: the image's inner loop is INDEXED -- `add r8, r29, r10` /
        // `slwi r7, r8, 2` / `lfsx f12, r7, r28` (0x82E5A214-0x82E5A230) and
        // `stfsx f12, r7, r28` (0x82E5A270) -- with r29 advanced by
        // `add r29, r29, r27` (0x82E5A280) each sample.  That is
        // `buf[i * numChans + ch]`: MSVC strength-reduces i*numChans to the
        // r29 += numChans walker but will not fold the second-level
        // `(i*numChans) + ch` into a pointer walk.  Spelling the index as a
        // running `sampleIdx += numChans` (w7-bh's form) makes the whole
        // subscript a reducible IV and yields the `stfsu f12, 0x4(r10)` /
        // `add r30, r28, r30` pointer walk (93.3); `unsigned` sampleIdx is
        // identical.  The countdown on numSamples (`subic. r30, r30, 0x1`,
        // 0x82E5A278) comes from MSVC itself: w20-e (raw 98.28 -> 98.6)
        // measured the plain `for (int i = 0; i < numSamples; i++)` over the
        // indexed subscript and it closes every callee-saved homing row
        // (image r28/r30/r27/r29 = buf/numSamples/numChans/i*numChans).  The
        // old note that a counting loop "costs r25 (94.1)" was measured
        // against the sampleIdx walker, not this indexed form.
        // RESIDUAL w20-e (15 rows): the mGain/`1 - newFreq` f10/f12/f13
        // colouring (rows 128-136), `fmuls f29, f17, f17` one slot before
        // `bl cos`, the r9/r10 swap of ch vs &stack50 in the inner loop, and
        // `fmuls f12, f13, f12` operand order.  Inert: `out * f13_gain`;
        // worse: `for (int ch ...)` inside the guard (98.1), no numChans
        // guard (98.1).  decomp-synth hill_climb (4 rounds x 60) found nothing.
        // w24-c3 STOP at 99.0 (same 15 rows): `f29 = f17 * f17` moved after
        // the sin() call is much worse (raw 96.9, frame 0x110 vs 0x120, every
        // callee-saved FPR shifts by one).  The FlangerEffect pointer-read
        // lever does not apply: no store-forwarding row here.
        // Mechanism hypothesis (c2 priority colouring, c2-rs
        // docs/whitebox/ref/P_REGALLOC.md; FPR list f0,f13,f12,...): the
        // image colours the mGain load (0x82E5A1A0) BEFORE `1 - newFreq`, so
        // mGain takes f13 and `1 - newFreq` falls to f12; we colour them the
        // other way round (f13 / f10).  The schedule is identical, so the
        // difference is the worklist order: a priority or tuple-visit-ordinal
        // tie decided the other way.  Same shape for ch (image r10) vs the
        // &stack50[ch] temp (image r9) in the inner loop.  Tried, all
        // byte-identical: reading mGain into a local right after the
        // mPrevEnv store (earlier first visit); folding `1 - newFreq` into
        // the blend expression (shorter range); one-expression blend.
        // Open question: what in the lowered tuple order makes the image's
        // mGain load outrank `1 - newFreq` -- neither its live range nor its
        // source position moves it.
```

Inside the body (was lines 250-254):

```text
                    // w7-bx: `ch` is declared above the `numChans > 0` guard
                    // because the image sets `li r10, 0x0` (0x82E5A1F4) BEFORE
                    // `cmpwi cr6, r27, 0x0`.  (w7-bh's byte-offset
                    // `*(float *)((char *)stack50 + ch * 4)` spelling of the
                    // state arrays was a no-op; plain subscripts are kept.)
```

Inside the body (was lines 262-268):

```text
                    // Biquad filter.  Under /fp:fast the source order of the
                    // two products is the OPPOSITE of the image's: the image
                    // does `fmuls f11, f12(sample), f0(feedback)` at
                    // 0x82E5A238 and then `fmadds f11, f10(state1), f28, f11`
                    // at 0x82E5A24C; writing sample*feedback first emits
                    // state1*f28 first and hoists the state1 load above the
                    // buf load (w7-bx).
```
