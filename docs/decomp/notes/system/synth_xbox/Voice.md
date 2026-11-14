# Lane notes: `src/system/synth_xbox/Voice.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?createOrReuse@Voice@@AAAJPAUPoolVoice@@AAIAAUtWAVEFORMATEX@@PAUXAUDIO2_VOICE_SENDS@@@Z`

`Voice::createOrReuse` -- moved verbatim from `src/system/synth_xbox/Voice.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 137-155):

```text
    // w17-e: 96.19 -> 100 canonical (modulo register permutation: this/wfx/sends
    // rotate r23/r21/r22 vs our r21/r22/r23, 11 rows).  The bare
    // MemPushTemp/MemPopTemp pair is a MemDoTempAllocations scope and the
    // engine lock is a CritSecTracker (the image's `stw r30, 0x50(r31)` right
    // after `addic. r30, r11, 0xb0` is CritSecTracker::mCritSec, and the pdata
    // carries an unwind region).  The failure path RETURNS from inside the
    // tracker's scope: that is what makes MSVC duplicate the Exit into both
    // arms and share the single `bl MemPopTemp` tail (82E36CDC), which the
    // w7-y `if (hr) result = hr; else {...}` spelling could not reach.
    // w26-f6, c2 candidate tap on this function -- three contested candidates
    // pop in this order and take these registers: id 1 pri -20 tie 6 -> r23,
    // id 5 pri -21 tie 7 -> r22, id 3 pri -22 tie 9 -> r21.  Resolving those
    // against the listing's roles (r23 = this, r22 = sends, r21 = wfx) gives the
    // pop order sends, wfx, this; the image's order is this, sends, wfx, i.e.
    // `this` needs to clear -20 (a tie at -20 also works, tie 9 > tie 6).
    // Tried and inert or worse: a named mChannels local, a
    // named SendCount local, the tail as if/else, a named bool holding the tail
    // expression, and a named local for the mPoolVoice.egParams assert.  Open
    // question: which block adds a reference to `this` without emitting code.
```

p2-reloc (2026-11-14), canonical 100.0 / fuzzy 99.686, 11 non-equal row(s): NOT A BUG.
Every row is register-only, and `scripts/analysis/regperm_equivalence.py` finds one
consistent target<->base value pairing at every use (parameters, call arguments, stores,
compares): pure register permutation, no operand swap. Name rows forgiven,
value-checked: the image passes CreateSourceVoice's 4.0 max frequency ratio from a .data
word (lbl_82F472F8, after gVoiceThread) where we use the `__real@40800000` literal -- a
mutable static in the original, same value; gVoicesActive = lbl_8316C730.

## `?UpdateMix@Voice@@AAAXXZ`

`Voice::UpdateMix` -- moved verbatim from `src/system/synth_xbox/Voice.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 195-256):

