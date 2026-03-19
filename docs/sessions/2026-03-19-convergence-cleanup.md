# Convergence Cleanup: Remove Pre-Convergence Workarounds

**Date**: 2026-03-19
**Status**: Phase 1-2 DONE, runtime verified
**Prerequisite**: FileMerger convergence (Phase 1-5 complete 2026-03-17)

## Context

FileMerger convergence landed 2026-03-17. Many native workarounds in App.cpp and
HamDirector.cpp predate that work and are now redundant. The DTA pipeline fires
`change_files` → `configure_file_merger` on dc3-native (MILO_VIEWER is NOT defined
for dc3-native, only for milo-viewer). The full Xbox character/viseme/outfit loading
chain should work through the engine pipeline.

## Key Findings

### 1. OnConfigureFileMerger IS Wired

The `change_files` handler fires on dc3-native. The chain:
```
LoadCharacters → SetOutfit → StartLoad → char.fm→StartLoadInternal
  → HandleType("change_files")   [#if !defined(MILO_VIEWER) — TRUE for dc3-native]
  → DTA "main" type handler (char_objects.dta)
  → {{$this dir} configure_file_merger}
  → HamCharacter::OnConfigureFileMerger
  → Select("viseme", path) + Select("outfit", path)
  → FileMerger loads visemes/outfit
```

The manual viseme loading in App.cpp:1199-1253 was added 2026-03-16, one day before
convergence. It's a pre-convergence workaround.

### 2. Venue Type Is Cleared by SetSubDir — On BOTH Platforms

When the venue WorldDir is added as a proxy subdir of the world root:
```
ObjectDir::AddedSubDir → dir->SetSubDir(true) → SetTypeDef(nullptr)
```

This clears the venue's type on **both Xbox and native**. The DTA never calls
`set_type "world"` or `set_type "venue"` on the venue WorldDir — confirmed by
exhaustive search of all .dta files. The HX_NATIVE workaround sets type "world",
but **on Xbox the venue has no type at all**.

### 3. select_camera Fires on WORLD ROOT, Not Venue

The `select_camera` DTA handler (world_objects.dta:1840) fires on the **world root**
(which has type "world"), not the venue. The venue is polled as a child through
HamDirector::ListPollChildren → `WorldDir::Poll()` → `TheWorld != nullptr` branch →
just calls `RndDir::Poll()` (no HandleType, no camera management).

So the VenueEnter SetType("world") workaround is **wrong** — it adds handlers the
Xbox venue never has. The select_camera mechanism works via the world root regardless.

### 4. Venue Poll Already Cascades Through Panel Hierarchy

The venue is polled through: `world_panel → WorldDir::Poll(world root) → RndDir::Poll()
→ mPolls (includes HamDirector) → HamDirector polls mVenue via ListPollChildren`.
The explicit `venueWorld->Poll()` in App.cpp:1186 causes **double-polling** during
gameplay. It's only needed for menu venues (when `!TheHamDirector`).

### 5. extras.fm May Need Force-Reload

After deserialization from the venue .milo, extras.fm mergers may have
`mSelected == mLoaded`, causing `NeedsLoading()` to return false. The
`OnLoadSong` call at line 1107 (`extras->StartLoad(b5)`) may not trigger loading.
The App.cpp code sets `mForceReload = true` to work around this. Needs testing.

Alternatively, extras.fm should be started in `OnFileLoaded("venue")` after
`mVenue` is set (it's currently only started in OnLoadSong, where mVenue may
still be null from the previous session).

## Phase 1: Delete Diagnostic Logging

All confirmed safe to remove — pure logging with no behavioral effect.

### FileMerger.cpp — DONE
- [x] `Merger::Load` diagnostic — already removed prior to this session
- [x] `FinishLoading` diagnostic (`DC3 DIAG FM::FinishLoading`) — removed
- [x] `PostMerge` diagnostic (`DC3 DIAG FM::PostMerge`) — removed

### HamDirector.cpp — DONE
- [x] `Initialize()` state dump (`[INIT]` logs) — removed
- [x] `OnLoadSong` logging — removed
- [x] `OnFileLoaded` logging — removed
- [x] `OnFileLoaded` venue diagnostic — removed (kept stub creation)
- [x] `Poll()` MILO_DEBUG_CLIPS state dump — removed

### App.cpp — DONE
- [x] WorldCrowd census diagnostic — removed (+ unused `Crowd.h` include)
- [x] Frame counter (every 1000 frames) — removed
- [x] Periodic UI state dump (every 500 frames) — removed

### Also fixed:
- [x] `venueWorld->Poll()` double-poll — now gated on `isMenuVenue` (only polls for menu venues, gameplay venue polled via HamDirector::ListPollChildren)

## Phase 2: Remove Pre-Convergence Workarounds (Test Each)

