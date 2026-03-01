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

### Phase 0: Compile on x86_64 (Foundation) — COMPLETE

**Goal**: Get the entire codebase compiling with Clang/GCC on x86_64-linux. No
runtime functionality needed — just a linking binary.

**Milestone**: WebGPU rendering proven — headless triangle rendered via Dawn on
RTX 3090 (Vulkan backend). See `native/` directory and [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md).

**Status**: All items complete. Binary compiles, links, and boots. Completed
across Sessions 1-2 (Feb 2026).

**Work items**:
- [x] Dawn builds and installs (`../dawn/install/Release/`)
- [x] `native/CMakeLists.txt` finds Dawn, builds `dc3-native`
- [x] Headless offscreen rendering to PNG (800x600 green triangle)
- [x] Create `CMakeLists.txt` build system for x86_64 — `native/CMakeLists.txt`
  compiles all ~874 .cpp files from `src/` with Clang, `HX_NATIVE` define
- [x] Write Win32 compatibility shim headers:
  - [x] `DWORD`, `HANDLE`, etc. — already in `xdk/win_types.h`
  - [x] `RTL_CRITICAL_SECTION` → pthread_mutex_t wrapper in `xdk/XBOXKRNL.h`
  - [x] `CreateThread` — guarded with `#ifdef HX_NATIVE` (skip threading, run sync)
  - [x] `CreateEventA` / `SetEvent` / `WaitForSingleObject` — POSIX impls in stubs
  - [x] `VirtualAlloc` / `VirtualFree` → mmap/munmap in `native/src/platform/`
  - [x] Timer → `std::chrono` in `native/src/platform/`
- [x] Create stub implementations for all `*_Xbox.cpp` files:
  - [x] `Rnd_Stub.cpp` (no-op renderer)
  - [x] `Synth_Stub.cpp` (no-op audio)
  - [x] `Joypad_Stub.cpp` (no-op input)
  - [x] `PlatformMgr_Stub.cpp`
  - [x] `Memcard_Stub.cpp`, `ContentMgr_Stub.cpp`
  - [x] `GestureMgr_Stub.cpp` (no-op Kinect)
- [x] Handle MSVC-specific C++ extensions:
  - [x] `__declspec(selectany)` → `__attribute__((weak))` in `macros.h`
  - [x] `__forceinline` → `__attribute__((always_inline))` in `macros.h`
  - [x] SEH — already mapped to try/catch in `macros.h`
  - [x] MSVC pragmas → GCC/Clang equivalents in `macros.h`
- [x] Handle endianness: Already abstracted in `Endian.h` — byte swaps
  happen on load, works as-is for LE x86_64
