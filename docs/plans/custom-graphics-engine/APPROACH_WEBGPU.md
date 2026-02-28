# Approach: WebGPU via Dawn — SELECTED

> **This is the chosen rendering approach for the DC3 native port.**
> sokol_gfx was the #2 option — see [APPROACH_BGFX_SOKOL.md](APPROACH_BGFX_SOKOL.md).

## Overview

Target the **WebGPU API** (`webgpu.h`) as the primary rendering abstraction. WebGPU
is a modern graphics API designed from the ground up to work on all platforms — desktop
(via Vulkan, Metal, D3D12) and web browsers (native WebGPU support).

This is the only approach that delivers **macOS + Web + Linux + Windows** from a single
rendering codebase with no translation layers.

## Local Setup

Dawn is cloned at `../dawn` (sibling to `dc3-decomp`):
```
/home/free/code/milohax/
  ├── dc3-decomp/    # this project
  └── dawn/          # git@github.com:google/dawn.git
```

## Why WebGPU?

WebGPU was designed by GPU engineers from Apple, Google, Mozilla, Microsoft, and Intel
to be the successor to both WebGL (in browsers) and a competitive native API. It maps
near 1:1 to Metal (Apple's GPU API), making macOS a first-class citizen.

### Platform Coverage

| Platform | Backend | Status |
|----------|---------|--------|
| macOS | Metal | Production (Safari 26, Chrome, Dawn native) |
| Linux | Vulkan | Production (Chrome, Firefox, Dawn/wgpu native) |
| Windows | D3D12 / Vulkan | Production |
| iOS / iPadOS | Metal | Production (Safari 26) |
| Android | Vulkan | Production (Chrome 113+) |
| Web (Chrome) | Native WebGPU | Shipped since April 2023 |
| Web (Firefox) | Native WebGPU | Shipped since v141 (Win), v145 (macOS) |
| Web (Safari) | Native WebGPU | Shipped in Safari 26.0 (WWDC 2025) |
| visionOS | Metal | Safari WebGPU with WebXR |

**All major browsers ship WebGPU as of November 2025.** This is not experimental.

## Implementations

### Dawn (Google, C++) — Selected

The implementation that powers Chrome/Chromium. Written in C++.

- **Most mature for C++ projects**: Better error messages, ahead in spec compliance
- **API**: Standard `webgpu.h` (C) and `webgpu_cpp.h` (C++ wrapper)
- **Build**: CMake supported (`CMakeLists.txt` present in repo root)
- **Web**: Via Emscripten + `emdawnwebgpu` bridge library
- **Backends**: D3D12, Metal, Vulkan
- **License**: BSD 3-Clause
- **Repo**: https://github.com/google/dawn
- **Local path**: `../dawn`

### wgpu-native (Rust + C API) — Alternative

The implementation that powers Firefox. Written in Rust, exposed via C API.

- **C API via `webgpu.h`**: Same standard header, usable from C++
- **Extra backend**: OpenGL ES fallback (Dawn doesn't have this)
- **Smaller binary**: Less Chromium heritage overhead
- **Build**: Cargo (Rust), can produce a static/shared C library
- **License**: MIT / Apache 2.0
- **Repo**: https://github.com/gfx-rs/wgpu-native

### Convergence

Both implementations are converging on the shared `webgpu.h` C header standard
(https://github.com/webgpu-native/webgpu-headers). Code targeting this API can
theoretically swap between Dawn and wgpu-native, though minor differences remain.

## How It Would Work

### Architecture

```
Rnd (abstract)
  └─ NgRnd
       └─ WgpuRnd (new)
            ├─ WGPUInstance → WGPUAdapter → WGPUDevice
            ├─ WGPUSwapChain (presentation)
            ├─ WGPURenderPipeline (per material/shader combo)
            ├─ WGPUBuffer (vertex, index, uniform)
            ├─ WGPUTexture + WGPUSampler
            ├─ WGPUBindGroup (resource binding)
            ├─ WGPUCommandEncoder → WGPURenderPassEncoder
            └─ WGSL shaders (compiled from GLSL via Naga or Tint)
```

### API Mapping

| Milo / D3D9 Concept | WebGPU Equivalent |
|---------------------|-------------------|
| `IDirect3DDevice9` | `WGPUDevice` |
| `SetRenderState()` (mutable) | `WGPURenderPipeline` (immutable, cached) |
| `SetTexture()` / `SetStreamSource()` | `WGPUBindGroup` |
| `CreateVertexBuffer()` | `wgpuDeviceCreateBuffer()` |
| `CreateTexture()` | `wgpuDeviceCreateTexture()` |
| `DrawIndexedPrimitive()` | `wgpuRenderPassEncoderDrawIndexed()` |
| `BeginScene()` / `EndScene()` | `wgpuCommandEncoderBeginRenderPass()` / `End()` |
| `Present()` | `wgpuSwapChainPresent()` (or `wgpuSurfacePresent()`) |
| Render targets | `WGPUTexture` as color attachment in render pass |
| Occlusion queries | `WGPUQuerySet` with `OCCLUSION` type |
| Compute shaders | `WGPUComputePipeline` + `wgpuComputePassEncoder` |

### Shader Strategy

Milo has ~12 fixed shader types. Each gets a WGSL shader:

**Option A: Write WGSL directly**
```wgsl
@vertex
fn vs_main(@location(0) position: vec3f,
           @location(1) normal: vec3f,
           @location(2) uv: vec2f) -> VertexOutput {
    var out: VertexOutput;
    out.position = uniforms.mvp * vec4f(position, 1.0);
    out.uv = uv;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(diffuse_tex, diffuse_sampler, in.uv);
}
```

**Option B: Write GLSL, cross-compile to WGSL**
- Use **Naga** (Rust, 30x faster than GLSLang): GLSL → WGSL
- Use **Tint** (C++, part of Dawn): SPIR-V → WGSL
- Use bgfx's shaderc pipeline: GLSL → HLSL → SPIR-V → Tint → WGSL

Option A is recommended for new code. Only ~12 shader programs need writing.

### Web Deployment (Emscripten)

```bash
# Build for web
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web

# Key flags
-sUSE_WEBGPU=1           # Enable WebGPU bindings
-sALLOW_MEMORY_GROWTH    # Dynamic memory
--preload-file assets/    # Bundle game assets
```

**Main loop requirement**: Browsers don't support blocking `while` loops. Must use:
```cpp
emscripten_set_main_loop_arg(main_loop_callback, &app_state, 0, true);
```

This is a real architectural requirement — the engine's main loop needs to be
refactored into a callback that runs one frame per invocation. The Milo engine
likely has a `Poll()` or `Update()` method that can serve this purpose.

## macOS Performance

WebGPU maps near 1:1 to Metal on macOS. Apple's assessment at WWDC 2025:
"Metal-level performance" in Safari.

Both Dawn and wgpu-native use Metal as the backend on macOS. There is no intermediate
translation layer (unlike DXVK which goes D3D9 → Vulkan → MoltenVK → Metal).

Safari 26 ships WebGPU on macOS Tahoe, iOS 26, iPadOS 26, and visionOS 26 with:
- HDR canvas support
- `shader-f16` extension (half-precision floats)
- WebXR integration on Vision Pro

## Pros

- **Only approach that covers macOS + Web + Linux + Windows natively**
- **Designed for Metal**: Near-zero overhead on macOS (Apple co-designed the API)
- **All major browsers**: Chrome, Firefox, Safari all ship WebGPU
- **Modern API**: Explicit control, compute shaders, render bundles
- **Standard C API**: `webgpu.h` works from C++ with no bindings layer
- **Occlusion queries supported**: Unlike sokol
- **Compute shaders**: Available on all native backends AND in browsers (unlike WebGL)
- **Two implementations**: Dawn (C++) and wgpu-native (Rust/C) — no vendor lock-in
- **Production-grade**: Chrome ships WebGPU to billions of users
- **Future-proof**: WebGPU is the successor to WebGL, actively developed

## Cons

- **Full renderer rewrite required**: No D3D9 compatibility — must write `WgpuRnd`
  from scratch against the WebGPU API
- **Immutable pipeline state**: Like Vulkan/Metal, all render state baked into pipeline
  objects. Milo's per-draw state changes need a pipeline cache.
- **Dawn build system**: GN/Ninja from Chromium ecosystem, not straightforward CMake.
  Integration requires effort. (wgpu-native is simpler to build.)
- **Main loop refactor for web**: Must convert blocking main loop to callback-based
  for Emscripten. Non-trivial but doable.
- **WGSL is new**: Smaller ecosystem than GLSL. No preprocessor (`#define`/`#ifdef`).
  Need build-time processing for shader variants.
- **No D3D9 or OpenGL backend**: Can't run on very old hardware. (wgpu-native has an
  OpenGL ES fallback; Dawn does not.)
- **Descriptor/bind group management**: Different from D3D9's slot-based binding.
  Requires architectural decisions about bind group layout.

## Effort Estimate

| Component | Effort |
|-----------|--------|
| Dawn/wgpu-native integration + build system | 1-2 weeks |
| Window + swap chain + basic rendering | 1-2 weeks |
| Pipeline cache (material → pipeline mapping) | 1-2 weeks |
| Buffer management (vertex, index, uniform) | 1 week |
| Texture loading + bind groups | 1 week |
| All 12 shader types in WGSL | 2-3 weeks |
| Render pass management (scene, shadows, post-proc) | 1-2 weeks |
| UI rendering (2D overlay) | 1 week |
| Emscripten/web build pipeline | 1-2 weeks |
| Main loop refactor for web | 3-5 days |
| **Total** | **~12-16 weeks** |

## Comparison: WebGPU vs bgfx vs sokol

| Feature | WebGPU (Dawn) | bgfx | sokol |
|---------|--------------|------|-------|
| macOS Metal | Excellent (1:1 mapping) | Has CPU sync bugs | Good |
| Web (browser) | All browsers ship it | WebGL only (no WebGPU in browser) | WebGL2 + WebGPU working |
| Occlusion queries | Yes | Yes | No |
| Compute shaders (web) | Yes | No (WebGL limitation) | WebGPU only |
| Shader system | WGSL (or cross-compile) | Custom .sc cross-compiler | sokol-shdc |
| Codebase size | Large (Dawn from Chromium) | 85k LOC | 50k LOC |
| Shipped games | Chrome itself | Minecraft, FM2018, etc. | Indie titles |
| API style | Explicit (Vulkan-like) | Declarative (views) | Explicit (pipelines) |
| Build integration | Complex (Dawn) / Easy (wgpu) | Medium (GENie/CMake) | Trivial (headers) |

## When to Choose WebGPU

Choose WebGPU if:
- **macOS is a first-class target** — WebGPU was co-designed by Apple
- **Web deployment is a real goal** — only option with compute shaders in browsers
- **You want one API for native + web** — no separate WebGL/Vulkan code paths
- **Future-proofing matters** — WebGPU is the active standard, WebGL is frozen

Don't choose WebGPU if:
- You need to ship in < 3 months (the rewrite is substantial)
- You need to support very old hardware (no OpenGL fallback in Dawn)
- You want a proven game-engine-level abstraction (bgfx has more shipped titles)

## Verdict

**Strongest candidate for this project's requirements.** WebGPU is the only option
that natively covers macOS (Metal), web (all browsers), Linux (Vulkan), and Windows
(D3D12) from a single API. The renderer rewrite is comparable in effort to bgfx or
sokol — you're writing a new `Rnd` subclass regardless. But with WebGPU, you write it
once and it genuinely runs everywhere, including in a browser tab.

The combination of **Dawn** for native C++ development and **Emscripten +
emdawnwebgpu** for web compilation provides the most complete cross-platform story
of any option evaluated.

## Learning Resources

- [Learn WebGPU for C++](https://eliemichel.github.io/LearnWebGPU/) — comprehensive
  tutorial covering both Dawn and wgpu-native
- [WebGPU Fundamentals](https://webgpufundamentals.org/) — web-focused tutorial
- [Chrome WebGPU docs](https://developer.chrome.com/docs/web-platform/webgpu/build-app)
- [webgpu-cross-platform-app](https://github.com/beaufortfrancois/webgpu-cross-platform-app)
  — Google's official CMake + Emscripten template
- [WebGPU Spec](https://www.w3.org/TR/webgpu/) — W3C specification
- [WGSL Spec](https://www.w3.org/TR/WGSL/) — shader language specification
- [Dawn GitHub](https://github.com/google/dawn)
- [wgpu-native GitHub](https://github.com/gfx-rs/wgpu-native)
- [webgpu-headers](https://github.com/webgpu-native/webgpu-headers) — shared C API
