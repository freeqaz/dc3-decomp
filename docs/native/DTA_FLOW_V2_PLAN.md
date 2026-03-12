# Native Port v2: Real DTA Flow

Goal: Remove C++ workarounds and let the real DTA screen-flow scripts drive the native port, the same way Xbox does.

## Current State (v1)

The native port has ~15 C++ workarounds that replace DTA-driven logic:

| Workaround | Location | What it replaces |
|---|---|---|
| Screen auto-advance timer | UI.cpp:554-606 | DTA enter/exit handlers that advance screens |
| mSink direct assignment | UI.cpp:659 | DTA `set_sink` handler |
| IsAnimating bypass | HamNavList.cpp | DTA `anim_done` → StopAnimation lifecycle |
| Controller mode force-on | GestureMgr.cpp:43-46 | DTA `enter_controller_mode` / `exit_controller_mode` |
| GameMode::SetMode skip | GameMode.cpp | DTA property evaluation referencing uninitialized objects |
| MultiUserGesturePanel auto-skip | MultiUserGesturePanel.cpp:64-112 | Kinect chooser → venue/char selection → loading_screen |
| Provider insurance | App.cpp:161-192 | DTA `ham_init.dta` player_provider creation |
| HamProvider fallback | Ham.cpp:84-100 | DTA `ham_init.dta` hamprovider creation |
| Property defaults | Ham.cpp:192-213 | DTA scripts that set party/skeleton/controller state |
| ShellInput guards | ShellInput.cpp (6 locations) | Kinect subsystems that don't exist on native |
| 8 stub managers | App.cpp:217-230 | Xbox-only platform services |
| EnterControllerMode no-op | ShellInput.cpp:404-409 | HelpBarPanel that may not be loaded |
| SyncVoiceControl fallback | ShellInput.cpp:333-348 | Speech/Kinect voice control |
| UIScreen auto-skip | UIScreen.cpp:294-318 | DTA `skip_selected` / `next_screen` handlers |
| Exit/enter animation timeouts | UI.cpp:631-702 | Animations that never complete |

**Key finding**: DTA IS mostly working on native (Session 39 confirmed). TypeDefs load, handlers fire, commands execute. The failures are specific DTA commands that reference Xbox-only manager stubs.

## Xbox Screen Flow (What DTA Does)

```
attract_screen
  └─ (skip_selected) → autosave_warning_screen
       └─ (enter) → acknowledge → title_screen
            └─ title_panel.enter:
                  {platform_mgr add_sink $this (ui_changed)}      ← STUB
                  {speech_mgr begin_recognition TRUE}               ← STUB
               (NAV_SELECT_MSG) → wait_main_after_saveload_screen
                    └─ (enter):
                          {saveload_mgr activate}                   ← STUB: never fires saveload_complete
                       (saveload_complete):
                          {content_mgr start_refresh}
                          {profile_mgr has_seen_tutorial ...}       ← STUB: returns null
                          {ui goto_screen $post_load_dest_screen}   → main_screen
                              └─ main_panel.enter:
                                    {platform_mgr add_sink ...}     ← STUB
                                    {profile_mgr add_sink ...}      ← STUB
                                    {profile_mgr clear_critical_profile}  ← STUB
                                    {content_mgr start_refresh}
                                 (NAV_SELECT_MSG) → choose_mode_screen
                                     └─ (NAV_SELECT_MSG perform):
                                           {gamemode set_mode perform}
                                           {ui goto_screen song_select_screen}
                                              └─ ... → multiuser_gesture_screen
                                                   └─ venue_select_pane.select:
                                                         {meta_performer set_venue_pref $name}
                                                         {meta_performer setup_venue}
                                                      startgame_pane.on_select_play:
                                                         {multiuser_panel start_game}
                                                            → loading_screen
```

**5 stub managers** cause DTA commands to silently fail. The auto-advance timers paper over the missing callbacks.

## v2 Plan: Remove Workarounds by Fixing Stubs

### Phase 1: Make Stubs Smart (LOW RISK)

Instead of empty `Hmx::Object()` stubs, give each manager minimal Handle() implementations that return sensible values. This lets DTA scripts execute correctly without implementing real Xbox platform services.

#### 1a. SaveLoadManager stub — CRITICAL
**Problem**: `{saveload_mgr activate}` does nothing → `saveload_complete` never fires → screen stuck until timer.
**Fix**: Add `HANDLE_ACTION(activate, ...)` that immediately posts `saveload_complete` message.
```cpp
// In the stub or SaveLoadManager native path:
HANDLE_ACTION(activate, {
    // No save system on native — immediately signal completion
    static Message saveload_complete("saveload_complete");
    TheUI->Handle(saveload_complete, false);
})
HANDLE_EXPR(is_idle, 1)  // Always idle on native
```
**Impact**: Removes UI.cpp auto-advance timer for wait_main_after_saveload_screen. DTA's `saveload_complete` handler fires naturally → runs `content_mgr start_refresh` → checks profile state → calls `goto_screen`.