```text
// w7-bu (2026-09-15): 92.4 -> 95.0 canonical, three levers on the MONO arm:
// (1) the two 0x3d9 asserts are ONE assert on a shared `HRESULT hr = 0`
// (the image compares it in cr6, 0x82E378F4, where a call result tested in
// place is cr0 -- compare the 0x37a site at 0x82E3758C -- and the
// `!unk54` path jumps straight past the assert, 0x82E378A4, because
// SUCCEEDED(0) folds).  Keeping the per-site `voice` locals matters:
// `hr = GetVoice()->SetOutputMatrix(...)` swaps this/r24 with the assert
// string's r25 (92.9), and `hr` assigned in every arm is 91.9.
// (2) loChannel/loPan/hiChannel/hiPan are declared interleaved: the
// image materialises the uninitialised set as lwz/lfs/lwz/lfs from 0x50(r1)
// (0x82E376A4 on).  (3) each pan arm assigns loChannel, loPan, hiChannel,
// hiPan in that order (0x82E37700-0x82E37780) -- with (2) this also removed
// the r30/r29/r28 rotation w7-an recorded.
// RESIDUAL (w7-bu, 95.0, 37 rows): all in the STEREO arm plus the 6 fmuls
// rows below.  We partially-redundancy-eliminate the `*TheXboxSynth` load out
// of the two `mFxSend ? ... : TheXboxSynth->OutputVoice()` ternaries into r8
// (hoisted between the `cmplwi` and its `beq`) and reload it after each call;
// the image reloads the global inside each arm (0x82E37474/0x82E37494).  The
// textually identical MONO copy of the same ternary pair (0x82E375C0 on)
// matches exactly, so this is contextual, not a spelling.  Tied to it: the
// image lays the cos/sin arm out as the fall-through of the `== 6 || == 2`
// test (`bne cr6` at 0x82E374F8) and puts the fill-1.0 loop out of line; we
// do the reverse.  Measured inert for both: stereo/mono as if/else instead of
// an early return (95.0, and it shrinks the frame by 0x20), and the 6/2 test
// as a `switch` (95.0, and the compares come out sorted 2-then-6).
// NEGATIVE RESULT (w7-an, 2026-09-14): the 6 `fmuls` commutative operand rows
// are NOT reachable from the source -- writing `(float)cos(angle) * mVolume`
// instead of `mVolume * (float)cos(angle)` at all six sites is byte-for-byte
// inert (MSVC normalises the order).  So is inverting the
// `destChannels == 6 || destChannels == 2` if/else into
// `!= 6 && != 2` with the arms swapped: the image falls through into the
// cos/sin arm (`bne cr6` at 0x82E374F8), we fall through into the fill-1.0
// arm, and neither spelling of the condition moves that.
// w18-e (94.97): a static inline OutputFor(mFxSend) helper for the six
// ternaries is worse (93.3 -- its null tests come out as cr0 compares);
// declaring the stereo destChannels above the assert is worse (94.8).
//
// w26-j (2026-11-01), measured against the target object with tgtcmp
// (baseline EQUAL 384/438, ours 438 == target 438 instructions):
//   - BEHAVIOUR: no bug.  The two insert/delete clusters are a basic-block
//     layout flip plus a CSE, not a missing or wrong statement.  unicorn has
//     no verdict for this symbol, so the row-for-row listing is the evidence.
//   - the 22-row block-layout cluster is rows 81-90/105-111: the image emits
//     `beq cr6, THEN; cmpwi 2; bne cr6, ELSE` and falls through into cos/sin,
//     we emit `beq THEN; cmpwi 2; beq THEN`, fall through into the fill-1.0
//     loop and jump over cos/sin.  Re-measured this lane:
//     `if (destChannels != 6 && destChannels != 2) {fill} else {cos/sin}`
//     is byte-inert (384/438 either way), confirming w7-an.
//   - NEW negative: naming mVolume as `float vol = mVolume;` inside the arm
//     collapses the function to 63/438.  Do not name it.
//   - the other cluster (rows 40-65) is c1 keeping `TheXboxSynth@l` in r8
//     across the first ternary's null test and reusing it for the second
//     ternary (insert at 45, reuse at 49/58, reload at 64 and 102); the image
//     reloads inside each ternary arm.  The textually identical MONO copy
//     matches row for row, so this is context, not a spelling.
//   - the 6 `fmuls` operand-order rows are c2's post-lowering commutative key
//     (C2RS-BRIDGE 8.7): our cqlo3 emits `V25{0x10320} V23{0x102e0}` and
//     `V617{0x19a40} V23{0x102e0}`, i.e. cos/sin (higher sid) before mVolume;
//     the image needs mVolume's leaf key to be the higher of the two.  Source
//     operand order is canonicalised away, so the lever is a leaf kind or sid
//     change on mVolume -- and the `vol` negative above is that lever firing
//     the wrong way.
```


## `?SetSpeed@Voice@@QAAXM@Z`

`Voice::SetSpeed` -- moved verbatim from `src/system/synth_xbox/Voice.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 657-662):

```text
// w17-e: 97.059 -> 100.  The lower clamp is std::max(0.01f, speed): both
// operands bound by const reference, which is what homes `speed` at 0x10bc and
// the literal at 0x50 and selects between their addresses (image 82E38A84
// `fcmpu f1, f0` / `bgt` keeps &speed only when speed > 0.01f, so a NaN speed
// takes the 0.01f arm -- the old hand-written `speed <= min ? &min : &speed`
// kept the NaN).  The w8-r "literal through r11" residual was this.
```


## `?Init@Voice@@QAAX_N@Z`

`Voice::Init` -- moved verbatim from `src/system/synth_xbox/Voice.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 847-854):

```text
    // RESIDUAL (w8-r, 99.9925, 1072 B, 2 of 268 rows).  The image loads the
    // vtable slot BEFORE the argument (`lwz r11, 0x18(r11)` then
    // `lwz r5, 0x60(r30)`); we emit those two independent loads the other way
    // round.  Both sides call slot 0x18 with the same four arguments, reload
    // mPoolVoice.egParams from 0x60(r30) for each of the four stores above and
    // once more here, and hoist the 0 / 0x10 constants to the same places --
    // this is instruction scheduling of two loads with no dependency between
    // them, and no spelling of the call expression has been found that moves it.
```


## `?InitVoiceParameters@Voice@@QAAXAAUXMA2WAVEFORMATEX@@UXAUDIO2_BUFFER@@@Z`

`Voice::InitVoiceParameters` -- moved verbatim from `src/system/synth_xbox/Voice.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 877-885):

```text
        // RESIDUAL (w8-r, 99.960, 5 rows).  The image emits PlayBegin (0x20)
        // BEFORE BytesPerBlock (0x1c) -- `stw r8, 0x20(r31)` then
        // `stw r9, 0x1c(r31)` -- and loads buf.PlayBegin (0x9c(r1)) before
        // buf.LoopCount (0xac(r1)).  We emit the constant store first whatever
        // the source order: MEASURED, swapping these two lines is INERT
        // (99.960 either way) and sinking BytesPerBlock past EncoderVersion
        // costs 99.960 -> 97.950.  The values stored are identical on both
        // sides; only the order of the constant store against the by-value
        // parameter's loads differs.
```


## `?StartVoiceThreadEntry@@YAKPAX@Z`

`StartVoiceThreadEntry` -- moved verbatim from `src/system/synth_xbox/Voice.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 906-968):

