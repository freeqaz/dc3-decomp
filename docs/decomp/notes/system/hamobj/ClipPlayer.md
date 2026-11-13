# Lane notes: `src/system/hamobj/ClipPlayer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?CanUseRestStep@ClipPlayer@@QAA_NXZ`

`ClipPlayer::CanUseRestStep` -- moved verbatim from `src/system/hamobj/ClipPlayer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 81-87):

```text
    // In non-edit mode (or when transitions are enabled), check if the out clip
    // is compatible with rest steps. Rest steps require a 3-beat clip with no flag 0x4.
    // The body is ONE returned short-circuit expression (the image converges
    // every true path on `li r11, 1` + a single `clrlwi`). mOutClip is assigned
    // INSIDE the expression so its load stays after the EditMode test, where
    // the image has it; a `CharClip *clip = mOutClip;` initialiser hoists it
    // into the prologue (w7-av measured that at 94.29).
```


## `?AnnotatePractice@ClipPlayer@@QAA?AVDataNode@@XZ`

`ClipPlayer::AnnotatePractice` -- moved verbatim from `src/system/hamobj/ClipPlayer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 93-111):

```text
// RESIDUAL (w12-d, 98.73): one insert/delete pair -- in the second
// EditMode/NoTransitions test the image loads TheHamDirector (`lwz r10`)
// BEFORE `lbz r11, 0xc(r27)`; we emit the lbz first. Measured INERT: a
// `bool editMode` local hoisted above `f31 = mPracticeEnd`, a hoisted
// `HamDirector *director` local, and `!(EditMode() && NoTransitions())`.
// w21-o (98.73, unchanged): behaviour re-read against the image -- every
// Annotate value/label and both tests match.  Both sides jump-thread the
// first test's skip arms onto `lfs f31` (reusing the r10/r11 loads), so the
// swapped pair is the reload block after the "" Annotate only.  Inert: one
// function-scope `f31` shared by both blocks; `if (EditMode && NoTransitions)
// {} else {...}`.  Worse: a static inline predicate for the test (91.1).
// w21-bb (98.73, same 2 rows): the swapped pair is in the block MSVC
// duplicates onto the inClip-taken path only (both skip arms land on
// `lfs f31`), so it is the order of that partial-redundancy reload.  Inert:
// `LoadMgr &` alias for either test; the f30 literal inlined / f30 declared
// late; folding `&& (outClip = mOutClip)` into the second test; re-storing
// the "" Annotate's value into the block-1 local; a leading
// `TheHamDirector,` comma operand on the second test.  Worse: `if (mInClip)`
// read twice by member (95.4).
```

Inside the body (was lines 117-123):

```text
    // w27-e: bind the director POINTER by reference, not by copy.  A reference
    // folds, so both tests still read TheHamDirector at their own use sites
    // (the image reloads it too, twice), but the extra named value re-keys the
    // IL so the scheduler issues `lwz r10, TheHamDirector@l(r28)` before
    // `lbz r11, 0xc(r27)` in BOTH window tests.  A copy here is wrong: c2 keeps
    // it in a callee-saved register across the Annotate calls and the prologue
    // grows to r24-r31 (measured 99.34).  98.734 -> 100.
```


## `?GetRoutineCrossoverClips@ClipPlayer@@IAAXMPBDPAPAVCharClip@@1@Z`

`ClipPlayer::GetRoutineCrossoverClips` -- moved verbatim from `src/system/hamobj/ClipPlayer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 586-590):

```text
            // w25-gm: the named practiceBeat and transLen locals each add one
            // lowering temp ahead of this block, moving mBeatOffset's base temp
            // to sid = 0 mod 4 (c2rs 8.7 memory-leaf key 0x10008), so beatDiff
            // sorts first in `fadds f0, f31, f0` like the image; the other
            // three bases land on 1 mod 4 (0x14008) and stay memory-first.
```
