# Convergence Synthesis: DTA Venue Flow

**Date**: 2026-03-21
**Status**: Implementation-ready roadmap
**Synthesized from**: 5 audit reports + original session analysis
**Scope**: Song select fix through hack removal and gameplay parity

---

## 1. Executive Summary

The DC3 native port successfully boots through the DTA screen chain from attract_screen to song_select_screen using a combination of auto-advance timers and MILO_INPUT_SCRIPT scripted navigation. The async loading pipeline (TheLoadMgr, FileMergerOrganizer, DirLoader) is fully operational. However, song select is the current hard blocker: scripted inputs at frame offsets beyond ~1100 do not register, preventing navigation past song_select_screen to gameplay. Beyond that, the path to gameplay requires wiring enter_gameplay() and MetaPerformer setup, which the DC3_SCREEN auto-nav path bypasses. The native port carries 71 SCAFFOLD blocks across 295 files that bypass Xbox DTA flows with C++ workarounds; of these, the venue discovery chain (gNativeVenueDir, NativeVenueInit, App.cpp venue block) and gameplay timing hacks (GamePanel intro force-advance) are the primary removal targets. Three safety infrastructure systems (cascade protection, FlowQueueable ObjPtrList, FileMerger cascade guards) must remain permanently. The rendering pipeline is confirmed capable of drawing background_panel's turbo_shell scene through the standard UIPanel path, meaning menu venues can be removed entirely once DTA flow convergence is achieved.

---

## 2. Current Baseline

### What Works Today

| Capability | Status | Notes |
|---|---|---|
| Boot to attract_screen | Working | Auto-advance or MILO_INPUT_SCRIPT |
| attract -> title -> wait_saveload -> main_screen | Working | Auto-advance table (UI.cpp:598-613), ~7s total |
| main_screen rendering | Working | UIPanel draw pipeline, shell background |
| main_screen -> choose_mode_screen | Working | MILO_INPUT_SCRIPT `confirm` at frame 500 |
| choose_mode_screen -> song_select_screen | Working | MILO_INPUT_SCRIPT `confirm` at frame 800 |
| Scripted input (MILO_INPUT_SCRIPT) | Working | Joypad_Native.cpp reads script, injects button events |
| Async loading pipeline (TheLoadMgr) | Working | SystemPoll() -> TheLoadMgr.Poll() every frame |
| FileMergerOrganizer async queue | Working | Priority sorting, one-at-a-time dispatch |
| DirLoader incremental .milo loading | Working | Sync I/O on native but yields via CheckSplit() |
| FileMerger::FinishLoading merge | Working | Native hash-table fixup for nested objects |
| Audio subsystem (Vorbis/mogg) | Working | StandardStream with wall-clock fallback |
| WebGPU mesh/material/camera/lighting | Working | Full pipeline: RndMesh, RndMat, RndTex, RndEnviron |
| PostProc (bloom, DOF, color grading) | Working | PostProcPass reads RndPostProc::Current() |
| DC3_SCREEN=game_screen auto-nav | Partially working | Navigates main->choose_mode->song_select->multiuser->loading->game_screen |
| Game::LoadSong via auto-nav | Partially working | Song loads, but MetaPerformer not wired |
| Venue loading via world.fm | Partially working | world_panel loads world.milo, change_files fires |

### What Does Not Work

| Problem | Impact | Root Cause |
|---|---|---|
| **Song select input dies after ~frame 1100** | BLOCKER: cannot navigate past song_select | Unknown -- inputs at frames 1100+ do not register; scroll/confirm are ignored |
| No interactive song selection | Cannot browse songs via keyboard/gamepad | Input routing issue on song_select_screen |
| enter_gameplay() never called | Missing gameplay data init (finale, golden_boomy, music_stop) | Auto-nav calls GotoScreen directly, bypasses DTA function |
| MetaPerformer not set up | May affect song sequence, end-game results, loading music | Auto-nav sets TheGameData->SetSong() but not MetaPerformer::SetSong() |
| GameMode enter handler not fired | Some hamprovider properties may be missing | Auto-nav calls SetMode but not the DTA mode enter handler |
| Game intro sequence skipped | No countdown, no intro camera shot | GamePanel force-advances past kGameInIntro after 30 frames |
| Menu background renders gameplay venue | Xbox uses turbo_shell (background_panel), not a WorldDir | gNativeVenueDir hack draws venue on menus -- not Xbox behavior |
| Kinect meshes visible as white rectangles | Visual artifacts in venues | App.cpp one-shot hide block is a hack workaround |

---

## 3. Convergence Roadmap

### Phase 0: Song Select Fix

**Objective**: Fix the input routing bug that prevents scripted and interactive inputs from registering on song_select_screen after ~frame 1100.

**Acceptance criteria**:
- `MILO_INPUT_SCRIPT` with `down` commands at frames 1100-1500 produces visible scroll in song list
- `MILO_INPUT_SCRIPT` with `confirm` at frame 1500 navigates to multiuser_screen
- The `song-scroll-test.txt` input flow completes all 8 scroll operations

