# Native Port TODO — UI Fully Working

## Current State (Session 59)
- **3D venue rendering on game_screen** — DCI venue with 505 draw calls/frame, 10000 frames stable (clean exit)
- Full menu navigation: main_screen → choose_mode → song_select → YMCA → multiuser → loading → game_screen
- Venue geometry (floor, walls, DJ booth, lighting rigs, graffiti), **fully-lit character** (skin, hair, outfit visible), HUD overlays all render
- Zero-color LightPreset detection enables fallback three-point lighting for character
- Flow→PropAnim UI animation pipeline verified working end-to-end (Session 58)
- Text rendering, mesh rendering, material pipeline all working
- HamUI two-pass draw pipeline active (letterbox + main draw pass)
- Scene is static: LightPreset::Load unimplemented (0 presets), song.anim DTA scripts crash on missing objects

## Headless GPU Rendering

dc3-native supports fully headless GPU rendering via Dawn/WebGPU (no display server needed):

```bash
# Headless render with auto-screenshots
MILO_RENDER=1 MILO_HEADLESS=1 \
  MILO_SCREENSHOT_DIR=archive/screenshots/session40 \
  MILO_SCREENSHOT_FRAMES=500,3500,3700 \
  MILO_INPUT_SCRIPT=path/to/input.txt \
  MILO_MAX_FRAMES=4000 \
  native/build/dc3-native

# Input script format (one "frame button" per line):
#   3500 down
#   3550 confirm
# Button names: start, confirm/a, cancel/b, up, down, left, right, option/back, x, y
```

Env vars:
- `MILO_RENDER=1` — enable GPU rendering (otherwise headless no-GPU mode)
- `MILO_HEADLESS=1` — skip window creation, render to offscreen buffer
- `MILO_SCREENSHOT_DIR=<dir>` — auto-capture frames as PNG
- `MILO_SCREENSHOT_FRAMES=<csv>` — which frames to capture (default: 100,600,900,1500)
- `MILO_INPUT_SCRIPT=<path>` — text file with timed button presses
- `MILO_MAX_FRAMES=<N>` — exit after N frames
- `MILO_FIRST_SCREEN=<name>` — skip attract/boot screens

## NEXT UP: UI Layout Fix (Session 41)

### Problem
UI elements render but are not positioned correctly. Comparing our output (`archive/screenshots/session40/frame_03500.png`) to the Xbox reference (`archive/screenshots/references/dc3_main_menu.jpg`):

| Element | Xbox Reference | Our Native | Issue |
|---------|---------------|------------|-------|
| **Player icons** | Top-left and top-right corners, ~100x100px, white outlined | Top-left and top-right, smaller, pink/magenta filled | Size, color, position offset |
| **Nav ribbon** | Right half of screen, "MAIN MENU" text with arrow | Center of screen, horizontal band with selection box | Position shifted, text missing |
| **Selection box** | N/A (main_screen has no selection box) | Center square with icon | Different screen content |
| **Logo** | "DANCE CENTRAL 3" left-center, large cyan text | Not visible | Missing or not rendered |
| **Copyright text** | Bottom center, white text | Not visible | Missing or not rendered |
| **Help bar** | Top bar: "EXIT CONTROLLER MODE" + "SELECT" | Not visible | Missing or not rendered |
| **Background** | Flowing blue/cyan neon lines | Plain dark gray | No venue/background rendering |
| **Kinect icon** | Bottom-right, "Say Xbox" | Bottom-right, small icon | Present but different style |

Note: The reference shows `main_screen` while our native shows `choose_mode_screen` — need to compare equivalent screens.

### Investigation Plan
1. **Camera/projection setup** — Is [ui.cam] positioned correctly? Check RndCam transform, FOV, aspect ratio
2. **RndTransformable world transforms** — Are mesh/group transforms being applied? Check if xfm matrices are loaded from .milo
3. **Coordinate system** — Milo uses a different coordinate convention (Y-forward?). Check if our projection matches
4. **Screen resolution** — Xbox renders at 1280x720. Are our viewport/projection matrices set up for this?
5. **Missing text** — Are RndText objects loading? Are font meshes being created? Check visibility/Showing state
6. **HelpBar rendering** — HamUI has a help bar system — is it entering/drawing?

### Key Files to Investigate
- `native/src/platform/Rnd_Wgpu.cpp` — Camera selection, projection setup, draw loop
- `native/src/platform/Mesh_Wgpu.cpp` — Transform application in DrawShowing
- `src/system/rndobj/Cam.cpp` — RndCam::UpdateLocal, projection matrix
- `src/system/rndobj/Trans.cpp` — RndTransformable::WorldXfm
- `src/system/ui/PanelDir.cpp` — Panel draw, camera setup
- `src/system/hamobj/HamUI.cpp` — HamUI::Draw, two-pass pipeline

