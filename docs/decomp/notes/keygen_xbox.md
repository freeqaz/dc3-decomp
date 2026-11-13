# Lane notes: `src/keygen_xbox.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?getMasher@KeyChain@@YAXPAE@Z`

`KeyChain::getMasher` -- moved verbatim from `src/keygen_xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 106-110):

```text
    // w12-c: byte-identical once the unit is built /Od /Os (objects.json) --
    // that flag is what makes the image recycle r11/r10 per statement and test
    // needs_byteswap against cr0.  The outer locals' frame slots are assigned
    // from a hash of their NAMES under /Od (not declaration order), so `one`
    // and `masher` are the names that land at 0x58/0x50 as in the image.
```


## `?opaquePredicate@@YAXXZ`

`opaquePredicate` -- moved verbatim from `src/keygen_xbox.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 186-199):

```text
// w8-g: opaquePredicate 75.00%, random 83.33% (normalized, full ninja), and the
// two share one cause, so they stand or fall together.  The image materializes a
// global's address in three instructions -- `lis r11, sym@ha` / `addi r11, r11,
// sym@l` / `lwz r10, 0(r11)` -- where we fold the displacement into the load:
// `lis r11, sym@ha` / `lwz r10, sym@l(r11)`.
// w12-c: the unit is now built /Od /Os, which is the image's flag set (18 of
// the 20 functions are byte-identical under it; the w8-g note that /Os moved 16
// functions off their match did not reproduce).  It still folds the addi.  Also
// inert under /Od /Os, probed in a scratch TU: a definition in this TU (plain,
// static, volatile, initialised, __declspec(align(16)), selectany, a #pragma
// bss_seg / __declspec(allocate) section), extern "C", a 1-element array at
// constant index, a struct member at offset 0, `(&g)[0]`, a char* offset of 0,
// and /Ou /Oz /Oc /Og /GF /Gy /Zi /GS- /Qfast_transcendentals.  Only a
// non-constant array index or a nonzero struct offset splits it.
```
