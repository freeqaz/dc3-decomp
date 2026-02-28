# Approach: Bevy Engine

## Overview

Use Bevy, a Rust-based ECS game engine, as a rendering and platform backend.

**Bevy version**: 0.18 (January 2026)
**License**: MIT / Apache 2.0 (dual-licensed)
**GitHub stars**: ~38,000

## Why Consider Bevy?

Bevy is one of the most talked-about game engines in the open-source community.
It offers modern rendering (wgpu-based), a clean ECS architecture, and an active
development community. The question is whether it fits this specific project.

## How It Would Work (Theoretically)

```
┌──────────────────────────────────────┐
│            Bevy Engine (Rust)        │
│  ┌────────────────────────────────┐  │
│  │  bevy_render (wgpu backend)    │  │  ← Vulkan / D3D12 / Metal / WebGPU
│  │  bevy_window, bevy_input       │  │
│  └────────────────────────────────┘  │
└─────────────────┬────────────────────┘
                  │ C FFI bridge (does not exist)
                  │
┌─────────────────▼────────────────────┐
│       Milo Engine Port (C++)         │
│  Game logic, scene graph, etc.       │
└──────────────────────────────────────┘
```

The C++ game logic would need to call into Bevy's rendering API via a C FFI layer.
This layer does not currently exist and would need to be written from scratch.

## Fundamental Problems

### 1. Language Barrier

Bevy is written in Rust. The DC3 codebase is ~1,200 files of C++. Integration options:

- **C FFI (`extern "C"`)**: Write `#[no_mangle] extern "C"` wrapper functions around
  every Bevy API you need. This means writing and maintaining a substantial Rust glue
  layer. No existing solution for this.
- **cxx crate**: Provides safer C++/Rust interop but adds build complexity and still
  requires explicit bridging for every type.
- **Separate processes + IPC**: Run Bevy as a rendering server communicating via shared
  memory. Adds latency, complexity, and fragility.

There is an [open GitHub issue](https://github.com/bevyengine/bevy/issues/2242) from
2021 asking for documentation on C++ FFI with Bevy. As of 2026, it remains open with
no official solution. Users reported linking errors with glslang libraries.

### 2. Architectural Mismatch: ECS vs OOP

Milo engine uses a deeply object-oriented design:
```
Hmx::Object (base, virtual Handle())
  └─ RndTransformable (transform hierarchy, parent chains)
       └─ RndDrawable (draw method, bounding spheres)
            └─ RndMesh (vertex/index data, materials)

ObjectDir (named object containers, reference counting)
ObjPtr / ObjOwnerPtr (reference-counted smart pointers)
```

Bevy uses ECS (Entity Component System), the philosophical opposite:
```
Entity: just an integer ID (no class, no inheritance)
Components: plain data structs (no virtual methods, no encapsulation)
Systems: free functions that query components (no object.method())
```

Porting Milo's object model to ECS would require **a complete architectural redesign**:
- Every `ObjectDir` → Entity with child entities
- Every `ObjPtr` → Entity reference
- Every virtual `Handle()` → System that queries components
- Every inheritance chain → Composition of components

This is not a translation — it's a rewrite of the core engine architecture.

### 3. API Instability

Bevy breaks its API every ~3 months:
- 0.15 → 0.16: Major breaking changes
- 0.16 → 0.17: Major breaking changes
- 0.17 → 0.18: Major breaking changes

Each version requires migration. Building on Bevy means committing to regular
maintenance as the API shifts under you. For comparison, Godot's 4.x API has been
stable since 2023.

### 4. Maturity Gaps

Bevy 0.18 (as of January 2026) is missing:
- **No editor**: Code-only development, no visual scene inspector
- **Limited animation system**: "Clunky" APIs per core contributors
- **Asset processing**: Only outdated BasisU texture compressor
- **No official UI framework**: For UI-heavy games like Dance Central
- **Sparse documentation**: Especially for rendering internals
- Core contributor assessment: "most gaps could close within 1-2 years"

### 5. No Rendering C API

Bevy's rendering system (`bevy_render`) is built on wgpu (Rust-native GPU abstraction).
The entire rendering pipeline uses Rust generics, traits, and Bevy's ECS:

```rust
// Bevy rendering uses Rust-native types throughout
pub struct RenderDevice { /* wgpu::Device */ }
pub struct RenderGraph { /* DAG of render passes */ }

// No C FFI exists for any of this
```

Exposing this through C FFI would require wrapping every type, every method, every
callback. This is a massive undertaking with no precedent.

## Pros

- **Modern rendering**: wgpu provides Vulkan, D3D12, Metal, WebGPU
- **Active community**: 38k GitHub stars, 1,500+ contributors
- **Performance**: ECS gives excellent data locality and cache performance
- **Rust safety**: Memory safety guarantees (if you're writing in Rust)
- **Open source**: MIT/Apache dual license

## Cons

- **Wrong language**: C++ ↔ Rust FFI is unsolved for Bevy's API surface
- **Architectural clash**: ECS vs OOP requires complete engine rewrite
- **API instability**: Breaking changes every 3 months
- **No editor**: Code-only, no visual debugging
- **Not production-ready**: By the project's own assessment
- **No rendering C API**: Would need to be written from scratch
- **Small shipped game portfolio**: Very few commercially shipped titles

## Effort Estimate

| Component | Effort |
|-----------|--------|
| C FFI wrapper for bevy_render | 4-8 weeks (pioneering work) |
| C FFI wrapper for bevy_window/input | 1-2 weeks |
| Architectural redesign (OOP → ECS) | 8-16 weeks |
| Game logic port to Rust (alternative) | 6-12 months |
| Ongoing API migration (per Bevy release) | 1-2 weeks per release |
| **Total** | **6+ months minimum** |

## Verdict

**Not recommended for this project.**

The combination of wrong language, wrong architecture, immature tooling, and unstable
API makes Bevy a poor fit for porting an existing C++ game engine. Every other option
in this directory is more practical.

Bevy would make sense if:
- You were writing a new game from scratch in Rust
- You wanted ECS architecture from the start
- You were OK with API churn during development
- You didn't need an editor for content iteration

None of these apply to the DC3 port.

### If Bevy Matures

If Bevy reaches 1.0 with a stable API and gains a C FFI for its rendering system,
it could become viable. But that's likely 2+ years away, and other options exist today.

## References

- [Bevy GitHub](https://github.com/bevyengine/bevy)
- [Bevy C++ FFI Issue #2242](https://github.com/bevyengine/bevy/issues/2242)
- [Bevy Fifth Birthday Retrospective](https://jms55.github.io/posts/2025-09-03-bevy-fifth-birthday/)
- [Bevy 0.17→0.18 Migration Guide](https://bevy.org/learn/migration-guides/0-17-to-0-18/)
- [Bevy vs Godot Analysis](https://aircada.com/blog/bevy-vs-godot)
