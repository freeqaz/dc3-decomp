# Mesh_Wgpu.cpp Refactoring Plan

**Date**: 2026-03-11
**Status**: Not started
**File**: `native/src/platform/Mesh_Wgpu.cpp` (1,346 lines, up from 1,280 at plan creation)

## Problem Statement

Mesh_Wgpu.cpp has grown into a monolith with 10+ distinct responsibilities in a single file. The `DrawMeshImmediate` function alone is 617 lines (630-1,246). Material uniform filling is duplicated between the main draw path and the multi-pass loop. Bone matrix computation is duplicated between main draw and shadow draw. Hardcoded mesh name skip lists and temporary diagnostics are interleaved with rendering logic. Frame capture recording is deeply tangled with draw state setup. The file has no clear seams — every piece reaches into globals and side-effects.

Since the original plan was written, the file grew by 66 lines:
- Added `debugLabel` field to `GpuMeshData` and `MeshLabel()` helper (committed)
- Added PropAnim shading overlay skip heuristic (lines 685-697, committed)
- Added position diagnostic tracing at frame 250 (lines 699-738, unstaged — temporary)
- Removed the TEXT_DIAG block that was previously at frame 250

## Design Principles

1. **Pure data transforms**: Uniform-filling functions take inputs and write output structs. No GPU calls, no globals. Testable with `printf`.
2. **Three-layer architecture**: Data Preparation → Resource Binding → Draw Emission. Debug any layer independently.
3. **One reason to change per file**: Each new file should change only when its domain changes.
4. **Slim orchestrator**: The draw function becomes a short pipeline of calls — visible control flow, no hidden logic.
5. **Diagnostics are separate**: Temporary frame-N tracing blocks and one-shot debug logging belong in their own module or are removed before refactor.

## Current Responsibility Map (updated)

| Lines | Responsibility | Coupling |
|-------|---------------|----------|
| 37-61 | Env var feature flags (IsSimpleRender, NoTransparentDefer) | Pure, self-contained |
| 63-76 | GpuMeshData struct definition (+ debugLabel) | Data type only |
| 78-115 | Dummy bone bind group (EnsureDummyBoneBindGroup) | `gWgpuRnd`, BoneUniforms |
| 117-165 | GPU mesh side table (cleanup, invalidation, OnSync) | `sMeshGpuData` global |
| 167-233 | Transparent draw queue (defer, sort, flush) | `sTransparentQueue`, camera/env state |
| 235-241 | MeshLabel helper (debug label resolution) | sMeshGpuData |
| 243-250 | SetMeshDepthBias | sMeshGpuData |
| 252-300 | Transform math (FillObjectUniforms, TransformToMat4) | Pure math |
| 302-327 | FixZeroAlpha vertex color fixup | Pure data transform |
| 329-418 | MikkTSpace tangent generation (7 callbacks + driver) | Pure data, mikktspace lib |
| 420-556 | Mesh upload (EnsureMeshUploaded — unpack, buffer create) | Side table, `gWgpuRnd`, vertex formats |
| 560-570 | Frame stats (sDrawCallsThisFrame, sFrameCounter) | Globals |
| 572-628 | DrawShowing entry (early-outs, transparent defer) | DrawMeshImmediate, transparent queue |
| 630-643 | DrawMeshImmediate preamble (null checks, scene sync) | `gWgpuRnd` |
| 645-697 | Mesh name skip filter (Kinect/UI + PropAnim overlay) | Hardcoded string checks |
| 699-738 | **TEMPORARY** position diagnostic (unstaged) | Debug-only, remove before refactor |
| 740-744 | Mesh upload call | MeshGpuCache |
| 754-780 | Pipeline key construction | Material state, PipelineKey |
| 782-915 | Material uniform filling + texture resolution | ~133 lines, duplicated in multi-pass |
| 917-947 | Auto-prelit detection (zero-ambient environment scan) | RndEnviron, lights |
| 949-960 | Detail normal / texgen uniforms | Material state |
| 962-1006 | Texture view resolution (diffuse through environ cube) | GetGpuTexView, fallbacks |
| 1008-1040 | Sampler creation + material bind group | SamplerDesc, bind groups |
| 1042-1060 | Object uniforms (group 2) | FillObjectUniforms |
| 1062-1101 | Bone uniforms (group 3) — duplicated in DrawMeshShadow | Bone matrices |
| 1103-1190 | Frame capture recording | DrawCallRecord, deeply interleaved |
| 1192-1246 | Draw emission + multi-pass loop | GPU commands + duplicated material setup |
| 1248-1346 | Shadow depth drawing (DrawMeshShadow) | Duplicated bone matrix logic |

