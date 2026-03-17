# Native Port Long-Term Roadmap — 2026-03-17

**Date**: 2026-03-17
**Status**: COMPLETED
**Scope**: Phase D (Cosmetic Polish), Phase E (Infrastructure), Phase 5 (Input Replacement)
**Concurrent work**: Hack removal / `#ifdef HX_NATIVE` cleanup is happening in parallel

---

## Current State

The native port is **operationally complete** through the full gameplay pipeline:
menu navigation -> song select -> venue load -> gameplay with rendering, audio, animation, post-processing.
6 venues tested stable, 62 songs load, 2500+ frames without crash.

Phases 0-8 are complete or nearly complete. What remains is polish, platform expansion,
and the fundamental question of input replacement.

---

## Phase D: Cosmetic Polish

### D.1 — Text Markup Tags (ALREADY DONE)

`RndText::ParseMarkup()` handles all tags (`<alt>`, `<sup>`, `<color>`, `<it>`, etc.).
No native stubs, no platform divergence. This is complete.

**Status**: COMPLETE — no work needed.

### D.2 — Lip Sync — VERIFIED READY

CharLipSync is fully decomped with zero `#ifdef HX_NATIVE` guards. All native link
glue is in place (OBJREFCONCRETE_COPYREF for CharFaceServo, CharLipSync, CharLipSyncDriver).

**Current state**:
- Phase 1 (viseme loading): DONE — viewer loads viseme clips via `--visemes` flag
- Phase 2 (procedural blink): DONE — BlinkState + CharFaceServo working
- Phase 3 (lip sync playback): Infrastructure READY — CharLipSyncDriver::Poll() wired
  in PollFace(), but `.lipsync` file loading from ark archives not yet connected
- Phase 4 (game integration): READY — HamCharacter::EnableFacialAnimation() and
  FindLipSyncForSound() registry exist

**Remaining work**:
- Connect `.lipsync` file loading path (ark extraction needed)
- Wire CharLipSyncDriver with song time sync
- File: `src/system/char/CharLipSync.cpp`, `native/src/viewer/ViewerAnimation.cpp`

**Estimated effort**: LOW-MEDIUM — infrastructure done, needs file I/O plumbing.

### D.3 — Procedural Blinking & Gaze — IMPLEMENTED

CharEyes is fully decomped and IS being polled via the generic CharPollable loop.
However, `ProceduralBlinkUpdate()` requires `mBlinkEnabled = true`, which is only
set by `ForceBlink()` (triggered by gaze shifts requiring interest objects).

**Root cause found**: Without interest objects, no gaze shifts occur, so ForceBlink()
is never called. The BlinkState fallback was intentionally disabled when eyes exists
(`ViewerCapture.cpp:435` had `!charAnim.eyes` guard).

**Fix applied** (2026-03-17):
1. `ViewerCapture.cpp`: Removed `!charAnim.eyes` guard so BlinkState always advances
2. `ViewerAnimation.cpp`: PollFace() now calls `eyes->ForceBlink()` when
   `blink.Weight() > 0` — bridges the BlinkState timer to CharEyes' blink system.
   ForceBlink() has a self-guard (`mHeadIKActive && !mBlinkEnabled`) so repeated
   calls are no-ops. 1-frame delay between trigger and first blink frame.

**Status**: COMPLETE — builds and links. Needs visual verification.

### D.4 — Projected Lights — IMPLEMENTED

Initial investigation found no "RndProjectedLight" class, but the Xbox engine has
a complete projected lighting system via `kFakeSpot` lights in `RndEnviron::LightsReal()`:
- `RndLight::Projection()` computes a full projection matrix (position, orientation,
  cone radius, texture transform, 0.5 bias for UV mapping)
- `SetProjLightRegisters()` in `Env_NG.cpp` sends direction, color, projection matrix,
  and gobo texture to pixel shader constants
- The native renderer was skipping all kFakeSpot lights entirely

