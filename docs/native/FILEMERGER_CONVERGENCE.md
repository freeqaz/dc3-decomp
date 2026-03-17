# FileMerger Convergence Plan (Revised)

**Date**: 2026-03-17 (updated 2026-03-17)
**Status**: Phase 1–4 complete (2026-03-17) — runtime verified on glitterati + dclive
**Context**: [LOADING_ARCHITECTURE.md](LOADING_ARCHITECTURE.md) documents the divergence. This doc is the fix.
**Review**: [2026-03-17-convergence-review.md](../sessions/2026-03-17-convergence-review.md)

## Summary

The native port bypasses the Xbox FileMerger loading pipeline, hand-rolling venue and
song loading in `App.cpp` and `GamePanel.cpp`. This causes all null pointers in the
MoveGraph/choreography/scoring pipeline and accounts for ~60 `HX_NATIVE` guards.

**The fix**: the engine's FileMerger pipeline **already works on native**. `world_panel`
loads `world.milo`, `world.fm` fires `change_files`, and `mMerger` gets wired — all
automatically. The native hacks just bypass it. We need to remove the bypasses and let
the engine do its job.

## Architecture (Empirically Verified 2026-03-17)

### Where world.fm lives

`world.fm` is NOT in the venue .milo. It's in `world/gen/world.milo_xbox` (3.3KB
skeleton ObjectDir), loaded by `world_panel`:

```dta
;; ui/game.dta
{new UIPanel world_panel
   (file "../world/world.milo")
   (unload_async TRUE)}
```

`world_panel` is part of `game_screen`'s panel list. On both Xbox and native, when
`game_screen` enters, `world_panel` loads `world.milo`.

### FileMerger topology (runtime diagnostic)

```
world/world.milo (3.3KB skeleton ObjectDir)
  ├── world.fm (type "world", 3 mergers: song, viz, venue)
  ├── modular.fm (type "modular", 0 mergers — populated dynamically)
  └── GameModeMerger.fm (type "game_mode", 1 merger: game_hud)

venue .milo (e.g., glitterati.milo)
  └── crowd_clips.fm (type "crowd_anim", 6 mergers: male/female × tempo/era/tempo_era)
```

### How mMerger gets wired (verified on native)

```
world_panel loads world.milo
  → world.fm::PreLoad fires
    → StartLoadInternal(true, true)
      → HandleType("change_files")
        → DTA handler (char_objects.dta "world" type):
          {$hamdirector set merger $this}     ← wires mMerger = world.fm
          {$hamdirector load_game_song FALSE} ← triggers song loading cascade
```

**Runtime proof**:
```
DC3 DIAG FM::PreLoad 'world.fm' type='world' mergers=3
DC3 DIAG FM::PreLoad   [0] 'song'
DC3 DIAG FM::PreLoad   [1] 'viz'
DC3 DIAG FM::PreLoad   [2] 'venue'
DC3 DIAG FM::StartLoadInternal after change_files: mMerger=0x55c6c6ce7b30  ← WIRED
```

Similarly:
- `modular.fm` change_files → `{$hamdirector set move_merger $this}` → wires mMoveMerger
- `GameModeMerger.fm` change_files → `{$hamdirector set game_mode_merger $this}` → wires mGameModeMerger

### Loading cascade (Xbox flow, works on native once unblocked)

```
world_panel loads world.milo
  → world.fm::PreLoad → change_files → mMerger wired
  → DTA: load_game_song → OnLoadSong
    → mMerger->Select("song", songPath) → StartLoad(async)
    → Song .milo merges into world ObjectDir
    → OnFileLoaded("song")
      → TheHamWardrobe->LoadCharacters(...)
      → mMerger->Select("viz", "ui/visualizer/visualizer.milo")
      → mMerger->Select("venue", "world/<venue>/<venue>.milo")
      → mGameModeMerger->StartLoad() → loads HUD
      → mMerger->StartLoad() → loads venue + viz
    → OnFileLoaded("venue")
      → mVenue = dynamic_cast<WorldDir*>(dir)
    → IsWorldLoaded() returns true
      → GamePanel::PollForLoading proceeds to Game::IsReady
```

### Native hacks removed (Phase 1–4)

