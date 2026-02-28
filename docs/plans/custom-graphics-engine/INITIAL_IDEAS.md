# Native Port: Initial Ideas

## Context

The DC3 decompilation is functionally complete. The C++ source is correct and matching
the original Xbox 360 binary. Two paths exist for leveraging this work:

1. **Hybrid linking** — produce a valid Xbox 360 XEX (in progress)
2. **Native port** — compile the decompiled code for modern platforms (Linux, Mac, Windows)
   using modern graphics, input, and motion capture libraries

This document captures initial brainstorming for approach #2.

## The Core Question

How do we get Dance Central 3 running natively on modern hardware without rewriting
the ~1,200 files of correct, tested game logic?

## What We Have

The Milo engine (shared between Rock Band and Dance Central) already has **good platform
separation**:

- **File naming convention**: `*_Xbox.cpp` / `*_Xbox.h` files contain all platform-specific code
- **Build system is file-list based** (`objects.json`), not `#ifdef` — swap platforms by
  swapping which files compile
- **Virtual base classes** at every platform boundary:
  - `Rnd` → `NgRnd` → `DxRnd` (rendering)
  - `File` (abstract) → `AsyncFile_Win` (I/O)
  - `Joypad` (abstract) → `Joypad_Xbox` (input)
  - `PlatformMgr` → `PlatformMgr_Xbox` (OS services)
- **Only ~50 core files** are Xbox-specific out of 1,000+ total
- The engine originally shipped on PS2, PS3, Wii, Xbox 360, and PC — multi-platform
  abstractions are baked in

## What Needs Replacing

| Subsystem | Current (Xbox 360) | Scope | Difficulty |
|-----------|-------------------|-------|------------|
| **Graphics** | D3D9 via `rnddx9/` | ~48 files | Hard |
| **Audio** | XAudio2 via `synth_xbox/` | ~39 files | Medium |
| **Motion Capture** | Kinect NUI via `gesture/` + `xdk/nui/` | ~42 files | Hard (ML) |
| **Input** | XInput via `Joypad_Xbox` | ~34 files | Easy |
| **Threading** | Win32 (`CritSec`, `ThreadCall`) | ~82 files (mostly wrappers) | Easy |
| **File I/O** | Xbox content packages | ~13 files | Easy |
| **Networking** | Xbox Live sessions | ~4 files | Medium |
| **Video** | Bink (proprietary codec) | ~20 files | Medium |

## What Stays Untouched

All of this compiles as-is once types are correct:

- **Game logic** (`src/lazer/` — 179 files): scoring, choreography, menus, progression
- **Character system** (`src/system/char/`): animation, IK, blending, lip sync
- **Object system** (`src/system/obj/`): DataArray scripting, message dispatch, serialization
- **Math library** (`src/system/math/`): vectors, matrices, quaternions
- **UI framework** (`src/system/ui/`): panels, labels, lists
- **World system** (`src/system/world/`): scene graph, transforms
- **MIDI/Music** (`src/system/midi/`): song parsing, beat tracking
- **Symbol system** (`src/system/symb/`): string interning, hash tables

## Rendering Approaches Considered

### Low-Level: Write a New Renderer

1. **Raw OpenGL 4.x** — simplest to get pixels on screen, well-understood, but
   maintenance mode with no new features. Good for prototyping.

2. **Raw Vulkan** — future-proof, maximum control, but extremely verbose API.
   Months of work to get a basic renderer running.

3. **DXVK-Native** — present a D3D9 API surface that translates to Vulkan internally.
   Proven in production (Valve Linux ports). Minimizes renderer rewrite since you keep
   the D3D9 call pattern.

### Mid-Level: Graphics Abstraction Libraries

4. **bgfx** — cross-platform rendering abstraction (Vulkan, OpenGL, Metal, D3D11/12).
   Used by Minecraft Bedrock. 16.8k GitHub stars. Has its own shader cross-compiler.
   Supports occlusion queries, render targets, multi-threaded submission.

5. **sokol_gfx** — lighter-weight alternative. Single-header C library. Clean API but
   missing occlusion queries and has only experimental Vulkan. Better for simpler projects.

### High-Level: Use an Existing Engine

6. **Godot Engine** — use as a rendering backend via GDExtension. C++ game logic compiles
   into a shared library, Godot handles rendering via RenderingServer API. 107k GitHub
   stars, mature cross-platform support. Audio latency is a concern for rhythm games.

7. **Bevy Engine** — Rust-based ECS engine. Fundamental architectural mismatch with Milo's
   OOP design. No C FFI for renderer. API breaks every 3 months. Not recommended.

### Community: Leverage Existing Work

8. **YARG / MiloHax ecosystem** — YARG is Unity-based and shares no code with Milo.
   MiloEditor can parse `.milo` files but has no renderer. No one has built an open-source
   Milo scene renderer. The RB3 decomp (Wii) is 54% complete. These are useful references
   but not drop-in solutions.

## Non-Rendering Subsystem Ideas

### Motion Capture (Kinect Replacement)
- **MediaPipe Pose Landmarker** — 33 body landmarks from any webcam, 30+ FPS on CPU,
  comparable accuracy to Kinect V2 in studies. C++ API via Bazel build. Strong candidate.
- **MoveNet** — TensorFlow-based, 17 keypoints, lighter weight
- **OpenPose** — older, heavier, more accurate for multi-person

### Audio
- **miniaudio** — single-header C library, cross-platform, low-latency
- **SDL2_mixer** — simple but limited
- **FMOD** — commercial, excellent latency, used by many rhythm games
- **OpenAL Soft** — open-source, 3D audio, moderate complexity

### Input
- **SDL2 GameController** — maps cleanly to XInput, broad device support
- **hidapi** — raw HID access for Rock Band peripherals
- **ALSA/CoreMIDI** — platform MIDI for USB instruments

### Threading
- **std::thread / std::mutex** — C++11 standard, drop-in for Win32 threading
- **SDL2 threading** — if already using SDL2

### Video
- **FFmpeg/libavcodec** — decode Bink (or re-encode to VP9/H.264)
- **Bink SDK** — RAD Game Tools offers a free decoder for open-source projects

## Strategic Questions

1. **Do we rewrite the renderer or translate D3D9 calls?**
   - Rewrite (new `VkRnd` or `GlRnd`): cleaner, more control, more work
   - Translate (DXVK-Native): less work, but Xbox 360 D3D9 != PC D3D9 (eDRAM, tiling, etc.)

2. **Do we use an engine (Godot) or go lower-level?**
   - Engine: faster to ship, but impedance mismatch with Milo's scene graph
   - Lower-level: more control, more work, fits Milo's architecture better

3. **What's the MVP?**
   - Headless engine that loads game data and runs state machines?
   - Software renderer with basic geometry?
   - Full renderer with stub input?

4. **How do we handle Xbox 360 shader bytecode?**
   - Milo has ~12 fixed shader types — rewrite them from scratch in GLSL/SPIR-V
   - Or use XenosRecomp as a reference for what the shaders compute

5. **What's the Kinect replacement timeline?**
   - Can the game be played with controllers first? (some modes might work)
   - MediaPipe integration is a separate, parallel workstream

## Next Steps

- Review the dedicated approach docs in this directory for each option
- Make decisions on the key questions above
- Prototype: get the engine compiling on x86_64 with stubs
- See [PLAN.md](PLAN.md) for the phased roadmap
