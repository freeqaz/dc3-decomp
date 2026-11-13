# Lane notes: `src/system/gesture/LiveCameraInput.cpp`

Sidecar for lane history that used to sit as comment walls in the source.
Each `## \`<mangled symbol>\`` section holds that function's notes VERBATIM,
one fenced block per original comment block.  `scripts/find_refutations.py`
reads this file (exact mangled-symbol match, then short-name fallback) and
prints these blocks first.  The source keeps only a short mechanism comment.

Add new lane notes to the function's section here, not to the source.

## `?StoreTextureClip@CamTexClip@@QAAXPAVRndTex@@MMMM@Z`

`CamTexClip::StoreTextureClip` -- moved verbatim from `src/system/gesture/LiveCameraInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 135-142):

```text
    // RESIDUAL (w8-r, 99.919, 296 B, 10 of 74 rows) -- pure FPR colouring in
    // the two trailing row scales, and the arithmetic is identical on both
    // sides.  `m.y *= scaleY` loads 0x14 into f13 on both sides and then the
    // image takes 0x10 into f12 / 0x18 into f0 where we take 0x18 into f12 /
    // 0x10 into f0; `m.z *= scaleZ` (folded to a load/store triple because
    // scaleZ is 1.0f) is the same story on 0x20/0x24/0x28.  The STORE order is
    // ascending on both sides -- only which FPR carries which component
    // differs, so there is no component swap here.
```


## `?UpdateFromColorBuffer@TextureStore@LiveCameraInput@@QAAXPAV2@@Z`

`LiveCameraInput::TextureStore::UpdateFromColorBuffer` -- moved verbatim from `src/system/gesture/LiveCameraInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 271-279):

```text
        // w20-c: both strides named as ints BEFORE the loop, the spelling
        // DrawUtl's UpdateBufferTex uses for the same blit.  Same values as the
        // old in-loop `(int)((pitch >> 1) - 640)` form (the unsigned
        // subtraction is converted to int before it steps the pointer, so the
        // LP64 native build still steps backwards when the pitch is narrow),
        // but it flips the image's r22/r23 (src/dest stride) and r10/r11
        // colouring: fuzzy 99.054 -> all 74 rows equal.  The one remaining
        // relocation row is D3DCubeTexture_UnlockRect vs D3DTexture_UnlockRect,
        // a proven ICF fold at 0x82B9BEC0 (icf_aliases.map).
```


## `?UpdateFromDepthBufferClip@TextureStore@LiveCameraInput@@QAAXPAV2@MM@Z`

`LiveCameraInput::TextureStore::UpdateFromDepthBufferClip` -- moved verbatim from `src/system/gesture/LiveCameraInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 481-500):

```text
// RESIDUAL (w8-r, 98.182, 440 B, 22 of 111 rows) -- ONE callee-saved
// colouring decision and nothing else.  The image puts clippedX in r27
// (`subf r27, r11, r10`, the %640 remainder) and srcPitch in r28
// (`srwi r28, r8, 1`); we colour them the other way round, and every later row
// -- r8<->r9 and r10<->r11 in the inner pixel loop, the `mullw`, the `lhzx`,
// the `sthu` -- inherits the swap.  The one non-rename row is the `mr r11,
// r27` copy of clippedX, which the image emits BEFORE the `add`/`cmpw` pair
// that bounds the row and we emit four instructions later.  No address,
// constant or value differs on either side.
// w17-c: the callee-saved colouring above came from the hand-rotated
// `if (n) do {} while` loops and the pre-decremented destRow.  Written as two
// plain for loops (MSVC rotates them and forms the same sthu itself) the
// r27/r28 swap and the misplaced `mr r11, r27` are gone; what is left on a
// non-PCH probe is one volatile r8/r9 swap in the inner pixel loop.
// depthPixel is an unsigned int holding the zero-extended halfword, not an
// unsigned short (same values: it is only masked and shifted).  As a short the
// pixel was a short-lived temp (pri 8, tie 77) that lost to destRow (pri 8,
// tie 97), so destRow took r9 and the pixel r8, the reverse of the image.  As
// an int it is a named candidate at pri 13, coloured first, and takes r9.
// Storing in each arm instead of via `color`: 94.3.
```


## `??0LiveCameraInput@@IAA@XZ`

`LiveCameraInput::LiveCameraInput` -- moved verbatim from `src/system/gesture/LiveCameraInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 541-554):

