# Native Port Convergence Plan

**Goal**: Make the native port operate 1:1 with the Xbox version — same DTA flows, same panel hierarchy, same loading pipeline. Diverge only where necessary (graphics API, 64-bit platform, input abstraction).

**Status**: Phase 1 complete. game_screen reached through full DTA flow. Phase 2-3 next.

**Working docs**: `docs/sessions/convergence/` (audit reports 01-06)

---

## High-Level Objectives

| # | Objective | Status | Acceptance Criteria |
|---|-----------|--------|---------------------|
| # | Objective | Status | Acceptance Criteria |
|---|-----------|--------|---------------------|
| 0 | Audit & scope all HX_NATIVE blocks | DONE | 5 audit docs written, synthesis complete |
| 1 | Fix song select input + navigate to gameplay | DONE | YMCA input script reaches `game_screen` (5000 frames stable) |
| 2 | Venue + character loading through DTA flow | TODO | `HamDirector::Enter()` fires, venue renders |
| 3 | Gameplay boot: song plays, characters dance | TODO | GamePanel reaches `kGamePlaying`, move cards visible |
| 4 | Remove scaffolding hacks | TODO | `gNativeVenueDir`, `NativeVenueInit`, App.cpp blocks removed |
| 5 | UI parity: menus match Xbox | TODO | `background_panel` turbo_shell renders, PostProc applies |
| 6 | End-to-end: boot → menu → song → gameplay → endgame | TODO | Full YMCA flow completes without crash |

---

## Current Baseline (2026-03-21)

### What Works
- Boot through attract → autosave → title → main_screen (DTA-driven, with auto-advance)
- Main menu navigation: "gameplay" → choose_mode → "perform" → song_select (HamNavList works)
- Panel loading and display (DirLoader, PanelDir::DrawShowing)
- Async FileMerger pipeline (TheLoadMgr.Poll every frame, organizer queues)
- Audio subsystem (miniaudio, Vorbis streams, MIDI)
- WebGPU renderer (meshes, materials, cameras, environ, PostProc, text)
- Input scripting (MILO_INPUT_SCRIPT)

### What Doesn't Work
- **Song select screen**: Input (scroll/confirm) doesn't register — flow gets stuck
- **No DTA gameplay navigation**: `enter_gameplay()` never fires
- **No game_screen**: world_panel never loads, HamDirector.mMerger never wires
- **Venue through hacks only**: gNativeVenueDir + NativeVenueInit bypass the DTA flow
- **No character choreography**: Characters load but no dance animations fire
- **No song playback during gameplay**: Game::LoadSong chain doesn't execute

### Key Numbers
- 865 `#ifdef HX_NATIVE` blocks across 295 files
- 71 SCAFFOLD blocks (to be replaced with convergent implementations)
- 177 PLATFORM blocks (permanent, legitimate differences)
- 131 BUGFIX blocks (permanent, crash prevention)
- 65 STUB blocks (permanent, Xbox-only features)
- 97 DEBUG blocks (diagnostic logging)

---

## Phase 1: Song Select → Gameplay Navigation

**Objective**: Fix song select input handling and navigate from song select through the Xbox DTA loading chain to `game_screen`.

### 1.1 Song Select Input Fix
The song select screen uses a UIList (not HamNavList). Scroll/confirm inputs don't reach it. Investigate:
- Is the UIList getting focus?
- Is IsAnimating() blocking input? (README.txt mentions this for HamNavList)
- Does the song browser need controller-mode-specific input handling?

### 1.2 DTA Navigation Chain
Once song select works, verify the Xbox flow fires:
```
song selected → enter_gameplay() [global.dta:212]
  → initialize_gameplay_data
  → loading_screen → preloading_screen → real_loading_screen
  → game_screen
```

### 1.3 Verify game_screen Enter
When `game_screen` enters, verify:
- `world_panel` loads `world.milo`
- `world.fm` fires `change_files` → `{$hamdirector set merger $this}`
- `HamDirector.mMerger` is set

