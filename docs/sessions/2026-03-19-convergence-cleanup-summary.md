# Session: Convergence Cleanup — Pre-Convergence Workaround Removal

**Date**: 2026-03-19
**Status**: Complete (runtime verified)
**Related**: [convergence-cleanup.md](2026-03-19-convergence-cleanup.md) (investigation notes),
[merge-parity-p0.md](2026-03-19-merge-parity-p0.md) (MergeDirs fix, concurrent),
[venue-draw-investigation.md](2026-03-19-venue-draw-investigation.md) (draw architecture)

## What We Did

Removed ~190 net lines of pre-convergence workarounds and diagnostic logging from
App.cpp, HamDirector.cpp, and FileMerger.cpp. These were leftovers from before
FileMerger convergence landed (2026-03-17) and are now redundant because the Xbox
DTA pipeline works on native.

### Diagnostic Logging Removed

| File | What | Lines |
|------|------|-------|
| FileMerger.cpp | FinishLoading + PostMerge diagnostics | -19 |
| HamDirector.cpp | Initialize, OnLoadSong, OnFileLoaded, Poll state dumps | -77 |
| App.cpp | WorldCrowd census, frame counter, UI state dump | ~-30 |

### Workarounds Removed

| Workaround | Why Redundant | Evidence |
|-----------|---------------|----------|
| **Manual viseme/face init** (App.cpp, 55→10 lines) | `OnConfigureFileMerger` → `Select("viseme")` → `SyncObjects()` handles servo/blinking wiring automatically | `EnableFacialAnimation` fires at runtime with lipsync names |
| **VenueEnter SetType("world")** (HamDirector.cpp) | Xbox venue has NO type (SetSubDir clears it, DTA never restores). select_camera fires on world root, not venue. | Exhaustive DTA search: zero `set_type "world"` for venues |
| **Song.anim frame fallback** (HamDirector.cpp, 28 lines) | `OnSelectCamera` fires every frame via world root's ProcCounter. `kProcessPost != kProcessWorld` is always TRUE after Enter(). | ProcCounter RE confirms state machine advances past kProcessWorld |
| **venueWorld->Poll() double-poll** (App.cpp) | Venue polled via `HamDirector::ListPollChildren` through panel hierarchy. | Gated on `isMenuVenue` — only polls for menu venues now |

### Includes Cleaned Up

Removed 6 unused includes:
- HamDirector.cpp: `MeshGpuCache.h`, `LightPreset.h`, `LightPresetManager.h`
- App.cpp: `CharFaceServo.h`, `CharLipSyncDriver.h`, `Crowd.h`

## Key Discoveries

### 1. OnConfigureFileMerger IS Wired on dc3-native

`MILO_VIEWER` is only defined for the `milo-viewer` CMake target, NOT `dc3-native`.
The `#if !defined(MILO_VIEWER)` guard in `FileMerger::StartLoadInternal` means
`change_files` fires on dc3-native, triggering the full Xbox character loading chain:

```
LoadCharacters → SetOutfit → StartLoad → char.fm→StartLoadInternal
  → HandleType("change_files") → DTA "main" type → configure_file_merger
  → OnConfigureFileMerger → Select("viseme") + Select("outfit")
  → FileMerger loads → SyncObjects wires CharFaceServo + CharLipSyncDriver
```

### 2. Venue Type Architecture (Xbox)

`SetSubDir(true)` calls `SetTypeDef(nullptr)`, clearing the venue's type on both
platforms. The DTA never calls `set_type` on the venue WorldDir. On Xbox, the venue
has **no type** — confirmed by exhaustive search of all .dta files.

The `select_camera` handler fires on the **world root** (type "world"), not the venue.
The venue is polled as a child via `HamDirector::ListPollChildren`, entering
`WorldDir::Poll()`'s `TheWorld != nullptr` branch which just calls `RndDir::Poll()`
— no `HandleType`, no camera management.

### 3. ProcCounter State Machine