## CRITICAL BLOCKER: DTA Loading Subsystem

**The native port cannot fully function without a DTA content/scripting system.** DTA (Data Array) files are the game's primary configuration and scripting format. They drive:

### What DTAs control
1. **Screen transitions** — DTA scripts define `next_screen`, screen flow logic, and transition triggers
2. **Content population** — List providers, mode definitions, song lists all come from DTA configs
3. **Animation lifecycle** — `StopAnimation()` calls that clean up `AnimTask` objects after enter animations complete
4. **UI initialization** — Panel enter/exit handlers, focus management, component wiring
5. **Object properties** — Material colors, animation ranges, timing parameters

### Current workarounds (native-only guards)
- `App.cpp`: 8 stub objects for Xbox-only managers (`platform_mgr`, `profile_mgr`, etc.)
- `App.cpp`: TheHamProvider fallback via PropertyEventProvider::NewObject()
- `UI.cpp`: Fallback button dispatch + mSink = screen on transition
- `HamNavList.cpp`: Bypass `IsAnimating()` check + TheHamProvider null guards
- `GestureMgr.cpp`: Force `mInControllerMode = true`
- `GameMode.cpp`: Skip full SetMode property evaluation on native
- `UI.cpp`: Screen auto-advance timer (replaces DTA-driven transitions)

## Phase 1: Interactive Menu Navigation — COMPLETE
- [x] Joypad input reaches UIManager (ButtonDownMsg dispatch)
- [x] mSink fallback dispatch to mCurrentScreen
- [x] Controller mode gate always-on
- [x] IsAnimating() bypass
- [x] ScrollDirection vertical mode fix (Up/Down navigation)
- [x] SetSelecting crash fix (TheHamProvider null)
- [x] GameMode::SetMode crash fix
- [x] Keyboard arrows → menu highlight movement (verified headless)
- [x] Confirm button → screen transition (verified headless)

## Phase 2: UI Layout & Visual Fidelity — COMPLETE
- [x] Camera & projection (sFlipYZ, Transform::Multiply decomp fix, [ui.cam] verified)
- [x] Transform hierarchy (WorldXfm from .milo, parent-child chain, PanelDir camera)
- [x] Text rendering (DXT5 alpha shader, depth test, backface cull, font loading)
- [x] Help bar & overlays (HamUI two-pass draw, voice-tip suppression)
- [x] Flow → PropAnim animation chain (verified Session 58)

## Phase 3: Song Loading & Venue Rendering — COMPLETE (Session 59)
- [x] Menu navigation to game_screen (input script driven)
- [x] Venue .milo loading (FileMerger → MergeDirs pipeline)
- [x] 3D venue rendering (DCI: floor, walls, DJ booth, lights — 391 draw calls)
- [x] Character mesh loading (silhouette visible)
- [x] HUD overlay rendering (move card geometry)
- [x] Crash recovery for merge failures (siglongjmp in FileMerger)

## Phase 4: Gameplay Visual Quality (CURRENT)
Goal: Character with proper materials, crowd, animated venue, gameplay HUD textures

### 4.1 Character Rendering
- [x] Character material/texture application — **DONE** (zero-color LightPreset detection enables fallback lighting)
- [ ] Skinned mesh rendering (bone transforms in vertex shader)
- [ ] Character dance animation (clips loaded but not driven)

### 4.2 Merge Pipeline Stability
- [x] Fix ObjRef ring corruption root cause — **DONE** (`ObjDirPtr(C*)` was double-linking the same ref node; `ObjRefConcrete` already links the node in its base ctor, so the extra `AddRef` corrupted the ring at creation time)
- [ ] Revalidate crowd character rendering now that the ring producer bug is fixed
- [ ] Revalidate audio merge now that the ring producer bug is fixed

### 4.3 Gameplay HUD
- [ ] Move card textures (currently pink rectangles — TexMovie render-to-texture)
- [ ] Score/progress display

### 4.4 Scene Animation
- [ ] **Implement LightPreset::Load** — currently stubbed, 0 presets deserialized from venue .milo
- [ ] Revalidate song/venue animation after the ObjRef ring fix, then narrow remaining blockers
- [ ] Venue lighting animation (still blocked by `LightPreset::Load`)
- [ ] Song.anim driving (remaining DTA crashes on missing game objects still need investigation)
- [ ] Character dance animation (clips present but SongAnimation() returns -1)

### 4.5 Loading State Machines — Analysis Complete (Session 60)

Ghidra DB analysis (22,397 decompiled functions) confirms all loading state setters/checkers are **fully decomped at 100%**. The loading pipeline itself is not the blocker — upstream subsystem init is.