**Test**: `MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt MILO_MAX_FRAMES=5000`
**Pass criteria**: Log shows `Screen 'game_screen' Enter`

---

## Phase 2: Venue + Character Loading

**Objective**: Venue and characters load through the async FileMerger pipeline, not through native hacks.

### 2.1 Song/Venue/Character Loading Chain
```
Game::LoadSong() → HamDirector::OnLoadSong()
  → mMerger->Select("song", ...)
  → mMerger->StartLoad(async=true)
  → on_file_loaded("song") → LoadCharacters + Select("venue") + Select("viz")
  → on_file_loaded("venue") → mVenue set
```

### 2.2 GamePanel::PollForLoading Gates
All 5 gates must pass:
1. UIPanel::IsLoaded()
2. world_panel on transition screen
3. TheHamDirector->IsWorldLoaded()
4. TheHamWardrobe->AllCharsLoaded()
5. mGame->IsReady()

### 2.3 Crew/Outfit Population
Either implement DTA player selection flow or ensure DC3_SCREEN auto-nav populates HamPlayerData fully before OnLoadSong.

**Test**: Add logging to PollForLoading gates, run YMCA flow
**Pass criteria**: All 5 gates pass, game transitions to gameplay

---

## Phase 3: Gameplay Boot

**Objective**: HamDirector enters, venue initializes, song plays, characters have choreography.

### 3.1 HamDirector::Enter() fires
When world_panel enters → HamDirector::Enter():
- VenueEnter(mVenue)
- Initialize() → SetupAnims()
- SyncScene() → SetNewWorld()
- PlayIntroShot()

### 3.2 GamePanel State Machine
```
kGameLoading → kGamePreIntro → kGameInIntro → kGamePlaying
```
Currently has HX_NATIVE hack to force-advance past kGameInIntro after 30 frames. This is acceptable scaffolding until audio timing is right.

### 3.3 Character Choreography
- MoveMgr::ResetRemixer() populates routine builder anims
- SongAnim(0)->StartAnim() starts the choreography timeline
- Characters should animate through the song

**Test**: Screenshot capture at frame 500 after game_screen enters
**Pass criteria**: Venue visible, characters in frame, move cards displayed

---

## Phase 4: Hack Removal

**Objective**: Remove scaffolding, let DTA flow drive everything.

### Removal Order (dependency-safe)
1. Remove App.cpp pre-game venue draw (lines 1142-1150) — venue draws through world_panel
2. Remove App.cpp venue poll/setup block (lines 1033-1131) — HamDirector polls venue
3. Remove NativeVenueInit() from Rnd_Wgpu.cpp — HamDirector::Enter() handles venue init
4. Remove gNativeVenueDir global + ObjectDir::AddedSubDir hook — mVenue set by DTA
5. Remove HamDirector crew/outfit fallback (lines 1008-1045) — DTA populates data
6. Remove HamDirector move remixer init (lines 546-562) — DTA modular.fm handles it

### Keep (permanent adaptations)
- FlowQueueable ObjPtrList (cascade safety)
- FileMerger cascade guards (deletion safety)
- FileMerger scope-resolution blocks (native MergeDirs behavior)
- ObjectDir cascade protection infrastructure
- GamePanel intro force-advance (until Kinect/audio timing converges)

**Test**: Full YMCA flow after each removal, verify no regression
**Pass criteria**: Same behavior with fewer HX_NATIVE blocks

---

## Phase 5: UI Parity

**Objective**: Menu screens look like Xbox. Turbo_shell background renders with PostProc.

### 5.1 Background Panel Rendering
- Verify background_panel loads and renders through standard panel hierarchy
- Verify PostProc_Blacklight.pp applies to turbo_shell scene
- Verify Flow animations run (gradients, glow, scanlines pulse)

### 5.2 PostProc Flush Timing
- Add FlushPostProcessingForOverlay() in EndWorld path
- PostProc affects background but not UI text overlay

