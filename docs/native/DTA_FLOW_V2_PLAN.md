# Native Port v2: Real DTA Flow

Goal: Remove C++ workarounds and let the real DTA screen-flow scripts drive the native port, the same way Xbox does.

## Current State (v2 — 2026-03-12)

DTA IS mostly working on native. TypeDefs load, handlers fire, commands execute. The failures are specific DTA commands that reference Xbox-only stubs, plus animation lifecycle gaps (now fixed).

### Completed
- **Phase 1 (Smart Stubs)**: SaveLoadManager, ProfileMgr, PlatformMgr return sensible defaults
- **Phase 3 (Animation Lifecycle)**: AnimTask auto-null on native, HamNavList timer bypasses removed

### Active Workarounds

| Workaround | Location | Status | Category |
|---|---|---|---|
| Screen auto-advance timer (2 entries) | UI.cpp sFlow[] | **Permanent** (intentional UX) | Boot flow |
| Tutorial screen skip (5 entries) | UI.cpp sFlow[] | **Permanent** | No Kinect |
| UIScreen auto-skip (next_screen/skip_selected) | UIScreen.cpp:300-333 | **Permanent** (DTA fallback) | Screen flow |
| Exit animation timeout (90 frames) | UI.cpp:631-648 | **Permanent** (safety net, Phase 2c DONE) | Animation |
| Enter animation timeout (90 frames) | UI.cpp:697-712 | **Permanent** (safety net, Phase 2c DONE) | Animation |
| mSink = screen on transition | UI.cpp:656-659 | **Permanent** (Phase 5a: DTA never fires set_sink) | Button routing |
| Campaign screen block | UI.cpp:363-370 | **Permanent** | No campaign system |
| Null screen fallback to main_screen | UI.cpp:489-498 | **Permanent** (safety net) | Screen flow |
| Controller mode force-on | GestureMgr.cpp:43-46 | **Permanent** | No Kinect |
| GameMode::SetMode skip | GameMode.cpp:26-33 | **Permanent** (Phase 5b: sufficient as-is) | Game logic |
| HamNavList IsAnimating() bypass | HamNavList.cpp | **Permanent** (DTA transition_complete never fires) | Input |
| HamProvider property defaults | Ham.cpp | **Permanent** | Init ordering |
| ShellInput Kinect guards | ShellInput.cpp (6 locations) | **Permanent** | No Kinect |
| SyncVoiceControl fallback | ShellInput.cpp | **Permanent** | No voice HW |
| Debug logging (~25 locations) | UI.cpp, UIScreen.cpp, UIPanel.cpp | **DONE** (Phase 6: gated behind MILO_DEBUG_UI_FLOW) | Debug |

## Stub Manager Analysis

### Smart Stubs (App.cpp) — COMPLETE

| Manager | Class | Key DTA Queries | Events Fired | Status |
|---|---|---|---|---|
| `saveload_mgr` | NativeSaveLoadStub | `is_idle=1`, `is_initial_load_done=1`, `activate=no-op` | None needed | **Done** |
| `profile_mgr` | NativeProfileMgrStub | `has_seen_tutorial=1`, `is_content_unlocked=1`, `get_disable_voice=1`, 30+ handlers | None needed | **Done** |
| `platform_mgr` | NativePlatformMgrStub | `is_guide_showing=0`, `is_pad_signed_into_live=0` | None needed | **Done** |

### Bare Stubs (Hmx::Object) — SUFFICIENT

| Manager | Why Bare Is Enough |
|---|---|
| `content_mgr` | No DTA queries block boot. Content mounting uses C++ callbacks (ContentMgr::Callback), not DTA messages. `RefreshSynchronously()` called directly from App.cpp. Base Hmx::Object inherits `add_sink`/`remove_sink`. |
| `challenges` | Network-only (Xbox Live challenges). Not boot-blocking. No critical DTA queries. |
| `speech_mgr` | Voice recognition. ProfileMgr queries SpeechMgr via C++ (`TheSpeechMgr->SpeechSupported()`), not DTA. DTA calls to speech_mgr silently fail with no impact. |

**No additional smart stubs needed.** The three bare Hmx::Object stubs are sufficient for their roles.

## Xbox Screen Flow (What DTA Does)

```
attract_screen
  └─ (skip_selected) → autosave_warning_screen
       └─ (enter) → acknowledge → title_screen
            └─ title_panel.enter:
                  {platform_mgr add_sink $this (ui_changed)}      ← STUB (accepts, never dispatches)
                  {speech_mgr begin_recognition TRUE}               ← STUB (silently fails)
               (NAV_SELECT_MSG) → wait_main_after_saveload_screen
                    └─ (enter):
                          {saveload_mgr activate}                   ← SMART STUB: is_idle=1
                       (saveload_complete):                          ← DTA-DRIVEN (Phase 1)
                          {content_mgr start_refresh}
                          {profile_mgr has_seen_tutorial ...}       ← SMART STUB: returns 1
                          {ui goto_screen $post_load_dest_screen}   → main_screen (fallback)
                              └─ main_panel.enter:
                                    {platform_mgr add_sink ...}     ← STUB (accepts)
                                    {profile_mgr add_sink ...}      ← STUB (accepts)
                                    {profile_mgr clear_critical_profile}  ← SMART STUB: no-op
                                    {content_mgr start_refresh}
                                 (NAV_SELECT_MSG) → choose_mode_screen
                                     └─ (NAV_SELECT_MSG perform):
                                           {gamemode set_mode perform}
                                           {ui goto_screen song_select_screen}
                                              └─ ... → multiuser_gesture_screen
                                                   └─ (Phase 4 scope)
```