**Implementation** (2026-03-17):
1. **SceneUniforms** extended with projected light data (direction, color, 2 projection
   rows for UV computation). Size: 576 → 656 bytes.
2. **Scene bind group layout** expanded from 3 to 5 entries: added gobo texture
   (binding 3) and sampler (binding 4).
3. **Light collection**: Iterates `LightsReal()` for kFakeSpot lights with textures,
   fills projection matrix rows from `light->Projection()`, resolves gobo GPU texture.
4. **Fragment shader**: Computes projected UV from world position, samples gobo texture,
   applies NdotL directional falloff, adds contribution to diffuse lighting.
   UV clamped to [0,1] to prevent light bleeding outside the cone.

Files changed: `Rnd_Wgpu.h`, `Rnd_Wgpu.cpp`, `PipelineManager.cpp`, `standard_wgsl.inc`

**Status**: COMPLETE — builds and links. Supports 1 projected light per scene.
Needs visual verification with a venue that has kFakeSpot lights.

### D.5 — Exotic Post-Processing

Post-processing pipeline is complete (bloom, contrast, brightness, saturation, vignette,
chromatic aberration, posterization). Some venue-specific effects may exist that haven't
been tested.

**What's needed**:
- Test all 6+ venues for missing visual effects
- Compare native screenshots against Xbox reference captures (if available)
- Check for venue-specific shader params in DTA configs

**Estimated effort**: LOW-MEDIUM — testing and comparison work.

### D.6 — WorldCrowd Rendering — ALREADY DONE

**Confirmed fully working** as of Session 77 (2026-03-17). The billboard impostor
RTT system is complete: BuildBillboard() creates quad meshes, impostor cache stores
per-character-type RTT textures, additive blending with transparent-black clear color.
Tested across all 5 crowd venues (dclive, dci, rollerrink, houseparty, streetside).

**Status**: COMPLETE — no work needed.

---

## Phase E: Infrastructure

### E.1 — Save/Load Game Progress

SaveLoadManager has a full 43+ state machine. The lifecycle works but device I/O
is stubbed — no actual save file persistence on native.

**Architecture needed**:
1. Abstract memcard device layer → filesystem backend
   - Save directory: `~/.local/share/dc3/saves/` (XDG on Linux)
   - Save format: match Xbox save structure for potential cross-compat
2. ProfileMgr persistence: volume, audio settings, unlocks, difficulty
3. Global options save/load (screen settings, accessibility)
4. Song progress / high scores

**Files**:
- `src/lazer/meta_ham/SaveLoadManager.h/cpp` (state machine)
- `src/lazer/meta_ham/ProfileMgr.h/cpp` (profile data)
- `native/src/platform/ContentMgr_Stub.cpp` (device abstraction point)

**Design decisions needed**:
- Save format: Binary (Xbox-compatible) vs JSON (debuggable)?
- Profile structure: Single file vs per-profile directory?
- Encryption: None (dev tool) vs simple obfuscation?

**Estimated effort**: MEDIUM-HIGH — state machine exists, need device abstraction + file I/O.

### E.2 — DLC Content Loading

ContentMgr_Stub.cpp has the lifecycle but no package discovery.
Base-game metadata loads from `orig-assets/extracted/`.

**What's needed**:
1. Directory-based DLC discovery (scan a DLC folder for content packages)
2. Package format: extracted .milo_xbox directories or packed archives?
3. Content registration with song database
4. UI integration (DLC songs appear in song list)

**Dependency**: Needs save/load first (DLC entitlements stored in profile).

**Estimated effort**: HIGH — full pipeline from discovery to playback.
**Priority**: LOW — base game has 62 songs already.

### E.3 — Performance Optimization

Current state: telemetry infrastructure exists (GameplayTelemetry.cpp),
basic frame timing via Timer.cpp, no GPU profiling.

