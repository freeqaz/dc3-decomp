# FileMerger-Based Character Loading — Session Summary

**Date:** 2026-03-05
**Goal:** Replace manual outfit/viseme wiring in the native port viewer with the game's `FileMerger` system (`char.fm` inside `main.milo_xbox`).

## What We Accomplished

### 1. Merge Decomp Status (Current Workspace)
`MergeDirs()` is implemented and matches, but `MergeObjectsRecurse()` is currently still a **stub** in this workspace and is a high-priority TODO.

Key DC3-vs-RB3 API differences discovered:
- `kReplace`/`kKeep` → `kMergeReplace`/`kMergeKeep`
- `FindObject(name, false)` → `FindObject(name, false, false)` (3 args)
- `Replace(from, to)` → `Replace(to)` (single arg)
- `Refs()` returns circular linked list (ObjRef next/prev), not vector

### 2. Added `ObjRef::NextRef()` Accessor
`src/system/obj/Object.h` — The ObjRef linked list traversal needed a public accessor for `next` since it was protected.

### 3. Fixed `HandleType(change_files)` Overwriting Selections
`src/system/char/FileMerger.cpp` — `StartLoadInternal()` sends `HandleType(change_files)` which propagates to HamCharacter's DTA handler, calling `OnConfigureFileMerger()` and re-selecting the default outfit. Fixed with `#ifndef HX_NATIVE` guard.

### 4. Viewer Integration (`--char-setup` CLI)
- **`ViewerArgs.h/.cpp`** — Added `--char-setup <path>` argument
- **`ViewerScene.cpp`** — `LoadFileMerger()` method: finds `char.fm`, calls `Select("outfit", ...)` and `Select("viseme", ...)`, then `StartLoad(false)` for synchronous merge
- **`milo_viewer.cpp`** — After merge, finds auto-wired components via `SyncObjects()`:
  - `CharFaceServo "face.faceservo"`
  - `CharEyes "CharEyes.eyes"` (2 eyes, 0 interests)
  - `CharLipSyncDriver "face.lipdrv"`

### 5. Rendering Works!
After merge: **24 meshes, 19 materials, 23 textures, 323 other objects**. The renderer draws **15 mesh draw calls per frame** stably (tested 40k+ frames without crash).

## Current State

### Working
- FileMerger finds `char.fm` in `main.milo_xbox`
- Outfit + viseme files merge correctly into the character dir
- `SyncObjects()` auto-wires CharFaceServo, CharEyes, CharLipSyncDriver
- Rendering is stable at 15 draw calls/frame
- Fallback path (no `--char-setup`) still works unchanged

### Workarounds In Place
1. **`ScanScene` skipped when FileMerger active** — `ObjDirItr<RndAnimatable>` crashes on dangling objects in the hash table after merge. Root cause is likely objects that were in the source dir, had matching names in the target (so data was copied via `o2->Copy(o1, ...)`), and then source dir was deleted — leaving stale vtable pointers somewhere in the iteration path.

2. **Subdir recursion disabled in `MergeObjectsRecurse`** — `#ifdef HX_NATIVE` guard skips the recursive subdir merge. Enabling it causes SIGSEGV in `PostMerge` → `ObjDirPtr::~ObjDirPtr` → `ObjectDir::HasDirPtrs`. The issue is stale `ObjDirPtr` references when the source dir's subdirs are destroyed after merge moved objects out.

3. **`realpath()` is optional** — Falls back to raw path since outfit/viseme files may not exist as standalone filesystem files (ark-relative paths).

## Answers (Follow-up)

### Q1: Why does `ObjDirItr` crash after merge?
Most likely because some hash entries point to dead objects after merge/delete in native. `ObjDirItr` was only checking for null object/null vptr before `dynamic_cast`, not whether the pointer was still live.

**Fix applied:** `ObjDirItr::Advance()` now checks `HmxObjectIsLive(mEntry->obj)` and skips dead entries. This makes post-merge iteration much safer in native.

### Q2: Why does subdir recursion crash?
The crash pattern is still consistent with subdir ownership/ref-lifetime issues during `AppendSubDir` + source-dir teardown. This path remains disabled under `HX_NATIVE`.

Important signal update (this workspace): `MergeDirs` is complete, but `MergeObjectsRecurse` currently objdiffs as a **stub** (all-insert). That means native behavior can diverge simply because core merge recursion logic is not implemented yet.

### Q3: How to get animation working with FileMerger path?
Two practical paths:
- **Already works:** use `--clips` (beat-based `CharDriver` path), independent from `ScanScene`.
- **For `RndAnimatable` timeline:** re-enable `ScanScene` after validating post-merge iteration stability with the live-object guard above.

