# Lane notes: `src/system/hamobj/RhythmBattlePlayer.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `(file-level)`

Not attributed to one function -- moved verbatim from `src/system/hamobj/RhythmBattlePlayer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

File-level (was lines 107-111; nearest following definition: `RhythmBattlePlayer::Load`):

```text
// w15-r: the image's .rdata for this TU is [0.6f @0x8204DAF4, gRev, gAltRev];
// UpdateState loads the 0.6 from that named slot (`lfs f0, lbl_8204DAF4@l` at
// 0x824D5F54).  With gRev off offset 0 MSVC anchors Load's ASSERT_REVS pair on
// gAltRev like the image -- this closes the 98.35 "anchor pick" residual that
// used to be documented here (the gRevs[4] array lead is moot).
```


## `?Poll@RhythmBattlePlayer@@UAAXXZ`

`RhythmBattlePlayer::Poll` -- moved verbatim from `src/system/hamobj/RhythmBattlePlayer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 227-238):

```text
            // BEHAVIOURAL FIX 2026-09-14 (w7-q): this used to read
            //     4.0f - mWindowElapsedTime - f17
            // which subtracts the frame delta TWICE -- `mWindowElapsedTime`
            // was already advanced by f17 on the line above.  The image
            // issues exactly ONE fsubs against the just-stored accumulator
            // (RhythmBattlePlayer.s):
            //     fadds f0, f11, f31        ; mWindowElapsedTime + f17
            //     stfs  f0, 0x258(r30)      ; mWindowElapsedTime =
            //     lfs   f13, __real@40800000@l(r11)   ; 4.0f
            //     fsubs f0, f13, f0         ; 4.0f - mWindowElapsedTime
            // The extra term made the phrase-meter countdown handed to
            // SetRatingFrac run one frame-delta short every frame.
```


## `?UpdateScore@RhythmBattlePlayer@@QAAXPAVObject@Hmx@@@Z`

`RhythmBattlePlayer::UpdateScore` -- moved verbatim from `src/system/hamobj/RhythmBattlePlayer.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Above the definition (was lines 678-703):

```text
// w21-ad: behaviour re-read against the image (824D9DD0..824DA4E0): every
// branch, constant and store target agrees (ShouldAutoPass inlined twice,
// unk14 > 1.0f, min/max foot Y, the four accumulator clears, SetRatingFrac(0,-1),
// kDataInt test). Stopped at 99.554 normalized / fuzzy 98.41: the charged rows
// are the li 1/li 0 order at 824D9E90 (ours materialises the i10 zero first)
// and the autotrick addi one slot ahead of the 0x27c store. Measured spellings:
// `int i10 = 0;` at function top 99.0 (fixes the this=r30 cascade but hoists
// the li 0 above the mStealPart test), after the mStealPart block 99.1, after
// skelIdx 99.1, just before `if (mTrickSymbol == none)` 99.3 (zero and i10
// split, extra mr); ShouldAutoPass as if/return true/return false: identical.
// w21-bl: `int i10 = 0;` declared just before `mTrickSymbol = none;` (after
// the static Symbol block) closes the li 1/li 0 order, the this=r30 cascade and
// the autotrick addi slot at once (99.554 -> 100.000 normalized, i.e. 100
// modulo register permutation) -- 15 rows left, all one f29<->f30 swap: the
// image keeps 1.0f in f29 and -1.0f in f30, ours the reverse. Same values, same
// uses. Tried: explicit 1.0f/-1.0f literals (identical); Min/Max helpers are
// fsel shapes the image does not have.
// w26-f11 (2026-11-02): the 15 residual rows are that swap and nothing else --
// 453 instructions, 15 mismatched, all f29<->f30 (image 1.0f in f29, -1.0f in
// f30; ours the reverse). The c2-rs candidate dump shows the three k03 float
// constants at pri -14 / -16 / -26 with ben 12 / 3 / 10, i.e. their order is
// decided by benefit/reference counts, not by a leaf key. Measured spellings
// that leave those three triples unchanged: naming each constant, literal vs
// static spelling, and moving the SetRatingFrac(0, -1) call. The open question
// is what statement arrangement gives one of the two constants one more
// reference than the other.
```

Inside the body (was lines 761-766):

```text
    // REFUTED (w7-i): hoisting `none` into a local read before the division and
    // storing mTrickSymbol after it -- the RhythmBattle::Begin lever -- is
    // byte-identical here, and swapping these two statements costs 0.75pp (the
    // division's fdivs/stfs pair moves out of the static-guard block). The
    // residual is one slot of the rematerialised `autotrick` anchor addi, which
    // stays ahead of the mTrickSymbol store where the image has it after.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.834, 15 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap.
