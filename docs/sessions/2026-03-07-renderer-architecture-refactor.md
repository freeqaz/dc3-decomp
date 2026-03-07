# Session: Renderer Architecture Refactor Plan

**Date**: 2026-03-07
**Goal**: Refactor native port WebGPU renderer for better debuggability and maintainability

## Current State

| File | Lines | Responsibility |
|------|------:|----------------|
| `Rnd_Wgpu.cpp` | 2,523 | Everything: frame lifecycle, uniforms, shadows, bloom, DoF, post-proc, 2D, screenshots |
| `Rnd_Wgpu.h` | 379 | 70+ member variables, 40+ methods |
| `Mesh_Wgpu.cpp` | 1,017 | Vertex upload, material setup, draw calls, transparent sorting, heuristics |
| `Tex_Wgpu.cpp` | 147 | Texture upload (cleanest file) |
| `Part_Wgpu.cpp` | 344 | Particle rendering |
| `PipelineManager.cpp` | 342 | Pipeline cache |
| `GpuDevice.cpp` | 381 | Device/window management |
| **Total** | **6,135** | |

28 scattered `printf`/`fprintf` calls. Zero structured debug output.

## Core Problems

### 1. No visibility into what's actually happening per-frame

When the UI looks wrong, you can't answer:
- Which meshes drew? Which were skipped, and why?
- What uniform values were sent? Was alpha 0? Was prelit forced?
- Which heuristic fired? Did auto-prelit kick in? Did alpha get forced to 1?
- What pipeline state was active? Blend mode? Z-mode? Cull?
- What textures were bound? Fallback white/black, or the real one?

The only diagnostics are periodic printf every 300 frames showing draw call count, and camera info every 500 frames.

### 2. God-object WgpuRnd (2,523 lines, 70+ members)

WgpuRnd owns everything: frame lifecycle, uniform ring buffers, scene uniform computation, shadow mapping, bloom, depth of field, post-processing, 2D drawing, screenshot capture, and default texture creation. Each subsystem has its own `Ensure*Pipeline()`, textures, shaders, and uniform buffers -- all crammed into one class.

### 3. Monolithic DrawMeshImmediate (~350 lines)

One function handles pipeline key construction, material uniform filling, 8+ embedded heuristics, texture resolution for 7 slots, sampler creation, object/bone transforms, bind group creation, vertex/index binding, and multi-pass rendering.

### 4. Implicit heuristics scattered in draw path

These are all embedded inline with no names, no toggles, no logging:

| Heuristic | Location | What it does |
|-----------|----------|-------------|
| Multiply skip | Mesh_Wgpu:532 | Skips all multiply-blend meshes |
| Alpha force | Mesh_Wgpu:561 | Forces alpha=1 on SrcAlpha materials with alpha<0.01 |
| Specular clamp | Mesh_Wgpu:577 | Forces minPower=32, scale=0.4x on low-power specs |
| Emissive guard | Mesh_Wgpu:590 | Zeroes emissive multiplier when no emissive map |
| Skin name detect | Mesh_Wgpu:609 | Detects skin materials by `_skin`/`_head` in name |
| Auto-prelit | Mesh_Wgpu:657 | Forces prelit for zero-ambient + <=1 light envs |
| Text mesh detect | Mesh_Wgpu:655 | Detects text by empty mesh name |
| Eye emissive boost | Mesh_Wgpu:709 | Boosts emissive for `eyes`/`eye_` materials |
| Zero vertex alpha fix | Mesh_Wgpu:400,431 | Forces alpha=1 when all sampled vertices have 0 alpha |

When the UI renders incorrectly, you must mentally trace through all of these. No logging, no way to disable one at a time.

### 5. GPU resource tracking via disconnected side tables

Three separate file-scope `unordered_map`s with no unified interface:
- `sMeshGpuData` in Mesh_Wgpu.cpp
- `sTexGpuData` in Tex_Wgpu.cpp
- `sCubeTexGpuData` in Tex_Wgpu.cpp

