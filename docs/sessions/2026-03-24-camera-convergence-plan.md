# Camera System Convergence Plan

**Date**: 2026-03-24
**Status**: RESOLVED — Scenario D confirmed, blend_time set in PlayNextShot

## Problem Statement

The web/native build's camera during song gameplay is "janky" — transitions are instant hard cuts instead of smooth blends, and the camera variety is limited compared to the original Xbox game.

## Root Cause

The Xbox camera system has **two cooperating paths** that fire simultaneously every frame during `songAnim->SetFrame()`:

| Path | What it does | Sets | Xbox | Native |
|------|-------------|------|------|--------|
| Symbol PropKeys ("shot") | Selects camera CATEGORY | `mShot`, `mPickNewShot` | Works | Works |
| kDirEvent PropKeys → EventTrigger → Flow → FlowCommand("pick_shot") | Sets blend_time, may override shot | `mBlendTime` | Works | **Silent** |

The C++ path (path 1) works identically on both platforms. The DTA/Flow path (path 2) — which is the **only source of `mBlendTime`** — never fires on native.

### Verified by diagnostics

- `CameraManager::OnPickCameraShot()` — never called on native (0 invocations)
- `EventTrigger::TriggerSelf()` — never called during gameplay (0 invocations)
- kDirEvent PropKeys — zero fire during `songAnim->SetFrame()`
- Symbol PropKeys ("shot") — 43 keys fire correctly, camera categories change as expected

## Architecture Detail

### Xbox Flow (working)

```
WorldDir::Poll()
  → HandleType("select_camera")
  → HamDirector::OnSelectCamera()
    → songAnim->SetFrame(frame, blend)
      → [Symbol PropKeys] "shot" → SetShot(category) → mPickNewShot = true
      → [kDirEvent PropKeys] → EventTrigger::Trigger()
        → TriggerSelf() → Flow activation
          → FlowCommand::Activate()
            → CameraManager::Handle("pick_shot", category, blend_time)
              → OnPickCameraShot() → MakeCategoryAndFilters() sets mBlendTime
    → FindNextShot() → CameraManager::FindCameraShot()
    → PlayNextShot() → CameraManager::ForceCameraShot()
  → CameraManager::PrePoll() → StartShot_()
  → CameraManager::Poll() → uses mBlendTime for smooth interpolation
```

### Native Flow (broken link)

The chain breaks at **kDirEvent PropKeys** — the song.anim used on native has zero kDirEvent keys. Three possible causes:

1. **Pre-authored song.anim lacks kDirEvent keys** — the event keys may be venue-specific, not song-specific
2. **Routine builder Copy strips kDirEvent keys** — `routineBuilderAnim->Copy(anim, kCopyDeep)` may not preserve ObjectKeys with kDirEvent exception
3. **kDirEvent target resolution fails** — PropKeys target a WorldDir's "event" property; after merge the target ObjectDir may not resolve

### Where pick_shot is defined

`pick_shot` is a **flow_command** on CameraManager, defined in `orig-assets/extracted/(..)/(..)/system/run/world/world_objects.dta`:

```dta
(flow_commands
   (pick_shot
      (editor
         (category symbol (help "Camera category"))
         (blend_time float (help "Blend time into this camera, in frames")))
      (category '')
      (blend_time 0.0)))
```

It's invoked from **Flow objects inside venue .milo files** (binary graph data), NOT from DTA text scripts. The FlowCommand stores `mObject` (CameraManager) and `mHandler` ("pick_shot") with parameters `(category, blend_time)`.

### How blend_time drives interpolation

In `CameraManager::Poll()` (lines 262-293):

```cpp
if (mBlendTime > 0.0f) {
    frame = Clamp(0.0f, 1.0f, frame / mBlendTime);  // normalize 0→1
} else {
    frame = 1.0f;  // instant snap
}
if (frame < 1.0f) {
    // Interpolate position, rotation, FOV between old and new shot
    Interp(savedXfm.v, localXfm.v, frame, localXfm.v);
    // ... rotation + frustum interpolation
}
```

With `mBlendTime == 0`, all transitions are instant snaps with no interpolation.

## Current Stopgap Hack

Per-transition blend_time set in `HamDirector::PlayNextShot()`:

```cpp
#ifdef HX_NATIVE
if (mCurShot) {
    CamShot *prev = camMgr->CurrentShot();
    bool sameCategory = prev && prev->Category() == mCurShot->Category();
    camMgr->SetBlendTime(sameCategory ? 10.0f : 15.0f);
}
#endif
```

Also added `CameraManager::SetBlendTime(float)` public setter in `CameraManager.h`.

This gives smooth transitions but with fixed blend times rather than the per-shot-authored values from venue Flow objects.

