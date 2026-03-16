# DC3 Native Port — Status

**Last updated**: 2026-03-14

## Current State

The DC3 native port (x86_64 Linux) has a fully operational rendering and gameplay pipeline.
The standalone viewer (Track B) is complete and the engine (Track A) runs the full game flow
from boot to gameplay with rendering, audio, animation, and camera systems all working.
Post-processing (bloom, color grading) and scene lighting improvements round out the visual
quality. The focus now shifts to **polish** — particle effects, light beam rendering, scoring,
and platform support.

### Phase Summary

| Phase | Status | Completion |
|-------|--------|:---:|
| Phase 0: Foundation | COMPLETE | 100% |
| Phase 1A: Headless Engine | NEARLY COMPLETE | ~95% |
| Phase 1B: Milo Viewer | COMPLETE | 100% |
| Phase 1.5: Asset Pipeline | COMPLETE | 100% |
| Phase 2: Rendering | IN PROGRESS | ~95% |
| Phase 2.5: Character Animation | WORKING | ~90% |
| Phase 3: Audio | COMPLETE | 100% (real-time MOGG playback verified) |
| Phase 4: Input | COMPLETE | 100% |
| Phase 5: Motion Capture | NOT STARTED | 0% |
| Phase 6: Polish & Platforms | IN PROGRESS | ~30% |

### What Works Today

**Viewer (Track B)**:
- Full Blinn-Phong material pipeline (specular, emissive, rim, intensify, fog)
- Skinned mesh rendering (4-bone blending, 40-bone palettes)
- Character dance animation (CharClip, CharDriver, twist bones, root motion)
- Multi-file scene loading (character + venue + clips)
- Post-processing (contrast, chromatic aberration, posterization, vignette)
- Video recording, batch rendering, glTF export
- 15+ static props, 12+ characters, 2+ venues verified

**Engine (Track A)**:
- Full boot through all subsystems → main loop → screen navigation
- Full menu flow: attract → main_screen → choose_mode → song_select → multiuser → loading → **game_screen**
- 10000 frames stable on game_screen, clean exit
- DCI venue rendering: 505 draw calls/frame (floor, walls, DJ booth, fully-lit character, HUD overlays)
- **game_screen gameplay**: 272 draw calls/frame — venue rendering, camera cuts (34 shot keys/song), character dance animation via song.anim ClipPlayer, real-time MOGG audio playback
- **Camera cuts working**: song.anim PropKeys "shot" → SetShot → FindNextShot → ForceCameraShot cycles through Area1_WIDE/NEAR/MOVEMENT/CLOSEUP
- **Character animation on main menu**: outfit loading via FileMerger, CharDriver clip playback, GPU-skinned bone animation
- Audio pipeline complete (FFmpeg, Vorbis, miniaudio, DSP effects, real-time MOGG decode)
- Input working (gamepad + keyboard + scripted headless input)
- Text rendering working (glyph meshes, DXT5 alpha shader, font loading)
- Flow→PropAnim UI animation pipeline verified end-to-end
- **Post-processing pipeline**: bloom (screen blend), Xbox-matched contrast/brightness formulas, saturation, levels, vignette, chromatic aberration, posterization
- **RndFlare**: visible in-view flares (occlusion bypass), DrawRect rendering
- **RndParticleSys**: full Load + WebGPU billboard renderer
- **RndLine**: CPU-side perspective geometry → mesh rendering pipeline

**Shared infrastructure**: .ark archive loading (6,377 files), runtime .milo loading,
LP64-safe DataArray scripting, BinStream endian conversion.

---

## Roadmap: Getting the Game Working

### Milestone 1: Stable Boot to Main Menu (Current Focus)

**Goal**: Engine boots to main menu without crashes, UI is readable.

