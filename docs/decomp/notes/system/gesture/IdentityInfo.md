# Lane notes: `src/system/gesture/IdentityInfo.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Identified@IdentityInfo@@AAAXI@Z`

`IdentityInfo::Identified` -- moved verbatim from `src/system/gesture/IdentityInfo.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 10-15):

```text
    // w8-i: -5 and -4 carry SEPARATE bodies rather than sharing a label group.
    // Stacked as `case -5: case -4: case -1:` the two contiguous labels reach one
    // basic block and MSVC range-merges them into `blt`/`ble`; the image tests all
    // four with `beq`.  Giving each its own body defeats the merge at lowering
    // time, and the identical blocks are tail-merged afterwards -- which is why the
    // image's two `beq`s both target the single `li r4, -0x2`.
```
