# Approach: Godot Engine

## Overview

Use Godot Engine as a rendering and platform backend for the DC3 port. The decompiled
C++ game logic compiles into a shared library via GDExtension, and Godot handles
rendering, windowing, and platform abstraction.

**Godot version**: 4.5+ (stable, released September 2025)
**License**: MIT
**GitHub stars**: 107,000+

## How It Would Work

### Architecture

```
┌─────────────────────────────────────────┐
│              Godot Engine               │
│  ┌───────────────────────────────────┐  │
│  │        RenderingServer API        │  │  ← Vulkan / D3D12 / Metal / OpenGL
│  │  mesh_create(), texture_create()  │  │
│  │  instance_create(), scenario()    │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │    Window / Input / OS services   │  │
│  └───────────────────────────────────┘  │
└────────────────────┬────────────────────┘
                     │ GDExtension (.so/.dll)
                     │
┌────────────────────▼────────────────────┐
│        Milo Engine Port (C++)           │
│  ┌───────────────────────────────────┐  │
│  │  GodotRnd : Rnd                   │  │  ← New renderer implementation
│  │    Translates Milo draw calls     │  │
│  │    to RenderingServer API         │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │  Game Logic (unchanged)           │  │
│  │  lazer/, char/, obj/, ui/, etc.   │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │  Audio (FMOD or miniaudio)        │  │  ← Bypass Godot audio
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

### Two Integration Paths

**Path A: RenderingServer (Low-Level)**

Use Godot purely as a GPU abstraction. Bypass the scene tree entirely. Call
`RenderingServer` methods directly from C++ to create meshes, textures, instances.

```cpp
// Pseudocode for GodotRnd mapping
RID mesh = RenderingServer::mesh_create();
RenderingServer::mesh_add_surface_from_arrays(mesh, PRIMITIVE_TRIANGLES, arrays);

RID texture = RenderingServer::texture_2d_create(image);

RID instance = RenderingServer::instance_create();
RenderingServer::instance_set_base(instance, mesh);
RenderingServer::instance_set_scenario(instance, scenario);
RenderingServer::instance_set_transform(instance, transform);
```

This maps to Milo concepts:
| Milo | Godot RenderingServer |
|------|----------------------|
| `RndMesh` | `mesh_create()` + `mesh_add_surface_from_arrays()` |
| `RndTex` | `texture_2d_create()` |
| `RndMat` | `material_create()` + shader params |
| `RndDir` (scene) | `scenario_create()` |
| `RndCam` | `camera_create()` + projection/transform |
| `RndTransformable` | `instance_set_transform()` |
| `RndEnviron` | `environment_create()` + light params |
| `RndFlare` | Custom particle/sprite implementation |

**Path B: Scene Tree (High-Level)**

Map Milo's object hierarchy to Godot nodes. `RndDir` → `Node3D`, `RndMesh` →
`MeshInstance3D`, `RndCam` → `Camera3D`, etc. Godot manages the full scene graph.

This is simpler for rendering but creates a dual scene graph problem — Milo has its
own scene management that would conflict with Godot's.

**Recommendation**: Path A (RenderingServer). It avoids the dual scene graph issue
and gives more control.

## Rendering Backends

Godot 4.5 provides three renderers:

| Renderer | API | Use Case |
|----------|-----|----------|
| **Forward+** | Vulkan, D3D12, Metal | Desktop 3D (recommended) |
| **Mobile** | Vulkan, D3D12, Metal | Simplified forward rendering |
| **Compatibility** | OpenGL ES 3.0 / GL 3.3 | Broad compatibility, web |

All three are available through the same RenderingServer API — your code doesn't
change between them.

## GDExtension Integration

GDExtension allows C++ code to run as a native shared library loaded by Godot:

```cpp
// register_types.cpp
#include <godot_cpp/core/class_db.hpp>

void initialize_dc3(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    ClassDB::register_class<MiloEngine>();
}

