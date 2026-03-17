# Session: Song Animation Advancement — Root Cause & Fix

**Date**: 2026-03-17
**Status**: COMPLETE
**Prerequisite**: FileMerger convergence Phase 1-5 (all done 2026-03-17)

## Problem

On native/web, dance animations weren't properly synchronized because `song.anim`
(the master RndPropAnim timeline) was advanced via a wall-clock hack instead of the
normal DTA-driven pipeline. The hack in `HamDirector::Poll()` manually called
`AdvanceFrame()` using `TheTaskMgr.Seconds(kRealTime)` — wall-clock time that drifts
from actual song position.

## Normal Xbox Pipeline

```
WorldDir::Poll()  (Dir.cpp:578)
  → HandleType("select_camera")         ← DTA TypeDef dispatch
    → venue TypeDef "world" type handler routes to $hamdirector
      → HamDirector::OnSelectCamera()   (HamDirector.cpp:2547)
        → BeatToSeconds(beat) * 30.0f   ← beat-synchronized frame
        → songAnim->SetFrame(frame, 1)  ← advances frame + evaluates PropKeys
          → AdvanceFrame()              ← updates internal mFrame
          → PropKeys evaluation         ← camera shots, clips, lighting
```

## Root Cause (Diagnosed)

The venue WorldDir had **no TypeDef at all** (`TypeDef=(nil)`).

Diagnostic output confirmed:
```
WorldDir::Poll: select_camera UNHANDLED — name='WorldDir' type='' TypeDef=(nil)
```

### Why the TypeDef was missing

1. The FileMerger loads venue `.milo` components (buildings, sky, set, etc.) and
   merges objects INTO a pre-existing WorldDir target
2. `MergeDirs()` → `MergeObject()` → `Copy(kCopyFromMax)` — but `kCopyFromMax`
   **skips TypeDef transfer** (Object.cpp:172: `if (ty != kCopyFromMax)`)
3. The venue `.milo` components are RndDirs (not WorldDirs) and have no TypeDef
4. On Xbox, the `world_panel` DTA handler calls `{$world set_type world}` to set
   the TypeDef from `world_objects.dta`. On native, this DTA handler never fires.

### The "world" type in DTA

Found in `orig-assets/extracted/world/world_objects.dta`:
```dta
(WorldDir
   (types
      (world
         ...
         (select_camera
            {if $hamdirector
               {handle ($hamdirector select_camera)}
               {handle ($hamdirector update_freestyle_state)}})
         ...)))
```

The `select_camera` handler checks `$hamdirector` exists, then forwards the message
to it. `$hamdirector` is a DTA variable set in `HamDirector::HamDirector()`.

## Fix Applied

### 1. Set venue WorldDir type in VenueEnter (HamDirector.cpp)

```cpp
void HamDirector::VenueEnter(WorldDir *dir) {
    if (dir) {
#ifdef HX_NATIVE
        if (!dir->TypeDef()) {
            dir->SetType("world");
        }
#endif
        dir->Enter();
    }
```

This sets the TypeDef BEFORE `Enter()` so all DTA handlers work immediately.
The "world" type is defined in `SystemConfig("objects", "WorldDir", "types")`
which is loaded from the ark's `world_objects.dta`.

### 2. Removed AdvanceFrame hack from Poll()

The entire `#if defined(HX_NATIVE) && !defined(MILO_VIEWER)` block that
manually computed frame time and called `AdvanceFrame()` was removed. The real
`OnSelectCamera` path now handles this with beat-synchronized timing.

### 3. Removed difficulty-specific songAnim override from Poll()

The `#ifdef HX_NATIVE` block that forced `SongAnimByDifficulty()` instead of
`SongAnim(0)` was removed. `OnSelectCamera` uses `SongAnim(0)` which already
handles difficulty selection correctly via `merge_moves` property check.

## Hack Problems (now fixed)

| Issue | Detail |
|-------|--------|
| Wrong time source | `Seconds(kRealTime)` = wall clock. `OnSelectCamera` uses `BeatToSeconds(Beat())` = song time. |
| No PropKeys | `AdvanceFrame()` skipped PropKeys. `SetFrame()` evaluates clip/move/practice keyframes. |
| No camera shots | `OnSelectCamera` handles shot selection (lines 2583-2614). |
| Redundancy | Two paths setting frame on same object. |

## Verification

- PPC build: no regressions (same match percentages)
- Native build: compiles clean
- Runtime: `OnSelectCamera` fires successfully, no crashes from PropKeys evaluation
- Runtime: reaches `main_screen` normally, 600+ draw calls/frame, no new errors

## Deeper Investigation: Why DTA Flow Doesn't Set the Type

### The two venue loading paths

