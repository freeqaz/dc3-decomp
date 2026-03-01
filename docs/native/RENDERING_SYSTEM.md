# Milo Engine Rendering System — DC3

## TL;DR: Is it PBR?

**No.** The Milo engine uses a **pre-PBR, Xbox 360-era shading model**:

- **Diffuse**: Half-Lambert (Valve-style wrapped diffuse: `NdotL * 0.5 + 0.5`, squared)
- **Specular**: Blinn-Phong (`pow(dot(N, H), specularPower)`) — no roughness/metalness
- **Rim lighting**: Fresnel-like edge highlight (`pow(1 - dot(N,V), rimPower)`)
- **Normal mapping**: Tangent-space, with optional detail normal overlay
- **Environment reflections**: Cube map, not split-sum IBL
- **Emissive**: Scalar-multiplied self-illumination map, not energy-conserving

There is no metalness map, no roughness map, no GGX BRDF, no split-sum approximation. Glossiness is packed into the specular map's alpha channel. This is a **custom multi-layer Blinn-Phong** system, characteristic of the Xbox 360/PS3 generation.

---

## Complete Texture Map Set Per Material

A `BaseMaterial` (the base class for `RndMat`) supports **9 texture slots**:

| Slot | Type | Field (offset) | Purpose |
|---|---|---|---|
| **Diffuse** | `RndTex` | `mDiffuseTex` (0x40) | Base color/albedo. Modulated by `mColor` and vertex color |
| **Diffuse 2** | `RndTex` | `mDiffuseTex2` (0x54) | Secondary diffuse (second UV layer or blend) |
| **Emissive** | `RndTex` | `mEmissiveMap` (0xe4) | Self-illumination/glow. Scaled by `mEmissiveMultiplier` |
| **Normal** | `RndTex` | `mNormalMap` (0xf8) | Tangent-space bump map. Requires `mPerPixelLit`. Strength via `mDeNormal` |
| **Detail Normal** | `RndTex` | `mNormDetailMap` (0x110) | Overlaid on primary normal map. Separate tiling (`mNormDetailTiling`) and strength (`mNormDetailStrength`) |
| **Specular** | `RndTex` | `mSpecularMap` (0x14c) | RGB = specular color, Alpha = glossiness/power. Requires per-pixel lit |
| **Rim** | `RndTex` | `mRimMap` (0x174) | RGB = rim color, Alpha = rim power |
| **Environment** | `RndCubeTex` | `mEnvironMap` (0x18c) | Cube map for reflections. Optional Fresnel falloff + specular-alpha masking |
| **Refraction Normal** | `RndTex` | `mRefractNormalMap` (0x1a8) | Screen-space distortion normal map. Falls back to `mNormalMap` if unset |

Additionally, `RndFur` objects carry a `mFurDetail` texture for fur noise patterns.

Lights can also carry textures:
- `RndLight::mTexture` — 2D projected gobo/spotlight texture
- `RndLight::mCubeTexture` — point light cube map projection

---

## Material Properties Reference

### Core Color & Blending
| Property | Type | Description |
|---|---|---|
| `mColor` | `Hmx::Color` | Base RGBA tint multiplied with diffuse texture |
| `mBlend` | enum | Dest, Src, Add, SrcAlpha, SrcAlphaAdd, Subtract, Multiply, PreMultAlpha, Screen, Lighten, Darken |
| `mZMode` | enum | Disable, Normal, Transparent, Force, Decal |
| `mAlphaThreshold` | int 0-255 | Alpha test cutoff |
| `mAlphaCut` / `mAlphaWrite` | bool | Alpha test / write control |
| `mIntensify` | bool | Doubles texture brightness (×2) |
| `mPrelit` | bool | Use vertex color as base instead of computed lighting |
| `mCull` | enum | None, Regular (backface), Backwards (frontface) |

### Specular
| Property | Type | Description |
|---|---|---|
| `mSpecularRGB` | `Hmx::Color` | RGB = specular color, Alpha = specular power |
| `mSpecular2RGB` | `Hmx::Color` | Secondary specular (for skin/hair shader variations) |
| `mAnisotropy` | float 0-100 | Hair strand anisotropic specular direction |
| `mPerPixelLit` | bool | Required for normal/specular maps to function |

### Rim Lighting
| Property | Type | Description |
|---|---|---|
| `mRimRGB` | `Hmx::Color` | RGB = rim color, Alpha = rim power (0-64) |
| `mRimLightUnder` | bool | Highlight undersides of meshes instead |

### Emissive
| Property | Type | Description |
|---|---|---|
| `mEmissiveMultiplier` | float | Bloom/emissive intensity scalar |
| `mBloomMultiplier` | float | Per-material bloom contribution |

### Normal Mapping
| Property | Type | Description |
|---|---|---|
| `mDeNormal` | float -3 to 1 | Bump strength damper |
| `mNormDetailTiling` | float | Detail normal UV tiling scale |
| `mNormDetailStrength` | float | Detail normal blend strength |

