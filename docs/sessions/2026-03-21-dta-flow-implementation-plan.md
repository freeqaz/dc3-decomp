# DTA Flow Implementation Plan

**Date**: 2026-03-21
**Status**: Planning
**Goal**: Remove native venue/flow hacks, let the DTA panel cascade drive boot-to-gameplay naturally
**Depends on**: [2026-03-20-dta-venue-flow-convergence.md](2026-03-20-dta-venue-flow-convergence.md)

## Current State

The native port has ~6 C++ workarounds that bypass the DTA panel flow. The
underlying infrastructure (Flow/FlowQueueable, UIScreen, PanelDir, FileMerger,
smart stubs) is fully implemented — the workarounds exist because the native port
never lets the DTA flow *run* end-to-end.

### Workarounds to Remove

| # | What | File | Lines | Replaces |
|---|------|------|-------|----------|
| 1 | `gNativeVenueDir` global | Dir.cpp:684-701, world/Dir.cpp:32 | ~20 | `HamDirector.mVenue` set by `on_file_loaded("venue")` |
| 2 | `NativeVenueInit()` | Rnd_Wgpu.cpp:819-866 | ~50 | `HamDirector::Enter()` → `VenueEnter()` via panel cascade |
| 3 | Venue poll/setup block | App.cpp:1037-1131 | ~95 | `HamDirector::ListPollChildren()` + one-shot Kinect mesh hide |
| 4 | Pre-game direct draw | App.cpp:1142-1150 | ~10 | `background_panel` (turbo_shell) renders menus |
| 5 | Crew/outfit reconstruction | HamDirector.cpp:1008-1045 | ~40 | DTA `select_player`/`select_crew` handlers populate data |
| 6 | Move remixer manual init | HamDirector.cpp:546-562 | ~15 | DTA `modular.fm` reset handler fires naturally |

### Auto-Nav Chain (App.cpp:1162-1279)

The 120-line if/else chain manually navigates:
```
main_screen → choose_mode → song_select → multiuser → loading → game_screen
```
This replaces what DTA enter handlers + user input do on Xbox.

## Stubbed Systems Inventory

### Smart Stubs (App.cpp:48-154) — Sufficient for DTA Flow

These intercept DTA handler queries and return sensible defaults. All are
**sufficient** for the screen chain to advance — no unstubbing needed for MVP.

| Stub | Key Responses | Screens That Query It |
|------|---------------|----------------------|
| **NativeSaveLoadStub** | `is_idle`→1, `is_initial_load_done`→1, `autosave`→0 | wait_main_after_saveload, loading screens |
| **NativeProfileMgrStub** | `has_seen_tutorial`→1, `is_content_unlocked`→1, `is_difficulty_unlocked`→1, volumes→8 | choose_mode, song_select, endgame |
| **NativePlatformMgrStub** | `is_guide_showing`→0, `is_pad_signed_into_live`→0 | main_screen, online features |
| **NativeSpeechMgrStub** | `begin_recognition`→0, `end_recognition`→0 | VoiceInputPanel, voice-command menus |

### Systems That Are Real (Not Stubbed)

| System | Status | Notes |
|--------|--------|-------|
| **ContentMgr** | Functional | `RefreshSynchronously()` at boot; `RefreshDone()`→true |
| **HamSongMgr** | Functional | Loads song DB from DTA, `HasSong()` / `Data()` work |
| **GameMode / GameData** | Functional | `SetMode()`, `SetSong()`, `SetVenue()`, `Player()` all work |
| **FileMerger pipeline** | Functional | Async loading, `on_file_loaded` callbacks, `FileMergerOrganizer` polling |
| **Flow / FlowQueueable** | Functional | All 20+ node types, queue state machine, listener callbacks |
| **UIScreen / UIPanel** | Functional | Screen transitions, auto-skip, panel loading gates |
| **PanelDir** | Functional | Flow activation, `ShouldActivateNativeFlow()` filtering, `SendTransition()` |
| **Joypad (XInput)** | Functional | Keyboard/gamepad UI navigation works |
| **GestureMgr** | Initialized | No live Kinect data; `mInControllerMode=true` forced |

