# Lane notes: `src/lazer/game/SongSequence.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DoNext@SongSequence@@QAA_N_N0@Z`

`SongSequence::DoNext` -- moved verbatim from `src/lazer/game/SongSequence.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 188-198):

```text
            // The residual r28/r29 swap on p0/p1 (diff rows 195/201) is NOT
            // operand order -- REFUTED 2026-09-14 (w7-q).  Rows 206-230 are
            // byte-identical on both sides: whichever provider the allocator
            // parked in r29 gets its Property() call first.  The image holds
            // p0 in r29, we hold p0 in r28.  Writing this as
            //     p1->Property("score")->Int() + p0->Property("score")->Int()
            // (MSVC evaluates `+` right-to-left, so that should evaluate p0
            // first) produced a BYTE-IDENTICAL object -- MSVC normalises the
            // two call operands and the source order does not reach the
            // allocator.  Verified with a sabotage control on the same object,
            // so the null result is not a stale build.
```

Inside the body (was lines 217-282):

```text
    // The size() load-order residual (diff rows 271-273) is not reachable from
    // here.  The image loads _M_start (0x0) then _M_finish (0x4) and lets the
    // begin register die in the `subf`, then RELOADS _M_start for the
    // mEntries[mCurrentIndex] below (`lwz r10, 0x0(r30)` @8288D9D4).  We load
    // _M_finish first, so begin survives in r9 and gets reused -- one
    // instruction shorter.  (Superseded by w21-m below: a const vector& bound
    // just before the compare DOES produce the image's longer form.)
    // Splitting this into `++mCurrentIndex;` + a separate `if` is BYTE-
    // IDENTICAL (measured 2026-09-14, w7-q, with a sabotage control).
    // w16-e: `(int)mEntries.size() <= ++mCurrentIndex` 99.133 (vs 99.14),
    // `mEntries.end() - mEntries.begin()` 98.43, reusing numEntries 98.80
    // (the image does reload begin/end here).
    // w21-e (99.14, same 13 rows): `int newIndex = ++mCurrentIndex;` then
    // `if (newIndex >= (int)mEntries.size() || b2)` is byte-identical too.  The
    // tell is our `clrrwi r10, r9, 0` (a 32-bit re-truncation of the kept begin
    // pointer) where the image does a fresh `lwz r10, 0x0(r30)` -- our begin
    // survives as a 64-bit value from the size() subtraction.  Behaviour checked
    // against the image on all 13 rows: lis pair 138/139 and p0/p1 195/201 are
    // register-only (p0+p1 sum is commutative), 271-288 is the same size/index.
    // w21-m: a const reference bound here (the w21-b/ResetDetectFrames lever)
    // gives the image's compare shape -- size() dies in the subf and
    // _M_start is RELOADED for mEntries[mCurrentIndex] (`lwz r10, 0x0(r30)`
    // @8288D9D4) -- 13 rows -> 10, 99.14 -> 99.84.  Left: start/finish load
    // order here and at the numEntries load above (both flip as a pair), one
    // dead `stw r30, 0x50(r31)` home of the reference, the 138/139 lis
    // pair and the p0/p1 r28/r29 pair (195/201).  Measured worse: binding at the top for numEntries too (p0/p1 fixed
    // but 11 rows / 99.7 with a second home), binding at the top and using it
    // for nextEntry or everywhere (r17/r18 + r29/r30 swaps, 99.7), binding at
    // the top used only here (98.6).
    // w21-az (99.84, same 10 rows): a `const std::vector<Entry> *` bound here
    // is byte-identical to the reference (still homed at 0x50(r31)); an inline
    // header helper `int NumEntries() const { return mEntries.size(); }` used
    // in the compare is the pre-w21-m shape (99.2: numEntries site 102-104
    // matches, but begin is kept and re-truncated with clrrwi); and
    // `mEntries.end() - mEntries.begin()` for numEntries at the top is 97.8
    // (r17/r18 swap).  The image's OnSongLoaded DOES home an &mEntries local
    // (`addi r18, r3, 0x8` @8288BE14, `stw r18, 0x50(r31)` @8288BE24) -- DoNext's image
    // has no such home, so whatever forces the begin reload here is not a
    // named local.
    // w26-g canonical decomposition: report.json charges 114 of 71200 units
    // (99.83989) = ONE insert (100) + FOUR immediate diffs (1 each) + the two
    // reloc-NAME rows at 138/139 (5 each): the image emits
    // `lis r26, TheGameData` before `lis r11, TheHamProvider`, we emit them in
    // the other order, and under this project's report ruler
    // (functionRelocDiffs=name_check) a vetted reloc-name disagreement is
    // charged rather than folded. The
    // insert is the dead `stw r30, 0x50(r31)` -- 0x50(r31) is a double scratch
    // here (stfd/lfd at 180/249) and this store at 269 is never read, so it is
    // not a behaviour difference. The four immediate units are the two swapped
    // load pairs (102/103 and 272/273: 0x8 then 0xc vs 0xc then 0x8, with the
    // `subf` operands swapped to match, i.e. the same end-begin value). The
    // p0/p1 rows (195/201) are register-only and folded into arg_diff_score,
    // i.e. forgiven. Repro must pass --map-file build/373307D9/icf_aliases.map
    // and read `canonical_match_percent`, not `normalized_match_percent`
    // (which is the fuzzy score relabelled).
    // w26-n3 (6 more variants, canonical, per-object path reproduced exactly
    // here -- baseline 99.83989): `*&mEntries`, a C-cast-initialised reference,
    // a non-const `std::vector<Entry> &` via const_cast, and an
    // `entries.end() - entries.begin()` size are ALL 99.83989, i.e. c1 folds
    // every one of them back to the same IL.  A named `int next = ++mCurrentIndex`
    // is also 99.83989.  Confining the reference to its own scope
    // (`bool atEnd; { const std::vector<Entry> &entries = mEntries; atEnd = ...; }`)
    // collapses to 74.98876 -- it moves the block boundary the MILO_LOG static
    // guard hangs off, so it is not a neutral way to shorten the live range.
    // The remaining handle is still the dead home of the reference at 0x50(r31);
    // nothing that keeps the image's `lwz r10, 0x0(r30)` begin-reload removes it.
