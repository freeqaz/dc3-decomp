# Rendering reference

How DC3's Milo rendering data maps onto the native WebGPU renderer, and where that renderer's code actually lives.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

Code paths below are repo-relative to dc3-decomp unless they start with
`../milo-native-engine/`, which is the shared engine checkout next to it.

## Where the renderer code lives

- **Every desktop and web target links the engine's renderer.** It is
  `WgpuRnd` (`../milo-native-engine/src/platform/Rnd_Wgpu.h`), built as the
  engine's `dc3` GPU backend flavor (`set(MILO_ENGINE_GPU_BACKEND dc3 ...)` in
  `native/CMakeLists.txt`). It includes:
  - the platform TUs `../milo-native-engine/src/platform/{Rnd_Wgpu,Tex_Wgpu,Mesh_Wgpu,Part_Wgpu,MaterialSetup,MeshGpuCache,TransparentQueue}.cpp`;
  - the gfx TUs under `../milo-native-engine/src/gfx/`.
- **DC3 plugs into the engine through one hook.** `HamRenderHook` in
  `native/src/dc3_render_hook.cpp` is a `GameRenderHook`
  (`../milo-native-engine/src/platform/GameRenderHook.h`).
- **dc3-web uses the same engine copies.** It has linked `milo-engine` since
  `2a51acd7e`; see the comment block above `set(DC3_WEB_CORE_SOURCES` in
  `native/CMakeLists.txt`.
- **dc3's renderer copies are gone.** `native/src/gfx/*` and
  `native/src/platform/{Rnd,Mesh,Tex,Part}_Wgpu.cpp` were deleted on
  2026-11-13 (H-1), with the source lists that named them
  (`DC3_NATIVE_SOURCES`, `DC3_GFX_CORE_SOURCES`, `DC3_GFX_SOURCES`,
  `DC3_WEB_GFX_SOURCES`) and `wgpu-window-test`, the only target that compiled
  any of them. No linked binary's defined-symbol set changed. Their stale
  headers had been shadowing the engine's for every dc3 TU (`-I native/src`
  precedes `-I <engine>/src`).
- **Some dc3 copies are still dead. Do not edit them expecting an effect.**
  `native/src/platform/MaterialSetup.cpp`, `MeshGpuCache.cpp`,
  `TransparentQueue.cpp` and the rest of
  `list(REMOVE_ITEM DC3_NATIVE_CORE_SOURCES_ENGINE ...)` in
  `native/CMakeLists.txt` are taken out of every engine-linking target.
- **One engine path still names the deleted copy.**
  `PipelineManager::ReloadShaders()` (`../milo-native-engine/src/gfx/PipelineManager.cpp`)
  looks for `native/src/gfx/standard_wgsl.inc` on disk. It has no caller today;
  the file no longer exists, so if it is wired up it finds nothing instead of
  loading a stale copy. The shader that
  actually runs is the engine's copy, `#include`d into `PipelineManager.cpp`.

## Shading model

`BaseMaterial` (`src/system/rndobj/BaseMaterial.h`) defines the Xbox-era
material inputs: multi-texture Blinn-Phong with rim, emissive, environment cube
and refraction. There are no metalness or roughness inputs, so it is not PBR.

The native WGSL is **authored, not extracted**. The header of
`../milo-native-engine/src/gfx/standard_wgsl.inc` says it is "written to be
readable as authored WGSL rather than as a direct extraction artifact". So the
formulas below describe the native port, not retail.

| `mShaderVariation` | Value | Native lighting function |
|---|---|---|
| `kShaderVariationNone` | 0 | `computeStandardLight`: Lambert diffuse, Blinn-Phong specular. With a specular map, the exponent is `specularPower * map.a` with a floor of 0.1. |
| `kShaderVariationSkin` | 1 | `computeSkinLight`: half-Lambert squared plus a warm shadow tint, and a second specular lobe from `mSpecular2RGB` (alpha = exponent). |
| `kShaderVariationHair` | 2 | `computeHairLight`: wrapped diffuse plus an anisotropic strand specular along the vertex tangent, exponent `max(mAnisotropy, 1)`. |
| `kShaderVariationWorldProjection` | 3 | Detected by the shader (`isWorldProjection`). |

