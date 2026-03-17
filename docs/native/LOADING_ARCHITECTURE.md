# Loading Architecture: Xbox FileMerger vs Native Direct Loading

**Divergence analysis**: [../sessions/2026-03-16-architecture.md](../sessions/2026-03-16-architecture.md)
**Convergence plan**: [FILEMERGER_CONVERGENCE.md](FILEMERGER_CONVERGENCE.md)
**Design review**: [../sessions/2026-03-17-convergence-review.md](../sessions/2026-03-17-convergence-review.md)

## Quick Summary

The native port's biggest architectural divergence from Xbox is world loading:

- **Xbox**: `world_panel` loads `world/world.milo` (3.3KB skeleton), which contains
  `world.fm` (FileMerger). `world.fm` merges venue + song + viz into one ObjectDir,
  driven by DTA scripts and `file_merger_organizer.dta`
- **Native**: `DirLoader::LoadObjects()` loads the venue `.milo` directly in
  `App.cpp:994`, bypasses FileMerger entirely

This was a pragmatic decision to get rendering working without fixing `MoveMgr::Init()`
crashes, DTA pipeline dependencies, and sync loading hangs on web. It's the root cause
of all null pointers in the MoveGraph/choreography/scoring pipeline.

## Key Discovery (2026-03-17)

**The engine pipeline already works on native.** `world_panel` loads `world.milo`,
`world.fm` fires `change_files`, and `mMerger` gets auto-wired. The native hacks in
App.cpp and GamePanel.cpp bypass the working pipeline. The fix is to remove the bypasses.

`world.fm` is NOT in the venue .milo (as originally assumed). It's in `world.milo`,
loaded by `world_panel`. The venue .milo only contains `crowd_clips.fm`.

## What Works Without FileMerger

Venue rendering, crowd, camera cuts, post-processing, audio, character animation — all
work because they don't depend on merged song content.

## What's Broken Without FileMerger

MoveGraph, choreography, autoplay, flashcards, phrase meters, scoring, dynamic
difficulty — all broken because `moves.milo` is never merged into the venue world.

## Recommended Path Forward

**Remove the bypasses, force async.** The pipeline self-wires via DTA change_files
handlers. Force `async = true` in `StartLoadInternal()` to prevent sync-poll hangs on
web. Remove the manual DirLoader in App.cpp and the PollForLoading hacks in
GamePanel.cpp. Let the DTA cascade (OnLoadSong → OnFileLoaded → IsWorldLoaded) drive
loading naturally.

See [FILEMERGER_CONVERGENCE.md](FILEMERGER_CONVERGENCE.md) for the validated
implementation plan with runtime-verified architecture.