### 5.3 Clear Color
- Set clear color to black (turbo_shell provides full-screen coverage)

**Test**: Screenshot capture on main_screen, compare to `archive/screenshots/references/dc3_main_menu.jpg`
**Pass criteria**: Visual similarity to Xbox main menu

---

## Journal

### 2026-03-21 — Session Start
- Audited original session doc, found blind spots (GamePanel, FlowQueueable, FileMerger blocks missing)
- Launched 5-agent audit fleet: HX_NATIVE inventory, boot-to-gameplay flow, dependency graph, background panel rendering, FileMerger async pipeline
- All audits complete: 865 blocks across 295 files cataloged
- Smoke tested native port: boots to song_select_screen, stuck there (input not registering)
- YMCA flow: main_screen enters at frame ~300, choose_mode at ~500, song_select at ~800, no further transitions
- Background panel CAN render on native (all features supported by WebGPU renderer)
- Async FileMerger pipeline WORKS on native (TheLoadMgr.Poll every frame)
- Key blocker: song select input handling prevents gameplay navigation

### 2026-03-21 — Milestone 1: Menu Flow Fixed
- Fixed HamPanel::Exiting() — return false on HX_NATIVE (gesture animations block transitions)
- Fixed GetNeutralSkeleton() — skip on native (PPC hardcoded offsets crash on x86_64)
- Fixed HamIKSkeleton::Poll() + CharBones::Zero() — null guards for uninitialized skeletons
- Flow now reaches: boot → attract → title → main → choose_mode → song_select → multiuser → loading_screen
- Committed: `native: fix menu flow — song select through loading screen now works`
- **Next blocker**: loading_screen → preloading_screen transition hits heap corruption
  - `malloc(): mismatching next->prev_size (unsorted)` during loading_screen Enter
  - Happens after Rnd::SetPostProcOverride cleanup (multiuser → loading transition)
  - Likely a use-after-free or buffer overrun in the PostProc or panel unload path
  - Need ASan build to diagnose

### 2026-03-21 — Technical Debt Cleanup
- **UIScreen::UnloadPanels()**: Removed `#ifdef HX_NATIVE return` skip. Cascade protection infrastructure (InDeleteObjects guards in ObjPtr/ObjPtrList/ObjRefConcrete destructors, DeferFree) now handles the use-after-free scenarios that originally motivated the skip. Panels will actually unload during screen transitions, fixing the memory leak.
- **GetNeutralSkeleton()**: Removed early return skip. The PPC-specific `reinterpret_cast` with hardcoded 0x10 offset was already replaced by `static_cast<CharBones*>` in `#ifdef HX_NATIVE` branches at the two cast sites. The full skeleton blending pipeline now runs on native.
- **Flow::~Flow() mRunningNodes**: Replaced no-op cascade skip with explicit `mRunningNodes.clear()`. During cascade, this safely destroys ObjPtrList nodes (their destructors skip ring unlinks via InDeleteObjects check) without triggering Deactivate's message-sending to potentially-destroyed listeners.

---

## Reference Docs

| Document | Purpose |
|----------|---------|
| `docs/sessions/convergence/01-hx-native-audit.md` | Complete HX_NATIVE block inventory |
| `docs/sessions/convergence/02-boot-to-gameplay-flow.md` | Xbox vs Native boot flow |
| `docs/sessions/convergence/03-dependency-graph.md` | Hack dependency graph + removal order |
| `docs/sessions/convergence/04-background-panel-rendering.md` | Background panel rendering check |
| `docs/sessions/convergence/05-filemerger-async-pipeline.md` | Async loading pipeline analysis |
| `docs/sessions/convergence/06-synthesis-final.md` | Synthesis document (implementation guide) |
| `docs/sessions/2026-03-20-dta-venue-flow-convergence.md` | Original analysis |
| `docs/debugging/native.md` | Debugging techniques (includes ObjRef ring debugging) |
| `archive/screenshots/references/` | Xbox target screenshots |
