# Pose Investigation Session — 2026-03-04

## Problem
Dance clip poses rendered by `CharClip::PoseMeshes` look completely wrong ("absolutely fucked"). Only T-pose is correct. Manual bone manipulation (test-bone rotations) works fine.

## Root Cause Found: `Vector3` has a hidden `u32 PAD` member

**File:** `src/system/math/Vec.h:149-150`
```cpp
private:
    u32 PAD; // should NEVER be used!!!! for simd alignment!!!
```

This makes `sizeof(Vector3) = 16` instead of the expected 12 (3 floats). This is present on BOTH Xbox 360 and native builds.

## How the bone buffer works

`CharBones` stores animation data in a flat byte buffer (`mStart`) with sections for each bone type:
```
[POS data][SCALE data][QUAT data][ROTX data][ROTY data][ROTZ data]
```

`mOffsets[]` marks where each section starts, computed by `RecomputeSizes()`:
```cpp
mOffsets[i+1] = mOffsets[i] + TypeSize(i) * count_diff;
```

`TypeSize(TYPE_POS)` returns `sizeof(Vector3) = 16` when uncompressed.

## Why the file data is tricky

The `BinStream operator>>` for Vector3 only reads 12 bytes (x, y, z) — it does NOT read the PAD. But `sizeof(Vector3) = 16` means pointer arithmetic (`p++`) advances by 16 bytes.

For **cached** .milo_xbox files (identified by `mIsCached = true` when filename contains `.milo_`), the Save code explicitly writes 4 bytes of zero padding after each Vector3:
```cpp
bs << *p;           // 12 bytes
if (cached) {
    float zero = 0.0f;
    bs << zero;     // 4 bytes padding
}
```

So cached files have 16 bytes per position on disk. Non-cached have 12.

## Three data loading paths

In `CharBonesSamples::LoadData`:

1. **Bulk read** (`Cached() && !cachedPaddingMismatch`): Reads entire raw buffer at once, then byte-swaps manually. Used when cached AND positions are compressed (shorts).

2. **Cached padding mismatch** (`Cached() && compression < kCompressVects`): Reads element-by-element, skipping 4-byte padding after each Vector3. Used when cached AND positions are uncompressed floats.

3. **Non-cached element-by-element**: Reads via `d >> *p` which handles endian swapping automatically.

## Compression enum (important!)

```cpp
enum CompressionType {
    kCompressNone  = 0,
    kCompressRots  = 1,   // <-- NOT kCompressVects!
    kCompressVects = 2,
    kCompressQuats = 3,
    kCompressAll   = 4
};
```

The `crouching_great_01` clip has `compression=1` (kCompressRots):
- Positions: **uncompressed** (float Vector3, TypeSize=16)
- Quats: compressed as **short quats** (TypeSize=8)
- Rots: compressed as **shorts** (TypeSize=2)

## Diagnostic data captured

```
mFull: compression=1 samples=251 totalSize=192
mFull offsets: POS=0 SCALE=16 QUAT=16 ROTX=168 ROTY=168 ROTZ=168 END=186
mFull counts: 0 1 1 20 20 20 29
sizeof(Vector3)=16 sizeof(Quat)=16 sizeof(short)=2
```

Bone type breakdown: 1 POS, 0 SCALE, 19 QUAT, 0 ROTX, 0 ROTY, 9 ROTZ = 29 total.

Manual offset calculation with 12-byte Vector3 gives DIFFERENT offsets:
```
manual[0]: cnt=1 tsz=12 -> offset=12   (actual: 16)
manual[2]: cnt=19 tsz=8 -> offset=164  (actual: 168)
manual[5]: cnt=9 tsz=2 -> offset=182   (actual: 186)
```

The 4-byte difference propagates through the entire buffer.

## What still needs investigation

1. **Is the cachedPaddingMismatch path working correctly?** With compression=1, `mCompression < kCompressVects` = `1 < 2` = true, so `cachedPaddingMismatch = true`. The code reads 12 bytes + skips 4 bytes per Vector3 from the cached stream. This should be correct IF the cached file actually has 4 bytes of padding (confirmed by the Save code).

2. **Byte-swap correctness**: The cachedPaddingMismatch path reads via `d >>` (which calls ReadEndian), so byte-swapping should be handled. But need to verify the padding bytes are also properly consumed.

3. **Why does the pose LOOK wrong?** The bone transforms after PoseMeshes show reasonable-looking values:
   - Pelvis: pos=(0,-1.8,41.1) → (-1.6,-1.0,12.6) — large Z drop, but it's a crouching pose
   - Head world: (-0.5, 3.7, 33.6)
   - Rotation matrices look like valid rotations

4. **The viewer crashes with SIGSEGV** after rendering the screenshot. This might be a separate issue (cleanup/shutdown crash).

## Other fixes made this session

