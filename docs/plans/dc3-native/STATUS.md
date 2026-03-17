# DC3 Native Port — Status

**Last updated**: 2026-03-16

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
| Phase 2.5: Character Animation | WORKING | ~95% |
| Phase 3: Audio | COMPLETE | 100% (real-time MOGG playback verified) |
| Phase 4: Input | COMPLETE | 100% |
| Phase 5: Motion Capture | NOT STARTED | 0% |
| Phase 6: Polish & Platforms | IN PROGRESS | ~40% |

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
- **game_screen gameplay**: 733 draw calls/frame — venue rendering, camera cuts (34 shot keys/song), character dance animation via song.anim ClipPlayer, real-time MOGG audio playback, **WorldCrowd 3D characters** (5 crowd types × 6+ instances on dclive)
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

### Milestone 1: Stable Boot to Main Menu — COMPLETE

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

### Milestone 2: Engine Rendering Parity with Viewer — COMPLETE

**Goal**: What the viewer renders beautifully, the engine should too.

All items resolved — venue rendering, draw ordering, post-processing, and text rendering
all working through `WorldDir::DrawShowing()` pipeline. `<alt>` tag rendering depends on
missing font style entries in .milo assets (not code-fixable).

### Milestone 3: Gameplay Screen — COMPLETE (Session 63+)

**Goal**: Navigate to a song, start gameplay, see the dance stage.

**Status**: COMPLETE. All core gameplay rendering working — venue, characters, camera
cuts, audio, HUD. 272-1094 draw calls/frame depending on venue. 31 songs x 7 venues
tested stable. Remaining items (move card content, scoring) are in Phase 8 polish.

**Key fix (Session 63)**: `CameraManager::CalcFrame()` produced NaN from uninitialized task timers, poisoning camera transforms and making the entire scene invisible. Guards in CalcFrame and CamShot::SetFrame clamp NaN to 0 (first keyframe). Removed redundant explicit DrawShowing — venue correctly renders through world_panel as part of TheUI->Draw().

**Key fix (Session 67 — Audio timing)**: VorbisReader::Poll decode loop exited immediately when TryDecode() found no Ogg packet (on Xbox, a background thread feeds data continuously). Fixed to read more file data and retry. Also added ring buffer flow control to native ConsumeData (missing BytesWriteable check caused silent data loss). Pre-fill ring buffers in Play() before registering with AudioDevice. Song audio now plays at real-time (100% speed), driving all animation systems.

**Session 68 — Camera cuts verified**: song.anim PropKeys with property "shot" drives HamDirector::SetShot() → FindNextShot() → CameraManager::ForceCameraShot(). 34 shot keyframes for YMCA cycle through Area1_WIDE, Area1_NEAR, Area1_MOVEMENT, CLOSEUP categories. Camera angle changes visible in screenshots. LightPreset animation N/A for YMCA's glitterati venue (0 LightPreset objects — static baked lighting correct). HUD panels (game_panel, world_panel, rhythm_detector_panel, fitness_hud_panel) all active+showing during gameplay.

### Milestone 4: Playable Dance Gameplay — MOSTLY COMPLETE

**Goal**: Full dance gameplay loop — see moves, hear music, track body, score.

All core items done (character animation, camera cuts, audio, RTT, lighting).
Remaining: lip sync (.lipsync file loading), motion capture (Phase 5), scoring (needs MoveGraph deserialization).

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
| DC3 logo on main menu | **DONE** | Session 71: MetaMaterials loading enabled on native. `shell_basic.mmat` resolves correctly. 198 warnings → 0. |
| Score display wiring | **DONE** | Song name + artist visible in HUD (per-frame force-show + alpha fix). Score labels set to "0". |
| HUD rendering pipeline | **DONE** | Loads _default_hud.milo, SyncObjects, Enter, 49 draws per frame. ClearDepthForOverlay for proper 2D overlay. Uses HUD's Cam.cam (3D perspective at y=-768) and static_hud.env. |