### Q4: CharLipSyncDriver data source?
`face.lipdrv` expects a `CharLipSync` object + viseme clips:
- `CharLipSync` objects register into `CharLipSync::sLipSyncMap` on load (`CharLipSync::RegisterLipSync` in `PostLoad`).
- Lookup is by sound name: `FindLipSyncForSound(sound)` converts `<sound>.*` to `<sound>.lipsync`.
- `HamCharacter::OnSoundPlay` calls that lookup, then `EnableFacialAnimation(lipSync, -seconds)`.

So yes: runtime lip sync comes from loaded `.lipsync` objects (typically in song/content milos), not from `face.lipdrv` alone.

Decomp status check:
- `CharLipSync::FindLipSyncForSound(Sound*)` is **100% COMPLETE**.
- `CharLipSyncDriver::Poll()` is currently **AT_LIMIT (~90.7%)**, so subtle behavioral differences there are still possible.

### Q5: Is there a screenshot capture bug?
Yes. The test command used `--output`/`--frames`, but viewer only parsed `--screenshot` (and previously ignored `--frames`), so it fell into interactive mode and kept running.

**Fixes applied:**
- `--output <png>` is now an alias for `--screenshot`.
- `--frames <N>` is now parsed:
  - screenshot mode: warmup render count before capture
  - video mode: exact frame count override
  - interactive mode: auto-exit after N frames

## Test Command
```bash
cd native
./build/milo-viewer \
  ~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/char/main/dancer/gen/aubrey01.milo_xbox \
  --char-setup ~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/char/main/gen/main.milo_xbox \
  --visemes ~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/char/main/dancer/aubrey01/gen/viseme.milo_xbox \
  --frames 1 --output /tmp/claude/fm_test.png
```

## Files Modified

| File | Change |
|------|--------|
| `src/system/obj/Utl.cpp` | `MergeDirs` present; `MergeObjectsRecurse` still pending implementation in this workspace |
| `src/system/obj/Object.h` | Added `ObjRef::NextRef()` accessor |
| `src/system/obj/Dir.h` | Added `HmxObjectIsLive` guard in `ObjDirItr` |
| `src/system/char/FileMerger.cpp` | `#ifndef HX_NATIVE` guard on `HandleType(change_files)` |
| `native/src/viewer/ViewerArgs.h` | `charSetupPath` + `maxFrames` field |
| `native/src/viewer/ViewerArgs.cpp` | `--char-setup` parsing + `--output` alias + `--frames` parsing |
| `native/src/viewer/ViewerCapture.h` | `ScreenshotMode::warmupFrames` |
| `native/src/viewer/ViewerCapture.cpp` | `--frames` behavior in screenshot/video/interactive modes |
| `native/src/viewer/ViewerScene.h` | `fileMergerActive` flag, `LoadFileMerger()` |
| `native/src/viewer/ViewerScene.cpp` | `LoadFileMerger()` implementation |
| `native/src/viewer/ViewerAnimation.h` | `CharLipSyncDriver*` in CharAnimState |
| `native/src/viewer/ViewerAnimation.cpp` | `lipDriver->Poll()` in PollFace, ScanScene skip |
| `native/src/viewer/milo_viewer.cpp` | FileMerger wiring, facial component auto-discovery |

## Cleanup Still Needed
- Remove debug `DBG:` print markers from `milo_viewer.cpp`
- Remove diagnostic object-counting code from `ViewerScene.cpp::LoadFileMerger()`
- Remove debug traces from `FileMerger.cpp` (FinishLoading, MergeDirs)
- Restore `ScanScene` recursive flag once dangling pointer issue is resolved

## TODOs (Updated)
- [ ] **Implement `MergeObjectsRecurse(ObjectDir*, ObjectDir*, MergeFilter&, bool)`** in `src/system/obj/Utl.cpp` (currently still a stub in this workspace; objdiff reports all-insert).
- [ ] Implement/verify `MergeObject(Object*, Object*, ObjectDir*, MergeFilter&)` filter-overload if still unresolved in this branch.
- [ ] Add A/B runtime toggles for native-only ref/lifetime guards (`HX_NATIVE` paths in `Object.cpp`, `ObjPtr_p.h`, `Dir.h`) to isolate which patch masks or introduces divergence.
- [ ] Keep and expand fixture-backed lifetime tests that load real content from archives (not only synthetic object graphs).
- [ ] Add fixture test coverage for:
  - FileMerger collision cases where src/dst contain duplicate names.
  - Subdir merge/ownership scenarios (`kMergeInlinedMoveSharedSubdirs`) to reproduce the PostMerge/ObjDirPtr crash path.
  - Post-merge `ObjDirItr` walks over complex character dirs (`main_resource`, `viseme_resource`, `skeleton_bones_resource`).