## Pre-Refactor Cleanup

Before starting Phase 1, remove temporary diagnostics:
- [ ] **0a**: Remove the position diagnostic block (lines 699-738) — it's unstaged and temporary
- [ ] **0b**: Audit for any other frame-N diagnostic blocks and remove or gate behind `#ifdef MESH_DEBUG`

## Target Architecture

### File Structure

```
native/src/platform/
├── Mesh_Wgpu.cpp          → slim orchestrator (DrawShowing, DrawMeshImmediate, DrawMeshShadow)
├── MeshGpuCache.h         → GpuMeshData struct, upload/cleanup/invalidation API
├── MeshGpuCache.cpp        → side table, EnsureMeshUploaded, tangent gen, FixZeroAlpha
├── MaterialSetup.h         → BuildMaterialParams() API
├── MaterialSetup.cpp       → material uniform filling, texture resolution, sampler creation
├── BoneSetup.h             → FillBoneUniforms() API
├── BoneSetup.cpp           → bone matrix computation, dummy bone bind group
├── TransparentQueue.h      → defer/flush/sort API
├── TransparentQueue.cpp    → DeferredDraw, sTransparentQueue, FlushTransparentDraws
├── MeshFilter.h            → ShouldSkipMesh() API
├── MeshFilter.cpp          → Kinect/UI name checks + PropAnim overlay heuristic
├── TransformUtils.h        → FillObjectUniforms(), TransformToMat4() (inline or small header)
```

### Layer Breakdown

**Layer 1: Pure Data Transforms** (no GPU, no globals)
- `FillObjectUniforms(Transform → ObjectUniforms)` — world matrix + inverse-transpose
- `TransformToMat4(Transform → float[16])` — row-major 4x4
- `BuildMaterialParams(RndMat*/BaseMaterial*, flags → MaterialParams)` — material state to uniform struct + texture views
- `FillBoneUniforms(RndMesh* → BoneUniforms)` — bone matrices
- `BuildPipelineKey(RndMat*, meshState → PipelineKey)` — pipeline state selection

**Layer 2: Resource Binding** (GPU but no draw calls)
- `EnsureMeshUploaded(RndMesh* → GpuMeshData)` — vertex/index buffer management
- Texture resolution is part of `BuildMaterialParams` (resolves views with fallbacks)
- `CreateSampler(texWrap → wgpu::Sampler)` — sampler from tex wrap mode

**Layer 3: Draw Emission** (thin, just issues GPU commands)
- `DrawShowing()` — early-outs, transparent deferral, calls DrawMeshImmediate
- `DrawMeshImmediate()` — pipeline + bind groups + draw. ~80 lines of glue.
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