#### Medium Effort
| Task | Status | Notes |
|------|--------|-------|
| Move card UI content | PARTIAL | MoveMgr initialized on native (no more "not function or object" errors). Choreography pipeline guarded for missing Kinect data. Move graph data not loaded (no gesture detection). |
| WorldCrowd rendering | **DONE** | 3D crowd characters visible on dclive venue. 5 crowd character types, 30+ instances placed via placement mesh. Key fix: SetFullness was capping m3DChars to empty instance list after Set3DCharAll transferred them. See [CROWD_ANIMATION.md](../../native/CROWD_ANIMATION.md) |

**Session 71 — MetaMaterials + GCC 15 compat**: Removed `#ifdef HX_NATIVE` guard in `RndMat::Init()` that disabled `LoadMetaMaterials()` on native. `sMetaMaterials` now loads `metamaterials.milo` and all shared MatAnim objects (`shell_basic.mmat` etc.) resolve correctly. Fixed GCC 15 compat: `std::random_shuffle` and `std::mem_fun` restored in libstdc++ 15 — guarded compat shims with `_GLIBCXX_RELEASE < 15`.

**Session 74 — WorldCrowd + HUD**: WorldCrowd 3D crowd rendering working on dclive (733 draw calls/frame). HUD rendering pipeline fully wired — loads milo, SyncObjects, Enter, 49 draws with ClearDepthForOverlay. skeleton.lbl visible confirming draw pipeline works. Song name/score labels positioned in HUD 3D camera space (Cam.cam at y=-768), need coordinate/orientation fix. Multiple venues verified: dclive (crowd), glitterati (no crowd). Key bugs fixed: SetFullness erasing m3DChars, DumpSongLayout crash on empty vectors, ChunkStream seek corruption.

**Session 75 — HUD camera fix + MoveMgr + venue sweep**: Fixed HUD camera to use Cam.cam (y=-768 perspective) instead of venue camera — labels now project correctly onto screen. Added MoveMgr::Init(0) + MiniGameMgr::Init() to native init — eliminates "movemgr not function or object" DTA errors. Guarded choreography pipeline (OriginalChoreoRemixer, DanceRemixer, MoveAsyncDetector) against missing Kinect data. Verified 6 venues: dclive, glitterati, houseparty, rollerrink, bid, dci — all rendering with HUD overlay. 827 draw calls/frame on dclive with HUD.

