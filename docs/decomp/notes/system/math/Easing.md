# Lane notes: `src/system/math/Easing.h`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?EaseElasticIn@@YAMMMM@Z`

`EaseElasticIn` -- moved verbatim from `src/system/math/Easing.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Inside the body (was lines 139-144):

```text
        // w13-d (92.86): the image compares `power` against a RUNTIME int->float
        // conversion of the constant 1 (`li r11,1; extsw; std; lfd; fcfid;
        // frsp` at 0x82411A44).  `power < 1`, `< true`, `< Max(1, 0)` and an
        // `int minPower = 1` local all fold it to the 1.0f held in f31.
        // w15-b: `abs(1)` -- the int abs() of Penner's `a < abs(c)` -- is what
        // survives as a runtime conversion: 92.86 -> 100 (84/84 equal).
```


## `?EaseStairstep@@YAMMMM@Z`

`EaseStairstep` -- moved verbatim from `src/system/math/Easing.h` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of e112f97a5)

Inside the body (was lines 336-349):

```text
    // w13-d (96.15, 3 rows: the image's fdivs sits before the EasePolyInOut
    // call into f31, ours after it).  Refuted, all bit-identical or worse:
    // a named `inv` before the call; `tmp_f30 -= tmp_f26` as its own statement
    // between the fdivs and the call; `/ f3` in the return (89.2 -- the 1.0f
    // stops being held in f28).
    // w18-d (96.15, same 3 rows): also inert -- the reciprocal moved above
    // floor(), and the call result in its own `ret` local.
    // w24-c2: TWO divisions, the w21-ap probe spelling.  MSVC (/fp:fast is
    // the Xenon default, see docs/decomp/patterns/fixable-fsel-fma.md) folds
    // them into one `fdivs f31, f28, f31` reciprocal right after floor() and
    // (p + fl) * inv -- the image's exact sequence.  Natively (no fast-math)
    // this is two rounded divisions and an add, at most ~1 ulp from the
    // image's (p + fl) * (1/steps); the same trade the NgMat per-division
    // temps made.
```
