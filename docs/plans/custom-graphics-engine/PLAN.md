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

#### Track A: Headless Engine (MVP) — COMPLETE

**Goal**: Engine main loop runs, loads game data from `.ark` archives, processes
DataArray scripts, instantiates game objects.

**Status**: Engine boots through ALL subsystems, enters main loop, navigates UI
screens automatically via DTA scripts, and runs 5000+ frames stably. Screen
navigation reaches `tutorial_party_mode_screen_1` (stuck on Kinect gesture).
ASan-clean with known suppressions. GTest integration tests verify boot stability.

See [STREAM_DESYNC.md](STREAM_DESYNC.md) for the nested dir detection hack and
defensive guards. See `docs/plans/dc3-native/STATUS.md` for detailed native port
status including error handling strategy and environment variables.

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
- [x] Detect nested ObjectDir DirLoader-format data (peek-and-unreread hack)
- [x] Implement `DrivenPropertyEntry::Load` and `FlowMathOp::Load` (from symmetric Save)
- [x] Add defensive guards for stream desync (rev caps, string size caps, count caps)
- [x] Boot through to main loop (5000+ frames stable)
- [x] Add test harness with scripted input (`MILO_INPUT_SCRIPT`)
- [x] GTest headless boot tests (`native/tests/test_headless_boot.cpp`)
- [x] ASan integration (`cmake -DENABLE_ASAN=ON`)
- [x] MILO_FAIL_DTA macro for non-fatal DTA errors
- [x] NewObject vtable verification via sigsetjmp guard
- [ ] Get past tutorial screens (skip Kinect gesture or DTA override)
- [ ] UI text rendering in headless screenshots

**Deliverable**: Engine boots, game state machine advances through screens.
GTest integration tests verify stability.

#### Track B: Standalone Milo Viewer — COMPLETE

**Goal**: A lightweight standalone app that loads `.milo` scene files and renders
them — without the full game runtime. Faster iteration on the rendering pipeline.

**Status**: Fully operational with full material pipeline. Loads `.milo_xbox` files
from CLI, renders meshes with specular, emissive, rim lighting, intensify, and
multi-directional lighting from environment data. Supports headless screenshot mode
and `--verbose` debug output. Batch script generates gallery of 17 props. Window
title shows loaded filename. See `archive/screenshots/` for rendered output.

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

### Phase 2: Rendering (Pixels on Screen) — IN PROGRESS (~85%)

**Goal**: Visual output. Characters, stages, UI visible and animating.

**Architecture decision**: Implement `WgpuRnd` directly against the existing `Rnd`
virtual interface using Dawn (`../dawn`). No extra abstraction layers for MVP.
Structure the implementation so a command recording layer could be inserted later
(keep draw calls going through a small number of methods that could become recording
points).

**Status**: Full rendering pipeline operational in the **standalone viewer** including
skinned meshes, post-processing, and 2D UI rendering. The **engine rendering path**
has the same GPU code but the scene traversal differs — venue backgrounds don't render
(turbo_shell behind UI is black), UI sprites/icons are missing, text markup tags render
as literals. The gap is in the engine's DrawShowing tree traversal, not the GPU code.

**Work items**:
- [x] Implement `WgpuRnd` subclass using `webgpu.h` / `webgpu_cpp.h`
- [x] Window creation and surface (GLFW, windowed + headless modes)
- [x] Mesh rendering (`RndMesh` → GPU vertex/index buffers, compressed vertex unpack)
- [x] Material system (`RndMat` → shader uniforms, blend states, pipeline cache)
- [x] Camera (`RndCam` → view/projection matrices, orbit camera in viewer)
- [x] Multi-light support (up to 4 directional + 4 point lights with range attenuation)
- [x] Write standard.wgsl shader (half-Lambert diffuse + Blinn-Phong specular + emissive + rim + intensify + fog + alpha test + skin/hair variants)
- [x] Texture loading (`RndTex` → GPU textures, DXT1/3/5 byte-swap + untile + decompress)
- [x] Specular highlights (Blinn-Phong from `BaseMaterial::GetSpecularRGB()`)
- [x] Emissive support (`BaseMaterial::GetEmissiveMultiplier()`)
- [x] Rim lighting (`BaseMaterial::GetRimRGB()`)
- [x] Intensify flag (`BaseMaterial::GetIntensify()` → 2x texture brightness)
- [x] Ring buffer overflow protection (auto-grow)
- [x] GPU resource cleanup (destructor hooks in RndMesh/RndTex)
- [x] Pipeline cache bounds warning (512 entries)
- [x] Skinned mesh rendering (40-bone blending, compressed verts, multi-pass)
- [x] Secondary texture maps (normal, specular, emissive, rim, env cube, detail-normal)
- [x] Post-processing (contrast, chromatic aberration, posterization, vignette, color levels)
- [x] 2D quad rendering (`DrawRect` with texture + gradient colors)
- [x] Text glyph mesh generation (`DrawShowing` → `FontMapBase` → glyph meshes)
- [x] Skin/hair shader variants (half-Lambert + warm shadows, Kajiya-Kay anisotropic)
- [ ] Particle systems (`RndParticleSys`) — cosmetic, not blocking
- [ ] Lines/Flares (`RndLine`, `RndFlare`) — cosmetic, not blocking
- [ ] `RndGroup` draw ordering — may need work for correct layering

