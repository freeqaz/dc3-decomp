# Approach: Vulkan / DXVK-Native

Two distinct Vulkan-based strategies: writing a Vulkan renderer from scratch, or
using DXVK-Native to translate D3D9 calls to Vulkan automatically.

## Option A: DXVK-Native (D3D9 → Vulkan Translation)

### What It Is

DXVK is a Vulkan-based implementation of D3D9/10/11 that powers most Windows game
compatibility on Linux (via Proton/Wine). **DXVK-Native** is the same library built
to run natively on Linux without Wine — it provides D3D9 API headers and translates
all calls to Vulkan internally.

### How It Would Work

```
Milo Engine (C++)
    │
    ▼
DxRnd (existing D3D9 code, adapted)
    │  IDirect3DDevice9::DrawPrimitive(), SetRenderState(), etc.
    ▼
DXVK-Native (library)
    │  Translates D3D9 calls → Vulkan commands
    │  Translates D3D9 shader bytecode → SPIR-V (automatic)
    ▼
Vulkan Driver
    │
    ▼
GPU
```

The key insight: keep the existing `DxRnd` code structure but replace the actual
D3D9 implementation with DXVK's Vulkan-backed version. Many D3D9 calls would work
as-is because DXVK implements the full D3D9 API surface.

### What DXVK Handles Automatically

- **Shader translation**: D3D9 shader bytecode (SM 2.0/3.0) → SPIR-V via `DxsoModule`
- **Memory management**: D3D9 pools → Vulkan memory heaps
- **State management**: D3D9 render states → Vulkan pipeline state objects
- **Command submission**: Immediate D3D9 calls → async Vulkan command buffers
- **Synchronization**: Implicit D3D9 sync → explicit Vulkan semaphores/fences
- **Pipeline caching**: State cache to avoid shader compilation stutter

### What DXVK Does NOT Handle (Xbox 360 Divergences)

This is the critical issue. Xbox 360 D3D9 is NOT standard PC D3D9:

| Xbox 360 Feature | PC D3D9 Equivalent | Action Needed |
|-------------------|-------------------|---------------|
| **eDRAM tiling** (`BeginTiling`/`EndTiling`) | No equivalent | Replace with standard render-to-texture |
| **Resolve operations** (eDRAM → main memory) | `StretchRect` (partial) | Replace with blit/copy |
| **XG* texture functions** (tiled format) | Standard linear textures | Deswizzle on load |
| **Physical memory alloc** (`XPhysicalAlloc`) | `VirtualAlloc` | Replace with standard alloc |
| **Unified memory model** | Split CPU/GPU memory | DXVK handles this |
| **Custom render target formats** (7e3 HDR) | Standard formats | Map to closest PC format |
| **Predicated tiling** (multi-pass for large RT) | Single-pass (enough VRAM) | Remove tiling logic |
| **`XGEstimateIdealShaderCost`** | No equivalent | Remove (profiling only) |
| **`XGRegisterPixelShader`** (Xenos microcode) | `CreatePixelShader` (SM bytecode) | Rewrite shaders |

### The Shader Problem

Xbox 360 shaders are in Xenos GPU microcode format, NOT standard D3D9 SM 2.0/3.0
bytecode. DXVK's `DxsoModule` translates PC D3D9 bytecode, not Xenos microcode.

**Solutions**:
1. **Rewrite the ~12 Milo shader types as PC D3D9 HLSL** → compile with `fxc.exe` to
   SM 3.0 bytecode → DXVK translates to SPIR-V automatically
2. Use **XenosRecomp** to decompile Xenos microcode to HLSL as reference
3. Write shaders from scratch based on `RndMat` material properties

Option 1 is the pragmatic choice. You rewrite 12 shaders in HLSL (a language the
existing codebase already uses conceptually), compile them to standard PC D3D9
bytecode, and DXVK handles the rest.

### Production Track Record

DXVK-Native is used in shipped products:
- **Portal 2** (Valve Linux port)
- **Left 4 Dead 2** (Valve Linux port)
- **Ys VIII / Ys IX** (NIS America PC ports)
- **Momentum Mod**
- FNA framework games (multiple indie titles)

### Integration

DXVK-Native uses Meson as its build system and supports SDL2/SDL3/GLFW as windowing
backends. Integration:

```bash
# Build DXVK-Native
meson setup --cross-file build-win64.txt build
ninja -C build

# Link against it
# Your CMake:
target_link_libraries(dc3-native dxvk_d3d9)
target_include_directories(dc3-native PRIVATE dxvk/include/native/directx)
```

DXVK provides its own Windows type definitions (`HWND` becomes `SDL_Window*`, etc.),
so many Win32 compatibility issues are resolved for free.

### Pros

- **Minimal renderer rewrite**: Keep the existing `DxRnd` call pattern, just fix
  Xbox 360-specific calls
