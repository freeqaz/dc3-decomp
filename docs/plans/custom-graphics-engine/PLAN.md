# Native Port: Plan

**Goal: Get Dance Central 3 running on modern platforms.**

Master plan for porting DC3 to Linux, macOS, Windows, and web. The endgame is
the full game playable — characters dancing, music playing, body tracking scoring.
See [INITIAL_IDEAS.md](INITIAL_IDEAS.md) for context and brainstorming.

## The Big Goal

**Ship a playable Dance Central 3.** Everything in this plan serves that goal.
The decomp gives us the complete game source. The native port makes it run on
hardware people actually own. Every phase below is a step toward:

1. **See the game** — rendering (WebGPU)
2. **Hear the game** — audio (songs, SFX)
3. **Play the game** — input (controller) + motion capture (webcam)

We already have the decomp running in xenia. This port removes the emulator.

## Decision: WebGPU via Dawn

**Rendering framework**: WebGPU via [Dawn](https://github.com/google/dawn) (Google's
C++ WebGPU implementation, the same one that powers Chrome).

**Dawn source**: `../dawn` (cloned from `git@github.com:google/dawn.git`)

**Why Dawn**: Only approach that natively covers macOS (Metal 1:1), web browsers
(all ship WebGPU since Nov 2025), Linux (Vulkan), and Windows (D3D12) from a single
`webgpu.h` C API. See [APPROACH_WEBGPU.md](APPROACH_WEBGPU.md) for full rationale.

**Runner-up**: sokol_gfx was the #2 option — web-first design, working browser demos,
clean Metal backend, simplest integration. Missing occlusion queries was the tiebreaker.
See [APPROACH_BGFX_SOKOL.md](APPROACH_BGFX_SOKOL.md). If Dawn proves too heavy or
its build system is too painful, sokol is the fallback.

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                  Game Logic (unchanged)              │
│  lazer/ (179 files), char/, obj/, ui/, world/,      │
│  midi/, math/, symb/, meta/                         │
└──────────────────────┬──────────────────────────────┘
                       │ calls into
┌──────────────────────▼──────────────────────────────┐
│              Platform Abstraction Layer              │
│  Rnd (virtual), File (virtual), Joypad (virtual),   │
│  PlatformMgr, Synth, GestureMgr, NetworkSocket      │
└──────────────────────┬──────────────────────────────┘
                       │ new implementations
