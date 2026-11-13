# Lane notes: `src/system/rndobj/ScreenMask.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?DrawShowing@RndScreenMask@@UAAXXZ`

`RndScreenMask::DrawShowing` -- moved verbatim from `src/system/rndobj/ScreenMask.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of dc4924045)

Inside the body (was lines 72-76):

```text
    // w13-a (97.8 -> 100 modulo register permutation): inside the branch
    // the image re-reads the texture through cam->TargetTex() for each
    // dimension (82712A68 `clrrwi r10, r11, 0` is that re-read pointer); with
    // both reads off the accessor, both ints are stored before either is
    // converted (slots 0x60 and 0x50), which is the image's 0xd0 frame.
```
