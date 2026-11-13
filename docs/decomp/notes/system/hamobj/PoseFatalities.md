# Lane notes: `src/system/hamobj/PoseFatalities.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawDebug@PoseFatalities@@QAAXXZ`

`PoseFatalities::DrawDebug` -- moved verbatim from `src/system/hamobj/PoseFatalities.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 754-759):

```text
    // Player 0
    // w21-bl: was `bool player0Active = false; if (mCurrentBeat >=
    // mFatalStartBeats[0]) player0Active = mInFatality[0];` (99.07, a 30-row
    // r21/r22 + r18/r19 cascade off one hoisted `li r11, 0`). w7-ag refuted the
    // `cond ? x : false` and `cond && x` spellings; an inline member with an
    // early `return false` (ActiveInFatality) emits the image's li-after-cmpw.
```

Inside the body (was lines 769-776):

```text
            // The image builds the colour BEFORE the compare call -- the four
            // `stfs f31/f30, 0x60..0x6c(r31)` at 824934E0..824934F8 sit between
            // `bl Draw@DebugMeter` and `bl CompareSkeletonPositions`, and the
            // DrawBar argument is then two `ld`s off the same slot.
            // w21-bl: the colour is a temporary in the call, and the compare
            // result is the argument itself: args evaluate right to left, so
            // the four colour stores land before CompareSkeletonPositions
            // (824934E0..F8) and f1 is set after `fmr f2, f1` (82493514).
```

Inside the body (was lines 802-806):

```text
            // w25-mb: the squared score is a named local.  Built inline in the
            // constructor call, the colour's alpha store and the 0x68 `ld` were
            // issued ahead of red/green and the 0x60 `ld` (5 rows); named, the
            // stores come out blue, red, green, alpha and the halves load 0x60
            // then 0x68, as in the image.
```
