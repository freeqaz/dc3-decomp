# Lane notes: `src/system/world/ThreeDSoundManager.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@ThreeDSoundManager@@QAAXXZ`

`ThreeDSoundManager::Poll` -- moved verbatim from `src/system/world/ThreeDSoundManager.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Inside the body (was lines 54-85):

```text
    // w21-af: an if/else that assigns in BOTH arms, not the ternary (99.41 -> 100).
    // With `Ptr() ? Ptr() : Cam()` MSVC threads the non-null arm past the
    // `if (listener)` test (our `bne` landed on the WorldXfm dirty check); the image
    // re-tests both arms at one `cmplwi cr6 / beq` join, which the if/else gives.
    // `p = Ptr(); if (!p) p = Cam();` measures 98.4 (cr6 lands on the first test).
    // Stop: report 99.90 (name_check), 100 normalized -- the three hoisted
    // MILO_NOTIFY_ONCE anchors come out in a different order: image `lis` order
    // &kMaxLoopingSounds, TheDebug, format string (kMax in r25, string in r24),
    // ours TheDebug, string, kMax (kMax r24, string r25); 7 name_check rows, 4
    // normalized, same MakeString(fmt, kMax) call. A function-local or
    // non-static kMaxLoopingSounds is inert.
    // w26-e, read through the c2rs taps (sections 8.4/8.6).  C = 7 code-band
    // mints, F = 19 frees (19 CDEMOTE, 19 `CFREE ... ben`), so C <= F: the code
    // band recycles the free list LIFO with descending ids and the tie-0 POP
    // ORDER EQUALS THE MINT ORDER.  That is why the section 8.4 free-count lever
    // does not apply here -- it needs C > F, i.e. 13 fewer demotions in this
    // body.  The two orders, read off the register the three pops received
    // (first pop takes r25, then r24, then r23):
    //   image   mint/pop order  &kMax, format string, &TheDebug
    //   ours    mint/pop order  format string, &kMax, &TheDebug
    // i.e. the image materialises the kMax address BEFORE the format literal,
    // and the source lever would be whatever makes sched1 reach that first.
    // Measured inert on the row set (tgtcmp, `bl` displacements excluded):
    // commuting the `loopCount == kMaxLoopingSounds` test and turning it into
    // `>=`; naming the value (`float maxSounds = kMaxLoopingSounds;`) explodes
    // the body (93 rows).
    // Second, independent cause: the image's constant is an ANONYMOUS 4-byte
    // .rdata dword -- lbl_820EE69C, `.4byte 0x00000064` -- while our
    // `static const int kMaxLoopingSounds` is a NAMED symbol, so two rows are
    // charged on the reloc NAME whatever the order does.  Matching those needs
    // a name in config/373307D9/symbols.txt for an object the original left
    // unnamed, which is the wrong direction.
```
