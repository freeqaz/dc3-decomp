# Lane notes: `src/system/char/CharGuitarString.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@CharGuitarString@@UAAXXZ`

`CharGuitarString::Poll` -- moved verbatim from `src/system/char/CharGuitarString.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 09e8d1c21)

Inside the body (was lines 64-68):

```text
    // w17-c: both differences spelled out at the call site in y, z, x order and
    // the denominator written as the image's (y + z) + x; with Subtract() the
    // load order could not follow (the w14-b note below measured parentheses
    // alone at 87.9).  Found by a 36x8 non-PCH probe sweep: this is the only
    // combination with zero diff rows.
```

Inside the body (was lines 77-82):

```text
    // w14-b: the numerator is the image's own association, (z + x) + y
    // (fmuls z, fmadds x, fmadds y); Dot(tmp, tmp2) sums x, z, y. MSVC honours
    // the parentheses, so this is the same float result the image computes.
    // 98.65 -> 99.96. Residual (12 rows): the denominator, image (y + z) + x,
    // ours (x + y) + z; spelling it with explicit parentheses in any order is
    // 87.9 (MSVC re-schedules every load), LengthSquared/flat sum inert.
```