After `WorldDir::Enter()` calls `SetProcAndLock(false)` + `ResetProcCounter()`:
- Frame 1: `mFirstPoll=true` → select_camera fires
- Frame 2+: `ProcCmds()` returns `kProcessPost` (2), condition `2 != kProcessWorld(1)`
  is TRUE → select_camera fires continuously

This means `OnSelectCamera` drives `songAnim->SetFrame()` every frame — no fallback
needed.

### 4. extras.fm Is Dead in DC3

DC3 venues don't have `extras.fm` (unlike RB3). The extras.fm code in
`OnLoadSong` (HamDirector.cpp:1104-1108) is inherited from BandDirector and never
fires. Venue component .milo files (`_buildings`, `_sky`, `_set`, `_chairs`,
`_table_glasses`) are loaded manually in App.cpp via `DirLoader::LoadObjects` +
`MergeDirs`.

## HX_NATIVE Guard Reduction

| File | Before | After |
|------|--------|-------|
| HamDirector.cpp | 15 | 8 |
| FileMerger.cpp | 5 | 3 |
| App.cpp | 8 | 7 |

## Runtime Verification

Tested with `DC3_VENUE=glitterati DC3_SCREEN=game_screen DC3_HEADLESS=1`:

- PollForLoading reaches state 4 (DONE)
- game_screen enters successfully
- Venue hash=169 objects (proper merge, not 1)
- `EnableFacialAnimation` fires via FileMerger pipeline with real lipsync names
- No crashes or asserts from any removal
- 5 venue components loaded successfully

## Follow-Up Items

### Investigate: SuperEasyRemixer::Init() explicit call
**File**: HamDirector.cpp:540-542 (`#ifdef HX_NATIVE`)
**What**: Calls `TheMoveMgr->mSuperEasyRemixer->Init()` explicitly in Initialize().
On Xbox, this fires from the DTA reset handler.
**Action**: Check if the DTA reset handler fires on native. If so, the explicit call
is redundant and can be removed.

### Investigate: Venue component loading on Xbox
**File**: App.cpp:1058-1094
**What**: Manually loads 5 venue component .milo files via DirLoader + MergeDirs.
extras.fm is dead in DC3 (inherited from RB3/BandDirector).
**Question**: On Xbox, are these components inlined subdirs in the venue .milo, loaded
by a DTA handler, or loaded through some other mechanism? Understanding this would
let us either remove the manual loading or properly wire it through the engine.

### Investigate: OnLoadSong crew/outfit resolution
**File**: HamDirector.cpp:1007-1044 (`#ifdef HX_NATIVE`)
**What**: Validates player presence, reconstructs crew/outfit from character token
for native single-player flows.
**Question**: Can this be removed once the full DTA multiuser flow is working? Or is
it a permanent difference in how native handles single-player mode?

### Investigate: Interest object creation
**File**: App.cpp:1170-1190
**What**: Creates fallback audience interest objects for characters with no interests.
On Xbox, interests come from character .milo files.
**Question**: Do the character .milo files actually include CharInterest objects? If
yes, the FileMerger pipeline loads them and this fallback can be removed. If no, the
fallback is needed permanently.

### Low priority: SongAnim routing
**File**: HamDirector.cpp:551-565 (`#ifndef HX_NATIVE`)
**What**: On Xbox, `merge_moves=1` returns routine builder anim; on native, always
returns difficulty-specific song.anim.
**Why**: The routine builder requires the full DTA-driven remixer flow
(`populate_movemgr`, `FillRoutineFromParents`, `InsertMoveInSong`). This is complex
and not yet wired on native.
**Action**: Long-term convergence goal. Low priority since difficulty anims work.

### Monitor: ReplaceNode suppressed erase warnings
Runtime shows `ReplaceNode: suppressed erase during ReplaceList` warnings from UI
flow merges (choose_mode_screen). These are pre-existing and from the
`gInReplaceList` mechanism in ObjPtrVec. Not a regression from this session, but
indicates the ObjRef ring manipulation still has edge cases worth investigating.
