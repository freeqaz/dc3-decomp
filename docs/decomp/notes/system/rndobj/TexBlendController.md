# Lane notes: `src/system/rndobj/TexBlendController.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?GetBlendState@RndTexBlendController@@QBA?AW4BlendState@1@AAMM@Z`

`RndTexBlendController::GetBlendState` -- moved verbatim from `src/system/rndobj/TexBlendController.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 144-181):

```text
            // Smoothstep.  MSVC canonicalises this into `fmuls 2.0*t3` +
            // `fmsubs t2*3.0 - that`; the image keeps -2.0f as a literal and
            // emits `fmadds t3, -2.0, t2*3.0` (so -2.0f survives as a literal).
            // Four spellings measured, ALL byte-for-byte identical to this one:
            // this order; the swapped addition `t2*3.0f + t3*(-2.0f)`; a
            // two-statement accumulator `float s = t2*3.0f; s += t3*(-2.0f);`;
            // and constant-first multiplies `3.0f*t2 + -2.0f*t3`.  MSVC
            // canonicalises the addition before forming the FMA and fuses the
            // term whose multiplicand is defined FIRST (t2) -- the opposite of
            // the image's choice.  2 rows, and 4 more under name_check.
            //
            // Wave 7, lane w7-y: that "defined FIRST" reading was tested both
            // ways and does not hold.  Hoisting `float t3;` above t2's
            // definition and assigning it afterwards is INERT (still 6 rows).
            // Defining t3 outright first, `float t3 = blend*blend*blend;
            // float t2 = blend*blend;`, is WORSE -- CSE still emits t2's
            // multiply first but now swaps its operands, adding a seventh row
            // (6 -> 7).  Reverted.  What the image actually keeps is -2.0f as a
            // LITERAL: MSVC rewrites any `a*3 + b*(-2)` we write into the
            // subtraction `a*3 - b*2` and loads 2.0f instead, so the constant
            // pair is downstream of the fusion choice, not a separate lever.
            // w15-a (99.22 canonical): a named `float negTwo = -2.0f;` multiplier is
            // INERT too -- the constant is still folded to 2.0f and the fmsubs stays.
            // w19-a (still 99.22): the -2.0f routed through an inline helper's
            // PARAMETER (`t3 * a + t2 * b` called with -2.0f, 3.0f) is folded to
            // the same fmuls 2.0 / fmsubs; the factored `t2 * (3.0f - 2.0f *
            // blend)` is worse (96.47, fnmsubs).
            // w26-p: 99.2157 -> 100.0 canonical.  What moves the fusion onto
            // the image's term is the SEPARATE `n3` statement.  Written as one
            // expression, every `a*3 + b*(-2)` spelling (and the negated-whole
            // `-(2.0f*t3 - 3.0f*t2)`, which reaches 99.3137) is canonicalised
            // by c2 into `fmuls 2.0*t3` + `fmsubs t3, 2.0, that`.  Materialised
            // as a single-use local first, `-2.0f*t3` keeps its negative
            // constant and c2 fuses it back into the add the image's way:
            // `fmuls t2*3.0` then `fmadds t3, -2.0, that`, with -2.0f surviving
            // as the __real@c0000000 literal.  Exact: -2.0f*t3 == -(2.0f*t3)
            // bit-for-bit, and the multiply is fused back into the fmadds, so
            // no extra rounding is introduced on either compiler.
```
