# Lane notes: `src/system/synth_xbox/PitchCorrectedVoice.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetCorrection@PitchCorrectedVoice@Synapse@DSP@@QAAMXZ`

`DSP::Synapse::PitchCorrectedVoice::GetCorrection` -- moved verbatim from `src/system/synth_xbox/PitchCorrectedVoice.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 17-23):

```text
// w17-e (97.73, 26 rows): the non-register difference is that the image
// RELOADS mSmoothedCorrection (`lfs f0, 0x8(r31)`) after the `mPrevFreq =
// mFreqCounter` store, where we reuse the value loaded for the attack/release
// test; the rest is the FPR renaming that follows.  Not chased further.
// w20-s: branch row 100 lands on the image's `stfs f12,0x30(r31); lfs f0,
// 0x8(r31)` vs our fmsubs -- the w17-e reload above, reached on both arms on
// both sides; mSmoothedCorrection = (f28*f27 - s)*coef + s either way.  ARTIFACT.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: target
re-loads mSmoothedCorrection after the mPrevFreq store; values identical.