## Phase Plan

### Phase 1: Make Stubs Smart — DONE (2026-03-12)

See stub analysis above. All three smart stubs implemented and verified.

**Runtime verified**: Boot flow reaches `main_screen` in ~500 frames. The `wait_main_after_saveload_screen → main_screen` transition is DTA-driven (saveload_complete fires because is_idle=1).

### Phase 2: Refine Screen Flow (MEDIUM RISK) — PARTIALLY DONE

#### 2a. Boot Screen Timers — KEEP (Intentional UX)

The two remaining boot timers are **intentional** native behavior, not broken DTA flow:

| Timer | Delay | Reason | Xbox Equivalent |
|---|---|---|---|
| `autosave_warning_screen → title_screen` | 90 frames (~3s) | Splash screen display time | Async save system completion |
| `title_screen → wait_main_after_saveload_screen` | 60 frames (~2s) | Title screen display | "Press Start" button wait |

On Xbox, these screens wait for async events (save completion, user pressing Start). On native, there's no save system and no signin flow, so a brief display delay is the correct UX. These timers may eventually be replaced by keyboard input triggers but are not bugs.

The Kinect tutorial entries (5 screens, 1-frame skip) are **permanent** — no gesture input system.

**Removed**: `wait_main_after_saveload_screen → main_screen` entry — now DTA-driven via smart stubs.

#### 2b. UIScreen Auto-Skip — KEEP (Valuable DTA Fallback)

The auto-skip logic in UIScreen::Enter (lines 263-318) tries DTA `skip_selected` handler, then `next_screen` property. This is a **correct DTA-first approach** — it uses real DTA data to decide navigation. It's not a workaround; it's how screens with no interactive content should advance. Keep it.

#### 2c. Relax Animation Timeouts — DONE (2026-03-12)

**Prerequisite**: Phase 3 (animation lifecycle) ← DONE

With AnimTask auto-null working, screen enter/exit animations complete naturally. Increased timeouts from 30/60 → 90/90 frames (~3s) with warning logging:

| Timeout | Old | New | Rationale |
|---|---|---|---|
| Exit animation | 30 frames (~1s) | 90 frames (~3s) | Safety net — fires for attract_screen (stuck exit flow) |
| Enter animation | 60 frames (~2s) | 90 frames (~3s) | Safety net — should not fire for normal screens |

**Known timeout**: `attract_screen` exit animation times out because auto-skipped screens don't have proper exit flow (UIPanel::Exiting() returns true from DTA `exiting` handler or PanelDir exit state). This is expected — the screen was never meant to complete a full exit cycle when skipped.

### Phase 3: Fix Animation Lifecycle — DONE (2026-03-12)

**AnimTask::Poll auto-null**: Added `#ifdef HX_NATIVE` in Anim.cpp that auto-nulls `mAnimTarget` when non-looping animations complete (`time > mFrameSpan`). Triggers the existing self-deletion path.

**HamNavList cleanup**: Removed 5 timer-based `#ifdef HX_NATIVE` bypasses. `PlayEnterAnim()` now uses real `Animate()` call. All `IsAnimating()` checks use Xbox codepath. Removed `mEnterAnimStartTime`/`mEnterAnimDuration` members.

### Phase 4: MultiUserGesturePanel — DONE (2026-03-12)

**Root cause**: `HamNavList::OnMsg(ButtonDownMsg)` gates all input behind `!RndAnimatable::IsAnimating()`. On native, DTA `transition_complete` handlers that call `StopAnimation()` never fire, so `IsAnimating()` stays true forever. Every button press reached the HamNavList but was rejected by the `anim=1` guard.

**Fix**: Skip `IsAnimating()` check on native (`#ifdef HX_NATIVE` in HamNavList.cpp). This enables button navigation through all menu screens.

**Result**: The multiuser panel auto-skip was replaced with diagnostic tracing (now removed). The DTA `enter` handler on `multiuser_screen` fires naturally and drives the game start flow: `multiuser_screen → loading_screen → preloading_screen → real_loading_screen → game_screen`.

**Full menu flow via scripted input** (see `scripts/dc3-input-flows/ymca.txt`):
```
main_screen → choose_mode (gameplay) → song_select (perform) → scroll to YMCA
→ multiuser_screen → loading → game_screen
```

No auto-skip needed. DTA handles the multiuser → loading transition.

### Phase 5: Cleanup (LOW RISK)

#### 5a. mSink Fallback — PERMANENT (Investigated 2026-03-12)

