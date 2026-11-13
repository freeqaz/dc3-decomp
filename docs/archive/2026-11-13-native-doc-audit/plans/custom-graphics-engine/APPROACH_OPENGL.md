# Approach: OpenGL

## Overview

Write a new `GlRnd` class implementing the `Rnd` / `NgRnd` virtual interface using
OpenGL 4.x (or OpenGL 3.3 for wider compatibility). This is the "simple and direct"
approach — no translation layers, no engines, just a new renderer.

## How It Would Work

### Architecture

```
Rnd (abstract)
  └─ NgRnd (next-gen base)
       └─ GlRnd (new, replaces DxRnd)
            ├─ Uses GLFW or SDL2 for window/context creation
            ├─ OpenGL 4.1+ core profile (or 3.3 for max compat)
            ├─ GLSL shaders (rewritten from Milo shader types)
            └─ Standard VAO/VBO/FBO pipeline
```

### Key Mappings

| Milo Concept | D3D9 (current) | OpenGL equivalent |
|-------------|----------------|-------------------|
| Vertex buffers | `IDirect3DVertexBuffer9` | `glGenBuffers` + `GL_ARRAY_BUFFER` |
| Index buffers | `IDirect3DIndexBuffer9` | `glGenBuffers` + `GL_ELEMENT_ARRAY_BUFFER` |
| Textures | `IDirect3DTexture9` | `glGenTextures` + `glTexImage2D` |
| Render targets | `IDirect3DSurface9` | FBO (`glGenFramebuffers`) |
| Shaders | `IDirect3DVertexShader9` / `PixelShader9` | `glCreateShader` + `glCreateProgram` |
| Render states | `SetRenderState()` | `glEnable`/`glDisable` + `glBlendFunc` etc. |
| Draw calls | `DrawIndexedPrimitive()` | `glDrawElements()` |
| Occlusion queries | `IDirect3DQuery9` | `glGenQueries` + `GL_SAMPLES_PASSED` |

### Shader Strategy

Milo has ~12 fixed shader types. Each gets a GLSL vertex + fragment shader pair:

| Milo Shader | Purpose | GLSL Complexity |
|-------------|---------|-----------------|
| `RndShaderSimple` | Unlit, single texture | Trivial |
| `RndShaderStandard` | Lit, textured, optional normal map | Moderate |
| `RndShaderParticles` | Billboard particles | Simple |
| `RndShaderMultimesh` | Instanced mesh rendering | Moderate |
| `RndShaderPostProc` | Screen-space post-processing | Simple (fullscreen quad) |
| `RndShaderFur` | Shell-based fur rendering | Moderate |
| `RndShaderVelocity` | Motion vector output | Simple |
| `RndShaderDepthVolume` | Depth-based volume effects | Moderate |
| `RndShaderSkinned` | Skeletal animation (GPU skinning) | Moderate |

Total: ~24 shader files (12 VS + 12 FS). This is very manageable.

### Window/Context

Use SDL2 or GLFW for window creation and OpenGL context management:

```cpp
// Pseudocode for GlRnd::Init()
SDL_Init(SDL_INIT_VIDEO);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
mWindow = SDL_CreateWindow("DC3", 1280, 720, SDL_WINDOW_OPENGL);
mContext = SDL_GL_CreateContext(mWindow);
gladLoadGLLoader(SDL_GL_GetProcAddress);
```

## Pros

- **Simplest API to get started with**: OpenGL has immediate-mode-style state setting
  that maps more naturally from D3D9 than Vulkan's explicit pipeline objects
- **Fast prototyping**: Can get triangles on screen in hours, not days
- **Well-understood**: Decades of tutorials, documentation, StackOverflow answers
- **Wide hardware support**: OpenGL 4.1 works on macOS (last supported version),
  OpenGL 3.3 works on nearly everything
- **No external dependencies** beyond a loader (GLAD) and windowing (SDL2/GLFW)
- **Easier debugging**: RenderDoc, apitrace, built-in debug output
- **macOS compatible**: Metal is macOS's future, but OpenGL 4.1 still works today

## Cons

- **Maintenance mode**: Khronos has stopped adding features to OpenGL. No new
  extensions since ~2017. Vulkan is the future.
- **Driver quality varies**: OpenGL drivers on different vendors (especially AMD on
  Windows, Intel on older hardware) have different bugs and performance characteristics
- **Single-threaded submission**: OpenGL contexts are inherently single-threaded.
  Multi-threaded rendering requires shared contexts (fragile) or Vulkan.
- **No explicit memory management**: Can't control GPU memory allocation, which limits
  optimization opportunities
- **Global state machine**: Easy to leak state between draw calls. Requires discipline
  with state tracking or a state cache.
- **macOS deprecation**: Apple deprecated OpenGL in macOS 10.14 (2018). It still works
  but receives no updates and will eventually be removed.
- **Performance ceiling**: OpenGL's implicit synchronization and driver overhead mean
  it will always be slower than Vulkan for CPU-bound scenarios. For DC3 (a 2012 game),
  this is unlikely to matter.

## Effort Estimate

| Component | Effort |
|-----------|--------|
| Window + context setup | 1-2 days |
| Basic mesh rendering | 1 week |
| All 12 shader types | 2-3 weeks |
| Texture loading + deswizzle | 1 week |
| Render targets + post-processing | 1 week |
| Material system (blend, cull, depth) | 1 week |
| Lighting (RndEnviron) | 3-5 days |
| UI (2D overlay rendering) | 1 week |
| **Total** | **~8-10 weeks** |

## Verdict

**Good for prototyping, questionable as final target.**

OpenGL is the fastest path to seeing pixels on screen. It's the right choice for a
proof-of-concept renderer to validate that the engine port works. But for a shipping
product, the deprecation on macOS and lack of future investment are concerns.

**Recommended strategy**: Start with OpenGL to prove out the rendering pipeline, then
migrate to bgfx (which has an OpenGL backend) or Vulkan once the architecture is
validated. The shader work (GLSL) transfers directly.

## References

- [OpenGL 4.6 Reference](https://registry.khronos.org/OpenGL-Refpages/gl4/)
- [Learn OpenGL](https://learnopengl.com/)
- [GLAD loader generator](https://glad.dav1d.de/)
- [SDL2 OpenGL guide](https://wiki.libsdl.org/SDL2/SDL_GLattr)