Lights, from `SceneUniforms` in `../milo-native-engine/src/gfx/UniformStructs.h`:
- up to 4 directional and 4 point lights, plus ambient;
- one projected light: the first textured `kFakeSpot` in `RndEnviron::LightsReal()`,
  filled in `Rnd_Wgpu.cpp`;
- one shadow-mapped light.

## Material inputs (`BaseMaterial`)

Offsets are the header's own comments in `src/system/rndobj/BaseMaterial.h`.

| Slot | Member (offset) | Notes |
|---|---|---|
| Diffuse | `mDiffuseTex` (0x40) | Modulated by `mColor` (0x2c) |
| Diffuse 2 | `mDiffuseTex2` (0x54) | |
| Emissive | `mEmissiveMap` (0xe4) | Scaled by `mEmissiveMultiplier` (0xe0) |
| Normal | `mNormalMap` (0xf8) | Strength damped by `mDeNormal` (0x10c) |
| Detail normal | `mNormDetailMap` (0x110) | Uses `mNormDetailTiling` (0x124) and `mNormDetailStrength` (0x128) |
| Specular | `mSpecularMap` (0x14c) | RGB = colour, A = gloss. `mSpecularRGB` (0x12c), `mSpecular2RGB` (0x13c) |
| Rim | `mRimMap` (0x174) | `mRimRGB` (0x164; A = rim power), `mRimLightUnder` (0x188) |
| Environment | `mEnvironMap` (0x18c, `RndCubeTex`) | `mEnvironMapFalloff` (0x1a0), `mEnvironMapSpecMask` (0x1a1) |
| Refraction | `mRefractNormalMap` (0x1a8) | `mRefractEnabled` (0x1a2), `mRefractStrength` (0x1a4) |

Other state that feeds pipeline selection:
- `mBlend` (0xb4), `mZMode` (0xb8), `mCull` (0x1ec);
- `mAlphaCut` (0xbc), `mAlphaWrite` (0xbd), `mAlphaThreshold` (0xc0);
- `mTexWrap` (0x6c), `mTexGen` (0x70);
- `mPerPixelLit` (0xdf), `mPrelit` (0x3d), `mIntensify` (0x68), `mFog` (0xd9);
- `mNextPass` (0xc4), which chains multi-pass materials; `RndMesh::DrawShowing`
  in the engine's `Mesh_Wgpu.cpp` walks `NextPass()`;
- `mBloomMultiplier` (0x1f4).

Other texture holders: `RndLight::mTexture` (0xf4, projected gobo),
`RndLight::mCubeTexture` (0x108), and `RndFur::mFurDetail` (0x70). The engine
has no fur path: `grep -rn RndFur ../milo-native-engine/src/platform` prints
nothing.

### Bind groups (`standard_wgsl.inc`)

| Group | Contents |
|---|---|
| 0 | `SceneUniforms`, shadow depth map + comparison sampler, projected-light texture + sampler |
| 1 | `MaterialUniforms`, then textures and samplers: diffuse, normal, specular, emissive, rim, environ cube, detail normal, refract normal, screen |
| 2 | `ObjectUniforms` |
| 3 | `bones: array<mat4x4f, 40>` |

The CPU-side sizes are pinned by `static_assert` in
`../milo-native-engine/src/gfx/UniformStructs.h`. Read them there rather than
from any document (`grep -n static_assert ../milo-native-engine/src/gfx/UniformStructs.h`).
Per-draw uniforms go through `UniformRingBuffer`, which uses 256-byte
alignment (`kAlignment`) and grows on overflow (`Grow`).

### TexGen (`TexGen` enum, implemented in `standard_wgsl.inc`)

| Mode | Value | Meaning (header comment) |
|---|---|---|
| `kTexGenNone` | 0 | vertex UV unchanged |
| `kTexGenXfm` | 1 | transform UV about centre with the stage xfm |
| `kTexGenSphere` | 2 | sphere map rotating with the camera |
| `kTexGenProjected` | 3 | project from the stage xfm direction in world coords |
| `kTexGenXfmOrigin` | 4 | like Xfm, about the origin |
| `kTexGenEnviron` | 5 | perspective-correct reflection map |