### Systems Not Registered (Missing Entirely)

| System | Impact | Needed for MVP? |
|--------|--------|-----------------|
| **TheNetMgr** | Online leaderboards, web services | No |
| **TheMemcardMgr** | Memory card (Xbox-specific) | No |
| **PresenceMgr** | Xbox Live presence broadcasts | No |

### Kinect-Dependent Subsystems (Permanently Stubbed)

| System | What It Does | Native Behavior |
|--------|-------------|-----------------|
| **VoiceInputPanel** | Speech recognition menus | Returns `kDataUnhandled`; joypad fallback works |
| **MultiUserGesturePanel** | Kinect skeleton detection | Auto-skipped via DTA `next_screen` |
| **SpeechMgr** | Microphone input → text | All methods no-op |
| **Kinect mesh rendering** | TVScreen, projections, reflections | Hidden by venue setup block (relocate to VenueEnter) |

## Implementation Plan

### Phase 1: Per-Screen Auto-Select Handlers

Replace the monolithic auto-nav chain (App.cpp:1162-1279) with per-screen
native enter handlers. Each screen already fires `HandleType(msg("enter"))` on
entry — we add native-side responses.

**Approach**: Add a single `NativeAutoNav` handler class (registered as a global
message handler, or inline in UIScreen::Enter under `#ifdef HX_NATIVE`) that
responds to screen enter messages by reading env vars and navigating forward.

```
attract_screen:  already auto-skips via skip_selected handler
title_screen:    already auto-skips via 60-frame delay + next_screen
main_screen:     NEW — set GameData (song/venue/mode/difficulty from env), goto choose_mode
choose_mode:     NEW — goto song_select (mode already set)
song_select:     NEW — goto multiuser (song already set)
multiuser:       NEW — goto loading_screen (skip Kinect gesture detection)
loading_screen:  existing DTA flow chains → preloading → real_loading → game_screen
game_screen:     existing panel cascade (world_panel → HamDirector::Enter)
```

**~60 lines** of new code, replacing ~120 lines of if/else chain.

**Key detail**: Game data setup (song, venue, difficulty, autoplay) must happen
*before* navigating past main_screen, since `loading_screen` triggers
`Game::LoadSong()` which reads `TheGameData->GetSong()`.

### Phase 2: Let the Panel Cascade Work

Once auto-nav reaches `game_screen` through the DTA flow:

1. `game_screen` loads its panels: `game_panel`, `world_panel`, etc.
2. `world_panel` loads `world.milo` containing `world.fm` (FileMerger)
3. `world.fm`'s `change_files` handler wires `HamDirector.mMerger = world.fm`
4. `Game::LoadSong()` → `HamDirector::OnLoadSong()` → async FileMerger chain
5. `GamePanel::PollForLoading()` gates on:
   - `HamDirector::IsWorldLoaded()` (venue merge complete)
   - `HamWardrobe::AllCharsLoaded()` (character outfits loaded)
   - `Game::IsReady()`
6. Once all true: panels Enter → `HamDirector::Enter()` → `VenueEnter(mVenue)`

**Risk**: This is the path that should "just work" once game_screen is reached.
If it doesn't, the failure will be in one of the gates above. Add logging to
each gate in `GamePanel::PollForLoading()`.

### Phase 3: Remove Workarounds

Once the panel cascade is confirmed working:

1. **Delete `NativeVenueInit()`** and its call in `BeginDrawing()`
2. **Delete `gNativeVenueDir`** global and the `AddedSubDir` capture hook
3. **Move Kinect mesh hiding** into `HamDirector::VenueEnter()` under
   `#ifdef HX_NATIVE` (one-shot, ~50 lines relocated)