**Investigation targets**:

1. **mSink routing** (`src/system/ui/UI.cpp:693-697`): The SCAFFOLD block force-sets `mSink = trans` on every transition. If song_select_screen replaces mSink with a child panel (e.g., song_select_panel) via DTA `{ui set_sink}`, our force-set may overwrite it. Check whether song_select_screen's DTA enter handler calls set_sink.

2. **UIPanel focus state** (`src/system/ui/UIPanel.cpp`): song_select_panel is a HamNavList-based panel. If it never receives focus (FocusPanel), button events won't reach it. Check whether the panel's `CanHaveFocus()` returns true and whether `UIScreen::FocusPanel()` selects it.

3. **InTransition blocking** (`src/system/ui/UI.cpp:910-911`): The handler preamble blocks all message dispatch during transitions. If song_select_screen's enter animation takes >1100 frames of wall time, inputs are silently dropped. Check `mTransitionState` at frame 1100.

4. **Automator script timing** (`native/src/platform/Joypad_Native.cpp`): The `wait_screen` directive in input scripts waits for a specific screen name. If the screen name check fails (e.g., the screen is `song_select_screen` but poll sees `choose_mode_screen` during a transition), the frame counter may not start. Verify the wait_screen implementation handles transition timing correctly.

5. **HamNavList initialization** (`src/system/hamobj/HamNavList.cpp`): The song list is populated by a provider (HamSongSelectProvider). If the provider has zero entries or the list fails to initialize its elements, scroll inputs have nothing to scroll.

**Files to modify**: Diagnosis first. Likely candidates:
- `src/system/ui/UI.cpp` (mSink routing, transition blocking)
- `native/src/platform/Joypad_Native.cpp` (input script timing)
- `src/system/hamobj/HamNavList.cpp` (list initialization)
- `src/system/ui/UIPanel.cpp` (focus state)

**Risk level**: LOW -- this is a bug fix, not an architectural change. The fix is likely a small timing or routing correction.

**Dependencies**: None -- this is the first thing to fix.

---

### Phase 1: DTA Gameplay Flow

**Objective**: Make `enter_gameplay()` work on native so the DTA flow chain from song select through loading screens to game_screen fires correctly.

**Acceptance criteria**:
- MILO_INPUT_SCRIPT navigates: main_screen -> choose_mode -> song_select -> confirm song -> multiuser -> loading_screen -> preloading_screen -> real_loading_screen -> game_screen
- `enter_gameplay` DTA function fires (visible via `MILO_LOG` instrumentation)
- TheGameData has valid song/venue/mode when game_screen loads
- MetaPerformer::Current() returns a valid object with the selected song

**Files to modify**:

1. **Call enter_gameplay before loading_screen** (`src/App.cpp:1260-1267`):
   At the `multiuser_screen` auto-nav step, before calling `GotoScreen("loading_screen")`, execute the DTA `enter_gameplay` function or replicate its effects:
   ```
   // Replicate enter_gameplay (global.dta:212):
   // 1. initialize_gameplay_data (set finale/golden_boomy)
   // 2. gesture_mgr set_identification_enabled FALSE
   // 3. ui force_letterbox_off_immediate
   // 4. meta music_stop
   ```

2. **Wire MetaPerformer** (`src/App.cpp:1177-1236`):
   After `TheGameData->SetSong()`, call `MetaPerformer::Current()->SetSong(songSym)` to set up the song pipeline. This ensures `Game::LoadSong()` -> `MetaPerformer::Handle(Message("on_load_song"))` dispatches correctly.

3. **Fire GameMode enter handler** (`src/App.cpp:1209`):
   After `TheGameMode->SetMode(Symbol("perform"), Symbol("none"))`, execute the mode's common enter handler to populate remaining hamprovider properties (requires_2_players, merge_moves, use_movegraph, etc.).

4. **MultiUserGesturePanel enter_gameplay** (`src/lazer/meta_ham/MultiUserGesturePanel.cpp:70`):
   This SCAFFOLD block already calls `enter_gameplay` directly for the interactive flow path. Verify it fires when navigating through multiuser_screen. If the auto-nav path bypasses it, the auto-nav code in App.cpp must compensate.

**Risk level**: MEDIUM -- enter_gameplay() calls DTA functions that may reference uninitialized objects (TheMetaPerformer, TheGestureMgr). Each call needs null-guarding.

**Dependencies**: Phase 0 (if using interactive navigation), or DC3_SCREEN auto-nav path (which bypasses song select entirely).

---

### Phase 2: Venue + Character Loading

**Objective**: Verify that world_panel loads world.milo, HamDirector.mMerger gets wired, venue loads through FileMerger, and characters load through HamWardrobe.

**Acceptance criteria**:
- `world_panel` loads `../world/world.milo` when game_screen enters
- `HamDirector.mMerger` is non-null after world.fm's `change_files` fires
- `HamDirector::OnLoadSong()` selects song, venue, viz mergers
- `HamDirector::OnFileLoaded("song")` triggers `TheHamWardrobe->LoadCharacters()`
- `HamDirector::OnFileLoaded("venue")` sets `mVenue` to the loaded WorldDir
- `GamePanel::PollForLoading()` reaches state 4 (all loaded)

