# Graphics Subsystem: System Design

**Status**: Draft v5 — Tier 1.5 rendering with full material pipeline (specular, emissive, rim, intensify, multi-light)
**Target**: ~90% complete design; open items reduced from 8 to 4 after code analysis
**Renderer**: WebGPU via Dawn (`../dawn`)

## Design Principles

1. **WgpuRnd is a backend, not a new abstraction** — Milo already has its rendering
   abstraction (Rnd → NgRnd). We implement it, not wrap it in another layer.
2. **Shared source files** — `native/src/gfx/` utilities consumed by both the Milo
   Viewer and `WgpuRnd`. Not a formal library — just shared code.
3. **Staged fidelity** — all interfaces designed upfront, implementation in three tiers
4. **Both texture paths** — runtime Xbox 360 byte-swap + untile for dev, offline converter for shipping
5. **WGSL shaders** — written directly, variant generation via C++ string builder
6. **Minimal modules** — 4 shared utilities (GpuDevice, TextureConvert, PipelineManager,
   VertexFormats) plus the platform classes (WgpuRnd, WgpuTex, WgpuMesh, WgpuShaderMgr).
   No handle pools, no frame graph, no unnecessary indirection.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Consumers                                 │
│  ┌──────────────────────┐  ┌──────────────────────────────┐ │
│  │   Milo Viewer App    │  │   WgpuRnd (Engine Subclass)  │ │
│  │   (standalone .milo  │  │   (Rnd → NgRnd → WgpuRnd)   │ │
│  │    file renderer)    │  │   WgpuTex, WgpuMesh,         │ │
│  │                      │  │   WgpuShaderMgr              │ │
│  └──────────┬───────────┘  └──────────────┬───────────────┘ │
│             │                              │                 │
│             └──────────┬───────────────────┘                 │
│                        ▼                                     │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │          Shared Utilities (native/src/gfx/)             │ │
│  │                                                         │ │
│  │  ┌───────────┐ ┌──────────────┐ ┌───────────────────┐  │ │
│  │  │ GpuDevice │ │  Pipeline    │ │  TextureConvert   │  │ │
│  │  │           │ │  Manager     │ │  (format convert,  │  │ │
│  │  │           │ │  (shaders +  │ │   byte-swap,       │  │ │
│  │  │           │ │   cache)     │ │   sampler cache)   │  │ │
│  │  └───────────┘ └──────────────┘ └───────────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
│                        │                                     │
│                        ▼                                     │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │          WebGPU C++ API (webgpu_cpp.h / Dawn)           │ │
│  └─────────────────────────────────────────────────────────┘ │
│                        │                                     │
│          ┌─────────────┼─────────────┐                       │
│          ▼             ▼             ▼                       │
│       Vulkan        Metal         D3D12                      │
│      (Linux)       (macOS)      (Windows)                    │
└─────────────────────────────────────────────────────────────┘
```

---

## Shared Utilities (`native/src/gfx/`)

These are plain source files compiled into both the viewer and engine targets.
No library target, no handle pools. GPU resources are stored in side tables
(`unordered_map<RndTex*, GpuTexData>` and `unordered_map<RndMesh*, GpuMeshData>`)
rather than on the Milo objects themselves — this avoids modifying decomp class
layouts or needing factory registration for subclasses.

### 1. GpuDevice (`gfx/GpuDevice.h`)

Owns the WebGPU device lifecycle. Thin wrapper around Dawn initialization.

```cpp
class GpuDevice {
public:
    bool Init(const GpuDeviceDesc& desc);  // headless or windowed
    void Shutdown();

    // Accessors
    wgpu::Device& Device();
    wgpu::Queue& Queue();
    wgpu::Instance& Instance();

    // Surface management (windowed mode)
    bool CreateSurface(void* nativeWindow, int width, int height);
    void ResizeSurface(int width, int height);
    wgpu::TextureFormat SurfaceFormat() const;

    // Frame lifecycle
    wgpu::TextureView AcquireNextFrame();
    void PresentFrame();

    // Capability queries
    bool HasBCCompression() const;     // texture-compression-bc feature
    bool HasOcclusionQueries() const;
    wgpu::Limits DeviceLimits() const;

    // Sampler cache (shared across all textures)
    wgpu::Sampler GetSampler(const SamplerDesc& desc);

private:
    wgpu::Instance mInstance;
    wgpu::Adapter mAdapter;
    wgpu::Device mDevice;
    wgpu::Queue mQueue;
    wgpu::Surface mSurface;
    wgpu::TextureFormat mSurfaceFormat;
    bool mHasBCCompression;
    std::unordered_map<SamplerDesc, wgpu::Sampler> mSamplerCache;
};
```

**Design notes**:
- `GpuDeviceDesc` specifies headless (offscreen texture) vs windowed (native surface)
- `Init()` requests `texture-compression-bc` optional feature. If unavailable (rare on
  desktop, common on mobile/web), `TextureConvert` falls back to CPU decompression.
- Single device instance shared across all rendering
- Sampler cache lives here since many textures share the same sampler settings

### 2. TextureConvert (`gfx/TextureConvert.h`)

Stateless utility functions for format conversion and GPU texture creation.
No handle pools — returns `wgpu::Texture` directly; the calling `WgpuTex` object
owns the result.

```cpp
namespace TextureConvert {
    // Create GPU texture from Milo bitmap data
    wgpu::Texture CreateFromBitmap(GpuDevice& gpu, const RndBitmap& bmp, int numMips);

    // Create from pre-converted file (PNG, KTX2, DDS)
    wgpu::Texture CreateFromFile(GpuDevice& gpu, const char* path);

    // Render targets
    wgpu::Texture CreateRenderTarget(GpuDevice& gpu, int w, int h, wgpu::TextureFormat fmt);
    wgpu::Texture CreateDepthTarget(GpuDevice& gpu, int w, int h);

    // Cube maps
    wgpu::Texture CreateCubeMap(GpuDevice& gpu, const RndBitmap faces[6]);

    // Format mapping
    wgpu::TextureFormat MapBitmapFormat(const RndBitmap& bmp, bool hasBCSupport);