## Pipeline state mapping (`../milo-native-engine/src/gfx/PipelineManager.cpp`)

WebGPU pipelines are immutable, so blend, depth and cull state are baked into
a cached pipeline keyed on material state.

| Blend (`BaseMaterial::Blend`) | Src | Dst | Op |
|---|---|---|---|
| `kBlendDest` 0 | Zero | One | Add |
| `kBlendSrc` 1 | One | Zero | Add |
| `kBlendAdd` 2 | One | One | Add |
| `kBlendSrcAlpha` 3 | SrcAlpha | OneMinusSrcAlpha | Add |
| `kBlendSrcAlphaAdd` 4 | SrcAlpha | One | Add |
| `kBlendSubtract` 5 | One | One | ReverseSubtract |
| `kBlendMultiply` 6 | Dst | Zero | Add |
| `kPreMultAlpha` 7 | One | OneMinusSrcAlpha | Add |
| `kScreen` 8 | OneMinusDst | One | Add |
| `kLighten` 9 | One | One | Max |
| `kDarken` 10 | One | One | Min |

When `SetAlphaBlendMax` is on, alpha for every blend except `kBlendSrc` uses
op Max with factors 1, 1. This reproduces retail's destination-alpha state;
`MapBlend` documents the reasoning.

| ZMode | Depth write | Compare |
|---|---|---|
| `kZModeDisable` 0 | no | Always |
| `kZModeNormal` 1 | yes | Less |
| `kZModeTransparent` 2 | no | LessEqual |
| `kZModeForce` 3 | yes | Always |
| `kZModeDecal` 4 | yes | LessEqual |

Cull: `kCullNone` → None, `kCullRegular` → Back, `kCullBackwards` → Front.

Texture wrap is mapped in `../milo-native-engine/src/platform/MaterialSetup.cpp`:
- `kTexWrapClamp` → ClampToEdge;
- `kTexWrapRepeat` → Repeat;
- `kTexWrapMirror` → MirrorRepeat;
- `kTexBorderBlack` and `kTexBorderWhite` fall through to ClampToEdge. There
  is no border colour.

Alpha-cut materials set `alphaToCoverage`, which only takes effect when
`sampleCount > 1`. Transparent draws are deferred and flushed back to front in
`../milo-native-engine/src/platform/TransparentQueue.cpp`.

## Textures

### `RndTex::Type` (`src/system/rndobj/Tex.h`)

| Value | Type |
|---|---|
| 0x1 | `kRegular` |
| 0x2 | `kRendered` |
| 0x4 | `kMovie` |
| 0x8 | `kBackBuffer` |
| 0x18 | `kFrontBuffer` |
| 0x22 | `kRenderedNoZ` |
| 0x42 | `kShadowMap` |
| 0xA2 | `kDepthVolumeMap` |
| 0x122 | `kDensityMap` |
| 0x200 | `kScratch` |
| 0x1000 | `kDeviceTexture` |
| 0x2000 | `kRegularLinear` |

`IsRenderTarget()` returns `mType & kRendered`.

### `RndBitmap::mOrder` (`src/system/rndobj/Bitmap.h`; decoded by `../milo-native-engine/src/gfx/TextureConvert.cpp`)

| Bits | Meaning |
|---|---|
| `& 0x38` (`kDXT_MASK`) | Compression. 0x08 DXT1/BC1, 0x10 DXT3/BC2, 0x18 DXT5/BC3, 0x20 DXN/BC5 (values from `TextureConvert.h`) |
| `& 4` | Milo custom tiling, undone by `UntileMilo` |
| `& 1` | Channel order for uncompressed 32-bit data. Set means RGBA; clear means BGRA, swapped on upload |
| `& 0x40` or `& 0x80` | "white" (R=G=B=255), per the header comment |

Other key fields: `mWidth` (0x4), `mHeight` (0x6), `mRowBytes` (0x8),
`mBpp` (0xa), `mOrder` (0xc), `mPixels` (0x10), `mPalette` (0x14), and
`mMip` (0x1c, the next mip in a linked list).

