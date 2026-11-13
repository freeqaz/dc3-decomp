# Lane notes: `src/system/rndobj/wordwrap.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?WordWrap_CanBreakLineAt@@YA_NPB_W0@Z`

`WordWrap_CanBreakLineAt` -- moved verbatim from `src/system/rndobj/wordwrap.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 229-245):

```text
    // w39-g (2026-11-06): `ch` and `prev` are NOT named locals in the original,
    // and that is what the old residual here was.  Hoisting `*cur` / `cur[-1]`
    // into `wchar_t` locals gives c1 a value whose 16-bit-ness it can prove
    // from the single `lhz` definition, so the `(unsigned short)c` truncation
    // inside CantStartLine/CantEndLine folds away -- and the image still emits
    // it (`clrlwi r6, r31, 16` on ch, `clrlwi r6, r4, 16` on prev).  Reading
    // the character at each use keeps the value 32-bit-typed at the call site,
    // the truncation survives, and the whole residual closes: 97.575760 ->
    // 100.000000 canonical, 96.454544 -> 99.969696 fuzzy, body 163 -> 165
    // instructions and 652 -> 660 bytes, exactly the image's 165 / 660.
    // `cur` is a parameter this function never advances, so re-reading it is
    // the same value every time -- behaviour is unchanged.  This also removes
    // the `mr r5, r3` entry copy (`cur` stays in r3 for the function's life, as
    // in the image) and flips `prev` back into r4 with its `mr r10, r4` copy.
    // Refuted here, so they are not retried: `unsigned int ch` alone 87.96,
    // `unsigned int prev` alone 96.61, both together 86.99, the helpers taking
    // `unsigned int c` 93.33, and dropping the `(unsigned short)` casts 97.58.
```