    // Xbox 360 data conversion (in-place where possible)
    void ByteSwapDXT(u8* data, size_t size);          // 16-bit byte-swap for Xbox BE DXT
    void UntileMilo(const RndBitmap& bmp, u8* dst);   // Milo's custom tiling (mOrder & 4)
    void DecompressDXT(const u8* src, u8* dst, int w, int h, DXTFormat fmt); // BC fallback
    void ConvertChannelOrder(u8* data, int w, int h, int bpp, u8 order); // BGRA↔RGBA
}
```

**Supported source formats**:

| Milo Format | WebGPU Format | Conversion Required |
|-------------|---------------|---------------------|
| DXT1 (BC1) | `BC1RGBAUnorm` | 16-bit byte-swap (Xbox BE) |
| DXT3 (BC2) | `BC2RGBAUnorm` | 16-bit byte-swap (Xbox BE) |
| DXT5 (BC3) | `BC3RGBAUnorm` | 16-bit byte-swap (Xbox BE) |
| DXN (BC5) | `BC5RGUnorm` | 16-bit byte-swap (Xbox BE) |
| 32-bit, `order & 1` | `RGBA8Unorm` | None (already RGBA) |
| 32-bit, `!(order & 1)` | `BGRA8Unorm` or swizzle | BGRA→RGBA if no BGRA support |
| RGB 24-bit | `RGBA8Unorm` | Expand to 32-bit, add alpha=0xFF |
| A1R5G5B5 16-bit | `RGBA8Unorm` | Expand to 32-bit |
| 8-bit indexed | `RGBA8Unorm` | Palette lookup on CPU |

**If `texture-compression-bc` is unavailable** (mobile, some web): `DecompressDXT()`
decompresses to RGBA8 on CPU before upload. This is the fallback path.

**Xbox 360 DXT endianness** (CRITICAL — confirmed by MiloEditor, LibForge, AND the
decomp's own `DecodeDxtColor`):

> **DXT block data is NOT endian-neutral.** Xbox 360 stores 16-bit color endpoints
> in big-endian byte order. All DXT/BC data from Xbox 360 archives must be 16-bit
> byte-swapped before uploading to a little-endian GPU.
>
> The swap pattern is `k8in16`: for every 2 adjacent bytes `[b0, b1]`, output
> `[b1, b0]`. This matches the Xbox 360 GPU's texture fetch endian mode and is
> confirmed by MiloEditor's `RndBitmap.cs` (lines 186-199) and the decomp's
> `DecodeDxtColor` which compensates for swapped index bytes.

**Milo custom tiling** (distinct from Xbox 360 GPU hardware tiling):

> When `mOrder & 4`, Milo applies its own pixel rearrangement using lookup tables
> in `RndBitmap::PixelOffset()` (`Bitmap.cpp` lines 288-380). This is NOT the
> Xbox 360 GPU's Morton-order hardware tiling (which is transparent to game code —
> the D3D runtime handles it). The Milo tiling uses custom `bytes02`/`bytes13`
> lookup tables for coordinate-to-offset mapping within tiles.

**`RndBitmap::mOrder` bitfield** (from decomp `Bitmap.h` — the key to format detection):

```
Bits [0]     : Channel order   — 0 = BGRA, 1 = RGBA
Bits [3:5]   : DXT compression — 0x00 = none, 0x08 = DXT1(BC1), others = DXT3/5
Bit  [2]     : Milo custom tiling — (mOrder & 4) triggers PixelOffset() lookup tables
Bits [6:7]   : White flag      — (mOrder & 0x40) or (mOrder & 0x80) = R=G=B=255
```

The converter checks these in order: DXT type → tiling → channel order → bpp fallback.

**Key RndBitmap fields** (from `Bitmap.h`, size 0x20):
- `mWidth` (u16), `mHeight` (u16), `mBpp` (u8: 8/16/24/32), `mOrder` (u32)
- `mRowBytes` (u16) — may include alignment padding
- `mPixels` (u8\*), `mPalette` (u8\*), `mMip` (RndBitmap\*) — mipmap linked list
- `PaletteBytes()` = `(mBpp <= 8 && !DXT) ? (1 << mBpp) * 4 : 0`
- `PixelBytes()` = `mRowBytes * mHeight`
- DXT row stride: `(mOrder & 0x38) ? mRowBytes * 4 : mRowBytes`

**Sampler cache** (on GpuDevice): WebGPU samplers are immutable. Cached by (wrap,
filter, mipLodBias) tuple.

| Milo TexWrap | WebGPU AddressMode | Notes |
|-------------|-------------------|-------|
| kTexWrapClamp | ClampToEdge | |
| kTexWrapRepeat | Repeat | |
| kTexBorderBlack | Dawn: ClampToBorder (native extension) | Web fallback: ClampToEdge + shader discard |
| kTexBorderWhite | Dawn: ClampToBorder (native extension) | Web fallback: ClampToEdge + shader discard |
| kTexWrapMirror | MirrorRepeat | |

### 3. Vertex Formats (`gfx/VertexFormats.h`)

No MeshManager module — GPU vertex/index buffers are stored in a side table keyed
by `RndMesh*`. This header defines the shared vertex layout descriptors and CPU-side
unpack functions.

```cpp
namespace VertexFormats {
    // WebGPU vertex buffer layouts (shared across all meshes of same type)
    const wgpu::VertexBufferLayout& StaticLayout();   // unpacked from DxMesh 36-byte
    const wgpu::VertexBufferLayout& SkinnedLayout();  // + bone weights/indices
    const wgpu::VertexBufferLayout& MutableLayout();  // 88-byte full-precision

    // CPU unpack: convert DxMesh packed vertex → GPU vertex buffer format
    // DEC4N (10-10-10-2 signed) has no WebGPU equivalent, so we unpack to Float32x3
    void UnpackStaticVertices(const void* dxVerts, int count, void* gpuVerts);
    void UnpackSkinnedVertices(const void* dxVerts, int count, void* gpuVerts);
}
```

**Vertex format mapping** (static mesh — after CPU unpack):

Xbox 360 DxMesh uses 36-byte packed vertices with `DEC4N` (10-10-10-2 signed
normalized) for normals/tangents. WebGPU has no `snorm10-10-10-2` format (only
unsigned `unorm10-10-10-2` exists as of Chrome 119). We unpack to float on CPU.

| Attribute | Source (DxMesh) | GPU Format | GPU Offset | GPU Size |
|-----------|----------------|------------|------------|----------|
| Position | Float32x3 (12B) | Float32x3 | 0 | 12 |
| Normal | DEC4N (4B, packed) | Float32x3 (unpacked) | 12 | 12 |
| Color | D3DCOLOR (4B) | Unorm8x4 | 24 | 4 |
| UV | Float16x2 (4B) | Float16x2 | 28 | 4 |
| Tangent | DEC4N (4B, packed) | Float32x3 (unpacked) | 32 | 12 |
| BoneWeights | UDEC4N (4B, packed) | Float32x4 (unpacked) | 44 | 16 |
| BoneIndices | UByte4 (4B) | Uint8x4 | 60 | 4 |

**GPU vertex stride**: 64 bytes (vs 36 bytes DxMesh source — expansion from unpacking
packed normals/tangents/weights). This is a one-time CPU cost at mesh load.

**Vertex data path**: The engine has two vertex representations:
- `RndMesh::Vert` (0x60 = 96 bytes): high-level with full float positions, normals,
  tangents, bone weights (Vector4), bone indices (short[4]), vertex color, UV.
  This is the CPU-side format used by `mVerts` vector.
- `DxMesh` packed format (36 bytes): GPU-optimized with DEC4N (10-10-10-2 signed)
  for normals/tangents, UDEC4N for bone weights, Float16x2 for UV.
  The Xbox 360 uploads this directly; we unpack it to our 64-byte GPU format.

For the Milo Viewer (loading .milo files directly), vertex data comes as `Vert`
structs — no DEC4N unpacking needed, just repack to our GPU layout. For the engine
path, data comes from DxMesh packed format and must be unpacked.

**Skinning limits**: Max 4 bones per vertex. DC3 characters typically use 40-80 bones
per skeleton. Bone matrices uploaded as storage buffer (Group 2).

### 4. PipelineManager (`gfx/PipelineManager.h`)

Combines shader compilation and pipeline caching. WebGPU render pipelines are
immutable — all render state baked in at creation. Shaders are useless without
pipelines, so they live together.

```cpp
struct PipelineKey {
    u64 shaderOptions;       // ShaderType + variant flags
    BlendMode blend;         // 11 Milo blend modes
    ZMode zMode;             // 5 Milo z-buffer modes
    CullMode cull;           // 3 Milo cull modes
    bool alphaCut;           // Alpha test discard
    bool alphaWrite;         // Write alpha channel
    StencilMode stencil;     // 3 Milo stencil modes
    VertexLayoutType layout; // static/skinned/mutable
    wgpu::TextureFormat targetFormat;

    bool operator==(const PipelineKey&) const = default;
    size_t Hash() const;
};

class PipelineManager {
public:
    void Init(GpuDevice* device);

    // Get or create pipeline (compiles shader variant if needed)
    wgpu::RenderPipeline GetPipeline(const PipelineKey& key);

    // Bind group layouts (shared across all pipelines)
    wgpu::BindGroupLayout SceneLayout() const;     // Group 0
    wgpu::BindGroupLayout MaterialLayout() const;  // Group 1
    wgpu::BindGroupLayout ObjectLayout() const;    // Group 2