- **Proven in production**: Valve ships games with this
- **Automatic shader translation**: D3D9 bytecode → SPIR-V with no manual work
- **Performance**: Vulkan backend is generally faster than native D3D9
- **Pipeline caching**: Eliminates shader compilation stutter after first run
- **Win32 type compatibility**: DXVK's native headers handle DWORD, HRESULT, etc.

### Cons

- **Xbox 360 D3D9 != PC D3D9**: eDRAM, tiling, Xenos shaders all need manual work
- **Vulkan-only**: No OpenGL fallback, no Metal (macOS unsupported)
- **Build complexity**: Meson (DXVK) + CMake (our code) = two build systems
- **DXVK is complex**: 85k+ lines of code as a dependency
- **Debugging is indirect**: Graphics bugs may be in your code, DXVK's translation,
  or the Vulkan driver
- **Still need shader rewrites**: Xenos microcode doesn't go through DXVK's translator

## Option B: Raw Vulkan (From Scratch)

### How It Would Work

Write a completely new `VkRnd` class using the Vulkan API directly. No translation
layer — every GPU operation explicitly managed.

```
Rnd (abstract)
  └─ NgRnd
       └─ VkRnd (new)
            ├─ VkInstance, VkDevice, VkSwapchain
            ├─ VkRenderPass, VkPipeline (per material config)
            ├─ VkBuffer (vertex/index), VkImage (textures)
            ├─ VkCommandBuffer (draw recording)
            ├─ VkDescriptorSet (resource binding)
            └─ SPIR-V shaders (compiled from GLSL via glslc)
```

### Pros

- **Maximum control**: Every GPU operation is explicit and optimizable
- **Future-proof**: Vulkan is the primary graphics API going forward
- **Multi-threaded rendering**: Command buffers can be recorded on multiple threads
- **Explicit memory management**: Full control over GPU memory
- **Cross-platform**: Linux, Windows, Android (not macOS without MoltenVK)
- **Best debugging tools**: RenderDoc, Vulkan validation layers

### Cons

- **Enormous development effort**: A basic Vulkan renderer is ~5,000 lines just for
  initialization, swap chain, and a triangle. Full game renderer is months of work.
- **Pipeline state objects**: Every unique combination of render states needs a
  pre-created `VkPipeline`. The Milo engine changes blend/depth/cull state per-draw —
  this needs a pipeline cache or dynamic state.
- **Descriptor sets**: Resource binding is fundamentally different from D3D9's
  slot-based model. Requires architectural decisions about descriptor management.
- **Synchronization**: Explicit barriers, semaphores, fences. Getting this wrong causes
  GPU hangs or visual corruption with no useful error messages.
- **No macOS without MoltenVK**: MoltenVK translates Vulkan to Metal, but adds another
  layer of complexity and potential issues.

### Effort Estimate

| Component | Effort |
|-----------|--------|
| Instance, device, swap chain | 1-2 weeks |
| Render pass, pipeline cache | 1-2 weeks |
| Buffer/image management | 1 week |
| Descriptor set management | 1 week |
| Command buffer recording | 1 week |
| Mesh rendering | 1 week |
| All 12 shader types (GLSL → SPIR-V) | 2-3 weeks |
| Texture loading + format handling | 1 week |
| Render targets + post-processing | 1-2 weeks |
| Material system integration | 1 week |
| Synchronization + resource lifetime | 1-2 weeks |
| **Total** | **~14-18 weeks** |

## Recommendation

**DXVK-Native is the pragmatic choice if Vulkan is the target API.**

The renderer rewrite is significantly less work with DXVK than raw Vulkan. The main
risk (Xbox 360 D3D9 divergences) is manageable because:

1. eDRAM tiling can be simply removed (PC has enough VRAM for full-resolution render
   targets without tiling)
2. Texture deswizzling is a known-solved problem (xenia has reference code)
3. The ~12 shaders are rewritable in a few weeks
4. DXVK's Win32 compatibility headers solve many porting issues for free

**If macOS support is required**, DXVK won't work. In that case, consider bgfx (see
[APPROACH_BGFX_SOKOL.md](APPROACH_BGFX_SOKOL.md)) or OpenGL as a compatibility
fallback.

## References

- [DXVK GitHub](https://github.com/doitsujin/dxvk) (DXVK-Native merged upstream)
- [DXVK-Native usage notes](https://github.com/doitsujin/dxvk/blob/master/README.md#native)
- [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) — Xbox 360 shader decompiler
- [Xenia GPU docs](https://github.com/xenia-project/xenia/blob/master/docs/gpu.md)
- [Vulkan Tutorial](https://vulkan-tutorial.com/)
- [vkguide.dev](https://vkguide.dev/) — practical Vulkan renderer guide
- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp) — Xbox 360 to PC
  port using XenosRecomp (successful precedent)
