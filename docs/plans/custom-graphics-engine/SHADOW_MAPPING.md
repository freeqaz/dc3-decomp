# Shadow Mapping Implementation Plan

## Current State

We have scaffolding in place from the bloom/shadow/DOF implementation pass:

- **`Rnd_Wgpu.h`**: Shadow depth texture (1024x1024 Depth32Float), comparison sampler, light VP matrix, `mShadowAvailable` flag
- **`Rnd_Wgpu.cpp`**: `EnsureShadowResources()` creates depth texture + comparison sampler. `RenderShadowPass()` computes light VP matrix from first directional light, clears shadow map — but **doesn't draw any geometry**.
- Shadow depth WGSL shader exists (static + skinned vertex shaders) but has **no pipeline** or bind group layout created for it.

## What's Missing

### 1. Shadow Depth Pipelines
Need dedicated shadow render pipelines (depth-only, no fragment output):
- **Static mesh** shadow pipeline (position-only vertex shader)
- **Skinned mesh** shadow pipeline (position + bone weights/indices)
- Different bind group layout from standard pipeline:
  - Group 0: `lightVP` uniform (mat4x4f)
  - Group 1: `object` uniform (world matrix only)
  - Group 2: `bones` uniform (skinned only)

### 2. Shadow Caster Geometry Collection
`RenderShadowPass()` needs to iterate all visible opaque meshes and draw them with the shadow pipeline. Options:

**Option A — Shadow draw flag on WgpuRnd** (chosen):
- Add `bool mInShadowPass` flag to WgpuRnd
- In `RenderShadowPass()`, set flag, iterate drawables, call a simplified draw path
- `DrawMeshImmediate()` or a new `DrawMeshShadow()` checks the flag and uses shadow pipeline
- Pro: Reuses existing mesh upload/bone infrastructure. Con: Requires drawable iteration mechanism.

**Option B — Replay draw list**:
- Record all opaque meshes during main pass, replay them in shadow pass
- Con: Requires an extra list, and shadow pass must run *before* main pass

**Chosen approach**: Option A with an explicit mesh list. After `BeginDrawing()` sets up the shadow pass, we need a way to iterate all drawables. The engine's `RndDir::DrawAll()` / `RndDrawable::DrawShowing()` traversal is what drives the main pass. For shadows, we'll collect meshes during the main pass (frame N) and use them in the shadow pass (frame N+1), or use the simpler approach of iterating the current world's draw list.

### 3. SceneUniforms Expansion
Add shadow fields to group 0 so the main fragment shader can sample the shadow map:

```cpp
// Append to SceneUniforms (currently 496 bytes):
float lightViewProj[16];  // +64 bytes  (mat4x4f)
float shadowEnabled;       // +4 bytes
float shadowBias;          // +4 bytes
float shadowMapSize;       // +4 bytes
float shadowStrength;      // +4 bytes  (0=full shadow, 1=no darkening)
// Total: 576 bytes (must be 16-byte aligned ✓)
```

### 4. Shadow Map Binding in Standard Shader
Bind group 0 currently has 1 entry (scene uniform buffer). Need to add:
- Binding 1: `texture_depth_2d` — shadow map
- Binding 2: `sampler_comparison` — shadow comparison sampler

This requires changes to:
- `PipelineManager::Init()` — expand group 0 bind group layout
- `WgpuRnd::WriteSceneUniforms()` — rebuild scene bind group with shadow texture
- `standard_wgsl.inc` — add shadow bindings, shadow sampling function, integrate into lighting loop

### 5. Shadow Sampling in Fragment Shader
In `fs_main`, after computing the normal and before the lighting loop:

```wgsl
fn sampleShadow(worldPos: vec3f) -> f32 {
    if (scene.shadowEnabled < 0.5) { return 1.0; }
    let clipPos = scene.lightViewProj * vec4f(worldPos, 1.0);
    let ndc = clipPos.xyz / clipPos.w;
    let uv = ndc.xy * vec2f(0.5, -0.5) + 0.5;  // flip Y for WebGPU
    if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0))) { return 1.0; }
    let bias = scene.shadowBias;
    // 3x3 PCF
    let texel = 1.0 / scene.shadowMapSize;
    var shadow = 0.0;
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            shadow += textureSampleCompare(shadowMap, shadowSampler,
                uv + vec2f(f32(x), f32(y)) * texel, ndc.z - bias);
        }
    }
    return mix(scene.shadowStrength, 1.0, shadow / 9.0);
}
```

Apply to directional light contributions:
```wgsl
let shadow = sampleShadow(in.worldPos);
totalDiffuse *= shadow;
totalSpecular *= shadow;
```

## Implementation Steps

### Step 1: Shadow Pipeline Infrastructure
- Create shadow-specific bind group layouts (light VP, object, bones)
- Create shadow depth render pipelines (static + skinned)
- Add shader module for shadow vertex shaders
- All in `Rnd_Wgpu.cpp` — keep separate from `PipelineManager` since these are specialized

### Step 2: Shadow Caster Drawing
- Add `DrawMeshShadow(RndMesh*)` function in `Mesh_Wgpu.cpp`
  - Uploads mesh GPU data (reuses `EnsureMeshUploaded`)
  - Binds shadow pipeline (static or skinned)
  - Writes object uniforms (world matrix) to ring buffer
  - Writes bone uniforms if skinned
  - Draws indexed
- Add `mInShadowPass` flag + shadow pipeline/bind group accessors to WgpuRnd

### Step 3: Shadow Pass Integration
- In `RenderShadowPass()`:
  - Compute light VP matrix (already done)
  - Write light VP to a dedicated uniform buffer
  - Begin depth-only render pass on `mShadowDepthTex`
  - Iterate current world's drawables → call `DrawMeshShadow()` for each opaque mesh
  - End pass
- Call `RenderShadowPass()` in `BeginDrawing()` before the main color pass

### Step 4: Scene Bind Group Expansion
- Expand `SceneUniforms` struct (496 → 576 bytes)
- Update WGSL `SceneUniforms` struct in `standard_wgsl.inc`
- Add shadow depth texture + comparison sampler to group 0 bind group layout in `PipelineManager::Init()`
- Update `WriteSceneUniforms()` to fill shadow fields
- Rebuild scene bind group to include shadow texture view + sampler

### Step 5: Shadow Sampling in Fragment Shader
- Add `sampleShadow()` function to `standard_wgsl.inc`
- Apply shadow factor to directional lighting in `fs_main`
- Gate on `scene.shadowEnabled` for zero-cost when no shadows

### Step 6: Light Frustum Tuning
- Current: fixed ±10m ortho centered at origin
- Improve: compute tight frustum from camera frustum + light direction (cascade shadow maps later)
- For now, fixed bounds work for DC3's stage-based venues

## Risk Areas

- **Performance**: Extra draw pass for all opaque meshes. Mitigated by depth-only (no fragment shader), and DC3 scenes are modest (typically <500 draw calls).
- **Drawable iteration**: We need access to the world's draw list outside the normal `DrawAll()` traversal. May need to tap into `RndDir`'s drawable list or the scene graph.
- **Bind group 0 change**: Expanding group 0 affects ALL existing pipelines. Must update `PipelineManager::Init()` carefully and clear the pipeline cache.
- **Self-shadowing artifacts**: PCF + bias handles most acne, but may need normal-offset bias for large polygons.

## Testing
- Simple scene with one directional light + flat ground + character standing
- Verify shadow on ground plane
- Toggle shadow on/off to compare
- Check skinned mesh shadows (character bones)