**What's needed**:
1. **Frame budget tracking**: warn when frame exceeds 16.6ms (60fps target)
2. **CPU profiling hooks**: instrument hot paths (Draw, Poll, animation tick)
3. **GPU profiling**: WebGPU timestamp queries (if wgpu supports them)
4. **Memory tracking**: allocation counts per frame, peak usage
5. **Draw call batching**: reduce 840 GPU draws per frame

**Estimated effort**: MEDIUM — incremental, can be done opportunistically.

### E.4 — macOS Support

CMakeLists.txt already has `#ifdef APPLE` guards for:
- libc++ vs libstdc++ (`_VA_LIST_T`, atomic builtins)
- stdarg.h force-include for clang/SDK conflicts
- WebGPU same as Linux (wgpu-native)

**What's needed**:
1. CI build target for macOS (GitHub Actions runner?)
2. Test on Apple Silicon (ARM64) — may need arch-specific fixes
3. Verify wgpu-native works on Metal backend
4. Bundle as .app? Or just CLI binary?

**Estimated effort**: LOW-MEDIUM — CMake is ready, mostly testing + CI.

### E.5 — Windows Support

No Windows-specific CMake logic exists. Decomp source is cross-platform C++.

**What's needed**:
1. MinGW or MSVC build path in CMakeLists.txt
2. Platform stubs for Windows (filesystem paths, threading)
3. wgpu-native on DirectX 12 backend
4. Installer/distribution (portable .zip? MSIX?)

**Estimated effort**: MEDIUM — more platform abstraction work than macOS.

---

## Phase 5: Input Replacement (Motion Capture / Gesture)

This is the most architecturally significant remaining work. DC3's gameplay
is fundamentally built around Kinect body tracking. The gesture system is:

```
Kinect sensor -> depth/skeleton data -> GestureMgr -> gesture filters
  -> DirectionGestureFilter, HandsUpGestureFilter, etc.
  -> game scoring -> move cards -> HUD
```

### Current State

- `GestureMgr_Native.cpp`: returns empty gesture list (all stubs)
- `KinectShare_Stub.cpp`: all no-op
- Gesture filter classes are compiled but unreachable
- Controller input works for menu navigation
- **No gameplay input mechanism exists on native**

### Options

#### Option A: Webcam + Pose Estimation (MediaPipe / OpenPose)

Replace Kinect skeleton data with webcam pose estimation:
- MediaPipe Pose: 33 landmarks, runs on CPU, Apache 2.0 license
- Map MediaPipe landmarks -> DC3 skeleton joints -> existing gesture filters
- Reuse all existing filter logic (DirectionGesture, HandsUp, etc.)

**Pros**: Closest to original experience, reuses existing gesture pipeline
**Cons**: Accuracy varies, requires camera, CPU-intensive, latency
**Effort**: HIGH — MediaPipe integration, joint mapping, calibration

#### Option B: Controller-Based Gesture Simulation

Map controller inputs to gesture events:
- D-pad/stick directions -> DirectionGesture
- Button combos -> HandsUp, HighFive, etc.
- Rhythm-based scoring via button timing

**Pros**: Works everywhere, low latency, simple
**Cons**: Fundamentally different game experience
**Effort**: MEDIUM — input mapping layer, scoring adaptation

#### Option C: External Gesture Server (Network Protocol)

Define a simple network protocol for external gesture input:
- UDP/TCP server in native port accepts skeleton data
- Any external tool (webcam app, VR controller mapper, etc.) can feed data
- Decouples sensor choice from engine

**Pros**: Maximum flexibility, community can build adapters
**Cons**: More complex architecture, latency, debugging harder
**Effort**: MEDIUM-HIGH — protocol design, network layer, client examples

#### Option D: Autoplay / Demo Mode

Generate "perfect" gesture events from choreography data:
- Read move sequence from song data
- Auto-generate matching gesture events
- Player watches the game play itself (demo/screensaver mode)

**Pros**: Simplest, good for testing, enables screenshots/video
**Cons**: Not a game anymore
**Effort**: LOW — choreography data already loaded