#### 1b. ProfileMgr stub — CRITICAL
**Problem**: `{profile_mgr has_active_profile}` returns null → DTA conditionals fail → tutorial/save flows break.
**Fix**: Return sensible defaults for all queries:
```
has_active_profile      → 1 (yes)
get_active_profile      → stub Profile object
has_seen_tutorial(X)    → 1 (skip all tutorials)
get_disable_voice       → 1 (no voice)
is_content_unlocked(X)  → 1 (unlock all)
get_num_valid_profiles  → 1
get_venue_preference    → "default"
```
**Impact**: DTA tutorial checks pass. Content unlock gates open. Profile-dependent screens work. Removes need for dedicated auto-skip of tutorial_voice_control screen.

#### 1c. PlatformMgr stub — MEDIUM
**Problem**: `{platform_mgr add_sink $this (ui_changed)}` silently fails → no live status events.
**Fix**: Accept `add_sink`/`remove_sink` calls (store sinks, never dispatch). DTA scripts continue past these calls without error.
```
add_sink         → accept and store (no-op dispatch)
remove_sink      → accept and remove
is_guide_showing → 0
```
**Impact**: DTA enter handlers for title_panel, main_panel complete fully. No functional change (no Xbox Live events to dispatch).

#### 1d. ContentMgr — LOW (verify only)
**Problem**: `RefreshSynchronously()` might hang if state machine stuck.
**Fix**: Verify `ContentMgr.Init()` sets correct initial state. If `mRootLoaded = 0`, synchronous refresh completes immediately (current behavior appears working).
**Impact**: Confirms existing behavior is correct; no code change likely needed.

#### 1e. SpeechMgr — NO CHANGE
Already a stub. DTA calls silently fail with no functional impact. Voice commands don't exist on native.

### Phase 2: Remove Screen Auto-Advance (MEDIUM RISK)

With smart stubs in place, DTA handlers should drive screen transitions naturally. Remove the hardcoded workarounds one at a time:

#### 2a. Remove UI.cpp auto-advance table
**Prerequisite**: Phase 1a (saveload_mgr) and 1b (profile_mgr) complete.
**Change**: Remove the native-only screen flow table at UI.cpp:575-588.
**Test**: Boot to main_screen via DTA-driven flow. Each screen transition should happen through DTA `goto_screen` calls, not timer-based auto-advance.
**Fallback**: Keep 120-frame emergency timeout as safety net (but log a warning if it fires).

#### 2b. Remove UIScreen auto-skip logic
**Prerequisite**: Phase 2a verified working.
**Change**: Remove native-only `skip_selected`/`next_screen` fallback at UIScreen.cpp:294-318.
**Test**: Attract screen skips via DTA handler. Loading screens advance via DTA completion callbacks.

#### 2c. Remove animation timeouts
**Prerequisite**: Animation lifecycle working (see Phase 3).
**Change**: Remove 30-frame exit timeout (UI.cpp:631-643) and 60-frame enter timeout (UI.cpp:691-702).
**Risk**: If any animation is truly stuck, the UI hangs. Keep timeout but increase to 5s and log warning.

### Phase 3: Fix Animation Lifecycle (MEDIUM RISK)

#### 3a. DTA `anim_done` → StopAnimation chain
**Problem**: On Xbox, DTA `anim_done` handlers call `StopAnimation()` which nulls `AnimTask::mAnimTarget`, allowing the task to self-delete. On native, `anim_done` handlers may not fire because the animation system timing is different.
**Investigation**: Check if PropAnim completion callbacks fire on native. If they do, the DTA chain should work. If not, the issue is in AnimTask polling or Timer resolution.
**Fix**: Ensure `AnimTask::Poll()` correctly detects animation completion and fires callbacks.

#### 3b. Remove IsAnimating bypass
**Prerequisite**: Phase 3a verified.
**Change**: Remove `#ifdef HX_NATIVE` bypass in HamNavList.cpp that skips `IsAnimating()` check.
**Test**: Menu navigation still works — enter animations complete, input not blocked.

### Phase 4: Remove MultiUserGesturePanel Auto-Skip (HIGH RISK)

This is the biggest change. Currently the native port hardcodes venue/character selection and jumps to loading_screen. The real flow goes through an interactive multiuser gesture panel.

#### 4a. Controller-only multiuser flow
**Approach**: On native (no Kinect), the multiuser panel should enter controller mode and let the player navigate venue/character/difficulty selection via gamepad/keyboard.
**DTA pane flow** (from multiuser.dta):
```
seldiff_pane → character_select_pane → venue_select_pane → startgame_pane → start_game
```
Each pane uses HamNavList providers (VenueProvider, CharacterProvider, CrewProvider, DifficultyProvider) which are already created in MultiUserGesturePanel::Enter().
**Fix**: Remove `mNativeAutoSkipPending` logic. Let the panel's normal Poll() run. The DTA pane handlers drive navigation via `set_pending_pane`. When the user selects "play" in startgame_pane, DTA calls `{multiuser_panel start_game}` which triggers `setup_venue` + `goto loading_screen`.

