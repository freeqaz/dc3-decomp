# Lane notes: `src/system/rndobj/TransAnim.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Load@RndTransAnim@@UAAXAAVBinStream@@@Z`

`RndTransAnim::Load` -- moved verbatim from `src/system/rndobj/TransAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Attribution: definition is macro-generated (BEGIN_LOADS); symbol matched by the name the macro expands to

Inside the body (was lines 98-114):

```text
        // 2-row residual (lane w7-j, 2026-09-14): the shipped build keeps
        // &mTransKeys in the callee-saved r30 across the mRotKeys call
        //     subi r30, r31, 0x34 / bl >>Key<Quat> / mr r4, r30
        // where we rematerialise it afterwards as `subi r4, r31, 0x34`.  The
        // other 394 instructions are equal, so this is a register-allocator
        // rematerialise-vs-keep tie, not a source-shape difference we have
        // found.  REFUTED: naming a `Keys<Vector3, Vector3> &transKeys =
        // mTransKeys;` before the statement (MSVC folds the reference, output
        // byte-identical) and spelling the chain as explicit
        // `operator>>(operator>>(d, mRotKeys), mTransKeys)` (also identical).
        // w16-a: closed (99.60 -> 100).  The image calls the std::vector
        // BinStreamRev reader directly for both operands, and a nested
        // free-function chain evaluates its arguments right to left, so
        // &mTransKeys is formed before the first call.  Both refuted spellings
        // still went through math/Key.h's inline Keys wrapper, whose inlining
        // sequences the inner call first.  Binding the vector base is exactly
        // what that wrapper does, so behaviour is unchanged.
```


## `?MakeTransform@RndTransAnim@@QAAXMAAVTransform@@_NM@Z`

`RndTransAnim::MakeTransform` -- moved verbatim from `src/system/rndobj/TransAnim.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 242-267):

```text
// w20-l: normalized 100 / fuzzy 99.71. 9 rows, all /fp:fast commutative operand
// order (one fadds in the follow-path Add, two fmuls in the 1/scale rows, six in
// the closing Scale(v9c, tf.m, tf.m)); each row multiplies/adds the same pair as
// the image. Branch targets, the uninitialised-v70 Interp (image reads 0x90 too)
// and the AtFrame callee (ICF 826307C8: Vector3/Color/Quat) checked. Writing the
// Scale out as `tf.m.x *= v9c.x` etc. drops to 96.7 -- reverted.
// w25-gu: reading the rot keys through `rotKeys` renumbers the temps so four of
// the Scale rows take the image order (9 -> 5 rows). Open, c2 operand-sort key
// (C2RS-BRIDGE 8.7): each remaining row is `var * load(temp base)`, and the
// load's key depends only on the base temp's sid mod 4 (0x10008/0x14008/0x18008/
// 0x1c008) against the var key (inv.x 0x132c0, s.y 0x146e0, s.z 0x14700, v58.y
// 0x14400). The image needs the bases of m.x.y and m.x.z (V824/V828, now 0 mod 4)
// at 1, m.y.x (V833, 1) at 2 or 3, m.z.x (V846, 2) at 1, and the Add's tf.v.y
// (V1076, 0) at 2 or 3, while V1121..V1153 stay at 1. No single alias, inlined
// helper or temp spelling tried (~40 variants) moves those sids independently.
// w26-f11 (2026-11-02): 99.838 -> 99.935 (fuzzy; 2 rows of 309 left). Naming
// the y reciprocal as `invY` keyed that row onto a named leaf, and binding the
// aliased `tf.v` destination as `const Vector3 &tv` re-keyed the follow-path
// Add; both are C2RS-BRIDGE 8.7/8.8 wins, no behaviour change. The two rows
// left are 292 and 299, and they want OPPOSITE orders relative to ours:
//  292 image `fmuls f11, f11, f13` (load first), ours `f11, f13, f11`;
//  299 image `fmuls f0, f12, f0` (multiplier first), ours `f0, f0, f12`.
// They multiply by two DIFFERENT reciprocal temps (0x84(r1) vs 0x88(r1)), so
// the nine naming slots tried (x, y, z, each pair, all three, two reorderings)
// can only move one row at a time and every combination is worse overall than
// the y-only form landed here.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.935, 2 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