| Task | Status | Blocker? |
|------|--------|:---:|
| ~~Implement `RndText::ConvertTextToWide`~~ | DONE | ~~YES~~ |
| ~~Skip Kinect tutorial screens~~ | DONE (auto-advance) | ~~YES~~ |
| ~~`Flow::Enter()` / `Flow::Exit()`~~ | DONE (81.8% / 99.1%) | ~~YES~~ |
| ~~MeshAnim proper stub~~ | DONE | ~~No~~ |
| ~~Fix localization tokens~~ | DONE (RegionInit + Locale loads 2091 tokens) | ~~No~~ |
| Scripted input DTA handler crash | KNOWN | No — crashes in UIScreen::Handle joypad config lookup |

**Status**: Engine successfully navigates attract → tutorials → main_screen → choose_mode_screen.
Stable at 3000+ frames on choose_mode_screen. Flow::Enter() enables menu navigation.
Scripted button input crashes in DTA handler path (joypad button_meanings config lookup).

### Milestone 2: Engine Rendering Parity with Viewer

**Goal**: What the viewer renders beautifully, the engine should too.

The standalone viewer and the engine share the same `Mesh_Wgpu.cpp` / `Rnd_Wgpu.cpp`
rendering code, but the engine's scene traversal path differs — the viewer manually
iterates drawables, while the engine uses `WorldDir::DrawShowing()` → `RndGroup` →
`RndDrawable` tree.

| Task | Priority | Notes |
|------|----------|-------|
| Venue background rendering | HIGH | `turbo_shell` 3D scene behind UI is black. Camera positioned but venue meshes not reaching DrawShowing |
| RndGroup draw ordering | HIGH | Hierarchical draw order for correct UI/3D layering |
| UI sprite/icon rendering | MEDIUM | Player silhouettes, button prompts, autosave orb — likely RndTex quads |
| Text markup processing | MEDIUM | `<alt>` tags render as literal text instead of styling |
| Bloom/glow post-process | LOW | Neon aesthetic from Xbox UI |

### Milestone 3: Gameplay Screen — WORKING (Session 63)

**Goal**: Navigate to a song, start gameplay, see the dance stage.

**Status**: FULLY WORKING. 272 draw calls/frame during gameplay. Characters animate via CharClip playback, camera cuts cycle through 34 shot keyframes (Area1_WIDE, Area1_NEAR, Area1_MOVEMENT, CLOSEUP), song audio plays at real-time via MOGG decode. HUD panels active but move card content not yet visible.

| Task | Status | Notes |
|------|--------|-------|
| Song select screen navigation | DONE | Full menu flow via input script |
| LightPreset stub removal | DONE | 12 stubs removed, real impl links |
| DataNode::GetObj graceful failure | DONE | Missing objects warn instead of crash |
| **Venue rendering through world_panel** | **DONE** | Fixed NaN camera in CalcFrame/SetFrame. Venue renders through TheUI->Draw() matching Xbox architecture |
| **LightPreset forcing** | **DONE** | Auto-force first valid preset on venue change. Baked lights work for venues without presets |
| **HamDirector venue selection** | **DONE** | GetVenueWorld() for gameplay venue, fallback to gNativeVenueDir for menu |
| Song animation playback | **DONE** | song.anim PropAnim loads, SetFrame advances at 30fps song time, PlayAnims drives character clips |
| Song audio playback during gameplay | **DONE** | VorbisReader decode loop fix + ring buffer flow control. Real-time MOGG decryption + Vorbis decode. |
| **Camera cuts (dircut)** | **DONE** | 34 shot keys in song.anim. Categories: Area1_WIDE, Area1_NEAR, Area1_MOVEMENT, CLOSEUP. ForceCameraShot applied each cut. |
| LightPreset animation | **N/A for YMCA** | glitterati venue has 0 LightPreset objects — static lighting is correct. Other venues with presets use ForcePreset. |
| Move card UI rendering | **TODO** | HUD panels (game_panel, fitness_hud_panel) are active+showing during gameplay but move card content invisible — likely needs TexMovie data or asset wiring. |
| Score display | **TODO** | Not yet wired |