- [x] Fix LP64 type model issues (see [PORTING_ANALYSIS.md](PORTING_ANALYSIS.md#lp64-issues))
- [x] Non-virtual thunk stubs for Itanium ABI (`native/src/thunk_stubs.cpp`)

**Deliverable**: `cd native/build && cmake --build . -j$(nproc)` produces a
binary that boots through archive loading, config parsing, and .milo object loading.

### Phase 1: Headless Engine + Standalone Viewer (Parallel Tracks)

Two workstreams run simultaneously:

#### Track A: Headless Engine (MVP) — IN PROGRESS (Sessions 3-4)

**Goal**: Engine main loop runs, loads game data from `.ark` archives, processes
DataArray scripts, instantiates game objects.

**Status**: Engine boots through archive loading, config parsing, SystemInit,
.milo object loading (Tex, Font, Text, etc.), subsystem inits (Flow/Char/World/Ham),
and enters `TheUI->Init()`. Major blockers resolved: ChunkStream infinite loop,
RndTex/Font stream desync, iterator/pointer compatibility (605 call sites),
ObjOwnerPtr null deref, Font3d vtable corruption (Itanium ABI key function).
Current blocker: stream desync during `PreloadSharedSubdirs()` — object reads
garbage revision from .milo stream (likely caused by broken vtable stubs for
44 classes in engine_stubs_generated.cpp).

**Work items**:
- [x] Implement native `File` / `AsyncFile` using POSIX I/O
- [x] Implement native `CritSec` / `ThreadCall` / `SynchronizationEvent`
- [x] Implement native `Timer` using `std::chrono`
- [x] Implement native `MemMgr` (redirects to malloc/free)
- [x] Load `.ark` archives and decompress files — 6,377 files, 10 ark files load
- [x] Boot `SystemPreInit()` — archive loading, DTA config parsing
- [x] Boot `SystemInit()` — subsystem initialization
- [x] Fix ChunkStream infinite loop (RndTex::PreLoad/PostLoad consuming stream data)
- [x] Fix Font loading (real RndFontBase::Load implementation)
- [x] Fix iterator/pointer compat (patched `__normal_iterator` in shadow stl_iterator.h)
- [x] Fix ObjOwnerPtr null deref (RefOwner null check)
- [x] Implement CachedRead for RndMesh face/vertex loading
- [x] Boot through subsystem inits (FlowInit, CharInit, WorldInit, HamInit)
- [ ] Implement remaining stubbed ::Load functions for full object parsing
- [ ] Boot through to main loop
- [ ] Add a test harness that feeds scripted commands

**Deliverable**: Engine boots, loads a song, game state machine advances through
states. Logged output shows object creation and state transitions.

#### Track B: Standalone Milo Viewer — COMPLETE

**Goal**: A lightweight standalone app that loads `.milo` scene files and renders
them — without the full game runtime. Faster iteration on the rendering pipeline.

**Status**: Fully operational. Loads `.milo_xbox` files from CLI, renders meshes
with materials and lighting, supports headless screenshot mode. Batch script
generates gallery of 17 props. See `archive/screenshots/` for rendered output.

**Work items**:
- [x] Load `.milo` archive, parse `ObjectDir` hierarchy
- [x] Extract and render `RndMesh` (vertex/index data → GPU)
- [x] Xbox 360 compressed vertex unpacking (BE floats, 10-10-10-2 normals, packed RGBA)
- [x] Apply `RndMat` materials (diffuse color, blend modes, cull, z-mode)
- [x] Auto-frame camera from mesh bounding box
- [x] Headless screenshot mode (`--screenshot output.ppm`)
- [x] Batch screenshot script (`native/scripts/render_screenshots.sh`)
- [x] Extract and display `RndTex` (textures — GPU upload + UV coords working for all vertex formats)
- [ ] Display `RndTransformable` hierarchy (bone/transform tree)
- [ ] Scrub animations (`RndTransAnim`, `RndMeshAnim`)
- [ ] Inspect materials and shader properties

**Deliverable**: Open a `.milo` file, see the 3D scene rendered with material
colors and directional lighting. Batch-render galleries of props.

**Why a viewer first**: Building the renderer against a simple viewer (load file →
render) is dramatically faster to iterate on than booting the full game engine. It
isolates rendering bugs from game logic bugs. Once the viewer renders scenes
correctly, the same rendering code plugs into the full engine.

### Phase 1.5: Asset Pipeline — PARTIALLY COMPLETE

**Goal**: Offline conversion of Milo assets to standard formats, plus runtime loading
of native Milo formats. Both paths supported.

**Decision update**: Runtime Milo loading proved viable first — the decomp's own
loaders work on x86_64 with LP64 fixes. No offline converter needed for MVP.

#### Offline Conversion (Development / Prototyping)

- [ ] Build `milo2gltf` converter: `.milo` → glTF 2.0 (meshes, skeleton, animations)
- [ ] Build texture converter: Xbox 360 tiled textures → PNG or KTX2
- [ ] Build material metadata exporter: `RndMat` properties → JSON
- [ ] Integrate with viewer: load converted assets for fast iteration
- [ ] Reference: MiloLib (C#) documents the .milo format; our decomp has original loaders

**Benefits**: Standard formats are loadable by any renderer, debuggable in Blender/
RenderDoc, and don't require Xbox 360-specific deswizzling at runtime.

#### Runtime Milo Loading (Full Fidelity) — WORKING

- [x] Port the existing Milo loaders from the decomp (they're in the C++ source)
- [x] Add Xbox 360 texture deswizzling (`TextureConvert.cpp` — byte-swap, untile, DXT decompress)
- [x] Handle endianness in binary Milo data (BE → LE conversion on load via BinStream)
- [x] Load `.ark` archives natively (6,377 files, 10 ark files)
- [x] Implement `CachedRead` for bulk binary loading with byte-swap
- [x] Xbox 360 compressed vertex unpacking (36-byte packed format → GPU vertices)

**Benefits**: Full fidelity, no asset conversion step, no lossy format changes.

**Status**: Runtime loading is the primary path. `.milo_xbox` files load directly
via the engine's `DirLoader` → `ObjDirPtr<ObjectDir>::LoadFile()`. Meshes, materials,
textures, and transforms all load correctly. Verified with 17+ prop files.

### Phase 2: Rendering (Pixels on Screen) — IN PROGRESS

**Goal**: Visual output. Characters, stages, UI visible and animating.

**Architecture decision**: Implement `WgpuRnd` directly against the existing `Rnd`
virtual interface using Dawn (`../dawn`). No extra abstraction layers for MVP.
Structure the implementation so a command recording layer could be inserted later
(keep draw calls going through a small number of methods that could become recording
points).

**Status (Tier 1 MVP)**: Mesh rendering operational. Props from `.milo_xbox` files
render with material colors, directional lighting, and proper geometry. Both
uncompressed and Xbox 360 compressed vertex formats supported. Pipeline cache,
ring buffer uniforms, sampler cache, bind group management all working. 17 props
rendered to `archive/screenshots/` as visual proof.

**Work items**:
- [x] Implement `WgpuRnd` subclass using `webgpu.h` / `webgpu_cpp.h`
- [x] Window creation and surface (GLFW, windowed + headless modes)
- [x] Mesh rendering (`RndMesh` → GPU vertex/index buffers, compressed vertex unpack)
- [x] Material system (`RndMat` → shader uniforms, blend states, pipeline cache)
- [x] Camera (`RndCam` → view/projection matrices, orbit camera in viewer)
- [x] Basic lighting (`RndEnviron` ambient color + single directional light)
- [x] Write standard.wgsl shader (diffuse + ambient + fog + alpha test)
- [x] Texture loading (`RndTex` → GPU textures, DXT1/3/5 byte-swap + untile + decompress)
- [ ] Skinned mesh rendering (bone transforms, vertex skinning shader)
- [ ] Multi-light support (read lights from `RndEnviron`)
- [ ] Additional shader types (emissive, environment map, etc.)
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

**Decision**: Hybrid — `#ifdef HX_NATIVE` in shared source, plus separate build targets.

- CMake (`native/CMakeLists.txt`) for native, Ninja for decomp matching — coexist
- Shared source uses `#ifdef HX_NATIVE` for LP64 fixes and platform guards
- Platform-specific code lives in `native/src/platform/` (CMake only)
- Decomp Ninja build is unaffected (HX_NATIVE not defined)

This works well in practice. The ifdefs are concentrated in a handful of files
(types.h, BinStream.h, Object.h, CharClip.cpp) and are necessary for LP64 safety.

### Q5: What about endianness?

**Decision**: Byte-swap on load — already works.

The decomp's existing `BinStream` and `Endian.h` infrastructure handles endian
conversion via `ReadEndian`. All file formats (.milo, .ark, DataArray) store BE.
The swap layer works transparently on LE x86_64 — no offline conversion needed.

### Q6: What's the minimum viable "playable" state?

For a dance game, "playable" requires:
1. Rendering (see characters, stage, UI)
2. Audio (hear the music, synchronized)
3. Motion capture (body tracking with scoring)

Without any one of these, the game isn't really playable in its intended form.
But controller-based menu navigation and non-dance modes could work with just
rendering + audio + input (no motion capture).

### Q7: Build system for the native port?

**Decision**: CMake.

`native/CMakeLists.txt` compiles all ~874 source files with Clang, links against
Dawn (WebGPU), pthreads, zlib, dl. Uses `-Wno-*` flags liberally to suppress
MSVC-ism warnings. Coexists with the decomp Ninja build.

### Q8: What about the DataArray scripting system?

**Answered**: Works as-is with minor LP64 fixes.

- BE serialization: handled by BinStream endian layer (transparent)
- Pointer-size: needed `DataNode(unsigned int)` constructor for LP64 (u32 ≠ int)
- Platform-specific scripts: not hit yet (config DTA files parse and execute fine)

The DTA parser/interpreter boots cleanly on x86_64, loads ham_preinit_keep.dta,
macros.dta, sfx_macros.dta, and all other config files without issues.

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
| [PORTING_ANALYSIS.md](PORTING_ANALYSIS.md) | Codebase analysis for x86_64 port |
