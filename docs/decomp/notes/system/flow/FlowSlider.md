# Lane notes: `src/system/flow/FlowSlider.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?UpdateActivations@FlowSlider@@IAAXXZ`

`FlowSlider::UpdateActivations` -- moved verbatim from `src/system/flow/FlowSlider.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 166-181):

```text
// w18-d (98.95, same 12 rows): both range-fraction guards as a real static
// inline helper returning 0.0f is byte-identical to the open-coded form.
// w21-p (98.95, same 12 rows; behaviour re-read against the image: every
// permuted f30/f31 row carries the same VALUE -- image f31 = 1.0f, f30 = 0.0f
// -- and every branch lands on the same block): `zero` replaced by the 0.0f
// literal (one stays a local), and the first fraction guard written
// `if (!(...)) f = ...; else f = zero;`, are both byte-identical.
// w21-an (98.95, same 12 rows): the f30/f31 constant assignment is NOT
// decided by declaration order or by constant value -- re-measured: decls
// swapped inert; zero changed to 3.0f / 0.5f / -1.0f (probe only) still lands
// in f31.  Also inert: a static inline RangeFraction() helper used as
// `t = one - RangeFraction(...)`, literal 1.0f to mEaseFunc, no casts on the
// mEaseFunc call, `zero` in the sIntensity compares, one/zero declared in the
// loop body, the two intensity=one arms joined with ||, an else-if for the
// ActivateChild arm, a FlowNode* copy for IsRunning().  Worse: savedIntensity
// declared after one/zero (95.71), `intensity = zero;` as a default (97.38).
```

Inside the body (was lines 189-196):

```text
    // NEGATIVE RESULT (w7-ap, 2026-09-14, 97.9 canonical): swapping these two
    // declarations is BYTE-IDENTICAL.  The residual f30/f31 swap (image
    // f31 = 1.0f, f30 = 0.0f; ours reversed, 10 rows) is a live-range
    // assignment, not a declaration-order effect.  w14-f re-checked after the
    // fixes below: swapping them, and replacing both locals with literals, are
    // still byte-identical (99.0 canonical, 12 rows: the f30/f31 swap plus the
    // `lwz r11, 0(r30)` vtable load scheduled before `mr r3, r30` for
    // curCase->IsRunning(); `IsRunning()` without `> 0` is also identical).
```

Inside the body (was lines 210-220):

```text
        // The original called an inlined range-fraction helper that guards all
        // three degenerate cases, so the range test is issued a SECOND time
        // inside the fraction: the image emits `fcmpu cr6, f0, f13` / blt,
        // `fcmpu cr6, f0, f12` / bgt and `fcmpu cr6, f13, f12` / beq at
        // 0x82421454-0x82421474 after the outer test has already passed.
        // w14-f: the outer test is `!(mValue > nextPos)` -- the image's `bgt`
        // skip, which ALSO enters on NaN.  Spelled `mValue <= nextPos`, MSVC
        // proved the inner `mValue > nextPos` false and folded it (the missing
        // fcmpu/bgt pair), and clang on native would skip the block on NaN where
        // the image enters it.  Holding lo = curCase->Value() in a local, like the
        // second helper below, gives the image's f13 (lo) / f12 (hi) assignment.
```

p2-b95 (2026-11-14): NOT A BUG. Every non-equal row was read against the listing, and
stores plus call arguments were compared symbolically on both sides. Residual: stores
equal under symbolic sim.


## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/flow/FlowSlider.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

File-level (was lines 306-342; no definition follows it):

```text
// ---------------------------------------------------------------------------
// w48-b (2026-11-06) EQUIVALENCE CERTIFICATE -- FlowSlider::UpdateActivations,
// canonical 98.95288 / fuzzy 98.69110.  191 instructions and 764 B on BOTH
// sides (180 equal / 10 diff_arg / 1 insert / 1 delete).
// Placed at END OF FILE so that no line above it moves (this TU has no
// MILO_ASSERT at all).
//
// BEHAVIOUR: no divergence, in two parts.
//   (a) 10 rows are ONE BIJECTION OF TWO FLOAT CONSTANTS.  The image has
//       f31 = 1.0f and f30 = 0.0f; ours has f30 = 1.0f and f31 = 0.0f.
//       Substituting T.f30 <-> B.f31 makes all ten identical: the two pool
//       loads (idx 35/36), the fmr/fsubs rows (87, 88, 107, 111, 141, 143)
//       and both `fcmpu ..., 0.0f` rows (160, 179).  Nothing reads a
//       different value anywhere.
//   (b) idx 146/148: `lwz r11, 0x0(r30)` -- the curCase vtable load for
//       IsRunning() -- is emitted one instruction EARLIER here than in the
//       image, on one side of `mr r3, r30`.  Same pair, same order relative
//       to the mtctr/bctrl.
//
// MECHANISM (c2rs tap, 40 candidates): the two pool constants are
//   id 25  pri -33 tie 35 ben 6 ref 6 sym 46/k03 -> f31  [the 0.0f constant]
//   id 26  pri -33 tie 34 ben 5 ref 5 sym 47/k03 -> f30  [the 1.0f constant]
// IDENTICAL priority.  The tie key -- the forward-walk visit ordinal -- decides
// by exactly ONE unit, 35 > 34, and the image has them the other way round.
// Every source spelling of BOTH constants is BYTE-IDENTICAL, re-measured this
// wave: `t = 1.0f - f`, `f = 0.0f`, `t = 0.0f`, `intensity = 0.0f`, `(double)1`
// in the mEaseFunc call, all uses replaced by literals at once, and the two
// declarations swapped.  So the tie is set by the IL visit order and not by the
// source text, and the constants are not source-visible candidates.
// The only spellings that move that order are the operand orders of the two
// comparisons that consume the 0.0f constant --
// `sIntensity == 0.0f && mAlwaysRun == 0` and
// `mAlwaysRun > 0 || sIntensity != 0.0f` -- and both cost 3.5pp (95.49738),
// because they are what shares the constant's live range in the first place.
// `IsRunning() > 0`, `IsRunning()` and `IsRunning() != 0` are all
// byte-identical, so the scheduling row has no source handle either.
// ---------------------------------------------------------------------------
```
