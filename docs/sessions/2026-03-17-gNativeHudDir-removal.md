# Session: Remove gNativeHudDir Hack — Final FileMerger Convergence

**Date**: 2026-03-17
**Status**: Blocked — gNativeHudDir removal causes visual regression; needs flow target resolution fix first
**Prerequisite**: FileMerger convergence Phase 1-4 (commit b9719618e)

## Problem

The native port has a standalone HUD loading hack in `App.cpp` that creates a parallel
ObjectDir scope, breaking DTA flow animations and causing white rectangles.

### What the hack does

1. **Loading** (~line 1200-1244): When `game_screen` is active, `App::Update()` loads
   the HUD .milo directly via `DirLoader::LoadObjects()` into a static `gNativeHudDir`.
   This bypasses the FileMerger pipeline entirely.

2. **Drawing** (~line 1499-1621): A manual rendering pass draws from `gNativeHudDir`
   with extensive workarounds:
   - Hides ALL drawables, then whitelists only `song_name.lbl` and `song_artist.lbl`
   - Forces material alpha to 1.0 (compensates for DTA flow animations not running)
   - Forces text font alpha to 1.0
   - Manually repositions score displays (detaches from flow-animated parent chain)
   - Sets up its own camera/environment

### Why it's wrong

The FileMerger pipeline **already loads the HUD correctly**. `GameModeMerger.fm`'s
`change_files` handler fires `load_game_hud`, which selects the right HUD .milo and
merges it into the world ObjectDir. Runtime logs confirm:

```
DC3 HamDirector::OnFileLoaded sym='game_hud' merger=0x560fa9c840a0
```

So the HUD is loaded **twice**:
1. By FileMerger → merged into world ObjectDir (correct scope)
2. By App.cpp hack → standalone `gNativeHudDir` (broken scope)

### The scope split

This dual loading creates two separate ObjectDir hierarchies:

```
world ObjectDir (FileMerger-managed)
├── venue objects (phrase_meter0, phrase_meter1, ...)
├── song objects
├── game_hud objects (flashcard_00-03, pose_flashcard_bg.mesh, ...)
│   └── flows (Start1.flow, set_color1.flow, Hide_Boxyman_Feedback1.flow, ...)
└── ...

gNativeHudDir (standalone — App.cpp hack)
├── same HUD objects (duplicate)
└── same flows (duplicate, but targets unresolved)
```

When flows in the merged HUD try to find targets like `phrase_meter0` or
`flashcard_00`, `FindObject(name, false, true)` searches the flow's own dir + subdirs.
Since phrase_meter is in the venue (sibling scope) and flashcard objects may be in a
different subdir, the lookup fails:

```
FlowMultiSetProperty:Start1.flow () couldn't find phrase_meter0 in Start1.flow ()
FlowMultiSetProperty:set_color1.flow () couldn't find flashcard_00 in set_color1.flow ()
```

The manual drawing code in App.cpp draws from `gNativeHudDir` (the standalone copy),
not from the merged copy. Meanwhile, it hides everything and whitelists only labels —
hiding the very objects that flows should be controlling.

### The white rectangle

When `skipUIDraw` was removed (allowing `TheUI->Draw()` to run on game_screen), flows
that can't find their targets can't hide background meshes like `pose_flashcard_bg.mesh`.
These meshes stay at their default `showing=true` state with white materials, producing
white rectangles. Different songs trigger different flows, which is why starships shows
the rectangle but glitterati/dclive don't.

## Root Cause

The hacks are the problem, not the pipeline. The ObjPtr_p.h parent dir fallback
(already in place) is a workaround for the scope split. Once the scope split is
removed, flows find their targets naturally.

## Fix Plan

### Step 1: Remove gNativeHudDir loading hack

**File**: `src/App.cpp` (~lines 1200-1244)

Remove the entire `if (!gNativeHudDir && ...)` block that directly loads the HUD via
`DirLoader::LoadObjects()`. The HUD is already loaded by `GameModeMerger.fm`.

Also remove the static variable declaration at line 40.

### Step 2: Remove manual HUD drawing code

**File**: `src/App.cpp` (~lines 1499-1621)

Remove the entire `if (gNativeHudDir && ...)` block that:
- Hides all drawables, whitelists labels
- Forces alpha to 1.0
- Manually repositions scores
- Draws via `rdir->DrawShowing()`

With the hack removed, `TheUI->Draw()` handles game_screen panels (including the
FileMerger-merged HUD). The DTA flow animations control visibility/alpha/positioning.

### Step 3: Restore skipUIDraw removal

The `skipUIDraw` was already removed in this session. Keep it removed — `TheUI->Draw()`
should run on game_screen.

### Step 4: Test across venues/songs

Test with multiple venues AND songs to verify:
- No white rectangles (flows control visibility)
- HUD labels visible (song name, artist, scores)
- Phrase meters accessible to MoveDir::Enter
- Flashcard dock panel renders when active
- No crashes from null gNativeHudDir references

Songs to test: glitterati, dclive, starships (the one that showed white rectangles).

### Step 5: Clean up related code

