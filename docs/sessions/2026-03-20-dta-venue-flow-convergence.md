# DTA Venue Flow Convergence

**Date**: 2026-03-20
**Status**: Analysis complete, roadmap defined
**Goal**: Remove native venue hacks, converge with Xbox's DTA-driven venue/character pipeline

## Background: How Xbox Works

### Menu Screens — Shell Background, No Gameplay Venue

Xbox menu screens do **not** load a gameplay venue (WorldDir). The menu background
is a UIPanel called `background_panel` that loads `background.milo` — internally
called the **turbo_shell**. Despite being "just a background", this is a full 3D
PanelDir scene rendered through its own camera and post-processing pipeline:

**Cameras**: `turbo_shell.cam` (main), `turbo_shellbg.cam` (background), `camera1.cam`

**Meshes** (flat screen-space geometry, not a 3D environment):
- Gradients: `bg_gradient.mesh`, `bg_gradient1.mesh`
- Frames/chrome: `frames_bg01-04.mesh`, `frames_glow_01-04.mesh`, `rt_frames_*`
- Surface effects: `bg_diagonal_bars.mesh`, `surface_scanline_lft.mesh`,
  `surfaceTelelines_lft.mesh`, `geo_surfaceTeleline_rght.mesh`

**Materials**: `shell_basic.mmat`, `shell_basic_wrap.mmat`, per-element mats

**Animations**: `bump.anim`, `diagonal.anim`, `eq4.anim`, `pulse.anim`,
`fade.anim`, `overlay_colorswitch.anim`

**Post-processing**: `PostProc_Blacklight.pp`, with `Environ` for lighting.
The `shell` type in `world_objects.dta:1880` configures postprocessing for this
environment (selects a PostProc object on enter, resets on exit).

```
main_screen:                                 [main.dta:176-177]
  (panels meta background_panel main_panel main_menu_wait_for_content_panel)

attract_screen:                              [main.dta:233-235]
  (panels attract_movie_panel movie_overlay_panel)
  → plays a video, not a 3D venue
```

`background_panel` is used by ~30+ screens (song select, store, campaign, pause,
etc.). Variants include `background_left_panel`, `background_right_panel`,
`background_confirmation_panel`, `background_endgame_panel`, `background_pause_panel`.
All defined in `ui/background/background.dta`. `HamUI::Draw()` (HamUI.cpp:225)
draws `mBackgroundPanel` during dialog events. `HamUI::InitPanels()` finds it via
`ObjectDir::Main()->Find<UIPanel>("background_panel")`.

There is no `world_panel` on any menu screen. Gameplay venue rendering (WorldDir)
only happens during gameplay via `game_screen`.

### The Gameplay Entry Flow

On Xbox, venues and characters are loaded entirely through DTA screen transitions
and the FileMerger async pipeline. No globals, no manual Enter() calls, no polling
hacks. The flow:

```
DTA: enter_gameplay()                        [global.dta:212]
  → initialize_gameplay_data
  → loading_screen                           [loading.dta:32-40]
    → goto_screen preloading_screen          [loading.dta:72-76]
      → preload_panel on_preload_ok          [loading.dta:63-71]
        → goto_screen real_loading_screen    [loading.dta:77-89]
          → {gamemode get game_screen}       [the gameplay UIScreen]

game_screen panels:                          [game.dta:163-164]
  game_panel, world_panel, rhythm_detector_panel,
  bustamove_visualizer_panel, bustamove_panel,
  flashcard_dock_panel, fitness_hud_panel

world_panel:                                 [game.dta:9-14]
  (file "../world/world.milo")
  (unload_async TRUE)
```

### World Panel Loading

`world_panel` loads `world.milo`, a 3.3KB skeleton containing `world.fm`
(a FileMerger). When `world.fm`'s `change_files` handler fires, it wires
`HamDirector.mMerger = world.fm` and calls `load_game_song`.

### Song/Venue/Character Loading Chain

