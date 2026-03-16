# Native Port TODO — Phase 6 Polish & Remaining Work

## Current State (Session 74)
- **Full gameplay pipeline operational** — Engine boots through menu → song select → game_screen with audio, rendering, animation, camera cuts, post-processing
- **6 venues tested** — dci, dclive, rollerrink, houseparty, streetside, throneroom — all render correctly
- **Per-song venue resolution** — Songs load their correct venue from metadata (HamSongMetadata::Venue())
- **~350-518 draw calls/frame** — varies by venue (houseparty=350, rollerrink=518)
- **Audio playback** — Real-time MOGG decoding (FFmpeg/Vorbis) → ring buffer → miniaudio output, drives songMs timing
- **Audio loading fix** — Game::IsLoaded() state 2 initiates audio from within load poll (fixes circular dependency)
- **Camera cuts** — song.anim PropKeys → HamDirector::SetShot() → CameraManager with 34+ keyframes/song
- **Post-processing** — Bloom (screen blend), Xbox-matched contrast/brightness, saturation, levels, vignette, chromatic aberration, posterization
- **Smart light selection** — DC3 doesn't use LightPresets; base illumination is point lights. Priority-weighted selection: default/stage > main > generic > backup > peak > rim. Point lights approximated as directional toward scene center.
- **Non-fatal Debug::Fail** — Matches Xbox "Continue" dialog (MILO_FATAL_FAILS=1 to restore abort)
- **Render-to-texture** — RndTexRenderer::DrawToTexture pipeline working (venue decorations)
- **Content system** — 62 songs load from DTA, 49 UI items in song_select_screen
- **RndFlare occlusion bypass** — SetVisible(true) + SetOcclusionResult(1.0) for in-view flares on native (no GPU occlusion query)
- **Stable 2500+ frames per song** — all tested songs run through gameplay without crash

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

## Historical: UI Layout Fix (Session 41) — RESOLVED
Transform::Multiply decomp bug (y/z coefficient swap in mtx.cpp) caused all transform compositions to produce wrong results. Fixed to 100% match. See `archive/screenshots/session41/` for before/after.

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

## Phase 4: Gameplay Visual Quality — COMPLETE
Goal: Character with proper materials, crowd, animated venue, gameplay HUD textures

### 4.1 Character Rendering — COMPLETE
- [x] Character material/texture application — **DONE** (zero-color LightPreset detection → brightness-sorted light selection)
- [x] Skinned mesh rendering (bone transforms in vertex shader) — **DONE** (Session 63: GPU skinning, 4-bone blending, 40-bone palettes)
- [x] Character dance animation pipeline — **DONE** (Session 63: ClipPlayer → HamDriver → bone transforms)

### 4.2 Merge Pipeline Stability — COMPLETE
- [x] Fix ObjRef ring corruption root cause — **DONE** (`ObjDirPtr(C*)` double-linking fix)
- [x] Fix SkeletonViz system-run resource loading — **DONE** (type fix + path canonicalization)
- [x] Audio merge validated — **DONE** (Session 67: full MOGG playback working)

### 4.3 Gameplay HUD
- [x] Move card textures (white rectangles) — **DONE** (Session 72: pose_flash_p0/p1 + preview.mesh filtered in MeshFilter)
- [x] Score/progress display — **DONE** (MeterDisplay uses real implementation; shows 0 without Kinect gesture scoring — expected behavior)

