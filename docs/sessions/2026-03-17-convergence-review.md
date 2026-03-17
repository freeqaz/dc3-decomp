# FileMerger Convergence Plan — Design Review

**Date**: 2026-03-17
**Reviewer**: Staff engineer design review of [FILEMERGER_CONVERGENCE.md](../native/FILEMERGER_CONVERGENCE.md)
**Status**: Review complete — all issues validated, plan revised

## Verdict

Direction is correct. Converging on the engine's FileMerger pipeline is the right call.
The original plan had a fundamental assumption error (world.fm location) but the
underlying insight was right: the engine already does this work.

**Key discovery**: The engine pipeline **already works on native**. world_panel loads
world.milo, world.fm fires change_files, mMerger gets wired, modular.fm and
GameModeMerger.fm also fire correctly. The native hacks just bypass it all.

The revised plan is in [FILEMERGER_CONVERGENCE.md](../native/FILEMERGER_CONVERGENCE.md).

---

## CRITICAL FINDING: world.fm is in world.milo, not the venue

**Empirically verified 2026-03-17** via diagnostic instrumentation.

### What we tested

1. Added runtime diagnostics to App.cpp venue loading path and FileMerger::PreLoad
2. Ran `DC3_SCREEN=game_screen` with `MILO_DEBUG_UI_FLOW=1`

### Results

**Venue .milo contents** (e.g., glitterati.milo):
```
world.fm = (nil)                     ← NOT in venue
crowd_clips.fm type='crowd_anim'     ← only FM in venue, 6 mergers
```

**world.milo contents** (loaded by world_panel from `world/gen/world.milo_xbox`, 3.3KB):
```
world.fm type='world' mergers=3      ← song, viz, venue
modular.fm type='modular' mergers=0
GameModeMerger.fm type='game_mode' mergers=1 (game_hud)
```

**change_files firing on native** (verified via printf in StartLoadInternal):
```
FM::StartLoadInternal 'world.fm' type='world' firing change_files async=1 loading=1
FM::StartLoadInternal after change_files: mMerger=0x55c6c6ce7b30  ← WIRED!
```

**world_panel loading** (verified via MILO_DEBUG_UI_FLOW):
```
Panel 'world_panel' starting load from 'world/world.milo' pos=1
Panel 'world_panel' finished loading (state=2)
```

### Architecture diagram

```
world/world.milo (3.3KB skeleton) ← loaded by world_panel (UIPanel)
  ├── world.fm (type "world", mergers: song/viz/venue)
  │   └── change_files: {$hamdirector set merger $this}
  ├── modular.fm (type "modular", mergers: dynamically populated)
  │   └── change_files: {$hamdirector set move_merger $this}
  └── GameModeMerger.fm (type "game_mode", mergers: game_hud)
      └── change_files: {$hamdirector set game_mode_merger $this}

venue .milo (e.g., glitterati.milo) ← merged into world ObjectDir by world.fm
  └── crowd_clips.fm (type "crowd_anim", 6 mergers)
```

---

## Issue Resolution Summary

### Issue 1: world.fm location — RESOLVED (was wrong, corrected)

world.fm is in world.milo, not the venue. The original plan proposed finding world.fm
in the venue and manually assigning mMerger. This is impossible. The correct approach:
let world_panel load world.milo and let change_files auto-wire mMerger.

### Issue 2: mMoveMerger/mGameModeMerger wiring — RESOLVED (auto-wired)

All three FMs fire change_files on native:
- world.fm → mMerger (verified: pointer set after change_files)
- modular.fm → mMoveMerger
- GameModeMerger.fm → mGameModeMerger

No manual wiring needed. Just remove the native hacks that bypass the pipeline.

### Issue 3: OnFileLoaded null deref — STILL NEEDS GUARD

Line 1246-1247 dereferences mMerger for any non-game_hud symbol. If OnFileLoaded fires
before mMerger is wired (unlikely but possible timing), null deref. Add guard.

### Issue 4: Venue loading chicken-and-egg — RESOLVED (new understanding)

The venue is loaded BY world.fm's "venue" merger, not by the DirLoader. On Xbox:
1. world.milo loads → world.fm wired
2. change_files fires load_game_song
3. OnLoadSong → mMerger->Select("song", ...) → song loads
4. OnFileLoaded("song") → mMerger->Select("venue", venuePath) → venue loads
5. OnFileLoaded("venue") → mVenue set

The direct DirLoader in App.cpp is the bypass. Removing it lets the engine load venues.

### Issue 5: MoveGraph deserialization — RESOLVED (already guarded)

Existing guards handle the 32-bit→64-bit pointer issue. Need runtime verification
that CacheLinks completes after async song merge.

### Issue 6: Async timing — RESOLVED (engine handles it)

IsWorldLoaded() is checked each frame by PollForLoading. It returns false while
HasPendingFiles() is true. The async cascade completes over multiple frames, then
IsWorldLoaded() returns true and the game proceeds. This is exactly how Xbox works.

### Issue 7: Guard count — CORRECTED

Realistic: ~60 → ~20-25 (not ~5). Many guards address real native behavior differences.

### Issue 8: Step ordering — CORRECTED

Revised plan uses a simpler linear ordering with clear dependencies.