**Files to examine/modify**:

1. **world_panel loading** (`orig-assets/extracted/ui/game.dta:9-14`):
   Verify world_panel file path resolves on native. DirLoader must find `../world/world.milo` in the ark or filesystem.

2. **change_files DTA handler** (`orig-assets/extracted/char/char_objects.dta`):
   Verify `{$hamdirector set merger $this}` fires correctly. The `SYNC_PROP(merger, mMerger)` in `src/system/hamobj/HamDirector.cpp:217` receives the set.

3. **OnLoadSong crew/outfit** (`src/system/hamobj/HamDirector.cpp:1008-1045`):
   Hack 4 (crew/outfit fallback) fills in missing player data. This hack is needed until Phase 4 when the DTA player selection flow is fully wired.

4. **Venue path resolution** (`src/system/hamobj/HamDirector.cpp:1179-1234`):
   `GetVenuePath()` constructs `world/<name>/<name>.milo`. Verify the venue milo exists in the ark.

5. **FileMerger async chain** (`src/system/char/FileMerger.cpp:467-507`):
   `StartLoadInternal(async=true)` -> `TheFileMergerOrganizer->AddFileMerger()`. Verified working per audit report 05.

6. **GamePanel::PollForLoading gate** (`src/lazer/game/GamePanel.cpp:929-962`):
   States: (0) UIPanel loaded, (1) world_panel transition, (2) world loaded, (3) chars loaded, (4) game ready. Add diagnostic logging if not already present (8e debug blocks exist).

**Risk level**: LOW -- the async pipeline is already functional. This phase is primarily verification and diagnostic instrumentation.

**Dependencies**: Phase 1 (game_screen must be reachable).

---

### Phase 3: Gameplay Boot

**Objective**: game_screen enters, HamDirector::Enter() fires, VenueEnter() runs, characters appear, song plays, GamePanel transitions kGameInIntro -> kGamePlaying.

**Acceptance criteria**:
- `HamDirector::Enter()` fires (visible via MILO_LOG)
- `VenueEnter(mVenue)` completes without crash
- At least one character is visible in the venue
- Song audio begins playing (MIDI events fire)
- `GamePanel::mState` reaches `kGamePlaying`
- DC3_SCREEN=game_screen produces a rendered gameplay frame

**Files to modify**:

1. **HamDirector::Enter** (`src/system/hamobj/HamDirector.cpp:321-379`):
   Called when world_panel enters. Sets up PostProc, calls VenueEnter, Initialize, SyncScene, PlayIntroShot. Verify no null pointer crashes.

2. **VenueEnter** (`src/system/hamobj/HamDirector.cpp:591-639`):
   Finds player0/1, backup0/1 HamCharacter objects in the venue. Resets transforms. Sets mCharsShowing.

3. **GamePanel intro/start** (`src/lazer/game/GamePanel.cpp:407-589`):
   Hack 8a (intro force-advance after 30 frames) and 8c (StartGame bypass) ensure gameplay starts even without audio timing. These hacks remain active for now.

4. **Game::Poll** (`src/lazer/game/Game.cpp`):
   Null-guards for mGameInput, mMoveDir, TheMoveMgr are already in place (BUGFIX blocks). Verify autoplay is active.

5. **Kinect mesh hiding** (`src/App.cpp:1047-1098`):
   The one-shot venue setup block hides Kinect meshes (TVScreen, projection, Reflect, refract, render targets). This hack stays until Phase 4.

**Risk level**: MEDIUM -- venue Enter() traverses a large object graph. Null characters, missing song anims, or unloaded move data can crash. Autoplay must be active to compensate for missing Kinect input.

**Dependencies**: Phase 2 (venue and characters must be loaded).

---

### Phase 4: Hack Removal

**Objective**: Remove scaffolding hacks in dependency-safe order once DTA flow convergence provides their replacements.

**Acceptance criteria**:
- Each removed hack has a regression test (MILO_INPUT_SCRIPT flow that exercises the replaced path)
- No crashes on boot-to-gameplay flow after removal
- Menu screens render background_panel (turbo_shell) instead of a gameplay venue
- Gameplay venues render through world_panel -> HamDirector pipeline only

**Sub-phases** (see Section 4 for detailed schedule):

**4a. Independent hacks** (no prerequisites):
- Remove Hack 8e (PollForLoading diagnostics) -- pure debug logging
- Remove Hack 4 (crew/outfit fallback) -- only after verifying DTA player selection works
- Remove Hack 5 (move remixer init) -- only after verifying DTA modular.fm reset fires

**4b. Gameplay timing hacks** (require Phase 3 verification):
- Remove Hack 8a (intro force-advance) -- only if audio timing advances correctly
- Remove Hack 8c (StartGame bypass) -- only if HasIntro() returns correct value
- Remove Hack 8d (game_stage forcing) -- only if SongSequence::Play sets game_stage
- Remove Hack 8b (frame time bounds mask) -- verify mJitterSampleCount stays in [0,31]

