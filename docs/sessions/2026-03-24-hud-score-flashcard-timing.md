# Session: HUD Score Display + Flashcard Timing Investigation

**Date**: 2026-03-24
**Focus**: Wire up score numbers on gameplay HUD, investigate flashcard timing desync

## What Was Done

### Score Display (DONE)
Wired up score numbers at top corners of the gameplay HUD. The score labels (`score1.lbl`, `score2.lbl`) exist inside `score_left`/`score_right` RndDir subdirs loaded from `hud_shared.milo`.

**Key challenges solved:**
1. **DTA files are immutable** — loaded from ark DTB, not extracted .dta text. All fixes must be in C++.
2. **Score positions outside camera frustum** — `left_score.trans`/`right_score.trans` at X=±500 were outside the HUD camera's FOV=0.6 rad view (visible: ±300). Fixed by repositioning to X=±150.
3. **Text not rendering** — `RndText::SetText()` doesn't call `UpdateText()` to rebuild mesh geometry. Need both calls.
4. **Mirrored text** — Parent transforms have X-axis flip. Fixed by negating `lx.m.x.x` on score1.lbl each frame.
5. **Overlapping shadow label** — `score2.lbl` overlaps `score1.lbl`. Hidden via `SetShowing(false)`.

**Files changed:**
- `src/lazer/game/GamePanel.cpp` — Score update in `Poll()` with cached label pointers, comma formatting
- `src/system/hamobj/HamDirector.cpp` — Reposition score transforms + trigger show-score anims in `OnFileMerged`

### Flashcard Timing (PARTIALLY INVESTIGATED)
Flashcards were appearing at wrong intervals — synced to animation clips rather than song beats.

**Root cause identified:** The HUD PanelDir's DTA `enter` handler registers beat sinks:
```dta
{if {exists master}
   {master add_sink $this (downbeat beat halfbeat quarterbeat first_beat)}}
```

The `hudDir->Enter()` call in `OnFileMerged` re-triggers this, which registers the sinks. Without Enter(), the app crashes (null deref from uninitialized HUD state).

## Architecture: HUD Loading Flow

```
director.milo loads
  └── PanelDir "hud" (type="hud" from binary) → Enter() #1 fires
       ├── "hud" DTA enter handler runs:
       │   ├── {set $hud_panel $this}
       │   ├── {set [cur_move_index] -1}
       │   ├── {if {exists master} {master add_sink $this (...)}}  ← TheMaster exists?
       │   └── {push_back [player_huds] {$this find "hud_left" FALSE}}  ← hud_left doesn't exist yet!
       └── "game_mode_hud" RndDir (placeholder)

Later: FileMerger loads _default_hud.milo → merges into "hud" PanelDir
  └── hud_shared.milo content merges in: hud_left, hud_right, score_left, score_right, etc.
  └── OnFileMerged fires → Enter() #2 fires
       ├── DTA enter handler re-runs (hud_left/hud_right now exist)
       ├── beat sinks registered
       └── BUT: flashcard state reset ({set [cur_move_index] -1})
```

## Open Questions

### 1. Does Enter() #1 actually run?
The subagent found `$hud_panel type=4` (kDataObject) BEFORE our Enter() call in OnFileMerged, proving it was already set. But we need to validate:
- Does the DTA enter handler actually execute during director.milo deserialization?
- Or is `$hud_panel` set through some other path?
- Add `{print "HUD ENTER HANDLER RUNNING\n"}` to the DTA enter block (requires DTB editing, not trivial)

### 2. Is TheMaster available during Enter() #1?
The subagent found `TheMaster` is created in `Game()` constructor (Game.cpp:77), which runs BEFORE FileMerger async loading. So `{exists master}` should be TRUE. But:
- Does the first Enter() happen during director.milo LOADING (async) or during a later Enter() pass?
- If async, TheMaster might not exist yet despite being created before StartLoad()

### 3. Why does removing Enter() #2 crash?
Without the second Enter(), SIGSEGV at address 0x10 (null + small offset). This suggests:
- `player_huds` array is empty (hud_left/hud_right not found during Enter() #1)
- Gameplay code indexes into it → crash
- The second Enter() IS needed to set up player_huds after merge

### 4. Can we avoid the double-Enter() state reset?
The second Enter() resets `[cur_move_index] = -1` and clears accumulated flashcards. Options:
- **Selective re-enter**: Only run the parts of the DTA enter that need updating (not feasible from C++ without DTA modification)
- **Don't accumulate before Enter() #2**: Ensure no flashcards are queued before the merge completes
- **Accept the reset**: If gameplay hasn't started yet when OnFileMerged fires, the reset is harmless

### 5. Score camera frustum — why is FOV so narrow?
The HUD camera (Cam.cam in hud_shared.milo) has FOV=0.6 rad (~34°). Score positions at X=±500 are WAY outside. On Xbox, these same positions would also be outside. Either:
- The Xbox HUD camera has a wider FOV configured elsewhere
- The score labels were never meant to show in standard perform mode (empty DTA handler supports this)
- There's a different viewport/camera mechanism on Xbox

### 6. Web port verification needed
The plan mentioned testing on web. The flashcard timing fix (Enter() → beat sink registration) works on desktop but needs web verification.

## Key Files Reference

| File | Purpose |
|------|---------|
| `orig-assets/extracted/ui/hud/hud_objects.dta` | DTA type "hud" definition — enter handler, set_score, update_flashcards |
| `orig-assets/extracted/ui/gameplay/perform.dta` | Game panel type "perform" — on_beat, move_passed, set_score calls |
| `orig-assets/extracted/char/char_objects.dta` | FileMerger types — load_game_hud, on_post_merge handlers |
| `world/shared/gen/director.milo_xbox` | Contains PanelDir "hud" (type="hud") + "hud1" + FileMerger |
| `ui/hud/gen/_default_hud.milo_xbox` | RndDir "game_mode_hud" — tiny, references hud_shared.milo |
| `ui/hud/gen/hud_shared.milo_xbox` | 14MB PanelDir "hud" — all HUD objects (hud_left, hud_right, score_left/right, flashcards) |
| `ui/hud/gen/score.milo_xbox` | Score display — score1.lbl, score2.lbl, glow anims |

## Milo Object Hierarchy (after merge)

```
PanelDir "hud" (type="hud", MergerDir for "game_hud")
├── Cam.cam — HUD camera (0,-768,0) +Y, FOV=0.6
├── hud_left (RndDir) — left player flashcard panel
├── hud_right (RndDir) — right player flashcard panel
├── score_left (RndDir) — left score display
│   ├── score1.lbl (HamLabel) — primary score text
│   ├── score2.lbl (HamLabel) — shadow/glow text
│   └── score.grp (Group) — draws score labels
├── score_right (RndDir) — right score display
├── left_score.trans — positions score_left
├── right_score.trans — positions score_right
├── flashcard_dock (PanelDir) — has its own camera for flashcards
├── player_huds.grp (Group) — contains hud_left, hud_right
├── show_left_score.anim — slides score into view
├── show_right_score.anim
└── ... (48 draw children total)
```
