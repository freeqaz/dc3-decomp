# Lane notes: `src/system/synth_xbox/Synth.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?ReverbConvertI3DL2ToNative@@YAXPBUXAUDIO2FX_REVERB_I3DL2_PARAMETERS@@PAUXAUDIO2FX_REVERB_PARAMETERS@@@Z`

`ReverbConvertI3DL2ToNative` -- moved verbatim from `src/system/synth_xbox/Synth.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 128-135):

```text
        // The remaining 5 mismatches live here: the target loads DecayHFRatio before
        // DecayTime and emits `fmuls f0, f13, f0`. Operand swap and a lifted temp are
        // both byte-inert -- MSVC canonicalises this. Backend floor.
        // Re-confirmed 2026-09-14 (lane w7-o) with a third spelling the note did
        // not cover: TWO named temps, one per operand, declared in the image's
        // load order and multiplied in the image's operand order.  Byte-inert as
        // well -- still 5 rows, 99.989.  Three independent spellings now agree,
        // so this is the backend and not the source.
```


## `?SetupHeadsetSubmixes@Synth360@@AAAXXZ`

`Synth360::SetupHeadsetSubmixes` -- moved verbatim from `src/system/synth_xbox/Synth.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 559-581):

```text
    // w7-bu: 89.8 -> 99.99.  The w7-ab RESIDUAL note that stood here filed the
    // two engine-call clusters as scheduling floors; they were the spelling.
    // The calls were raw vtable-slot function-pointer casts through an
    // `int *pEngine` local (pre-dating this TU's IXAudio2 declaration), which
    // let MSVC float `lwz r3, 0xec(r24)` to the top of each block.  As real
    // virtual calls the engine load is evaluated as the `this` argument and
    // lands where the image has it, last before `lwz r11, 0x0(r3)`: 98.9.
    // Then, each measured alone against that (the old note had only stacked
    // them on the 84.3 declaration-order variant): `buffer.Flags = 0` above
    // the memset (image `stw r28, 0xa0(r31)` before `bl memset`) 99.9;
    // wBitsPerSample before nBlockAlign, and the XAUDIO2_BUFFER assignment
    // order below (the ready-zero stores emit as a rotation of source order
    // starting at the 5th of 8, the li-fed ones keep source order) 99.99.
    //
    // RESIDUAL 2 rows: `stw r26, 0x84(r31)` (nSamplesPerSec) and
    // `stw r25, 0x6c(r31)` (voiceSends.pSends) emit in the other order.
    // Inert: voiceSends before/after format, pSends before SendCount,
    // nAvgBytesPerSec spelled as nSamplesPerSec * nBlockAlign (folds).
    // Worse: nSamplesPerSec first in the format block (99.4, it then emits
    // first overall); the two voiceSends stores interleaved into the format
    // block (97.9).
    // Ensure mHeadsetSubmixes has exactly 4 entries. resize() already contains the
    // shrink-with-erase branch; spelling the outer test by hand emits it twice.
```


## `?AddRef@CXAPOBase@@UAAKXZ`

`CXAPOBase::AddRef` -- moved verbatim from `src/system/synth_xbox/Synth.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 748-755):

```text
// w8-d: AddRef was DECLARED in xdk/xaudio2/xapobase.h:46,89 and defined nowhere
// in the tree -- only the `WCA@` adjustor thunk was ever emitted.  The body is
// at 0x82E2D4F8, sandwiched between ?Release@CXAPOBase@@ (0x82E2D520) and
// ?QueryInterface@CXAPOParametersBase@@ (0x82E2D598) in Synth.obj's carve, and
// is the same interrupt-masked reservation idiom as Release with the counter
// address formed up front: `addi r11, r3, 0x1c` (m_lReferenceCount) / mfmsr r9 /
// mtmsrd r13,1 / lwarx / addi r10,r10,1 / stwcx. / mtmsrd r9,1 / bne, returning
// the POST-increment value in r10.
```


## `?CalcInputFrames@CXAPOBase@@UAAII@Z`

`CXAPOBase::CalcInputFrames` -- moved verbatim from `src/system/synth_xbox/Synth.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 769-773):

```text
// w8-d: the CXAPOBase frame-count pair, both `return n;`.  The target body at
// 0x82E2D590 is literally `mr r3, r4` / `blr`, and ham_xbox_r.map prints BOTH
// ?CalcInputFrames@CXAPOBase@@UAAII@Z and ?CalcOutputFrames@CXAPOBase@@UAAII@Z
// at that one address from synth_xbox:Synth.obj -- so the two fold into each
// other and symbols.txt can bind only one of the pair.
```
