# Mesh_Wgpu.cpp Refactoring Plan

**Date**: 2026-03-11
**Status**: Planning
**File**: `native/src/platform/Mesh_Wgpu.cpp` (1,280 lines)

## Problem Statement

Mesh_Wgpu.cpp has grown into a monolith with 9+ distinct responsibilities in a single file. The `DrawMeshImmediate` function alone is 593 lines. Material uniform filling is duplicated between the main draw path and the multi-pass loop. Bone matrix computation is duplicated between main draw and shadow draw. Hardcoded mesh name skip lists are interleaved with rendering logic. Frame capture recording is deeply tangled with draw state setup. The file has no clear seams — every piece reaches into globals and side-effects.

## Design Principles

1. **Pure data transforms**: Uniform-filling functions take inputs and write output structs. No GPU calls, no globals. Testable with `printf`.
2. **Three-layer architecture**: Data Preparation → Resource Binding → Draw Emission. Debug any layer independently.
3. **One reason to change per file**: Each new file should change only when its domain changes.
4. **Slim orchestrator**: The draw function becomes a short pipeline of calls — visible control flow, no hidden logic.

## Current Responsibility Map

| Lines | Responsibility | Coupling |
|-------|---------------|----------|
| 37-61 | Env var feature flags (IsSimpleRender, NoTransparentDefer) | Pure, self-contained |
| 63-120 | GPU mesh side table (GpuMeshData, cleanup, invalidation) | `sMeshGpuData` global, `gWgpuRnd` |
| 130-201 | Transparent draw queue (defer, sort, flush) | `sTransparentQueue` global, camera/env state |
| 203-254 | Transform math (FillObjectUniforms, TransformToMat4) | Pure math |
| 256-281 | FixZeroAlpha vertex color fixup | Pure data transform |
| 283-372 | MikkTSpace tangent generation (7 callbacks + driver) | Pure data, mikktspace lib |
| 374-510 | Mesh upload (vertex unpack, buffer creation) | Side table, `gWgpuRnd`, vertex formats |
| 512-583 | DrawShowing entry (early-outs, transparent defer) | DrawMeshImmediate, transparent queue |
| 585-638 | Mesh name skip filter (Kinect/UI workarounds) | Hardcoded string checks |
| 639-709 | Pipeline key construction | Material state, PipelineKey |
| 710-910 | Material uniform filling + texture resolution | ~200 lines, duplicated in multi-pass |
| 911-944 | Sampler creation | Material tex wrap mode |
| 946-1005 | Object + bone uniform filling | FillObjectUniforms, bone matrices |
| 1007-1094 | Frame capture recording | DrawCallRecord, deeply interleaved |
| 1096-1177 | Draw emission + multi-pass loop | GPU commands + duplicated material setup |
| 1179-1280 | Shadow depth drawing | Duplicated bone matrix logic |

## Target Architecture

### File Structure

```
native/src/platform/
├── Mesh_Wgpu.cpp          → slim orchestrator (DrawShowing, DrawMeshImmediate, DrawMeshShadow)
├── MeshGpuCache.h         → GpuMeshData struct, upload/cleanup/invalidation API
├── MeshGpuCache.cpp        → side table, EnsureMeshUploaded, tangent gen, FixZeroAlpha
├── MaterialSetup.h         → FillMaterialUniforms(), ResolveTextures() API
├── MaterialSetup.cpp       → material uniform filling, texture resolution, sampler creation
├── BoneSetup.h             → FillBoneUniforms() API
├── BoneSetup.cpp           → bone matrix computation, dummy bone bind group
├── TransparentQueue.h      → defer/flush/sort API
├── TransparentQueue.cpp    → DeferredDraw, sTransparentQueue, FlushTransparentDraws
├── MeshFilter.h            → ShouldSkipMesh() API
├── MeshFilter.cpp          → hardcoded Kinect/UI name checks (single point of change)
├── TransformUtils.h        → FillObjectUniforms(), TransformToMat4() (inline or small header)
```

### Layer Breakdown

**Layer 1: Pure Data Transforms** (no GPU, no globals)
- `FillObjectUniforms(Transform → ObjectUniforms)` — world matrix + inverse-transpose
- `TransformToMat4(Transform → float[16])` — row-major 4x4
- `FillMaterialUniforms(RndMat*, flags → MaterialUniforms)` — material state to uniform struct
- `FillBoneUniforms(RndMesh* → BoneUniforms)` — bone matrices
- `BuildPipelineKey(RndMat*, meshState → PipelineKey)` — pipeline state selection

