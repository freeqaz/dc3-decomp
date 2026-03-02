# Material Rendering Improvements

**Goal**: Get the most out of DC3's existing assets by implementing all material features the Milo engine supports. The Xbox 360 shader microcode is lost, but BaseMaterial.h documents every input and its purpose. This plan covers everything from high-impact quick wins to longer-term visual polish.

## Current State (2026-03-02)

### Completed
- [x] **Phase 1: Multi-Texture Bind Group + Normal Mapping**
  - Bind group 1 expanded: 3→10 bindings (uniform + diffuse/normal/specular/emissive/rim textures + cube map + 3 samplers)
  - Default textures: flat normal (128,128,255), black (0,0,0), white (255,255,255), 1x1x6 black cube
  - Tangent frame: `float tangent[4]` added to both GpuVertex (48→64) and GpuVertexSkinned (72→88)
  - MikkTSpace tangent computation for uncompressed meshes; compressed meshes extract tangent from Xbox DEC4N `mBinormal` field
  - TBN matrix construction in vertex shaders, normal map sampling with `deNormal` diminish
  - worldInvTranspose: proper 3x3 inverse-transpose computation (was memcpy)
- [x] **Phase 2: Specular + Rim + Emissive Maps** (implemented as part of Phase 1)
  - Specular map: RGB modulates specular color, A modulates specular power
  - Emissive map: sampled and multiplied by emissiveMultiplier
  - Rim map: RGB + A modulate rim color and power
  - Material getters added: `GetSpecularMap()`, `GetRimMap()`, `GetDeNormal()`, `GetAnisotropy()`
- [x] **Phase 3: Hair Shader**
  - Kajiya-Kay anisotropic specular for `shaderVariation == 2`
  - Wrap diffuse (same as skin) for soft shadow falloff
  - Uses `material.anisotropy` for strand highlight sharpness
- [x] **Phase 4: Environment Mapping**
  - `TextureConvert::CreateCubeFromBitmaps()` — 6-face cubemap creation with Xbox byte swap/untile/DXT decompression
  - Cube texture GPU side table with lazy upload via `GetGpuCubeTexView()`
  - Bind group 1 expanded: 8→10 bindings (+cube texture, +cube sampler)
  - Default 1x1x6 black cubemap for materials without environ map
  - MaterialUniforms: 112→128 bytes (+environMapStrength, +environMapFalloff, +environMapSpecMask)
  - Reflection sampling with Fresnel falloff and specular map alpha masking
- [x] **Phase 5: Quick Wins**
  - Per-material fog opt-out (`mFog` + `AllowFog` blend mode check)
  - Prelit vertex color mode (skip lighting computation)
  - `GetFog()`, `GetUseEnviron()`, `GetEnvironMap()`, `GetEnvironMapFalloff()`, `GetEnvironMapSpecMask()` getters

- [x] **Phase 6: TexGen** — sphere map, environ, projected, xfm UVs
- [x] **Phase 7: Alpha-to-Coverage** — smooth alpha-tested edges with MSAA
- [x] **Phase 8: Detail Normal Maps** — second-layer normal mapping with UDN blending
- [x] **Phase 9: Point Lights** — up to 4 point lights with quadratic attenuation
- [x] **Phase 10: Multi-Pass Materials** — mNextPass chain rendering
- [x] **Phase 11: Transparent Sort Order** — deferred draw queue with back-to-front sorting
- [x] **Phase 12: Particle Rendering** — billboard quad generation in Part_Wgpu.cpp
- [x] **Phase 13: RndLine Rendering** — MapVerts, UpdateLine, SetPointsColor implemented
- [x] **Phase 14: DrawRect** — screen-space 2D textured/colored quads with gradients
- [x] **Phase 15: Post-Processing** — intermediate render target, color correction, vignette, chromatic aberration, posterize, levels

---

## Current Pipeline Architecture