### Xbox 360 data quirks (both handled in `TextureConvert.cpp`)

- **DXT block data is not endian-neutral.** Every DXT/BC payload is 16-bit
  byte-swapped (`ByteSwapDXT`, the k8in16 pattern) before upload. The decomp
  compensates for the same swap in `DecodeDxtColor` (`src/system/rndobj/Bitmap.cpp`).
- **Milo tiling is not the Xbox GPU's Morton tiling.** When `mOrder & 4` is
  set, `RndBitmap::PixelOffset()` (`Bitmap.cpp`) remaps pixels through custom
  lookup tables. GPU hardware tiling is invisible to game code, because the
  D3D runtime handles it.

### Upload path

`MapBitmapFormat` chooses BC1/BC2/BC3 when the adapter exposes
`TextureCompressionBC` (`GpuDevice::HasBCCompression`). Otherwise it decodes on
the CPU to RGBA8. DXN/BC5 is always decoded to RGBA8, because `BC5RGUnorm`
returns B=0 and that breaks the shader's normal decode; the reason is in the
`MapBitmapFormat` comment.

Upload order: untile, then byte-swap, then BC upload or CPU decompress. Palette
formats (8 and 4 bpp) expand through `PixelColor`. Uploads happen lazily, in
`RndTex::PresyncBitmap` (engine `Tex_Wgpu.cpp`), into a side table keyed by
`RndTex*`. `~RndTex` (`src/system/rndobj/Tex.cpp`, `HX_NATIVE` block) calls
`CleanupGpuTex`, so a freed texture's entry is erased.

### Side tables, not subclasses

GPU resources live in `unordered_map`s keyed by the decomp object:
`sTexGpuData` in `Tex_Wgpu.cpp` and the mesh cache in `MeshGpuCache.cpp`.
There are no `WgpuTex`/`WgpuMesh` subclasses. That means:
- objects created by the engine's own loaders need no factory hooks;
- decomp class layouts stay identical to the PPC build.

## Render-to-texture

The engine API is unchanged from retail:
1. `RndCam::SetTargetTex(tex)`.
2. `RndCam::Select()` calls `tex->MakeDrawTarget()`.
3. Draw.
4. `tex->FinishDrawTarget()`.

The native implementation is in the engine's `Tex_Wgpu.cpp` and `Rnd_Wgpu.cpp`:
- `EnsureRenderTargetData` allocates on first use. Zero-sized targets default
  to 256×256, and a new target is cleared to opaque black, because WebGPU
  leaves initial contents undefined.
- `GetGpuTexView`, `GetGpuTexDepthView` and `IsGpuTexRenderable` are the accessors.
- `WgpuRnd::SelectRenderTarget`, `BeginTexturePass` and `FinishRenderTarget`
  switch passes. The texture pass clears to transparent black, so impostor
  RTT can alpha-cut.
- Colour format: `RGBA8UnormSrgb`, or `RGBA8Unorm` when the surface is non-sRGB
  (web) or the texture is `kDepthVolumeMap`.
- Depth: `Depth24PlusStencil8` (`TextureConvert::CreateDepthTarget`) for
  `kRendered` types without the 0x20 bit. So `kRenderedNoZ` and
  `kDepthVolumeMap` get no depth.

Limits:
- Compressed (BC) textures lack RenderAttachment usage and are skipped by
  `BeginTexturePass`.
- The engine refuses nested RTT; `RndTexRenderer` logs "Cannot render to texture
  ... while already rendering to texture" (`src/system/rndobj/TexRenderer.cpp`).