**Layer 2: Resource Binding** (GPU but no draw calls)
- `EnsureMeshUploaded(RndMesh* → GpuMeshData)` — vertex/index buffer management
- `ResolveTextures(RndMat* → MaterialTexViews)` — texture view resolution with fallbacks
- `CreateSampler(RndMat* → wgpu::Sampler)` — sampler from tex wrap mode

**Layer 3: Draw Emission** (thin, just issues GPU commands)
- `DrawShowing()` — early-outs, transparent deferral, calls DrawMeshImmediate
- `DrawMeshImmediate()` — pipeline + bind groups + draw. ~60 lines of glue.
- `DrawMeshShadow()` — shadow-specific pipeline + draw

### Key Design Decisions

#### MaterialSetup: Pure struct filling, not GPU binding

```cpp
// MaterialSetup.h
struct MaterialParams {
    MaterialUniforms uniforms;
    WgpuRnd::MaterialTexViews texViews;
    SamplerDesc samplerDesc;
    SamplerDesc mapSamplerDesc;
    uint32_t heuristics;
};

// Pure function — no GPU calls, no side effects
MaterialParams BuildMaterialParams(
    RndMat* mat,
    bool isTextMesh,
    bool forcePrelit,
    const WgpuRnd& rnd  // only for fallback texture views
);

// Same for multi-pass materials (eliminates duplication)
MaterialParams BuildMaterialParams(
    BaseMaterial* pass,
    const WgpuRnd& rnd
);
```

This eliminates the 70-line duplication between the main draw path and the multi-pass loop. Both call the same function.

#### BoneSetup: Shared between main + shadow

```cpp
// BoneSetup.h
void FillBoneUniforms(RndMesh* mesh, BoneUniforms& out);
void EnsureDummyBoneBindGroup(WgpuRnd& rnd);
wgpu::BindGroup GetDummyBoneBindGroup();
```

This eliminates the duplicated bone matrix loop between `DrawMeshImmediate` and `DrawMeshShadow`.

#### MeshFilter: Data-driven skip rules

```cpp
// MeshFilter.h
bool ShouldSkipMesh(const char* meshName);
```

One function, one file. When we remove Kinect workarounds or add new skip rules, we touch exactly one file. The skip list could eventually become data-driven (a `static const char*[]` table), but even as `strcmp` chains it's better isolated here than buried in DrawMeshImmediate.

#### Frame Capture: Observer, not interleaver

Currently, frame capture recording is interleaved throughout `DrawMeshImmediate` — checking `capturing` at 6+ points and building `DrawCallRecord` incrementally. Instead:

```cpp
// At the end of DrawMeshImmediate, after the draw:
if (capturing) {
    RecordDrawCall(mesh, mat, matParams, objUni, meshData, key);
}
```

One call at the end. `RecordDrawCall` builds the entire `DrawCallRecord` from the already-computed state. The NDC projection math moves into `FrameCapture.cpp` since it's only needed for capture.

#### TransparentQueue: Self-contained module

```cpp
// TransparentQueue.h
void DeferTransparentDraw(RndMesh* mesh, RndCam* cam, RndEnviron* env);
void FlushTransparentDraws();
bool HasTransparentDraws();
bool IsFlushingTransparentDraws();
```

The `DrawMeshImmediate` callback for deferred draws is registered once, not hardcoded.

## Execution Plan

### Phase 1: Extract pure utilities (no behavior change)

- [ ] **1a**: Create `TransformUtils.h` — move `FillObjectUniforms` and `TransformToMat4`. Header-only (inline functions). Zero risk.
- [ ] **1b**: Create `MeshFilter.h/cpp` — extract the mesh name skip block (lines 600-638) into `ShouldSkipMesh()`. Replace in-place with a single call. One `#include` change.
- [ ] **1c**: Create `BoneSetup.h/cpp` — extract `FillBoneUniforms`, `EnsureDummyBoneBindGroup`, dummy bone globals. Used by both DrawMeshImmediate and DrawMeshShadow.

**Verify**: `native-build` compiles. Run `milo-viewer` on `glitterati.milo_xbox` — visual parity.

### Phase 2: Extract resource management

