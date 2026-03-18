# Song Select Scroll Fix + Skeleton Backend Wiring

**Date:** 2026-03-17

## Problem

Song select UI shows ~5 items but scrolling down doesn't change the visible songs. The list state advances internally (selected index, firstShowing) but the visual rendering never updates.

## Investigation

### Initial hypothesis (wrong)

The plan suspected `UIList::LimitCircularDisplay()` was capping `mNumDisplay` to 1 because the provider had 0-1 items when `SetProvider()` was called during async loading.

Diagnostic logging confirmed this does happen for `HamList` (the `UIList` subclass used by choose_mode), but it's not the song select bug. The choose_mode list works fine — `numDisp` gets capped to 3 (matching the 3 mode options).

### Key discovery: Song select uses HamNavList, not UIList

The song select list at `right_hand.hnl (ui/song_select/song_select.milo)` is a **`HamNavList`** — a completely separate list implementation that shares `UIListState` for scroll state but has its own rendering pipeline via `HamListRibbon` and `HamScrollBehavior`.

`HamNavList` is a `UIComponent` + `RndAnimatable` + `UIListProvider` + `UIListStateCallback` + `SkeletonCallback`. It does NOT inherit from `UIList`.

### Root cause: Skeleton-gated scroll progress

In `HamNavList::Poll()`, the flow is:

```
Skeleton *skeleton = TheGestureMgr->GetSkeletonByTrackingID(mSkeletonTrackingID);
if (skeleton && skeleton->IsValid() && !skeleton->IsSideways() && !sForceDisengage) {
    UpdateGestures(skeleton);  // <-- mScrollBehavior.Update() is in here
}
```

`mScrollBehavior.Update()` drives pending scrolls to completion — it accumulates `dt * speed` each frame until progress exceeds 1.0, then calls `mListState->Scroll()` which triggers `StartScroll`/`CompleteScroll` callbacks and updates the visual elements.

**On Xbox 360:** Even in controller mode, the Kinect sensor runs and provides skeleton data. `UpdateGestures()` always executes. Controller mode just means button input drives navigation instead of gestures — the skeleton is still there.

**On native:** No Kinect = no skeleton = `UpdateGestures()` never runs = pending scrolls from `OnMsg(ButtonDownMsg)` never complete. Buttons queue scrolls that nothing ever processes.

### Data flow confirmation

Logging confirmed:
- `OnMsg(ButtonDownMsg)` fires correctly with `btn=8` (kAction_Down)
- `InControllerMode()` = true, `IsAnimating()` = false, focus is correct
- `ScrollDirection()` returns 1 (down), `NumShowing()` = 49
- `mScrollBehavior.ScrollDown()` sets `mPendingScrollDir = 2`
- But `mScrollBehavior.Update()` never runs → scroll never completes

## Fix: Dummy Skeleton

### Design choice

Rather than adding workarounds to skip skeleton checks, we wire up the Kinect backend properly by providing a **dummy skeleton** when no pose server is connected.

This is Option B from the design evaluation: a neutral standing pose with hands at sides (below hip height). It:
- Passes the quality filter (20 confident joints, not sitting, not sideways)
- Keeps gesture filters in "disengaged" state (hands below hips = no accidental swipe triggers)
- Makes all skeleton-gated code paths work without special cases

### Implementation

**`native/src/platform/Skeleton_Native.cpp`** — `NativeSkeletonProvider::FillDummySkeleton()`

Static method (uses friend access to `Skeleton` protected members). Fills all 20 joints with a standing T-pose in camera coordinates (meters, Y-up):
- Head at 1.6m, shoulders at 1.4m, hips at 0.9m
- Hands at 0.85m (below hip center at 0.9m = disengaged)
- All joints `kConfidenceTracked`, tracking state `kSkeletonTracked`, ID = 1

**`native/src/platform/GestureMgr_Native.cpp`** — `GestureMgr_NativePoll()`

- If pose server is connected: works as before (YOLO data fills skeletons)
- If no pose server: calls `FillDummySkeleton()` on slot 0
- Sets `mActiveSkelTrackingID = 1` if not already set
- Always runs `PostUpdate()` pipeline (quality filters, identity tracking)

Also moved `NativeCameraInput` creation to `GestureMgr_NativeInit()` so it's always available (was previously skipped in headless mode).

