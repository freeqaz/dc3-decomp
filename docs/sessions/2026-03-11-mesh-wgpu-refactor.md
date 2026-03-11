# Mesh_Wgpu.cpp Refactoring Plan

**Date**: 2026-03-11
**Status**: Complete (Phases 0-5)

## Results

| Metric | Before | After |
|--------|--------|-------|
| Mesh_Wgpu.cpp | 1,346 lines | 387 lines |
| DrawMeshImmediate | 575 lines | 154 lines |
| Largest file | 1,346 lines | 387 lines |
| Material duplication | ~70 lines duplicated | 0 (shared BuildMaterialParams) |
| Bone duplication | ~40 lines duplicated | 0 (shared FillBoneUniforms) |
| Frame capture interleaving | 87 lines inline | 1 line call + helper |
| Skip logic | 53 lines inline | 1 line call (ShouldSkipMesh) |

### Final File Structure

```
native/src/platform/
├── Mesh_Wgpu.cpp          387 lines  Orchestrator (DrawShowing, DrawMeshImmediate, DrawMeshShadow)
├── TexGpu.h                11 lines  GetGpuTexView/GetGpuCubeTexView declarations
├── MeshGpuCache.h           47 lines  GpuMeshData struct + API
├── MeshGpuCache.cpp        339 lines  GPU resource management, upload, tangent gen, frame stats
├── MaterialSetup.h          26 lines  MaterialParams struct + builders
├── MaterialSetup.cpp       318 lines  Material uniform filling, texture resolution, auto-prelit
├── BoneSetup.h              17 lines  Bone API
├── BoneSetup.cpp            81 lines  Bone matrices + dummy bind group
├── TransparentQueue.h       20 lines  Queue API + blend classification
├── TransparentQueue.cpp    143 lines  Deferred draw sorting, text queue, env var flags
├── MeshFilter.h              5 lines  ShouldSkipMesh API
├── MeshFilter.cpp           61 lines  Kinect/UI skip rules + PropAnim overlay
├── TransformUtils.h         59 lines  FillObjectUniforms, TransformToMat4 (inline)
```

## Execution Log

### Phase 0: Pre-refactor cleanup
- [x] **0a**: Position diagnostic block was already unstaged/removed
- [x] **0b**: No other temporary diagnostics found

### Phase 1: Extract pure utilities
- [x] **1a**: Created `TransformUtils.h` — FillObjectUniforms + TransformToMat4 (inline)
- [x] **1b**: Created `MeshFilter.h/cpp` — ShouldSkipMesh() with Kinect/UI + PropAnim overlay skip
- [x] **1c**: Created `BoneSetup.h/cpp` — FillBoneUniforms + EnsureDummyBoneBindGroup

### Phase 2: Extract resource management
- [x] **2a**: Created `MeshGpuCache.h/cpp` — GpuMeshData, side table, upload, MikkTSpace, frame stats
- [x] **2b**: Created `TransparentQueue.h/cpp` — DeferredDraw, TextDraw, flush functions

### Phase 3: Extract material setup
- [x] **3a**: Created `MaterialSetup.h/cpp` with BuildMaterialParams() and BuildPassMaterialParams()
- [x] **3b**: Replaced ~200-line material block with single BuildMaterialParams() call
- [x] **3c**: Replaced duplicated multi-pass material filling with BuildPassMaterialParams()

### Phase 4: Clean up orchestrator
- [x] **4a**: Extracted frame capture into RecordDrawCall() helper (single call in DrawMeshImmediate)
- [x] **4b**: Moved NoTransparentDefer env var flag to TransparentQueue.cpp
- [x] Cleaned up unused includes (UiRenderHeuristics.h, Text.h, unordered_set, algorithm, vector, cmath)

### Phase 5: Staff Engineer review cleanup
- [x] **5a**: Created `TexGpu.h` — shared header for `GetGpuTexView`/`GetGpuCubeTexView`, replacing 5 duplicated `extern` declarations (Mesh_Wgpu.cpp, MaterialSetup.cpp, Part_Wgpu.cpp, DrawRect2D.cpp, ViewerScene.cpp)
- [x] **5b**: Removed dead transparent defer block in `DrawShowing` (24 lines guarded by `if (false && ...)`)
- [x] **5c**: Made `BuildPassMaterialParams` use `ResolveMap()` helper (consistency with `BuildMaterialParams`)
- [x] **5d**: Removed stale includes from Mesh_Wgpu.cpp (TransparentQueue.h, CubeTex.h, Env.h, Tex.h) and MaterialSetup.cpp (Tex.h)

## Verification Status
- [x] All mesh-related .cpp files compile clean
- [x] dc3-native links clean (pre-existing CharLipSync.cpp error is unrelated)
- [x] milo-viewer glitterati.milo_xbox — visual parity confirmed (venue geometry, textures, specular, lighting)
- [x] milo-viewer dclive.milo_xbox — visual parity confirmed (stage, speakers, floor, lighting rigs)
- [x] Character .milo dark as expected (no environment/lights in standalone character files — not a regression)
- [x] dc3-native headless: 10,000 frames, boots to choose_mode_screen, no crashes/segfaults/assertions