### Bind Groups
```
Group 0 — SceneUniforms (496 bytes): viewProj, view, cameraPos, fog, 4 dir lights, ambient, 4 point lights
Group 1 — Material (176-byte uniform + 8 textures + 3 samplers)
  Binding 0: MaterialUniforms (dynamic offset)
  Binding 1: Diffuse texture (default: 1x1 white)
  Binding 2: Diffuse sampler
  Binding 3: Normal map (default: 1x1 flat normal 128,128,255)
  Binding 4: Specular map (default: 1x1 white)
  Binding 5: Emissive map (default: 1x1 black)
  Binding 6: Rim map (default: 1x1 white)
  Binding 7: Map sampler (shared for bindings 3-6)
  Binding 8: Environment cube map (default: 1x1x6 black cube)
  Binding 9: Cube sampler
  Binding 10: Detail normal map (default: 1x1 flat normal)
Group 2 — ObjectUniforms (128 bytes): world + worldInvTranspose (proper inverse-transpose)
Group 3 — BoneUniforms (2560 bytes): 40 bone matrices (always bound, identity for static)
```

### MaterialUniforms (176 bytes)
```cpp
float color[4];             // base color
float alphaThreshold;       // alpha cut
float useTexture;           // 1.0 if diffuse bound
float specularPower;        // Blinn-Phong exponent
float emissiveMultiplier;   // self-illumination scale
float specularColor[4];     // specular RGB + unused .a
float rimColor[4];          // rim RGB + .a = rim power
float intensify;            // 1.0 or 2.0
float shaderVariation;      // 0=standard, 1=skin, 2=hair
float rimLightUnder;        // backlit rim
float deNormal;             // normal map flatten/exaggerate (-3..1, 0=neutral)
float specular2Color[4];    // dual-lobe skin specular
float anisotropy;           // hair strand highlight sharpness
float hasNormalMap;          // 1.0 when normal map bound
float materialFogEnabled;   // 1.0 if fog applies to this material
float prelit;               // 1.0 if vertex color is pre-lit
float environMapStrength;   // 1.0 when environ map bound
float environMapFalloff;    // 1.0 for Fresnel falloff
float environMapSpecMask;   // 1.0 to mask by specular map alpha
float _pad3;                // align to 128
```

### Vertex Formats
- **Static (64 bytes)**: pos(3f), normal(3f), color(4f), uv(2f), tangent(4f)
- **Skinned (88 bytes)**: pos(3f), normal(3f), color(4f), uv(2f), boneWeights(4f), boneIndices(4u8), pad, tangent(4f)

### Shader Entry Points
- `vs_main` (static), `vs_skinned` (skinned), `fs_main` (shared)
- Standard: Lambert + Blinn-Phong with specular map masking
- Skin (`shaderVariation == 1`): Half-Lambert + warm shadow + dual specular
- Hair (`shaderVariation == 2`): Kajiya-Kay anisotropic specular + wrap diffuse

---

## Phase 6: Texture Coordinate Generation (TexGen)

The highest-impact remaining feature. Many DC3 materials use generated UVs for fake reflections (sphere map), scrolling/rotating patterns (xfm), and projections. Without TexGen, these materials either show nothing or incorrect UVs.

### TexGen modes (from BaseMaterial.h)

| Mode | Value | Description | Usage |
|------|-------|-------------|-------|
| `kTexGenNone` | 0 | Use vertex UV unchanged | Default, most materials |
| `kTexGenXfm` | 1 | Transform UV about center with stage xfm | Animated textures (scrolling, rotating) |
| `kTexGenSphere` | 2 | Sphere map that rotates with camera | Fake reflections on costumes, shiny props |
| `kTexGenProjected` | 3 | Project from direction of stage xfm in world coords | Projected textures, light cookies |
| `kTexGenXfmOrigin` | 4 | Like Xfm but about origin rather than center | Similar to Xfm with different pivot |
| `kTexGenEnviron` | 5 | Reflection map, perspective-correct | Higher quality reflections |

### Data available

- `mat->GetTexGen()` → `TexGen` enum
- `mat->TexXfm()` → `Transform` (3x3 rotation + translation) used by Xfm/Projected modes
- Vertex normal, position, camera position already in vertex shader

### Implementation plan

**Step 6.1: Pass TexGen mode to shader**

Add to MaterialUniforms (use `_pad3`):
```cpp
float texGenMode;  // 0=none, 1=xfm, 2=sphere, 3=projected, 4=xfmOrigin, 5=environ
```

Add texXfm as a mat3x3 (or pass as 3 vec4s for alignment) to MaterialUniforms. This grows the struct — we can pack it as 3 floats (2D xfm: scale, rotation, translate) since most TexXfm usage is 2D UV transforms:
```cpp
float texXfmRow0[4];  // xfm row 0 (u transform)
float texXfmRow1[4];  // xfm row 1 (v transform)
```

