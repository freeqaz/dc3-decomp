# DC3 3D Pose Corpus

Extracts the game's own **reference choreography** — real 3D skeletons in exactly
DC3's 20-joint layout — out of the shipped milo assets, so depth-estimation work
can be *measured* instead of eyeballed.

## Why this exists

The native port feeds the scorer 2D webcam pose with a constant `z = 3.0 m` for
every joint. DC3's scoring is genuinely 3D: `DetectFrame::LimbPSNR`
(`src/system/hamobj/DetectFrame.cpp:100`) dots a per-move `Vector3` weight against
a `Vector3` error, and since the numerator is `Dot(w,e)²` rather than a
per-component sum, a wrong z can partially *cancel* an x error. So a bad z does
not merely add a constant penalty — it corrupts x/y grading.

This corpus is the ground truth to score a z-estimator against.

## What's in it

`DancerSequence` reference poses — the very skeletons the scorer compares the
player to (`MoveAsyncDetector.cpp:135-176`, `MoveDir::ResetDetectFrames`
`MoveDir.cpp:1218-1260`).

| | |
|---|---|
| Total | 391,523 frames / 5,634 sequences across 57 songs |
| Deduplicated + label-aligned | 30,318 frames / 1,901 named sequences, 1,332 move names |
| Space | Kinect camera space, **metres**, origin at the sensor, +Y up, +Z away |
| Joints | DC3's 20 (`src/system/gesture/BaseSkeleton.h:28-50`) |
| Mean within-pose z spread | **0.483 m** (max 1.20 m), per-pose z std 0.129 m |

That last row is the point: constant `z = 3.0` is roughly the right *mean* and
captures none of the variation, so there is a large measurable error to beat.

**Quality check that matters:** across all 93 sequences in `12step`, **15 of the
19 bones are exactly length-constant to float precision** (`min == max`). Only
the six derived/compliant bones vary (ShoulderCenter→Shoulder, Wrist→Hand,
HipCenter→Hip). Segment lengths are realistic — upper arm 0.2625 m, forearm
0.2280 m, shin 0.4466 m. This is genuine rigid-limb 3D, not float noise.

## Regenerating

The artifact lives at `build/pose_corpus/poses.npz` (gitignored, ~8.8 MB). To
rebuild from `orig-assets/`:

```sh
python3 tools/pose_corpus/extract_seqs.py      # -> build/pose_corpus/poses.npz + manifest.tsv
python3 tools/pose_corpus/scan_fast.py <milo>  # numpy frame scanner, ~9 s per 12 MB milo
python3 tools/pose_corpus/dump_movedir.py <milo>  # entry-table dump (type, name) pairs
```

Output arrays: `pos`/`disp` `float32 (30318, 20, 3)`, `ms int32`, `move_idx`,
`move_frame_idx`, `seq_id`. `manifest.tsv` maps `seq_id → song, seq_name, n_frames`.

### Format notes

A `DancerFrame` is exactly **488 bytes big-endian**: `s16 moveIdx`,
`s16 moveFrameIdx`, 20 × (`Vector3` pos, `Vector3` disp), `s32 elapsedMs`
(`DancerSequence.cpp:149-163`, rev ≥ 7). The scanner masks on the high bytes of
the two `s16`s and the trailing `s32`, chains at stride 488, and asserts the
preceding `u32` equals the run length — 5,634/5,634 sequences passed.

The milo `ObjectDir` header parses as `u32 rev(=32)`, length-prefixed BE type
string, length-prefixed name, `u32` string-table count, `u32` string-table size,
**one unknown byte**, `u32 entryCount`, then `entryCount × (type, name)`. That
stray byte is the only field not obvious from `ObjectDir::PreLoad`
(`src/system/obj/Dir.cpp:1170`).

## Camera intrinsics (exact)

`NuiTransformSkeletonToDepthImage` is declared (`src/xdk/nui/nuiskeleton.h:52-55`)
but never defined in-tree — it links from the XDK. Recovered from the target
disassembly, `build/373307D9/asm/system/gesture/JointUtl.s:580-627` (`0x824435E0`):

```
u = 160 + 285.63 * x/z        v = 120 - 285.63 * y/z      (320x240 depth image)
hFOV = 2*atan(160/285.63) = 58.51 deg      vFOV = 45.58 deg
```

Corroborated by `JointUtl.cpp:89,103` normalising by 1/320 and 1/240, and
`LiveCameraInput.cpp:494` opening depth at `NUI_IMAGE_RESOLUTION_320x240`.

## Benchmark loop

3D truth → project with the intrinsics above → normalise → candidate estimator →
compare recovered per-joint z against truth. `native/scripts/pose_server_synthetic.py`
is the template for the replay harness.

`bench_z.py` runs three root-recovery estimators against this loop: torso
similar triangles on the full 3D length (the original bug: +1/cos(tilt) depth
bias), on the in-plane extent (unbiased to first order, −0.24 m perspective
residual at high tilt), and exact linear least-squares over all landmarks
(zero residual on perfect landmarks; the production estimator in
`pose_mediapipe.py::_absolute_root` since `d8977110`). Its Q3 section adds
world-landmark + pixel noise: the least-squares form degrades ~7–10× more
gracefully than the torso heuristic (1 px jitter: 0.010 m vs 0.102 m).

**Framing:** poses are authored body-centred with lateral travel to ±1.2 m, so
raw projection puts only 0.6% of frames fully in-frame. Recentre the hip on the
optical axis and shift so hip depth = 3.0 m → **99.7% project fully inside
320×240** (z then spans 1.90–3.94 m). This is legitimate because the scorer is
translation-invariant, working through `CameraToPlayerXfm` and normalised offsets
(`BaseSkeleton.cpp:75-97, 157+`).

## Caveats — read before trusting a result

- This is **authored/retargeted choreography, not raw sensor output**. No Kinect
  noise, no occlusion dropout, no inferred-joint flicker, and the feet are clamped
  to a perfectly flat `y = -1.0` floor. Scoring well here does *not* validate a
  method against real Kinect jitter.
- Only 6 of 19 bones vary in length, so a method can over-fit to a rigid-skeleton
  prior that real data will not honour.
- Frames are at **move-frame/beat resolution, not 30 Hz**. `mElapsedMs` values are
  multiples of 33 (99, 132, …, 1584) and hand joints move up to 0.91 m between
  consecutive frames. Good for *per-frame* z recovery, poor for anything temporal.
  For 30 Hz continuity, drive character animation through `CharCameraInput`
  instead (`src/system/hamobj/CharCameraInput.cpp:56-70`; character units are
  inches, X mirrored, Y/Z swapped, `kDrawScale = 39.370079`).

## What would be better, and isn't here

`moves.milo_xbox` contains **5,332 `SkeletonClip` entries** named after real
playtesters (`12step_benr_hard_2-1.clp`, `12step_erica_hard_1-1.clp`, …). Those
are actual Kinect sensor output of humans dancing at 30 Hz — exactly the noise
realism this corpus lacks.

**Only the names ship.** `SkeletonClip::Load` deserialises just `mFile`, a path
(`src/system/gesture/SkeletonClip.cpp:380-401`); frames come from an external
`.clp` written to `devkit:\%s.clp` (`:609`). `find . -name '*.clp*'` returns zero
files. The format is fully documented at `SkeletonClip.cpp:552-579` and
`SkeletonClip.h:17-30` if the files ever surface.