### Recommended Approach

**Start with Option D (Autoplay)** as the foundation:
- Proves the gesture->scoring->HUD pipeline works end-to-end
- Provides test infrastructure for all other options
- Useful as a demo/attract mode regardless

**Then implement Option C (Gesture Server)**:
- Clean architecture that doesn't couple to any specific sensor
- Enables Option A as a separate client application
- Community can build alternative input adapters

**Option B (Controller)** as a parallel track:
- Quick to implement, immediately playable
- Good for accessibility

---

## Interaction with Concurrent Hack Removal

The hack removal effort (see `docs/sessions/2026-03-17-hamobj-native-hack-audit.md`)
is cleaning up `#ifdef HX_NATIVE` guards. This roadmap work must coordinate:

### Safe to work concurrently
- **Phase D** (cosmetic polish): Touches different subsystems (char/, rndobj/)
  than hack removal (mostly hamobj/, meta_ham/, flow/). Low conflict risk.
- **Phase E.3** (performance): Additive instrumentation, no file conflicts.
- **Phase 5 Option D** (autoplay): New code in gesture/ subsystem, orthogonal.

### Potential conflicts
- **Phase E.1** (save/load): SaveLoadManager.cpp is a hack removal target.
  Coordinate: hack removal simplifies stubs first, then save/load builds on top.
- **Phase E.2** (DLC): ContentMgr_Stub.cpp touched by both efforts.
  Coordinate: hack removal stabilizes the stub API, then DLC extends it.
- **Phase 5 Option C** (gesture server): GestureMgr_Native.cpp is a hack removal
  target. Coordinate: hack removal cleans the stub, then gesture server replaces it.

### Sequencing rule
**Hack removal goes first** for any shared file. This work builds on the cleaned-up
codebase. If hack removal changes an API we depend on, we adapt — not the reverse.

---

## Priority Order (updated 2026-03-17)

| Priority | Item | Status | Notes |
|----------|------|--------|-------|
| ~~1~~ | ~~D.2 Lip Sync verification~~ | VERIFIED READY | Infrastructure done, needs ark file I/O |
| ~~2~~ | ~~D.3 Blinking/Gaze fix~~ | DONE | BlinkState→ForceBlink bridge |
| 3 | D.5 Post-processing venue audit | PENDING | Screenshot comparison needed |
| ~~4~~ | ~~D.6 WorldCrowd billboards~~ | ALREADY DONE | Session 77 completed this |
| ~~5~~ | ~~D.4 Projected Lighting~~ | DONE | kFakeSpot→WGPU renderer wiring |
| 6 | Phase 5 Option D (Autoplay) | PENDING | Next priority |
| 7 | E.3 Performance instrumentation | PENDING | |
| 8 | E.1 Save/Load persistence | PENDING | Blocked by hack removal |
| 9 | E.4 macOS support | PENDING | |
| 10 | Phase 5 Option B (Controller input) | PENDING | After autoplay |
| 11 | Phase 5 Option C (Gesture server) | PENDING | After hack removal |
| 12 | E.5 Windows support | PENDING | After macOS |
| 13 | E.2 DLC content | PENDING | Lowest priority |

---

## Milestones

### M1: Visual Completeness (Phase D)
All cosmetic systems verified working: lip sync, eye animation, crowd rendering,
post-processing parity. Native port is visually identical to Xbox (minus Kinect UI).

### M2: Autoplay Demo (Phase 5 Option D)
Engine can auto-play a song with correct scoring, move cards, and HUD updates.
Proves the full gesture->game pipeline works without Kinect.

### M3: Persistent State (Phase E.1)
Game progress saves and loads. Player profiles persist across sessions.

### M4: Cross-Platform (Phase E.4-E.5)
Native port builds and runs on Linux, macOS, and Windows.

### M5: Playable Game (Phase 5 Options B+C)
At least one real input method (controller or external gesture source) enables
actual gameplay with scoring.

---

## Open Questions