- **Z rotation convention**: Fixed transposed sin/cos in test-bone Z rotation (`milo_viewer.cpp`). Only affects test-bone, not PoseMeshes.
- **directPose inversion**: Fixed `!directPose` → `directPose` condition.
- **Midpoint beat**: Added `--frame -2` sentinel for midpoint = `(StartBeat + EndBeat) / 2`.
- **HamWardrobe.cpp**: Guarded `#include "xdk/LIBCMT/stdio.h"` with `#ifndef HX_NATIVE`.
- **pose_regression.sh**: Fixed `--start-frame` → `--frame`, updated paths to `archive/screenshots/`.

## RB3 comparison result

All 4 key functions (PoseMeshes, AcquirePose, ScaleAdd, ScaleAddSample) are **identical** between RB3 and DC3 decomps. The logic is correct — the issue is in data loading or layout, not in the math.

## Next steps

1. Add more diagnostics: dump the actual quat values from the bone buffer after ScaleAdd to verify they're reasonable
2. Compare a single sample's raw bytes against what the Xbox 360 would produce
3. Check if the SIGSEGV crash is related to the pose issue or a separate problem
4. Consider whether the visual result might actually be "correct data, wrong rendering" (transform hierarchy, skinning pipeline)

## Files modified (diagnostic code still present)
- `native/src/viewer/milo_viewer.cpp` — extensive diagnostic printf
- `src/system/char/CharBones.h` — added `GetCount()` accessor

## Additional Validation Added (2026-03-04, follow-up)

Added targeted tests to validate cached/non-cached data loading and end-to-end pose flow:

### 1) Serialization and stream-consumption tests (`native/tests/test_charbones_serialization.cpp`)

New tests:
- `CharBonesSamplesTest.SaveCachedFormat_SizeDeltaMatchesExpectedPadding`
- `CharBonesSamplesTest.TwoConsecutiveLoads_CachedUncompressed_NoDesync`
- `CharBonesSamplesTest.TwoConsecutiveLoads_CachedCompressedVects_NoDesync`

What these prove:
- Cached save path writes the expected extra bytes from:
  - per-`Vector3` padding (12-byte data + 4-byte pad for uncompressed vects), and
  - per-sample 16-byte alignment padding.
- Cached load path does not desync stream position when loading `mFull` then `mOne` sequentially (the exact failure mode we were chasing).
- Both cached branches are exercised:
  - `cachedPaddingMismatch` path (`compression < kCompressVects`)
  - bulk-read cached path (`compression >= kCompressVects`)

Result:
- `./milo-tests --gtest_filter='CharBonesSamplesTest.*'` -> **10/10 passed**

### 2) End-to-end clip channel to model/bones tests (`native/tests/test_bone_ground_truth.cpp`)

New tests:
- `ClipPoseFixture.ChannelEvaluationIsFiniteAtKeyBeats`
- `ClipPoseFixture.PelvisPosChannelMatchesPoseMeshesLocalPos`

What these prove:
- Channel evaluation values are finite and sane for clip bones at start and midpoint beats.
- End-to-end path is coherent for pelvis translation:
  - `CharClip::EvaluateChannel("bone_pelvis.pos")`
  - then `CharClip::PoseMeshes(...)`
  - resulting `bone_pelvis.mesh` local position matches evaluated channel value.

Result:
- `./milo-tests --gtest_filter='ClipPoseFixture.ChannelEvaluationIsFiniteAtKeyBeats:ClipPoseFixture.PelvisPosChannelMatchesPoseMeshesLocalPos'` -> **2/2 passed**

### Updated interpretation

The data read path is now strongly validated for both cached and non-cached test scenarios, and the channel->bones->transform pipeline is behaving consistently for key channels (pelvis position). This shifts remaining risk further toward rendering/visualization/screenshot harness behavior rather than raw clip binary decode.

## SIGSEGV hardening + regression guard (2026-03-04, follow-up 2)

To address the intermittent post-screenshot crash risk, `milo_viewer` teardown now uses a single shared shutdown path:
- release loaded dirs (`clipsDir`, `subdirs`, `baseDir`) first
- then call renderer termination (`gWgpuRnd->Terminate()`)
- used consistently by screenshot, video, export, and normal exits
- video error paths (framebuffer allocation / encoder start failure) now also use the same teardown path

Added integration test:
- `MiloViewerScreenshot.ScreenshotModeExitsCleanlyAndWritesPng` (`native/tests/test_milo_viewer_screenshot.cpp`)
  - launches `milo-viewer` in screenshot mode with real clip data
  - asserts clean exit (no signal), exit code `0`, and non-empty PNG output

Validation run:
- `./milo-tests --gtest_filter='MiloViewerScreenshot.*'` -> **1/1 passed**
- repeated manual screenshot loop (5 runs) -> **5/5 passed, no SIGSEGV**