**Key fix (Session 63)**: `CameraManager::CalcFrame()` produced NaN from uninitialized task timers, poisoning camera transforms and making the entire scene invisible. Guards in CalcFrame and CamShot::SetFrame clamp NaN to 0 (first keyframe). Removed redundant explicit DrawShowing — venue correctly renders through world_panel as part of TheUI->Draw().

**Key fix (Session 67 — Audio timing)**: VorbisReader::Poll decode loop exited immediately when TryDecode() found no Ogg packet (on Xbox, a background thread feeds data continuously). Fixed to read more file data and retry. Also added ring buffer flow control to native ConsumeData (missing BytesWriteable check caused silent data loss). Pre-fill ring buffers in Play() before registering with AudioDevice. Song audio now plays at real-time (100% speed), driving all animation systems.

**Session 68 — Camera cuts verified**: song.anim PropKeys with property "shot" drives HamDirector::SetShot() → FindNextShot() → CameraManager::ForceCameraShot(). 34 shot keyframes for YMCA cycle through Area1_WIDE, Area1_NEAR, Area1_MOVEMENT, CLOSEUP categories. Camera angle changes visible in screenshots. LightPreset animation N/A for YMCA's glitterati venue (0 LightPreset objects — static baked lighting correct). HUD panels (game_panel, world_panel, rhythm_detector_panel, fitness_hud_panel) all active+showing during gameplay.

### Milestone 4: Playable Dance Gameplay

**Goal**: Full dance gameplay loop — see moves, hear music, track body, score.

| Task | Priority | Notes |
|------|----------|-------|
| Character animation in gameplay | **DONE** | CharClip playback synced to beat. SongAnimation() returns 0 (SongDriver has clips). ClipPlayer.PlayAnims drives skeleton from song.anim frame. |
| TexMovie render-to-texture | DONE | Full pipeline: FFmpeg decode → UploadRGBAToRndTex → WebGPU. MakeDrawTarget/FinishDrawTarget implemented. Pink rectangles are asset/wiring issue. |
| **Camera cuts via song.anim** | **DONE** | PropKeys "shot" property drives HamDirector::SetShot → FindNextShot → ForceCameraShot. 34 keys for YMCA. |
| LightPreset loading + animation | **DONE** | ForcePreset active on venue load. YMCA's glitterati venue has no LightPreset objects — static lighting correct. Venues with presets animate via force_preset messages. |
| Song.anim graceful DTA failure | DONE | DataNode::GetObj returns nullptr gracefully for missing objects (non-fatal MILO_FAIL_DTA) |
| Lip sync (CharFaceServo, CharLipSyncDriver) | MEDIUM | See [LIP_SYNC.md](../custom-graphics-engine/LIP_SYNC.md) |
| Procedural blinking (CharFaceServo) | LOW | Cosmetic |
| CharEyes gaze direction | LOW | Cosmetic |
| Motion capture integration (Phase 5) | HIGH | Kinect replacement via webcam + ML pose estimation |
| Scoring system verification | HIGH | Gesture matching → score calculation |

### Milestone 5: Polish

**Goal**: Full game experience.

| Task | Priority | Notes |
|------|----------|-------|
| **Post-processing pipeline** | **DONE** | Bloom (screen blend), contrast (Xbox formula), brightness, saturation, levels, vignette, chromatic aberration, posterization. Works in headless mode. |
| **RndFlare visibility** | **DONE** | Bypass GPU occlusion query on native — treat in-view flares as fully visible. DrawRect rendering via DrawRect2D WebGPU pipeline. |
| Particle systems (RndParticleSys) | **DONE** | Full Load (rev 0x29), WebGPU billboard renderer (Part_Wgpu.cpp). Real implementations linked (strong symbols override weak stubs). |
| Lines (RndLine) | **DONE** | Full UpdateLine implementation linked. CPU-side perspective-corrected geometry → RndMesh::DrawShowing via existing WebGPU pipeline. |
| Save/load game progress | MEDIUM | Profile, unlocks |
| DLC content loading | LOW | Extra songs |
| macOS / Windows support | MEDIUM | WebGPU handles backends |
| Web build (Emscripten) | DONE | `scripts/build/web.sh` — WASM port running in browser |
| Performance optimization | MEDIUM | Draw call batching, culling |