Consumers in the decomp:
- `RndTexRenderer` (`src/system/rndobj/TexRenderer.cpp`);
- the WorldCrowd impostors (`src/system/world/Crowd.cpp`: `gImpostorCamera`,
  `gImpostorTex[]`; re-rendered per frame as in the image, but the shipped
  venues put every crowd member in 3D, so none is drawn — see
  [ANIMATION.md](ANIMATION.md#worldcrowd-rendering));
- `src/system/world/SpotlightDrawer_NG.cpp`;
- `src/system/world/Reflection.cpp`;
- `src/system/rndobj/ShadowMap.cpp`.

Movie frames arrive through `UploadRGBAToRndTex` (engine `Tex_Wgpu.cpp`,
declared in `TexGpu.h`), called from `src/system/movie/TexMovie.cpp`. Which of
these consumers render correctly at runtime is not re-verified here.

## Passes in the engine (`../milo-native-engine/src/gfx/`)

| Pass | What it is |
|---|---|
| `ShadowPass` | 1024² `Depth32Float` shadow map (`kShadowMapSize`); static and skinned depth pipelines; `InShadowPass()` drives `DrawMeshShadow` in `Mesh_Wgpu.cpp`. The standard shader samples it with a 3×3 `textureSampleCompare` PCF. |
| `PostProcPass` | Driven by `RndPostProc`. Contrast and brightness use the retail `RndColorXfm::AdjustContrast` formula. Also saturation, levels, posterize, vignette, chromatic aberration + sharpen, flicker (`StepFlicker`), noise, and the bloom composite. On the pinned engine the noise is procedural ±0.5 static per pixel. The image overlays `mNoiseMap` instead (PLAN G-14; the engine patch is not landed). |
| `BloomPass` | Bloom extraction and blur, composited by `PostProcPass`. |
| `DofPass` | Depth of field, ported from retail `NgDOFProc::DoPost`. |
| `SpotBeamPass` | Volumetric spotlight beams, ported from retail `NgSpotlightDrawer`. |
| `PointTestPass` | Flare occlusion queries, ported from retail `DxRnd::DoPointTests`. |
| `TexBlendPass` | `RndTexBlender::DrawShowing`. |
| `DisplayRamp` | Display gamma ramp on the finished frame. |
| `DrawRect2D` | Screen-space rects. |

`ShippedWgsl.h` / `ShippedWgsl_DC3.cpp` list every WGSL module the dc3 flavor
compiles, so a test can validate all of them against a real device.

`RndPostProc` has members that `PostProcPass.cpp` does not consume; a
case-insensitive grep for kaleido, gradient, trail, refract, motion blur and
hall-of-time in that file returns 0. Those are kaleidoscope
(`mKaleidoscope*`), trails (`mTrail*`), motion blur (`mMotionBlur*`),
gradient map (`mGradientMap*`), full-screen refraction (`mRefract*`) and
hall-of-time (`mHallOfTime*`).

## Matrix convention

Milo is row-major with row vectors, like D3D. The engine copies the row-major
`float[16]` straight into WGSL `mat4x4f`, which stores column-major. WGSL then
sees the transpose, so `VP * pos` in WGSL equals D3D's `mul(pos, VP)`.

The view matrix comes from the engine's own `GetViewProjectXfms` through
`rndshape::CamViewProjectXfms`. That includes the Milo Y-forward to Z-forward
flip. See `CamSceneMatrices` in the engine's `Rnd_Wgpu.cpp`. When a viewer has
set `mViewProjMatrix` explicitly (anything other than identity), that matrix
is used as-is.

## Dawn/WebGPU gotchas still in the code

- `#undef Success` after the X11 headers (`../milo-native-engine/src/gfx/GpuDevice.cpp`).
- Texture readback rows are padded to 256 bytes (`alignedBytesPerRow` in
  `GpuDevice.cpp`).
- Current Dawn names are `TexelCopyTextureInfo` and `TexelCopyBufferLayout`.
  `depthWriteEnabled` is a `wgpu::OptionalBool`.

## UI render path, and which side to blame

This is the shared-engine chain that decides what a UI frame contains:
1. `HamUI::Draw` (`src/lazer/meta_ham/HamUI.cpp`)
2. `UIManager::Draw` (`src/system/ui/UI.cpp`)
3. `UIScreen::Draw` (`UIScreen.cpp`)
4. `UIPanel::Draw` (`UIPanel.cpp`)
5. `PanelDir::DrawShowing` (`PanelDir.cpp`)
6. `RndDir::DrawShowing` (`src/system/rndobj/Dir.cpp`)
7. Panel drawables:
   - `UIList::DrawShowing`;
   - `UIListDir::BuildDrawState`, `SetElementPos` and `DrawWidgets`;
   - `HamNavList::DrawShowing` (`src/system/hamobj/`);
   - `UILabel::DrawShowing`;
   - `RndText::UpdateText` and `RndText::DrawShowing` (`src/system/rndobj/Text.cpp`).

Shell chrome is drawn by `HelpBarPanel::{Draw,EnterControllerMode,SyncToPanel}`
and `LetterboxPanel::{Draw,EnterBlacklightMode,ExitBlacklightMode}`
(`src/lazer/meta_ham/`). `ShellInput::{SyncToCurrentScreen,EnterControllerMode}`
chooses the shell state.

Native code only submits the result. Submission goes through
`RndMesh::DrawShowing` (engine `Mesh_Wgpu.cpp`),
`WgpuRnd::EnsureSceneUniformsCurrent` (engine `Rnd_Wgpu.cpp`) and the
transparent and text queues (engine `TransparentQueue.cpp`, `FlushTextDraws`).

**Triage rule.**
- Wrong placement, highlight, list spacing, missing or misframed shell chrome,
  or text in the wrong place: suspect the shared-engine UI code above first.
- Wrong blend, transparency order, camera uniforms at draw time, or correct text
  meshes drawn with the wrong GPU state: suspect the native backend.

Check a function's current match state with `run_objdiff` rather than
trusting a table.

## Milo archive container (`src/system/utl/ChunkStream.{h,cpp}`)

The header is `ChunkStream::ChunkInfo`:
- `mID` (0x0);
- `mChunkInfoSize` (0x4, default 0x810);
- `mNumChunks` (0x8);
- `mMaxChunkSize` (0xC);
- `mChunks[512]` (0x10), the per-block sizes.

| `mID` | Meaning |
|---|---|
| `0xCABEDEAF` | uncompressed |
| `0xCBBEDEAF` (`CHUNKSTREAM_Z_ID`) | compressed |
| `0xCCBEDEAF` (`CHUNKSTREAM_Z_ID2`) | compressed; payload read from offset +10 |
| `0xCDBEDEAF` (`CHUNKSTREAM_Z_ID3`) | compressed; this is what a compressed `ChunkInfo` defaults to on write |

See `ChunkStream::DecompressChunk` for the variants. `DirLoader` writes 0x20000
blocks (0x10000 on Wii). The "MILO_A..D" names are external-tool convention.

## External references

All of these are under `~/code/milohax/milo-engine-libs/harmonix-repos/` and
are used for hints, not ground truth:
- MiloEditor: `MiloLib/Assets/Rnd/RndBitmap.cs` has the DXT byte-swap.
- pikaxe: `core/pikaxe/src/texture/dxt.rs`.
- LibForge: `LibForge/LibForge/Texture/TextureConverter.cs`.
- PyMilo: `MiloContainer.py`.
- Boomy: DC3-specific assets.
- `milo-rnd-library/dc3/`: pre-extracted .milo files.

The index of those tools is `~/code/milohax/milo-engine-libs/INDEX.md`. The
decomp itself (`src/system/rndobj/`) is authoritative for offsets and enums.

For GPU capture and debugging tools see [TOOLS.md](../TOOLS.md).

## Sources

Lifted, re-verified and trimmed from these archived documents:
- [RENDERING_SYSTEM.md](../../archive/2026-11-13-native-doc-audit/native/RENDERING_SYSTEM.md)
- [RENDER_TO_TEXTURE.md](../../archive/2026-11-13-native-doc-audit/native/RENDER_TO_TEXTURE.md)
- [UI_RENDER_DECOMP_GUIDE.md](../../archive/2026-11-13-native-doc-audit/native/UI_RENDER_DECOMP_GUIDE.md)
- [GRAPHICS_SYSTEM_DESIGN.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/GRAPHICS_SYSTEM_DESIGN.md)
- [MATERIAL_RENDERING.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/MATERIAL_RENDERING.md)
- [SHADOW_MAPPING.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/SHADOW_MAPPING.md)