**Path A — Full game flow** (Xbox path, works on native when game_screen enters):
```
game_screen → world_panel loads world.milo
  → world.fm (type="world") PreLoad → StartLoadInternal(true, true)
    → change_files → {$hamdirector set merger $this} → wires mMerger
    → load_game_song → OnLoadSong → venue .milo loads via FileMerger
```
This path works correctly on native — confirmed by convergence doc runtime proof.
But it only fires when `game_screen` enters (user navigates to gameplay).

**Path B — App.cpp venue bypass** (native fallback, fires in poll loop):
```
App.cpp:1030-1076 — one-shot venue component loading
  → loads 5 component .milo files (_buildings, _sky, _set, etc.)
  → MergeDirs() into bare WorldDir — no FileMerger, no DTA
  → WorldDir has no TypeDef
```
This path fires earlier (during App poll) for venues that are pre-loaded before
game_screen enters. It creates a bare WorldDir without the "world" TypeDef.

### Why world.fm doesn't fire during headless/automated tests

Runtime confirmed: the screen flow stops at `main_screen` (main menu). Without
user input to select a song and navigate through the menu, `game_screen` never
enters, `world_panel` never loads, and `world.fm` never fires. The App.cpp
fallback loads venue components directly instead.

When `game_screen` DOES enter (scripted input or user interaction), `world_panel`
loads `world.milo`, `world.fm` fires `change_files`, and the full FileMerger
cascade runs. But even in that path, `VenueEnter()` is called before the DTA
`{$world set_type world}` handler fires.

### The fix covers both paths

`SetType("world")` in `VenueEnter()` is correct because:
- **Path B**: The App.cpp bypass creates a typeless WorldDir. `VenueEnter` adds the
  TypeDef before `Enter()` runs.
- **Path A**: Even when world.fm loads correctly, `VenueEnter` ensures the TypeDef
  is set before `Enter()` (the `if (!dir->TypeDef())` guard avoids double-setting).
- The "world" type definition is always available in SystemConfig (loaded from ark's
  `world_objects.dta`).

### Future: Remove App.cpp venue bypass

The App.cpp component-merge code (lines 1030-1076) is a pre-convergence remnant.
Once the full `game_screen` flow is exercised end-to-end, it can be removed. The
`SetType("world")` in `VenueEnter()` would still be needed as a safety net unless
the DTA flow sets it before `VenueEnter` is called.

## Remaining Work

- The `select_camera` DTA handler also calls `{$hamdirector update_freestyle_state}`.
  This is a bonus — freestyle state management now works on native too.
- PropKeys evaluation in `SetFrame()` runs fully — camera shots, clips, lighting,
  EventTriggers all evaluate. If any cause issues in specific venues, individual
  PropKeys can be guarded, but no crashes observed so far.
- The venue TypeDef also provides `enter`, `post_tool_sync`, and other handlers
  that were previously missing on native.
- **App.cpp venue component loading bypass** (lines 1030-1076) should be removed
  once the FileMerger pipeline loads venues end-to-end.

## Key Files Modified

| File | Change |
|------|--------|
| `src/system/hamobj/HamDirector.cpp` | `VenueEnter()`: set type "world" on native; removed AdvanceFrame hack and difficulty override from `Poll()` |
| `src/system/world/Dir.cpp` | (diagnostic only, removed) |
| `src/system/obj/Utl.cpp` | (diagnostic only, removed) |

## WASM Crash Fix: OnSoundPlay MILO_NOTIFY Spam

After enabling `SetFrame` (the real path), PropKeys evaluation triggers Flow nodes
that fire `Sound::Play` on backup dancers without outfits. Each call generates a
`MILO_NOTIFY` (stderr write). On native Linux this is harmless; on WASM the
accumulated `write()` syscalls trap after ~60 seconds.

**Stack**: `AnimTask::Poll → SetFrame → SymbolKeys::SetFrame → SetProperty →
Flow::SyncProperty → FlowSound::Activate → Sound::Play → HamCharacter::Handle
→ OnSoundPlay → MILO_NOTIFY → write() → WASM trap`

**Fix**: Guard the `MILO_NOTIFY` in `OnSoundPlay` with `#ifndef HX_NATIVE`. The
early return (skip lipsync) still fires. The warning is a developer aid for missing
outfits, not a gameplay requirement.

## Key Files for Future Work

| File | Relevance |
|------|-----------|
| `src/App.cpp:1030-1076` | Native venue component bypass — to be removed |
| `orig-assets/extracted/char/char_objects.dta:384-428` | FileMerger "world" type handlers |
| `orig-assets/extracted/world/world_objects.dta:1239-1848` | WorldDir "world" type handlers |