### Safety net kept

The `#ifdef HX_NATIVE` workaround in `HamNavList::Poll()` that directly calls `mScrollBehavior.Update()` without a skeleton is kept as a safety net. If the quality filter ever rejects the dummy skeleton, scrolling still works.

## Other changes in this session

### `wait_screen` input script directive

**`native/src/platform/Joypad_Native.cpp`**

Replaced frame-based input scripts with event-based synchronization:

```
wait_screen main_screen     # blocks until screen is active + not transitioning
+30 confirm                  # 30 frames after wait satisfied
```

- `wait_screen <name>` — blocks script execution until `TheUI->CurrentScreen()->Name()` matches and `!TheUI->InTransition()`
- `+N button` — relative frame offset from last wait satisfaction
- Absolute `N button` still works (backwards compatible)
- 30-second timeout with warning
- Old scripts with only absolute frames work unchanged

### `UIList::Refresh()` defensive fix

Added `LimitCircularDisplay` re-evaluation in `Refresh()` for `UIList` (not `HamNavList`). When a circular list's provider gets more data after initial `SetProvider()`, `mNumDisplay` is uncapped to match. This is defense-in-depth for async providers — doesn't trigger in current code but prevents the category of bug.

## Subsystem Architecture (for future work)

### Skeleton pipeline on native

```
GestureMgr::Poll()
  └─ GestureMgr_NativePoll(mgr)
       ├─ If pose server: TheSkeletonProvider->Poll() → FillSkeleton()
       ├─ If no server:   FillDummySkeleton(slot 0)
       ├─ Set mActiveSkelTrackingID = 1
       └─ mgr->PostUpdate(&data)  ← runs quality filters, gesture callbacks
            └─ GestureMgr::Update(SkeletonUpdateData)
                 ├─ SkeletonQualityFilter for each slot
                 └─ Registered SkeletonCallback::Update() calls
```

### What's wired up

| Component | Status |
|-----------|--------|
| GestureMgr singleton | Working |
| 6 skeleton slots | Working (slot 0 filled by dummy or YOLO) |
| Quality filter pipeline | Working (validates dummy pose) |
| Gesture filters (28) | Compiled + linked (see disengaged skeleton) |
| NativeSkeletonProvider | Working (YOLO COCO-17 → DC3 20-joint mapping) |
| HamScrollBehavior | Working (scroll progress driven by Update()) |
| Dummy skeleton fallback | NEW — neutral pose when no camera |

### What's NOT wired up (future work)

| Component | Notes |
|-----------|-------|
| SkeletonUpdate thread | Stubbed — native does synchronous polling |
| LiveCameraInput | Stubbed — NativeCameraInput returns null frames |
| SkeletonChooser UI | Bypassed on native (`mNativeEnterPending` flag) |
| SpeechMgr | Stubbed |
| Camera tilt control | Stubbed |
| Gesture-driven gameplay | Needs YOLO pose server running |

### To test with real pose data

```bash
# Terminal 1: start YOLO pose server
python3 native/scripts/pose_server.py

# Terminal 2: run with camera
DC3_POSE_SOCKET=/tmp/dc3_pose.sock native/build/dc3-native
```

## Files changed

| File | Change |
|------|--------|
| `native/src/platform/GestureMgr_Native.cpp` | Dummy skeleton fallback, camera input init fix |
| `native/src/platform/Skeleton_Native.cpp` | `FillDummySkeleton()` implementation |
| `native/src/platform/Skeleton_Native.h` | `FillDummySkeleton()` declaration |
| `native/src/platform/Joypad_Native.cpp` | `wait_screen` directive system |
| `src/system/hamobj/HamNavList.cpp` | Scroll behavior safety net (kept) |
| `src/system/ui/UIList.cpp` | `Refresh()` defensive numDisplay fix |
| `src/system/ui/UIListDir.cpp` | Scroll debug logging (pre-existing) |
| `scripts/dc3-input-flows/song-scroll-test.txt` | Converted to wait_screen |
| `scripts/dc3-input-flows/README.txt` | Documented new format |

## Verification

- Headless scroll test: 8 down presses on song_select_screen, all fire correctly
- `wait_screen` timing: main_screen@449, choose_mode@494, song_select@539
- PPC decomp: zero regressions (all native changes are `#ifdef HX_NATIVE`)