**4c. Venue discovery chain** (require Phase 3 + background_panel working):
- Remove Hack 2 (NativeVenueInit) -- HamDirector::Enter() fires via DTA flow
- Remove Hack 3 (App.cpp venue block) -- world_panel renders venue via TheUI->Draw()
- Remove Hack 1 (gNativeVenueDir) -- LAST in chain, only after all readers gone

**4d. Scope/merge adaptations** (require MergeDirs investigation):
- Evaluate Hack 7c (post-merge registration) -- may need to stay if MergeDirs behavior differs
- Evaluate Hack 7d (DirLoader parent propagation) -- may need to stay

**Risk level**: MEDIUM per hack, but cumulative risk is HIGH if multiple hacks are removed simultaneously. Remove one at a time with full regression testing between each.

**Dependencies**: Phases 1-3 fully working.

---

### Phase 5: Polish

**Objective**: UI parity with Xbox menu flow, proper PostProc timing, animation flows, and background_panel rendering.

**Acceptance criteria**:
- background_panel's turbo_shell renders on all menu screens (main_screen, song_select, etc.)
- PostProc from background.milo applies to background but not UI overlay text
- Flow animations (bump, diagonal, pulse) activate on panel enter
- Interactive navigation (keyboard/gamepad) works on all screens without DC3_SCREEN auto-nav

**Files to modify**:

1. **PostProc flush timing** (`src/system/ui/PanelDir.cpp:391`):
   Add `FlushPostProcessingForOverlay()` call when `mCanEndWorld` is true, so PostProc applies to 3D background but not 2D UI elements drawn afterward.

2. **Flow activation filter** (`src/system/ui/PanelDir.cpp:32-90`):
   Verify `ShouldActivateNativeFlow("background", ...)` activates all turbo_shell animations. May need to expand the curated flow name list.

3. **Auto-advance table removal** (`src/system/ui/UI.cpp:598-613`):
   Once interactive navigation works on all screens, the auto-advance table can be reduced to only attract_screen (which has no video decoder) and autosave_warning (which has no real save system).

4. **Clear color** (`native/src/platform/Rnd_Wgpu.cpp`):
   With turbo_shell providing full-screen coverage, the clear color should be black. Verify.

**Risk level**: LOW -- these are visual refinements, not structural changes.

**Dependencies**: Phase 4c (venue hacks removed, background_panel is the menu background).

---

## 4. Hack Removal Schedule

Ordered by dependency safety. Each entry specifies the gate (when it can be removed), what replaces it, and the regression test.

### Tier 0: Remove Anytime (no dependencies)

| Hack | Gate | Replacement | Regression Test |
|---|---|---|---|
| **8e** — PollForLoading diagnostics (`GamePanel.cpp:930,956`) | Now | None needed (pure debug fprintf) | `DC3_SCREEN=game_screen MILO_MAX_FRAMES=3000 native/build/dc3-native` -- game_screen loads |
| **7a** — sDisableAll static (`FileMerger.cpp:22`) | Now | Remove static + SYNC_PROP | Verify no DTA config sets `disable_all`. Full boot flow test. |

### Tier 1: After Phase 1 (DTA gameplay flow working)

| Hack | Gate | Replacement | Regression Test |
|---|---|---|---|
| **4** — crew/outfit fallback (`HamDirector.cpp:1008-1045`) | DTA multiuser flow wires crew/outfit before OnLoadSong | Normal Xbox DTA player selection flow | `MILO_INPUT_SCRIPT=ymca.txt` -- characters load with correct outfits |
| **5** — move remixer init (`HamDirector.cpp:546-562`) | DTA modular.fm reset handler fires | `SetGameplayMode(perform, true)` -> remixer init chain | `DC3_SCREEN=game_screen` -- characters animate (not T-posed) |

### Tier 2: After Phase 3 (gameplay boots successfully)

| Hack | Gate | Replacement | Regression Test |
|---|---|---|---|
| **8a** — intro force-advance (`GamePanel.cpp:407-416`) | Audio/timing advances correctly | SongSequence timing drives intro->playing transition | `DC3_SCREEN=game_screen MILO_MAX_FRAMES=5000` -- kGamePlaying reached |
| **8c** — StartGame bypass (`GamePanel.cpp:571-581`) | HasIntro() returns correct value | Normal intro gate logic | Same as 8a |
| **8d** — game_stage forcing (`GamePanel.cpp:584-589`) | SongSequence::Play fires | DTA `game_stage` property set by SongSequence | Verify `{hamprovider get game_stage}` == `"playing"` |
| **8b** — frame time bounds mask (`GamePanel.cpp:548-552`) | Verify mJitterSampleCount stays in [0,31] always | Direct array access | `DC3_SCREEN=game_screen MILO_MAX_FRAMES=10000` -- no crash |

### Tier 3: After Phase 4 background_panel verified (venue hacks removable)

