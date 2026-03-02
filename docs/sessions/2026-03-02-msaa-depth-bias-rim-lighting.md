# Session: MSAA, Depth Bias, and Rim Lighting Fixes

**Date**: 2026-03-02
**Focus**: Fixing bright pixel artifacts on character silhouettes in native Milo Viewer

## Problems

Characters rendered in the native viewer had bright colored pixels along silhouette edges,
especially visible on dark outfits (aubrey05 black catsuit). Three separate causes identified:

1. **Z-fighting** — combined body mesh (skin material) bleeding through split meshes (outfit materials)
2. **Geometry edge aliasing** — jagged staircase edges at mesh silhouettes against black background
3. **Rim lighting fringe** — hot pink rim color `(1.0, 0.5, 0.75)` at power 50 creating colored edge glow

## Fixes

### 1. 4x MSAA (Multi-Sample Anti-Aliasing)

Added MSAA render target that resolves to the surface/readback texture.

**Files modified:**
- `native/src/platform/Rnd_Wgpu.h` — Added `kMSAASamples=4`, MSAA texture/view members
- `native/src/platform/Rnd_Wgpu.cpp` — MSAA color target creation in `BeginDrawing`, depth texture with `sampleCount=4`, render pass resolves MSAA→surface
- `native/src/gfx/PipelineManager.cpp` — `pipeDesc.multisample.count = 4`

**Bug encountered:** First attempt produced all-white screenshots. Root cause: MSAA texture was
created in `CreateDepthTexture()` during `Init()`, before `AcquireHeadlessFrame()` set the surface
format. The MSAA texture got `BGRA8Unorm` (default) while the headless readback texture was
`RGBA8UnormSrgb` — WebGPU validation error on format mismatch. Fixed by removing MSAA creation
from `CreateDepthTexture` and only creating it lazily in `BeginDrawing` after frame acquisition.

### 2. Depth Bias for Combined/Split Mesh Overlap

Increased depth bias from 4 to 100 on combined meshes that have splits. This pushes the combined
mesh far enough behind that split meshes always win the depth test, eliminating skin-colored
pixel bleed-through at mesh overlap edges.

**Files modified:**
- `native/src/viewer/milo_viewer.cpp` — `SetMeshDepthBias(&(*meshIt), 100)` for combined meshes

**Investigation path:** Tried hiding combined mesh entirely → lost arms and floating accessories.
Tried force-showing arm/leg meshes → wrong materials (CharMeshHide-assigned, not evaluated).
Depth bias was the right approach — keeps combined mesh as fallback for uncovered geometry.

### 3. Rim Lighting — Match Original Engine Behavior

Decompiled `NgMat::SetRegularShaderConst(bool)` via Ghidra to understand the original material→shader
constant pipeline. Key findings:

- **Rim color/power sent to both VS and PS at register 0x3d**
- **Rim power clamped to minimum 0.5** — `if (power < 0.5) power = 0.5`
- **`mRimLightUnder` flag** — compile-time `ENABLE_RIMLIGHT_UNDER` macro in original HLSL shader
- **Specular dimmed by 0.4x** when `mPerPixelLit && mSpecularMap` on first pass

Implemented `mRimLightUnder` support: when true, rim is modulated by `saturate(1 - dot(N, L))`
so it only appears on edges facing away from the primary light (backlit edge glow).

**Files modified:**
- `src/system/rndobj/BaseMaterial.h` — Added `GetRimLightUnder()` getter
- `native/src/platform/Rnd_Wgpu.h` — Added `rimLightUnder` to `MaterialUniforms`
- `native/src/platform/Mesh_Wgpu.cpp` — Pass `rimLightUnder` to uniform
- `native/src/gfx/standard_wgsl.inc` — Rim power min clamp, backlit modulation when `rimLightUnder`

**Remaining:** Aubrey05's outfit materials have omnidirectional (`rimLightUnder=false`) pink rim —
this is correct behavior. The pink fringe against black background will resolve once scene
backgrounds are loaded.

### Decomp Work Identified

`NgMat::SetRegularShaderConst(bool)` was not decomped — declared in `Mat_NG.h:27` but had no
implementation body. Had to use Ghidra to read it. This was handed off to a separate session
for decompilation. Symbol: `?SetRegularShaderConst@NgMat@@IAAX_N@Z`, file: `src/system/rndobj/Mat_NG.cpp`.

## Pixel Analysis Summary

| Artifact | Cause | Fix | Status |
|----------|-------|-----|--------|
| Staircase edges | No anti-aliasing | 4x MSAA | Fixed |
| Pink skin bleed at scarf/neck | Combined mesh z-fighting | Depth bias 100 | Fixed |
| Pink fringe on dark outfits | Rim lighting (correct) | `rimLightUnder` + power clamp | Correct behavior |
| Pinkish knee patches | Baked SSS in diffuse texture | Need dedicated skin shader | Known, not fixable now |

## Test Results

31/31 batch screenshots render successfully with all fixes applied.
