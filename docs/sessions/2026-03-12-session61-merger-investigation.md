# Session 61: FileMerger Pipeline Investigation

**Date**: 2026-03-12
**Goal**: Fix venue visibility regression after LightPreset stub removal; understand why FileMerger pipeline doesn't wire up in native.

## Summary

After removing 12 LightPreset stubs from `engine_stubs_generated.cpp` (session 59), draw calls on `game_screen` dropped from 505→78. This session traced the root cause through the entire rendering/loading chain.

## Key Findings

### 1. The 505→78 Drop is NOT a Bug

With stub `LightPreset::Load` (return 0), BinStream was desynced for all subsequent objects in venue `.milo` files. Objects loaded with garbage data, accidentally getting `Showing=true`. With real `LightPreset::Load`, the stream stays synchronized and objects load with their correct serialized state — many venue meshes have `Showing=false` by design.

### 2. Venue Isn't Loaded into game_screen

The 505 draw calls on attract_screen come from attract_screen's **own built-in world content**, not from a venue. On `game_screen`, `world_panel` loads `world.milo` which is an **empty container** WorldDir. The venue is supposed to be merged INTO this container via `FileMerger`, but the merger is never activated.

### 3. FileMerger Pipeline is Fully Implemented

All components exist and compile — nothing is stubbed:
- `FileMerger` — fully implemented with Select/StartLoad/LaunchNextLoader/FinishLoading
- `FileMergerOrganizer` — fully implemented, orchestrates async loading
- `DirLoader` — fully implemented, loads .milo files
- `HamDirector::OnLoadSong` — fully implemented, calls `mMerger->Select("venue", path)`

### 4. mMerger is Null — The Root Cause

`HamDirector::mMerger` is an `ObjPtr<FileMerger>` that starts null. It's a DTA property (`SYNC_PROP(merger, mMerger)`) — NOT binary-serialized in `HamDirector::Load`.

**The merger is set by the embedded DTA inside `director.milo_xbox`** (a subdir of `world.milo`). When this subdir loads, it creates a HamDirector and FileMerger objects, then runs embedded init scripts that wire `merger → <FileMerger object>`.

**Diagnostic findings:**
- HamDirector IS constructed from `director.milo_xbox` during world.milo load
- 7 FileMerger objects ARE constructed
- HamDirector's TypeProps are EMPTY (no inline properties for `merger`)
- The `merger` SyncProperty set callback NEVER fires
- Therefore: the embedded DTA init script in `director.milo_xbox` either fails silently or never executes

### 5. The Full Loading Chain (Original Game)

```
MetaPerformer → Game::LoadSong() → HamDirector::OnLoadSong()
  → mMerger->Select("song", songPath, true)      // queue song file
  → mMerger->Select("venue", venuePath, false)    // queue venue file
  → mMerger->StartLoad(async)                     // kick off loading
    → FileMerger loads venue .milo_xbox
    → Objects merge into world.milo's dir
    → OnFileLoaded() → mVenue = dynamic_cast<WorldDir*>(dir)
    → Song animation starts → force_preset → LightPresetManager
    → LightPreset shows/hides venue objects
```

### 6. What's Actually Blocking

1. **mMerger is never wired** — embedded DTA in director.milo_xbox doesn't execute or fails
2. **Without mMerger**, `OnLoadSong` can't queue files
3. **Without venue merge**, world_panel has no venue content
4. **Without song animations**, LightPresetManager never activates presets
5. **Without active presets**, venue objects stay at their serialized `Showing=false`

## Changes Made This Session

### Kept
- **DataNode::GetObj graceful failure** (`src/system/obj/DataNode.cpp`): Under `#ifdef HX_NATIVE`, missing objects log a warning instead of `MILO_FAIL_DTA` crash. This unblocks song.anim execution for when the merger eventually works.

### Reverted (wrong approach)
- All brute-force `SetShowing(true)` hacks in HamDirector::Poll
- LightPresetManager::HasActivePreset() accessor
- WorldDir::Enter auto-preset activation
- All diagnostic fprintf code in HamDirector, FileMerger, Object

### Pre-existing issues reverted
- `src/system/obj/Task.cpp` — ScriptTask::UpdateVarsObjects had compilation errors (sMainDir access, wrong arg counts)
- `src/system/synth/SampleData.cpp` — compilation errors
- `src/system/synth/StandardStream.cpp` — PushData missing

## Next Steps: Fixing the Merger

### Option A: Fix embedded DTA execution (correct approach)
Debug why `director.milo_xbox`'s embedded DTA init doesn't wire the `merger` property. This requires:
1. Adding diagnostics to `ObjectDir::PostLoad` or `DirLoader::LoadObjs` to trace DTA init execution
2. Checking if `director.milo_xbox`'s objects are even being loaded (subdirs might be skipped)
3. Verifying the FileMerger objects inside director.milo_xbox get proper names

### Option B: Manual merger setup (pragmatic workaround)
In HamDirector's native-only Enter/Poll code:
1. Find a FileMerger object in the dir hierarchy by iterating: `ObjDirItr<FileMerger>`
2. If found, assign it to `mMerger`
3. Then call `OnLoadSong` manually with the venue path

### Option C: Direct venue load (bypass merger)
Skip the merger entirely. In native code:
1. Create a DirLoader pointing at a venue .milo_xbox file
2. Load it directly into world_panel's WorldDir
3. Force the first LightPreset
This is the simplest approach but doesn't exercise the full engine.

## Files Modified
| File | Change |
|------|--------|
| `src/system/obj/DataNode.cpp` | GetObj graceful null return under HX_NATIVE |
| `native/src/engine_stubs_generated.cpp` | 12 LightPreset stubs removed (from session 59) |
| `native/CMakeLists.txt` | `--allow-multiple-definition` for dc3-native |
| `src/system/ui/UIScreen.cpp` | Local DebugUIFlow() for HX_NATIVE |
| `src/system/world/Dir.h` | GetLightPresetMgr() getter under HX_NATIVE |