This grows MaterialUniforms 128→160 bytes.

**Step 6.2: Vertex shader UV generation**

In `vs_main` and `vs_skinned`, after computing worldPos and worldNormal:
```wgsl
var finalUV = in.uv;
if (material.texGenMode > 0.5 && material.texGenMode < 1.5) {
    // kTexGenXfm: transform UV about (0.5, 0.5) center
    let centered = finalUV - vec2f(0.5);
    finalUV = vec2f(
        dot(centered, material.texXfmRow0.xy) + material.texXfmRow0.z + 0.5,
        dot(centered, material.texXfmRow1.xy) + material.texXfmRow1.z + 0.5
    );
} else if (material.texGenMode > 1.5 && material.texGenMode < 2.5) {
    // kTexGenSphere: sphere map from view-space normal
    let viewNormal = (scene.view * vec4f(N, 0.0)).xyz;
    finalUV = viewNormal.xy * 0.5 + 0.5;
} else if (material.texGenMode > 4.5) {
    // kTexGenEnviron: reflection-based UV (higher quality sphere map)
    let R = reflect(-V, N);
    let viewR = (scene.view * vec4f(R, 0.0)).xyz;
    finalUV = viewR.xy * 0.5 + 0.5;
}
out.uv = finalUV;
```

**Step 6.3: Fill TexGen uniforms at draw time**

In `Mesh_Wgpu.cpp`:
```cpp
matUni.texGenMode = (float)mat->GetTexGen();
if (mat->GetTexGen() == kTexGenXfm || mat->GetTexGen() == kTexGenXfmOrigin) {
    const Transform& xfm = mat->TexXfm();
    matUni.texXfmRow0[0] = xfm.m.x.x; matUni.texXfmRow0[1] = xfm.m.x.y;
    matUni.texXfmRow0[2] = xfm.v.x;   matUni.texXfmRow0[3] = 0;
    matUni.texXfmRow1[0] = xfm.m.y.x; matUni.texXfmRow1[1] = xfm.m.y.y;
    matUni.texXfmRow1[2] = xfm.v.y;   matUni.texXfmRow1[3] = 0;
}
```

### Files Modified (Phase 6)
- `native/src/platform/Rnd_Wgpu.h` — MaterialUniforms growth (128→160)
- `native/src/platform/Mesh_Wgpu.cpp` — fill texGenMode + texXfm
- `native/src/gfx/standard_wgsl.inc` — UV generation in vertex shaders

---

## Phase 7: Alpha-to-Coverage

Quick win — one pipeline flag change gives smooth alpha-tested edges on hair, foliage, and fences using existing 4x MSAA.

### Implementation

**Step 7.1: Add alphaToCoverage flag to PipelineKey**

```cpp
bool alphaToCoverage = false;  // in PipelineKey
```

**Step 7.2: Set flag in pipeline creation**

In `PipelineManager::CreatePipeline`:
```cpp
pipeDesc.multisample.alphaToCoverageEnabled = key.alphaToCoverage;
```

**Step 7.3: Enable for alpha-cut materials**

In `Mesh_Wgpu.cpp`, when building PipelineKey:
```cpp
key.alphaToCoverage = mat->GetAlphaCut();
```

### Files Modified (Phase 7)
- `native/src/gfx/PipelineManager.h` — `alphaToCoverage` in PipelineKey
- `native/src/gfx/PipelineManager.cpp` — set flag in CreatePipeline
- `native/src/platform/Mesh_Wgpu.cpp` — enable for alpha-cut materials

---

## Phase 8: Detail Normal Maps

Second-layer normal mapping for close-up surface detail (fabric weave, skin pores, scratches). Quick win since the normal map pipeline is already in place.

### Data available
- `mNormDetailMap` (ObjPtr<RndTex>, offset 0x110) — detail normal texture
- `mNormDetailTiling` (float, offset 0x124) — UV tiling scale for detail map
- `mNormDetailStrength` (float, offset 0x128) — blend strength of detail bumps

### Implementation

**Step 8.1: Add getters to BaseMaterial.h**
```cpp
RndTex* GetNormDetailMap() const { return mNormDetailMap; }
float GetNormDetailTiling() const { return mNormDetailTiling; }
float GetNormDetailStrength() const { return mNormDetailStrength; }
```

