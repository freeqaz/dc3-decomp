# Camera Blend Implementation Results

**Date**: 2026-03-24
**Status**: RESOLVED

## Summary

Implemented the camera convergence plan from `2026-03-24-camera-convergence-plan.md`. The investigation confirmed **Scenario D**: no venue-level camera events exist, so `mBlendTime` was always 0 on Xbox too for the C++ shot-selection path. Fixed by setting blend_time directly in `HamDirector::PlayNextShot()`.

## Diagnostic Results

### Step 1: kDirEvent PropKeys in song anims

All song anims have **zero kDirEvent PropKey tracks**:

```
SongAnim(0) = 'song.anim': 0 kDirEvent keys across 5 PropKey tracks
SongAnimByDifficulty(0) = 'song.anim': 0 kDirEvent keys across 5 tracks
SongAnimByDifficulty(1) = 'song.anim': 0 kDirEvent keys across 5 tracks
```

The 5 PropKey tracks are all regular symbol/clip keys (shot category, clip, move, etc.). No kDirEvent keys exist in any song.anim.

### Step 2: Venue EventTriggers and CamShot anims

Tested across multiple venues (throneroom, dci). All show the same pattern:

```
VenueDir (throneroom): 225 PropAnims, 0 kDirEvent, 0 EventTriggers, 41 Flows, 0 FlowCommands
VenueDir (dci): 316 PropAnims, 0 kDirEvent, 0 EventTriggers, 55 Flows, 0 FlowCommands
```

Key findings:
- **0 kDirEvent PropKey tracks** in any venue PropAnim
- **0 EventTriggers** in any venue (the trigger mechanism doesn't exist)
- **0 FlowCommands** (no `pick_shot` dispatch nodes exist in the venue Flow graphs)
- The 41-55 Flows are for lighting/effects (e.g. `setup_projection_cam.flow`), not camera

### Step 3: Flow activation

`Flow::Enter()` fires correctly for UI panels (game_screen, etc.) but no venue Flows fire during gameplay because the venues lack the EventTrigger→Flow→FlowCommand chain.

### Step 4: OnPickCameraShot

`CameraManager::OnPickCameraShot()` was never called during the entire gameplay session (0 invocations). Confirmed the DTA `pick_shot` path is completely inert.

## Root Cause (confirmed)

The `pick_shot` flow_command is defined in `world_objects.dta` as part of CameraManager's schema, but **no DC3 venue actually instantiates the FlowCommand nodes** that would dispatch it. The venue Flow graphs handle lighting, particles, and projection cameras — not shot picking.

The C++ camera path (`songAnim PropKeys["shot"] → SetShot → FindNextShot → PlayNextShot → ForceCameraShot`) is the **only** camera selection mechanism in use. This path never set `mBlendTime`, so all transitions were instant snaps (`mBlendTime=0 → frame=1.0 → no interpolation`).

## Fix

Added blend_time assignment in `HamDirector::PlayNextShot()` before `ForceCameraShot()`:

```cpp
#ifdef HX_NATIVE
if (world && world->GetCameraManager()) {
    CamShot *prev = world->GetCameraManager()->CurrentShot();
    bool sameCategory = prev && curShot && prev->Category() == curShot->Category();
    world->GetCameraManager()->SetBlendTime(sameCategory ? 10.0f : 15.0f);
}
#endif
```

- **Same-category cuts**: 10 frames (~0.33s) — tighter blend for similar angles
- **Cross-category transitions**: 15 frames (~0.5s) — wider blend for camera pans

The `SetBlendTime()` accessor on CameraManager (added during initial investigation) remains.

## Files Changed

| File | Change |
|------|--------|
| `src/system/hamobj/HamDirector.cpp` | Added blend_time in PlayNextShot |
| `src/system/world/CameraManager.h` | SetBlendTime accessor (pre-existing) |
| `src/system/char/CharEyes.cpp` | Fixed `erase()` with raw pointer → iterator (pre-existing build fix) |

## Verification

Screenshots at frames 12000-12515 show smooth camera transitions with gradual position/rotation interpolation between shots. No hard cuts visible. Camera blending confirmed working via `CameraManager::Poll()` interpolation path (lines 262-293).

## What Was NOT the Problem

- The `pick_shot` DTA system is fully implemented in engine code — it just isn't used by any DC3 venue
- `director.milo` loads correctly (DirLoader path normalization fix from 2026-03-22 is working)
- EventTrigger, Flow, and FlowCommand classes all work — venues simply don't contain camera-related instances
- PropAnim::SetFrame correctly iterates kDirEvent keys and fires EventTrigger::Trigger() — there are just no such keys to fire