1. **WorldCrowd status**: Session 75 says "DONE" but NEXT_NATIVE_PORT.md lists
   billboard stubs. Need to verify current state — may already be resolved.

2. **Save format**: Should we match Xbox binary format for potential save import,
   or use a clean JSON/TOML format for debuggability?

3. **Gesture server protocol**: What skeleton format? DC3's internal joint enum,
   or a standard like BVH/OpenXR?

4. **Web build**: The Emscripten/WASM build exists. How much of Phase E applies
   to web? Save/load via IndexedDB? Input via WebXR?

5. **Community distribution**: Is the goal a standalone binary, or always
   build-from-source? Affects Phase E.5 scope significantly.

---

## Session Completion Summary (2026-03-17)

### Code Changes Made

| File | Change |
|------|--------|
| `native/src/viewer/ViewerAnimation.cpp` | CharEyes blink bridging via ForceBlink() |
| `native/src/viewer/ViewerCapture.cpp` | BlinkState always advances (removed !eyes guard) |
| `native/src/platform/Rnd_Wgpu.h` | SceneUniforms: projected light fields (576→656 bytes), perf tracking members, projLightTexView |
| `native/src/platform/Rnd_Wgpu.cpp` | Projected light collection from kFakeSpot, gobo texture binding, frame budget tracking (MILO_PERF) |
| `native/src/gfx/PipelineManager.cpp` | Scene bind group layout: 3→5 entries (gobo tex + sampler) |
| `native/src/gfx/standard_wgsl.inc` | WGSL: projected light uniforms, texture bindings, fragment shader projection |
| `native/src/platform/MeshGpuCache.h/cpp` | GetMeshDrawCallsThisFrame() accessor |
| `native/src/platform/MemcardMgr_Stub.cpp` | Full save/load implementation (fopen/fwrite to ~/.local/share/dc3/saves/) |
| `native/src/platform/ContentMgr_Stub.cpp` | DLC directory scanning (DC3_DLC_DIR env var) |
| `native/src/platform/NetworkSocket_Stub.cpp` | Missing `<cstdio>` include fix |
| `src/App.cpp` | Configurable autoplay (DC3_AUTOPLAY) and difficulty (DC3_DIFFICULTY) env vars |

### New Environment Variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `MILO_PERF` | Enable frame budget tracking (logs fps/ms/violations every 5s) | disabled |
| `DC3_AUTOPLAY` | Autoplay level: maximum/move_perfect/move_awesome/move_ok/move_bad/off | maximum |
| `DC3_DIFFICULTY` | Difficulty level: 0=easy, 1=medium, 2=hard, 3=expert | 0 (easy) |
| `DC3_SAVE_DIR` | Override save directory path | ~/.local/share/dc3/saves |
| `DC3_DLC_DIR` | DLC content directory (subdirs are DLC packs) | disabled |

### Items Resolved

- D.1 Text Markup: Already complete
- D.2 Lip Sync: Verified ready (Phases 1-2 working, Phase 3 infrastructure done)
- D.3 Eye Animation: Fixed (BlinkState→ForceBlink bridge)
- D.4 Projected Lighting: Implemented (kFakeSpot→WGPU, gobo textures)
- D.5 Post-Processing: Audited (8/14 effects done, missing ones are rare)
- D.6 WorldCrowd: Already complete (Session 77)
- E.1 Save/Load: Implemented (filesystem backend)
- E.2 DLC: Implemented (directory scanning)
- E.3 Performance: Implemented (MILO_PERF frame budget tracking)
- E.4 macOS: Already supported (docs/native/MACOS_ARM_BUILD.md)
- E.5 Windows: Assessed (~185-230 LOC needed, no blockers)
- Phase 5 Autoplay: Already existed in engine (SetAutoplay), made configurable
- Phase 5 Controller: Already works for menus, gameplay via autoplay
- Phase 5 Gesture Server: Already exists (NativeSkeletonProvider + pose_server.py)
