# Lane notes: `src/lazer/meta_ham/SkeletonIdentifier.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawDebug@SkeletonIdentifier@@QAAXXZ`

`SkeletonIdentifier::DrawDebug` -- moved verbatim from `src/lazer/meta_ham/SkeletonIdentifier.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Above the definition (was lines 298-310):

```text
// w17-e (99.17, 76 rows, all one register shift): the image keeps `this` in
// r30 through the first (7-line) loop and then RELOADS it from its home slot
// in the second loop (`lwz r10, 0x224(r1)`, r30 reused for the "%d. %d %s"
// literal); we keep `this` in r14 for the whole function, which pushes every
// other callee-saved value up by one.  Not chased further.
// w22-a19: this body used to sit entirely inside `#ifndef HX_NATIVE`, so the
// native port silently dropped the whole `toggle_draw_debug` overlay the image
// draws (DrawRectScreen @ idx 65, seven DrawStringScreen lines, then the eight
// enrolled-player lines).  The only non-portable construct is the two
// `EnrollmentIndexString(...)` Strings handed BY VALUE to sprintf_s's `...`:
// the image passes the 8-byte String {vfptr, mStr} in one GPR (`ld r6, 0x0(r10)`
// before `bl sprintf_s<200>`), and `%s` reads the slot's low word -- mStr.  On
// native that is a non-trivial object through varargs, so pass c_str() there.
```


## `?OnMsg@SkeletonIdentifier@@AAA?AVDataNode@@ABVSkeletonIdentifiedMsg@@@Z`

`SkeletonIdentifier::OnMsg` -- moved verbatim from `src/lazer/meta_ham/SkeletonIdentifier.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 67b5316e8)

Inside the body (was lines 450-457):

```text
    // w21-ba (99.83 -> 100): an early return, not an enclosing `if`.  With
    // TWO by-value return statements MSVC evidently spends destructor-flag
    // bit 0 on the return object, so the conditionally-built GetSignedIn()
    // vector temp below lands on bit 1 -- the image's `li r28, 0x2` /
    // `rlwinm. r10, r28, 0, 30, 30`.  With one return it was bit 0.
    // Both returns are DataNode(0) (DataNode(kDataInt, 0) at the tail swaps the
    // two return-slot stores).  The braces keep the block's lexical scope: the
    // function-local statics' scope ordinals (?BN@, ?CC@) depend on it.
```

Inside the body (was lines 517-536):

```text
            // MEASURED, 2026-09-14 (lane w7-aa).  99.83 canonical; the only two
            // residual rows are idx 320 and 327, and they are ONE fact: the bit
            // index MSVC gives the conditionally-destructed GetSignedIn() vector
            // temp below.  The image emits `li r28, 0x2` / `rlwinm. r10, r28, 0,
            // 30, 30` (bit 1); we emit `li r28, 0x1` / `clrlwi. r10, r28, 31`
            // (bit 0).  Same flag, same register, same guarded deallocate at
            // 0x88/0x90 -- the image simply numbers one more destructible temp
            // before this one.  Every function-local-static scope ordinal in the
            // function already agrees with the image ($S7@?BN@,
            // identificationCompleteMsg@?BN@, identificationFailedMsg@?CC@,
            // p1/p2/identification_failed/identification@?CC@), so the lexical
            // scope structure is not the difference.  Refuted, both bit-identical:
            //   spelling the TriggerStringMsg argument as an explicit
            //     String(Localize(...)) temp rather than an implicit conversion;
            //   turning this `else if` into `else { if (...) }` to add one
            //     lexical scope around the temp.
            // w21-ba: closed by the early return at the top of this function
            //   (the earlier temp is the return object's flag).  Refuted on
            //   the way: `mIdentityStatus = cond ? kIdentityStatus_WaitingForSignIn
            //   : kIdentityStatus_None` (97.9).
```
