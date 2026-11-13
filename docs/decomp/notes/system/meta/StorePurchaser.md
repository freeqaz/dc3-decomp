# Lane notes: `src/system/meta/StorePurchaser.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?OnMsg@XboxPurchaser@@AAA?AVDataNode@@ABVUIChangedMsg@@@Z`

`XboxPurchaser::OnMsg` -- moved verbatim from `src/system/meta/StorePurchaser.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 81-87):

```text
            // w25-gc: a return per arm adds references to the hidden sret
            // pointer, lifting its colour priority (-3 -> 19) above the
            // RemoveSink temp and `this`, which gives the image's r30 (sret),
            // r29 (temp), r28 (this). MSVC cross-jumps the two returns back
            // into one tail after colouring, so the emitted code is unchanged;
            // the tail's mType-before-mValue store order comes out right with
            // `return 0` in both arms.
```


## `?OnMsg@XboxMultipleItemsPurchaser@@AAA?AVDataNode@@ABVUIChangedMsg@@@Z`

`XboxMultipleItemsPurchaser::OnMsg` -- moved verbatim from `src/system/meta/StorePurchaser.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of d3ee0dfbb)

Inside the body (was lines 168-174):

```text
            // w25-gc: a return per arm adds references to the hidden sret
            // pointer, lifting its colour priority (-3 -> 19) above the
            // RemoveSink temp and `this`, which gives the image's r30 (sret),
            // r29 (temp), r28 (this). MSVC cross-jumps the two returns back
            // into one tail after colouring, so the emitted code is unchanged;
            // the tail's mType-before-mValue store order comes out right with
            // `return 0` in both arms.
```