4. **Delete venue poll/draw block** in App.cpp main loop (gameplay polling goes
   through `HamDirector::ListPollChildren()` naturally)
5. **Delete crew/outfit reconstruction** fallback (DTA flow populates this)
6. **Delete move remixer manual init** (DTA `modular.fm` reset fires naturally)
7. **Simplify pre-game draw**: Menu screens use `background_panel` (turbo_shell),
   not a gameplay venue. Remove the direct `menuVenue->DrawShowing()` call.

### Phase 4: Background Panel for Menus

Xbox menus use `background_panel` (turbo_shell) — a flat 2D scene with gradients,
frames, and animations. The native port currently shows a gameplay venue on menus
(wrong).

**Options**:
- **A) Load background.milo**: Render the turbo_shell as Xbox does. Requires the
  background_panel to load and its PropAnims/Flows to activate.
- **B) Solid color / custom background**: Skip turbo_shell, render a simple
  background. Simpler but less authentic.
- **C) Keep venue on menus**: Intentional divergence — looks cool, just wrong.

Option A is ideal but not blocking. The venue renders fine as a placeholder.

## Risk Areas

### Screens That May Hang

Some DTA handlers query systems beyond what stubs cover. Known risks:

| Screen | Handler | Potential Issue |
|--------|---------|----------------|
| `choose_mode_screen` | `profile_mgr.is_content_unlocked` | Stub returns 1 (OK) |
| `song_select_screen` | `content_mgr.RefreshDone` | Real, returns true (OK) |
| `loading_screen` | `saveload_mgr.is_idle` | Stub returns 1 (OK) |
| `multiuser_screen` | Kinect gesture detection | Needs auto-skip to `next_screen` |
| Any screen | `platform_mgr.is_guide_showing` | Stub returns 0 (OK) |

**Mitigation**: UIScreen already has a 90-frame animation timeout safety net.
Add a screen-level timeout (e.g., 300 frames / 5 seconds) that auto-advances
via `next_screen` if no navigation occurs. This catches any screen that hangs
waiting for missing input.

### Flow Filtering

`ShouldActivateNativeFlow()` (PanelDir.cpp:96-157) curates which DTA flows
activate on native. The current token-based filtering (skip exit/hide/deactivate,
keep enter/show/select) works for menu screens. Gameplay screens may have flows
that the filter incorrectly skips or includes.

**Mitigation**: Set `MILO_NATIVE_FLOW_FILTER=all` during testing to activate
everything, then curate failures back into the skip list.

### Loading Screen → game_screen Transition

The loading screen chain (`loading_screen` → `preloading_screen` →
`real_loading_screen` → `game_screen`) is DTA-driven. Each screen's enter
handler checks readiness and navigates forward. If any gate fails:

- `GamePanel::PollForLoading()` returns false forever
- The loading screen stays indefinitely

**Mitigation**: Add diagnostic logging to each `PollForLoading()` gate. If a
gate is stuck, it means the corresponding FileMerger callback didn't fire.

### Cascade Destruction

Recent work (2026-03-20) fixed cascade destruction safety for `ObjPtrList` in
`FlowQueueable` and `FileMerger`. The fixes use `ObjectDir::InDeleteObjects()`
guards to detect cascade teardown and skip operations that would use-after-free.
These fixes are prerequisites for the DTA flow working end-to-end — screen
transitions unload panels, which destroy objects, which trigger ring unlinks.

## Estimated Effort

| Phase | Work | New Code | Deleted Code |
|-------|------|----------|-------------|
| 1: Auto-select handlers | Move auto-nav into per-screen handlers | ~60 lines | ~120 lines |
| 2: Panel cascade verification | Logging + testing | ~20 lines logging | 0 |
| 3: Remove workarounds | Delete 6 hacks | ~50 lines relocated | ~230 lines |
| 4: Background panel (optional) | Load turbo_shell for menus | ~30 lines | ~10 lines |
| **Total** | | ~160 lines added | ~360 lines removed |