| Hack | Removed in | Replacement |
|------|-----------|-------------|
| `App.cpp:994-1052` — DirLoader venue loading | Phase 2 | world.fm venue merger cascade |
| `GamePanel.cpp:1022-1026` — Skip `IsWorldLoaded()` wait | Phase 1 | Engine waits for FileMerger cascade |
| `GamePanel.cpp:1039-1063` — Sync-load song via world.fm hack | Phase 1 | DTA async cascade |
| `GamePanel.cpp:1066-1121` — Manual phrase_meter/stub merge | Phase 1 | game_hud FileMerger |
| `GamePanel.cpp:709-752` — StartIntro native block | Phase 3 | Game::IsLoaded + HandleWait pipeline |
| `Game.cpp:779-785` — IsWorldLoaded bypass | Phase 3 | PollForLoading gates on it first |
| `Game.cpp:820-826` — IsMoveMergerFinished bypass | Phase 3 | PollForLoading gates on it first |
| `HamDirector.cpp:337-386` — Enter() early return | Phase 2 | Full Initialize/SetupAnims path runs |
| `HamDirector.cpp:591-593` — GetWorld() mVenue fallback | Phase 2 | mMerger wired via change_files |
| `HamDirector.h:121` — SetNativeVenueWorld() | Phase 4 | No callers (dead code) |
| `HamDirector.cpp:74-87` — DebugWorldLoad() | Phase 4 | Logging made unconditional |
| `GamePanel.cpp:63-70` — DebugWorldLoad() | Phase 4 | Unused (dead code) |
| `Song.cpp:423/533` — SyncState `#ifndef HX_NATIVE` guard | Phase 3 | Unguarded; sync-wait loop has native guard |
| `engine_stubs_generated.cpp:659-661` — SyncState stub | Phase 3 | Real implementation runs |

### Song::SyncState fix (Phase 3)

`Song::SyncState()` was previously guarded with `#ifndef HX_NATIVE` and stubbed in
`engine_stubs_generated.cpp`. This function initializes MidiParser/LightPreset/Camera
state when seeking to a song position — without it, lights and cameras don't get their
initial state set at song start.

The blocker was a sync-wait loop at lines 514-522 that spins until `HxAudio::IsReady()`:
```cpp
while (true) {
    if (a2->IsReady()) break;
    TheSynth->Poll();
    a2->Poll();
}
```

