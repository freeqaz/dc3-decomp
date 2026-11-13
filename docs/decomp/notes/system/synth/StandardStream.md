# Lane notes: `src/system/synth/StandardStream.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ConsumeData@StandardStream@@QAAHPAPAXHH@Z`

`StandardStream::ConsumeData` -- moved verbatim from `src/system/synth/StandardStream.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 835-839):

```text
            // One WriteData per arm: MSVC cross-jumps the two calls back into
            // the image's single call site AFTER colouring, but the duplicate
            // drops the loop down-counter's COLOR priority (6 -> -6), so it
            // pops after samplesToConsume and the memcpy size and takes r27
            // as in the image (c2rs tap, w25-gq).
```


## `?IsPastStreamJumpPointOfNoReturn@StandardStream@@QAA_NXZ`

`StandardStream::IsPastStreamJumpPointOfNoReturn` -- moved verbatim from `src/system/synth/StandardStream.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 885-897):

```text
    // w21-af: 97.727 -> 100. ONE condition for the true exit, with the two
    // jump-point tests written as negated `>=` so NaN still falls through to
    // true exactly as the image's two `bge`s do. Every earlier spelling (kept
    // in the history below) returned false from the jump-point tests as
    // separate statements, which let MSVC tail-duplicate a `li r3, 0x0`
    // before the last `bge`; as one || / && the false exit is the single
    // shared `li r3, 0x0` block at the top, as in the image.
    // History: w7-av/w9-c measured 97.727 for `if (curTime >= mJumpFromMs)
    // return false; if (mJumpFromMs >= fromTime) return false; return true;`;
    // a merged `curTime >= mJumpFromMs || mJumpFromMs >= fromTime -> false`
    // read 86.02, `return mJumpFromMs < fromTime;` 90.659, and w19-e's nested
    // `if (curTime < mJumpFromMs) { if (mJumpFromMs < fromTime) return true; }`
    // 86.0 (those `<` spellings also return false on NaN, unlike the image).
```