#### 4b. Single-player shortcut
For quick testing or single-player modes, add a native-only "auto-select defaults" option that programmatically navigates the panes (instead of hardcoded skip):
```cpp
// Instead of bypassing the panel entirely, simulate selections:
// 1. Enter seldiff_pane, select default difficulty
// 2. Enter character_select_pane, select default character
// 3. Enter venue_select_pane, select default venue
// 4. Enter startgame_pane, select "play"
```
This uses the real DTA handlers at each step, just with automated input.

### Phase 5: Remove Remaining Workarounds (LOW RISK)

#### 5a. mSink assignment
**Prerequisite**: DTA `set_sink` commands execute correctly (they should after Phase 1).
**Change**: Remove `mSink = screen` fallback in UI.cpp:659.
**Test**: Button routing works through DTA-assigned sink.

#### 5b. GameMode::SetMode
**Prerequisite**: Profile stubs return valid data (Phase 1b).
**Change**: Remove native guard that skips property evaluation.
**Test**: `{gamemode set_mode perform}` correctly evaluates all properties.

#### 5c. Controller mode force-on
**Change**: Remove `mInControllerMode = true` in GestureMgr init. Let DTA `enter_controller_mode` message set it (fires when entering main_screen on Xbox).
**Prerequisite**: ShellInput::EnterControllerMode native path works (already implemented).
**Risk**: If `enter_controller_mode` DTA message doesn't fire on native, input breaks. Keep as last removal.

## Dependency Graph

```
Phase 1a (saveload_mgr) ─┐
Phase 1b (profile_mgr) ──┼──→ Phase 2a (remove auto-advance) ──→ Phase 2b (remove auto-skip)
Phase 1c (platform_mgr) ─┘                                              │
                                                                         v
Phase 3a (anim lifecycle) ──→ Phase 3b (remove IsAnimating bypass) ──→ Phase 2c (remove timeouts)
                                                                         │
Phase 4a (multiuser flow) ─────────────────────────────────────────→ Phase 4b (auto-select)
                                                                         │
                                                                         v
                                                                  Phase 5 (cleanup)
```

## Testing Strategy

Each phase should be tested independently before proceeding:

1. **Boot-to-main**: `MILO_MAX_FRAMES=1000` — verify boot reaches main_screen without auto-advance timer firing
2. **Mode select**: Input script navigating choose_mode_screen → song_select
3. **Full flow**: Boot → mode → song → multiuser → loading → gameplay
4. **Screenshot diff**: Compare native screenshots to Xbox reference at each screen

Key env vars for testing:
```bash
# Verbose DTA execution logging
MILO_DTA_TRACE=1

# Skip to specific screen
MILO_FIRST_SCREEN=main_screen

# Headless with screenshots at key screens
MILO_RENDER=1 MILO_HEADLESS=1 \
  MILO_SCREENSHOT_FRAMES=100,300,500,800,1200 \
  MILO_MAX_FRAMES=1500
```

## Files to Modify

| Phase | Files | Changes |
|---|---|---|
| 1a | App.cpp or SaveLoadManager.cpp | SaveLoadManager native HANDLE_ACTION(activate) |
| 1b | App.cpp or ProfileMgr.cpp | ProfileMgr native query handlers |
| 1c | App.cpp | PlatformMgr add_sink/remove_sink handlers |
| 2a | UI.cpp | Remove auto-advance table (lines 554-606) |
| 2b | UIScreen.cpp | Remove auto-skip logic (lines 294-318) |
| 2c | UI.cpp | Increase animation timeouts, add logging |
| 3a | AnimTask or PropAnim | Fix completion callback chain |
| 3b | HamNavList.cpp | Remove IsAnimating bypass |
| 4a | MultiUserGesturePanel.cpp | Remove auto-skip, let normal Poll run |
| 5a | UI.cpp | Remove mSink fallback |
| 5b | GameMode.cpp | Remove SetMode native guard |
| 5c | GestureMgr.cpp | Remove force controller mode |

## Risk Assessment

- **Phase 1**: Very low risk — adding handlers to stubs, no existing behavior changes
- **Phase 2**: Medium — removing safety nets. Keep emergency timeouts with warnings.
- **Phase 3**: Medium — animation lifecycle is complex. Test thoroughly.
- **Phase 4**: High — multiuser panel is the most complex flow. May need iterative debugging.
- **Phase 5**: Low — cleanup after everything else works.

## What Stays

Some workarounds are permanent (not DTA-related):

- **ShellInput Kinect guards**: mSkelIdentifier/mSkelExtTracker/DepthBuffer are genuinely absent on native (no Kinect hardware). These guards are correct, not workarounds.
- **HamProvider property defaults**: Even with full DTA flow, some properties may be read before DTA sets them during init. The defensive defaults in Ham.cpp should stay.
- **ExitControllerMode no-op**: Without Kinect gesture input, there's no way to re-enter controller mode after exiting. This is a platform difference, not a workaround.
- **SyncVoiceControl fallback**: No voice hardware = no voice control. Permanent.
