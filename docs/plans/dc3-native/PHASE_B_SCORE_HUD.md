# Phase B: Score / HUD Display

**Status**: Research complete — gameplay-only, blocked without input pipeline
**Last Updated**: 2026-03-16

## Finding

Score display in DC3 is **exclusively a gameplay feature**. There are no score elements on main_screen, title_screen, or choose_mode_screen.

### Score Pipeline
```
Gameplay:  RhythmBattlePlayer.mScore → HamPhraseMeter → HUD labels
End game:  Game.GetResult() → SongStatusData → SongStatusMgr (per-difficulty)
Profile:   HamProfile.UpdateScore() → MetagameRank.AwardPoints() (XP)
Menu:      HamStarsDisplay.SetSongImpl() → star rating display (song select only)
```

### Score Display Components (Gameplay Only)
- `RhythmBattlePlayer.mScoreLabel` — live score HamLabel
- `HamPhraseMeter` — progress bar per move
- `CharFeedback` — limb flash overlay
- `text_feedback0/1` — move rating text

### Menu-Visible Score Elements
- **Star ratings** on song select screen (HamStarsDisplay)
- **Rank/tier** on main menu (MetagameRank → player.ep PropertyEventProvider)
- **Fitness calories** on fitness screens

### Blockers
1. Gameplay mode requires input scripting to reach
2. Live scoring requires gesture detection (Kinect or substitute)
3. Star ratings require saved profile data (SongStatusMgr)

### Recommendation
Deprioritize until gameplay input pipeline exists. Menu-visible rank/tier display already works through PropertyEventProvider. Star ratings would only show on song_select_screen.

## Key Files
| File | Purpose |
|------|---------|
| `src/system/hamobj/ScoreUtl.cpp` | Rating conversion, score bonuses |
| `src/lazer/meta_ham/SongStatusMgr.h` | Per-song score storage |
| `src/lazer/meta_ham/HamProfile.h` | Player profile, UpdateScore() |
| `src/lazer/meta_ham/MetagameRank.h` | XP/level system |
| `src/lazer/meta_ham/HamStarsDisplay.cpp` | Star rating display (song select) |
| `src/system/hamobj/RhythmBattlePlayer.h` | Live gameplay score tracking |
| `src/system/hamobj/HamPhraseMeter.h` | Move progress bar |
