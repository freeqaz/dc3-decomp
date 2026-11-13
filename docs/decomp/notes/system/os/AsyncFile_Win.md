# Lane notes: `src/system/os/AsyncFile_Win.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?_WriteAsync@AsyncFileWin@@MAAXPBXH@Z`

`AsyncFileWin::_WriteAsync` -- moved verbatim from `src/system/os/AsyncFile_Win.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 13de8b551)

Above the definition (was lines 145-160):

```text
// `aligned` is a BOOL here (w7-bn, 97.84 -> 99.3): the image sets it with
// `mr r28, r23` (the 1 already materialised for mWriteInProgress) and tests it
// with `clrlwi. r11, r28, 24` at 82605xxx, holding it in a callee-saved
// register across the two Tell() calls -- that register is the ninth GPR in
// the image's __savegprlr_22.  bg's _ReadAsync fix changed this function's
// local to `int` as well, which let MSVC fold the test away and drop to
// __savegprlr_23 (the 97.84 "collateral"); the fix in _ReadAsync itself is
// independent of this and stays.  Residual (all register permutation plus
// one `mr`): the image materialises the zero for `aligned = false` directly
// in r28 and lets the inlined memset borrow it; we materialise memset's zero
// in r11 and copy it (`mr r28, r11`).  Refuted at 99.3, full ninja each:
// `aligned` declared before the memset, before `mWriteInProgress = true`,
// `unsigned char aligned = 0`; declared above the asserts -> 95.6.
// w14-d (99.29, 1 row): the image reuses `aligned`'s zero register (r28) for
// the OVERLAPPED clear; we clear through r11 and copy.  Declaring `aligned`
// above the memset is byte-inert.
```
