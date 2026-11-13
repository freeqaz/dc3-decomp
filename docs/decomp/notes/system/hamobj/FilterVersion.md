# Lane notes: `src/system/hamobj/FilterVersion.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?NodeInput@Ham1FilterVersion@@UBAXHPBVDetectFrame@@W4MoveMode@@AAUErrorNodeInput@@@Z`

`Ham1FilterVersion::NodeInput` -- moved verbatim from `src/system/hamobj/FilterVersion.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 50-58):

```text
    // w21-f (85.0 -> 100): read the two DetectFrame members DIRECTLY (friend),
    // one inline level fewer than the GetMoveFrame()/Mirror() accessors.  With
    // the by-value accessors MSVC read the mirror off the still-live incoming r5
    // (`lwz r11, 0xc(r5)` + `mr r6, r11`); the image sets mode first (`mr r5, r6`)
    // and loads both members late off the saved copy (`lwz r6, 0xc(r30)`,
    // `lwz r3, 0x4(r30)`), which is what the direct member reads give.  The
    // nine spellings w8-i/w9-c refuted all kept the accessors.  (Making both
    // accessors return `const &` also reaches 100; the friend read keeps the
    // header's accessors, and every other caller, unchanged.)
```
