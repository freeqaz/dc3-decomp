# Session: Skin Shader Implementation Plan

**Date**: 2026-03-01
**Focus**: Fix skin rendering artifacts, implement proper character skin material

## The Problem

Characters with exposed skin (bare arms, legs) show faint pinkish/reddish patches at areas
like inner knees and elbows. Systematic elimination proved these are **authored into the
diffuse texture** — the artists painted subsurface scattering hints into the albedo, expecting
the Xbox 360's skin shader to handle them properly.

Our current shader treats skin materials identically to every other material (standard Lambert
diffuse + Blinn-Phong specular), which means:
- The SSS-tinted albedo regions render too visibly pink/red
- Hard shadow terminator makes skin look plastic
- Missing dual-specular lobes (broad "oily" + tight highlight)
- No warm shadow tinting at the diffuse falloff boundary

## What the Original Engine Had

The Milo engine has **explicit skin material support**:

### Material Properties (from `BaseMaterial.h`)

| Property | Field | Offset | Purpose |
|----------|-------|--------|---------|
| Shader Variation | `mShaderVariation` | 0x1bc | `kShaderVariationSkin = 1`, `kShaderVariationHair = 2` |
| Specular 1 | `mSpecularRGB` | 0x12c | Primary specular color + power (`.alpha`) |
| Specular 2 | `mSpecular2RGB` | 0x13c | Secondary specular color + power (`.alpha`) |
| Specular Map | `mSpecularMap` | 0x14c | Per-pixel specular mask texture |
| Rim Color | `mRimRGB` | 0x164 | Rim/fresnel light color |
| Rim Map | `mRimMap` | 0x174 | Per-pixel rim mask texture |
| Rim Light Under | `mRimLightUnder` | 0x188 | Whether rim also lights underside |
| Prelit | `mPrelit` | 0x3d | Skip dynamic lighting |
| Intensify | `mIntensify` | 0x68 | 2x texture brightness |

The `shader_variation` property tells the Xbox GPU shader system to select a **skin-specific
pixel shader**. The Xbox 360's compiled shader microcode is in the original binary
(not decompilable), but based on the available material inputs and standard techniques of that
era (2012 Xbox 360), the skin shader almost certainly used:

1. **Softened diffuse** — Half-Lambert or wrap lighting (value = NdotL * 0.5 + 0.5)
2. **Warm shadow terminator** — red/warm tint blended into the shadow falloff zone
3. **Dual specular lobes** — broad "oily skin" lobe + tight highlight lobe
4. **Fresnel rim** — warm-tinted, using the rim color/power from the material
5. **Per-pixel specular map** — modulates specular intensity across the surface

### What the Skin Shader Does NOT Need

Full multi-pass texture-space SSS diffusion (GPU Gems 3 style) was likely too expensive for
DC3 with multiple full-body dancers. The Xbox 360's limited eDRAM (10MB) makes multi-pass
framebuffer effects expensive due to resolve costs. A single-pass approximation with wrap
lighting + dual specular is the standard approach for games of that era with many characters.

## Implementation Plan

### Phase 1: Short-Term Fix (Current Session)

**Goal**: Make skin look right with a proper single-pass skin shader in WGSL.

#### 1a. Pass `shader_variation` to the GPU

In `Mesh_Wgpu.cpp`, read `mShaderVariation` from the material and pass it to the shader
as a new field in `MaterialUniforms`:

```c
// In MaterialUniforms struct:
float shaderVariation;  // 0=none, 1=skin, 2=hair

// In draw code:
matUni.shaderVariation = (float)mat->mShaderVariation;
```

Also pass `mSpecular2RGB` (second specular lobe):

```c
matUni.specular2Color[0] = mat->mSpecular2RGB.red;
matUni.specular2Color[1] = mat->mSpecular2RGB.green;
matUni.specular2Color[2] = mat->mSpecular2RGB.blue;
matUni.specular2Power   = mat->mSpecular2RGB.alpha;  // power stored in alpha
```

#### 1b. Add skin branch to fragment shader

In `standard_wgsl.inc`, add a skin-specific lighting path:

