# DC3 Native Port — Character Animation Pipeline

## Overview

The character animation system flows from .milo clip data through bone transforms to GPU-skinned mesh rendering.

## Data Flow: .milo → Screen

### Stage 1: Asset Loading
- **FileMerger** loads character .milo files and outfit packages
- **CharClip** stores compressed bone keyframe samples (positions, quaternions, rotations, scales)
- Data stored in `CharBonesSamples` grouped by keyframe, with beat→frame conversion

### Stage 2: Clip Playback (CharDriver → CharClipDriver)
- **CharDriver** (`src/system/char/CharDriver.cpp`) manages clip playback layers
- Maintains stack of `CharClipDriver` instances (clip playback state)
- `CharDriver::Poll()` evaluates animation:
  1. `mFirst->PreEvaluate()` — advance beat, check transitions
  2. `mFirst->Evaluate()` — interpolate keyframes
  3. `ScaleDown(*mBones, deltaBeat)` — initialize bone buffer to identity
  4. `ScaleAdd(*mBones, weight)` — blend clip animation into bone buffer

### Stage 3: Bone Buffer (CharBones → CharBonesMeshes)
- **CharBones** — flat array: `[positions...][scales...][quaternions...][rotXYZ...]`
- **CharBonesMeshes** — subclass that owns RndTransformable meshes
- `PoseMeshes()` applies bone data to mesh transforms via `SetLocalPos()` / `SetLocalXfm()`

### Stage 4: Mesh Transforms → GPU Skinning
- Each bone mesh is an `RndTransformable` with parent-child hierarchy
- `WorldXfm()` computes world transform via parent chain
- `FillBoneUniforms()` (`native/src/platform/BoneSetup.cpp`):
  - Reads `mesh->BoneTransAt(i)->WorldXfm()` for each bone
  - Computes `skinMatrix = BoneOffsetAt(i) * WorldXfm()`
  - Sanity check: bones with |translation| > 100,000 use identity matrix
- Vertex shader: `pos = sum(bone_matrix[i] * vertex_pos * weight[i])` for 4-bone blending

## dc3-native vs milo-viewer

| Aspect | milo-viewer | dc3-native |
|--------|------------|------------|
| Clip evaluation | Direct: `PoseMeshesWithFacing()` | Via `CharDriver::Poll()` |
| PoseMeshes | Explicit call each frame | Via CharBonesMeshes in driver |
| Face servo | Explicit `PollFace()` call | Via `RndDir::mPolls` iteration |
| Facing/foot | Post-adjustment | Handled by driver |

## Key Fixes Applied

1. **CharDriver::Poll early return bug** (commit 0190d6b38) — Two early returns blocked animation evaluation. Restructured as independent if-blocks.
2. **CharDriver::Enter timing** (commit 3ecf9f557) — Re-enter drivers after outfit loading completes.
3. **Bone garbage mitigation** (commit d6dffa637) — Sanity check in FillBoneUniforms for invalid translations.
4. **GPU skinning pipeline** (Session 63) — Skinned vertex format, 4-bone blending, 40-bone palettes.

## Current Status (Session 74)

- Characters animate correctly in gameplay (confirmed via screenshots)
- T-pose issue reported earlier has been resolved by CharDriver fixes
- Bone garbage fallback still in place (leg bones occasionally use bind-pose)
- Face rendering appears functional (head shapes with features visible)