**GamePanel::PollForLoading** (5 states, `src/lazer/game/GamePanel.cpp:908-954`):
- State 0: Check `TheGame->IsLoaded()`
- State 1: Wait for `HamDirector::OnFileLoaded` → merge pipeline
- State 2: Wait for `HamDirector::IsWorldLoaded`
- State 3: Call `HamDirector::Initialize` + `HudEntered`
- State 4: Done — `IsLoaded()` returns true

**Game::IsLoaded** (4 states, `src/lazer/game/Game.cpp:712-775`):
- State 0: `HamMaster::IsLoaded()` + world loaded + `PostLoad()`
- State 1: Move merger (requires `TheMoveMgr` — **null on native**)
- State 2: Audio ready (requires audio subsystem — **not init'd on native**)
- State 3: Done

**MetaPanel::IsLoaded** — has game_screen shortcut: if bottom screen is `game_screen`, returns `UIPanel::IsLoaded()` immediately (bypasses TheMetaMusic check). Our source already has `!TheMetaMusic` null guard.

**Conclusion**: The loading pipeline works end-to-end on native via null guards and state skips. The real gaps are the **null-on-native subsystems** (see priority list below). All Tier 1 stubs (flow, camera, animation) are now implemented — see STUB_BURNDOWN.md (updated 2026-03-13).

## Phase 5: DTA/Content System
Goal: Remove C++ workarounds and let real DTA screen-flow scripts drive the native port.

**Full plan**: [DTA_FLOW_V2_PLAN.md](DTA_FLOW_V2_PLAN.md)

- [x] **Smart stubs** (Phase 1): SaveLoadManager, ProfileMgr, PlatformMgr return sensible defaults
- [x] **Boot screen timers** (Phase 2): Intentional UX delays (permanent — no async Xbox events)
- [x] **Animation lifecycle** (Phase 3): AnimTask auto-null on native, removed HamNavList timer bypasses
- [x] **mSink investigation** (Phase 5a): DTA `set_sink` never fires in DC3 — fallback is permanent
- [x] **GameMode guard** (Phase 5b): `#ifdef HX_NATIVE` in constructor is correct and sufficient
- [x] **Debug logging cleanup** (Phase 6): All ~25 debug printfs gated behind `MILO_DEBUG_UI_FLOW=1`
- [x] **Remove multiuser auto-skip** (Phase 4): DTA `enter` handler drives game start naturally. IsAnimating() bypass in HamNavList.cpp enables button input.
- [ ] Content system integration for list population

## Null-on-Native Subsystems — Prioritized (Updated Session 60)

These globals are null on native because their init is suppressed with `#ifndef HX_NATIVE`. Prioritized by impact on gameplay pipeline.

### Priority A — Blocks Game::IsLoaded State Machine

| Global | Init suppressed at | Impact | Action |
|--------|-------------------|--------|--------|
| `TheMoveMgr` | Not initialized on native | Game::IsLoaded state 1 requires move merger. Currently skipped via null guard at `Game.cpp:556/677` | Implement lightweight native stub (no Kinect, just move routine generation) |
| `TheGameMode` | `GameMode.cpp:26` (DTA-driven init) | Controls game mode properties (difficulty, scoring rules). Null guard at `GameMode.cpp:242` | Init with hardcoded defaults for `perform` mode |

### Priority B — Blocks Content & UI Population

| Global | Init suppressed at | Impact | Action |
|--------|-------------------|--------|--------|
| `TheHamProvider` | Factory stub in App.cpp | Nav list content (song lists, mode lists). 6 null guards in `HamNavList.cpp` | Current PropertyEventProvider stub works; full impl needs content system |
| `TheCampaign` | `MetaPanel.cpp:95` (ctor skipped) | Campaign song select. Guard at `CampaignSongSelectPanel.cpp:137` | Low priority — perform mode doesn't need campaign |
| `TheMetaMusic` | `MetaPanel.cpp:95` (ctor skipped) | Shell music. Guards at `MetaPanel.cpp:280/293/307/340`. MetaPanel::IsLoaded has game_screen shortcut that bypasses this | Low priority — audio Phase 6 |

### Priority C — Platform/Kinect (Not Needed)

| Global | Init suppressed at | Impact | Action |
|--------|-------------------|--------|--------|
| `TheNetCacheMgr` | `System.cpp:493` | Xbox Live DLC cache. Guards at System.cpp, StorePanel, MainMenuPanel | Not applicable to native |
| `TheSkeletonIdentifier` | Kinect subsystem | Kinect player tracking. Guard at `HamUI.cpp:348` | Not applicable (controller mode) |
| `ThePassiveMessenger` | Kinect subsystem | Kinect gesture messages. Guard at `HamUI.cpp:348` | Not applicable (controller mode) |
| `TheCacheMgr` | Defensive | Cache manager. Guard at `System.cpp:227` | May already be init'd |

## Key DTA Config Files (Ghidra String Literal Search)

These `.dta`/`.dtb` files are loaded during boot and drive game configuration:

| File | Purpose | Loaded by |
|------|---------|-----------|
| `ham_preinit_keep.dta` | Pre-init persistent objects | HamUI early boot |
| `ham_keep.dta` | Persistent UI objects (screens, providers) | HamUI init |
| `flow.dtb` | Flow graph definitions (screen transitions, logic) | FlowDir |
| `loading_screens.dtb` | Loading screen configuration | Loading system |
| `gameconfig_macros.dtb` | Game config macros (difficulty, scoring) | GameConfig |
| `system.dtb` | System-level config (paths, memory, etc.) | SystemInit |

## Key DTA Handlers — HamDirector

HamDirector (`src/system/hamobj/HamDirector.cpp:137-202`) exposes ~40+ DTA handlers via `BEGIN_HANDLERS`. Key ones for the native loading pipeline:

| Handler | What it does | Native status |
|---------|-------------|---------------|
| `on_file_loaded` | Callback after .milo file load completes | Works (99.99% AT_LIMIT) |
| `on_file_merged` | Callback after FileMerger merges dirs | Works |
| `is_world_loaded` | Checks if venue world dir is ready | Works |
| `initialize` | Full director initialization after loading | Works |
| `hud_entered` | HUD panel entered callback | Works |
| `load_song` | Triggers song asset loading | Works |
| `remap_song_anim_to_tempo_map` | Maps song.anim to tempo | **STUB** (Tier 1) |

## Phase 6: Audio (LOW PRIORITY)
- [ ] UI click/select/scroll sounds via miniaudio backend
- [ ] Background music playback

## Phase 7: Post-Processing (LOW PRIORITY)
- [ ] Bloom, color correction, venue lighting effects
- [ ] Multiply blend mode (needs bright destination)

---

## Known Issues to Fix
| Issue | File | Status |
|-------|------|--------|
| HamRibbon::UpdateChase resize-before-copy UB | HamRibbon.cpp | **FIXED** |
| UIListWidget::DisplayColor assert on corrupted mElementState | UIListWidget.cpp | **FIXED** |
| IsAnimating() blocks input forever | HamNavList.cpp + Anim.cpp | **FIXED** (AnimTask auto-null) |
| mSink null — button dispatch broken | UI.cpp | **FIXED** (set on transition) |
| Controller mode gate blocks input | GestureMgr.cpp | **FIXED** (force on) |
| TheHamProvider null crash | HamNavList.cpp + App.cpp | **FIXED** (factory stub) |
| GameMode::SetMode crash | GameMode.cpp | **FIXED** (skip eval on native) |
| ScrollDirection vertical mode missing | Utl.cpp | **FIXED** (100% match) |
| Transform::Multiply decomp bug (y/z swap) | mtx.cpp | **FIXED** (Session 41) |
| UI elements mispositioned | Rendering pipeline | **FIXED** (Session 41) |
| Text labels missing | Text/Font pipeline | **FIXED** (Session 47) |
| Flow→PropAnim not animating | Animation pipeline | **FIXED** (Session 58) |
| ObjRef ring crash during venue merge | Dir.h `ObjDirPtr(C*)`, Object.cpp, FileMerger.cpp | **ROOT CAUSE FIXED** (extra `AddRef` removed); legacy validation/recovery guards still present pending crowd/audio/song revalidation |
| Character dark silhouette | Zero-color LightPreset lights | **FIXED** (Session 59 — fallback lighting) |
| Null crashes on game_screen (3) | HamCharacter/HamCamShot/PoseFatalities null ptrs | **FIXED** (Session 59) |
| HUD move cards pink rectangles | TexMovie render-to-texture | TODO — Phase 4 |
| Crowd/audio merges crash-skipped | Previously ObjRef ring corruption; now needs fresh runtime validation after ctor fix | TODO — Phase 4 |
| Static scene (no animation) | ObjRef ring bug was a shared blocker; remaining blockers appear to be `LightPreset::Load` + song.anim DTA object gaps | TODO — Phase 4 |
| Empty lists (no content) | Content system | TODO — Phase 5 |

## Crashes Fixed (Session 59)
1. ObjRef ring corruption producer bug in `ObjDirPtr(C*)` → fixed by removing the extra `AddRef`; direct lifetime regression test added
2. RndShadowMap::PrepShadow undefined → runtime link error (implemented)
3. RndFlare::CalcRect undefined → runtime link error (implemented)
4. SpotlightDrawer::RemoveFromLists undefined → runtime link error (implemented)
5. RndTexBlendController::GetBlendState undefined → runtime link error (implemented)
6. Signal handler consumed after first recovery (SA_RESETHAND removed)
7. All previous session crashes (see NATIVE_PORT_STATUS.md for full history)