    // Stats
    int CachedPipelineCount() const;
    int CachedShaderCount() const;

private:
    // Shader compilation
    wgpu::ShaderModule GetOrCompileShader(ShaderType type, u64 options);
    std::string GenerateStandardWGSL(u64 options);
    std::string GenerateParticleWGSL(u64 options);

    // Pipeline creation
    wgpu::RenderPipeline CreatePipeline(const PipelineKey& key);
    wgpu::BlendState MapBlend(BlendMode mode);
    wgpu::DepthStencilState MapDepthStencil(ZMode z, StencilMode s);
    wgpu::PrimitiveState MapPrimitive(CullMode cull);

    GpuDevice* mDevice;
    std::unordered_map<u64, wgpu::ShaderModule> mShaderCache;
    std::unordered_map<PipelineKey, wgpu::RenderPipeline, PipelineKeyHash> mPipelineCache;
    wgpu::BindGroupLayout mLayouts[3];
};
```

**Shader inventory** (11 unique implementations mapping to 38 types):

| Implementation | Types | Complexity | Variants? |
|---------------|-------|------------|-----------|
| `simple.wgsl` | bloom, blur, downsample(x3), drawrect, error, line(x2), movie, shadowmap, postproc_error, playerdepth(x3), depthbuffer_3d, yuv(x2), greenscreen(x2), crew_photo, twirl, killalpha | Low | Entry point selection only |
| `standard.wgsl` | standard, standard_bb, allwhite | **High** | ~20 relevant variant bits |
| `particles.wgsl` | particles | Medium | Skinned/billboard |
| `multimesh.wgsl` | multimesh, multimesh_bb | Medium | Billboard flag |
| `fur.wgsl` | fur | Medium | Shell layers, wind |
| `postprocess.wgsl` | postprocess | Medium | Bloom/blur/vignette flags |
| `depthvolume.wgsl` | depthvolume | Low | None |
| `synctrack.wgsl` | sync_track, sync_track_charge | Low | Charge effect flag |
| `unwrapuv.wgsl` | unwrapuv | Low | None |
| `velocity.wgsl` | velocity_object | Low | None |
| `velocity_cam.wgsl` | velocity_camera | Low | None |

**Standard shader variant generation** — C++ string builder concatenating WGSL snippets:

```cpp
std::string PipelineManager::GenerateStandardWGSL(u64 opts) {
    std::string wgsl;
    wgsl += kStandardUniforms;
    if (opts & OPT_DIFFUSE_MAP)    wgsl += kDiffuseTexBindings;
    if (opts & OPT_NORMAL_MAP)     wgsl += kNormalMapBindings;
    if (opts & OPT_SKINNED)        wgsl += kBoneStorageBindings;

    wgsl += kVertexShaderHeader;
    if (opts & OPT_SKINNED)        wgsl += kSkinningCode;
    if (opts & OPT_BILLBOARD)      wgsl += kBillboardCode;
    wgsl += kVertexShaderFooter;

    wgsl += kFragmentShaderHeader;
    if (opts & OPT_PER_PIXEL_LIT)  wgsl += kPerPixelLighting;
    else                            wgsl += kPerVertexLighting;
    if (opts & OPT_FOG)            wgsl += kFogCode;
    if (opts & OPT_TONE_MAPPING)   wgsl += kToneMappingCode;
    wgsl += kFragmentShaderFooter;
    return wgsl;
}
```

Each `k*` is a C++ raw string literal containing a WGSL snippet. ~20 relevant
variant bits (SKINNED, DIFFUSE_MAP, NORMAL_MAP, SPECULAR_MAP, ENVIRON_MAP,
PER_PIXEL_LIGHTING, HAS_REAL_LIGHTS, FOG, PRELIT, BILLBOARD, RIMLIGHT,
SHADOW_BUFFER, TONE_MAPPING, SOFT_DEPTH_BLEND, ENABLE_AO, COLOR_MOD(2-bit),
NUM_POINT(2-bit), NUM_PROJ(2-bit), FADE_OUT(2-bit)).

**Blend mode mapping**:

| Milo Blend | WebGPU Src Factor | WebGPU Dst Factor | Op |
|-----------|------------------|------------------|-----|
| kBlendDest | Zero | One | Add |
| kBlendSrc | One | Zero | Add |
| kBlendAdd | One | One | Add |
| kBlendSrcAlpha | SrcAlpha | OneMinusSrcAlpha | Add |
| kBlendSrcAlphaAdd | SrcAlpha | One | Add |
| kBlendSubtract | One | One | ReverseSubtract |
| kBlendMultiply | DstColor | Zero | Add |
| kPreMultAlpha | One | OneMinusSrcAlpha | Add |
| kScreen | OneMinusDstColor | One | Add |
| kLighten | One | One | Max |
| kDarken | One | One | Min |

**Depth/stencil mapping**:

| Milo ZMode | DepthWrite | DepthCompare |
|-----------|------------|-------------|
| kZModeDisable | false | Always |
| kZModeNormal | true | Less |
| kZModeTransparent | false | LessEqual |
| kZModeForce | true | Always |
| kZModeDecal | true | LessEqual |

### Uniform Buffer Layout

Uniform management is inline in WgpuRnd (not a separate module). The bind group
layout is defined by `PipelineManager` and shared across all pipelines.

**Ring buffer pattern**: Per-draw uniforms (material and object) use a
`UniformRingBuffer` — a fixed-size GPU buffer (64KB each) with a write offset that
advances per draw call. Each `Write()` returns the offset used for the bind group's
`buffer.offset`. The buffer wraps to 0 when full (safe for single-frame use since
submissions complete before the next frame). This avoids WebGPU's restriction that
`WriteBuffer` + `SetBindGroup` ordering within a frame can produce stale reads if
using a single offset.

**Dynamic offset alignment**: `minUniformBufferOffsetAlignment` is 256 bytes on
most hardware. The ring buffer pads all allocations to 256-byte boundaries.

```cpp
// Bind group layout:
//   Group 0: Per-frame (scene) — camera, environment, fog, lights
//   Group 1: Per-material — color, texture, sampler
//   Group 2: Per-object — world transform
```

#### Tier 1.5 (Current Implementation — `Rnd_Wgpu.h`)

```cpp
struct SceneUniforms {           // Group 0, 336 bytes — updated once per frame
    float viewProj[16];          // camera view-projection matrix
    float view[16];              // camera view matrix (inverse world transform)
    float cameraPos[3];          // world-space camera position (for specular + rim)
    float _pad0;
    float fogColor[3];           // from RndEnviron::FogColor()
    float fogStart;
    float fogEnd;
    float fogEnabled;
    float _pad1[2];
    float lightDirs[4][4];       // up to 4 directional light directions (from RndEnviron lights)
    float lightColors[4][4];     // up to 4 directional light colors
    float ambientColor[4];       // from RndEnviron::AmbientColor()
    float numLights;             // active light count
    float _padN[3];
};
static_assert(sizeof(SceneUniforms) == 336);

struct MaterialUniforms {        // Group 1, 80 bytes — written per material via ring buffer
    float color[4];              // mColor (RGBA) from RndMat::GetColor()
    float alphaThreshold;        // mAlphaThreshold / 255.0 (0 if !alphaCut)
    float useTexture;            // 1.0 if diffuse texture bound, 0.0 otherwise
    float specularPower;         // from mSpecularRGB.alpha
    float emissiveMultiplier;    // from BaseMaterial::GetEmissiveMultiplier()
    float specularColor[4];      // from BaseMaterial::GetSpecularRGB()
    float rimColor[4];           // .rgb = color, .a = power from BaseMaterial::GetRimRGB()
    float intensify;             // 2.0 if mIntensify, 1.0 otherwise
    float _pad[3];
};
static_assert(sizeof(MaterialUniforms) == 80);

