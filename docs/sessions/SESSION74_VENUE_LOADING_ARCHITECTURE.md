# Session 74: Venue/World Loading Architecture & MoveGraph Pipeline

**Date**: 2026-03-17
**Context**: Web hang investigation revealed `OriginalChoreoRemixer::Init()` DataWhile infinite loop caused by uninitialized `mTotalMeasures`. Root cause traced to a blanket `#ifdef HX_NATIVE return;` that skipped the entire MoveGraph pipeline. Fixing that exposed the question: why doesn't the MoveGraph pipeline work on native?

## The Web Hang (Fixed)

`DataWhile()` in `num_rated_measures` (perform.dta:1507) loops `while {<= $measure {[remixer] measures_total}}`. `measures_total` returns `mTotalMeasures`, which was uninitialized garbage because `OriginalChoreoRemixer::Init()` early-returned on native before calling `DanceRemixer::Init()`.

**Fix**: Removed blanket early return. Pipeline now tries to run for real. `MILO_FAIL` fires if `MoveParents` is empty, then `mTotalMeasures = 0` prevents the infinite loop.

## Architecture: Xbox vs Native World Loading

### Xbox: FileMerger DTA Pipeline

Xbox uses a **centralized FileMerger pipeline** driven by DTA scripts. HamDirector owns `mMerger` (FileMerger) which orchestrates ALL content loading:

```
HamDirector::OnLoadSong()
  → mMerger->Select("song", songPath, true)
  → mMerger->StartLoad(async)
  → [DTA handlers in game_modes.dta fire for each category]
  → OnFileLoaded("song") callback
    → mMerger->Select("venue", venuePath, false)
    → mMerger->Select("viz", vizPath, false)
    → mMerger->StartLoad()
  → OnFileLoaded("venue") callback
    → mVenue = dynamic_cast<WorldDir*>(loaded dir)
    → OnPopulateMoves() fires
      → mMoveMerger loads song-specific moves from:
        modular_song_data/hammoves/{hamMiloName}.milo
        modular_song_data/charclips/{clipName}.milo
      → Merges into world dir's "moves" MoveDir
```

**Key**: Everything lands in `mMerger->Dir()` — a single merged ObjectDir containing the venue world, song data, HUD elements, and MoveDir with MoveGraph. `GetWorld()` returns `mMerger->Dir()`.

Load order is defined in `config/file_merger_organizer.dta`:
```
(category_order
  (song) (outfit) (viseme) (vo_bank)
  (female_tempo) (female_era) (female_tempo_era)
  (venue) (viz) ... (hammoves)
)
```

### Native: Direct DirLoader (Bypasses Pipeline)

Native loads the venue world directly, bypassing the entire FileMerger pipeline:

```
App::Update() — game_screen entered
  → DirLoader::LoadObjects("world/{venue}/{venue}.milo")
  → TheHamDirector->SetNativeVenueWorld(wdir)
  → TheHamDirector->VenueEnter(wdir)
  → Merge component .milos (_buildings, _sky, _set, _chairs, _table_glasses)
```

**Key difference**: `mMerger` is null. `GetWorld()` returns `mVenue` directly. The venue world does NOT contain:
- Song-specific MoveDir ("moves")
- CharClip animations from the song
- HamMove data from `modular_song_data/hammoves/`
- MoveGraph with choreography layout

This is why `Game::PostLoad()` finds `world->Find<MoveDir>("moves")` → null → `mUseMoveGraph = false` → entire choreography pipeline disabled.

### HamDirector::Enter() Divergence

Xbox:
```cpp
if (mMerger) {
    mWorldPostProc = GetWorld()->Find<RndPostProc>("world.pp", true);
    VenueEnter(mVenue);
    Initialize();     // Sets up anims, clips, moves
    SetupAnims();     // Finds mClipDir, mMoveDir in merged world
    SyncScene();
    PlayIntroShot();
}
```

Native:
```cpp
if (mVenue) {
    mWorldPostProc = GetWorld()->Find<RndPostProc>("world.pp", true);
    VenueEnter(mVenue);
    return;  // ← SKIPS Initialize, SetupAnims, SyncScene, PlayIntroShot
}
```

## The Null Pointer Dependency Chain