| Hack | Gate | Replacement | Regression Test |
|---|---|---|---|
| **2** — NativeVenueInit (`Rnd_Wgpu.cpp:819-866`) | HamDirector::Enter() fires via DTA flow | Panel DTA flow: game_screen enter -> world_panel -> HamDirector::Enter() -> VenueEnter() | `DC3_SCREEN=game_screen` -- venue renders, lights work |
| **3** — App.cpp venue block (`App.cpp:1037-1131`) | world_panel renders venue via TheUI->Draw(); background_panel renders menus | UIPanel draw hierarchy for venues; DTA mesh visibility for Kinect meshes | `MILO_INPUT_SCRIPT=boot-to-main.txt` -- menu renders (no white rects, no venue on menu) |
| **1** — gNativeVenueDir (`Dir.cpp:684, world/Dir.cpp:32`) | Remove LAST after Hacks 2 and 3 are gone | HamDirector::mVenue via OnFileLoaded("venue") callback | Full boot-to-gameplay flow: no crashes, venues only during gameplay |

### Tier 4: Evaluate (may be permanent)

| Hack | Gate | Assessment | Decision |
|---|---|---|---|
| **7c** — post-merge subdir object registration (`FileMerger.cpp:240-252`) | MergeDirs scope parity with Xbox | Xbox flattens all objects into single scope; native keeps subdirs separate. Without this, `Find<T>(name, false)` fails for merged subdir objects. | **Likely permanent** unless MergeDirs is rewritten to fully flatten. |
| **7d** — DirLoader parent propagation (`FileMerger.cpp:448-455`) | ObjPtr resolution parity with Xbox | Xbox MergeDirs puts everything in one scope for ObjPtr resolution. Native needs parent chain fallback. | **Likely permanent** for same reason as 7c. |

### Never Remove (permanent infrastructure)

| Hack | Reason |
|---|---|
| **9** — Cascade protection infra (`Dir.cpp:49-92,714-777`, `Object.cpp:112`, etc.) | Three-phase DeleteObjects, InDeleteObjects(), sInMergeDirs, deferred free. Without it, any ObjectDir teardown during gameplay crashes. Would require proving Xbox's exact destruction order is replicated. |
| **7b** — FileMerger cascade guards (`FileMerger.cpp:54-83`) | Skip on_pre_clear, delete, RemoveSubDir during cascade. Depends on Hack 9 infrastructure. |
| **6** — FlowQueueable ObjPtrList (`FlowQueueable.h:33-37`, `FlowQueueable.cpp`) | Replaces `std::list<raw ptr>` with ring-tracked ObjPtrList. Raw pointer list is fundamentally unsafe when objects can be destroyed by ObjectDir cascade. |

---

## 5. Permanent Adaptations

These HX_NATIVE blocks are PLATFORM, BUGFIX, or STUB classifications -- they are necessary for the native port and should NOT be removed during convergence.

### Platform: Architecture Differences (177 blocks)

**Memory & Types (LP64)**:
- `types.h:5,46,72` -- POSIX equivalents, u32=unsigned int, intptr_t
- `Data.h:64,72,80,88,115` -- Zero all 8 bytes of DataNode union on LP64
- `MemMgr.cpp/h` -- Native malloc/free instead of custom Xbox heaps
- `PoolAlloc.cpp/h` -- malloc/free bypass (pool uses int-sized slots)
- `AllocInfo.cpp` -- LP64 offset adjustments
- `CharClip.cpp:57,218`, `CharClip.h:155,164` -- intptr_t, size_t operators
- `Symbol.h:3,36` -- operator int via intptr_t
- `HamListRibbon.cpp:430`, `.h:20` -- LP64 pointer member types

**Endianness**:
- `BinStream.cpp:77,185` -- Endian-conditional byte swap (LE reads BE files)
- `ChunkStream.cpp` (10 blocks) -- Synchronous decompression, endian handling
- `Mesh.cpp:355` -- CachedRead with endian swap
- `CharBonesSamples.cpp:126,226` -- Big-endian to little-endian bone data

**Audio**:
- `StandardStream.cpp` (6 blocks) -- Vorbis header pump, ring buffers, audio timing
- `StreamReceiver.cpp` (4 blocks) -- Native ring buffer, play cursor
- `Synth.cpp` (5 blocks) -- Native stream creation, mogg resolution, VorbisReader
- `VorbisReader.cpp:134,399` -- AES-CTR decryption, mogg demux

**Graphics**:
- `Cam.cpp:193,301,360` -- Native camera Select, WorldToScreen, ScreenToWorld
- `Mesh.cpp:133,1678` -- CleanupGpuMesh, compressed vertex preservation
- `Tex.cpp:104,122` -- Native RndTex Load/PreLoad
- `Text.cpp` (8 blocks) -- wchar_t 4-byte, blacklight packet pool
- `Part.cpp:583` -- DrawParticlesBillboard

**File I/O**:
- `ArkFile.cpp:12,95` -- Native ark file read (direct, synchronous)
- `File.cpp:23,51,365` -- POSIX filesystem (stat, realpath)
- `Loader.cpp:293` -- Synchronous loader polling