**Session 69 — Post-processing + visual quality**: Enabled full post-processing pipeline in headless mode. Fixed bloom composite (screen blend instead of additive prevents blown-out whites). Matched Xbox's non-linear contrast formula from `RndColorXfm::AdjustContrast`. Fixed RndFlare visibility by bypassing GPU occlusion query. RndLine and RndParticleSys implementations already linked via strong symbols.

### Milestone 6: Visual Completeness

**Goal**: Fill remaining visual gaps — the high-visibility missing elements.

#### Quick Wins
| Task | Status | Notes |
|------|--------|-------|
| DC3 logo on main menu | TODO | Most visible gap. Likely TexRenderer/render-to-texture not triggered or subdir not traversed during scene load. |
| Score display wiring | TODO | Connect scoring outputs to HUD labels during gameplay. Infrastructure exists. |

#### Medium Effort
| Task | Status | Notes |
|------|--------|-------|
| Move card UI content | TODO | HUD panels active but content invisible. TexMovie pipeline implemented — needs asset/data flow tracing for game_panel. |
| WorldCrowd rendering | TODO | Crowd character instancing for venue backgrounds. |

#### Cosmetic / Low Priority
| Task | Status | Notes |
|------|--------|-------|
| Lip sync | TODO | CharFaceServo, CharLipSyncDriver |
| Procedural blinking | TODO | CharFaceServo cosmetic |
| CharEyes gaze direction | TODO | Cosmetic |
| Projected light textures | TODO | Gobo/spotlight cookies |
| Exotic post-processing | TODO | Gradient map, kaleidoscope, flicker, noise, video feedback |
| Performance optimization | TODO | Draw call batching, culling, profiling |
| Save/load game progress | TODO | Profile, unlocks |
| macOS / Windows | TODO | WebGPU handles backends |

#### Not Code-Fixable
| Item | Reason |
|------|--------|
| Text `<alt>` markup tags | ParseMarkup fully implemented — missing alt font style entries in .milo asset files |

---

## Architecture

### Error Handling Strategy

| Macro | Xbox 360 Behavior | Native Behavior | Use Case |
|-------|-------------------|-----------------|----------|
| `MILO_ASSERT(cond, line)` | `Debug::Fail` (modal dialog + Continue) | `Debug::Fail` (fatal by default) | Code invariant violations |
| `MILO_FAIL(...)` | `Debug::Fail` (modal dialog + Continue) | `Debug::Fail` (fatal by default) | Unexpected runtime errors |
| `MILO_FAIL_DTA(...)` | `Debug::Fail` (same as MILO_FAIL) | `MILO_WARN` (non-fatal) | DTA runtime errors (property not found, type mismatches) |
| `MILO_WARN(...)` | `Debug::Warn` (log only) | `Debug::Warn` (log only) | Non-fatal warnings |

**`MILO_FATAL_FAILS` env var**: Controls `Debug::Fail` behavior.
- `1` (default): Fatal — abort on MILO_ASSERT and MILO_FAIL. Catches real bugs early.
- `0`: Non-fatal — print + continue (Xbox 360 "Continue" dialog behavior). Use for exploring past crashes.

### DataNode Safe Fallback Returns

When `MILO_FAIL_DTA` returns (non-fatal mode), DataNode accessor methods need safe return values to prevent SIGSEGV from dereferencing garbage union members. Under `#ifdef HX_NATIVE`, each accessor returns a safe default after the error:

- `Sym()` / `LiteralSym()` / `ForceSym()` → `Symbol("")`
- `Str()` / `LiteralStr()` → `""`
- `Array()` / `LiteralArray()` / `Command()` → `nullptr`
- `Var()` / `Func()` → `nullptr`