After gNativeHudDir removal is verified:
- Remove any remaining references to `gNativeHudDir` in App.cpp
- Update `docs/native/TODO.md` — mark flashcard rendering as fixed
- Update `docs/native/HACK_AUDIT.md` — mark gNativeHudDir removal
- The ObjPtr_p.h parent dir fallback can stay (harmless safety net)

## Related Work Threads

### Phrase meters (#1)
- **Objects**: phrase_meter0/1 are in venue .milo (serialized HamPhraseMeter)
- **Lookup**: MoveDir::Enter, HamDirector::SetPlayerSpotlightsEnabled, RhythmBattlePlayer
- **Flow**: Hide_Boxyman_Feedback1.flow, Show_Boxyman_Feedback1.flow, Start1.flow
- **Status**: Flows can't find them due to scope split. Fix: remove scope split.
- **After fix**: Flows in merged HUD can find phrase_meters in merged venue (same world ObjectDir)

### Flashcard rendering (#2)
- **Objects**: flashcard_00-03, pose_flashcard_bg.mesh in HUD .milo
- **Panel**: `flashcard_dock_panel` is panel[6] of game_screen
- **Flow**: set_color1.flow, mind_control1.flow, pose_fatalities1.flow
- **Status**: `skipUIDraw` was removed. White rectangles appear on some songs because
  flows can't hide background meshes. Fix: remove scope split so flows find targets.
- **After fix**: Flows control flashcard visibility; TheUI->Draw() renders them

### Bone garbage (#3)
- **Fix applied**: `mOffset.Reset()` in RndBone constructor (native-only)
- **Diagnostic**: BoneSetup.cpp now logs garbage WorldXfm (one-shot, 20 max)
- **Status**: Zero garbage logged on glitterati + dclive. Root cause was uninitialized
  bind-pose offset matrix being multiplied with valid world transforms.
- **Remaining**: If garbage reappears on other venues, the diagnostic will identify
  which bones are affected.

## Architecture After Fix

```
world ObjectDir (FileMerger-managed, single source of truth)
├── venue objects (phrase_meter0, phrase_meter1, meshes, lights, ...)
├── song objects (song.anim, clips, ...)
├── game_hud objects (flashcard_00-03, pose_flashcard_bg.mesh, scores, ...)
│   └── flows (Start1.flow, set_color1.flow, ...)
│       └── mTargets resolve against world ObjectDir → finds everything
├── modular objects (moves, choreography, ...)
└── ...

TheUI->Draw() draws game_screen panels:
  game_panel, world_panel, rhythm_detector_panel, bustamove_visualizer_panel,
  bustamove_panel, flashcard_dock_panel, fitness_hud_panel

No gNativeHudDir. No manual drawing. No skipUIDraw. Flows control visibility.
```

## Files to Modify

| File | Changes |
|------|---------|
| `src/App.cpp` | Remove gNativeHudDir declaration, loading, drawing (~100 lines) |
| `docs/native/TODO.md` | Update items 8.4 (flashcard, phrase meter) |
| `docs/native/HACK_AUDIT.md` | Document gNativeHudDir removal |

## Risks

- **DTA flows not controlling all elements**: Some HUD elements may need their
  visibility set by DTA handlers that don't fire on native. If labels/scores disappear,
  we may need targeted fixups (but on the merged world ObjectDir, not a separate one).
- **Score display positioning**: The manual score positioning code may be needed if
  the DTA flow system doesn't position scores correctly. Could add targeted fixups
  in a panel Enter handler rather than every-frame manual positioning.
- **Double Enter()**: If gNativeHudDir was calling Enter() on the standalone copy,
  removing it means only the merged copy gets Enter(). This should be correct — the
  merged HUD gets Enter() through the panel machinery.

---

## Investigation Results (2026-03-17)

### What was attempted

1. **Removed gNativeHudDir loading** (App.cpp ~1201-1398) — ~200 lines of standalone
   HUD loading, score.milo loading, visibility management, text label setup.

2. **Removed gNativeHudDir drawing** (App.cpp ~1486-1608) — ~130 lines of every-frame
   manual rendering with visibility overrides, alpha forcing, score repositioning.

3. **Removed skipUIDraw** — let `TheUI->Draw()` run on game_screen.

4. **Added FileMerger parent dir** (FileMerger.cpp:411) — pass `Dir()` as parent dir
   to DirLoader so ObjPtr fallback can reach the world ObjectDir during deserialization.

### Visual result: REGRESSION

GPU screenshots show scanline/noise overlay covering the entire frame. Song name and
artist labels are gone. The venue renders underneath but is obscured by HUD overlays.

**Root cause**: The HUD .milo contains ~130+ objects referenced by Flow animations.
These flows set `showing`, `alpha`, position, and other properties on their targets
to control what's visible at each moment during gameplay. When the targets can't be
found, the flows do nothing, and HUD elements stay at their DEFAULT state — which
includes fullscreen overlay meshes (PostProcer, camera.mesh, blacken.mesh,
scanline-effect meshes) at `showing=true`.

### Why the parent dir fix wasn't sufficient

The parent dir fix (`Dir()` passed to DirLoader) is correct but insufficient because:

1. **Load ordering**: game_hud loads BEFORE the venue. When HUD flows deserialize
   their `ObjPtrVec<Hmx::Object> mTargets`, `phrase_meter0` doesn't exist yet —
   the venue hasn't been merged into the world ObjectDir.

   ```
   OnFileLoaded("song"):
     mGameModeMerger->StartLoad()   ← HUD starts loading FIRST
     mMerger->StartLoad()           ← venue starts loading SECOND
   ```

2. **Sibling scope**: Even with the parent dir, some HUD objects are in sibling subdirs
   of the flow. `FindObject(name, false, true)` searches the flow's own dir + subdirs,
   and the fallback walks UP to the parent. But sibling subdirs are never searched.

3. **Non-existent objects**: Many targets (campaign sounds, game-mode animations,
   minigame labels) genuinely don't exist on native because those subsystems are
   stubbed. There are ~130 unique missing objects — most are harmless, but fullscreen
   overlays like `pose_flashcard_bg.mesh` cause visible artifacts.

### What's still in the codebase

The following changes from this session are STILL APPLIED and should be kept:

| Change | File | Status |
|--------|------|--------|
| FileMerger parent dir for DirLoader | `FileMerger.cpp:411` | **KEEP** — correct fix, helps ObjPtr resolution |
| RndBone mOffset identity init | `Mesh.h:28` | **KEEP** — fixes bone garbage root cause |
| BoneSetup diagnostic logging | `BoneSetup.cpp:16` | **KEEP** — tracks remaining garbage bones |
| skipUIDraw removal | `App.cpp:~1285` | **NEEDS REVERT** if gNativeHudDir is restored |
| gNativeHudDir loading removal | `App.cpp:~1201` | **NEEDS REVERT** |
| gNativeHudDir drawing removal | `App.cpp:~1288` | **NEEDS REVERT** |

### How to revert gNativeHudDir removal

If we need to restore the hack while working on the real fix:

1. **Restore `gNativeHudDir` declaration** (App.cpp line 40):
   Replace the comment with: `static ObjectDir *gNativeHudDir = nullptr;`

2. **Restore loading block** (App.cpp ~line 1201):
   The full loading block (~200 lines) was in the git history at commit `b9719618e`.
   Extract with: `git show b9719618e:src/App.cpp` and copy lines 1201-1398.

3. **Restore drawing block** (App.cpp ~line 1288):
   Currently replaced with a one-line comment. Restore from same commit, lines 1486-1608.

4. **Restore skipUIDraw** (App.cpp ~line 1285):
   Replace `if (TheUI && !getenv(...))` with the full `skipUIDraw` block from commit
   `b9719618e`, lines 1477-1493.

Or simply: `git checkout b9719618e -- src/App.cpp` then re-apply only the changes we
want to keep (FileMerger parent dir is in a different file).

### The real fix: two options

**Option A: Make flow targets available on native** (preferred)

The HUD flows reference ~130 objects. Categorized:

| Category | Count | Examples | Fix |
|----------|-------|---------|-----|
| Venue objects | 2 | phrase_meter0/1 | Load ordering — venue must merge BEFORE HUD |
| HUD sibling-scope objects | ~15 | flashcard_00-03, move_feedback0/1, text_feedback0/1, pose_flashcard_bg.mesh | Fix FindObject scope or restructure merge |
| Game-mode animations | ~40 | stars_0-6.anim, enter_gem01-04.anim, show_difficulty.anim | Load the animations (they're in the HUD .milo) |
| Labels/materials | ~30 | score1/2.lbl, gamertag1/2.lbl, diamond/emerald.mat | Same — should be in HUD .milo |
| Campaign-specific sounds | ~5 | campaign_scene10-1_vo_*.snd | Not needed — can be suppressed |
| Particle effects | ~4 | timeMachine_*.part, spiral_emit.mesh | Not needed initially |

The key insight: most of these objects SHOULD be in the HUD .milo. The fact that they
can't be found suggests the merge scope is wrong (sibling subdir issue), not that they
don't exist. If we fix FindObject to also search sibling subdirs during deserialization,
most of these would resolve.

**Option B: Post-merge ObjPtr re-resolution pass** (less ideal)

After `FileMerger::FinishLoading` merges the HUD into the world ObjectDir, walk through
all objects in the merged dir. For any ObjPtrVec with null entries, try to re-resolve
the object name against the merged scope.

Problem: ObjPtrVec doesn't store the original name after Load — it only stores the
resolved pointer (or null). Would need to add name storage or re-serialize and
re-deserialize.

This approach diverges from Xbox behavior and adds complexity. Option A is preferred.

### `$hud_panel` DataVariable

Confirmed: the DTA `enter` handler at `hud_objects.dta:162` sets `$hud_panel = $this`.
This means `hud_panel` is set automatically when the HUD's Enter() runs, regardless of
whether gNativeHudDir exists. This is used by GamePanel, BustAMovePanel, SongSequence,
HamMove, RhythmBattle, HollaBackMinigame, PoseFatalities — all via
`DataVariable("hud_panel").Obj<ObjectDir>()`.
