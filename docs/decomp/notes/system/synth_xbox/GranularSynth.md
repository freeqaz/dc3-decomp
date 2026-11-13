# Lane notes: `src/system/synth_xbox/GranularSynth.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0GranularSynth@Synapse@DSP@@QAA@ABV?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@III@Z`

`DSP::Synapse::GranularSynth::GranularSynth` -- moved verbatim from `src/system/synth_xbox/GranularSynth.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 62-66):

```text
            // w15-b (98.65 -> 100): the value is computed into a local BEFORE
            // the store -- assigning straight into window[j] makes MSVC form the
            // element address first and pin _M_start across `bl cos`, where the
            // image reloads it after the call -- and the size is converted
            // before j (image: size to 0x88(r31), j to 0x90(r31)).
```


## `?ExtractGranules@GranularSynth@Synapse@DSP@@QAAXXZ`

`DSP::Synapse::GranularSynth::ExtractGranules` -- moved verbatim from `src/system/synth_xbox/GranularSynth.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 126-157):

```text
            // RESIDUAL (ExtractGranules, 92.2 canonical / 91.6 raw).  The
            // placement of this local is load-bearing and already tuned:
            // HERE = 92.2; before the gr.mGain store = 91.5 (adds an
            // `fmr f0, f13`); after the window loop with the 0.7 compare on
            // gr.mLength = 87.3; removed entirely = 87.3.  The last row of
            // this cluster is the image's `lfs f0, 0x8(r11)` feeding BOTH the
            // fsel and the fadds out of one register where we still emit a
            // copy.
            //
            // w7-bu NEGATIVE (still 92.2).  Six more spellings, each measured
            // alone against the base:
            //   - `length` hoisted above the gr.mGain store AND feeding
            //     gr.mFadeIn (one load for both, like the image's `lfs f0`
            //     at 82E4B3AC): 89.1 -- the constant-pool hoist (f4-f8 at
            //     82E4B1xx) re-registers and 44 rows swap;
            //   - `length` declared just before gr.mFadeIn and feeding it:
            //     89.1, identical rows;
            //   - `float startOffset = (float)(unsigned)gr.mStartOffset`
            //     hoisted above `rounded` (the image gives that conversion
            //     the EARLIER temp, `std r8, -0x48(r1)` 82E4B494, and
            //     `rounded`'s fctidz the later one, `stfd f0, -0x40(r1)`
            //     82E4B4B4; ours are -0x40/-0x48): inert, slots unchanged;
            //   - the rounding inlined into the `delay` expression: inert;
            //   - `gr.mWindowLen + span` for the `add r10, r30, r10` at
            //     82E4B510: inert (the allocator canonicalises the order);
            //   - `unsigned windowLen` local stored then reused (the image
            //     stores 0x28(r11) at 82E4B4BC before the startOffset
            //     conversion): 87.6, 13 inserts;
            //   - `rounded` computed before the window loop: 86.5.
            // The -0x48/-0x40 temp-slot order does not follow source order
            // of the two conversions; with the r9/r10 and f0/f13 swaps that
            // is the whole residual (22 diff_arg + 17 insert/delete rows).
```