**STLport -> libstdc++ Compatibility**:
- Numerous `PLATFORM` blocks skipping STLport template instantiations
- `msvc_compat.h:11` -- C++17 compat shims

### Bugfix: Crash/Corruption Prevention (131 blocks)

**Cascade Destruction Safety (core infrastructure)**:
- `Dir.cpp:720` -- Three-phase DeleteObjects
- `Dir.cpp:56` -- NullifyAllRefs
- `Object.cpp:340,371` -- Snapshot-based ReplaceRefs, NullifyAllRefs
- `Object.cpp:107` -- Cascade skip in ~Object
- `Object.cpp:56,94,102,106` -- Ring sentinel tracking
- `ObjPtr_p.h:35,55,71` -- Cascade skip in ~ObjPtr, safe release, skip_release
- `Task.cpp:13,22,28,33,410,432,507` -- LiveTasks stale-pointer detection, cascade guard

**Flow System Safety**:
- `Flow.cpp:48`, `FlowNode.cpp:25,36` -- Clear running nodes during cascade
- `FlowQueueable.cpp/h` -- ObjPtrList listener tracking
- `UITransitionHandler.cpp:12` -- Skip during cascade

**Object Resolution**:
- `DataNode/Array/Func` (13 blocks) -- Null property returns, type-safe fallbacks
- `CharClip.cpp:26,252` -- ObjRef ring corruption prevention
- `PropKeys.h:82` -- Handle mTarget replacement (Itanium ABI vtable dispatch)

**Null Guards (50+ blocks across subsystems)**:
- Game.cpp (7 blocks), HamDirector (1), HamWardrobe, HelpBarPanel (5), HamNavList (4), UIList (2), UIListSubList (2), Character (2), SkeletonUpdate (7), CameraManager (2), etc.

**Math Correctness**:
- `Rand.cpp:65` -- Modulo instead of UB overflow
- `mtx.cpp:78` -- Correct matrix multiply formula
- `CameraManager.cpp:109,233` -- NaN/inf guards

### Stub: Unimplemented Xbox Features (65 blocks)

**Kinect/Gesture (15 blocks)**:
- `GestureMgr.cpp:43,84,163,191,201,406` -- Controller mode, no Kinect
- `SkeletonUpdate.cpp:133` -- Skip thread creation
- `StreamRecorder.cpp:281`, `StreamRenderer.cpp:282` -- Empty recording stubs
- `LiveCameraInput.cpp:1112,1139` -- Skip NUI calls

**Xbox Live / Network (6 blocks)**:
- `RockCentral.cpp:154,241` -- Skip resolution job
- `MetaPerformer.cpp:898,904` -- Skip player drop jobs
- `Achievements.cpp:36` -- Empty PlatformInit

**Video / Bink (4 blocks)**:
- `BinkMovieImpl.cpp:117` -- Stub all Bink methods
- `BinkMovieSys.cpp:115` -- Stub Bink SDK functions
- `BinkIntegration.cpp:87` -- Stub Bink SDK

**Holmes Remote Debug (2 blocks)**:
- `HolmesClient.cpp:419,881` -- Debug protocol stubs

**Speech/Voice (3 blocks)**:
- `VoiceInputPanel.cpp:41,227` -- Skip voice context
- `SpeechMgr.cpp:12` -- mbstowcs_s shim

---

## 6. Risk Register

| # | Risk | Probability | Impact | Mitigation |
|---|---|---|---|---|
| 1 | **Song select fix is more complex than expected** -- may involve deep UIList/HamNavList provider lifecycle issues, not just input routing | Medium | HIGH -- blocks all subsequent phases | Bisect with MILO_INPUT_SCRIPT at different frame counts. Add diagnostic logging to ButtonDownMsg handler chain. Compare against DC3_SCREEN auto-nav path (which bypasses song select). |
| 2 | **enter_gameplay() DTA calls crash on null objects** -- DTA handler references TheGestureMgr, TheMetaPerformer, TheProfileMgr which may be stubs/null | High | MEDIUM -- requires null-guarding each call | Wrap in try/catch or pre-check each object. Can replicate effects in C++ instead of calling DTA function. |
| 3 | **world.milo fails to load on native** -- file path resolution differences, missing ark entries | Low | HIGH -- blocks entire venue pipeline | world_panel loading is standard DirLoader path, already proven to work for other panels. Verify path `../world/world.milo` resolves. |
| 4 | **FileMerger cascade crash during venue reload** -- switching songs or re-entering game_screen triggers merger Clear() during active loads | Medium | HIGH -- crash | FileMerger cascade guards (Hack 7b) are permanent. Ensure InDeleteObjects() returns true during dir teardown. |
| 5 | **Character loading hangs on web (Emscripten)** -- deep cascade destruction during PostMerge overflows 4MB WASM stack | Medium | MEDIUM -- web-only | Desktop native is primary target. Web port can use `asyncify` or increase stack size. Not a convergence blocker. |
| 6 | **Removing venue hacks breaks menu rendering** -- background_panel may not render correctly without the gNativeVenueDir fallback | Low | HIGH -- blank menu screens | background_panel rendering verified in audit 04. Test with `DC3_NO_VENUE=1` env var before removing hacks. |
| 7 | **GamePanel PollForLoading never reaches state 4** -- some IsLoaded check fails (audio timeout, missing MoveDir, etc.) | Medium | HIGH -- stuck at loading screen | Native already has timeouts and null guards. Add per-state diagnostic logging. |
| 8 | **PostProc timing wrong after hack removal** -- PostProc affects UI overlay text (bloom on menu text) | Medium | LOW -- visual only | Add FlushPostProcessingForOverlay() call in PanelDir::DrawShowing when mCanEndWorld is true. |
| 9 | **Concurrent agents modifying same files** -- CLAUDE.md warns against git stash, multiple agents may work in the repo | Medium | MEDIUM -- merge conflicts | Use git worktrees via `scripts/setup_worktree.sh` for each phase. Merge after verification. |
| 10 | **Auto-nav path diverges from interactive path** -- DC3_SCREEN auto-nav works but interactive MILO_INPUT_SCRIPT flow fails due to different screen transition timing | Medium | MEDIUM -- testing gap | Test BOTH paths for every phase. Auto-nav is fast iteration; input script is the acceptance test. |