### 2a: Manual viseme/face init (App.cpp:1168-1223) — DONE
- **What**: Manually loaded viseme clips, wired CharEyes, enabled blinking
- **Xbox path**: `OnConfigureFileMerger` → `Select("viseme")` → FileMerger loads
  visemes → `HamCharacter::SyncObjects()` wires CharFaceServo, CharLipSyncDriver,
  and blinking automatically (lines 205-214 of HamCharacter.cpp)
- **Evidence**: Added 2026-03-16, one day before FileMerger convergence
- **Fix**: Removed viseme loading, servo wiring, and blinking init (55→10 lines).
  Kept interest object fallback creation (characters may not have interests in .milo).
  Removed unused CharFaceServo.h and CharLipSyncDriver.h includes.
- **Status**: DONE

### 2b: Manual venue component loading (App.cpp:1058-1094) — BLOCKED BY P0
- **What**: Manually loads `_buildings`, `_sky`, `_set`, `_chairs`, `_table_glasses`
  .milo files via `DirLoader::LoadObjects` + `MergeDirs`
- **Key finding**: DC3 venues do NOT have extras.fm (unlike RB3). The extras.fm code
  in OnLoadSong (HamDirector.cpp:1104-1108) is dead code inherited from BandDirector.
  Component .milo files are separate archives that must be loaded manually.
- **Blocker**: This code calls `MergeDirs`, which has heap corruption bugs on real
  venue data (see `docs/sessions/2026-03-19-merge-parity-p0.md`). Fixing MergeDirs
  is P0 before this can be cleaned up.
- **On Xbox**: How are components loaded? Likely inlined into the venue .milo or loaded
  by a DTA handler we haven't found yet. Needs investigation.
- **Status**: BLOCKED by MergeDirs P0

### 2c: venueWorld->Poll() double-poll (App.cpp:1152) — DONE (in Phase 1)
- **What**: Explicit poll of venue WorldDir every frame
- **Finding**: Venue IS polled through HamDirector::ListPollChildren. This call
  caused double-polling during gameplay.
- **Fix**: Gated on `isMenuVenue` (only polls for menu venues)
- **Status**: DONE

### 2d: VenueEnter SetType("world") (HamDirector.cpp:608-616) — SAFE TO REMOVE, NEEDS RUNTIME TEST
- **What**: Sets venue type to "world" if no TypeDef
- **Finding**: Xbox venue has NO type (cleared by SetSubDir). DTA never sets it.
  Setting "world" is harmless but wrong — it adds DTA handlers that Xbox doesn't
  have on the venue. The "world" type's enter handler is `post_tool_sync` which only
  fires in editor mode, so it's a no-op in-game. The "world" type's select_camera
  handler is irrelevant because the venue is polled as a child (TheWorld != nullptr
  branch in WorldDir::Poll — just calls RndDir::Poll(), no HandleType).
- **Corrected analysis**: select_camera fires on the WORLD ROOT (not venue), so the
  venue's type doesn't affect camera management. OnSelectCamera drives song.anim via
  the world root's Poll chain regardless.
- **Fix**: Remove the `#ifdef HX_NATIVE` SetType block to match Xbox (null type)
- **Status**: DONE — runtime verified

### 2e: Song.anim frame fallback (HamDirector.cpp:3037-3064) — LIKELY REMOVABLE, NEEDS RUNTIME TEST
- **What**: Drives song.anim frame from beat/wall-clock when OnSelectCamera hasn't fired
- **Finding**: select_camera fires on WORLD ROOT every frame after Enter(). The
  ProcCounter state machine ensures `ProcCmds()` returns `kProcessPost` (2) on
  frame 2+, so the condition `ProcCmds() != kProcessWorld` (2 != 1) is TRUE.
  OnSelectCamera reliably calls `songAnim->SetFrame(frame, blend)`, making the
  fallback's condition `songAnim->GetFrame() < 0` false after the first frame.
- **Risk**: If DTA handler chain is broken (missing TypeDef on world root, handler
  not registered), OnSelectCamera won't fire and choreography won't advance.
  The fallback catches this case.
- **Fix**: Remove the `#ifdef HX_NATIVE` fallback block
- **Status**: DONE — runtime verified

### 2f: Duplicate Kinect mesh hiding in explicit draw — ALREADY CLEAN
- **Finding**: The explicit draw block (App.cpp:1235-1243) was already cleaned up in
  a prior session. It only draws the menu venue when `!TheHamDirector`. No duplicate
  mesh hiding remains.
- **Status**: N/A (already clean)

## Phase 3: Proper Pipeline Fixes

### 3a: extras.fm is DEAD in DC3 — N/A
DC3 venues don't have extras.fm (confirmed by concurrent P0 investigation). The
extras.fm code in OnLoadSong (HamDirector.cpp:1104-1108) is inherited from
BandDirector and never fires. Component .milo files (_buildings, _sky, etc.) are
separate archives loaded manually in App.cpp via DirLoader::LoadObjects + MergeDirs.