```wgsl
// Detect skin material
let isSkin = material.shaderVariation > 0.5 && material.shaderVariation < 1.5;

if (isSkin) {
    // Half-Lambert (wrap) diffuse — softens shadow falloff
    let wrapNdotL = dot(N, L) * 0.5 + 0.5;
    let skinDiffuse = wrapNdotL * wrapNdotL;  // squared for energy conservation

    // Warm shadow tint at the terminator (SSS approximation)
    let shadowZone = smoothstep(0.0, 0.5, dot(N, L));
    let warmTint = mix(vec3f(0.8, 0.3, 0.15), vec3f(1.0), shadowZone);
    totalDiffuse += lightColor * skinDiffuse * warmTint;

    // Dual specular: broad "oily" lobe + tight highlight
    let H = normalize(L + V);
    let NdotH = max(dot(N, H), 0.0);
    let spec1 = pow(NdotH, material.specularPower) * material.specularColor.rgb;
    let spec2 = pow(NdotH, material.specular2Power) * material.specular2Color.rgb;
    totalSpecular += lightColor * (spec1 + spec2);
} else {
    // Standard Lambert + Blinn-Phong (unchanged)
    ...
}
```

#### 1c. Verify with crop renders

Run the crop one-liner on aubrey01, aubrey04 to verify the pinkish knee patches are
softened by the wrap lighting and warm shadow tint.

### Phase 2: Polish (Near-Term)

**Goal**: Fill out the remaining material features that affect skin quality.

- **Specular map support** — read `mSpecularMap` texture and use it to modulate specular
  intensity. This prevents specular from appearing equally on all skin regions.
- **Rim map support** — read `mRimMap` texture for per-pixel rim lighting control.
- **Rim light under** — honor `mRimLightUnder` flag for lighting underside geometry.
- **Hair shader variation** — `kShaderVariationHair = 2` likely uses anisotropic specular
  (Kajiya-Kay or similar). Lower priority but visible on close-ups.

### Phase 3: HQ Mode (Long-Term, Modern Approach)

**Goal**: Optional high-quality rendering mode using modern WebGPU techniques.

#### Pre-Integrated Skin Shading (Single-Pass)

A lookup-texture approach that bakes the SSS profile into a 2D LUT indexed by NdotL and
curvature. This is the FaceWorks / NVIDIA approach — single rendering pass, no extra
framebuffer allocations, good for multiple characters.

Implementation:
1. Pre-compute a 256x256 LUT: x-axis = NdotL, y-axis = 1/curvature
2. Each texel = integral of the diffusion profile over the corresponding surface patch
3. In shader: compute curvature from `fwidth(worldNormal)` or `fwidth(worldPos)`
4. Sample the LUT instead of using `max(NdotL, 0.0)`

#### Screen-Space Separable SSS (Multi-Pass)

For close-ups, hero shots, or photo mode:

1. Render skin materials into a separate color+depth buffer (diffuse irradiance only,
   no specular)
2. Horizontal Gaussian blur (weighted by depth, skin mask)
3. Vertical Gaussian blur
4. Recombine: blurred diffuse * albedo + unblurred specular

This is Jimenez's separable SSS technique — two 1D convolution passes instead of full 2D,
much cheaper than texture-space diffusion. WebGPU compute shaders make this straightforward.

#### Derived Data from Existing Assets

Since DC3 assets don't have dedicated SSS inputs, derive them:

| Needed | Source |
|--------|--------|
| Scatter mask | Redness channel of diffuse texture + `shader_variation == skin` |
| Curvature | `fwidth(worldNormal)` in shader, or pre-baked from mesh |
| Thickness | Simple backlight term based on `-NdotL` for silhouette edges |
| Skin detection | `mShaderVariation == kShaderVariationSkin` (already in material data) |

## Priority Order

1. **Phase 1** — Fix the current visual issue. Half-Lambert + warm tint + dual specular
   in WGSL. This should take the pink patches from "jarring" to "subtle authoring choice."
2. **Phase 2** — Specular and rim maps. Incremental improvement, straightforward.
3. **Phase 3** — Only when there's demand for a "photo mode" or face close-up quality.

## References

- [GPU Gems 3 Ch.14: Advanced Skin Rendering](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-14-advanced-techniques-realistic-real-time-skin)
- [Valve Source Engine Shading (Half-Lambert)](https://cdn.fastly.steamstatic.com/apps/valve/2006/SIGGRAPH06_Course_ShadingInValvesSourceEngine.pdf)
- [FaceWorks: Pre-Integrated Skin Shading](https://www.reedbeta.com/talks/faceworks/gtc-2014-faceworks.pdf)
- Jimenez et al., "Separable Subsurface Scattering" (EGSR 2015)

## Files to Modify

| File | Change |
|------|--------|
| `native/src/gfx/standard_wgsl.inc` | Add skin shader branch with Half-Lambert + warm tint + dual spec |
| `native/src/platform/Mesh_Wgpu.cpp` | Pass `shaderVariation`, `specular2Color`, `specular2Power` |
| `native/src/platform/Rnd_Wgpu.h` | Update `MaterialUniforms` struct |
| `native/src/gfx/GpuDevice.cpp` | May need to update uniform buffer size |