### NewObject Vtable Guard

`Hmx::Object::NewObject()` wraps factory calls in `sigsetjmp/siglongjmp` to catch SIGSEGV from broken vtables (weak stub constructors that don't initialize the vtable). After construction, it calls `obj->ClassName()` to verify the vtable works. Broken types are blacklisted in `sBrokenClasses` and return nullptr on subsequent calls.

**Currently blacklisted**: `MeshAnim` (weak stub only).

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `MILO_MAX_FRAMES` | 10000 | Headless frame limit |
| `MILO_RENDER` | 0 | Enable GPU rendering (1=on) |
| `MILO_HEADLESS` | 0 | Headless mode (no window, 1=on) |
| `MILO_SCREENSHOT_DIR` | (none) | Directory for auto-screenshots |
| `MILO_SCREENSHOT_FRAMES` | (none) | Comma-separated frame numbers |
| `MILO_INPUT_SCRIPT` | (none) | Path to scripted input file |
| `MILO_FATAL_FAILS` | 1 | Fatal Debug::Fail (0=continue past errors) |
| `MILO_FORCE_DRAW_PANEL` | (none) | Force-draw a specific panel (debug) |
| `MILO_FIRST_SCREEN` | (none) | Skip to a specific screen (e.g., `main_screen`) |

## Test Commands

```bash
# Build
cd native/build && cmake --build . -j$(nproc)

# Quick smoke test (500 frames, no render)
MILO_MAX_FRAMES=500 ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 60 ./native/build/dc3-native

# Headless render with screenshots
MILO_RENDER=1 MILO_HEADLESS=1 MILO_MAX_FRAMES=500 \
  MILO_SCREENSHOT_DIR=/tmp/claude-1000/shots \
  MILO_SCREENSHOT_FRAMES=50,100,200,300,400 \
  ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 60 ./native/build/dc3-native

# Skip to main menu
MILO_FIRST_SCREEN=main_screen MILO_MAX_FRAMES=500 \
  ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 60 ./native/build/dc3-native

# Non-fatal mode (explore past crashes)
MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=5000 \
  ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 180 ./native/build/dc3-native
```

## Testing Infrastructure

### GTest Integration Tests (`native/tests/`)

| Test | What it verifies |
|------|-----------------|
| `HeadlessBootTest.BootAndRun100Frames` | Engine boots and survives 100 frames |
| `HeadlessBootTest.SurvivesMainLoop` | 2000 frames of main loop stability |
| `HeadlessBootTest.InputReplayStartButton` | Scripted input processed correctly |
| `HeadlessBootTest.LongRunStability` | 10000 frames (env-gated: `MILO_LONG_TEST=1`) |
| `Subsystems.RandomSeeded` | RNG produces varied output |
| `Subsystems.ThreadCallRoundTrip` | Async ThreadCall → callback pipeline |
| `Subsystems.TaskMgrPoll` | TaskMgr timing and poll |
| `Subsystems.LocaleInitialized` | Locale subsystem doesn't crash |
| `Subsystems.JoypadPoll` | Joypad polling doesn't crash |

### ASan Build

```bash
cmake .. -DENABLE_ASAN=ON && cmake --build .
```

Run with: `ASAN_OPTIONS="alloc_dealloc_mismatch=0:detect_odr_violation=0"`

## Key Fixes Log

See `docs/native/NATIVE_PORT_STATUS.md` for the full session-by-session fix log including
LP64 issues, iterator compat, vtable crashes, stream desyncs, and rendering pipeline work.

## Related Docs

- [VIEWER_STATUS.md](VIEWER_STATUS.md) — Track B: standalone milo viewer status & roadmap
- [../custom-graphics-engine/PLAN.md](../custom-graphics-engine/PLAN.md) — master native port plan
- [../../native/NATIVE_PORT_STATUS.md](../../native/NATIVE_PORT_STATUS.md) — detailed session log