### 4.4 Scene Animation — COMPLETE
- [x] LightPreset::Load — **DONE** (Session 61)
- [x] Camera cuts — **DONE** (Session 68: song.anim → HamDirector → CameraManager)
- [x] Character dance animation — **DONE** (Session 63)
- [x] Light energy cap — **DONE** (Session 70: prevents overexposure across all 6 venues)
- [x] Smart light selection — **DONE** (Session 71: DC3 doesn't use LightPresets — base illumination uses point lights, not directional. Smart priority: default/stage > main > generic > backup > peak > rim)
- [x] WorldCrowd rendering — **DONE** (real DrawShowing implementation linked; crowd meshes visible in gameplay)

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
- [x] **Content system integration** (Phase 5): 62 songs load from DTA, 46 populate song_select list (49 items with headers). Full pipeline: ContentMgr → SongMgr → HamSongMgr → SongSortMgr → UIList. MoviePanel::IsLoaded() fix unblocks attract_screen transition. Engine boots to main_screen, auto-nav to song_select_screen works via `DC3_SCREEN` env var.

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

## Phase 6: Audio — COMPLETE (Sessions 67, 73)
- [x] Real-time MOGG decoding via FFmpeg/Vorbis/miniaudio
- [x] Ring buffer flow control (native ConsumeData with BytesWriteable check)
- [x] Song audio drives animation timing via songMs
- [x] UI/SFX audio — **DONE** (Session 73: XMA→PCM decoder via FFmpeg, 92 samples decoded from common_bank.milo, sample rate conversion 32kHz→44.1kHz)

## Phase 7: Post-Processing — COMPLETE (Sessions 69, 73)
- [x] Bloom (screen blend, Xbox-matched)
- [x] Contrast/brightness (non-linear Xbox formula from RndColorXfm)
- [x] Saturation, levels, vignette, chromatic aberration, posterization, DOF
- [x] Exotic effects (motion blur, gradient map, kaleidoscope, flicker, noise) — **NOT NEEDED** (debug/test effects never triggered in shipped game content)

## Phase 8: Polish & Remaining Gaps

### 8.1 Challenges System — COMPLETE (Session 74)
- [x] TheChallenges null crash fixed — enabled `Challenges::Init()` on native (constructor reads DTA config only, no Xbox Live calls)
- [x] `HasNewChallenges()` returns false (empty profile data — expected for native)
- [x] Root cause: blanket `#ifndef HX_NATIVE` suppression in MetaPanel.cpp included safe systems

### 8.2 Build Stability — COMPLETE (Session 74)
- [x] FreestyleMoveRecorder.cpp — PPC-only code (`__fsel` intrinsics, STLport) guarded with `#ifdef HX_NATIVE` stubs
- [x] MoveAsyncDetector.cpp — `stlpmtx_std` STLport namespace guarded for native
- [x] PostProc_NG.cpp Bloom_Blur — Xbox shader code (`SetBloomBlurWeights`, `unk14`) guarded
- [x] SpecialOfferEnumJob virtual dtor — implemented (Session 73)

### 8.3 Character Animation Verification — CONFIRMED (Session 74)
- [x] Characters animate in gameplay (NOT T-pose) — confirmed via 1920x1080 screenshots
- [x] Multiple dance poses visible across frames 800-1800 (YMCA)
- [x] Camera cuts working (different angles between frames)
- [x] Face rendering functional (head shapes with features visible)
- [x] Bone garbage mitigation in place (FillBoneUniforms sanity check)
- See `docs/native/ANIMATION_PIPELINE.md` for full pipeline documentation

### 8.4 Remaining Polish Items
- [ ] **Move graph loading** — `"Failed to load move graph for: <song>"` — DanceRemixer::Init requires TheMoveMgr->MoveParents() to be populated. Without this, autoplay/flashcards/phrase meters/scoring all dead. Root cause: TheMoveMgr is null on native (move routine generation suppressed). Need lightweight native stub or full MoveMgr init.
- [ ] **RockCentral::ManageJob unstub** — Currently guarded with `#ifdef HX_NATIVE delete job; return;`. The actual TheServer needs null-safe ManageJob or the network layer needs proper stub initialization. Crashes on `SendDropInDatapoint` / `SendDropOutDatapoint` during game start.
- [ ] **Flashcard rendering during game_screen** — `TheUI->Draw()` is skipped on game_screen (skipUIDraw) to prevent white rectangle artifacts. This also prevents flashcard_dock_panel from rendering. Need selective panel drawing or separate flashcard draw pass.
- [ ] **Phrase meter objects** — Need to verify phrase_meter0/phrase_meter1 exist in venue worlds. These are HamPhraseMeter objects looked up by MoveDir from the venue WorldDir.
- [ ] **Bone garbage root cause** — Some leg/foot bones produce invalid translations (~1e16). Currently mitigated by identity fallback. Need to trace through CharBones data to find source of corruption.
- [ ] **Face servo explicit polling** — Verify CharFaceServo is in Character::mPolls. If not, add explicit poll in gameplay path.
- [ ] **movemgr DTA error** — `movemgr not function or object` spam every frame during gameplay. TheMoveMgr is null on native; DTA scripts reference it. Low priority (cosmetic log noise).
- [ ] **Letterboxing artifact** — Some camera angles show black bars on right side. May be aspect ratio mismatch in camera framing.
- [ ] **Missing mmat references** — Shell/HUD materials can't find `shell_basic.mmat` in some .milo dirs. Cosmetic only (fallback material used).

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
| HUD move cards white rectangles | Kinect pose_flash render targets | **FIXED** (Session 72 — filtered pose_flash meshes) |
| Crowd/audio merges crash-skipped | Previously ObjRef ring corruption | **FIXED** (Session 67 — audio merge works, MOGG playback operational) |
| MoviePanel::IsLoaded blocks forever | mMovie.Ready() stub returns false | **FIXED** (Session 62 — `#ifdef HX_NATIVE` bypass) |
| Static scene (no animation) | Character anim + camera cuts + audio timing all working | **FIXED** (Sessions 63-68 — full gameplay animation pipeline) |
| Empty lists (no content) | Content system | **FIXED** (Session 62 — 49 items in song_select) |
| Blown-out bloom whites | Additive bloom composite | **FIXED** (Session 69 — screen blend prevents blowout) |
| Zero-color lights filling slots | LightPreset placeholders in LightsApprox | **FIXED** (Session 69 — brightness-sorted selection) |
| Post-proc not in screenshots | Headless check blocked post-proc | **FIXED** (Session 69 — removed `!mGpu.IsHeadless()` gate) |
| RndFlare invisible on native | No GPU occlusion query readback | **FIXED** (Session 69 — occlusion bypass) |
| All songs loading same venue | App.cpp hardcoded glitterati fallback | **FIXED** (Session 70 — venue metadata from HamSongMetadata) |
| DCI venue stuck on loading | Audio loading circular dependency in Game::IsLoaded() | **FIXED** (Session 70 — initiate audio from load poll) |
| Characters blown out/overexposed | All venue lights active simultaneously | **FIXED** (Session 70 — light energy cap) |
| DTA script crashes on native | Debug::Fail calls abort() | **FIXED** (Session 70 — non-fatal by default) |
| TheChallenges null crash on main menu | MetaPanel.cpp — Challenges::Init() suppressed | **FIXED** (Session 74 — enabled Init on native, reads DTA config only) |
| FreestyleMoveRecorder PPC intrinsics | __fsel, STLport not available on native | **FIXED** (Session 74 — native stubs for Kinect gesture code) |
| SpecialOfferEnumJob undefined vtable | HamStorePanel.cpp — missing dtor | **FIXED** (Session 73 — implemented dtor) |

## Crashes Fixed (Session 59)
1. ObjRef ring corruption producer bug in `ObjDirPtr(C*)` → fixed by removing the extra `AddRef`; direct lifetime regression test added
2. RndShadowMap::PrepShadow undefined → runtime link error (implemented)
3. RndFlare::CalcRect undefined → runtime link error (implemented)
4. SpotlightDrawer::RemoveFromLists undefined → runtime link error (implemented)
5. RndTexBlendController::GetBlendState undefined → runtime link error (implemented)
6. Signal handler consumed after first recovery (SA_RESETHAND removed)
7. All previous session crashes (see NATIVE_PORT_STATUS.md for full history)