Net: **-200 lines**, plus significantly simpler architecture.

## Testing Strategy

1. **Boot to main_screen** — verify existing auto-skip flow still works
2. **Set `DC3_SCREEN=game_screen DC3_SONG=boyfriend`** — verify auto-nav
   reaches game_screen through DTA flow (not manual GotoScreen)
3. **Check `GamePanel::PollForLoading()` logs** — all 5 gates should pass
4. **Verify venue renders** — HamDirector::Enter → VenueEnter → meshes draw
5. **Verify characters load** — HamWardrobe async pipeline completes
6. **Remove workarounds one at a time** — confirm no regression at each step
7. **Test without `DC3_SCREEN`** — boot stays on main_screen, no crashes

## Appendix: DTA Screen Chain Reference

```
attract_screen        → auto-skip via skip_selected
  ↓
autosave_warning_screen → auto-advance (90-frame safety)
  ↓
title_screen          → auto-advance (60-frame delay + next_screen)
  ↓
wait_main_after_saveload_screen → auto-advance (saveload_mgr.is_idle = 1)
  ↓
main_screen           → [NATIVE: auto-select perform mode, set game data]
  ↓
choose_mode_screen    → [NATIVE: auto-advance]
  ↓
song_select_screen    → [NATIVE: auto-advance (song already set)]
  ↓
multiuser_screen      → [NATIVE: auto-skip (no Kinect)]
  ↓
loading_screen        → DTA chains: preloading → real_loading → game_screen
  ↓
game_screen           → panel cascade: world_panel → HamDirector::Enter()
```

## Appendix: Smart Stub Coverage Audit

Full list of DTA queries each stub handles, verified against screen handlers:

### NativeSaveLoadStub
```
activate              → 0 (no-op)
is_idle               → 1 (always idle)
is_initial_load_done  → 1
is_autosave_enabled   → 0
autosave              → 0
```
**Missing**: None identified. All loading screen gates pass.

### NativeProfileMgrStub
```
has_active_profile           → 0
has_active_profile_no_override → 0
get_active_profile           → 0
get_num_valid_profiles       → 0
has_seen_tutorial            → 1 (skip tutorials)
mark_tutorial_seen           → 0
is_content_unlocked          → 1 (everything available)
is_difficulty_unlocked       → 1
get_disable_voice*           → 1 (no Kinect mic)
is_voice_commander_suboptimal → 1
get_music/fx/crowd_volume    → 8
get_venue_preference         → "default"
get_overscan                 → 0
get_mono                     → 0
get_disable_photos           → 0
get_disable_freestyle        → 0
get_no_flashcards            → 0
has_finished_campaign        → 0
get_all_unlocked             → 0
needs_upload                 → 0
global_options_needs_save    → 0
is_any_profile_signed_into_live → 0
```
**Missing**: Possible `get_num_players`, `get_player_outfit`, `get_player_crew`
queries from choose_mode or song_select. If hit, these would return
`kDataUnhandled` and the DTA handler would need a fallback.

### NativePlatformMgrStub
```
is_guide_showing             → 0
is_pad_signed_into_live      → 0
show_controller_required     → 0
enable_xmp / disable_xmp    → 0
guide_showing                → 0
```
**Missing**: `has_kinect`, `is_kinect_connected` — some screens may check Kinect
presence before showing gesture UI. If queried, these return `kDataUnhandled`.
Workaround: add `has_kinect`→0 to the stub.

### NativeSpeechMgrStub
```
set_rule              → 0
begin_recognition     → 0
set_recognizing       → 0
end_recognition       → 0
```
**Missing**: `is_recognizing`, `get_result` — VoiceInputPanel may poll these.
Non-blocking since voice input is optional.