// Pure function — resolves textures but doesn't create bind groups
MaterialParams BuildMaterialParams(
    BaseMaterial* mat,     // works for RndMat* and multi-pass BaseMaterial*
    bool isTextMesh,
    bool forcePrelit,
    const WgpuRnd& rnd    // for fallback texture views
);
```

This eliminates the ~70-line duplication between the main draw path and the multi-pass loop. Both call the same function. The auto-prelit environment scan also moves here since it's part of material setup logic.

#### BoneSetup: Shared between main + shadow

```cpp
// BoneSetup.h
void FillBoneUniforms(RndMesh* mesh, BoneUniforms& out);
void EnsureDummyBoneBindGroup(WgpuRnd& rnd);
wgpu::BindGroup GetDummyBoneBindGroup();
```

Eliminates the duplicated bone matrix loop between `DrawMeshImmediate` (lines 1062-1101) and `DrawMeshShadow` (lines 1296-1321).

#### MeshFilter: Single point of change for skip rules

```cpp
// MeshFilter.h
bool ShouldSkipMesh(const char* meshName, RndMat* mat);
```

One function, one file. Consolidates the Kinect UI name checks (lines 645-683) AND the PropAnim overlay heuristic (lines 685-697). When we add/remove skip rules, we touch exactly one file.

#### Frame Capture: Observer, not interleaver

Currently, frame capture recording is interleaved throughout `DrawMeshImmediate` — checking `capturing` at 6+ points and building `DrawCallRecord` incrementally across ~87 lines (1103-1190). Instead:

```cpp
// At the end of DrawMeshImmediate, after the draw:
if (capturing) {
    RecordDrawCall(mesh, mat, matParams, objUni, meshData, key);
}
```

One call at the end. `RecordDrawCall` builds the entire `DrawCallRecord` from already-computed state. The NDC projection math (1134-1175) moves into `FrameCapture.cpp` since it's only needed for capture.

#### TransparentQueue: Self-contained module

```cpp
// TransparentQueue.h
bool IsTransparentBlend(int blend);
void DeferTransparentDraw(RndMesh* mesh, RndCam* cam, RndEnviron* env);
void FlushTransparentDraws(DrawFunc drawImmediate); // takes callback
bool HasTransparentDraws();
bool IsFlushingTransparentDraws();
```

`FlushTransparentDraws` takes a draw function callback instead of hardcoding `DrawMeshImmediate`, making the dependency explicit.

## Execution Plan

### Phase 0: Pre-refactor cleanup
- [ ] **0a**: Remove unstaged position diagnostic block (lines 699-738)
- [ ] **0b**: Audit for other temporary diagnostics; remove or `#ifdef` gate

### Phase 1: Extract pure utilities (no behavior change)

- [ ] **1a**: Create `TransformUtils.h` — move `FillObjectUniforms` and `TransformToMat4`. Header-only (inline functions). Zero risk.
- [ ] **1b**: Create `MeshFilter.h/cpp` — extract Kinect/UI skip block (lines 645-683) AND PropAnim overlay skip (lines 685-697) into `ShouldSkipMesh(name, mat)`. Replace in-place with single call.
- [ ] **1c**: Create `BoneSetup.h/cpp` — extract `FillBoneUniforms`, `EnsureDummyBoneBindGroup`, dummy bone globals. Used by both DrawMeshImmediate and DrawMeshShadow.

**Verify**: `native-build` compiles. Run `milo-viewer` on `glitterati.milo_xbox` — visual parity.

### Phase 2: Extract resource management

- [ ] **2a**: Create `MeshGpuCache.h/cpp` — move `GpuMeshData`, `sMeshGpuData`, `EnsureMeshUploaded`, `CleanupGpuMesh`, `OnSync`, `SetMeshDepthBias`, `MeshLabel`. Move `FixZeroAlpha` template and MikkTSpace callbacks + `ComputeMikkTangents` here (only used by upload). Move frame stats (sDrawCallsThisFrame, sFrameCounter, RndMesh_ResetFrameStats).
- [ ] **2b**: Create `TransparentQueue.h/cpp` — move `DeferredDraw`, `sTransparentQueue`, `FlushTransparentDraws`, `IsTransparentBlend`, `HasTransparentDraws`, `IsFlushingTransparentDraws`. The flush function takes a `void(*)(RndMesh*)` callback for DrawMeshImmediate.

**Verify**: Build + visual parity.

### Phase 3: Extract material setup (eliminates duplication)

- [ ] **3a**: Create `MaterialSetup.h/cpp` — implement `BuildMaterialParams()` as a pure function that fills `MaterialParams` from `BaseMaterial*`. Move all material uniform filling, heuristic logic (specular clamp, emissive guard, skin name detect, eye emissive boost, missing environ boost, fog blend check), auto-prelit detection, texture resolution with fallbacks, and sampler desc construction here.
- [ ] **3b**: Replace the ~200-line material block in `DrawMeshImmediate` with a single `BuildMaterialParams()` call + bind group creation.
- [ ] **3c**: Replace the duplicated material filling in the multi-pass loop (lines 1219-1232) with the same `BuildMaterialParams()` call.