## Implementation Plan

### Phase 1: Diagnose kDirEvent Absence (est. 1-2 hours)

**Goal**: Determine why song.anim has zero kDirEvent PropKeys on native.

1. Check the **pre-authored song.anim** (not routine builder copy) for kDirEvent PropKeys:
   ```cpp
   RndPropAnim *preauthored = SongAnimByDifficulty(kDifficultyEasy);
   // Count PropKeys with GetExceptionID() == PropKeys::kDirEvent
   ```

2. Check if `routineBuilderAnim->Copy(anim, kCopyDeep)` preserves kDirEvent PropKeys — compare key counts before/after copy

3. Check if kDirEvent PropKeys exist in a **different PropAnim** — maybe they're in the venue's own anim, not song.anim

4. Check if kDirEvent keys exist but the target ObjectDir doesn't resolve, causing the PropKeys to be silently dropped during load

### Phase 2: Fix the Event Chain (depends on Phase 1)

**Scenario A**: kDirEvent keys are in a venue PropAnim, not song.anim
- The venue PropAnim needs to be driven (SetFrame called) during gameplay
- Check if it's supposed to be animated via the task system (Animate() on Enter)
- Ensure VenueEnter activates venue PropAnims

**Scenario B**: kDirEvent keys exist but target doesn't resolve after merge
- Fix object resolution in PropKeys after venue merge into world root
- Similar to the PropKeys retargeting fix already done for routine builder

**Scenario C**: kDirEvent keys are stripped by routine builder Copy
- Preserve kDirEvent PropKeys during copy, or
- Use pre-authored song.anim for event evaluation while using routine builder for clip/move keys

### Phase 3: Verify Flow System (est. 1-2 hours)

Once EventTriggers fire:

1. Ensure venue Flow objects get `Enter()` called during `VenueEnter()`
2. Ensure FlowCommand target (`mObject` → CameraManager) resolves after venue merge
3. Verify `FlowCommand::Activate()` successfully dispatches "pick_shot" with blend_time
4. Verify `MakeCategoryAndFilters()` correctly parses the blend_time from FlowCommand arguments

### Phase 4: Remove Native Workarounds

After convergence confirmed:

| Guard | File | Lines | Remove? |
|-------|------|-------|---------|
| blend_time hack in PlayNextShot | HamDirector.cpp | ~2530 | YES — DTA path sets it |
| CalcFrame NaN guard | CameraManager.cpp | 109-115 | MAYBE — keep as safety net |
| Poll transform NaN guard | CameraManager.cpp | 233-260 | MAYBE — keep as safety net |
| 6x CameraShot NaN guards | CameraShot.cpp | various | MAYBE — keep as safety net |
| FindNextShot Area1_WIDE fallback | HamDirector.cpp | 2262-2269 | MAYBE — defensive default |

**Recommendation**: Keep NaN guards even after convergence — they're cheap and protect against edge cases. Remove the blend_time hack and Area1_WIDE fallback.

## Key Files

| File | Role |
|------|------|
| `src/system/hamobj/HamDirector.cpp` | OnSelectCamera, FindNextShot, PlayNextShot, SetShot |
| `src/system/world/CameraManager.cpp` | Poll (blend interpolation), OnPickCameraShot, MakeCategoryAndFilters |
| `src/system/world/CameraManager.h` | SetBlendTime setter (new), mBlendTime member |
| `src/system/world/CameraShot.cpp` | CamShot keyframe interpolation, NaN guards |
| `src/system/rndobj/PropAnim.cpp` | SetFrame — fires kDirEvent PropKeys → EventTriggers |
| `src/system/rndobj/EventTrigger.cpp` | TriggerSelf — activates Flows and proxy calls |
| `src/system/flow/FlowCommand.cpp` | Activate — dispatches pick_shot to CameraManager |
| `src/system/flow/Flow.cpp` | Flow graph activation and Enter |
| `orig-assets/extracted/world/world_objects.dta` | pick_shot flow_command definition |

## Other Findings

### Feet in Floor

Characters' feet appear slightly embedded in the floor surface. Investigation showed:
- Bone matrices computed correctly (`BoneOffset * WorldXfm`)
- Shader applies transforms correctly
- CharIKFoot system is implemented
- Issue may be camera-angle dependent or a subtle Y-offset — deferred for separate investigation

### Pre-existing Build Issues Fixed

- `GamePanel.cpp:474` — `ObjectDir::SetShowing` doesn't exist, needed `#include "rndobj/Dir.h"` for `RndDir` base class
- `HamDirector.cpp:457` — `ClassName()` returns `Symbol`, needed `.Str()` for `strstr()`
