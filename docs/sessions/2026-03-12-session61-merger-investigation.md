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

**The merger is wired by DTA type handlers** in `char_objects.dta`. Each FileMerger has a type ("world", "game_mode", "modular") with a `change_files` handler that does `{$hamdirector set merger $this}`. This handler fires from `FileMerger::StartLoadInternal()`, which is called from `FileMerger::PreLoad()`.

**Root cause found:** `PreLoad` had `#ifndef HX_NATIVE` guarding the `StartLoadInternal(true, true)` call. This prevented the `change_files` message from firing on native, so the merger was never wired.

**Fix applied:** Fire the `change_files` message directly on native (without the full `StartLoadInternal` which would try to load Xbox asset paths). The DTA type handlers then wire all three mergers (`merger`, `move_merger`, `game_mode_merger`) on the HamDirector.

**Diagnostic findings (prior to fix):**
- HamDirector IS constructed from `director.milo_xbox` during world.milo load
- 7 FileMerger objects ARE constructed
- HamDirector's TypeProps are EMPTY — merger is NOT set via TypeProps
- The `merger` SyncProperty set callback NEVER fired because `change_files` was suppressed

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

## Fix Applied

### Phase 1: change_files only (partially correct)

Initial fix fired only the `change_files` DTA message on native, skipping the full `StartLoadInternal`. This successfully wired all three mergers (mMerger, mMoveMerger, mGameModeMerger) via the DTA type handlers, but the venue didn't render (0 draw calls on game_screen).

### Phase 2: Full StartLoadInternal (correct fix)

**Root cause:** `FileMerger::PreLoad` had `#ifndef HX_NATIVE` (added commit `72538161a`, 2026-03-02) suppressing `StartLoadInternal(true, true)`. This was added during early native port bring-up when the ark file system wasn't resolving Xbox paths. Once the ark was working, the guard was stale.

**Why change_files alone wasn't enough:** `StartLoadInternal` does TWO things:
1. Fires `change_files` → DTA handler wires `$hamdirector set merger $this` AND calls `$hamdirector load_game_song FALSE` (which selects the song file via `mMerger->Select("song", ...)` but does NOT start loading because `$load=FALSE`)
2. **Iterates all mergers checking `NeedsLoading()`** → the song file just selected in step 1 is found, `AppendLoader` creates a DirLoader, and `TheFileMergerOrganizer->AddFileMerger(this)` kicks off async loading

Without step 2, the song was selected but never loaded. The entire chain (song load → `OnFileLoaded("song")` → select venue/viz → load venue → `OnFileLoaded("venue")` → set mVenue) never fired.

**Fix:** Removed the `#ifdef HX_NATIVE` guard entirely. `StartLoadInternal(true, true)` runs unconditionally:
```cpp
    d >> mMergers;
    // StartLoadInternal fires change_files (which lets DTA type handlers
    // wire merger properties, e.g. {$hamdirector set merger $this}),
    // then iterates mergers to start loading any files that were selected
    // during change_files (e.g. the song .milo queued by load_game_song).
    StartLoadInternal(true, true);
```

### The loading chain (now working end-to-end)

```
FileMerger::PreLoad → StartLoadInternal(true, true)
  → change_files DTA handler:
    → {$hamdirector set merger $this}        // wire merger
    → {$hamdirector load_game_song FALSE}    // select song (if game_panel exists)
  → NeedsLoading("song") = true → AppendLoader → async load
    → song .milo loads → on_pre_merge → {$hamdirector on_file_loaded song ...}
    → OnFileLoaded("song"):
      → TheHamWardrobe->LoadCharacters(...)
      → mMerger->Select("viz", "ui/visualizer/visualizer.milo")
      → mMerger->Select("venue", "world/rollerrink/rollerrink.milo")
      → mMerger->StartLoad(async)
        → venue .milo loads → OnFileLoaded("venue") → mVenue = WorldDir
        → viz .milo loads → OnFileLoaded("viz") → mVisualizer = HamVisDir
        → game_hud .milo loads → OnFileLoaded("game_hud")
```

**Result:** 415 mesh draw calls on game_screen. Roller rink venue (YMCA's assigned venue) renders with dance floor, railing, arcade machines, neon signs, and character model.

### Additional fixes
- **SampleData::Load** — ChunkStream can't seek; added read-and-discard fallback under `HX_NATIVE`
- **CamShotFrame::Interp** — `blendT` undeclared; added `float blendT = f1;`
- **Geo.cpp BSPFace::Update** — `next` variable scoped inside do-while but used in while condition

## Next Steps

1. **LightPreset activation** — Venue objects load with correct serialized `Showing` state, but song-time LightPresets that dynamically show/hide venue elements need the song animation pipeline
2. **Character materials** — Character model renders but needs proper material/texture setup
3. **TexMovie/HUD** — Move cards and HUD elements need render-to-texture support
4. **Song-time animation** — Beat-synced venue/character animations (kTaskSeconds pipeline)

## Files Modified
| File | Change |
|------|--------|
| `src/system/char/FileMerger.cpp` | Removed `#ifndef HX_NATIVE` guard — `StartLoadInternal(true,true)` runs unconditionally |
| `src/system/synth/SampleData.cpp` | Read-and-discard for cached audio on ChunkStream (no seek support) |
| `src/system/world/CameraShot.cpp` | Declare `blendT` variable (was undeclared) |
| `src/system/math/Geo.cpp` | Fix `next` variable scope in BSPFace::Update do-while |
| `src/system/obj/DataNode.cpp` | GetObj graceful null return under HX_NATIVE (prior session) |
| `src/system/hamobj/HamDirector.cpp` | Removed diagnostic fprintf statements |

## Screenshots

`archive/screenshots/2026-03-12-venue-rendering/`
- `frame_00500.png` — choose_mode_screen (main menu)
- `frame_01000.png` — song_select_screen (browsing songs)
- `frame_01500.png` — game_screen with roller rink venue, character, 415 draw calls
