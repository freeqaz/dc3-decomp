# Lane notes: `src/system/synth_xbox/PeakDetector.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Detect@PeakDetector@Synapse@DSP@@QAAXI@Z`

`DSP::Synapse::PeakDetector::Detect` -- moved verbatim from `src/system/synth_xbox/PeakDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 85-109):

```text
        // RESIDUAL (w7-al, 97.62 canonical): 10 charged rows, all regalloc /
        // scheduling, none reachable from source. Semantics were re-verified
        // instruction-by-instruction against 0x82E4ABC8 and are exact (the
        // unsigned rldicl widenings, the 1.2f / 2.0f / 0.5f / 0.4f constants,
        // the fsel round-half-away-from-zero, fnmsubs for mNextCenter, and the
        // gaussianWindow call order prev/center/next all agree).
        //  * rows 73-86, the `(mPeakPos + Size() - 1) % Size()` site: the image
        //    emits `mr r7, r9` to keep `end` live because its scheduler puts the
        //    `add` BETWEEN the two `srawi`s; ours emits the two `subf`s adjacent
        //    into distinct registers and needs no copy. Splitting the expression
        //    into two statements (`unsigned prevIdx = mPeakPos + Size() - 1;`
        //    then `prevIdx % Size()`) is byte-inert -- measured, still 97.62.
        //  * rows 139/140 + 224/225, (0x0,0x4) offset swaps: `Size()` is
        //    `end() - begin()` and the image loads begin first at all six
        //    `(float)Size()` sites. We already load begin first at four of the
        //    six; forcing it globally (a `const float *b = begin();` temp) would
        //    flip the six integer-context sites the wrong way, which currently
        //    all match. Per-site scheduler noise, not a spelling.
        // w19-e: 97.62 -> 99.99.  Both residuals above were ONE thing: Size()
        // spelled `end() - begin()` (two inline calls) where the image uses
        // the vector's own size() (`_M_finish - _M_start`, one inline level
        // fewer).  Remaining: 3 rows at the `mOrigin >= Size()` test below,
        // where the image loads _M_finish first (0x82E4AE98 `lwz r8, 0x4(r11)`)
        // and we load _M_start first.  Inert there: an explicit
        // `end() - begin()`; worse: `Size() <= mOrigin` (99.97, flips blt).
```