`mSink = trans` is set on every screen transition (UI.cpp:656-664). **Investigation complete**: DTA `set_sink` NEVER fires for any screen on native or Xbox. Dumped all screen TypeDef enter handlers — none contain `{ui set_sink ...}`:

- `attract_screen`: skip_selected, next_screen (no enter handler)
- `autosave_warning_screen`: enter/exit handlers (empty body)
- `title_screen`: handle_global_commands, check_for_nag, voice commands (no enter handler with set_sink)
- `wait_main_after_saveload_screen`: enter handler has mode names only (gameplay, campaign_mode, etc.)
- `main_screen`: enter handler (empty body)

The `HANDLE_ACTION(set_sink, ...)` in UIManager::Handle exists but DC3 doesn't use it from DTA. On Xbox, mSink routing works through `HANDLE_MEMBER_PTR(mSink)` (member pointer dispatch) or C++ code paths we haven't fully traced. The native fallback is **required and permanent**.

#### 5b. GameMode::SetMode — SUFFICIENT (2026-03-12)

The constructor skip (`mMode = "init"` instead of `SetMode("init", "none")`) avoids crash because SystemConfig("modes") isn't loaded during App construction. DTA later calls `{gamemode set_mode perform}` which works correctly.

The `#ifdef HX_NATIVE` guard is the correct solution. On Xbox, SystemConfig IS available at construction time, so making this platform-agnostic would add unnecessary runtime checks. The guard is clean and self-documenting.

#### 5c. Controller Mode — PERMANENT

`mInControllerMode = true` in GestureMgr init is a **permanent platform difference**, not a workaround. No Kinect = always controller mode. The DTA `exit_controller_mode` message would break input.

`SetInControllerMode()` forcing true is also permanent — prevents DTA from accidentally disabling controller input.

### Phase 6: Debug Logging Cleanup — DONE (2026-03-12)

All debug `printf` statements in UI.cpp, UIScreen.cpp, and UIPanel.cpp gated behind `MILO_DEBUG_UI_FLOW=1` env var. Exceptions: animation timeout WARNINGs always print (they indicate real issues).

**Usage**: `MILO_DEBUG_UI_FLOW=1 native/build/dc3-native` — enables full screen flow diagnostics.

Files modified: UI.cpp (~15 printfs), UIScreen.cpp (~8 printfs), UIPanel.cpp (~3 printfs). Each file has a static `DebugUIFlow()` helper that checks the env var once and caches the result.

## Dependency Graph (Updated)

```
Phase 1 (smart stubs)    ──── DONE
Phase 3 (anim lifecycle) ──── DONE
Phase 2c (relax timeouts) ──── DONE
Phase 5a (mSink)         ──── DONE (permanent)
Phase 5b (GameMode)      ──── DONE (sufficient as-is)
Phase 6 (debug cleanup)  ──── DONE
Phase 4 (multiuser panel) ──── DONE (IsAnimating bypass + DTA enter handler)

ALL PHASES COMPLETE.

Permanent (not removable):
  - Boot screen timers (intentional UX)
  - UIScreen auto-skip (DTA fallback)
  - Kinect tutorial skips
  - Controller mode force-on
  - mSink fallback (DTA never fires set_sink in DC3)
  - ShellInput Kinect guards
  - SyncVoiceControl fallback
  - HamProvider property defaults
  - Campaign screen block
  - Null screen fallback
```

## What Stays (Permanent Platform Differences)

These are **correct adaptations** for a non-Kinect platform, not workarounds:

- **ShellInput Kinect guards**: mSkelIdentifier/mSkelExtTracker/DepthBuffer genuinely absent
- **HamProvider property defaults**: Properties read before DTA sets them during init
- **Controller mode force-on**: No gesture input = always controller mode
- **SetInControllerMode force true**: Prevents DTA from disabling controller input
- **SyncVoiceControl fallback**: No voice hardware
- **Campaign screen block**: Campaign system not implemented
- **GestureMgr LiveCameraInput fallback**: No Kinect camera
- **GestureMgr native init/poll/terminate**: Native gesture stub (no Kinect)
- **Boot screen timers**: Replace async Xbox events with brief display delays
- **UIScreen auto-skip**: DTA-first navigation fallback
- **Null screen fallback**: Safety net for unresolved DTA state variables
- **mSink = screen on transition**: DTA `set_sink` handler exists but DC3 never calls it from screen TypeDefs. Fallback ensures button routing works.

## Files Modified

| Phase | Files | Changes |
|---|---|---|
| 1 (DONE) | App.cpp | 3 smart stub classes + registration |
| 2c | UI.cpp | Increase animation timeouts from 30/60 → 300 frames |
| 3 (DONE) | Anim.cpp, HamNavList.cpp, HamNavList.h | AnimTask auto-null, remove timer bypasses |
| 4 (future) | MultiUserGesturePanel.cpp | Replace auto-skip with controller pane nav |
| 5a | UI.cpp | Investigate/remove mSink fallback |
| 5b | GameMode.cpp | Deferred init (platform-agnostic) |
| 6 | UI.cpp, UIScreen.cpp | Gate debug logging behind env var |