```text
// COMDAT-SELECTION LEVER: MEASURED for this unit (w8-r).  The mechanism is NOT
// "add `inline` to make the callee a COMDAT" -- retracted; MSVC/Xenon puts every
// compiled function in its own COMDAT, and ham_xbox_r.map's flag column reports
// the COMDAT *selection type* from the section symbol's aux record at offset 14
// (`f i` = IMAGE_COMDAT_SELECT_ANY, bare `f` = NODUPLICATES).  Measured over
// build/373307D9/src/system/synth_xbox/Voice.obj with
// scripts/analysis/comdat_selection_audit.py: 8 selection mismatches, and ALL
// EIGHT are the reverse, non-actionable direction (ours ANY / image
// NODUPLICATES) on dynamic-initialiser thunks -- ??__EgInProgressSyncVoices,
// ??__EgInProgressVoices, ??__EgLockPendingLists, ??__EgPendingSyncVoices,
// ??__EgPendingVoices, ??__EgVoiceGC, ??__Es_voiceGC, ??__Es_voiceGCInProgress.
// ZERO in the actionable ours-NODUP / image-ANY direction, and not one of the
// eight is a sub-100 row.  Nothing in this unit to match.
//
// Do NOT read this unit's save sets as a lead for it either: a sibling lane's
// BuildBeam kept `__savegprlr_16` byte-identically at 85.3415 after its
// same-TU callee's class was matched, and all four selection closures verified
// anywhere so far were fidelity-only with zero score movement.  Target save
// sets as reference only: UpdateMix __savegprlr_22 + __savefpr_26;
// createOrReuse __savegprlr_21; StartVoiceThreadEntry __savegprlr_14; SetSpeed
// __savegprlr_29.  createOrReuse's only helper-shaped row is an EPILOGUE
// STRUCTURE difference, not a save-set one -- the image tail-branches
// `b __restgprlr_21` after `addi r1, r31, 0x1910` where we branch to a shared
// local epilogue block; its other 37 rows are a callee-saved rotation
// (r21->r23, r22->r21, r23->r22, r29<->r30) plus one target-only dead home
// store `stw r30, 0x50(r31)`.
//
// w7-bh: the MakeString template-instantiation row objdiff reports here is a
// benign ICF fold, not a wrong callee -- both the image's and our instantiation
// resolve to 0x824D1870 in build/373307D9/icf_aliases.map.  MakeString's array
// bounds are template parameters that never reach the code, so all such
// instantiations are byte-identical and the linker folds them.
// w7-bu (2026-09-15): 90.5 -> 95.8 canonical.  (1) BEHAVIOURAL: the egParams
// free in the drain loop is `delete pv.egParams` through the header's
// POOL_OVERLOAD operator delete -- the image's PoolFree at 0x82E3964C takes
// r15 = "e:\lazer_build_gmc1\system\src\synth360\EnvelopeGenerator.h"
// (hoisted at 0x82E39308), not this file's "Voice.cpp"; a hand-written
// `PoolFree(0x10, ..., __FILE__, 0x1e, ...)` passed the wrong file string,
// and a `delete` on the old `void *` field went to the global ??3@YAXPAX@Z
// (86.2, WRONG_CALLEE), so PoolVoice::egParams is now typed.  (2) The GC loop
// refreshes `front = s_voiceGC.begin()` BEFORE the `gcCount >= 4` break: the
// image copies the four iterator words (0x82E39560-0x82E39590) and only then
// takes `bge cr6` out (0x82E39594), followed by the duplicated end() test;
// `for (...; gcCount < 4 && front != end(); front = begin())` shares the
// header test instead (93.0).
// RESIDUAL (w7-bu, 95.8, 43 rows): rows 20-65 are the prologue's lis/addi
// hoist order and scratch registers (same hoist SET now, including the
// header-path string in r15 and "EnvelopeGeneratorParams" in r14); the
// image tests TheXboxSynth in cr0 at 0x82E39470 where every other test of it
// in this TU is cr6 (nested `if`s: inert).
// w17-e: 95.8 -> 99.59.  The "gVoiceCounters[2]" array was two separate
// file-static ints, gVoicesActive (0x8316C730) and gVoicesPendingGC
// (0x8316C734): the image reaches the GC count here as its own symbol
// (`lis r20, lbl_8316C734@ha` hoisted, `lwz r11, lbl_8316C734@l(r20)` at
// 0x82E39558), where an array made us hoist the full base (lis+addi) and
// shifted the whole prologue hoist set by one register; dispose() reaches the
// pair as anchor+displacement off gVoicesActive exactly as before.  Both are
// written `= 0`: uninitialised file statics are laid out ahead of every
// global, initialised ones in declaration order -- which is the image's
// .bss order (the four bools, the two counts, gCommitTag, gWasCommitTag,
// rolling).  RESIDUAL (99.59, 10 rows): rows 32-56 swap two hoisted string
// /deque bases between r4/r5 and r29/r30 (lis scratch only), and the cr0
// TheXboxSynth test (rows 135-136).
```