extern "C" GDExtensionBool dc3_init(
    GDExtensionInterfaceGetProcAddress p_get_proc_address,
    const GDExtensionClassLibraryPtr p_library,
    GDExtensionInitialization *r_initialization
) {
    // ...
}
```

The entire Milo engine codebase compiles into this library. No Rust, no managed
language — pure C++.

**Build system**: godot-cpp uses SCons by default but CMake is supported via
community builds.

## Pros

- **Cross-platform rendering solved**: Vulkan, D3D12, Metal, OpenGL all handled
- **Mature engine**: 107k GitHub stars, massive community, stable 4.x API
- **Editor available**: Visual debugging, scene inspection, profiling tools
- **Shader system**: Godot Shading Language (GLSL-like) with visual shader editor
- **Built-in features**: Lighting, shadows, post-processing, particle systems,
  physics (Jolt), 2D rendering — all available if needed
- **C++ integration**: GDExtension provides native C++ linking, no interpretation overhead
- **Active development**: Regular releases with rendering improvements
- **Asset pipeline**: Image loading, format conversion, compression

## Cons

### Critical: Audio Latency

This is the **biggest risk** for a rhythm/dance game on Godot.

Documented issues:
- `AudioServer.get_output_latency()` returns zero on some platforms
- 165ms delay reported on mobile
- Output latency setting has no effect in some backends
- No DSP-time scheduling — audio sync depends on frame rate
- Variance is "unavoidable due to audio chunking"

**Mitigation**: Bypass Godot's audio entirely. Use FMOD, miniaudio, or direct
platform audio APIs via GDExtension. Handle audio-visual sync in your own code.
This is doable but adds complexity.

### Impedance Mismatch

Milo engine has its own:
- Scene graph (`ObjectDir` hierarchy)
- Object model (`Hmx::Object` with virtual `Handle()`)
- Serialization (DataArray-based `.milo` files)
- Transform hierarchy (`RndTransformable` parent chains)

Godot has its own versions of ALL of these. Using Godot means either:
- Maintaining two parallel scene graphs (Milo's for logic, Godot's for rendering)
- Replacing Milo's scene graph with Godot's (massive rewrite)

Path A (RenderingServer) keeps Milo's scene graph as the source of truth and only
uses Godot for GPU operations, but you still need to sync transforms, visibility,
and material changes every frame.

### Overhead

You carry the weight of Godot's full engine even though you only use rendering:
- GDScript interpreter, editor hooks, physics engine, navigation, multiplayer
- None of this is used but it's linked and initialized
- Binary size increases significantly

### RenderingServer Limitations

- "The internals are entirely implementation-specific and cannot be accessed" — you
  cannot reach below the RenderingServer abstraction
- Custom render passes require modifying Godot's rendering pipeline (C++ module, not
  GDExtension)
- Some Milo rendering techniques (custom eDRAM tiling, specific blend modes) may not
  map cleanly to Godot's rendering model

## Effort Estimate

| Component | Effort |
|-----------|--------|
| GDExtension project setup + build system | 1-2 weeks |
| GodotRnd: basic mesh rendering via RenderingServer | 2-3 weeks |
| GodotRnd: textures + materials | 1-2 weeks |
| GodotRnd: lighting + environment | 1 week |
| GodotRnd: cameras + viewports | 3-5 days |
| GodotRnd: UI overlay (2D) | 1-2 weeks |
| GodotRnd: post-processing | 1 week |
| Audio bypass (FMOD/miniaudio integration) | 1-2 weeks |
| Milo scene graph → Godot sync layer | 2-3 weeks |
| **Total** | **~12-16 weeks** |

## When Godot Makes Sense

Godot is the right choice if:
- You want **cross-platform with minimal custom rendering code**
- The **editor** is valuable for debugging and iterating on visuals
- You're OK with **bypassing Godot audio** for a custom solution
- **Web deployment** (via Compatibility renderer) is a goal
- The team has **Godot experience** or wants to build it

## When Godot Doesn't Make Sense

Godot is the wrong choice if:
- You need **tight control over the rendering pipeline** (custom render passes,
  exotic blend modes, GPU-driven rendering)
- **Audio latency** must be guaranteed by the engine (not a separate system)
- You want to **minimize dependencies** (Godot is a 200MB+ engine)
- You want **maximum performance** with no overhead from unused engine systems
- The **dual scene graph** complexity is unacceptable

## Verdict

**Viable but heavy-handed.** Godot solves the cross-platform rendering problem
comprehensively but introduces complexity from the impedance mismatch between two
scene graph systems and requires bypassing its audio for a rhythm game.

For this specific project — a decompiled game with its own mature engine architecture
— a lighter-weight rendering library (bgfx, or even raw OpenGL for prototyping) may
be a better fit. Godot makes more sense for a from-scratch reimplementation where you
can build around Godot's architecture rather than fighting it.

## Precedent

Projects that have used Godot as a rendering backend for existing game logic:
- **OpenLiberty** — GTA III reimplementation on Godot
- **UnderworldGodot** — Ultima Underworld recreation
- **Unknown Horizons** — Strategy game reimplementation

These demonstrate the approach works, but they built around Godot's scene tree rather
than using RenderingServer directly.

## References

- [Godot GDExtension C++ Tutorial](https://docs.godotengine.org/en/4.4/tutorials/scripting/gdextension/gdextension_cpp_example.html)
- [godot-cpp repository](https://github.com/godotengine/godot-cpp)
- [RenderingServer API docs](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html)
- [Godot Renderers Overview](https://docs.godotengine.org/en/stable/tutorials/rendering/renderers.html)
- [Godot Internal Rendering Architecture](https://docs.godotengine.org/en/4.4/contributing/development/core_and_modules/internal_rendering_architecture.html)
- [Godot Audio Sync docs](https://docs.godotengine.org/en/stable/tutorials/audio/sync_with_audio.html)
- [Godot Audio Latency Issue #108204](https://github.com/godotengine/godot/issues/108204)
- [Allow Custom Rendering Backends (Proposal #4287)](https://github.com/godotengine/godot-proposals/issues/4287)