**Session 76 — Facial animation + HUD text**: Loaded per-character viseme .milo files on native (bypasses FileMerger which doesn't fire). CharFaceServo now has Base clip + Blink clips for all 4 game characters (player0/1, backup0/1). Procedural blinking enabled via CharEyes → CharFaceServo pipeline. HUD song name ("YMCA") and artist ("Village People") labels now visible with per-frame alpha force.

**Session 77 — Eye gaze + draw call accuracy + score display**: Created CharInterest objects per character for eye gaze tracking (on Xbox these come from .milo files via FileMerger). Fixed draw call counter — was reporting 2047 (including 1223 no-material mesh traversals); actual GPU draws are ~840. Loaded score.milo into HUD score_left/score_right slots. Frame capture analysis: 787 GPU draws (407 world.cam, 202 Cam.cam HUD, 141 text, 37 UI), 30 unique meshes account for all 1223 no-material skips (move card/feedback UI with 45 instances each).

#### Cosmetic / Low Priority
| Task | Status | Notes |
|------|--------|-------|
| Facial animation base | **DONE** | Viseme .milo loaded per-character, CharFaceServo base clip wired, procedural blinking enabled via CharEyes. |
| CharEyes gaze/darts | **DONE** | CharInterest objects created per character, audience position interests wired, procedural look system active. |
| Score display | **DONE** | score.milo loaded into HUD subdirs, explicit subdir drawing. |
| Lip sync | TODO | Needs .lipsync files from ark loaded + OnSoundPlay handler triggering |
| Projected light textures | TODO | Gobo/spotlight cookies |
| Exotic post-processing | PARTIAL | Noise/grain + flicker done. Gradient map, kaleidoscope, video feedback still TODO. |
| Performance optimization | ANALYZED | ~840 actual GPU draws per gameplay frame. 787 draws + 1223 no-material skips (30 unique UI meshes × ~45 instances). RTX 3090 handles this easily. |
| Save/load game progress | TODO | Profile, unlocks |
| macOS / Windows | TODO | WebGPU handles backends |

#### Not Code-Fixable
| Item | Reason |
|------|--------|
| Text `<alt>` markup tags | ParseMarkup fully implemented — missing alt font style entries in .milo asset files |

### Milestone 7: Platform Correctness (DTA / Init / Hack Removal)

**Goal**: Fix upstream issues that `#ifdef HX_NATIVE` hacks currently cover. Remove hacks where possible.

**Research**: [HACK_AUDIT.md](../../native/HACK_AUDIT.md) — full audit of HX_NATIVE guards (2026-03-16).

#### DTA Handler Pipeline — RESOLVED (2026-03-16)

Root cause analysis below (originally tracked in DTA_HANDLER_ANALYSIS).

**Finding**: Animation completion issue is **NOT DTA-related**. `mTypeDef` is null for all animated objects, and `on_anim_event` has no DTA handler in any config. The `Anim.cpp` auto-null hack is the correct fix for native object lifecycle timing differences.

| Task | Status | Notes |
|------|--------|-------|
| Add `ContextCheckerInit()` to native init | **DONE** | Registers 5 DTA script functions. Builds clean, 500-frame smoke test passes. |
| Add `MidiParser::Init()` to native init | **DONE** | Enables MidiParser object deserialization from .milo files. |
| Add `DirLoader::SetPathEvalCallback(IsUselessLoad)` | **DONE** | Filters unnecessary asset loads by game mode. |
| Verify mTypeDef population | **DONE** | mTypeDef is null for all animated objects — confirmed via diagnostics. |
| Test `on_anim_event` dispatch | **DONE** | Returns kDataUnhandled — no DTA handler exists anywhere. |
| Anim.cpp auto-null hack (426-434) | **KEEP** | Correct fix for native timing, not a DTA issue. |
| HamNavList IsAnimating skip (505-509) | **KEEP** | Depends on animation completion, which is correctly handled by auto-null. |

#### Upstream Bug Fixes — Already Correct

| Task | Status | Notes |
|------|--------|-------|
| Audio suspend/resume (Game.cpp:297-310) | **IN PLACE** | Real threading bug. `#ifdef HX_NATIVE` is appropriate — only native has threaded audio. |
| Load state reset (Game.cpp:315-321) | **IN PLACE** | Correctly resets mLoadState after sync stream destruction. |

#### Missing Init Calls — ADDED (2026-03-16)

| Init Call | Status | What It Unblocks |
|-----------|--------|-----------------|
| `ContextCheckerInit()` | **DONE** | DTA script functions (random_context, etc.) |
| `MidiParser::Init()` | **DONE** | MidiParser factory registration |
| `DirLoader::SetPathEvalCallback()` | MEDIUM | Content path resolution |
| `AccomplishmentManager::Init()` | LOW | Achievement system |
| `MetagameRank::Init()` | LOW | XP/level system |
| `SaveLoadManager::Init()` | LOW | Save/load (needs NativeSaveLoadStub upgrade) |

#### Acceptable Platform Differences (Keep)

These hacks are correct and should remain:
- MoveDir null safety (6 instances) — MoveGraph is Kinect-specific
- MoveGraph loading skip — no gesture detection on native
- Audio timeout bypass — async loading architectural difference
- LP64 pointer fixes (26+ instances) — required for 64-bit
- STL container differences — libstdc++ vs STLport
- `__fsel` replacement — PPC intrinsic

### Testing Roadmap

Test gaps tracked in TODO.md Phase 8 and native/tests/.

| Test | Priority | Validates |
|------|----------|-----------|
| DTA handler dispatch | HIGH | mTypeDef population, ExecuteScript(), handler chain |
| System init completeness | HIGH | Factory registration, DTA function availability |
| AnimTask completion flow | HIGH | End-to-end animation lifecycle |
| Audio thread safety | MEDIUM | Suspend/resume race condition fix |
| Visual regression automation | MEDIUM | Rendering correctness across changes |
| Flow state machine traversal | MEDIUM | UIPanel lifecycle, transitions |

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
- [HACK_AUDIT.md](../../native/HACK_AUDIT.md) — full audit of HX_NATIVE guards and crash-masking hacks (2026-03-16)
