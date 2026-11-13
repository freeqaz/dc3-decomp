# Approach: The Forge / Diligent Engine / LLGL

Backup rendering abstraction candidates, evaluated in case bgfx/sokol/WebGPU don't
work out. Researched at the suggestion of an external review.

**TL;DR**: None of these beat our top candidates (WebGPU via Dawn, sokol) for the
DC3 port's specific requirements (macOS + web + C++ + D3D9-era codebase). Each has
a dealbreaker. But they're documented here for completeness.

## The Forge (ConfettiFX)

**GitHub**: https://github.com/ConfettiFX/The-Forge
**Stars**: ~5,500 | **License**: Apache 2.0 (console backends = commercial)

### What It Is

A low-level, D3D12/Vulkan-style rendering framework used in shipped AAA games
including Star Wars: Bounty Hunter, Hades (Supergiant), No Man's Sky (macOS/iOS port),
and Warzone Mobile. The most battle-tested option here.

### Backends

| Backend | Status |
|---------|--------|
| Direct3D 12 | Full |
| Vulkan 1.1 | Full |
| Metal 2 | Full (native, production-proven) |
| WebGPU | Experimental only (separate repo, stale) |
| D3D11 | **Dropped** |
| OpenGL | **None** |
| WebGL | **None** |
| PS4/PS5/Switch | Commercial license |

### Shader System

**FSL (Forge Shading Language)** — an HLSL superset. Write `.fsl` files, a Python
script transpiles to HLSL, GLSL (Vulkan), MSL, and PSSL. Unified resource tables
shared between shader and C++ code. Clean approach.

### macOS

Excellent. Native Metal 2 backend with Apple Silicon support. The No Man's Sky
macOS/iOS port validates this at scale.

### Web

**Dealbreaker.** WebGPU exists as a separate experimental repo that ConfettiFX
found too problematic to integrate ("Dawn didn't follow game development rules").
No WebGL. No production web path.

### Build System

**No CMake.** Custom Python-based build (`PyBuild.py`) + per-platform IDE projects.
The team explicitly refuses CMake support. Community forks (The-Forge-Lite) add
CMake but are unofficial.

### API Complexity

High. This is a D3D12/Vulkan-level explicit API — command buffers, pipeline state
objects, descriptor sets, explicit synchronization. The C99 rewrite (in progress)
is clean but requires deep modern GPU knowledge. Significantly harder to port a
D3D9-era renderer to than bgfx/sokol/WebGPU.

### Verdict

**Not recommended.** The Metal backend is excellent and the AAA track record is
impressive, but: no web support, no CMake, and the API is too low-level for our
use case. This is designed for teams building new AAA renderers from scratch, not
retrofitting a D3D9-era engine.

---

## Diligent Engine

**GitHub**: https://github.com/DiligentGraphics/DiligentEngine
**Stars**: ~4,200 | **License**: Apache 2.0 (**Metal backend = commercial license**)

### What It Is

A modern C++ rendering abstraction with the unique selling point of HLSL as a
universal shading language. API sits between D3D11 and D3D12 in complexity.

### Backends

| Backend | Status |
|---------|--------|
| Direct3D 11 | Full |
| Direct3D 12 | Full |
| OpenGL 4.x | Full |
| OpenGL ES 3.x | Full |
| Vulkan | Full |
| Metal | **Commercial license only** |
| WebGPU | Yes (via Dawn/Emscripten) |
| WebGL | Yes (via OpenGL ES/Emscripten) |

### Shader System