### 6. Embedded shader source strings

WGSL shader sources are embedded as C string literals (~260 lines total across post-proc, bloom, shadow, 2D, DoF). Can't hot-reload or edit without recompiling.

---

## Refactoring Plan

### Phase 1: Draw Call Capture System (highest impact, lowest risk)

**Goal**: Answer "what happened this frame?" for any draw call.

New files:
```
native/src/gfx/FrameCapture.h
native/src/gfx/FrameCapture.cpp
```

**DrawCallRecord** captures: mesh name, material name, draw index, PipelineKey, MaterialUniforms snapshot, object transform, heuristics bitmask, texture bindings (real vs fallback), skip reason, camera name, deferred status.

**Activation**:
- `MILO_CAPTURE_FRAME=N` -- capture frame N and dump to stdout/file
- Keypress (F12) -- capture next frame
- Programmatic: `FrameCapture::Get().StartCapture()`

**Output**: Structured text or JSON, filterable by mesh name, material name, blend mode, or heuristic.

**Estimate**: ~300 lines new code. Zero risk -- read-only observation.

### Phase 2: Extract Render Passes (WgpuRnd 2,523 -> ~800 lines)

New files:
```
native/src/gfx/ShadowPass.h/.cpp    -- shadow map rendering (~200 lines)
native/src/gfx/BloomPass.h/.cpp     -- bloom chain (~250 lines)
native/src/gfx/DofPass.h/.cpp       -- depth of field (~200 lines)
native/src/gfx/PostProcPass.h/.cpp   -- color grading/vignette (~300 lines)
native/src/gfx/DrawRect2D.h/.cpp    -- 2D quad drawing (~200 lines)
```

Each pass owns its GPU resources, lazy init, Run/Render method, and cleanup. WgpuRnd becomes a thin orchestrator.

### Phase 3: Material State Builder (tame the heuristics)

New files:
```
native/src/gfx/MaterialStateBuilder.h/.cpp
```

Each heuristic becomes a named enum value with toggle support:
- `kMultiplySkip`, `kAlphaForce`, `kSpecularClamp`, `kEmissiveGuard`, `kSkinNameDetect`, `kAutoPrelit`, `kTextMeshDetect`, `kEyeEmissiveBoost`, `kZeroAlphaFix`
- Disable at runtime: `MILO_DISABLE_HEURISTIC=alpha_force,auto_prelit`
- Integrates with Phase 1 capture to log which heuristics fired per draw call.

`DrawMeshImmediate` drops from ~350 lines to ~80 (just bind + draw).

### Phase 4: GPU Resource Registry

New files:
```
native/src/gfx/GpuResourceRegistry.h/.cpp
```

Replaces three scattered `unordered_map` side tables with unified registry. Provides mesh/texture CRUD, debug queries (count, memory estimate), and `DumpStats()`.

### Phase 5: Shader Source Management (optional)

Move WGSL shader sources to separate files:
```
native/shaders/postproc.wgsl
native/shaders/bloom.wgsl
native/shaders/shadow.wgsl
native/shaders/dof.wgsl
native/shaders/draw2d.wgsl
```

Hot-reload: watch file timestamps, recompile on change. Fallback: embed via `.wgsl.inc` at build time.

## Execution Priority

| Phase | Effort | Risk | Debugging value |
|-------|--------|------|-----------------|
| **1. Frame Capture** | Small (~300 lines new) | Zero | **Highest** |
| **3. Material State Builder** | Medium (~400 lines, refactor) | Low | **High** |
| **2. Extract Render Passes** | Medium (~500 lines moved) | Low | Medium |
| **4. GPU Resource Registry** | Small (~200 lines) | Low | Medium |
| **5. Shader Hot-Reload** | Medium (~200 lines + build) | Low | Nice-to-have |

Recommended order: 1, 3, 2, 4, 5.
