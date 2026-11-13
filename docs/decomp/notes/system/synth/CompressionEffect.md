# Lane notes: `src/system/synth/CompressionEffect.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Process@CompressionEffect@@QAAXPAMHH@Z`

`CompressionEffect::Process` -- moved verbatim from `src/system/synth/CompressionEffect.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 53-62):

```text
// w16-e (98.73): residual is the hoisted-constant FPR assignment (image
// f27..f31 for 1.15/0.001/0.002/1.0/0.02, ours f3/f4/f27..f29) and the
// f12/f13 relabelling it drags.  A plain `for (channel...)` without the
// numChannels guard is worse (95.8, 161 vs 159 instructions).
// w21-av: the gate update writes mGateMin directly in both arms and the 1.15
// test reads mGateMin back (the image stores, then multiplies the stored
// value: stfs f0,0x2c(r3); fmuls f0,f0,f27).  The `attack_release` local was
// ours; dropping it fixed the whole hoisted-constant FPR colouring.  Left:
// one `fmuls f0,f12,f0` operand order (sample*mDCBlock) in the output loop;
// `mDCBlock * samples[idx]` and a `float sample` local are inert.
```