```


## `?OnSongLoaded@SongSequence@@QAAXXZ`

`SongSequence::OnSongLoaded` -- moved verbatim from `src/lazer/game/SongSequence.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 361-380):

```text
// Open (c2 colour order, w25-hb): normalized 100, but two nonvolatile
// triples are rotated against the image.
//   - `this` (cand id5, pri -105) gets r14, below hasIntro (id8, pri -102)
//     and the static-guard `lis` half (id197, tie 0, pri -103). The image
//     needs `this` above hasIntro, i.e. +4: its last use (mVenueEntered)
//     keeps it live, unreferenced, through the whole visualizer/hollaback
//     tail.
//   - the TheHamDirector `lis` half (id159, tie 0, pri -58) pops before
//     set_type's address (id144, pri -60, tie 58), so it needs pri <= -60.
// ~25 code-neutral spellings (loop/branch forms, int vs bool locals, tail and
// goto restructures, store placement) moved none of these keys without also
// changing the emitted code.
// w26-m (same 71 rows): the §8.7 commutative-key lever does not apply -- every
// charged row is an address/register rotation (lis, lwz, stw, mr, addi, stb,
// cmplwi), with no commutative op involved.  The §8.6 demotion lever (C=43 code
// mints, F=87 demotions) was tested with named Symbol copies in one block: a
// `Symbol mode = curEntry.mGameplayMode` copy is inert, and copies at the
// "intro"/"playing" SetProperty sites are worse.  The remaining handle is the
// priority ledger for `this` and the TheHamDirector lis half, i.e. how many
// blocks each spans unreferenced.
```