┌──────────────────────▼──────────────────────────────┐
│              Native Platform Backends               │
│  ┌─────────┐ ┌──────────┐ ┌───────┐ ┌───────────┐  │
│  │Graphics │ │  Audio   │ │ Input │ │  Motion   │  │
│  │(WebGPU/ │ │(miniaudio│ │(SDL2) │ │(MediaPipe)│  │
│  │ Dawn)   │ │ or FMOD) │ │       │ │           │  │
│  └─────────┘ └──────────┘ └───────┘ └───────────┘  │
│  ┌─────────┐ ┌──────────┐ ┌───────┐                │
│  │Threading│ │ File I/O │ │ Video │                │
│  │(std::)  │ │ (POSIX)  │ │(FFmpeg│                │
│  │         │ │          │ │      )│                │
│  └─────────┘ └──────────┘ └───────┘                │
└─────────────────────────────────────────────────────┘
```

## Phased Roadmap

### Phase 0: Compile on x86_64 (Foundation)

**Goal**: Get the entire codebase compiling with Clang/GCC on x86_64-linux. No
runtime functionality needed — just a linking binary.

**Milestone**: WebGPU rendering proven — headless triangle rendered via Dawn on
RTX 3090 (Vulkan backend). See `native/` directory and [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md).

**Work items**:
- [x] Dawn builds and installs (`../dawn/install/Release/`)
- [x] `native/CMakeLists.txt` finds Dawn, builds `dc3-native`
- [x] Headless offscreen rendering to PNG (800x600 green triangle)
- [ ] Create `CMakeLists.txt` (or Meson) build system for x86_64
- [ ] Write Win32 compatibility shim headers:
  - `DWORD`, `HANDLE`, `HRESULT`, `BOOL`, `LONG`, `LPCSTR`, etc.
  - `RTL_CRITICAL_SECTION` → `std::recursive_mutex`
  - `CreateThread` → `std::thread`
  - `CreateEventA` / `SetEvent` / `WaitForSingleObject` → condition variables
  - `VirtualAlloc` / `VirtualFree` → `mmap` / `munmap`
  - `LARGE_INTEGER` / `QueryPerformanceCounter` → `std::chrono`
- [ ] Create stub implementations for all `*_Xbox.cpp` files:
  - `Rnd_Stub.cpp` (no-op renderer)
  - `Synth_Stub.cpp` (no-op audio)
  - `Joypad_Stub.cpp` (no-op input)
  - `PlatformMgr_Stub.cpp`
  - `Memcard_Stub.cpp`, `ContentMgr_Stub.cpp`
  - `GestureMgr_Stub.cpp` (no-op Kinect)
- [ ] Handle MSVC-specific C++ extensions:
  - `__declspec(selectany)` → `__attribute__((weak))`
  - `__forceinline` → `__attribute__((always_inline))`
  - SEH (`__try`/`__except`) → POSIX signals or remove
  - MSVC pragmas → GCC/Clang equivalents
- [ ] Handle endianness: Xbox 360 is big-endian PPC, x86 is little-endian.
  File formats and network protocols may assume BE byte order.

**Deliverable**: `cmake --build build/ && ./dc3-native` produces a binary that
exits cleanly.

### Phase 1: Headless Engine + Standalone Viewer (Parallel Tracks)

Two workstreams run simultaneously:

#### Track A: Headless Engine (MVP)

**Goal**: Engine main loop runs, loads game data from `.ark` archives, processes
DataArray scripts, instantiates game objects.

**Work items**:
- [ ] Implement native `File` / `AsyncFile` using POSIX I/O
- [ ] Implement native `CritSec` / `ThreadCall` / `SynchronizationEvent`
- [ ] Implement native `Timer` using `std::chrono`
- [ ] Implement native `MemMgr` (can simplify — use standard allocator)
- [ ] Load `.ark` archives and decompress files
- [ ] Boot `SystemInit()` → `AppInit()` → main loop
- [ ] Add a test harness that feeds scripted commands

**Deliverable**: Engine boots, loads a song, game state machine advances through
states. Logged output shows object creation and state transitions.

#### Track B: Standalone Milo Viewer

**Goal**: A lightweight standalone app that loads `.milo` scene files and renders
them — without the full game runtime. Faster iteration on the rendering pipeline.

**Work items**:
- [ ] Load `.milo` archive, parse `ObjectDir` hierarchy
- [ ] Extract and render `RndMesh` (vertex/index data → GPU)
- [ ] Extract and display `RndTex` (textures, with Xbox 360 deswizzle)
- [ ] Apply `RndMat` materials (diffuse, blend modes)
- [ ] Display `RndTransformable` hierarchy (bone/transform tree)
- [ ] Scrub animations (`RndTransAnim`, `RndMeshAnim`)
- [ ] Inspect materials and shader properties
- [ ] Compare against captured screenshots/footage from the original game

**Deliverable**: Open a `.milo` file, see the 3D scene, inspect bones and materials.
First visual validation of the rendering pipeline.

**Why a viewer first**: Building the renderer against a simple viewer (load file →
render) is dramatically faster to iterate on than booting the full game engine. It
isolates rendering bugs from game logic bugs. Once the viewer renders scenes
correctly, the same rendering code plugs into the full engine.

### Phase 1.5: Asset Pipeline

**Goal**: Offline conversion of Milo assets to standard formats, plus runtime loading
of native Milo formats. Both paths supported.

#### Offline Conversion (Development / Prototyping)

- [ ] Build `milo2gltf` converter: `.milo` → glTF 2.0 (meshes, skeleton, animations)
- [ ] Build texture converter: Xbox 360 tiled textures → PNG or KTX2
- [ ] Build material metadata exporter: `RndMat` properties → JSON
- [ ] Integrate with viewer: load converted assets for fast iteration
- [ ] Reference: MiloLib (C#) documents the .milo format; our decomp has original loaders

**Benefits**: Standard formats are loadable by any renderer, debuggable in Blender/
RenderDoc, and don't require Xbox 360-specific deswizzling at runtime.

#### Runtime Milo Loading (Full Fidelity)

- [ ] Port the existing Milo loaders from the decomp (they're in the C++ source)
- [ ] Add Xbox 360 texture deswizzling (reference: xenia `texture_info.cc`)
- [ ] Handle endianness in binary Milo data (BE → LE conversion on load)
- [ ] Load `.ark` archives natively

**Benefits**: Full fidelity, no asset conversion step, no lossy format changes.

**Decision**: Use offline conversion during early development for faster iteration.
Add runtime Milo loading for full fidelity once the renderer is stable. Ship with
both paths available.

### Phase 2: Rendering (Pixels on Screen)

**Goal**: Visual output. Characters, stages, UI visible and animating.

**Architecture decision**: Implement `WgpuRnd` directly against the existing `Rnd`
virtual interface using Dawn (`../dawn`). No extra abstraction layers for MVP.
Structure the implementation so a command recording layer could be inserted later
(keep draw calls going through a small number of methods that could become recording
points).

**Work items**:
- [ ] Implement `WgpuRnd` subclass of `NgRnd` using `webgpu.h` / `webgpu_cpp.h`
- [ ] Window creation and swap chain (SDL2 or sokol_app)
- [ ] Mesh rendering (`RndMesh` → GPU vertex/index buffers)
- [ ] Texture loading (`RndTex` → GPU textures, deswizzled or pre-converted)
- [ ] Material system (`RndMat` → shader uniforms, blend states, pipeline cache)
- [ ] Rewrite ~12 shader programs in WGSL
- [ ] Lighting (`RndEnviron` → light uniforms)
- [ ] Camera (`RndCam` → view/projection matrices)
- [ ] Post-processing (bloom, etc.)
- [ ] UI rendering (2D overlays, text)

**Deliverable**: Game renders a venue with characters. Visual fidelity may be rough
but geometry, textures, and animation are correct.

### Phase 3: Audio

**Goal**: Music playback synchronized with gameplay. SFX and voice.

**Work items**:
- [ ] Choose audio library (miniaudio, FMOD, SDL2, OpenAL)
- [ ] Implement `Synth` backend for chosen library
- [ ] Implement `Voice` (sample playback with pitch/volume control)
- [ ] Implement audio streaming from `.ark` (songs are typically Vorbis or custom format)
- [ ] DSP effects chain (reverb, EQ) — may stub initially
- [ ] Audio-visual sync (critical for a rhythm game — need sub-frame accuracy)
- [ ] Bink audio decode (for cutscenes)

**Deliverable**: Songs play in sync with gameplay. Hit/miss feedback sounds work.

### Phase 4: Input

**Goal**: Playable with a game controller.

**Work items**:
- [ ] Implement `Joypad` backend using SDL2 GameController API
- [ ] Map SDL2 buttons to Milo's joypad enum (`kPad_X`, `kPad_Circle`, etc.)
- [ ] USB MIDI support for Rock Band instruments (ALSA on Linux, CoreMIDI on Mac)
- [ ] Keyboard input for menu navigation

**Deliverable**: Navigate menus and play controller-compatible game modes.

### Phase 5: Motion Capture (Kinect Replacement)

**Goal**: Full Dance Central gameplay with body tracking from a webcam.

**Work items**:
- [ ] Integrate MediaPipe Pose Landmarker (or chosen ML framework)
- [ ] Create `LiveCameraInput_Webcam.cpp` that feeds skeleton data to `GestureMgr`
- [ ] Map MediaPipe's 33 landmarks to Kinect's 20-joint skeleton format
- [ ] Calibrate coordinate spaces (MediaPipe uses normalized coords, Kinect uses meters)
- [ ] Validate gesture filters work with webcam skeleton data
- [ ] Latency optimization (webcam capture → ML inference → game logic)
- [ ] Handle multi-player (MediaPipe supports multi-person)

**Deliverable**: Dance gameplay works with a standard webcam. Scoring is functional.

### Phase 6: Polish and Platform Support

- [ ] macOS support (Metal via WebGPU/sokol — should work from Phase 2 if using
  cross-platform renderer)
- [ ] Windows support (D3D12 via WebGPU or D3D11 via sokol)
- [ ] Web build (Emscripten + WebGPU — see [APPROACH_WEBGPU.md](APPROACH_WEBGPU.md))
- [ ] Save/load game progress
- [ ] DLC content loading
- [ ] Network play (if desired — could stub or remove)
- [ ] Video playback (Bink → FFmpeg, or re-encode to modern codec)
- [ ] Performance optimization
- [ ] Optional: Add command recording IR for frame capture/regression testing

---

## Open Questions

### Q1: Which rendering approach?

The biggest decision. See dedicated docs for each option:

| Approach | Doc | macOS | Web | Pros | Cons |
|----------|-----|-------|-----|------|------|
| **WebGPU (Dawn)** | [APPROACH_WEBGPU.md](APPROACH_WEBGPU.md) | Metal (1:1) | All browsers | Only option for macOS+Web+Desktop | Full rewrite, Dawn build complexity |
| sokol | [APPROACH_BGFX_SOKOL.md](APPROACH_BGFX_SOKOL.md) | Metal | WebGL2+WebGPU working | Web-first, minimal, proven browser demos | No occlusion queries |
| bgfx | [APPROACH_BGFX_SOKOL.md](APPROACH_BGFX_SOKOL.md) | Metal (CPU bugs) | WebGL only (no WebGPU in browser) | Most backends, Minecraft uses it | macOS Metal issues, weak web story |
| OpenGL 4.x | [APPROACH_OPENGL.md](APPROACH_OPENGL.md) | 4.1 max (deprecated) | No | Simple, fast prototype | Dead end, Apple removing it |
| Vulkan + DXVK-Native | [APPROACH_VULKAN.md](APPROACH_VULKAN.md) | No (Linux only) | No | Minimal D3D9 rewrite | No macOS, no web |
| Godot | [APPROACH_GODOT.md](APPROACH_GODOT.md) | Yes | Compatibility renderer | Full engine, huge community | Impedance mismatch, audio latency |
| Bevy | [APPROACH_BEVY.md](APPROACH_BEVY.md) | Yes (wgpu) | Yes (wgpu) | Modern | Wrong language, architectural clash |
| YARG ecosystem | [APPROACH_YARG_COMMUNITY.md](APPROACH_YARG_COMMUNITY.md) | N/A | N/A | Community knowledge | No renderer exists |

**Decision**: **WebGPU via Dawn** (`../dawn`). See top of this document.

sokol_gfx is the #2 fallback if Dawn's build system proves too painful.

All other options were ruled out:
- DXVK-Native: no macOS, no web
- bgfx: macOS Metal CPU sync bugs, WebGPU native-only (not in browsers)
- Godot: impedance mismatch, audio latency for rhythm games
- Bevy: wrong language (Rust), wrong architecture (ECS)
- The Forge: no web, no CMake
- Diligent: Metal backend paywalled
- LLGL: no WebGPU, under-maintained

Key insight from research: **no D3D9 translation layer supports both macOS and web**.
All paths require a renderer rewrite. Since we're rewriting anyway, target the API
with the widest reach.

### Q2: What's the shader strategy?

Options:
- **Rewrite from scratch**: Milo has ~12 fixed shader types. Write GLSL/SPIR-V versions
  based on material properties (`RndMat`, `MatShaderOptions`). Most practical.
- **Translate from Xenos microcode**: Use XenosRecomp or xenia's shader translator as
  reference. Over-engineered for ~12 shaders.
- **Use bgfx shaderc**: Write once in bgfx's GLSL dialect, cross-compile to all targets.

### Q3: How do we handle Xbox 360 texture formats?

Xbox 360 textures use a proprietary tiled memory layout (32x32 pixel tiles). Options:
- Write a deswizzler (reference: xenia `texture_info.cc`, `texture_extent.cc`)
- Convert all game assets offline to standard formats (lossy for compressed textures)
- Load textures at runtime with deswizzle step

### Q4: Do we keep the decomp matching while building the port?

Options:
- **Separate build targets**: CMake for native, existing Ninja for decomp matching.
  Two builds coexist. Native build may diverge from matching source.
- **`#ifdef HX_NATIVE`**: Same source files compile for both targets. Keeps decomp
  matching but pollutes source with ifdefs.
