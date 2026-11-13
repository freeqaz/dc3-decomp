# Lane notes: `src/system/hamobj/MoveAsyncDetector.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?EnqueueDetectFrames@MoveAsyncDetector@@QAAXHHMH@Z`

`MoveAsyncDetector::EnqueueDetectFrames` -- moved verbatim from `src/system/hamobj/MoveAsyncDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 185-191):

```text
        // w21-al: 90.53 -> 100.  The image reads the MEMBER straight into r3
        // after the PlayerDetectFrames copy (`mr r24, r3; lwz r3, 0x0(r25)`,
        // 0x8252D3B4..B8): one inline level fewer than Move().  Every earlier
        // lane went through the Move() accessor, which lets MSVC hoist the load
        // above the frames copy (w7-av/w14-b/w21-b: 86.0-90.53).  Same values,
        // same call order: Poll, PlayerDetectFrames, mMove->FilterVer(),
        // EnqueueDetectFrames(f3, i4, frames, ver).
```


## `?EnableDetector@MoveAsyncDetector@@QAAXPAVHamMove@@@Z`

`MoveAsyncDetector::EnableDetector` -- moved verbatim from `src/system/hamobj/MoveAsyncDetector.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 225-231):

```text
            // w17-b: 97.96 -> 100.  The activation is an INLINE MoveDetector
            // method (the mirror of the header's Reset()), not open code here.
            // Written inline in this body, MSVC scheduled the dead loop pointer
            // `addi r11, r3, 0x34` between the two -1 stores (loop form, 95.9)
            // or dropped it and swapped r10/r11 (straight-line form, 97.96, see
            // w7-av / w8-i history in git); through the inline call boundary the
            // stores land exactly where the image has them (0x8252EA90..AAC).
```

Inside the body (was lines 235-240):

```text
            // w8-i: `const char *`, NOT a `(char *)` cast.  MakeString takes its
            // varargs by reference, so the cast instantiated
            // `??$MakeString@PAD@@YAPBDPBDABQAD@Z` where the image calls
            // `??$MakeString@PBD@@YAPBDPBDABQBD@Z` -- a genuinely different
            // callee that only the name_check ruler charges (Hmx::Object::Name()
            // has returned `const char *` all along).
```