```
Game::LoadSong()                             [Game.cpp:584]
  → MetaPerformer::Handle(Message("on_load_song"))  [Game.cpp:590]
    → HamDirector::OnLoadSong()              [HamDirector.cpp:1001, via message dispatch]
      → mMerger->Select("song", song.milo)
      → mMerger->StartLoad(async=true)

on_file_loaded("song"):                      [HamDirector.cpp:1179]
  → TheHamWardrobe->LoadCharacters(outfits, crews, venue, async=true)
  → mMerger->Select("venue", "world/<name>/<name>.milo")
  → mMerger->Select("viz", visualizer.milo)
  → mGameModeMerger->StartLoad()
  → mMerger->StartLoad()

on_file_loaded("venue"):                     [HamDirector.cpp:1218]
  → mVenue = dynamic_cast<WorldDir*>(dir)
```

All loading is async. The FileMergerOrganizer queues mergers and processes
them incrementally through the normal `LoadMgr::Poll()` in the main loop.

### Character Outfit Loading (HamWardrobe)

`TheHamWardrobe->LoadCharacters()` is called from `on_file_loaded("song")`:

```
HamWardrobe::LoadCharacters(outfit1, outfit2, crew1, crew2, backupType, speed, venue, async)
  → LoadMainCharacter(0, outfit1, async)     [HamWardrobe.cpp:434]
    → HamCharacter::SetOutfit(outfit)
    → HamCharacter::StartLoad(async)
      → FileMerger::StartLoad(async)
        → StartLoadInternal(async, false)
          → Message("change_files").HandleType()
            → HamCharacter::OnConfigureFileMerger()
              → mFileMerger->Select("outfit", outfitPath)
              → Select("vo_bank", voPath)
              → Select("viseme", visemePath)
          → AppendLoader() for each dirty merger
          → TheFileMergerOrganizer->AddFileMerger(this)  [async path]
  → LoadMainCharacter(1, outfit2, async)
  → LoadBackup(0, backupOutfit, async)
  → LoadBackup(1, backupOutfit, async)
  → LoadCrowdClips(speed, venue, async)
```

### Game Ready Gate

`GamePanel::PollForLoading()` gates the transition to gameplay:

```
GamePanel::PollForLoading()                  [GamePanel.cpp:929]
  1. UIPanel::IsLoaded()?                    ← base panel loaded (line 936)
  2. world_panel on transition screen?       ← find world_panel (line 938)
  3. TheHamDirector->IsWorldLoaded()?        ← venue merge complete (line 944)
  4. TheHamWardrobe->AllCharsLoaded()?       ← all characters loaded (line 950)
  5. mGame->IsReady()?                       ← game ready (line 954)
  → Once all true: mPollLoadState reaches 4, game_screen can Enter()
```

### HamDirector::Enter() — The Venue Initialization

When `game_screen` enters, `world_panel` enters, which triggers
`HamDirector::Enter()`:

```
HamDirector::Enter()                         [HamDirector.cpp:321]
  → Initialize post-proc state (world.pp, world_start.pp)
  → TheHamWardrobe->ClearCrowd()
  → VenueEnter(mVenue)                      [line 357]
    → mVenue->Enter()                       [WorldDir::Enter — lights, physics, objects]
    → Find player0/1, backup0/1 HamCharacter objects
    → Find & reset transform nodes to identity
    → Reset mCharsShowing[]
  → Initialize() → SetupAnims()             [difficulty-specific song anims]
  → SongAnim(0)->StartAnim()
  → SyncScene() → SetNewWorld()
    → TheHamWardrobe->SetDir(mVenue)
    → GetWorld()->SetSphere(mVenue->GetSphere())
  → PlayIntroShot()
```

### Key DTA Messages in the Pipeline

| Message | Sender | Recipient | Purpose |
|---------|--------|-----------|---------|
| `load_characters` | Game code | HamWardrobe | Trigger outfit loading |
| `set_venue` | Game code | HamWardrobe | Menu venue init (alt path) |
| `change_files` | FileMerger | HamCharacter | Translate outfit symbol → file paths |
| `on_file_loaded` | FileMerger | HamDirector | Venue/song/viz load complete |
| `on_pre_merge` | FileMerger | Self | Pre-merge hook |
| `on_post_delete` | FileMerger | HamCharacter | Post-load (fires clip group sync) |
| `configure_file_merger` | DTA | HamCharacter | Configure outfit/vo/viseme paths |

## What the Native Port Does Today

### The Problem

The native port never navigates to `game_screen` during menu/attract flow.
Without `game_screen`, `world_panel` never loads, `HamDirector::Enter()` never
fires, and the DTA venue pipeline never runs.

Instead, the native port uses 6 scaffolding hacks:

### Hack 1: `gNativeVenueDir` Global

**File**: `src/system/obj/Dir.cpp:684-701`, declared in `src/system/world/Dir.cpp:32`

```cpp
// ObjectDir::AddedSubDir — watches for "chars_base" to capture venue dir
#ifdef HX_NATIVE
if (dir && dir->Name() && strcmp(dir->Name(), "chars_base") == 0) {
    extern ObjectDir *gNativeVenueDir;
    gNativeVenueDir = this;
}
#endif
```

**Xbox equivalent**: `mVenue` is set by `on_file_loaded("venue")` callback in
HamDirector. No global needed.

### Hack 2: `NativeVenueInit()` — Manual Venue Enter

**File**: `native/src/platform/Rnd_Wgpu.cpp:819-866`

Called every frame from `BeginDrawing()`. Detects when `gNativeVenueDir` changes
or its hash table grows (content loaded), then manually calls `venue->Enter()` or
`TheHamDirector->VenueEnter(venue)`.

**Xbox equivalent**: `HamDirector::Enter()` → `VenueEnter(mVenue)`, triggered by
`world_panel` entering.

### Hack 3: Venue Poll/Setup Block

**File**: `src/App.cpp:1037-1131` (~95 lines)

- Hides Kinect-dependent meshes (TVScreen, projections, reflections, render targets)
- Polls venue WorldDir for menu screens (when no HamDirector)
- Resets character root positions to prevent root-motion drift

**Xbox equivalent**: HamDirector polls venue via `ListPollChildren()`. Mesh
visibility configured by DTA/artist settings. Character positions managed by
choreography system.

### Hack 4: Pre-game Direct Draw

**File**: `src/App.cpp:1142-1150`

```cpp
#ifdef HX_NATIVE
if (!TheHamDirector && gNativeVenueDir) {
    WorldDir *menuVenue = dynamic_cast<WorldDir*>(gNativeVenueDir);
    if (menuVenue) menuVenue->DrawShowing();
}
#endif
```

**Xbox equivalent**: Panel hierarchy draws venue: `world_panel` →
`PanelDir::DrawShowing()` → `HamDirector::DrawShowing()` → `mVenue->DrawShowing()`.

### Hack 5: Crew/Outfit Fallback

**File**: `src/system/hamobj/HamDirector.cpp:1008-1045`

Reconstructs crew/outfit data when single-player flows skip parts of the DTA
initialization chain (`select_player`, `select_crew` handlers never fire).

**Xbox equivalent**: DTA handlers populate `HamPlayerData` fields before
`OnLoadSong` processes them.

### Hack 6: Move Remixer Init

**File**: `src/system/hamobj/HamDirector.cpp:546-562`

```cpp
#ifdef HX_NATIVE
if (TheMoveMgr && TheMoveMgr->mSuperEasyRemixer) {
    TheMoveMgr->mSuperEasyRemixer->Init();
    if (mPlayer1RoutineBuilderAnim && mPlayer2RoutineBuilderAnim)
        TheMoveMgr->ResetRemixer();
}
#endif
```

**Xbox equivalent**: DTA reset handler from `modular.fm`'s `change_files`
callback fires choreography init.

### Other Minor Hacks

- `HamDirector.cpp:1220-1226` — `video_recorder.srec` stub (Kinect)
- `HamDirector.cpp:2244-2252` — `PlayNextShot` fallback to `Area1_WIDE` camera
- `HamWardrobe.cpp:286-300` — `SyncInterestObjects` diagnostic logging

## Gap Analysis

### Gap 1: No `game_screen` Navigation

The native port stays on menu screens (`attract_screen` → `main_screen` → etc.).
It never calls `enter_gameplay()` or navigates to `game_screen`. Without
`game_screen`, the entire Xbox pipeline — `world_panel`, `GamePanel`,
`HamDirector::Enter()` — is dormant.

**Fix**: When the user triggers gameplay (via `DC3_SCREEN=game_screen` or normal
menu flow), the DTA `enter_gameplay()` function must fire. This requires:
- `TheGameData` populated (song, venue, player data)
- `TheGameMode` set (perform/practice)
- DTA screen transition chain working through loading screens

The native port's `DC3_SCREEN` auto-nav in App.cpp already handles some of this
(lines 1162-1216), but it may not trigger `enter_gameplay()` — it just calls
`TheUI->GotoScreen()` directly.