**Step 8.2: Expand MaterialUniforms**
Add 3 floats (+ 1 pad) = 16 bytes growth (160→176):
```cpp
float normDetailTiling;    // UV tiling for detail normal map
float normDetailStrength;  // blend strength (0 = disabled)
float hasNormDetailMap;    // 1.0 when detail map bound
float _pad4;
```

**Step 8.3: Add binding for detail normal map**
Option A: Reuse existing map sampler with a new texture binding (group 1 binding 10).
Option B: Sample detail map using same normalMapTex slot but with tiled UVs — not possible (different texture). Use binding 10.

**Step 8.4: WGSL fragment shader**
After initial normal map sampling, blend detail normal:
```wgsl
if (material.hasNormDetailMap > 0.5) {
    let detailUV = in.uv * material.normDetailTiling;
    let detailSample = textureSample(normDetailMapTex, mapSampler, detailUV);
    let detailNorm = detailSample.xyz * 2.0 - 1.0;
    // Blend detail into base normal using UDN blending
    tsNormal = normalize(vec3f(
        tsNormal.x + detailNorm.x * material.normDetailStrength,
        tsNormal.y + detailNorm.y * material.normDetailStrength,
        tsNormal.z
    ));
}
```

### Files Modified (Phase 8)
- `src/system/rndobj/BaseMaterial.h` — detail map getters
- `native/src/platform/Rnd_Wgpu.h` — MaterialUniforms growth (160→176)
- `native/src/gfx/PipelineManager.cpp` — bind group 1: 10→11 bindings
- `native/src/platform/Rnd_Wgpu.cpp` — CreateMaterialBindGroup 10→11 entries
- `native/src/platform/Mesh_Wgpu.cpp` — resolve detail map, fill uniforms
- `native/src/gfx/standard_wgsl.inc` — detail normal blending

---

## Phase 9: Point Lights

Venues and stages use point lights for localized illumination (spotlights on dancers, colored stage lights). Currently only directional lights are supported.

### Data available (RndLight)
- Light types: `kPoint` (0), `kDirectional` (1), `kFakeSpot` (2), `kFloorSpot` (3), `kShadowRef` (4)
- Point light data: position via `WorldXfm().v`, color via `GetColor()`, range via `GetRange()`, falloff start
- Iteration: `RndEnviron::LightsReal()` returns list of point/spot lights
- Currently we only iterate for `kDirectional` — need to also handle `kPoint`

### Implementation

**Step 9.1: Expand SceneUniforms**
Add point light array (up to 4):
```cpp
float pointLightPos[4][4];     // array<vec4f, 4> — position per light (.w unused)
float pointLightColors[4][4];  // array<vec4f, 4> — color per light
float pointLightRanges[4];     // falloff range per light
float numPointLights;
float _padPL[3];
```
SceneUniforms grows from 336 to ~464 bytes.

**Step 9.2: Fill point lights from RndEnviron**
In `WriteSceneUniforms()`, iterate `LightsReal()` for `kPoint` type lights:
```cpp
int pointIdx = 0;
for (auto it = lights.begin(); it != lights.end() && pointIdx < 4; ++it) {
    RndLight* light = *it;
    if (!light || !light->Showing()) continue;
    if (light->GetType() != RndLight::kPoint) continue;
    const Transform& lxfm = light->WorldXfm();
    scene.pointLightPos[pointIdx] = {lxfm.v.x, lxfm.v.y, lxfm.v.z, 0};
    // ... color, range
    pointIdx++;
}
scene.numPointLights = (float)pointIdx;
```

**Step 9.3: WGSL fragment shader**
After directional light loop, add point light loop with distance attenuation:
```wgsl
for (var i = 0; i < i32(scene.numPointLights); i++) {
    let lightPos = scene.pointLightPos[i].xyz;
    let toLight = lightPos - in.worldPos;
    let dist = length(toLight);
    let L = normalize(toLight);
    let range = scene.pointLightRanges[i];
    let atten = saturate(1.0 - dist / range);
    let NdotL = max(dot(N, L), 0.0);
    totalDiffuse += scene.pointLightColors[i].rgb * NdotL * atten * atten;
    // specular...
}
```