---

## 7. Test Matrix

### Phase 0: Song Select Fix

```bash
# Test 1: Scripted scroll on song_select_screen
MILO_HEADLESS=1 MILO_MAX_FRAMES=3000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/song-scroll-test.txt \
  timeout 120 native/build/dc3-native 2>&1 | grep -E "DC3|scroll|song_select"
# PASS: All 8 "down" commands register, log shows scroll events

# Test 2: Scripted confirm on song_select_screen
MILO_HEADLESS=1 MILO_MAX_FRAMES=3000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  timeout 120 native/build/dc3-native 2>&1 | grep -E "DC3|multiuser|select"
# PASS: "confirm" at frame 1500 navigates to multiuser_screen

# Test 3: Interactive windowed navigation
MILO_RENDER=1 native/build/dc3-native
# PASS: Arrow keys scroll song list, Enter selects a song
```

### Phase 1: DTA Gameplay Flow

```bash
# Test 1: Full auto-nav to game_screen
DC3_SCREEN=game_screen MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "DC3|enter_gameplay|game_screen"
# PASS: Log shows enter_gameplay fired, game_screen reached

# Test 2: MetaPerformer wired
DC3_SCREEN=game_screen DC3_SONG=boyfriend MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "MetaPerformer|set_song"
# PASS: MetaPerformer::SetSong logged

# Test 3: Scripted navigation (non-auto-nav)
MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "loading_screen|game_screen"
# PASS: Navigates through loading screens to game_screen
```

### Phase 2: Venue + Character Loading

```bash
# Test 1: world_panel and merger wiring
DC3_SCREEN=game_screen MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "world_panel|merger|change_files|OnLoadSong"
# PASS: world.milo loads, mMerger wired, OnLoadSong fires

# Test 2: Venue and character loading
DC3_SCREEN=game_screen DC3_VENUE=glitterati MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "OnFileLoaded|LoadCharacters|AllCharsLoaded|PollForLoading"
# PASS: venue loaded, characters loaded, PollForLoading reaches state 4

# Test 3: Different venue
DC3_SCREEN=game_screen DC3_SONG=ymca DC3_VENUE=dclive MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "OnFileLoaded|dclive"
# PASS: dclive venue loaded
```

### Phase 3: Gameplay Boot

```bash
# Test 1: Gameplay state machine
DC3_SCREEN=game_screen MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "kGamePlaying|StartGame|Enter|VenueEnter"
# PASS: HamDirector::Enter fires, kGamePlaying reached

# Test 2: Rendered gameplay frame (screenshot)
DC3_SCREEN=game_screen MILO_RENDER=1 MILO_MAX_FRAMES=3000 \
  MILO_SCREENSHOT_DIR=/tmp/claude/convergence MILO_SCREENSHOT_FRAMES=2500 \
  timeout 180 native/build/dc3-native
# PASS: Screenshot at frame 2500 shows venue with characters

# Test 3: Audio playback
DC3_SCREEN=game_screen DC3_SONG=boyfriend MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "audio|stream|MasterPoll"
# PASS: Audio stream created and polling

# Test 4: Autoplay active
DC3_SCREEN=game_screen DC3_AUTOPLAY=maximum MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep -E "autoplay|move_perfect"
# PASS: Autoplay generates move inputs
```

### Phase 4: Hack Removal (per-hack regression)

