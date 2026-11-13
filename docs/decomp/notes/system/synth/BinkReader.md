# Lane notes: `src/system/synth/BinkReader.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?Poll@BinkReader@@UAAXM@Z`

`BinkReader::Poll` -- moved verbatim from `src/system/synth/BinkReader.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 3b37567dd)

Above the definition (was lines 70-76):

```text
// w18-e (98.05): BinkOpenTrack(mBink, (int)i) is inert -- the cast was
// truncated straight back by the old `unsigned char` prototype; w21-ay fixed
// the PROTOTYPE (U32, as in bink.h) and that closed the function.
// w20-t (98.1, branch-scan row 298 adjudicated ARTIFACT): the BinkOpenTrack
// loop's back-edge lands on `mr r4, r28` in the image and past it in ours,
// because ours carries i into r4 on the back-edge (clrlwi r4) and in the
// preheader; r4 == i at every BinkOpenTrack call on both sides.
```

Inside the body (was lines 169-173):

```text
            // w21-ay (98.05 -> 100): with BinkOpenTrack's u32 prototype the
            // r10/r11 naming lined up and left only the (0x14,0x24) offset swap,
            // which this Buffers-then-Offsets order closes.  (w7-aa's note that
            // the swap "only adds an offset swap" was measured under the old
            // unsigned-char prototype.)
```
