# Character Animation Freeze — Root Cause Analysis & Fix

**Date**: 2026-03-17 (updated 2026-03-18)
**Status**: FIXED on both native Linux and web — characters now dance during gameplay

## Problem

Characters freeze during gameplay on native. Song animation (song.anim) advances correctly, beat system works, venue loads — but characters stand motionless in their rest pose.

## Root Cause

**Async loading timing mismatch.** On Xbox, `.milo` loading is synchronous — DTA handlers fire after all data is loaded. On native, DirLoader is async, so DTA handlers fire before the merger completes.

The sequence:
1. DTA `reset` fires early (~frame 20) → `OriginalChoreoRemixer::Init()` → `SongInit()`
2. `SongInit()` tries to find difficulty proxies, PropKeys, MoveDir → all null (not merged yet)
3. `#ifdef HX_NATIVE` guards skip initialization, set `mClipPropKeys = nullptr`
4. Merger completes (~frame 1600) → `Initialize()` → `SetupAnims()` sets up anims
5. **Nobody re-runs `SongInit()`** — the PropKeys pointers stay null
6. `SongAnim(0)` returns routine builder anim (empty clip keys, cleared in `SetupRoutineBuilderAnims`)
7. `PlayAnims()` → `PushClip()` finds no keyframes → 0 clips → frozen character

## Fix Applied (Two Parts)

### Part 1: Post-merge deferred initialization

In `HamDirector::Initialize()`, added `#ifdef HX_NATIVE` block that re-runs `SuperEasyRemixer::Init()` after the merger completes. This initializes the MoveGraph, populates PropKeys, and saves per-difficulty move parents.

### Part 2: Bypass routine builder on native

On Xbox, `merge_moves=1` causes `SongAnim(0)` to return the routine builder anim, which is dynamically populated by the DTA-driven choreography system (`FillRoutineFromParents` → `InsertMoveInSong`). This system requires the full synchronous DTA flow that doesn't work with async loading.

On native, `SongAnim()` now skips the routine builder and returns the difficulty-specific song.anim directly. This anim has pre-authored clip keyframes from the song `.milo` files (e.g., easy.milo has 33+ clip keys for YMCA). The clip player reads these keyframes and pushes the correct dance clips.

## Telemetry After Fix

```
frame=1700: clipDir=1 masterClip=1 clipPlayerInit=1 charClipLayers=1
            clipKeyCount=34 songAnimKeys=3 diffProxy=1
```

- `charClipLayers=1` — clips queued to HamDriver, character dancing
- `clipKeyCount` grows over time (34→59) as `InsertMoveInSong` adds keyframes

## General Pattern: Async Loading on Native

When the Xbox code assumes synchronous loading, the native port needs:

1. **`#ifdef HX_NATIVE` null guards** — prevent crashes when init runs before data loads
2. **Post-merge init hook** — `HamDirector::Initialize()` runs when merger completes, re-runs init that failed early
3. **Bypass DTA-driven systems** that depend on sync loading order (routine builder, choreography system)

## Files Modified

- `src/system/hamobj/HamDirector.cpp` — Post-merge Init() + SongAnim() routine builder bypass
- `native/src/telemetry/GameplayTelemetry.cpp` — Added diffProxy, songAnimKeys, clipKeyCount, routineLoaded telemetry
- `native/tests/test_gameplay_telemetry.cpp` — Added DifficultyProxyExists, SongAnimHasPropKeys, SongAnimHasClipKeys, RoutineBuilderLoaded tests
- `src/system/obj/Utl.cpp` — MergeObject diagnostic logging (env-gated: MILO_DEBUG_MERGE=1)
- `src/system/rndobj/PropAnim.cpp` — PropAnim::Copy diagnostic logging (env-gated)
