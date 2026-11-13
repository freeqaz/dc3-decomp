# Lane notes: `src/system/hamobj/HamAudio.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?PollCrossfade@HamAudio@@AAAXXZ`

`HamAudio::PollCrossfade` -- moved verbatim from `src/system/hamobj/HamAudio.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 721dc5a22)

Inside the body (was lines 455-480):

```text
    // NEGATIVE RESULT on the residual f29<->f30 permutation (26 of the 45 rows
    // left at 97.1%): the image holds currentTime in f29 and 0.5 in f30, ours the
    // other way round.  Neither declaring kEpsilon/halfFade ahead of the
    // GetInSongTime() call nor dropping the `halfFade` local for a bare 0.5f
    // literal moves it -- both produced byte-identical output (97.1 / 96.3, same
    // 45 rows).  Callee-saved FPR numbering here is regalloc, not declaration
    // order.
    // w21-j: 97.13 -> 98.1. The image does NOT CSE mStart in jumpPoint (it
    // reloads 0x5c(r31) after the fadeStart fnmsubs) nor mEnd in case 2 (reload
    // of 0x70(r31) for the start<end test), and SetLoop's end argument is read
    // off the member (0x60(r31)), not cf (0x4(r30)). Stopped at 36 rows: the
    // f29<->f30 swap above, r10/r11 in the four-word copy, and the fade tail --
    // the image keeps `fmadds f30,f0,f13,f31` with a LOADED -0.5f
    // (__real@bf000000) and an unfactored `fmadds f30,f0,f30,f30`, i.e. it
    // never treats f30 as the constant 0.5 there. REFUTED (byte-identical):
    // `-0.5f * clamped + 1.0f`, Interp(1.0f, 0.5f, c) / Interp(0.5f, 1.0f, c),
    // reusing halfFade itself as the fadePos variable.
    // w21-au (still 98.10, same 36 rows): the tail is the tell -- the image
    // treats the 0.5 in f30 as an OPAQUE value (no c*k+k -> (c+1)*k factoring,
    // no -0.5 -> fnmsubs-with-+0.5 rewrite), our MSVC constant-propagates it.
    // Inert (byte-identical): `clamped * halfFade + 0.5f`, `clamped * 0.5f +
    // halfFade`, `clamped * 0.5f + 0.5f`, `clamped * -halfFade + 1.0f`,
    // `1.0f - clamped * 0.5f`; taking halfFade's address through the
    // out-parameter Interp(a, b, t, float &) overload (as the result, or as
    // fadePos) -- MSVC still folds it after inlining.  Values identical on
    // every path (fma vs factored forms round the same: *0.5 is exact).
```

Inside the body (was lines 595-602):

```text
            // w14-b: Interp(1.0f, 0.5f/halfFade, clamped) here and
            // Interp(halfFade, 1.0f, clamped) below compile byte-identically
            // to these spellings (97.128 either way); the image's un-factored
            // `fmadds f30, f0, f30, f30` is not reached by either.
            // w27-e (c2rs 8.11): the named intermediate blocks c1's rewrite of
            // `c * -0.5f + 1.0f` into `1.0f - c * 0.5f` (which then contracts to
            // fnmsubs with the shared +0.5).  Split, c2 contracts the image's way:
            // `fmadds f30, f0, f13, f31` with a loaded -0.5f.  98.095 -> 99.048.
```
