# Approach: bgfx / sokol

Two cross-platform graphics abstraction libraries that sit between raw API (OpenGL/
Vulkan) and a full engine (Godot). They provide a unified rendering API that targets
multiple backends.

**Important context**: With macOS and web as critical requirements for this project,
both libraries need to be evaluated on the quality of their Metal and WebGL/WebGPU
backends, not just backend count.

## bgfx

### What It Is

A cross-platform rendering library with 16.8k GitHub stars, 12+ years of development,
and production use in Minecraft (Bedrock Edition), Football Manager, MAME, and dozens
of other shipped games.

**License**: BSD 2-Clause (fully permissive)

### Supported Backends

| Backend | Status | Notes |
|---------|--------|-------|
| Direct3D 11 | Full | Primary Windows backend |
| Direct3D 12 | Full | |
| OpenGL 2.1+ | Full | |
| OpenGL ES 2/3 | Full | |
| Vulkan | Full | |
| Metal | Works, issues | CPU sync bugs documented (see below) |
| WebGL 1/2 | Works | Undocumented build process, no production games |
| WebGPU (Dawn) | **Experimental** | Native-only, does NOT work in browsers yet |
| GNM (PS4) | Licensed devs | |

**Important corrections from earlier research**:
- The D3D9 backend was **removed** — bgfx skips from D3D11 up
- The WebGPU backend was removed in 2023, then reimplemented in late 2025 as a
  "second take" — it is locked to Dawn native only, not browser-compatible
- Metal backend has documented performance issues (see below)

### macOS Metal Issues

bgfx's Metal backend has several documented problems:

- **CPU blocking**: `nextDrawable` blocks the CPU for ~10ms per frame. The vertex
  shader cannot overlap with the fragment shader, resulting in
  `frame_time = VS_time + FS_time` instead of `max(VS_time, FS_time)`.
  ([bgfx #2550](https://github.com/bkaradzic/bgfx/discussions/2550))
- **Memory leaks**: Reported with SDL2 on Metal
  ([bgfx #1269](https://github.com/bkaradzic/bgfx/issues/1269))
- **Incorrect VRAM reporting**: `gpuMemoryMax` / `gpuMemoryUsed` always wrong
  ([bgfx #2693](https://github.com/bkaradzic/bgfx/issues/2693))
- **GPU-driven rendering crashes**: Segfault on Metal
  ([bgfx #1502](https://github.com/bkaradzic/bgfx/issues/1502))
- **vendorId/deviceId always 0**
  ([bgfx #2688](https://github.com/bkaradzic/bgfx/issues/2688))

For a project where macOS is a critical target, these are concerning.

### Web Status (WebGL)

bgfx compiles to WebGL via Emscripten, but:
- **No official documentation** for the web build path
  ([bgfx #2811](https://github.com/bkaradzic/bgfx/issues/2811))
- No production games have shipped with bgfx + WebGL
- Threading issues with some examples
- Canvas initialization API changed across Emscripten versions
- **No compute shaders** (WebGL limitation, not bgfx)
- Feature subset: limited MRT on WebGL 1.0, texture format restrictions

### Web Status (WebGPU)

The WebGPU backend was [reimplemented in late 2025](https://bkaradzic.github.io/posts/webgpu/):
- **Native-only via Dawn** — does not work in browsers
- Cannot use wgpu-native or Emscripten WebGPU
- Occasional rendering failures on macOS
- `webgpu.h` header versioning described as "chaotic"
- **Not production-ready** — estimated 6-12 months from browser viability

### API Design

bgfx uses a **stateless submit model** fundamentally different from D3D9:

```cpp
// D3D9 (current Milo approach):
device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
device->SetTexture(0, myTexture);
device->SetStreamSource(0, myVB, 0, stride);
device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, numTriangles);

// bgfx equivalent:
bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA);
bgfx::setTexture(0, s_texColor, myTexture);
bgfx::setVertexBuffer(0, myVB);
bgfx::submit(viewId, myProgram);
```

Key differences:
- **Views** (up to 256): Each view has its own render target, clear state, and sort
  mode. Multi-pass rendering is done by submitting to different views.
- **Automatic draw call sorting**: bgfx sorts submissions within each view for optimal
  state changes. You don't need to sort your draw calls.
- **Multi-threaded encoding**: Up to 8 encoder threads can record draw calls in parallel.
- **Transient buffers**: Per-frame dynamic geometry (like D3D9's `DrawPrimitiveUP`)
  via `bgfx::allocTransientVertexBuffer`.

### Shader System

bgfx has its own shader cross-compiler called **shaderc**:

- **Write once** in a GLSL-like dialect (`.sc` files)
- Uses macros (`mul()`, `SAMPLER2D()`, `vec3_splat()`) for cross-API compatibility
- A `varying.def.sc` file declares vertex attributes with semantics
- **Cross-compiles to**: GLSL, ESSL, HLSL (SM 3.0-5.0), Metal, SPIR-V, WGSL, PSSL
- Offline compilation: `shaderc -f shader.sc -o shader.bin --type vertex --platform linux`
- WGSL output uses a long pipeline: bgfx GLSL → HLSL → GLSLang → SPIR-V → Tint → WGSL

### How BgfxRnd Would Work

```
Rnd (abstract)
  └─ NgRnd
       └─ BgfxRnd (new)
            ├─ bgfx::init() — selects best backend per platform
            ├─ Views map to Milo's render passes (scene, shadows, post-proc, UI)
            ├─ bgfx::VertexBufferHandle ← RndMesh vertex data
            ├─ bgfx::TextureHandle ← RndTex texture data
            ├─ bgfx::ProgramHandle ← Compiled .sc shaders
            └─ bgfx::submit() per draw call
```

### Key Feature Coverage

| Milo Feature | bgfx Support | Notes |
|-------------|-------------|-------|
| Mesh rendering | Yes | VAO-style vertex layouts |
| Textures (2D, cube) | Yes | All common formats + compressed |
| Render targets / MRT | Yes | `bgfx::createFrameBuffer()` |
| Occlusion queries | Yes | `bgfx::createOcclusionQuery()` |
| Multi-pass rendering | Yes | View system is designed for this |
| Post-processing | Yes | View chaining with fullscreen quads |
| Instanced rendering | Yes | `bgfx::setInstanceDataBuffer()` |
| Compute shaders | Yes | Separate compute pipeline |
| Alpha blending | Yes | Full blend mode support via `setState` |
| Skeletal animation (GPU) | Yes | Uniform arrays for bone matrices |

### Integration

bgfx is a C++ library with a C99-compatible API:

```cmake
# CMakeLists.txt
add_subdirectory(external/bgfx)  # or FetchContent
target_link_libraries(dc3-native bgfx bimg bx)
```

Dependencies: bgfx depends on **bx** (base library) and **bimg** (image loading).
All three are from the same author and build together.

### Pros

- **Most backends on paper**: Vulkan, OpenGL, Metal, D3D11/12, WebGL, WebGPU, PS4
- **Battle-tested on native**: Minecraft Bedrock alone validates it at massive scale
- **Shader cross-compilation**: Write GLSL-ish once, cross-compile to all targets
- **Automatic draw call sorting**: Less work in the renderer
- **Multi-threaded submission**: Up to 8 encoder threads
- **Occlusion queries**: Supported (unlike sokol)
- **Active development**: Regular updates, responsive maintainer
- **BSD license**: Fully permissive

### Cons

- **macOS Metal has real bugs**: CPU sync blocking (~10ms/frame), memory leaks,
  crashes in advanced rendering features. For a macOS-critical project, this is risky.
- **Web story is weak**: WebGL undocumented, WebGPU native-only. No production games
  shipped on web. If web is a goal, bgfx is not the proven path.
- **Different paradigm from D3D9**: Stateless submit vs stateful pipeline. The entire
  `DxRnd` render loop needs restructuring.
- **85k lines of dependency**: Not a small library
- **Build system**: GENie/Premake by default. CMake via community (bgfx.cmake).
- **Custom shader language**: Not pure GLSL or HLSL. Learning curve for `.sc` dialect.
- **Default draw call limit**: 65,536 per frame (configurable).

---

## sokol_gfx

### What It Is

A minimal, single-header C graphics library by Andre Weissflog (floooh). 9.6k GitHub
stars. **Designed from the ground up with WebAssembly as a first-class citizen.** This
design philosophy shows in the quality of its web and macOS support.

**License**: zlib (even more permissive than BSD — zero attribution required)

### Supported Backends

| Backend | Status | Notes |
|---------|--------|-------|
| OpenGL 4.1+ | Full | Desktop |
| OpenGL ES 3.0 / WebGL2 | Full | Mobile + web |
| Direct3D 11 | Full | Windows |
| Metal | Full | macOS, iOS, recently modernized |
| WebGPU | Full | **Working in browsers today** |
| Vulkan | Experimental | Dec 2025, Linux X11 only |

Notable absences: No D3D9, no D3D12, no OpenGL 2.x. Vulkan is experimental.

### Web Support: The Standout Feature

sokol's web story is **far ahead of bgfx's**. Andre Weissflog designed the library
with Emscripten/WASM as a core target, not an afterthought.

**Live browser demos running today**:
- WebGL2: https://floooh.github.io/sokol-html5/ (many demos)
- WebGPU: https://floooh.github.io/sokol-webgpu/ (triangle, texcube, shapes,
  offscreen rendering, instancing, and more)

These are not theoretical — they are playable in your browser right now.

**WebGPU in browsers**: Unlike bgfx (which only works with Dawn native), sokol's
WebGPU backend works in Chrome, Firefox, and Safari via Emscripten. This is because
sokol was designed with the web platform constraints in mind from the beginning.

**Performance benchmarks** (from [floooh's blog](https://floooh.github.io/2023/10/16/sokol-webgpu.html),
M1 Mac at 120Hz):
- WebGL2: ~8,500 draw calls before dropping below target framerate
- WebGPU: ~11,000 draw calls before dropping below target
- Native Metal: ~110,000 draw calls (10-13x faster than web)

The web-to-native gap is ~10-13x regardless of library — this is a fundamental browser
overhead, not a library issue. For DC3-complexity scenes (a handful of skinned
characters, one stage, particles, UI), both WebGL2 and WebGPU are more than sufficient.

### macOS Metal Support

sokol's Metal backend is well-maintained and recently modernized:
- **Feb 2026**: Replaced `MTKView` with `CAMetalLayer` + `CADisplayLink` for better
  macOS/iOS integration
- No documented CPU sync bugs (unlike bgfx's Metal backend)
- Proper Objective-C ARC memory management
- Clean integration with the sokol_app.h windowing layer

### API Design

Modern explicit-state API:

```cpp
// Create immutable pipeline state object (all render state baked in)
sg_pipeline pip = sg_make_pipeline({
    .shader = shd,
    .layout = { .attrs = { { .format = SG_VERTEXFORMAT_FLOAT3 }, ... } },
    .depth = { .compare = SG_COMPAREFUNC_LESS, .write_enabled = true },
    .colors = { { .blend = { .enabled = true, .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA } } },
});

// In render loop:
sg_begin_pass({ .action = { .colors[0] = { .load_action = SG_LOADACTION_CLEAR } } });
sg_apply_pipeline(pip);
sg_apply_bindings(&bind);
sg_draw(0, num_elements, 1);
sg_end_pass();
sg_commit();
```

Key characteristics:
- **No automatic sorting**: Draw order = submission order. You manage your own
  rendering order. This is actually fine — most game renderers sort their own draws.
- **Immutable pipelines**: All render state baked at creation time. Need a pipeline
  cache for varying material configurations (same as WebGPU and Vulkan).
- **Pass-based**: Explicit `sg_begin_pass` / `sg_end_pass` for each render target.
- **Handle-based resources**: Opaque 32-bit handles with generation counters for
  use-after-free detection.

### Shader System

Uses a companion tool **sokol-shdc** with an excellent workflow:

- Write annotated GLSL 450 (Vulkan-style with separate texture/sampler objects)
- sokol-shdc cross-compiles to **all targets natively**:
  - GLSL 410/430 (desktop OpenGL)
  - GLSL ES 300 / WebGL2
  - HLSL SM 5.0 (D3D11)
  - Metal Shading Language
  - **WGSL** (WebGPU)
- Output is a **C header** with embedded shader bytecode and reflection data
- One `.glsl` file → one `.h` file → link and use. No runtime shader compilation.

This is arguably the cleanest shader workflow of any option. For the ~12 Milo shader
types, each gets a `.glsl` file, sokol-shdc produces a header, and you're done.

### Compute Shaders

Added throughout 2025 in multiple milestones
([blog 1](https://floooh.github.io/2025/03/03/sokol-gfx-compute-update.html),
[blog 2](https://floooh.github.io/2025/05/19/sokol-gfx-compute-ms2.html)):

| Backend | Compute Support |
|---------|----------------|
| Metal | Yes |
| D3D11 | Yes |
| OpenGL 4.3+ | Yes |
| WebGPU | Yes |
| WebGL2 | **No** (WebGL2 limitation) |
| Vulkan | Not yet (experimental backend) |

Compute shaders work in browsers via the WebGPU backend but not WebGL2. This is a
platform limitation, not a sokol limitation — WebGL2 doesn't support compute at all.

### How SokolRnd Would Work

```
Rnd (abstract)
  └─ NgRnd
       └─ SokolRnd (new)
            ├─ sg_setup() — selects backend per platform
            ├─ sg_pipeline per material/shader combination (cached)
            ├─ sg_buffer ← RndMesh vertex/index data
            ├─ sg_image ← RndTex texture data
            ├─ sg_shader ← Pre-compiled from sokol-shdc headers
            ├─ sg_begin_pass() per render pass
            └─ sg_draw() per draw call
```

### The Occlusion Query Question

sokol does **not** support occlusion queries. This is explicitly listed as a design
omission — the author considers them outside sokol's scope of "minimal but complete."

**Is this actually a blocker for DC3?** It depends on whether Milo's renderer uses
occlusion queries for gameplay-critical visibility culling or just as an optimization.
If it's an optimization, we can skip it — DC3 scenes are simple enough that brute-force
rendering is fine on modern hardware. If it's gameplay-critical (e.g., determining
whether a UI element is visible behind geometry), we'd need an alternative approach
(CPU-side raycasting, or bounding box checks).

This needs investigation in the existing `DxRnd` code to determine if occlusion
queries are used and what for.

### Companion Libraries

sokol is part of a family of single-header C libraries that work together:

| Header | Purpose | Relevance |
|--------|---------|-----------|
| `sokol_gfx.h` | GPU rendering | Core — the renderer |
| `sokol_app.h` | Window, input, main loop | Handles the Emscripten callback model |
| `sokol_audio.h` | Audio output | Could replace part of the audio subsystem |
| `sokol_fetch.h` | Async file loading | Useful for web asset loading |
| `sokol_time.h` | High-resolution timer | Cross-platform timing |
| `sokol_log.h` | Logging | Debug output |
| `sokol_glue.h` | Glue between app and gfx | Context passing |
| `sokol_imgui.h` | Dear ImGui integration | Debug UI overlay |

`sokol_app.h` is particularly valuable because it already handles the
`emscripten_set_main_loop` callback model — your app provides a frame callback
and sokol handles the platform-specific main loop. This solves one of the trickiest
parts of web deployment.

### Pros

- **Web-first design**: Working browser demos in both WebGL2 and WebGPU today.
  This is proven, not aspirational.
- **Clean macOS Metal**: Recently modernized, no documented CPU sync issues
- **Minimal footprint**: ~50k lines, single headers, smallest WASM binary size
- **Excellent shader workflow**: sokol-shdc produces self-contained C headers with
  cross-compiled shaders for all backends
- **Clean API**: ~200 functions. Easy to understand the entire library.
- **sokol_app.h handles web main loop**: The Emscripten callback architecture is
  built in — you don't need to restructure your main loop manually
- **zlib license**: Zero attribution required
- **Compute shaders**: Supported on Metal, D3D11, GL, and WebGPU
- **Active development**: Regular updates, thoughtful design blog posts

### Cons

- **No occlusion queries**: Explicitly excluded from scope. Potential blocker
  depending on how Milo uses them.
- **Single-threaded only**: No multi-threaded command recording. For DC3-complexity
  scenes this is likely fine, but limits scalability.
- **No automatic draw call sorting**: You manage render order yourself.
- **Experimental Vulkan**: Only tested on Intel Meteor Lake, Linux X11. Not ready
  for production Vulkan.
- **No D3D12**: Windows support is D3D11 only. Unlikely to matter for DC3.
- **Smaller ecosystem**: 9.6k stars vs bgfx's 16.8k. Fewer examples and community
  resources.
- **No console support**: No PS4/Xbox backends.
- **Indie track record only**: No AAA games shipping with sokol (though several
  commercial Steam games do).

---

## Comparison for DC3 Port

Given that **macOS and web are critical requirements**:

| Criterion | bgfx | sokol | Winner |
|-----------|------|-------|--------|
| **macOS Metal quality** | CPU sync bugs, memory leaks, crashes | Clean, recently modernized | **sokol** |
| **Web (WebGL2)** | Works but undocumented, no shipped games | Working demos, web-first design | **sokol** |
| **Web (WebGPU browser)** | Native-only, not in browsers | Working in browsers today | **sokol** |
| **Occlusion queries** | Yes | No | **bgfx** |
| **Multi-threaded submit** | Yes (8 encoders) | No | **bgfx** |
| **Shader workflow** | Good (.sc cross-compiler) | Excellent (sokol-shdc → .h) | **sokol** |
| **Web main loop** | Manual Emscripten integration | sokol_app.h handles it | **sokol** |
| **WASM binary size** | Larger (~85k LOC C++) | Smaller (~50k LOC C) | **sokol** |
| **Compute (web)** | No (WebGL only, no browser WebGPU) | Yes (WebGPU backend works) | **sokol** |
| **Production track record** | Minecraft, FM2018, many others | Indie/Steam titles | **bgfx** |
| **Platform count** | More (D3D12, PS4, D3D9) | Fewer (no D3D12, no consoles) | **bgfx** |
| **Dependency weight** | Medium (~85k LOC, 3 libs) | Low (~50k LOC, 1 header) | **sokol** |

## Recommendation (Updated)

**For a project where macOS and web are critical, sokol is the stronger choice
between these two.**

The original recommendation favored bgfx based on feature count and production
track record. But with the updated requirements:

1. **bgfx's macOS Metal backend has real bugs** (CPU blocking, memory leaks) that
   would need to be fixed or worked around. sokol's Metal backend is clean.
2. **bgfx's web story is aspirational**, not proven. sokol has working browser demos
   in both WebGL2 and WebGPU today.
3. **sokol_app.h solves the web main loop problem** that every web-targeting engine
   must handle. bgfx leaves this to you.
4. The occlusion query gap needs investigation — if Milo doesn't use them (or uses
   them only as an optimization), it's not a blocker.

### sokol vs WebGPU (Dawn)

See [APPROACH_WEBGPU.md](APPROACH_WEBGPU.md) for the WebGPU-native approach. The
key tradeoff:

| Aspect | sokol | WebGPU (Dawn) |
|--------|-------|---------------|
| Abstraction level | Higher (sokol handles backends) | Lower (you talk to GPU API) |
| Backend selection | Automatic per platform | You pick (Metal/Vulkan/D3D12) |
| Occlusion queries | No | Yes |
| Shader workflow | sokol-shdc (excellent) | Write WGSL or cross-compile |
| WebGL2 fallback | Built-in | Not available (Dawn has no GL backend) |
| Build complexity | Drop-in headers | Dawn build system (GN/Ninja) |
| Learning curve | Low (~200 functions) | Medium (more explicit API) |

**If occlusion queries are needed**: WebGPU (Dawn) is the better choice — it has
them and covers all the same platforms.

**If occlusion queries are not needed**: sokol is simpler, has a cleaner web
deployment story, and its shader tooling is best-in-class.

Both are viable. The deciding factor is whether Milo actually uses occlusion queries.

## References

### bgfx
- [bgfx GitHub](https://github.com/bkaradzic/bgfx)
- [bgfx API Reference](https://bkaradzic.github.io/bgfx/bgfx.html)
- [bgfx Overview](https://bkaradzic.github.io/bgfx/overview.html)
- [bgfx Shader Tools](https://bkaradzic.github.io/bgfx/tools.html)
- [bgfx.cmake](https://github.com/bkaradzic/bgfx.cmake) — CMake integration
- [bgfx examples](https://bkaradzic.github.io/bgfx/examples.html)
- [bgfx WebGPU second take (Jan 2026)](https://bkaradzic.github.io/posts/webgpu/)
- [bgfx Metal CPU blocking (#2550)](https://github.com/bkaradzic/bgfx/discussions/2550)
- [bgfx Metal memory leak (#1269)](https://github.com/bkaradzic/bgfx/issues/1269)
- [bgfx Emscripten docs missing (#2811)](https://github.com/bkaradzic/bgfx/issues/2811)

### sokol
- [sokol GitHub](https://github.com/floooh/sokol)
- [sokol-shdc docs](https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md)
- [sokol WebGL2 samples (live)](https://floooh.github.io/sokol-html5/)
- [sokol WebGPU samples (live)](https://floooh.github.io/sokol-webgpu/)
- [sokol WebGPU blog post](https://floooh.github.io/2023/10/16/sokol-webgpu.html)
- [sokol Vulkan backend blog](https://floooh.github.io/2025/12/01/sokol-vulkan-backend-1.html)
- [sokol compute update (Mar 2025)](https://floooh.github.io/2025/03/03/sokol-gfx-compute-update.html)
- [sokol compute milestone 2 (May 2025)](https://floooh.github.io/2025/05/19/sokol-gfx-compute-ms2.html)
