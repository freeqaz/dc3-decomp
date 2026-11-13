# Pose Regression Testing Guide

## Overview

The pose regression suite captures deterministic screenshots of character poses and compares them against golden baselines. This catches skeleton, animation, and rendering regressions.

## Quick Reference

```bash
# Capture new screenshots
./native/scripts/pose_regression.sh

# Compare against goldens
./native/scripts/pose_regression.sh --compare

# Update goldens after intentional changes
./native/scripts/pose_regression.sh --update-goldens
```

## Prerequisites

- `milo-viewer` built: `cd native/build && cmake --build . --target milo-viewer`
- Vulkan ICD available (GPU headless rendering)
- DC3 assets at `$MILO_LIB` (defaults to `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3`)

## Test Poses

| Name | Description | Mode |
|------|-------------|------|
| `tpose_aubrey` | Rest pose (no clip) | Default |
| `crouch_great_start` | `crouching_great_01` at start beat | `--direct-pose` (PoseMeshes) |
| `crouch_great_mid` | `crouching_great_01` at midpoint | `--direct-pose` (PoseMeshes) |
| `stand_bad_start` | `stand_bad_01` at start beat | `--direct-pose` (PoseMeshes) |
| `stand_bad_mid` | `stand_bad_01` at midpoint | `--direct-pose` (PoseMeshes) |

## When to Update Goldens

Re-baseline after intentional changes to:
- `CharClip::PoseMeshes` or `CharBonesSamples` evaluation
- Skeleton loading/hierarchy (`CharBones`, bone transforms)
- Mesh rendering pipeline (shaders, transform application)
- `milo-viewer` screenshot/pose path

Do **not** re-baseline to paper over unexpected diffs — investigate first.

## Troubleshooting

### All captures show SKIP
**Cause**: Asset files not found at `$MILO_LIB` path.
**Fix**: Set `MILO_LIB` to point to extracted DC3 assets, or verify the default path exists.

### All captures show FAIL (0 bytes)
**Cause**: `milo-viewer` not built, or Vulkan ICD not available.
**Fix**: Rebuild `milo-viewer`. Check `vulkaninfo` works. For headless environments, ensure a GPU or software Vulkan driver is available.

### Dance clips show T-pose instead of dance pose
**Cause**: `--direct-pose` not being passed, or the `PoseMeshes` codepath is broken.
**Fix**: Verify `pose_regression.sh` passes `--direct-pose` for dance entries. Check that `milo_viewer.cpp` uses `PoseMeshes` when `directPose` is true (not inverted).

### Start and mid captures are identical
**Cause**: `--frame` sentinel values (`-1` for start, `-2` for midpoint) not being handled by the viewer.
**Fix**: Ensure the viewer resolves `-1` to `activeClip->StartBeat()` and `-2` to the midpoint `(StartBeat() + EndBeat()) * 0.5`. Check that the script passes `--frame` (not `--start-frame`).

### DIFF on regression check after driver/GPU update
**Cause**: Different GPU drivers can produce pixel-level differences in rendering.
**Fix**: This is expected after driver updates. Verify the diff is cosmetic (use ImageMagick `compare` for visual diff), then re-baseline with `--update-goldens`.

### Sandbox blocks Vulkan access
**Cause**: Claude Code sandbox restricts device access.
**Fix**: Run with `dangerouslyDisableSandbox: true` for GPU-dependent commands. Use `/sandbox` to manage restrictions.

## File Locations

- Script: `native/scripts/pose_regression.sh`
- Captures: `archive/screenshots/pose_regression/captures/`
- Goldens: `archive/screenshots/pose_regression/goldens/`
- Numeric pose tests: `native/tests/test_bone_ground_truth.cpp` (Gates 1-3)