**Remaining work**:
- Venue background rendering in engine (turbo_shell scene behind UI)
- RndGroup draw ordering for correct UI/3D layering
- UI sprite/icon rendering (player silhouettes, button prompts)
- Text markup processing (`<alt>` tags)
- Particles, lines, flares (cosmetic)

**Deliverable**: Game renders a venue with characters. Visual fidelity may be rough
but geometry, textures, and animation are correct.

### Phase 2.5: Character Animation Fidelity — IN PROGRESS (~60%)

**Goal**: Characters animate with full fidelity — root motion, twist bones, lip sync,
blinking. Covers the gap between "bones move" and "characters look alive."

**Status**: Root motion (facing bones), twist bone solvers (upper/fore/neck), and full
CharClip dance animation implemented in the standalone viewer. Lip sync, procedural
blinking, and eye gaze not yet started.

**Work items**:
- [x] Twist bone solvers (CharUpperTwist, CharForeTwist fallbacks in viewer)
- [x] Root motion / facing bones (`bone_facing.pos`, `bone_facing.rotz` applied to character transform)
- [x] Neck twist fallback (CharNeckTwist half-yaw algorithm)
- [ ] Lip sync — viseme clip loading, CharFaceServo, CharLipSyncDriver playback.
  See [LIP_SYNC.md](LIP_SYNC.md) for full plan.
- [ ] Procedural blinking (CharFaceServo blink weight timer)
- [ ] CharEyes gaze direction (eye bone targeting)

**Deliverable**: Characters dance with natural root motion, properly twisted limbs,
blinking eyes, and lip-synced mouths during songs.

### Phase 3: Audio — COMPLETE

**Goal**: Music playback synchronized with gameplay. SFX and voice.

**Status**: COMPLETE. Full audio pipeline: FFmpegAudioReader for .bik, VorbisReader for
.ogg/.mogg, miniaudio output device, StreamReceiverNative ring buffer, SampleInstNative
for SFX, DSP effects chain (EQ, compressor, delay, distortion, flanger, chorus, bitcrush,
wah, reverb), configurable audio-visual sync offset. See [AUDIO_SYSTEM.md](AUDIO_SYSTEM.md).

**Work items**:
- [x] Choose audio library → **miniaudio** (header-only, callback-based, cross-platform)
- [x] Bink audio decode → **FFmpegAudioReader** replaces BinkReader (see [VIDEO_PLAYBACK.md](VIDEO_PLAYBACK.md))
- [x] Wire `NativeSynth::NewStreamDecoder()` → FFmpegAudioReader for "bink" type
- [x] Implement `AudioDevice` wrapper around miniaudio (sub-phase 3.1)
- [x] Implement `StreamReceiverNative` — ring buffer → PCM output (sub-phase 3.2)
- [x] Wire `StreamReceiver::sFactory` in Synth init (sub-phase 3.3)
- [x] `SampleInstNative` implemented and wired to `SynthSample::NewInst()` (sub-phase 3.4)
- [x] Audio mixing + volume/pan in AudioDevice callback (sub-phase 3.6)
- [x] OGG/Vorbis streaming wired — `NativeSynth::NewStreamDecoder` creates `VorbisReader` (sub-phase 3.5)
- [x] `NativeSynth::NewStreamFile` opens real files + detects codec from extension
- [x] `NativeSynth::NewStream` creates real `StandardStream` instead of `StreamNull`
- [x] DSP effects chain — `FxSendNative` processes EQ, compressor, delay, distortion, flanger, chorus, bitcrush, wah, reverb via portable DSP classes
- [x] Audio-visual sync — `sAudioOffsetMs` configurable via `synth { audio_offset_ms }`, applied in `StandardStream::GetTime()`

**Deliverable**: Songs play in sync with gameplay. Hit/miss feedback sounds work.

### Phase 4: Input — COMPLETE

**Goal**: Playable with a game controller.

**Status**: Implemented via GLFW (already linked for windowing). Gamepad polling via
`glfwGetGamepadState` + keyboard-as-joypad fallback for testing without a controller.
Keyboard events via GLFW key callback → ring buffer → `KeyboardSendMsg`.

**Work items**:
- [x] Implement `Joypad` backend using GLFW GameController API (`Joypad_Native.cpp`)
- [x] Map GLFW gamepad buttons to Milo's joypad enum (`kPad_X`, `kPad_Circle`, etc.)
- [x] Keyboard-as-joypad fallback (arrows→D-pad, Enter→A, Esc→B, Space→Start, etc.)
- [x] Keyboard input via GLFW callbacks (`Keyboard_Native.cpp`)
- [x] `gNativeWindow` global for input subsystem access to GLFW window
- [x] 19 unit tests covering button mapping, delta logic, stick translation, triggers
- [ ] USB MIDI support for Rock Band instruments (future)

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
- [x] Video playback (Bink → FFmpeg) — see [VIDEO_PLAYBACK.md](VIDEO_PLAYBACK.md)
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
| [LIP_SYNC.md](LIP_SYNC.md) | Lip sync system — viseme loading, CharFaceServo, CharLipSyncDriver |
| [MOTION_CAPTURE.md](MOTION_CAPTURE.md) | Kinect replacement / motion capture |
| [PORTING_ANALYSIS.md](PORTING_ANALYSIS.md) | Codebase analysis for x86_64 port |
| [STREAM_DESYNC.md](STREAM_DESYNC.md) | Stream desync: nested ObjectDir detection, defensive guards |
