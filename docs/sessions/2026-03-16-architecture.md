# Architecture: Xbox vs Native World Loading

**Date**: 2026-03-16 / 2026-03-17
**Linked from**: [docs/native/LOADING_ARCHITECTURE.md](../native/LOADING_ARCHITECTURE.md)

## Why This Matters

The native port's most significant architectural divergence from Xbox is how it loads
venue worlds and song content. On Xbox, a centralized **FileMerger** pipeline driven by
DTA scripts loads everything — venue, song, animations, HUD, choreography — into a
single merged ObjectDir. On native, we bypass this entirely and load the venue `.milo`
directly with `DirLoader::LoadObjects()`.

This divergence is the root cause of every null pointer in the MoveGraph/Remixer/scoring
pipeline, and it's the reason ~15 `HX_NATIVE` guards exist in HamDirector alone. It
deserves a clear justification and a path forward.

---

## Xbox: The FileMerger DTA Pipeline

On Xbox, `HamDirector` owns `mMerger` (a `FileMerger`), which orchestrates ALL content
loading through a category-ordered pipeline defined in `config/file_merger_organizer.dta`:

```
category_order:
  song → outfit → viseme → vo_bank
  → female_tempo → female_era → female_tempo_era
  → venue → viz
  → move_graph → charclips → hammoves → transition_charclips
  → hb_vo → venue_audio → game_hud
```

The sequence works like this:

```
GameMode::SetGameplayMode()
  → DTA handlers in game_modes.dta fire
    → HamDirector::OnLoadSong()
      → mMerger->Select("song", songPath)
      → mMerger->StartLoad(async)
    → OnFileLoaded("song") callback
      → mMerger->Select("venue", venuePath)
      → mMerger->Select("viz", vizPath)
      → mMerger->StartLoad()
    → OnFileLoaded("venue") callback
      → mVenue = dynamic_cast<WorldDir*>(loaded dir)
      → OnPopulateMoves()
        → mMoveMerger loads song-specific moves:
            modular_song_data/hammoves/{hamMiloName}.milo
            modular_song_data/charclips/{clipName}.milo
        → Merges into world dir's "moves" MoveDir
```

Everything lands in `mMerger->Dir()` — a single merged ObjectDir containing the venue
world, song animation data, HUD elements, and the MoveDir with MoveGraph. The key
accessor `GetWorld()` returns `mMerger->Dir()`.

When loading completes, `HamDirector::Enter()` runs the full initialization:

```cpp
if (mMerger) {
    // ... state reset ...
    mWorldPostProc = GetWorld()->Find<RndPostProc>("world.pp", true);
    VenueEnter(mVenue);
    Initialize();     // → SetupAnims() → finds mClipDir, mMoveDir
    SyncScene();
    PlayIntroShot();
    TheHamWardrobe->PlayCrowdAnimation("realtime_idle", 2, true);
}
```

`Initialize()` → `SetupAnims()` discovers the MoveDir and CharClips from the merged
world. This is what populates `mMoveDir`, which `Game::PostLoad()` later uses to find
the MoveGraph for the choreography/scoring pipeline.

## Native: Direct DirLoader (Current State)

Native bypasses all of this. In `App.cpp:994-1052`, when `game_screen` is entered:

```
App::Update() detects game_screen
  → DirLoader::LoadObjects("world/{venue}/{venue}.milo")
  → HamDirector::SetNativeVenueWorld(wdir)
  → HamDirector::VenueEnter(wdir)
  → Load component .milos (_buildings, _sky, _set, _chairs, _table_glasses)
  → MergeDirs() components into venue
```

`mMerger` is null. `GetWorld()` falls back to `mVenue` directly. The venue world does
**not** contain:

- Song-specific MoveDir ("moves")
- CharClip animations from the song
- HamMove data from `modular_song_data/hammoves/`
- MoveGraph with choreography layout
- Song animation (`song.anim`) — loaded separately via camera system
- Game HUD elements — loaded separately or skipped

In `HamDirector::Enter()`, native takes the early-return path:

```cpp
#ifdef HX_NATIVE
if (mVenue) {
    // ... state reset, post-proc setup ...
    VenueEnter(mVenue);
    return;  // ← SKIPS Initialize, SetupAnims, SyncScene, PlayIntroShot
}
#endif
```