On Xbox, these components are likely inlined into the venue .milo file itself or
loaded through a DTA mechanism we haven't traced yet. This needs investigation but
is blocked by the MergeDirs heap corruption P0.

### 3b: SuperEasyRemixer::Init() (HamDirector.cpp:576-578)
- **What**: Called explicitly in Initialize() on native
- **Xbox path**: Fires from DTA reset handler
- **Test**: Check if DTA reset fires on native; if so, remove explicit call
- **Status**: PENDING

## Code That Must Stay (Architectural)

| Location | What | Why |
|----------|------|-----|
| HamDirector.cpp:587-596 | SongAnim routing (`#ifndef HX_NATIVE`) | Different choreography model |
| App.cpp:1265-1273 | Pre-game venue draw | Menu/attract mode before HamDirector exists |
| App.cpp:1188-1198 | Menu character position reset | Only fires for menu venues |
| App.cpp:1131-1178 | Kinect mesh hiding (one-shot) | No Kinect/RTT hardware on native |
| HamDirector.cpp:1043-1080 | OnLoadSong crew/outfit resolution | Native single-player flow differences |
| HamDirector.cpp:1273-1276 | video_recorder.srec stub | DTA expects this object |
| HamDirector.cpp:2316-2322 | SetShot frame guard | Different anim source |
| HamDirector.cpp:741-744 | FindNextDircut null guard | Safety for missing dircut data |
| HamDirector.cpp:2294-2301 | Camera shot Area1_WIDE fallback | Safety for missing shots |
| FileMerger.cpp:229-241 | Nested object flattening | Match Xbox flat scope |
| FileMerger.cpp:432-440 | DirLoader parent propagation | Deserialization scope fix |
| Dir.cpp:657-663 | gNativeVenueDir detection | Menu venue rendering |

## P0 Dependency: MergeDirs Heap Corruption

See `docs/sessions/2026-03-19-merge-parity-p0.md`.

MergeDirs crashes with `corrupted double-linked list` on real venue data. This blocks:
- Phase 2b (manual venue component loading uses MergeDirs)
- Venue proxy merge producing correct object graphs (`hash=1` instead of hundreds)
- Any convergence that depends on venue objects being properly registered

The ObjRef ring manipulation in `MergeObjectsRecurse` (Utl.cpp:367-379) and the
`gInReplaceList` suppression in `ObjPtrVec::ReplaceNode` are the suspected root causes.

## Investigation Results

### Song.anim frame fallback (Phase 2e)
**select_camera fires every frame** via the world root's Poll. After `WorldDir::Enter()`
calls `SetProcAndLock(false)` + `ResetProcCounter()`, the ProcCounter advances to
`kProcessPost` (2) on frame 2+. The condition `ProcCmds() != kProcessWorld` (2 != 1)
is TRUE, so `HandleType("select_camera")` fires continuously.

OnSelectCamera sets `songAnim->SetFrame(frame, blend)` from beat-to-frame conversion.
This means the native fallback in Poll() is unnecessary IF the DTA `select_camera`
handler fires correctly. The fallback's condition `songAnim->GetFrame() < 0` would
never trigger once OnSelectCamera runs.

**Recommendation**: Remove fallback, but requires runtime test to confirm OnSelectCamera
actually receives the message (DTA handler chain must be intact).

### VenueEnter SetType (Phase 2d)
**Pending final analysis** — agent still running. Preliminary finding: Xbox venue has
NO type after SetSubDir clears it. The "venue" DTA type exists but is for standalone
venues in the editor, not gameplay proxy subdirs.

## Verification Results

### Build verification
- [x] PPC build passes (no regressions)
- [x] Native build passes (dc3-native)
- [x] No unused includes remaining
- [x] MergeScopeParity tests pass (venue merge objects=5054 corrupt=0)

### Runtime verification (DC3_VENUE=glitterati DC3_SCREEN=game_screen)
- [x] PollForLoading reaches state 4 (DONE)
- [x] game_screen enters successfully
- [x] Venue hash=169 (proper object count, not 1)
- [x] EnableFacialAnimation fires via FileMerger pipeline (player0/player1 get lipsync)
- [x] No crashes or asserts from VenueEnter SetType removal
- [x] No crashes or asserts from song.anim fallback removal
- [x] 5 venue components loaded successfully

### HX_NATIVE guard reduction
- HamDirector.cpp: 15 → 8 guards
- FileMerger.cpp: 5 → 3 guards
- App.cpp: 8 → 7 guards
- Net: -290 lines deleted, +100 inserted across 3 key files
- Removed 6 unused includes (LightPreset.h, LightPresetManager.h, MeshGpuCache.h
  from HamDirector; CharFaceServo.h, CharLipSyncDriver.h, Crowd.h from App.cpp)
