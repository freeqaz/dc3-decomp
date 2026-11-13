# Lane notes: `src/system/hamobj/HamLabel.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Count@HamLabel@@UAAXHHMVSymbol@@@Z`

`HamLabel::Count` -- moved verbatim from `src/system/hamobj/HamLabel.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 71-79):

```text
    // w21-at: 99.96 -> 100 (fuzzy 100).  ONE Key<float> local, reused for both
    // pushes, with the second frame formed by `+= f3` on the stored first
    // frame.  The image's f30/f31 assignment, its frame-before-value store
    // order into 0x54/0x50(r1) for BOTH keys, and `fadds f0, f31, f30` all
    // follow from that; two temporaries (named or not), a hoisted sum, or a
    // reused key assigned `f1 + f3` from a separate local all kept the
    // allocator swap that w7-ak/w8-i/w9-a recorded as a floor.  Same values:
    // the default ctor's zeroes are dead stores, and frame + f3 is the same
    // single add.
```
