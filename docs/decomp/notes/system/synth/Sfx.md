# Lane notes: `src/system/synth/Sfx.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `??0SfxInst@@QAA@PAVSfx@@@Z`

`SfxInst::SfxInst` -- moved verbatim from `src/system/synth/Sfx.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 15-33):

```text
// Open: fuzzy 99.744 / normalized 100.0, three rows, all one commutative
// `fadds` operand order -- image `fadds f1, f0, f13`, ours `fadds f1, f13, f0`
// (the loads themselves, and their order, already match).
// At the §8.7 sort both operands are memory leaves: `it->Volume()` has base
// `it` (the SfxMap pointer) at disp 0x14, `mRandVol` has base `this` at disp
// 0x30.  Per §8.7 the key is ((sid & mask) << 13|14) + fold(disp), so the
// IMAGE's order (Volume first) is what two VARIABLE bases give (a local
// outranks a param), and our order is what two bases of equal sid residue give,
// where the displacement decides and 0x30 beats 0x14.  That reading is inferred
// from the emitted order -- the c2 tap's cqlo3 dump for this function was
// searched and no tuple carrying both of these displacements is printed, so the
// fadds tuple itself was NOT read from the tap.
// w26-f6 measured and rejected, all inert or worse: swapping the source
// operands (canonicalised), binding the `it->X()` result or `mRandX` as a
// const float reference, plain named copies of either, `const SfxMap &map =
// *it`, `SfxMap *map = it`, naming `it->Sample()`'s result, and hoisting
// mRandVol/mRandPan/mRandTp to the top of the loop body.  Open question: which
// surviving named load upstream moves exactly ONE of the two base temps' sid
// mod 4 without changing the emitted loads.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.744, 3 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
