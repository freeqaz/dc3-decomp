# Session 52 — Animation Lifetime Fix (2026-03-12)

Step 1 of the [UI Animation Unwind Plan](../native/UI_ANIMATION_STATUS.md).

## Problem

`HamNavList::PlayEnterAnim()` created a dummy `AnimTask` (start=end=0) as a flag for `IsAnimating()`. On Xbox, something external (DTA scripts) calls `StopAnimation()`. On native, nothing does — and the `AnimTask` never self-deletes because `AnimTarget()` returns `this` (non-null), making the self-delete path in `AnimTask::Poll()` unreachable.

This caused a cascade:
- Enter animation was immediately killed by native `Poll()` to avoid permanent "still animating" state
- `UIManager` skipped all transition exit/enter waits to avoid hanging
- `PanelDir::Enter()` had to blanket-start flows and force PropAnims to end frame
- Text alpha hacks kept the frame readable despite incomplete state

## Changes

### 1. Timer-Based Enter Animation (`HamNavList.cpp`, `HamNavList.h`)

Replaced the dummy `AnimTask` with a timer:
- Computes duration from `mEnterAnim->EndFrame()/FramesPerUnit()` (default 0.5s)
- Stores `mEnterAnimStartTime` and `mEnterAnimDuration` as native-only members
- `Poll()` linearly interpolates ribbon frame from `StartFrame` to `EndFrame` during the animation period
- Input handlers (ButtonDown, select mode) gate on the timer instead of `IsAnimating()`

### 2. UIManager Transition Waits with Timeouts (`UI.cpp`)

Re-enabled exit/enter wait checks that were previously skipped entirely on native:
- `kTransitionTo` block: static frame counter, 30-frame timeout for exit waits
- `kTransitionFrom` block: static frame counter, 60-frame timeout for enter waits
- `UITrigger::IsBlocking()` is time-based (uses `UISeconds`), so triggers complete naturally

### 3. Removed Text Alpha Force Hacks

- **`HamListRibbon.cpp`**: Removed the `#ifdef HX_NATIVE` label alpha force-to-1.0 block from `DrawRibbon`
- **`MaterialSetup.cpp`**: Removed `NativeShouldForceTextAlpha` call and `kHeuristicAlphaForce` flag

## A/B Test: Alpha Floor

Tested removing the zero-alpha floor in `Mesh_Wgpu.cpp`:
- **With floor**: 614,593 non-black pixels
- **Without floor**: 543,826 non-black pixels (70K pixels of background content lost)
- **Decision**: Keep the floor for now — it's still load-bearing for decorative background meshes

## Visual Verification

Enter animation now produces visible frame-by-frame progression:
- Frame 3: teal glow bars beginning to appear
- Frame 5-8: bars expanding, menu items sliding in
- Frame 10-12: animation settling to final state
- Frame 220: fully rendered main menu

Screenshots archived: `archive/screenshots/session52/`

## Verification

- Frame 220 pixel count: 614,593 (vs 613,295 baseline) — no regression
- Text, background meshes, teal glow bars all render correctly
- PPC build clean (all `#ifdef HX_NATIVE` guards compiled out)