### Files Modified (Phase 9)
- `native/src/platform/Rnd_Wgpu.h` — SceneUniforms growth + point light fields
- `native/src/platform/Rnd_Wgpu.cpp` — fill point lights in WriteSceneUniforms()
- `native/src/gfx/standard_wgsl.inc` — point light loop in fragment shader
- `src/system/rndobj/Lit.h` — add `GetRange()` getter if not present (check `#ifdef HX_NATIVE`)

---

## Phase 10: Multi-Pass Materials

`mNextPass` chains multiple BaseMaterial objects on the same mesh. Used for detail overlays (dirt, scratches), decal-on-skin layering, costume transparency effects.

### Implementation

After drawing the first material pass, walk the `NextPass()` chain and issue additional draw calls with each subsequent material's uniforms and textures. Each pass uses its own blend mode.

```cpp
// In RndMesh::DrawShowing(), after the primary draw call:
BaseMaterial* nextPass = mat->NextPass();
while (nextPass) {
    // Build new pipeline key with nextPass blend/z/etc
    // Fill material uniforms from nextPass
    // Resolve textures from nextPass
    // Issue draw call with same vertex/index buffers
    nextPass = nextPass->NextPass();
}
```

### Files Modified (Phase 10)
- `native/src/platform/Mesh_Wgpu.cpp` — nextPass draw loop

---

## Phase 11: Transparent Sort Order

Transparent objects (SrcAlpha, Add, SrcAlphaAdd blend modes) need back-to-front sorting. Currently drawn in arbitrary order causing artifacts on overlapping translucent surfaces.

### Implementation

Split rendering into two passes:
1. **Opaque pass**: Draw all meshes with opaque blend modes (Src, Multiply) — any order
2. **Transparent pass**: Collect transparent meshes, sort by distance from camera (centroid), draw back-to-front

This requires deferring draw calls rather than issuing them immediately in `DrawShowing()`. Options:
- Collect draw commands in a vector during traversal, sort, then execute
- Or use a two-pass frame structure in `BeginDrawing`/`EndDrawing`

### Files Modified (Phase 11)
- `native/src/platform/Mesh_Wgpu.cpp` — deferred draw collection + sort
- `native/src/platform/Rnd_Wgpu.cpp` — two-pass frame structure (optional)

---

## Phase 12: Particle Rendering

RndParticleSys is DC3's particle system — confetti, stage effects, light beams, sparkles.

### Current State (from research)
- **Core physics IMPLEMENTED**: `DrawShowing()`, `UpdateParticles()`, `CreateParticles()`, `MoveParticles()` all working
- **Stubbed methods (7)**: `Load()` (native only), `Poll()`, `InitParticle()`, `Mats()`, `Replace()`, plus `RndSoftParticleBuffer::Queue/DoPost`
- `DrawShowing()` already calls `UpdateParticles()` — the physics engine runs
- **Missing**: Billboard quad generation — particles are simulated but never rendered as geometry

### Implementation plan

**Step 12.1: Unstub critical methods**
- `Poll()` — time-based particle updates (non-frame-driven mode)
- `InitParticle()` — per-particle initialization with override
- `Mats()` — material querying for draw ordering
- `Load()` — binary deserialization (complex, many fields)

**Step 12.2: Billboard quad generation (Part_Wgpu.cpp)**
Create `native/src/platform/Part_Wgpu.cpp` that hooks into `DrawShowing()`:
- Walk the active particle linked list
- For each particle, generate a camera-facing quad (4 verts, 6 indices):
  ```
  right = camera.xAxis * particle.size
  up    = camera.zAxis * particle.size
  // Rotate by particle.angle around view direction
  v0 = center - right - up    uv(0,1)
  v1 = center + right - up    uv(1,1)
  v2 = center + right + up    uv(1,0)
  v3 = center - right + up    uv(0,0)
  ```
- UV tiling: `u = (tileIdx % tilesAcross) / tilesAcross`, `v = (tileIdx / tilesAcross) / tilesDown`
- Vertex color from `particle.col` (includes alpha for fade)

**Step 12.3: Dynamic vertex buffer + batch draw**
- Pre-allocate GPU buffer for max particles (e.g., 1024 quads = 4096 verts)
- Each frame: write all particle quads, single draw call per particle system
- Use the particle system's `mMat` for pipeline (blend mode, texture)
- Reuse DrawRect's 2D pipeline or the standard mesh pipeline with identity transforms