struct ObjectUniforms {          // Group 2, 128 bytes — written per draw via ring buffer
    float world[16];             // world transform (row-major)
    float worldInvTranspose[16]; // = world (correct for orthogonal rotation + translation)
};
```

#### Full Design (Tier 2/3 — planned expansion)

<details>
<summary>Full uniform structs for later tiers</summary>

```cpp
// Light types from RndLight::Type (Lit.h):
//   kPoint=0, kDirectional=1, kFakeSpot=2, kFloorSpot=3, kShadowRef=4
struct LightData {           // 64 bytes
    float color[4];          // mColor (RGBA)
    float position[3];       // world position (for point/spot)
    float range;             // mRange — attenuation radius
    float direction[3];      // world direction (for directional/spot)
    float falloffStart;      // mFalloffStart — inner radius (no attenuation)
    int type;                // 0=point, 1=directional, 2=fakespot, 3=floorspot
    float topRadius;         // mTopRadius (spot cone)
    float botRadius;         // mBotRadius (spot cone)
    float _pad;
};

struct SceneUniforms {       // Group 0, ~832 bytes (well under 64 KiB limit)
    Hmx::Matrix4 viewProj;   // 64 bytes
    Hmx::Matrix4 view;       // 64 bytes
    Vector3 cameraPos;       // 12 bytes
    float _pad0;
    float fogColor[3];
    float fogStart;
    float fogEnd;
    float fogEnabled;
    float _pad1[2];
    LightData lights[8];     // 8 × 64 bytes = 512 bytes
    int numRealLights;       // mLightsReal count
    int numApproxLights;     // mLightsApprox count
    float _padL[2];
    float ambientColor[4];   // mAmbientColor (RGBA)
    float colorXfmM[12];    // 3x3 matrix (3 rows of vec4 for alignment)
    float colorXfmT[4];     // vec3 translation + pad
    float exposure;          // mExposure (HDR)
    float whitePoint;        // mWhitePoint (tone mapping)
    float useColorAdjust;    // mUseColorAdjust
    float useToneMapping;    // mUseToneMapping
    float fadeStart;
    float fadeEnd;
    float fadeEnabled;
    float _pad2;
    float lrFade[4];
    float aoEnabled;
    float aoStrength;
    float _pad3[2];
};

struct MaterialUniforms {    // Group 1, ~256 bytes
    float color[4];
    float specularRGB[4];
    float specular2RGB[4];
    float rimRGB[4];
    float colorMod[3][4];
    float emissiveMultiplier;
    float deNormal;
    float anisotropy;
    float bloomMultiplier;
    int texGen;
    float _pad0[3];
    float texXfm[16];
    float refractStrength;
    int alphaThreshold;
    int shaderVariation;
    float environMapFalloff;
    int flags;
    float _pad1[3];
};

struct ObjectUniforms {      // Group 2, 128 bytes
    Hmx::Matrix4 world;
    Hmx::Matrix4 worldInvT;  // Proper inverse transpose for non-uniform scale
};
```

</details>

### Render Pass Structure

No FrameGraph module — the engine's pass order is fixed and linear.

**Tier 1 (current)**: Single render pass in `BeginDrawing()` → `EndDrawing()`.
Renders directly to the surface texture (no intermediate targets).

```
BeginDrawing():
  1. GLFW PollEvents, check window close
  2. Reset ring buffers for new frame
  3. Acquire surface texture view (windowed) or headless framebuffer
  4. Resize depth texture if window resized
  5. WriteSceneUniforms() from RndCam::Current() + RndEnviron::Current()
  6. CreateCommandEncoder, BeginRenderPass (color clear + depth clear)
  7. SetBindGroup(0, sceneBindGroup)

[Engine draws all drawables — each calls RndMesh::DrawShowing()]

EndDrawing():
  8. EndRenderPass, Finish → Submit
  9. PresentFrame (if windowed)
```

**Tier 2/3 (planned)**:
```
1. Shadow Pass — depth-only render target
2. World Pass — intermediate color+depth targets
3. Post-Process — bloom/blur/tone mapping chain
4. Present — final blit to surface
```

---

## WgpuRnd: Engine Integration

`WgpuRnd` inherits from `NgRnd` and implements the Rnd virtual interface. Uniform
buffer management and render pass management are inline here (not separate modules).

### Tier 1 Implementation (`Rnd_Wgpu.h` / `Rnd_Wgpu.cpp`)

The Tier 1 WgpuRnd implements only the methods needed for basic mesh rendering.
All other Rnd/NgRnd virtuals remain as inherited stubs.

```cpp
class WgpuRnd : public NgRnd {
public:
    // === Lifecycle ===
    void Init() override;              // GpuDevice + PipelineManager + ring buffers + depth + defaults
    void Terminate() override;         // Release all GPU resources

    // === Frame lifecycle ===
    void BeginDrawing() override;      // GLFW poll, acquire frame, write scene uniforms, begin render pass
    void EndDrawing() override;        // End pass, submit, present
    void Clear(unsigned int flags, const Hmx::Color& color) override;  // Store clear color

    // === Accessors (used by Mesh_Wgpu.cpp, Tex_Wgpu.cpp) ===
    GpuDevice& Gpu() { return mGpu; }
    PipelineManager& Pipelines() { return mPipelines; }
    UniformRingBuffer& MaterialRing() { return mMaterialRing; }
    UniformRingBuffer& ObjectRing() { return mObjectRing; }
    wgpu::RenderPassEncoder& CurrentPass() { return mPass; }
    bool IsInPass() const { return mInPass; }
    wgpu::TextureView WhiteTexView() { return mWhiteTexView; }

    // Bind group creation helpers (called per-draw from Mesh_Wgpu.cpp)
    wgpu::BindGroup CreateMaterialBindGroup(uint32_t bufOff, uint32_t bufSize,
                                             wgpu::TextureView& tex, wgpu::Sampler& samp);
    wgpu::BindGroup CreateObjectBindGroup(uint32_t bufOff, uint32_t bufSize);

private:
    void WriteSceneUniforms();         // Populate SceneUniforms from RndCam/RndEnviron
    void CreateDepthTexture(int w, int h);
    void CreateDefaultTextures();      // 1x1 white texture + default sampler

    // Shared utilities
    GpuDevice mGpu;
    PipelineManager mPipelines;

    // WebGPU state
    wgpu::CommandEncoder mEncoder;
    wgpu::RenderPassEncoder mPass;
    wgpu::TextureView mFrameView;      // Current frame's surface texture view
    bool mInPass = false;

    // Uniform buffers
    wgpu::Buffer mSceneBuffer;         // Group 0 — updated once per frame
    wgpu::BindGroup mSceneBindGroup;
    UniformRingBuffer mMaterialRing;   // Group 1 — 64KB, 256-byte aligned per-draw writes
    UniformRingBuffer mObjectRing;     // Group 2 — 64KB, 256-byte aligned per-draw writes

    // Depth buffer
    wgpu::Texture mDepthTex;
    wgpu::TextureView mDepthView;
    int mDepthWidth = 0, mDepthHeight = 0;

    // Default resources
    wgpu::Texture mWhiteTex;           // 1x1 white for untextured materials
    wgpu::TextureView mWhiteTexView;
    wgpu::Sampler mDefaultSampler;

