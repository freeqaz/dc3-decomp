# HUD Rendering Pipeline — Phrase Meters, Move Cards, Scoring

## Status (2026-03-16)

| Component | Status | Notes |
|-----------|--------|-------|
| Phrase meter rings | **Rendering** | 29 draws per meter, white arc rings visible |
| Score display | Visible (stuck at 0) | HUD labels render, no scoring events |
| Move flashcards | Not rendering | flashcard_dock_panel not drawn (skipUIDraw) |
| Song info | Rendering | "Boyfriend - Justin Bieber" visible |
| Game state | Stuck on intro | DC3 logo never transitions to gameplay |

## Phrase Meter Fix (Root Cause + Solution)

### Root Cause
After `MergeDirs(pmDir, venue, filter)`, phrase_meter0/1 (HamPhraseMeter objects)
are regular objects in the venue's hash table — NOT subdirectories. Venue's
`SyncDrawables()` adds them to `venue->mDraws`, so they get drawn. But
phrase_meter0/1's **own** `mDraws` lists are empty — their meshes (29 drawables:
feedback_ring_01-12, bullseye, shockwave, etc.) live in their own hash tables,
and `SyncObjects()` was never called on them.

When venue draws phrase_meter0 → `DrawShowing()` → iterates empty `mDraws` → nothing.

### Solution
Call `SyncObjects()` on each phrase_meter after venue's SyncObjects:
```cpp
// In GamePanel::PollForLoading, after venue->SyncObjects():
HamPhraseMeter *pm0 = venue->Find<HamPhraseMeter>("phrase_meter0", false);
if (pm0) pm0->SyncObjects();  // Populates pm0->mDraws with its 29 meshes
```

### Why They're Not Subdirs
`MergeFilter::DefaultSubdirAction` with `kMergeInlinedMoveSharedSubdirs`:
- `kInlineNever` or `kInlineCachedShared` → `kMergeReplace` (append as subdir)
- Other types → `kMergeMerge` (flatten contents)

But phrase_meter0/1 aren't subdirs of phrase_meter.milo's top-level dir — they're
regular objects in its hash table. So MergeObjectsRecurse moves them to venue via
`SetName(name, venue)`, making them regular objects in venue's hash table.

If they WERE subdirs, `ObjDirItr<RndDrawable>(venue, true)` in `SyncDrawables`
would recurse into them and directly add their meshes to venue's mDraws. But as
regular objects, they're treated as standalone RndDrawables (whose DrawShowing
draws their own mDraws).

## Rendering Pipeline

```
DirLoader::LoadObjects("phrase_meter.milo")
  → ObjectDir with phrase_meter0/1 as objects (not subdirs)

MergeDirs(pmDir, venue, kMergeInlinedMoveSharedSubdirs)
  → MergeObject moves phrase_meter0/1 to venue's hash table
  → phrase_meter0/1 own hash tables still contain meshes

venue->SyncObjects()
  → SyncDrawables → ObjDirItr(venue, true)
  → Adds phrase_meter0/1 (RndDrawable) to venue->mDraws
  → phrase_meter0/1's own mDraws are empty!

pm0->SyncObjects()  ← THE FIX
  → SyncDrawables → ObjDirItr(pm0, true)
  → Populates pm0->mDraws with 29 meshes

Venue draws → pm0->DrawShowing() → iterates pm0->mDraws → meshes render
```

## Key Data Structures

| Object | Location | Contents |
|--------|----------|----------|
| venue (WorldDir) | Top-level | mDraws = ~1759 drawables including pm0/pm1 |
| phrase_meter0 (HamPhraseMeter) | venue hash table | Own hash table with 29 drawables |
| phrase_meter1 (HamPhraseMeter) | venue hash table | Own hash table with 29 drawables |
| feedback_ring_01-12.mesh | pm0/pm1 hash table | Compressed Xbox vertices |
| bullseye.mesh | pm0/pm1 hash table | Center bullseye target |
| shockwave.mesh | pm0/pm1 hash table | Hit effect animation |

## Remaining Work

### 1. Game State Progression (Intro → Gameplay)
The game is stuck on the intro screen (DC3 logo). `StartIntro()` is called and
`mState = kGameInIntro`, but the game never transitions to active gameplay.
Likely cause: DTA `pick_intro` handler expects a timer/callback that isn't
firing, or the intro animation sequence isn't completing.

### 2. Phrase Meter Animation
Currently static (default animation frame). Need to:
- Call `HamPhraseMeter::SetRatingFrac()` to drive progress
- Call `HamPhraseMeter::Poll()` to animate toward target frame
- Wire to scoring system (MoveDir → HamPhraseMeter::SetBounds)

### 3. Move Flashcards
UI draw is skipped on game_screen (skipUIDraw flag). The flashcard_dock_panel
exists but isn't rendering. Need to investigate the UI draw path and ensure
move card textures are loaded and rendered.

### 4. Scoring
Score is stuck at 0. Autoplay generates moves but no scoring events are being
produced. The scoring pipeline (MoveDir → Scorer → HamGameData) needs to be
connected.

## Files Modified

- `src/lazer/game/GamePanel.cpp` — phrase_meter loading + SyncObjects fix
- `src/system/hamobj/HamDirector.cpp` — SetPlayerSpotlightsEnabled native guard
- `src/App.cpp` — HUD loading, venue setup