**Verify**: Build + visual parity. This is the highest-risk phase since material rendering is sensitive. Screenshot comparison on glitterati + dclive venues.

### Phase 4: Clean up the orchestrator

- [ ] **4a**: Move frame capture recording to end-of-draw — one `RecordDrawCall()` invocation after `DrawIndexed`. Move NDC projection math into `FrameCapture.cpp`. The early-out skip recording (AddSkip calls) can stay inline since they're just one-liners.
- [ ] **4b**: Move env var flags (`IsSimpleRender`, `NoTransparentDefer`) into `MeshFilter.cpp` (they're rendering policy, same reason-to-change as skip rules).
- [ ] **4c**: Move `DrawMeshShadow` into its own file or keep at bottom of Mesh_Wgpu.cpp (small enough at ~100 lines). Decision: keep in Mesh_Wgpu.cpp since it shares the DrawMeshImmediate forward declaration pattern and is the only other draw entry point.

**Verify**: Build + visual parity + frame capture output matches.

### Phase 5: Final shape of Mesh_Wgpu.cpp

After all extractions, the file should be ~150-180 lines:

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
    // ~80 lines: orchestrate the three layers
    //   1. Filter → ShouldSkipMesh()
    //   2. Upload → EnsureMeshUploaded()
    //   3. Pipeline → BuildPipelineKey()
    //   4. Material → BuildMaterialParams() + write to ring + create bind group
    //   5. Object → FillObjectUniforms() + write to ring + create bind group
    //   6. Bones → FillBoneUniforms() or dummy + bind
    //   7. Draw → SetVertexBuffer/SetIndexBuffer/DrawIndexed
    //   8. Multi-pass → loop calling same BuildMaterialParams
    //   9. Capture → RecordDrawCall()
}

void DrawMeshShadow(RndMesh* mesh) {
    // ~60 lines: shadow-specific pipeline + shared BoneSetup
}
```

Each step is a single function call. The orchestrator reads like a recipe.

## Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|------------|
| 0 | None — removing dead code | Just build |
| 1 | Very low — pure moves | Compile + run |
| 2 | Low — resource management is mostly self-contained | Compile + visual check |
| 3 | Medium — material rendering is the most sensitive path | Screenshot comparison on 3+ venues. Diff frame capture output before/after. |
| 4 | Low — frame capture is diagnostic, not rendering | Compare capture JSON before/after |

## Lines of Code Estimate (post-refactor)

| File | Lines | Responsibility |
|------|-------|---------------|
| Mesh_Wgpu.cpp | ~160 | Orchestrator + shadow draw |
| MeshGpuCache.h/cpp | ~50/~260 | GPU resource management + upload + tangent gen + frame stats |
| MaterialSetup.h/cpp | ~30/~220 | Material uniform filling + tex resolution + auto-prelit |
| BoneSetup.h/cpp | ~15/~70 | Bone matrices + dummy bind group |
| TransparentQueue.h/cpp | ~15/~80 | Deferred transparent sorting |
| MeshFilter.h/cpp | ~5/~60 | Mesh skip rules + env var flags |
| TransformUtils.h | ~50 | Inline math utilities |
| **Total** | **~1,015** | Down from 1,346 (duplication + diagnostics removed) |

## Non-Goals

- Shader refactoring (standard.wgsl is a separate concern)
- Pipeline cache changes (PipelineManager is already well-structured)
- Render architecture changes (bind group layout, ring buffer strategy)
- Performance optimization (this is a readability refactor)
- Rnd_Wgpu.cpp refactoring (separate concern, separate plan)

## Verification Checklist

After each phase:
1. `native-build` target `dc3-native` compiles clean
2. `milo-viewer glitterati.milo_xbox` — visual parity (no rendering regressions)
3. `milo-viewer dclive.milo_xbox` — second venue for coverage
4. Frame capture output matches (phase 4)
5. Shadow rendering still works (phase 1c, 4c)