---

## The Null Pointer Dependency Chain

Every null pointer in the MoveGraph/Remixer/scoring pipeline traces back to the missing
FileMerger content:

```
mMerger (null on native)
  → GetWorld() returns mVenue (no merged content)
    → Find<MoveDir>("moves") → NULL (moves.milo never loaded)
      → mMoveDir = null
        → mUseMoveGraph = false
          → TheMoveMgr->LoadMoveData() never called
            → MoveParents empty
              → OriginalChoreoRemixer::Init() → MILO_FAIL
                → mTotalMeasures = 0 (was garbage before Session 74 fix)
                  → DanceRemixer::Init(0) → no choreography
```

This chain means autoplay, flashcards, phrase meters, scoring, and dynamic difficulty
are all dead on native.

---

## Why We Diverged

The native port was built incrementally from Session 1 (basic rendering) through Session
59 (first venue rendering). At each stage, the goal was to get the next visual milestone
working with minimal code changes. The FileMerger pipeline was bypassed for three
concrete reasons:

### 1. MoveMgr::Init() Crashes

The full DTA pipeline fires `GameMode::SetGameplayMode()`, which eventually calls
`MoveMgr::Init()`. This crashes because `SuperEasyRemixer`'s `OBJ_CLASSNAME` /
`SetType` registration has a mismatch on native — the Milo engine's type system
initialization order differs. This was the original blocker (Session 72, commit
`7d68adb98`).

Rather than fix a deep engine-level type registration bug to unblock venue rendering,
the direct `DirLoader` approach got venues on screen immediately.

### 2. Sync Loading Hangs on Web

`FileMerger::StartLoadInternal(async=false)` uses a tight polling loop:

```cpp
while (!IsDone()) {
    TheLoadMgr.Poll();
}
```

On Xbox, this always terminates — file loads complete or assert. On web (Emscripten),
sync XHR blocks the main thread. If any loader stalls (missing file, network timeout),
this loop hangs forever. This caused the `DataWhile` infinite loop in
`OriginalChoreoRemixer::Init()` that was the trigger for this investigation.

A 100k-iteration safety timeout was added (commit `5bdffe302`), but the fundamental
issue remains: sync polling is hostile to single-threaded environments.

### 3. DTA Pipeline Expects Xbox-Only Globals

The full pipeline assumes working instances of:
- `TheMoveMgr` — move/choreography manager (crashes on init)
- `TheGameMode` — full DTA property evaluation (guarded on native)
- `TheHamWardrobe` — character outfit resolution from DTA config
- `TheSkeletonIdentifier` / `ThePassiveMessenger` — Kinect subsystems
- Xbox Live analytics (`RockCentral::SendDropInDatapoint`)

Each of these requires either a working implementation or careful stubbing. The direct
loading approach sidesteps all of them.

### Historical Justification

The decision was pragmatic: get rendering working *now*, defer pipeline integration to
later. This was the right call for Sessions 59-74, which needed to prove that the engine
could render venues, characters, crowds, HUD, audio, and post-processing. All of that
works.

But the debt has accumulated. The native port now has ~15 `HX_NATIVE` guards in
HamDirector alone, plus downstream guards in Game.cpp, GamePanel.cpp, and the
Remixer/MoveMgr layer. Each new feature that depends on merged content needs its own
explicit load path.

---

## What's Actually Broken vs Working

| Component | Status | Root Cause |
|-----------|--------|------------|
| Venue world rendering | **Working** | Direct DirLoader + component merges |
| Crowd animation | **Working** | CharClipGroup null purge + FastInt fix |
| Camera cuts | **Working** | song.anim loaded separately via camera system |
| Post-processing | **Working** | world.pp found in venue world |
| Audio playback | **Working** | MOGG decoder independent of merger |
| Character animation | **Working** | ClipPlayer → HamDriver independent |
| MoveGraph loading | **Broken** | moves.milo never merged into world |
| Choreography / dynamic difficulty | **Broken** | Depends on MoveGraph |
| Autoplay / flashcards | **Broken** | Depends on MoveGraph |
| Phrase meters / scoring | **Broken** | Depends on MoveGraph |
| HamDirector Initialize/SetupAnims | **Skipped** | Early return in Enter() |
| SyncScene / PlayIntroShot | **Skipped** | Early return in Enter() |