- **Fork**: Branch the codebase. Native port evolves independently. Decomp matching
  is no longer a concern.

**Current leaning**: Separate build targets in the same repo. Platform-specific code
lives in new files (e.g., `Rnd_Vulkan.cpp`), CMake selects them. Decomp Ninja build
is unaffected.

### Q5: What about endianness?

Xbox 360 is big-endian. All file formats (.milo, .ark, DataArray) store data in BE.
Options:
- Byte-swap on load (add endian-aware read functions)
- Convert assets offline to LE
- Both: convert what you can offline, swap the rest at runtime

### Q6: What's the minimum viable "playable" state?

For a dance game, "playable" requires:
1. Rendering (see characters, stage, UI)
2. Audio (hear the music, synchronized)
3. Motion capture (body tracking with scoring)

Without any one of these, the game isn't really playable in its intended form.
But controller-based menu navigation and non-dance modes could work with just
rendering + audio + input (no motion capture).

### Q7: Build system for the native port?

Options:
- **CMake**: Industry standard, well-supported by IDEs, handles cross-platform well
- **Meson**: Simpler syntax, good cross-compilation, what DXVK uses
- **Premake**: Generates IDE projects (VS, Xcode, Make)
- **Keep Ninja**: Modify `configure.py` to support a native target alongside Xbox

