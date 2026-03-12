# DC3 Native Port — Status

**Last updated**: 2026-03-12

## Current State

The DC3 native port (x86_64 Linux) has a fully operational rendering pipeline in the
standalone viewer (Track B) and a mostly-functional engine boot path (Track A). Audio,
input, and asset loading are all complete. The focus now shifts to **getting the actual
game working** — merging the viewer's rendering quality into the engine path and reaching
gameplay screens.

### Phase Summary

| Phase | Status | Completion |
|-------|--------|:---:|
| Phase 0: Foundation | COMPLETE | 100% |
| Phase 1A: Headless Engine | NEARLY COMPLETE | ~95% |
| Phase 1B: Milo Viewer | COMPLETE | 100% |
| Phase 1.5: Asset Pipeline | COMPLETE | 100% |
| Phase 2: Rendering | IN PROGRESS | ~85% |
| Phase 2.5: Character Animation | IN PROGRESS | ~60% |
| Phase 3: Audio | COMPLETE | 100% |
| Phase 4: Input | COMPLETE | 100% |
| Phase 5: Motion Capture | NOT STARTED | 0% |
| Phase 6: Polish & Platforms | NOT STARTED | 5% |

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
- Audio pipeline complete (FFmpeg, Vorbis, miniaudio, DSP effects)
- Input working (gamepad + keyboard + scripted headless input)
- Text rendering working (glyph meshes, DXT5 alpha shader, font loading)
- Flow→PropAnim UI animation pipeline verified end-to-end

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
| Fix localization tokens (Localize() returns raw tokens) | TODO | No — cosmetic |
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

### Milestone 3: Gameplay Screen — REVISED (Session 61)

**Goal**: Navigate to a song, start gameplay, see the dance stage.

**Status**: Menu screens render at 505 draw calls. game_screen loads but venue is EMPTY (78 HUD-only draws). Root cause fully diagnosed.

| Task | Status | Notes |
|------|--------|-------|
| Song select screen navigation | DONE | Full menu flow via input script |
| LightPreset stub removal | DONE | 12 stubs removed, real impl links. Session 59 |
| DataNode::GetObj graceful failure | DONE | Missing objects warn instead of crash. Session 61 |
| **FileMerger mMerger wiring** | **BLOCKER** | mMerger is null — embedded DTA in director.milo_xbox doesn't execute. See [Session 61](../../sessions/2026-03-12-session61-merger-investigation.md) |
| Venue merge into world.milo | BLOCKED | Needs mMerger. world_panel's world.milo is an empty shell without venue content |
| Song animation playback | BLOCKED | Needs mMerger → GetWorld() → song.anim PropAnim |
| LightPreset animation | BLOCKED | Needs song.anim → force_preset → LightPresetManager |
| Move card UI rendering | **TODO** | Pink rectangles — TexMovie render-to-texture needed |
| Score display | **TODO** | Not yet wired |
| Song audio playback during gameplay | **TODO** | FFmpeg backend ready, not wired to game flow |

**Critical path**: Fix mMerger wiring → venue loads → song.anim drives LightPresets → venue visible.

**Note**: The 505 draw calls seen in session 59 were from attract_screen's own world content, NOT from the venue. With real LightPreset::Load, those objects show with their correct serialized Showing states.

See [Session 61 Investigation](../../sessions/2026-03-12-session61-merger-investigation.md) for full diagnosis and fix options.

### Milestone 4: Playable Dance Gameplay

**Goal**: Full dance gameplay loop — see moves, hear music, track body, score.

| Task | Priority | Notes |
|------|----------|-------|
| Character animation in gameplay | HIGH | CharClip playback synced to beat. SongAnimation() returns -1 (clips present but not driven) |
| TexMovie render-to-texture | HIGH | FFmpeg RGBA → WebGPU texture upload. See [Session 60 Plan](../../sessions/2026-03-12-session60-plan-animation-texmovie.md) |
| LightPreset loading + animation | HIGH | Decomp source exists, just stubbed. 9 functions to un-stub |
| Song.anim graceful DTA failure | HIGH | Guard DataNode::GetObj for missing objects |
| Lip sync (CharFaceServo, CharLipSyncDriver) | MEDIUM | See [LIP_SYNC.md](../custom-graphics-engine/LIP_SYNC.md) |
| Procedural blinking (CharFaceServo) | LOW | Cosmetic |
| CharEyes gaze direction | LOW | Cosmetic |
| Motion capture integration (Phase 5) | HIGH | Kinect replacement via webcam + ML pose estimation |
| Scoring system verification | HIGH | Gesture matching → score calculation |

### Milestone 5: Polish

**Goal**: Full game experience.

| Task | Priority | Notes |
|------|----------|-------|
| Particle systems (RndParticleSys) | MEDIUM | Stage effects, confetti |
| Lines/Flares (RndLine, RndFlare) | LOW | Light beams, glow |
| Save/load game progress | MEDIUM | Profile, unlocks |
| DLC content loading | LOW | Extra songs |
| macOS / Windows support | MEDIUM | WebGPU handles backends |
| Web build (Emscripten) | LOW | Future |
| Performance optimization | MEDIUM | Draw call batching, culling |

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