---

## Path Forward: Option Analysis

Options A-C were the initial analysis. Option D supersedes them — see
[docs/native/FILEMERGER_CONVERGENCE.md](../native/FILEMERGER_CONVERGENCE.md) for the
full implementation plan.

### Option A: Replicate the Full Xbox FileMerger Pipeline

Create a FileMerger from scratch, configure DTA categories, replicate the callback chain.

| Pro | Con |
|-----|-----|
| Removes ~60 HX_NATIVE guards | Must create FileMerger from scratch |
| Everything works automatically | Sync polling hostile to web/Emscripten |
| Single code path | DTA callback chain replication is complex |

### Option B: Keep Direct Loading, Add Missing Content Explicitly

After venue loads, explicitly load song content files and merge them in.

| Pro | Con |
|-----|-----|
| Minimal changes | Adds MORE HX_NATIVE guards |
| Incremental | Must manually replicate DTA pipeline logic |

### Option C: Hybrid — Direct Venue + Explicit Song Content Merge

Keep venue direct-load, add song content merge replicating `OnPopulateMoves()`.

| Pro | Con |
|-----|-----|
| Venue loading stays proven | Still need to know which files to load |
| MoveGraph pipeline works | Partial divergence remains |

### Option D: Enable the Existing Engine, Force Async (Selected)

**Key discovery**: The venue `.milo` files already contain a FileMerger object
(`world.fm`) with pre-configured merger categories. The decomped code in `Game.cpp:702`
and `Game.cpp:980` already uses it. The native `GamePanel::PollForLoading()` hack at
line 1049 is already finding and using `world.fm` — just with sync loading and static
bools instead of letting the engine drive it.

**The fix**: Wire `mMerger = world.fm` (1 line), force async in `StartLoadInternal`
(1 line), let the existing DTA handlers drive loading. The async path uses cooperative
polling via `TheFileMergerOrganizer`, which is already initialized on native
(`CharInit() → FileMergerOrganizer::Init()`). Web-safe because no sync blocking.

| Pro | Con |
|-----|-----|
| Uses decomped engine as-is | MoveGraph deserialization must be fixed |
| `world.fm` already exists in venue .milo | Need to verify merger categories |
| Async by default — web-safe | Some DTA handlers expect Xbox globals |
| Removes ~60 guards | |
| DTA scripts drive loading — no manual file paths | |
| `Game::IsLoaded()` already waits for `world.fm` | |

**Full plan**: [docs/native/FILEMERGER_CONVERGENCE.md](../native/FILEMERGER_CONVERGENCE.md)

---

## Concrete Next Steps (Option D)

1. **Dump `world.fm` merger categories** — verify it has "song", "venue", etc.
2. **Wire `mMerger = world.fm`** after venue load
3. **Force async** in `FileMerger::StartLoadInternal()`
4. **Fix `SetupAnims()`** — use `GetWorld()` instead of `mMerger->Dir()` (2-line change)
5. **Fix MoveGraph deserialization** — endian-aware Load() for MoveVariant/MoveCandidate
6. **Remove `Enter()` early return** — let full init path run
7. **Remove ~60 native loading hacks** incrementally

---

## HX_NATIVE Guards in HamDirector (Reference)

| Line | Location | Purpose | Removable? |
|------|----------|---------|------------|
| 337-386 | `Enter()` | Skip full merger init, run venue-only path | Yes, with Option C |
| 591-593 | `GetWorld()` | Fall back to mVenue when mMerger null | Yes, with Option C |
| 756-759 | `FindNextDircut()` | Skip crowd camera (no dircut data) | Maybe, with merged content |
| 1045-1052 | `OnLoadSong()` | Debug logging | Keep (diagnostic) |
| 1060-1078 | `OnLoadSong()` | Single-player crew/outfit fixup | Keep (real platform diff) |
| 1232-1239 | `OnFileLoaded()` | Debug logging | Keep (diagnostic) |
| 1281-1296 | `OnFileLoaded()` | video_recorder.srec stub | Maybe, with merged content |
| 1951 | `OnPopulateMoves()` | Not yet investigated | TBD |
| 2355-2377 | Various | Not yet investigated | TBD |
| 2531, 2606 | Various | Not yet investigated | TBD |