### Q8: What about the DataArray scripting system?

DataArray (.dta files) is Milo's scripting language. It's embedded in `.ark` archives.
The interpreter is part of the engine code and should work as-is on x86_64, but:
- Does it assume big-endian serialization?
- Are there pointer-size assumptions (32-bit Xbox vs 64-bit host)?
- Some scripts reference platform-specific resources

---

## Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Xbox 360 D3D9 too different from PC D3D9 for DXVK | High | Medium | Write new renderer instead of translating |
| Endianness bugs in file loading | Medium | High | Comprehensive byte-swap layer |
| 32-bit pointer assumptions in serialization | High | Medium | Audit all `Load()`/`Save()` code paths |
| MediaPipe latency too high for dance gameplay | Medium | Low | MediaPipe runs at 30+ FPS on CPU alone |
| Shader rewrite takes longer than expected | Medium | Medium | Start with basic shaders, iterate |
| Audio sync drift in rhythm gameplay | High | Medium | Use dedicated audio lib, not engine-level timing |
| `.ark` archive format has undocumented quirks | Medium | Low | Existing `Archive` class handles decompression |
| Milo scene files have Xbox-specific data | Medium | Medium | Handle per-format during loading |

---

## File Index

| File | Contents |
|------|----------|
| [INITIAL_IDEAS.md](INITIAL_IDEAS.md) | Brainstorming and initial analysis |
| [PLAN.md](PLAN.md) | This file — master plan and open questions |
| [APPROACH_WEBGPU.md](APPROACH_WEBGPU.md) | **Recommended** — WebGPU via Dawn/wgpu-native |
| [APPROACH_BGFX_SOKOL.md](APPROACH_BGFX_SOKOL.md) | bgfx and sokol comparison |
| [APPROACH_OPENGL.md](APPROACH_OPENGL.md) | OpenGL rendering approach |
| [APPROACH_VULKAN.md](APPROACH_VULKAN.md) | Vulkan / DXVK-Native approach |
| [APPROACH_GODOT.md](APPROACH_GODOT.md) | Godot Engine approach |
| [APPROACH_BEVY.md](APPROACH_BEVY.md) | Bevy Engine approach (not recommended) |
| [APPROACH_FORGE_DILIGENT_LLGL.md](APPROACH_FORGE_DILIGENT_LLGL.md) | The Forge, Diligent Engine, LLGL (backup options) |
| [APPROACH_YARG_COMMUNITY.md](APPROACH_YARG_COMMUNITY.md) | YARG and community project survey |
| [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) | Build instructions + step-by-step walkthrough |
| [MOTION_CAPTURE.md](MOTION_CAPTURE.md) | Kinect replacement / motion capture |