### Files Modified (Phase 12)
- `src/system/rndobj/Part.cpp` — unstub `Load()` (native `#ifdef`)
- `native/src/engine_stubs_generated.cpp` — remove stubs for Poll, InitParticle, Mats
- New: `native/src/platform/Part_Wgpu.cpp` — billboard generation + batch draw

---

## Phase 13: RndLine Rendering

3D lines with width and perspective — debug visualization, light beams, trails.

### Current State (from research)
- **Most of Line.cpp IMPLEMENTED**: constructor, Load, Save, DrawShowing, SetNumPoints, UpdateInternal, etc.
- **3 methods STUBBED**: `UpdateLine()` (612 bytes), `MapVerts()` (200 bytes), `SetPointsColor()` (448 bytes)
- `DrawShowing()` calls `UpdateLine()` then `mMesh->DrawShowing()` — once UpdateLine generates geometry, rendering is free
- `SetNumPoints()` allocates 2 verts per point (+ cap verts), sets UVs and colors
- Vertex layout: `RndMesh::Vert` (0x60 bytes) with pos, norm, color, uv, bone weights
- Stubbed in RB3 too — platform-specific code, needs from-scratch implementation

### Implementation plan

**Step 13.1: Implement MapVerts(int idx, VertsMap& vmap)**
Maps a point index to its vertex range in `mMesh->Verts()`:
```cpp
void RndLine::MapVerts(int idx, VertsMap& vmap) {
    int vertIdx = idx * 2;  // base: 2 verts per point
    if (mHasCaps && !mLinePairs) {
        // First point: cap + main (4 verts), subsequent: main only (2 verts)
        if (idx == 0) { vmap.t = 1; vertIdx = 0; }          // first cap
        else if (idx == (int)mPoints.size() - 1) { vmap.t = 2; vertIdx = ...; } // last cap
        else { vmap.t = 0; vertIdx = 2 + (idx - 1) * 2 + 2; }
    } else { vmap.t = 0; }
    vmap.v = &mMesh->Verts()[vertIdx];
}
```

**Step 13.2: Implement UpdateLine(const Transform& camXfm, float nearPlane)**
Core ribbon geometry generation — for each consecutive point pair:
1. Compute segment direction `D = normalize(p[i+1] - p[i])`
2. Compute camera-facing perpendicular: `side = normalize(cross(camPos - midpoint, D)) * mWidth * 0.5`
3. Set vertex positions: `v[2i] = p[i] + side`, `v[2i+1] = p[i] - side`
4. Handle fold angle: when angle between segments > `mFoldAngle`, insert a fold
5. Generate end caps if `mHasCaps`: semicircle or flat cap at first/last points
6. Call `mMesh->Sync(0x1F)` to mark dirty

**Step 13.3: Implement SetPointsColor(int start, int count, const Hmx::Color& color)**
Batch color update — loop from start to start+count, call `UpdatePointColor(i, false)`, then sync once.

### Files Modified (Phase 13)
- `src/system/rndobj/Line.cpp` — implement `UpdateLine()`, `MapVerts()`, `SetPointsColor()` with `#ifdef HX_NATIVE`
- `native/src/engine_stubs_generated.cpp` — remove 3 Line stubs

---

## Phase 14: RndFlare + DrawRect [DONE]

DrawRect implemented in WgpuRnd — screen-space textured/colored quads with gradient support, all blend modes. Separate 2D WGSL shader + pipeline. Both `Rnd::DrawRect` and `NgRnd::DrawRect` overrides. TestPoint stubbed (always visible).

---

## Phase 15: Post-Processing Effects

Full-screen effects managed by `RndPostProc` (0x228+ bytes) and `RndPostProcMgr`.

### Current State (from research)
- **RndPostProc**: All property syncing + Load/Save implemented. 20+ effect parameters fully deserialized
- **RndColorXfm**: `AdjustLightness/Contrast/Brightness/Levels` implemented. `AdjustHue/Saturation` STUBBED
- **NgPostProc**: `OnSelect()` implemented. `DoPost()`, `EndWorld()`, `SetBloomColor()` all STUBBED
- **PostProcessor interface**: `BeginWorld()`, `EndWorld()`, `DoPost()` — virtual dispatch via `Rnd::DoPostProcess()`
- **PostProcMgr**: Poll-based blending between PostProcs (fully implemented)