This hangs on web (can't block main thread) and risks hanging on native. Fix: the
`#ifndef HX_NATIVE` guard was removed so SyncState runs on all platforms, and the
sync-wait loop was wrapped in `#ifdef HX_NATIVE` so it's skipped on native/web. Audio
continues loading asynchronously; callers poll again if state is still dirty.

### Hacks that remain (architectural necessities)

| Hack | Why it stays |
|------|-------------|
| `Dir.cpp:645-648` — gNativeVenueDir from AddedSubDir | Menu/title screen venue (no FileMerger) |

## Implementation Steps

### Step 1: Force async loading on native

In `FileMerger::StartLoadInternal()` (`FileMerger.cpp:431`):

```cpp
#ifdef HX_NATIVE
async = true;  // Never sync-poll on native/web — use cooperative polling
#endif
```

This prevents the sync busy-wait hang on web and lets the async polling chain work:
`App::Update() → TheLoadMgr.Poll() → FileMergerOrganizer::Poll() → process loaders`.

### Step 2: Remove GamePanel::PollForLoading native hacks

Remove the `#ifdef HX_NATIVE` blocks at:
- Lines 1022-1026: Restore `IsWorldLoaded()` wait (remove the empty native block)
- Lines 1032-1036: Restore character loading wait
- Lines 1039-1121: Remove manual song/HUD merge hacks

With async loading and the DTA cascade, `PollForLoading` should naturally wait for
`IsWorldLoaded()` to return true before proceeding.

**Caution**: `IsWorldLoaded()` (line 1701) requires `mMoveMerger` to be non-null:
```cpp
return mVenue && mMerger && !mMerger->HasPendingFiles()
    && mMoveMerger && !mMoveMerger->HasPendingFiles();
```
If `modular.fm` hasn't loaded yet when this is checked, it returns false. This should
be fine — the DTA cascade loads everything before `IsWorldLoaded` is first checked.
But verify that `modular.fm` does fire its `change_files` before the check.

### Step 3: Remove App.cpp direct venue loading

Remove `App.cpp:994-1052` (the DirLoader path). The venue is loaded by world.fm's
"venue" merger category during the `OnFileLoaded("song")` cascade.

**Note**: The component loading at `App.cpp:1095-1128` (buildings, sky, set, chairs,
table_glasses) may still be needed if these are not part of the venue .milo. Check
whether the merged venue includes these or whether they need separate loading.

### Step 4: Remove HamDirector::Enter() early return

Delete `HamDirector.cpp:337-386`. Once `mMerger` is wired by `change_files`, the
Xbox path at line 388 (`if (mMerger)`) runs naturally, enabling:

- `Initialize()` → `SetupAnims()` — discovers MoveDir, CharClips
- `SyncScene()` — sets up camera world
- `PlayIntroShot()` — fires intro camera animation
- `TheHamWardrobe->PlayCrowdAnimation()` — crowd idle animation

### Step 5: Remove GetWorld() fallback

Delete `HamDirector.cpp:591-593` (the `#ifdef HX_NATIVE` block). `mMerger` is wired,
so `GetWorld()` returns `dynamic_cast<WorldDir*>(mMerger->Dir())` naturally.

### Step 6: Guard OnFileLoaded against null mMerger

Line 1246 has a logic issue:
```cpp
if (sym != game_hud || mMerger) {
    mAsyncLoaded = mMerger->AsyncLoad();  // null deref if !mMerger && sym != game_hud
```

Add a null guard:
```cpp
if (mMerger && (sym != game_hud || mMerger)) {
```

Or simply: `if (mMerger) {`

### Step 7: Incremental guard removal sweep

After steps 1-6 are working, remove remaining guards incrementally:

| Location | Guard | Can remove? |
|----------|-------|-------------|
| `HamDirector.cpp:1045-1052` | OnLoadSong debug logging | Keep (useful) |
| `HamDirector.cpp:1060-1097` | Crew/outfit single-player resolution | Keep (real native behavior diff) |
| `HamDirector.cpp:1232-1239` | OnFileLoaded debug logging | Keep (useful) |
| `HamDirector.cpp:1281-1295` | video_recorder.srec stub | Keep if DTA still needs it |
| `GamePanel.cpp:709-752` | StartIntro hack | Remove (Initialize/SetupAnims handles it) |
| `App.cpp:1065-1128` | Component .milo loading | Test if venue merger includes these |
| `Dir.cpp:645-648` | gNativeVenueDir from AddedSubDir | Keep for menu/title screen venue |

Realistic guard reduction: ~60 → ~20-25 (not ~5).

## Known Blockers

### MoveGraph Deserialization (already guarded)

The binary deserialization path has existing HX_NATIVE guards:

- `MoveCandidate::Load()` (MoveVariant.cpp:44-48): Clears bit 0 of mAdjacencyFlag
- `MoveVariant::CacheLinks()` (MoveVariant.cpp:241-252): Null-checks pointer names

These appear sufficient. Verify by running with a song.milo merged and checking that
`MoveGraph::CacheLinks()` completes without crash.

### SuperEasyRemixer OBJ_CLASSNAME (already guarded)

`MoveMgr.cpp:37-43` — `#ifndef HX_NATIVE` around the `SetType` call. Not a blocker.

### DTA Handlers Expecting Xbox Globals

Some DTA handlers reference `platform_mgr`, `speech_mgr`, etc. Already individually
guarded with `{if {exists X}}` in the DTA scripts. Not a blocker.

### OnFileLoaded timing with GameModeMerger

`GameModeMerger.fm` loads from the venue .milo (chars_base) BEFORE `world.fm` loads
from `world.milo`. Its `change_files` fires with `loading=1`, which tries to wire
`mGameModeMerger`. But at that point `$hamdirector` might not exist. The DTA condition
`{if {&& $loading $hamdirector}}` should handle this gracefully (skips if no director).
If `mGameModeMerger` stays null, the HUD loading in `OnFileLoaded("song")` line 1270
is gated on `if (mGameModeMerger)`. Verify this doesn't cause issues.

## Verification Plan

### Phase 1: Force async + remove PollForLoading hacks — DONE (2026-03-17)

1. [x] Add `async = true` in StartLoadInternal (FileMerger.cpp)
2. [x] Remove sync-poll timeout hack (FileMerger.cpp:458-478)
3. [x] Remove 3 GamePanel native hacks (IsWorldLoaded skip, AllCharsLoaded skip, manual song/HUD merge)
4. [x] Guard OnFileLoaded: `if (sym != game_hud || mMerger)` → `if (mMerger)`
5. [x] Verify: world_panel loads, change_files fires, mMerger wired, cascade starts
6. [x] Verify: IsWorldLoaded() eventually returns true

### Phase 2: Remove App.cpp venue loading + Enter() early return — DONE (2026-03-17)

1. [x] Remove App.cpp:994-1052 (direct DirLoader venue loading)
2. [x] Remove HamDirector::Enter() early return (337-386)
3. [x] Remove GetWorld() mVenue fallback (591-593)
4. [x] Verify: venue loaded via FileMerger cascade
5. [x] Verify: Initialize/SetupAnims runs, mClipDir/mMoveDir found

### Phase 3: Runtime verification + crash fixes — DONE (2026-03-17)

1. [x] Remove Game::IsLoaded() `#ifdef HX_NATIVE` bypass for `IsWorldLoaded()` — conditions already satisfied by PollForLoading gate
2. [x] Remove Game::IsLoaded() `#ifdef HX_NATIVE` bypass for `IsMoveMergerFinished()` — same
3. [x] Kept Game::IsLoaded() state 2 native audio initiation + timeout (still needed)
4. [x] Remove GamePanel::StartIntro native block (LoadNewSongAudio, SetupAnims, Enter, spotlights, phrase meters) — all handled by engine pipeline
5. [x] Unguard Song::SyncState — removed `#ifndef HX_NATIVE` guard, added `#ifdef HX_NATIVE` around sync-wait loop to prevent blocking
6. [x] Remove Song::SyncState stub from engine_stubs_generated.cpp
7. [x] Runtime verified: PollForLoading reaches state 4 on glitterati + dclive, no crashes

### Phase 4: Guard cleanup — DONE (2026-03-17)

1. [x] Remove `SetNativeVenueWorld()` from HamDirector.h (no callers)
2. [x] Remove `DebugWorldLoad()` from HamDirector.cpp + GamePanel.cpp (diagnostic helper)
3. [x] Simplify OnLoadSong/OnFileLoaded logging to unconditional (still inside `#ifdef HX_NATIVE`)
4. [x] Kept diagnostic logging (PollForLoading state 4, OnLoadSong, OnFileLoaded)
5. [x] Kept Game.cpp null-guards (cheap safety for edge cases)
6. [x] Kept all architectural necessities (App.cpp venue components, DC3_VENUE fallback, explicit drawing, GamePanel safety timeouts, HamDirector crew/camera/merger guards)

## What This Enables

| Feature | Current | After |
|---------|---------|-------|
| Choreography / MoveGraph | Broken (null MoveDir) | Working |
| Autoplay / flashcards | Broken | Working |
| Phrase meters / scoring | Broken | Working |
| HamDirector Initialize/SetupAnims | Skipped | Running |
| SyncScene / PlayIntroShot | Skipped | Running |
| Crowd idle animation | Skipped | Running |
| `movemgr not function or object` spam | Every frame | Gone |
| HX_NATIVE guards | ~60 | ~20-25 |

## File References

| File | Key info |
|------|----------|
| `world/gen/world.milo_xbox` | 3.3KB skeleton containing world.fm, modular.fm, GameModeMerger.fm |
| `ui/game.dta:7-14` | world_panel definition: loads world.milo |
| `char/char_objects.dta:386-428` | "world" type change_files handler: wires mMerger |
| `char/char_objects.dta:429-465` | "game_mode" type: wires mGameModeMerger |
| `char/char_objects.dta:754-759` | "modular" type: wires mMoveMerger |
| `FileMerger.cpp:181-195` | PreLoad: deserializes mergers, fires StartLoadInternal |
| `FileMerger.cpp:431-487` | StartLoadInternal: fires change_files, queues loaders |
| `HamDirector.cpp:335-386` | Enter() early return (to remove) |
| `HamDirector.cpp:1044-1128` | OnLoadSong: song/crew/outfit setup |
| `HamDirector.cpp:1231-1303` | OnFileLoaded: cascade venue/viz/character loading |
| `HamDirector.cpp:1700-1703` | IsWorldLoaded: gates on mMerger + mMoveMerger |
| `GamePanel.cpp:1011-1121` | PollForLoading: native hacks (to remove) |
| `App.cpp:994-1052` | Direct venue DirLoader (to remove) |