```
mMerger (null on native)
  → GetWorld() returns mVenue (no merged content)
    → Find<MoveDir>("moves") → NULL (moves.milo never loaded)
      → mMoveDir = null
        → mUseMoveGraph = false
          → TheMoveMgr->LoadMoveData() never called
            → MoveParents empty
              → OriginalChoreoRemixer::Init() → MILO_FAIL
                → mTotalMeasures = 0 (was garbage before fix)
```

Every null pointer in the MoveGraph/Remixer pipeline traces back to: **native doesn't use the FileMerger pipeline, so song-specific content (moves, clips) is never merged into the world**.

## Decision Point: Replicate vs Diverge

### Option A: Replicate the Xbox FileMerger Pipeline on Native

**What it would take**:
1. Initialize `mMerger` in HamDirector on native
2. Configure it with the same DTA categories as Xbox
3. Load song/venue/moves via the merger pipeline instead of direct DirLoader
4. Remove all `if (!mMerger) return mVenue` fallbacks
5. Remove the early return in `HamDirector::Enter()`
6. Let the full DTA pipeline (`game_modes.dta`, `perform.dta`) drive loading

**Pros**:
- Removes ~20 `HX_NATIVE` guards in HamDirector alone
- MoveGraph, choreography, dynamic difficulty all work automatically
- DTA scripts work identically to Xbox (no missing objects)
- Single code path = fewer bugs

**Cons**:
- FileMerger pipeline assumes sync-capable I/O (blocks in while loops)
- On web/Emscripten, sync XHR blocks the main thread (already caused hangs)
- DTA pipeline expects Kinect, NUI, Xbox-specific objects that don't exist
- Significant integration work to get the full pipeline running

### Option B: Keep Direct Loading, Add Missing Content

**What it would take**:
1. After venue loads, explicitly load `modular_song_data/hammoves/*.milo` files
2. Create a MoveDir in the venue world and populate it
3. Load `move_data.milo` into the MoveDir
4. Call `TheMoveMgr->LoadMoveData()` explicitly

**Pros**:
- Minimal changes to existing native loading
- Can be done incrementally
- Avoids FileMerger sync loading issues on web

**Cons**:
- Adds more `HX_NATIVE` guards, not fewer
- Must manually replicate what the DTA pipeline does automatically
- Risk of getting the load order wrong
- Each new feature that depends on merged content needs its own explicit load

### Option C: Hybrid — Use FileMerger for Song Content Only

**What it would take**:
1. Keep direct venue loading (works well)
2. After venue loads, use FileMerger (or manual DirLoader) for song-specific content:
   - `{song}.milo` → song animation data
   - `moves.milo` / `move_data.milo` → MoveGraph + MoveDir
   - `charclips/*.milo` → animation clips
3. Merge song content into the venue world (just like Xbox does via OnPopulateMoves)

**Pros**:
- Venue loading stays fast and simple
- Song content gets the same merge treatment as Xbox
- MoveGraph pipeline works naturally
- Fewer guards than Option B

**Cons**:
- Still need to understand which song files to load (currently driven by DTA)
- Need to handle async loading for web

## Current State of Guards (HACK_AUDIT.md)

See `docs/native/HACK_AUDIT.md` for the full audit of 31 MoveGraph/Remixer guards. Key stats:
- 9 crash-masking null checks that should be fixed at the source
- 3 post-MILO_FAIL returns that should be unconditional
- 12 real platform divergences (keep)
- 5 debug-only guards (low priority cleanup)

## What's Actually Broken vs Working

| Component | Status | Why |
|-----------|--------|-----|
| Venue world rendering | Working | Direct DirLoader + component merges |
| Crowd animation | Working (Session 73) | CharClipGroup null purge + FastInt fix |
| MoveGraph loading | Broken | moves.milo never loaded into world |
| Choreography / dynamic difficulty | Broken | Depends on MoveGraph |
| `mTotalMeasures` | Fixed (this session) | Was garbage, now 0 on init failure |
| Song animation timing | Partially working | Wall-clock fallback; no SetSecondsAndBeat |
| HamDirector Enter pipeline | Partially working | Skips Initialize/SetupAnims/SyncScene |

## Next Steps

1. **Decide on loading strategy** (Options A/B/C above)
2. If Option B/C: identify exactly which `.milo` files need loading for a given song
3. Test whether `MoveGraph::CacheLinks()` works on native (it has one null guard in `MoveCandidate::CacheLinks`)
4. Once MoveGraph loads, the 9 null-check guards in the Remixer pipeline should become unnecessary
5. Re-run the web build to verify the DataWhile hang is fixed
