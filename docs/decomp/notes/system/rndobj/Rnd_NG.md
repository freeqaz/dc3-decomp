# Lane notes: `src/system/rndobj/Rnd_NG.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?EstimateDraw@@YAMH@Z`

`EstimateDraw` -- moved verbatim from `src/system/rndobj/Rnd_NG.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Above the definition (was lines 170-189):

```text
// w26-h: 100.0.  The last two charged rows were [5] `addi r8, r11, 0x4` and
// [8] `addi r5, r11, 0x8`, swapped against each other: in the INNERMOST add of
// the chain the image fuses mParts into the `fmadds` and leaves mPartSys as the
// standalone `fmuls`, while we did the reverse.  Both compute
// mPartSys*0.005 + mParts*0.00023333334, so the rows were a difference of
// rounding only -- but they are the same rows the earlier waves could not move.
//
// Mechanism (c2rs tap, C2RS-BRIDGE sec 8.7).  c2 sorts each commutative tuple's
// operands descending by a key and the LAST sort, after lowering, is the emitted
// order; the operand that lands second is the one folded into the `fmadds`.  A
// product of an int->float conversion and a constant is a subexpression leaf
// (key SU<<24|nodes<<16|hash16); naming the conversion in a `float` local turns
// it into a variable leaf (key 0x10000|sid<<5) and moves it in the sort.  One
// named local is the whole fix; the arithmetic is unchanged.
//
// Refuted before that, each a full ninja + report.json read and all inert at
// 99.808690: swapping the two innermost terms, writing both innermost muls
// constant-first, naming the mPartSys *product*, and binding
// `NgStats &stats = gNgStats[idx]` (18.59) -- the last one, like a flat
// left-associated sum (52.53), changes the shape more than one operand.
```