    Hmx::Color mWgpuClearColor;
};
```

**Global instances** (in `Rnd_Wgpu.cpp`):
```cpp
static WgpuRnd gWgpuRndInstance;
Rnd& TheRnd = gWgpuRndInstance;
NgRnd& TheNgRnd = gWgpuRndInstance;
WgpuRnd* gWgpuRnd = &gWgpuRndInstance;  // used by Mesh_Wgpu.cpp, Tex_Wgpu.cpp
```

### Full Virtual Interface (Tier 2/3 — planned)

<details>
<summary>Complete WgpuRnd virtual method list for later tiers</summary>

```cpp
// Additional overrides to implement in Tier 2/3:
void ForceColorClear() override;
void DrawRect(...) override;
void DrawString(...) override;
void DrawLine(...) override;
RndTex* MakeDrawTarget(...) override;
void BeginDrawPass(RndTex* target) override;
void EndDrawPass() override;
void SetViewport(const Viewport& vp) override;
void SetShadowMap(RndTex* tex) override;
RndTex* PreProcessTexture() override;
RndTex* PostProcessTexture() override;
void ScreenDump(const char* path) override;
// Protected callbacks:
void DoWorldBegin() override;
void DoWorldEnd() override;
void DoPostProcess() override;
```

</details>

### WgpuShaderMgr

Replaces `DxShaderMgr`. Implements the `RndShaderMgr` virtual interface.

**Tier 1**: All 14 `SetVConstant`/`SetPConstant` methods are **no-ops**. The engine
calls these during material setup (via `Mat_NG.cpp`, `Env_NG.cpp`), but Tier 1
bypasses this path entirely — `Mesh_Wgpu.cpp::DrawShowing()` writes uniforms
directly from material/transform data, not through the shader constant dispatch.

**Tier 2** will implement a dispatch table mapping VShaderConstant/PShaderConstant
register indices to uniform buffer offsets. The decomp's enums have ~25 named
constants (e.g., `kVS_ViewProjMatrix = 4`, `kPS_BloomParams = 7`).

```cpp
class WgpuShaderMgr : public RndShaderMgr {
public:
    // All 14 SetVConstant/SetPConstant overrides — Tier 1: no-ops
    void SetVConstant(VShaderConstant c, const Hmx::Matrix4& m) override {}
    void SetVConstant(VShaderConstant c, const Vector4& v) override {}
    // ... (all other overloads are empty bodies)

    // Shader/material accessors — Tier 1: return nullptr
    void* FindShader(ShaderType, const ShaderOptions&) override { return nullptr; }
    void* NewShaderProgram() override { return nullptr; }
    RndMat* GetWork() override { return nullptr; }
    RndMat* GetPostProcMat() override { return nullptr; }
    RndMat* DrawHighlightMat() override { return nullptr; }
    RndMat* DrawRectMat() override { return nullptr; }
};
```

**Global instance** (in `Rnd_Wgpu.cpp`):
```cpp
static WgpuShaderMgr gWgpuShaderMgr;
RndShaderMgr& TheShaderMgr = gWgpuShaderMgr;
```

### GPU Resource Management: Side Tables (Not Subclasses)

The original design proposed `WgpuTex` and `WgpuMesh` subclasses storing GPU resources
as member variables. The actual implementation uses **side tables** — `unordered_map`
keyed by the decomp object pointer. This approach was chosen because:

1. **No factory registration needed** — `RndTex` and `RndMesh` objects are created by
   the engine's existing object system (DirLoader, ObjectDir). Injecting subclasses
   would require hooking the factory, which is complex and fragile.
2. **No decomp class layout changes** — Adding `wgpu::Texture` to `RndTex` would change
   its size, breaking compatibility with the PPC build and serialized data.
3. **`#ifdef HX_NATIVE` overrides** — We override virtual methods directly on the
   existing decomp classes, guarded by `#ifdef HX_NATIVE`.

#### Tex_Wgpu.cpp — GPU Texture Side Table

```cpp
struct GpuTexData {
    wgpu::Texture texture;
    wgpu::TextureView view;
    bool uploaded = false;
};
static std::unordered_map<RndTex*, GpuTexData> sTexGpuData;

// Public accessor (used by Mesh_Wgpu.cpp)
wgpu::TextureView GetGpuTexView(RndTex* tex);

// Overrides weak stubs in engine_stubs_generated.cpp
void RndTex::PresyncBitmap();    // creates GPU texture via TextureConvert::CreateFromBitmap
void RndTex::SyncBitmap();       // no-op (work done in PresyncBitmap)
```

#### Mesh_Wgpu.cpp — GPU Mesh Side Table

```cpp
struct GpuMeshData {
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    int numIndices = 0;
    int numVertices = 0;
    bool uploaded = false;
};
static std::unordered_map<RndMesh*, GpuMeshData> sMeshGpuData;

// DrawShowing override added to RndMesh in Mesh.h via #ifdef HX_NATIVE
void RndMesh::DrawShowing();     // THE draw call — see pipeline below
```

#### Decomp Header Modifications

Minimal `#ifdef HX_NATIVE` additions to decomp headers (no PPC build impact):

- **`Mesh.h`**: `virtual void DrawShowing();` override
- **`BaseMaterial.h`**: 6 public getters (GetCull, GetStencil, GetAlphaCut, GetAlphaWrite, GetAlphaThreshold, GetTexWrap)
- **`Env.h`**: FogStart(), FogEnd(), FogColor() accessors

### Mesh Draw Submission Pipeline

`RndMesh::DrawShowing()` is THE hot path — where material state becomes a draw call.
Implemented in `Mesh_Wgpu.cpp`.

```
RndMesh::DrawShowing()  (Mesh_Wgpu.cpp)
  1. Guard: gWgpuRnd && gWgpuRnd->IsInPass(), has material
  2. EnsureMeshUploaded(this):
     - Check side table, return if already uploaded
     - Get geom owner (shared geometry support)
     - UnpackStaticVertices() → GpuVertex[] (from RndMesh::Vert)
     - Create vertex buffer + index buffer, upload via WriteBuffer
     - Store in sMeshGpuData side table
  3. Build PipelineKey from material state:
     - shaderType = 18 (kStandardShader)
     - blend, zMode, cull, stencil via getters (GetBlend, GetZMode, etc.)
     - alphaCut, alphaWrite from material
     - layout = VertexLayoutType::Static
     - targetFormat from GpuDevice surface format
  4. pipeline = gWgpuRnd->Pipelines().GetPipeline(key)  // cached lookup
  5. pass.SetPipeline(pipeline)
  6. Write MaterialUniforms (color, alphaThreshold, useTexture) to ring buffer
  7. Get diffuse texture: mat->GetDiffuseTex() → PresyncBitmap() → GetGpuTexView()
     - Falls back to gWgpuRnd->WhiteTexView() if no texture
  8. Get sampler from material's GetTexWrap() → gWgpuRnd->Gpu().GetSampler()
  9. Create material bind group (ring buffer offset + texture view + sampler)
  10. pass.SetBindGroup(1, materialGroup)
  11. Write ObjectUniforms (world transform) to ring buffer
  12. Create object bind group (ring buffer offset)
  13. pass.SetBindGroup(2, objectGroup)
  14. pass.SetVertexBuffer(0, vertexBuffer)
  15. pass.SetIndexBuffer(indexBuffer, Uint16)
  16. pass.DrawIndexed(numIndices)
```

**Key design decisions**:
- **Bind groups created per draw**: Fresh bind groups each draw call, referencing
  ring buffer at different offsets. Dawn caches these internally — acceptable for
  Tier 1. Tier 2 can add bind group caching keyed by material+texture.
- **Texture upload on demand**: `PresyncBitmap()` called lazily during first draw,
  not at load time. Side table caches the result.

### Texture Lifecycle

