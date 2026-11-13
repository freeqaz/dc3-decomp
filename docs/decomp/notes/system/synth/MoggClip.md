# Lane notes: `src/system/synth/MoggClip.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?SetupPanInfo@MoggClip@@QAAXMM_N@Z`

`MoggClip::SetupPanInfo` -- moved verbatim from `src/system/synth/MoggClip.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 421-477):

```text
// SOLVED 2026-09-30 (w9-c), 78.387% -> 100.0%.  The lever is the two pairs of
// PARENTHESES below, and nothing else: `(panWidth * -0.5f) + pan` instead of
// `-panWidth / 2.0f + pan`.
//
// The target computes the two pans as two INDEPENDENT fused multiply-adds, each
// loading its own signed half:
//     lfs    f0, __real@bf000000   ; -0.5f
//     fmadds f1, f2,  f0, f1       ; panWidth * -0.5 + pan
//     bl     SetPan                ; channel 0
//     lfs    f0, __real@3f000000   ; +0.5f   (rematerialised after the call)
//     fmadds f1, f30, f0, f31      ; panWidth * +0.5 + pan
// Unparenthesised, Xenon MSVC's /fp:fast pass hoists the negative literal's sign
// out of the product (`x * -0.5f + y` -> `y - x * 0.5f`), which makes BOTH arms
// share the single product `panWidth * 0.5f`.  It then CSEs that product into f30
// across the intervening SetPan call and reaches the two call sites with
// `fmuls f30, f2, f0` / `fsubs f1, f1, f30` / `fadds f1, f30, f31` -- same values,
// one multiply fewer, and it cost 7 of 33 rows.  Explicit parentheses are a
// grouping barrier for that pass: the sign stays inside the product, the two
// constants stay distinct (bf000000 and 3f000000, both of which the target's
// .rdata holds), there is nothing left to CSE, and panWidth is homed into f30
// exactly as the image does it.  31 of 31 instructions equal.
//
// Twelve spellings measured here, one full ninja each.  GROUPING is the only axis
// that moves the score; constant spelling and operand order are all inert:
//   78.387% (byte-identical to the old `/ 2.0f` form, i.e. the CSE survives):
//     -panWidth / 2.0f + pan   |   panWidth / -2.0f + pan
//     pan + panWidth * -0.5f   |   -0.5f * panWidth + pan
//     pan + -0.5f * panWidth   |   -(panWidth * 0.5f) + pan
//     pan - panWidth * 0.5f  /  pan + panWidth * 0.5f
//     pan - panWidth / 2.0f  /  pan + panWidth / 2.0f
//     named `const float` halves (folded, then CSEd)
//     second product hoisted into a post-call local
//   76.903% (WORSE -- breaks the CSE but pays a runtime `fneg` the image folds
//     into its literal):  `float negWidth = -panWidth; negWidth * 0.5f + pan`
//     and `(-panWidth) / 2.0f + pan`
//   95.484%: `panWidth * -0.5f + pan` / `pan - panWidth * -0.5f` -- the asymmetry
//     leaves the two constants distinct so the CSE dies and the structure becomes
//     exact, but both arms come out as `fnmsubs` (the sign-hoisted form) where the
//     image has `fmadds`.
//   100.0%: the parenthesised form below, and equivalently each product bound to
//     its own local (`float leftOffset = panWidth * -0.5f; SetPan(0, leftOffset + pan);`).
//     A local is a grouping barrier for the same reason; the parens are cheaper.
// Parenthesising only ONE arm is not enough (78.710% / 78.387%): the surviving
// unparenthesised arm still offers the shared product.
//
// The channel/sign pairing is confirmed by the two distinct SINGLE-precision
// literals -- __real@bf000000 ahead of the `li r4, 0` SetPan and __real@3f000000
// ahead of the `li r4, 1` one -- so the arithmetic is float, not double.
//
// Retracted by the above: the earlier note that this was a "/fp:fast
// REASSOCIATION floor ... codegen wall; do not re-dig", and the imported
// rb3-xenon adjudication of four spellings.  Those four refutations reproduce
// exactly (all 78.387% here) -- they were just all drawn from the one axis that
// does not matter.  Also retracted: the claim that a `2.0` DOUBLE literal on
// channel 0 "scores HIGHER (92.9%)".  It does, and it is still provably not the
// original source (it emits an lfd of a double 0.5 plus an frsp the target lacks),
// but the honest spelling now scores 100.0% and the point is moot.
```
