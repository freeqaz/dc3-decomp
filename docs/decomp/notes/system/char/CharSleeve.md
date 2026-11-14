# Lane notes: `src/system/char/CharSleeve.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@CharSleeve@@UAAXXZ`

`CharSleeve::Poll` -- moved verbatim from `src/system/char/CharSleeve.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Above the definition (was lines 69-93):

```text
// w26-f10 stop note.  Canonical 100.0, fuzzy 99.795: ten `diff_arg` rows, all
// commutative operand order, and in all ten we emit the image's two operands
// reversed (55 and 139 `fadds` on `absed + mPosLength`, 235 and 403 the `fmadds`
// of the inlined ScaleAdd/ScaleAddEq, 323 and 330 two products of the first
// Normalize(tf90.m.z), 453/454/456/460 four of the second).
//
// Not a class rule, and not the 8.8 base-binding lever.  The same expressions
// occur again in this one function with the OPPOSITE order and score equal:
// `absed + mPosLength` at ClampEq's upper bound is equal while the other two
// sites are charged, and of the six products in Normalize two are charged in
// the first copy and four in the second.  So the operand keys are the per-site
// temp sids of C2RS-BRIDGE 8.7 lever 3, not a property of these spellings.
//
// Measured on this row, all with nsearch (per-target, exact for src/system):
//   const float &posLength = mPosLength;  used at one / all three sites
//                                           99.754 (worse)
//   const Vector3 &mx / &mz before Cross     99.774 (worse)
//   const Vector3 &pv2 = ...WorldXfm().v     99.755 (worse)
//   const Vector3 &pmx2 over the whole
//     mTopSleeve block                       92.846 (much worse)
// The CalcError lever (name a single-use subexpression so it changes leaf
// class) has no target here: every charged operand is already a leaf, and
// naming either side of `absed + mPosLength` only moves the sids.  What is
// left is an id-budget question -- how many temps the image mints before this
// block -- of the kind 8.9 describes, and no source spelling tried moves it.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.795, 10 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