```
.ark archive load
  → RndTex::PreLoad(BinStream)     // reads bitmap header + pixel data into mBitmap
  → RndTex::PostLoad(BinStream)    // finalize

First draw referencing this texture (in Mesh_Wgpu.cpp):
  → mat->GetDiffuseTex()
  → diffTex->PresyncBitmap()        // Tex_Wgpu.cpp override of weak stub
    → Check sTexGpuData side table — skip if already uploaded
    → TextureConvert::CreateFromBitmap(gpu, mBitmap, numMips)
      → Internally: ByteSwapDXT + UntileMilo + CreateTexture + WriteTexture
    → Store {texture, view, uploaded=true} in sTexGpuData[this]
  → GetGpuTexView(diffTex)          // returns wgpu::TextureView from side table
```

**Cleanup**: GPU texture resources are currently leaked at shutdown (cleaned up by
process exit). Tier 2 should hook `RndTex` destructor or add reference counting
via `CleanupGpuTex()`.

---

## Milo Viewer: Standalone Application

The viewer uses the same shared utilities as `WgpuRnd` but with a simpler entry
point — it reuses the decomp's `ObjectDir` / `DirLoader` for .milo loading (not a
bespoke parser), but skips game logic initialization. This means it links the
engine's object system but doesn't require the full game runtime. Its purpose is
to validate the rendering pipeline before the full engine boots with WgpuRnd.

```cpp
class MiloViewer {
public:
    bool Init(const char* miloPath, int width, int height);
    void MainLoop();
    void Shutdown();

private:
    // Shared utilities (same code as WgpuRnd uses)
    GpuDevice mGpu;
    PipelineManager mPipelines;

    // Viewer-specific
    OrbitCamera mCamera;         // mouse-controlled orbit camera
    MiloScene mScene;            // loaded .milo file contents
    bool mShowWireframe;
    bool mShowBones;
    int mSelectedObject;
};
```

---

## Implementation Tiers

### Tier 1.5: Geometry + Textures + Full Material Pipeline (Current)

**Goal**: See meshes with textures and proper material-driven lighting. Validate the
full per-material property pipeline before moving to skinned meshes and post-processing.

| Component | What gets implemented | Status |
|-----------|---------------------|--------|
| GpuDevice | Init, surface creation, present, BC feature detection, sampler cache | **DONE** |
| TextureConvert | RGBA8, BC1, BC3 + Xbox byte-swap + untile | **DONE** |
| VertexFormats | Static mesh unpack + layout descriptors | **DONE** |
| PipelineManager | `standard.wgsl` (full material), all blend modes, all ZModes, stencil, pipeline cache (512 warning) | **DONE** |
| WgpuRnd | BeginDrawing, EndDrawing, Clear, scene uniforms, ring buffers (auto-grow), multi-light from RndEnviron | **DONE** |
| WgpuShaderMgr | SetVConstant/SetPConstant stubs (direct uniform writes) | **DONE** |
| WgpuTex | PresyncBitmap (GPU upload via TextureConvert side table), destructor cleanup | **DONE** |
| WgpuMesh | DrawShowing (pipeline selection, bind groups, indexed draw), destructor cleanup | **DONE** |

**Shaders implemented**: standard (half-Lambert diffuse, Blinn-Phong specular, emissive,
rim lighting, intensify, multi-light (up to 4 directional), fog, alpha test)

**Visual result**: Textured meshes with full material properties — specular highlights,
emissive glow, rim edge lighting, intensified textures, environment-driven multi-light.
17/17 test props rendered to `archive/screenshots/`.

**Reliability**: Ring buffer auto-grows on overflow (was silent wrap). GPU resource
cleanup via destructor hooks. Error logging in upload failure paths. Pipeline cache
size warning at 512 entries.

**Milo Viewer** (Step 5): Standalone app (`milo-viewer`) loads `.milo_xbox` files from CLI,
renders with auto-framing orbit camera, supports `--screenshot` headless mode and
`--azimuth`/`--elevation` camera overrides. Batch script: `native/scripts/render_screenshots.sh`

**Build notes**: GFX shared utilities compile with `-fms-compatibility` (engine headers) and
Dawn/WebGPU headers simultaneously via `-D__GNUC_STDC_INLINE__` and
`-D__GCC_ATOMIC_TEST_AND_SET_TRUEVAL=1` workarounds for GCC 15 + clang MSVC compat mode.

**Key implementation discoveries**:

- **Matrix convention**: Milo uses row-major matrices (D3D convention). The Transform
  struct stores rotation as 3×3 `Hmx::Matrix3` + `Vector3` translation. `memcpy`-ing
  the row-major `float[16]` into WGSL's `mat4x4<f32>` (column-major storage) gives the
  correct transpose for `M * v` in WGSL matching D3D's `v * M` convention.
- **View matrix computation**: Not stored directly — computed as inverse of camera's
  `WorldXfm()`. For orthogonal rotation: transpose the 3×3 rotation, negate the
  translated position (`tx = -(R^T * t)`).
- **Dawn API naming (2025+)**: `TexelCopyTextureInfo` (not `ImageCopyTexture`),
  `TexelCopyBufferLayout` (not `TextureDataLayout`), `BlendFactor::Dst` (not `DstColor`),
  `depthWriteEnabled` is `wgpu::OptionalBool` (not `bool`).
- **Weak stub override pattern**: `engine_stubs_generated.cpp` defines `PresyncBitmap()`,
  `SyncBitmap()`, `DrawShowing()` as weak symbols. `Tex_Wgpu.cpp` and `Mesh_Wgpu.cpp`
  provide strong definitions that the linker prefers — no registration needed.

### Tier 2: Full Lighting + Normal/Specular Maps + Post-Processing

**Goal**: Visually representative rendering. Characters look recognizable.

| Component | What gets added |
|-----------|----------------|
| TextureConvert | BC5 (normal maps), cube maps, render targets, DecompressDXT fallback |
| VertexFormats | Skinned mesh unpack (bone weights/indices) |
| PipelineManager | Full standard shader variants, all 11 blend modes, all 5 ZModes, stencil |
| WgpuRnd | Shadow pass, bloom/blur post-process chain, DoPostProcess() |
| WgpuShaderMgr | Full shader constant forwarding |

**Shaders implemented**: standard (full variants), particles, multimesh, shadowmap,
bloom, blur, downsample, postprocess
**Visual result**: Lit characters with normal/specular maps, shadows, bloom

### Tier 3: Advanced Effects + Polish

**Goal**: Visual parity with Xbox 360. All rendering features.

| Component | What gets added |
|-----------|----------------|
| TextureConvert | Movie textures, density maps |
| PipelineManager | Fur, refraction, depth volume, velocity shaders |
| WgpuRnd | Motion blur pass, depth-of-field, occlusion queries |

**Shaders implemented**: fur, depthvolume, velocity (object+camera), movie/YUV,
sync_track, player depth/greenscreen, twirl, crew_photo
**Visual result**: Full Xbox 360 visual fidelity including fur, refraction, motion blur

---

## File Structure

Files marked with ✓ exist; others are planned for Tier 2/3.