### Architecture required
Scene rendering must change from direct-to-swapchain to intermediate texture:
1. **BeginDrawing**: Render to intermediate MSAA texture (not swapchain)
2. **EndDrawing**: Resolve MSAA → single-sample intermediate
3. **DoPost**: Read intermediate texture, apply effects, write to swapchain
4. **Ping-pong buffers**: bloom requires multiple blur passes

### Implementation plan (priority order)

**Step 15.1: Render pipeline restructuring**
- Add intermediate render target texture (same size as swapchain)
- BeginDrawing renders to intermediate instead of swapchain
- EndDrawing resolves MSAA → intermediate, then runs post-proc, then presents
- If no post-proc active, blit intermediate directly to swapchain

**Step 15.2: Fullscreen quad infrastructure**
- Reuse DrawRect's 2D pipeline for fullscreen passes
- Post-proc bind group: intermediate texture + effect uniform buffer
- New WGSL shader: `postproc_wgsl.inc` with fullscreen vertex shader + effect fragment shaders

**Step 15.3: Color correction (highest impact)**
Implement `NgPostProc::DoPost()` color correction pass:
- Read from intermediate texture
- Apply RndColorXfm transform: contrast, brightness, levels
- Also implement `AdjustHue()` and `AdjustSaturation()` in ColorXfm.cpp (HSL rotation)
- Write to swapchain

**Step 15.4: Vignette**
- Screen-space distance from center → darken edges
- `mVignetteIntensity` (0-2) and `mVignetteColor` (edge tint)
- Simple addition to color correction pass

**Step 15.5: Bloom**
Multi-pass effect:
1. **Threshold pass**: Extract bright pixels (`luminance > mBloomThreshold`)
2. **Downsample**: 4 mip levels (half-res each time)
3. **Blur**: Gaussian blur at each mip level (horizontal + vertical = 2 passes per level)
4. **Composite**: Blend blurred result with scene color (`mBloomIntensity`, `mBloomColor`)
Requires 2 ping-pong textures per mip level.

**Step 15.6: Chromatic aberration**
- Sample R/G/B channels at slightly offset UVs
- `mChromaticAberrationOffset` controls pixel offset
- Simple single-pass effect

### Files Modified (Phase 15)
- `native/src/platform/Rnd_Wgpu.h/.cpp` — intermediate render target, post-proc dispatch
- New: `native/src/gfx/postproc_wgsl.inc` — fullscreen shaders (color correct, bloom, vignette)
- `src/system/rndobj/ColorXfm.cpp` — implement `AdjustHue()`, `AdjustSaturation()` (`#ifdef HX_NATIVE`)
- `src/system/rndobj/PostProc_NG.cpp` — implement `DoPost()` dispatch (`#ifdef HX_NATIVE`)

---

## Implementation Order & Dependencies

```
Phase 1: Multi-Texture Bind Group + Normal Mapping  [DONE]
Phase 2: Specular + Rim + Emissive Maps             [DONE — merged into Phase 1]
Phase 3: Hair Shader                                 [DONE]
Phase 4: Environment Mapping                         [DONE]
Phase 5: Quick Wins (fog, prelit)                    [DONE]
Phase 6: TexGen (sphere map, xfm, environ UVs)      [DONE]
Phase 7: Alpha-to-Coverage                           [DONE]
Phase 8: Detail Normal Maps                          [DONE]
Phase 9: Point Lights                                [DONE]
Phase 10: Multi-Pass Materials                       [DONE]
Phase 11: Transparent Sort Order                     [DONE]
Phase 12: Particle Rendering                         [DONE]
Phase 13: RndLine Rendering                          [DONE]
Phase 14: RndFlare + DrawRect                        [DONE]
Phase 15: Post-Processing                            [DONE]
```

### All phases complete!

Remaining stretch goals (not planned):
- **Bloom**: Multi-pass blur pipeline (threshold → downsample → gaussian blur → composite). Requires ping-pong textures.
- **Motion blur**: Velocity buffer + temporal blending
- **Depth of field**: DOFProc with bokeh sampling
- **Gradient map**: Color grading via 1D LUT texture
- **Noise/film grain**: Animated noise overlay
- **Hall of Time**: Feedback effect with trail persistence