### Gap 2: Missing DTA Player Selection Flow

On Xbox, the flow from song select to gameplay populates `HamPlayerData` through
DTA handlers (`select_player`, `select_crew`, character outfit selection). The
native port skips this, which is why Hack 5 (crew/outfit fallback) exists.

**Fix**: Either implement the DTA player selection handlers, or ensure
`TheGameData->Player(i)` is fully populated before `OnLoadSong` runs. The
existing `DC3_SCREEN=game_screen` auto-nav code (App.cpp:1177-1216) already sets
song/venue/mode but may not set player outfit/crew data.

### Gap 3: Menu Venue Display Is a Native-Only Invention

Xbox menu screens use `background_panel` (the turbo_shell — a PanelDir with flat
meshes, animations, and post-processing, but **no gameplay venue/WorldDir**). The
attract screen plays a video. The native port's concept of "show a gameplay venue
on the main menu" doesn't exist on Xbox. The `gNativeVenueDir` hack,
`NativeVenueInit`, the App.cpp direct draw — all replace something that was never
there.

**Fix**: Remove all menu-venue code. Let `background_panel` render through the
normal panel system. The turbo_shell scene (gradients, frames, glow effects) draws
via the standard PanelDir draw path. Gameplay venues (WorldDir) only appear during
gameplay via `game_screen` → `world_panel` → `HamDirector`.

### Gap 4: `FileMerger::sDisableAll` (Low Risk)

Defined only under `#ifdef HX_NATIVE` (FileMerger.cpp:22). Zero-initialized to
`false`, but `SYNC_PROP(disable_all, ...)` means DTA config could set it. Should
verify at runtime that it stays `false`.

## Roadmap

### Phase 1: Gameplay Flow Convergence

**Goal**: Get `game_screen` working through the Xbox DTA flow.

1. **Verify `enter_gameplay()` fires correctly.** The DTA function
   `enter_gameplay()` (global.dta:212) initializes game data and navigates
   through loading screens to `game_screen`. Trace whether this runs when the
   native port navigates to `game_screen`.

2. **Ensure `TheGameData` is fully populated.** Before `game_screen` loads,
   `TheGameData` needs song, venue, player outfits, crew, game mode. The
   App.cpp auto-nav code partially does this — verify what's missing.

3. **Verify `world_panel` loads and enters.** When `game_screen` enters,
   `world_panel` should load `world.milo`, wire `HamDirector.mMerger`, and
   trigger `load_game_song`. Check that this fires on native.

4. **Verify `HamDirector::Enter()` fires.** Once `world_panel` enters,
   `HamDirector::Enter()` should call `VenueEnter(mVenue)`. If this works,
   the entire venue init pipeline runs without native hacks.

5. **Verify character loading.** `on_file_loaded("song")` should trigger
   `TheHamWardrobe->LoadCharacters()` with async=true. Characters load through
   FileMergerOrganizer. `GamePanel::PollForLoading()` gates on
   `AllCharsLoaded()`.

### Phase 2: Remove Native Hacks

Once Phase 1 is verified working:

1. **Remove `NativeVenueInit()`** and its calls from `BeginDrawing()`.
   Remove `mVenueInited`, `mLastVenueDir`, `mLastVenueHashSize` members.

2. **Remove `gNativeVenueDir`** global and the `#ifdef HX_NATIVE` detection
   in `ObjectDir::AddedSubDir()`. Remove all App.cpp references.

3. **Remove App.cpp venue block** (lines 1037-1131). Venue polling moves to
   `HamDirector::ListPollChildren()`. Kinect mesh hiding either moves to a
   one-shot init in `HamDirector::VenueEnter()` or is handled by DTA config.

4. **Remove App.cpp direct draw** (lines 1142-1150). Venue draws through
   `world_panel` → panel hierarchy.

5. **Remove HamDirector crew/outfit fallback** (lines 1008-1045). DTA
   player selection flow populates fields before `OnLoadSong`.

6. **Remove HamDirector move remixer init** (lines 546-562). DTA
   `modular.fm` reset handler fires choreography init.

### Phase 3: Remove Menu Venue Hacks

Xbox uses the turbo_shell `background_panel` on menu screens — no gameplay
venue/WorldDir. Remove:

1. **`gNativeVenueDir`** global, the `#ifdef HX_NATIVE` hook in
   `ObjectDir::AddedSubDir()`, and its declaration in `Dir.cpp`.

2. **`NativeVenueInit()`** entirely — it only exists for menu venues. During
   gameplay, `HamDirector::Enter()` → `VenueEnter()` handles venue init.
   Remove `mVenueInited`, `mLastVenueDir`, `mLastVenueHashSize` members and
   both `NativeVenueInit()` calls from `BeginDrawing()`.

3. **App.cpp venue poll/setup block** (lines 1037-1131) — Kinect mesh hiding,
   venue polling, character position reset. All menu-venue-specific.

4. **App.cpp pre-game direct draw** (lines 1142-1150) — `menuVenue->DrawShowing()`
   bypass. Menu screens draw through `background_panel` via `TheUI->Draw()`.

Verify that `background_panel` loads and renders correctly on native before
removing. It should already work through the normal panel system — it's just
a UIPanel loading a .milo file.

### Key Files

| File | Lines | What |
|------|-------|------|
| `src/system/hamobj/HamDirector.cpp` | 321-379 | `Enter()` — venue init |
| `src/system/hamobj/HamDirector.cpp` | 591-639 | `VenueEnter()` — character setup |
| `src/system/hamobj/HamDirector.cpp` | 1001-1077 | `OnLoadSong()` — merge chain |
| `src/system/hamobj/HamDirector.cpp` | 1179-1234 | `OnFileLoaded()` — merge callbacks |
| `src/system/hamobj/HamWardrobe.cpp` | 434-499 | `LoadCharacters()` — outfit pipeline |
| `src/system/hamobj/HamCharacter.cpp` | 313, 560 | `StartLoad()`, `OnConfigureFileMerger()` |
| `src/system/char/FileMerger.cpp` | 467-507 | `StartLoadInternal()` — async/sync path |
| `src/system/char/FileMergerOrganizer.cpp` | 233 | `AddFileMerger()` — async queue |
| `src/lazer/game/Game.cpp` | 584-615 | `LoadSong()` — entry point (message-dispatched to HamDirector) |
| `src/lazer/game/GamePanel.cpp` | 929-962 | `PollForLoading()` — ready gate |
| `orig-assets/extracted/ui/game.dta` | 9-14, 163 | `world_panel`, `game_screen` defs |
| `orig-assets/extracted/ui/global.dta` | 212 | `enter_gameplay()` DTA function |
| `orig-assets/extracted/ui/loading/loading.dta` | 32-89 | Loading screen chain (loading→preloading→real_loading) |
| `orig-assets/extracted/ui/background/background.dta` | 1-30 | `background_panel` + variants (menu bg) |
| `orig-assets/extracted/ui/main/main.dta` | 165-238 | `main_screen`, `attract_screen` defs |
| `orig-assets/extracted/config/preload_subdirs.dta` | 62-71 | Preloaded subdirs (chars_base, director) |
| `src/lazer/meta_ham/HamUI.cpp` | 225, 551 | `mBackgroundPanel` draw + init |
| `native/src/platform/Rnd_Wgpu.cpp` | 819-866 | `NativeVenueInit()` (to remove) |
| `src/App.cpp` | 1037-1150 | Native venue hacks (to remove) |
| `src/system/obj/Dir.cpp` | 684-701 | `gNativeVenueDir` hook (to remove) |
| `src/system/world/Dir.cpp` | 32 | `gNativeVenueDir` declaration (to remove) |

## Changes Made Today (2026-03-20)

### Sync Load Hang Fix

Removed synchronous outfit loading from `NativeVenueInit` that was blocking the
main loop. Previously, `StartLoad(false)` entered a tight
`while (!mFilesPending.empty()) { TheLoadMgr.Poll(); }` loop that never returned
to the main loop — no frame counting, no UI polling, no exit checking.

**Removed**:
- Manual `SetOutfit` / `SetOutfitDir` / `StartLoad(false)` calls
- `NativeVenueSetupClips()` function (deferred clip playback)
- `mOutfitClipsSetup` member variable
- Emscripten `async = true` override in `FileMerger::StartLoadInternal`

Character outfits now load through the DTA wardrobe flow
(`HamWardrobe::LoadCharacters` → `StartLoad(true)` → async FileMerger).

**Verified**: Process exits cleanly at `MILO_MAX_FRAMES=100` — no hang.