```
native/
├── CMakeLists.txt                    ✓ Builds dc3-native target
├── src/
│   ├── main.cpp                      ✓ Engine entry point
│   ├── viewer/                       ✓ Step 5 — operational
│   │   └── milo_viewer.cpp          ✓ Standalone viewer: orbit camera, auto-frame, --screenshot
│   ├── gfx/                          ✓ Shared utilities
│   │   ├── GpuDevice.h / .cpp        ✓ WebGPU device lifecycle, GLFW window, sampler cache
│   │   ├── TextureConvert.h / .cpp   ✓ Format conversion, byte-swap, untile, DXT decompress
│   │   ├── VertexFormats.h / .cpp    ✓ Layout descriptors + CPU unpack from RndMesh::Vert
│   │   ├── PipelineManager.h / .cpp  ✓ Pipeline cache, bind group layouts, shader compilation
│   │   └── standard_wgsl.inc         ✓ Embedded WGSL shader source (C++ raw string literal)
│   └── platform/
│       ├── Rnd_Wgpu.h               ✓ WgpuRnd + WgpuShaderMgr + uniform structs
│       ├── Rnd_Wgpu.cpp             ✓ WgpuRnd implementation (replaces Rnd_Stub.cpp)
│       ├── Tex_Wgpu.cpp             ✓ RndTex::PresyncBitmap override + GPU texture side table
│       ├── Mesh_Wgpu.cpp            ✓ RndMesh::DrawShowing override + GPU mesh side table
│       ├── RndTex_Native.cpp         ✓ RndTex::PreLoad/PostLoad stream consumption
│       ├── Rnd_Stub.cpp             (replaced by Rnd_Wgpu.cpp, kept for reference)
│       └── ...                       Other platform stubs
├── shaders/                          WGSL shader files
│   ├── standard.wgsl                ✓ Tier 1.5: half-Lambert + Blinn-Phong specular + emissive + rim + intensify + multi-light + fog + alpha test
│   ├── bloom.wgsl                   Tier 2: bloom extraction + composite
│   ├── blur.wgsl                    Tier 2: gaussian blur (separable)
│   ├── downsample.wgsl              Tier 2: mip-chain downsampling
│   ├── drawrect.wgsl                Tier 2: UI rectangle rendering
│   ├── line.wgsl                    Tier 2: debug line rendering
│   ├── particles.wgsl               Tier 2: particle system (billboard/skinned)
│   ├── multimesh.wgsl               Tier 2: instanced mesh (billboard flag)
│   ├── postprocess.wgsl             Tier 2: bloom/blur/vignette/tone mapping
│   ├── shadowmap.wgsl               Tier 2: shadow depth pass
│   ├── fur.wgsl                     Tier 3: shell-based fur rendering
│   ├── depthvolume.wgsl             Tier 3: depth volume effects
│   ├── synctrack.wgsl               Tier 3: sync track + charge effect
│   ├── unwrapuv.wgsl                Tier 3: UV unwrap visualization
│   ├── velocity.wgsl                Tier 3: per-object motion vectors
│   ├── velocity_cam.wgsl            Tier 3: camera motion vectors
│   └── movie.wgsl                   Tier 3: YUV video texture decoding
├── include/
│   └── bits/
│       └── stl_iterator.h            ✓ Shadow copy with iterator→pointer implicit conversion
└── tools/
    └── milo_tex_convert/             Tier 2: offline texture converter
        ├── main.cpp
        └── Xbox360Detile.cpp
```

---

## Texture Pipeline Detail

### Runtime Path (Development)

```
.ark archive
  → RndTex::PreLoad (reads bitmap header + pixel data into mBitmap)
  → [lazy, on first draw] RndTex::PresyncBitmap()  (Tex_Wgpu.cpp)
    → Check sTexGpuData side table — skip if already uploaded
    → TextureConvert::CreateFromBitmap(gpu, bitmap, numMips)
      → Detect format from RndBitmap::Order()
      → If DXT: ByteSwapDXT() — 16-bit byte-swap for Xbox BE data (CRITICAL)
      → If Milo-tiled (mOrder & 4): UntileMilo() — custom lookup-table untile
      → If uncompressed: ConvertChannelOrder() based on mOrder & 1
      → If BC feature unavailable: DecompressDXT() → RGBA8 fallback
      → wgpu::Device::CreateTexture() + wgpu::Queue::WriteTexture()
      → Upload mip chain if present
    → Store {texture, view} in sTexGpuData[this]
```

### Offline Path (Shipping)

```
milo_tex_convert tool:
  → Read .ark textures
  → ByteSwapDXT + UntileMilo + ConvertChannelOrder
  → Write as KTX2 (GPU-native compressed) or PNG (debug)
  → TextureConvert::CreateFromFile() loads pre-converted files (no CPU conversion)
```

See the **TextureConvert** section above for the definitive endianness and tiling
documentation. Key points: DXT data is NOT endian-neutral (requires 16-bit byte-swap),
and Milo tiling uses custom lookup tables (not Morton order).

Reference implementations:
- MiloEditor: `MiloLib/Assets/Rnd/RndBitmap.cs` (lines 186-199, byte-swap)
- LibForge: `TextureConverter.cs` (full pipeline)
- xenia: `src/xenia/gpu/texture_info.cc` (Xbox 360 GPU tiling, NOT Milo tiling)
- decomp: `src/system/rndobj/Bitmap.cpp` (`DecodeDxtColor`, `PixelOffset`)

---

## Milo Archive Format (Quick Reference)