### Environment Mapping
| Property | Type | Description |
|---|---|---|
| `mEnvironMapFalloff` | bool | Fresnel-like glancing angle boost |
| `mEnvironMapSpecMask` | bool | Mask reflections by specular map alpha |

### Refraction
| Property | Type | Description |
|---|---|---|
| `mRefractEnabled` | bool | Enable screen-space refraction |
| `mRefractStrength` | float 0-100 | Distortion intensity |

### Shader Variations
| Variation | Description |
|---|---|
| `kShaderVariationNone` | Standard mesh shading |
| `kShaderVariationSkin` | Subsurface-scattering-style skin (dual specular lobes) |
| `kShaderVariationHair` | Anisotropic specular along strand direction |
| `kShaderVariationWorldProjection` | World-space UV projection with blend gradient |

### Fur System (`RndFur`)
Shell-based fur rendering with geometry layers, alpha falloff, gravity/wind physics, root-to-tip color gradient, and detail noise texture.

---

## Lighting Architecture

### Environment (`RndEnviron`)
- Two light lists: `mLightsReal` (high-quality, per-pixel, up to 4 shadow-casting) and `mLightsApprox` (cheap approximation)
- Ambient color with optional inheritance
- Linear fog (start/end/color), per-material opt-in
- Distance-based fade with left/right fade planes
- Tone mapping with exposure and white point
- Ambient occlusion (baked AO on meshes)

### Light Types (`RndLight::Type`)
| Type | Description |
|---|---|
| `kPoint` | Omni point light with range/falloff |
| `kDirectional` | Infinite directional light |
| `kFakeSpot` | Projected from point using cone math |
| `kFloorSpot` | Floor projection light |
| `kShadowRef` | Shadow projection reference |

All lights can carry projected 2D textures and/or cube maps.

---

## Post-Processing Pipeline (`RndPostProc`)

The engine has a rich post-processing stack:
- Bloom (tint, threshold, intensity, directional light streaks)
- Color grading (HSL, contrast, brightness, levels)
- Posterization, Kaleidoscope, Flicker
- Noise overlay, Trails/ghosting
- Motion blur (velocity-based + accumulation)
- Gradient map (luminance remapping with depth range)
- Full-screen refraction (normal map distortion)
- Chromatic aberration / sharpen
- Vignette, FPS emulation (24fps film look)

---

## Texture Formats

### RndTex::Type
| Type | Value | Purpose |
|---|---|---|
| `kRegular` | 0x1 | Standard bitmap |
| `kRendered` | 0x2 | Render target |
| `kMovie` | 0x4 | Bink video frame |
| `kBackBuffer` | 0x8 | Screen capture |
| `kShadowMap` | 0x42 | Shadow depth map |
| `kDepthVolumeMap` | 0xA2 | Volumetric depth |
| `kDensityMap` | 0x122 | Density (particles/fog) |
| `kRegularLinear` | 0x2000 | Linear (non-gamma) |

### Pixel Formats (RndBitmap `mOrder`)
| Name | mOrder | Format |
|---|---|---|
| ARGB | 1 | 32-bit BGRA |
| RGBA | 3 | 32-bit RGBA |
| DXT1/BC1 | 8 | Block compressed (4bpp) |
| DXT5/BC3 | 24 | Block compressed with alpha (8bpp) |
| ATI2/BC5 | 32 | Two-channel (normal maps) |

**Xbox 360 quirk**: DXT block data is big-endian. 16-bit color endpoints within each 4×4 block must be byte-swapped before uploading to a little-endian GPU.

---

## Native Port Status

The native port (WebGPU/Dawn) currently implements a **Tier 1 subset**:

### What works
- Single diffuse texture per material
- Half-Lambert diffuse + Blinn-Phong specular (uniform-driven, not texture-driven)
- Rim lighting (uniform only)
- Emissive multiplier (uniform only)
- All blend modes, depth modes, cull modes, stencil modes
- Vertex skinning (up to 40 bones)
- Linear fog
- 4 directional lights + ambient

### What's missing
- Normal maps (no TBN matrix, no tangent vertex attributes)
- Specular maps (specular is uniform-only)
- Emissive maps (uniform-only)
- Environment/cube maps
- Detail normal maps, rim maps
- UV channel 1 (only UV0 is unpacked)
- Shader variation programs (skin, hair, world projection)
- True `worldInvTranspose` (currently = `world`, wrong for non-uniform scale)
- Post-processing pipeline

---

## External Resources

- **MiloEditor** (`~/code/milohax/milo-engine-libs/harmonix-repos/MiloEditor/`) — C# reference implementations for all Rnd asset types, including full serialization/deserialization
- **milo-engine-libs INDEX** (`~/code/milohax/milo-engine-libs/INDEX.md`) — index of third-party Milo analysis tools
- **GRAPHICS_SYSTEM_DESIGN.md** (`docs/plans/custom-graphics-engine/`) — detailed porting analysis with WebGPU mapping