```text
// w21-bd (98.908 -> 100, all 380 rows equal, name_check too).  Three levers:
//  * CamTexClip has an inline default ctor (mTex = nullptr), so mTexClips is
//    built by MSVC's member-init phase (the image's unfolded
//    `addi r10, r30, 0x1224` / `subi r10, r10, 0x4` CTR loop) and mSpeechMgr's
//    single store follows it from the init list; the body loop and the body
//    `mSpeechMgr = nullptr` (the duplicate store w7-aa measured) are gone.
//  * mNumSnapshots = 0 written LAST before clear(): MSVC emits the last of the
//    four member stores first, giving the image's 1200,14a8,14ac,14b0.
//  * mFrames[] cleared by an inner 2-trip loop: MSVC hands CTR to the inner
//    loop and unrolls it, leaving the outer stream loop on the image's GPR
//    down-counter (li 4 / subic. / bne, +0xc-biased pointer).  Not an
//    artifact after all (w20-p).  Same pattern: FreestyleMoveRecorder::
//    ClearFrameScores.
// Behaviour identical to before on every path.
```

Inside the body (was lines 565-583):

```text
    // BEHAVIOURAL FIX (w8-r): SpeechMgr is constructed from the "speech"
    // SUB-ARRAY, not from the whole "kinect" array.  The image keeps
    // FindArray("speech")'s result in r25 (`mr r25, r3` at 0x82432E78) and
    // that is the register it passes to SpeechMgr::SpeechMgr -- `mr r4, r25`
    // at 0x82432FA8, the instruction before the
    // `bl ??0SpeechMgr@@QAA@PBVDataArray@@@Z` at 0x82432FAC.  kinectArr lives
    // in r23 (`mr. r23, r3` at 0x82432E44) and is never passed there.  LiveCameraInput::Init corroborates it: the image
    // feeds SpeechMgr::InitGrammars the same speech array.
    //
    // `speechArr` is also declared OUTSIDE the `if` and deliberately left
    // uninitialised, because the image reads it back on the !kinectArr path:
    // 0x82432EBC is `lwz r25, 0x54(r31)`, and 0x54(r31) is the slot the
    // "speech" Symbol temp occupies (`addi r3, r31, 0x54` at 0x82432E54),
    // i.e. the stack packer shares it with speechArr's home.  The read is harmless in the
    // shipped game -- b17 can only be true when kinectArr is non-null -- but
    // it is what buys the `b` over the reload at target indices 110/111, and
    // w7-bp's reading of those two rows as "the image spills and reloads a
    // value across that join" is hereby RETRACTED: it is an uninitialised
    // local, the same shape as LiveCameraInput::Init.
```


## `?StreamBufferData@LiveCameraInput@@QBAPAXW4BufferType@1@@Z`

`LiveCameraInput::StreamBufferData` -- moved verbatim from `src/system/gesture/LiveCameraInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Above the definition (was lines 908-919):

```text
// BEHAVIOURAL FIX (w8-r): the previous body indexed mStreams[type] and read
// mFrames[i3] with a *constant* i3.  The image (0x8242F7F0..0x8242F824) does
// neither.  It remaps the buffer type to a STREAM index -- kBufferPlayer(2)
// reads the depth stream, kBufferPlayerColor(3) reads the colour stream, i.e.
// `idx = type==2 ? 1 : (type==3 ? 0 : type)`, the `type != 3` arm lowered as
// the subfic/subfe bool mask at 0x8242F804-0x8242F80C and `and r11, r11, r31`
// -- and then reads mFrames[mReadIdx], the same double-buffer read index
// PollNewStream maintains.  The old spelling read mStreams[2]/mStreams[3],
// which PollNewStream never fills (it asserts type is colour or depth), so
// StreamBufferData(kBufferPlayer) always returned null; and for colour/depth
// it pinned mFrames[0] instead of following mReadIdx, so it returned the
// wrong half of the double buffer on every other frame.
```


## `?CameraDump@@YAXPBD@Z`

`CameraDump` -- moved verbatim from `src/system/gesture/LiveCameraInput.cpp` on 2026-11-13 by scripts/notes/migrate_notes.py (line numbers as of 89d252553)

Inside the body (was lines 1379-1389):

```text
    // `= nullptr`, not a bare declaration: the image writes 0 into the slot
    // before the TexelsLock call (`li r11, 0x0` / `stw r11, 0x50(r31)` at
    // 0x82434964..0x8243496C, reloaded as memcpy's r4 at 0x8243497C).  That
    // took 94.737 from 6 charged rows to 5.  RESIDUAL (w8-r, 94.737): the
    // image hoists tex's vtable load ABOVE the `mr r29, r3` / home store
    // (`lwz r10, 0x0(r27)` is the instruction after `li r11, 0x0`) where we
    // emit it two instructions after, and it reloads `lwz r4, 0x50(r31)` after
    // `mr r3, r29` where we load it before -- a two-instruction hoist, no
    // value or address differs.
    // w24-c2: closed (94.737 -> 100) by scoping texels to a BLOCK around the
    // lock/copy -- declaration scope, not order, moved both hoists.
```