- [ ] **2a**: Create `MeshGpuCache.h/cpp` — move `GpuMeshData`, `sMeshGpuData`, `EnsureMeshUploaded`, `CleanupGpuMesh`, `OnSync`, `SetMeshDepthBias`. Move `FixZeroAlpha` template and MikkTSpace callbacks + `ComputeMikkTangents` here (they're only used by upload).
- [ ] **2b**: Create `TransparentQueue.h/cpp` — move `DeferredDraw`, `sTransparentQueue`, `FlushTransparentDraws`, `IsTransparentBlend`, `HasTransparentDraws`, `IsFlushingTransparentDraws`.

**Verify**: Build + visual parity.

### Phase 3: Extract material setup (eliminates duplication)

- [ ] **3a**: Create `MaterialSetup.h/cpp` — implement `BuildMaterialParams()` as a pure function that fills `MaterialParams` from `RndMat*`. Move all the material uniform filling, heuristic logic, texture resolution, and sampler desc construction here.
- [ ] **3b**: Replace the ~200-line material block in `DrawMeshImmediate` with a single `BuildMaterialParams()` call.
- [ ] **3c**: Replace the duplicated material filling in the multi-pass loop with the same `BuildMaterialParams()` call (but passing `BaseMaterial* nextPass` instead of `RndMat*`).

**Verify**: Build + visual parity. This is the highest-risk phase since material rendering is sensitive. Screenshot comparison on glitterati + dclive venues.

### Phase 4: Clean up the orchestrator

- [ ] **4a**: Move frame capture recording to end-of-draw — one `RecordDrawCall()` invocation after `DrawIndexed`. Move NDC projection math into `FrameCapture.cpp`.
- [ ] **4b**: Move env var flags (`IsSimpleRender`, `NoTransparentDefer`) into a small `RenderFlags.h` or fold into `MeshFilter`.
- [ ] **4c**: Move `DrawMeshShadow` to `MeshShadow.cpp` (small, self-contained, uses BoneSetup + MeshGpuCache).

**Verify**: Build + visual parity + frame capture output matches.

### Phase 5: Final shape of Mesh_Wgpu.cpp

After all extractions, the file should be ~120-150 lines:

```cpp
#include "platform/MeshGpuCache.h"
#include "platform/MaterialSetup.h"
#include "platform/BoneSetup.h"
#include "platform/TransparentQueue.h"
#include "platform/MeshFilter.h"
#include "platform/TransformUtils.h"

void RndMesh::DrawShowing() {
    // ~20 lines: early-outs, transparent deferral
}

static void DrawMeshImmediate(RndMesh* mesh) {
    // ~60 lines: orchestrate the three layers
    //   1. Filter → ShouldSkipMesh()
    //   2. Upload → EnsureMeshUploaded()
    //   3. Pipeline → BuildPipelineKey()
    //   4. Material → BuildMaterialParams() + bind
    //   5. Object → FillObjectUniforms() + bind
    //   6. Bones → FillBoneUniforms() or dummy + bind
    //   7. Draw → SetVertexBuffer/SetIndexBuffer/DrawIndexed
    //   8. Multi-pass → loop calling same BuildMaterialParams
    //   9. Capture → RecordDrawCall()
}
```

Each step is a single function call. The orchestrator reads like a recipe.

## Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|------------|
| 1 | Very low — pure moves | Compile + run |
| 2 | Low — resource management is mostly self-contained | Compile + visual check |
| 3 | Medium — material rendering is the most sensitive path | Screenshot comparison on 3+ venues. Diff frame capture output before/after. |
| 4 | Low — frame capture is diagnostic, not rendering | Compare capture JSON before/after |

## Lines of Code Estimate (post-refactor)

| File | Lines | Responsibility |
|------|-------|---------------|
| Mesh_Wgpu.cpp | ~120 | Orchestrator |
| MeshGpuCache.h/cpp | ~50/~250 | GPU resource management + upload + tangent gen |
| MaterialSetup.h/cpp | ~30/~200 | Material uniform filling + tex resolution |
| BoneSetup.h/cpp | ~15/~70 | Bone matrices + dummy bind group |
| TransparentQueue.h/cpp | ~15/~80 | Deferred transparent sorting |
| MeshFilter.h/cpp | ~5/~50 | Mesh skip rules |
| TransformUtils.h | ~50 | Inline math utilities |
| MeshShadow.cpp | ~100 | Shadow depth drawing |
| **Total** | **~1,035** | Down from 1,280 (duplication removed) |

## Non-Goals

- Shader refactoring (standard.wgsl is a separate concern)
- Pipeline cache changes (PipelineManager is already well-structured)
- Render architecture changes (bind group layout, ring buffer strategy)
- Performance optimization (this is a readability refactor)

## Verification Checklist

After each phase:
1. `native-build` target `dc3-native` compiles clean
2. `milo-viewer glitterati.milo_xbox` — visual parity (no rendering regressions)
3. `milo-viewer dclive.milo_xbox` — second venue for coverage
4. Frame capture output matches (phase 4)
5. Shadow rendering still works (phase 1c, 4c)