The Milo Viewer loads `.milo` files directly. Key format details (confirmed by
MiloEditor, PyMilo, and the decomp's `ObjectDir`):

**Container** (block-compressed archive):
```
Header (LE):
  u32 magic          — 0xCABEDEAF (MILO_A/uncompressed), 0xCBBEDEAF (MILO_B),
                        0xCCBEDEAF (MILO_C/gzip), 0xCDBEDEAF (MILO_D/zlib, DC3)
  u32 startOffset    — byte offset to first block
  u32 numBlocks      — number of compressed blocks
  u32 largestBlock   — max decompressed block size
  u32[numBlocks] blockSizes  — decompressed size of each block

Blocks:
  Each block is independently zlib-compressed (MILO_D).
  Max block size: 0x20000 (128 KB).
  Decompress all blocks, concatenate → raw object directory stream.
```

**Object Directory** (inside decompressed stream):
- Revision/version header
- Entry table: array of (type, name) pairs
- Serialized objects in entry order (each object reads its own fields from stream)
- Subdirectories (recursive)
- External file references (proxy dirs loaded lazily)

**Reference implementations**:
- PyMilo `MiloContainer.py` — simplest (Python, ~200 lines)
- MiloEditor `MiloFile.cs` — most complete (C#, parallel decompression)
- pikaxe `scene/milo.rs` — Rust, with platform detection

For the viewer, we need: decompress container → parse directory → deserialize
RndMesh, RndMat, RndTex, RndTransformable, RndEnviron, RndLight, RndCam objects.

---

## Window Management

For the Milo Viewer and windowed engine mode, use **GLFW** for MVP window
creation and event handling:

- GLFW provides the native window handle
- Dawn creates a `wgpu::Surface` from the native window handle
  (X11: `SurfaceSourceXlibWindow`, Wayland: `SurfaceSourceWaylandSurface`)
- GLFW handles keyboard/mouse input for the viewer's orbit camera
- Dawn's own examples use GLFW — well-tested integration path

**Why GLFW over SDL2 for MVP**: Simpler dependency, Dawn-native, fewer build
concerns. SDL2 can be added later (Phase 4) when input/audio/gamepad support
is actually needed. Don't pull in SDL2 complexity before it's justified.

---

## Performance Considerations

### Pipeline State Changes

The original D3D9 engine sets render state per-draw (mutable state). WebGPU requires
immutable pipeline objects. Mitigation:

1. **Sort draws by material** — already done by `RndDir::SyncDrawables()`
2. **Pipeline cache** — key on (shader+blend+depth+cull+vertex layout+target format)
3. **Expected cache size**: 200-500 unique pipelines for a typical DC3 scene
4. **Pipeline creation**: ~1-5ms each, amortized over first few frames

### Uniform Buffer Updates

WebGPU has no equivalent to D3D9's `SetVertexShaderConstant()` immediate updates.
Tier 1 uses `UniformRingBuffer` — a 64KB GPU buffer per group with advancing write
offset (256-byte aligned). Each draw call writes at a new offset and creates a fresh
bind group referencing that offset.

1. **Group 0 (scene)**: Single `wgpu::Buffer`, written once per frame via `WriteBuffer`
2. **Group 1 (material)**: Ring buffer, `Write()` returns offset, new bind group per draw
3. **Group 2 (object)**: Ring buffer, `Write()` returns offset, new bind group per draw
4. **Bind groups created per draw**: Dawn caches these internally. Tier 2 can optimize
   by reusing bind groups for repeated material+texture combinations

### Texture Binding

WebGPU bind groups are immutable — can't swap textures like D3D9's `SetTexture()`.
Mitigation:

1. **Per-material bind groups** — create bind group per unique texture combination
2. **Bind group cache** — key on set of texture handles
3. **Lazy creation** — only create when first drawn with this texture set

### Draw Call Batching

DC3 scenes typically have 500-2000 draw calls per frame. WebGPU handles this fine
without instancing. For MultiMesh (hundreds of instances of one mesh), use:

1. **Instance buffer** with per-instance transforms
2. **`drawIndexed(... instanceCount)`** for hardware instancing
3. Falls back to individual draws if instance data varies (visibility culling)

---

## Open Items (the ~10% deferred)

### Resolved (by code analysis, staff review session)

1. ~~**Exact shader constant mappings**~~ — **RESOLVED**: VShaderConstant/PShaderConstant
   enums now populated with ~25 named constants. Register→semantic mappings discovered
   by grepping the decomp (Env_NG.cpp, Mat_NG.cpp, Rnd_Xbox.cpp, Fur_NG.cpp, etc.).
   WgpuShaderMgr implements as a dispatch table, not a complex emulation layer.
2. ~~**RndColorXfm**~~ — **RESOLVED**: Fully decompiled in `ColorXfm.h/.cpp`. It's a
   3x3 affine RGB transform + vec3 translation (`outRGB = M * inRGB + T`), built by
   chaining hue→saturation→lightness→contrast→brightness→levels adjustments.
   Not a 4x4 matrix — SceneUniforms updated to reflect actual format.
3. ~~**Multi-tile rendering**~~ — **NOT NEEDED**: Xbox 360 EDRAM tiling is handled by
   the D3D runtime, not game code. WebGPU doesn't need this.

### Remaining open items

4. **Movie texture double-buffering** — needs Bink/FFmpeg integration first (Tier 3)
5. **Occlusion query integration** — depends on scene traversal being correct (Tier 3)
6. **Exact post-processing parameters** — bloom threshold, blur kernel sizes need
   tuning against original game captures (Tier 2)
7. **RndFur shell rendering** — shell pass count discoverable from Fur_NG.cpp
   decomp (registers kPS_FurGeometry/kPS_FurShell), but needs runtime validation
8. **Projected light textures** — coordinate space mapping from `RndLight::Projection()`
   and `mTextureXfm` (see `Lit.h:79`). Register kPS_SpotlightTex (0xB) used.

### New items (from staff review)

9. **Dawn version pinning** — pin to a specific commit hash for reproducibility.
   Dawn follows Chromium cadence with frequent breaking changes (Surface API
   overhaul, StringView callbacks). Document the pinned commit and update deliberately.
10. **Performance profiling** — pipeline count (200-500) and draw call count
    (500-2000) are unvalidated estimates. Profile actual DC3 venue .milo files
    from milo-rnd-library to validate before optimizing.

---

## Validation & Testing

No testing/validation strategy existed before this review. This section defines how
we verify the renderer is correct.

### Visual Regression Testing

1. **Reference captures from xenia**: Capture screenshots at known game states using
   xenia's screenshot functionality. These serve as ground truth for pixel comparison.
   - Capture at: title screen, character select, venue load, mid-gameplay
   - Store in `native/test/reference/` as PNGs with descriptive names

2. **Automated screenshot comparison**: After each rendering milestone, dump the
   WgpuRnd output to PNG (headless mode) and compare against xenia reference:
   - Structural similarity (SSIM) threshold — not pixel-exact, but perceptually close
   - Flag regressions when SSIM drops below threshold after code changes

3. **Per-object validation**: Load individual .milo files from `milo-rnd-library/dc3/`
   in the viewer and compare against MiloEditor's viewport rendering. Focus on:
   - Texture correctness (DXT byte-swap, channel order, tiling)
   - Vertex positions (DEC4N unpack correctness)
   - Material transparency (blend mode mapping)

### Shader Constant Verification

The shader constant register map (VShaderConstant/PShaderConstant enums) was
extracted from decomp source by grepping all `SetVConstant`/`SetPConstant` calls.
To verify completeness:

1. **Ghidra cross-reference**: Decompile DxShaderMgr::SetVConstant implementations
   in Ghidra to find any register slots used from non-decomped code paths
2. **Runtime instrumentation**: Add logging to WgpuShaderMgr that records which
   register slots are actually written during a frame — compare against our enum
3. **RB3 decomp comparison**: Check if RB3's equivalent shader manager has
   additional constant mappings not present in DC3's decomp

### Texture Format Coverage

Verify all `mOrder` values seen in DC3 assets are handled:

1. Scan all .milo textures in `milo-rnd-library/dc3/` with a script that reads
   `mBpp` and `mOrder` fields from each RndBitmap header
2. Cross-reference against `TextureConvert::MapBitmapFormat()` to ensure no
   unhandled format combinations
3. Specific DXT5 validation: confirm 16-bit byte-swap applies correctly to the
   8-byte alpha block (verified: the decomp's `DecodeDxt5Alpha` compensates for
   swapped byte pairs throughout, confirming universal k8in16 swap)

### Performance Baseline

Before optimizing:

1. Profile actual draw call counts per frame in a DC3 venue (not estimates)
2. Count unique (shader+blend+zmode+cull) combinations across all materials
3. Measure pipeline creation latency on target hardware (Vulkan/Linux)
4. Establish per-frame GPU time budget (16.6ms target for 60fps)

---

## Reference Material

| Resource | Location | Use |
|----------|----------|-----|
| RB3 decomp | `mcp__orchestrator__lookup_rb3` | Shared Milo engine code |
| MiloEditor | `~/code/milohax/milo-engine-libs/harmonix-repos/MiloEditor/` | Best general Milo parser (C#). `MiloLib/Assets/Rnd/` has 50+ asset types. `ImMilo/` has mesh/texture viewers. |
| pikaxe | `~/code/milohax/milo-engine-libs/harmonix-repos/pikaxe/` | Rust toolkit. Complete DXT codec (`core/pikaxe/src/texture/dxt.rs`), mesh→glTF (`core/pikaxe_gltf/`), material parsing (`core/pikaxe/src/scene/mat/`). Most actively maintained. |
| Boomy | `~/code/milohax/milo-engine-libs/harmonix-repos/Boomy/` | DC3-specific editor. Choreography, camera shots, move definitions. `BoomyDeps/MiloLib/Assets/Ham/` has DC3 asset types. |
| Mackiloha | `~/code/milohax/milo-engine-libs/harmonix-repos/Mackiloha/` | ARK archive tools, .milo container I/O |
| LibForge | `~/code/milohax/milo-engine-libs/harmonix-repos/LibForge/` | Texture conversion (`TextureConverter.cs`), 010 Editor templates (`010/`) |
| GameArchives | `~/code/milohax/milo-engine-libs/harmonix-repos/GameArchives/` | ARK v1-v10 reader, STFS/CON (Xbox 360 packages) |
| milo-rnd-library | `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/` | Pre-extracted .milo files (DC3: ~9.4 GB under `dc3/`) |
| milo-script-library | `~/code/milohax/milo-engine-libs/harmonix-repos/milo-script-library/` | Extracted DTA scripts. `dc3/1.0 final/` has `rnd_objects.dta`, `milo_objects.dta` |
| DC3 decomp | `src/system/rndobj/`, `src/system/rnddx9/` | Authoritative field offsets and enums |
| Dawn examples | `../dawn/src/dawn/samples/` | WebGPU API usage |
| Learn WebGPU | https://eliemichel.github.io/LearnWebGPU/ | Dawn C++ tutorial |