**HLSL as universal language** — the standout feature:
- D3D11/12: HLSL compiled directly by DXC
- Vulkan: HLSL → SPIR-V via glslang (bundled, at runtime)
- OpenGL/ES: HLSL → GLSL via built-in source-to-source converter (has limitations:
  no macros in declarations, structs can't be function arguments)
- Metal: HLSL → MSL (commercial backend only)
- WebGPU: HLSL → WGSL via SPIR-V

This is attractive for a D3D9-era codebase since existing HLSL shaders would need
minimal modification.

### macOS

**Dealbreaker.** The native Metal backend requires a commercial license (undisclosed
pricing). Under the open-source license, macOS support is via MoltenVK (Vulkan → Metal
translation) or deprecated OpenGL 4.1. MoltenVK works but adds overhead and is a
translation layer, not native Metal.

### Web

Good. WebGPU via Dawn (Emscripten) and WebGL via OpenGL ES are both supported and
CI-tested.

### Build System

CMake (3.20+). Clean, standard integration. Best build system story of these three.

### API Complexity

Medium. D3D12-style pipeline state objects but with D3D11-like convenience for
resource binding. Resources classified as Static/Mutable/Dynamic — maps well to
how game renderers think. COM-like interface pattern (`IRenderDevice`, `IDeviceContext`).
Familiar to anyone who's used D3D11.

### Verdict

**Would be the best fit if Metal weren't paywalled.** The HLSL-first shader model
is ideal, the API complexity is manageable, WebGPU works, and CMake integrates
cleanly. But requiring a commercial license for native macOS Metal is a dealbreaker
when macOS is a critical target. MoltenVK is a workaround but not a first-class
solution.

---

## LLGL (Low Level Graphics Library)

**GitHub**: https://github.com/LukasBanana/LLGL
**Stars**: ~2,500 | **License**: BSD-3-Clause

### What It Is

A thin C++11 abstraction over native GPU APIs. The most minimal of the three —
LLGL provides a unified interface but does not abstract away the shader compilation
problem or manage windowing.

### Backends

| Backend | Status |
|---------|--------|
| OpenGL 2.x-4.6 | Full |
| OpenGL ES 3.x | Full |
| Direct3D 11 | Full |
| Direct3D 12 | Full |
| Vulkan | Full |
| Metal | Full (open source) |
| WebGL | Yes (Emscripten + GLES) |
| WebGPU | **None** |

### Shader System

**None.** LLGL is deliberately shader-agnostic. You must provide platform-appropriate
shaders yourself (HLSL for D3D, GLSL for GL, MSL for Metal, SPIR-V for Vulkan).
This means writing 4+ shader variants or integrating a third-party cross-compiler
(SPIRV-Cross, Naga, etc.) yourself.

### macOS

Native Metal backend is open source and included. Less battle-tested than The Forge
but functional. [Web examples exist](https://lukasbanana.github.io/LLGL/docu/WebPage).

### Web

WebGL only (via Emscripten + OpenGL ES). No WebGPU. Future-proofing concern since
WebGL is being superseded by WebGPU.

### Maintenance Risk

**Concerning.** Solo developer project (Laura Hermanns). Only 4 beta releases ever
(0.01-0.04). Sporadic updates. No documented commercial users or shipped games.

### Verdict

**Not recommended.** Free open-source Metal is nice, but: no WebGPU, no shader
cross-compilation, beta status, solo-developer maintenance risk, and no community
to speak of. You'd end up building too much infrastructure yourself.

---

## Comparison Matrix

| Criterion | The Forge | Diligent | LLGL | bgfx | sokol | WebGPU (Dawn) |
|-----------|-----------|----------|------|------|-------|---------------|
| **macOS Metal (free)** | Yes | No (commercial) | Yes | Yes (bugs) | Yes | Yes |
| **WebGPU (browser)** | No | Yes | No | No (native only) | Yes | Yes |
| **WebGL** | No | Yes | Yes | Yes | Yes | No |
| **Shader cross-compile** | FSL | HLSL universal | None | .sc shaderc | sokol-shdc | Write WGSL |
| **Build system** | Custom Python | CMake | CMake | GENie/CMake | Headers | GN/CMake |
| **API complexity** | High | Medium | Medium-High | Low-Medium | Low | Medium |
| **D3D9 porting ease** | Hard | Moderate | Moderate-Hard | Moderate | Moderate | Moderate |
| **Shipped AAA games** | Yes (NMS, Hades) | No | No | Yes (Minecraft) | No | Chrome itself |
| **Maintenance** | Very active | Active | Low | Active | Active | Very active |

## Bottom Line

None of these three dislodge our top candidates:

1. **WebGPU (Dawn)**: macOS + web + desktop, occlusion queries, compute in browser
2. **sokol**: macOS + web + desktop, simplest integration, best web demos
3. **bgfx**: Most backends on native, but macOS Metal bugs and weak web

The Forge has the best Metal and best AAA credentials but no web.
Diligent has the best shader story (HLSL universal) but paywalled Metal.
LLGL is too immature and under-maintained.

If Diligent ever open-sources their Metal backend, it would become a serious
contender due to the HLSL shader workflow. Worth watching.
