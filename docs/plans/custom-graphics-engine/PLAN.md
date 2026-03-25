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

### Phase 0: Foundation — COMPLETE

x86_64 cross-compilation with Clang/GCC, Win32 shim headers, LP64 type fixes, Dawn WebGPU proven. See [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md).

### Phase 1: Headless Engine + Standalone Viewer — COMPLETE

**Track A (Headless Engine)**: Full boot through all subsystems, main loop, UI screen navigation, 5000+ frames stable, GTest integration tests, ASan-clean. See [STREAM_DESYNC.md](STREAM_DESYNC.md) for stream desync guards.

**Track B (Standalone Viewer)**: Full material pipeline (specular, emissive, rim, intensify), skinned meshes, headless screenshots, batch gallery rendering. See `archive/screenshots/`.

### Phase 1.5: Asset Pipeline — COMPLETE (runtime path)

Runtime Milo loading is the primary path (no offline converter needed). `.milo_xbox` files load directly via `DirLoader` with Xbox 360 texture deswizzling, endian conversion, and compressed vertex unpacking. Offline conversion (milo2gltf) remains a future option.

### Phase 2: Rendering — IN PROGRESS (~85%)

**Goal**: Visual output. Characters, stages, UI visible and animating.

`WgpuRnd` implements the `Rnd` virtual interface using Dawn WebGPU. Full pipeline operational in viewer and engine: meshes, materials, textures, cameras, multi-light, skinned animation, post-processing, 2D quads, text glyphs, skin/hair shader variants.

**Remaining work**:
- Venue background rendering in engine (turbo_shell scene behind UI)
- RndGroup draw ordering for correct UI/3D layering
- UI sprite/icon rendering (player silhouettes, button prompts)
- Text markup processing (`<alt>` tags)
- Particles, lines, flares (cosmetic)

### Phase 2.5: Character Animation Fidelity — IN PROGRESS (~60%)

Root motion, twist bone solvers, and CharClip dance animation implemented. Remaining:
- [ ] Lip sync — viseme clip loading, CharFaceServo, CharLipSyncDriver. See [LIP_SYNC.md](LIP_SYNC.md).
- [ ] Procedural blinking (CharFaceServo blink weight timer)
- [ ] CharEyes gaze direction (eye bone targeting)

### Phase 3: Audio — COMPLETE

Full pipeline: FFmpegAudioReader (.bik), VorbisReader (.ogg/.mogg), miniaudio output, StreamReceiverNative ring buffer, SampleInstNative SFX, DSP effects chain, configurable A/V sync offset. See [AUDIO_SYSTEM.md](AUDIO_SYSTEM.md).

### Phase 4: Input — COMPLETE

GLFW gamepad polling + keyboard-as-joypad fallback + GLFW key callbacks. 19 unit tests. USB MIDI support for Rock Band instruments is future work.

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