```bash
# After each hack removal, run the FULL test suite:

# Regression: Boot flow still works
MILO_HEADLESS=1 MILO_MAX_FRAMES=1500 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/boot-to-main.txt \
  timeout 60 native/build/dc3-native 2>&1 | grep "main_screen"
# PASS: Reaches main_screen

# Regression: Song select still works
MILO_HEADLESS=1 MILO_MAX_FRAMES=3000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/song-scroll-test.txt \
  timeout 120 native/build/dc3-native 2>&1 | grep "scroll"
# PASS: Song list scrolls

# Regression: Gameplay still works
DC3_SCREEN=game_screen MILO_HEADLESS=1 MILO_MAX_FRAMES=5000 \
  timeout 180 native/build/dc3-native 2>&1 | grep "kGamePlaying"
# PASS: Gameplay state reached

# Regression: Menu rendering (after venue hack removal)
MILO_RENDER=1 MILO_MAX_FRAMES=1500 \
  MILO_SCREENSHOT_DIR=/tmp/claude/convergence MILO_SCREENSHOT_FRAMES=1000 \
  timeout 60 native/build/dc3-native
# PASS: Screenshot shows turbo_shell background (gradients, frames), no white rects
```

### Phase 5: Polish

```bash
# Test 1: Background panel animations
MILO_RENDER=1 MILO_MAX_FRAMES=2000 \
  MILO_SCREENSHOT_DIR=/tmp/claude/convergence MILO_SCREENSHOT_FRAMES=500,1000,1500 \
  timeout 60 native/build/dc3-native
# PASS: Screenshots show animated background (bump, pulse, diagonal anims active)

# Test 2: PostProc on background only (not UI text)
MILO_RENDER=1 MILO_MAX_FRAMES=2000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/boot-to-main.txt \
  MILO_SCREENSHOT_DIR=/tmp/claude/convergence MILO_SCREENSHOT_FRAMES=1500 \
  timeout 60 native/build/dc3-native
# PASS: Menu text is crisp (no bloom bleed), background has PostProc effects

# Test 3: Full interactive flow (no auto-nav, no DC3_SCREEN)
MILO_RENDER=1 native/build/dc3-native
# PASS: User can navigate boot -> main -> choose_mode -> song_select -> confirm -> multiuser -> loading -> game_screen entirely via keyboard
```

---

## Appendix A: Key Source Files Quick Reference

| File | Convergence Role |
|---|---|
| `src/App.cpp:1000-1278` | Native main loop, auto-nav, venue hacks (Hacks 1,3) |
| `src/system/ui/UI.cpp:556-628` | UIManager::Poll, auto-advance table, mSink routing |
| `src/system/ui/UIPanel.cpp` | Panel loading, focus, draw pipeline |
| `src/system/ui/PanelDir.cpp:384-428` | PanelDir::DrawShowing, Flow activation |
| `src/system/hamobj/HamDirector.cpp` | OnLoadSong, OnFileLoaded, Enter, VenueEnter (Hacks 4,5) |
| `src/lazer/game/GamePanel.cpp` | PollForLoading, StartGame, intro hacks (Hack 8) |
| `src/lazer/game/Game.cpp` | LoadSong, IsLoaded state machine |
| `src/system/char/FileMerger.cpp` | Async merge pipeline, cascade guards (Hack 7) |
| `src/system/char/FileMergerOrganizer.cpp` | Async queue management |
| `src/system/obj/Dir.cpp:684` | gNativeVenueDir hook (Hack 1) |
| `src/system/world/Dir.cpp:32` | gNativeVenueDir declaration |
| `native/src/platform/Rnd_Wgpu.cpp:819-866` | NativeVenueInit (Hack 2) |
| `src/system/flow/FlowQueueable.cpp/h` | ObjPtrList listener safety (Hack 6) |
| `src/system/obj/Dir.cpp:49-92,714-777` | Cascade protection infra (Hack 9) |
| `native/src/platform/Joypad_Native.cpp` | MILO_INPUT_SCRIPT, input injection |
| `orig-assets/extracted/ui/global.dta:212` | enter_gameplay() DTA function |
| `orig-assets/extracted/ui/game.dta` | game_screen, world_panel definitions |
| `orig-assets/extracted/char/char_objects.dta` | world.fm/game_mode.fm DTA handlers |
| `orig-assets/extracted/ui/background/background.dta` | background_panel (turbo_shell) |

## Appendix B: Audit Report Cross-Reference

| Report | Key Finding | Used In Phase |
|---|---|---|
| 01 (HX_NATIVE audit) | 71 SCAFFOLD, 177 PLATFORM, 131 BUGFIX, 65 STUB, 97 DEBUG blocks | Phase 4 (removal), Section 5 (permanent) |
| 02 (Boot-to-gameplay flow) | enter_gameplay() never called, MetaPerformer not wired, GameMode enter handler not fired | Phase 1 |
| 03 (Dependency graph) | Three independent components: venue chain (1->2,3), cascade safety (9->6,7b), gameplay (4,5,8) | Phase 4 (removal order) |
| 04 (Background panel rendering) | All features supported: mesh, material, camera, environ, PostProc, Flow. One caveat: PostProc flush timing. | Phase 5 |
| 05 (FileMerger async pipeline) | Core pipeline working end-to-end. Blockers: no game_screen navigation, NativeVenueInit bypass, missing player selection flow. | Phase 2 |
