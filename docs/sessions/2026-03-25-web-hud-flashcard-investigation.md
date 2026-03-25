# Session: Web HUD Flashcard Investigation

**Date**: 2026-03-25  
**Focus**: Dance move card sync and render failures on the web build

## Summary

Two distinct issues were under investigation:

1. Flashcards were not staying in sync with the song.
2. Flashcards did not render reliably on web. Better Off Alone showed no move cards in the browser repro, and prior reports said YMCA could show only the translucent container.

The sync bug is now understood and fixed in shared `HX_NATIVE` code. Native Better Off Alone now advances flashcards from the proper routine-builder anim and renders the full three-card stack again.

The remaining browser problem appears to be a web-only flashcard render/display issue, not the original remixer timing bug.

## What We Confirmed

### 1. The move timeline is driven by DTA through `HamDirector.move`

This path is correct:

- `song.anim` animates `HamDirector.move`
- `HamDirector.move` has `interp_handlers move_interp`
- `world_objects.dta` forwards `HamDirector.move_interp` to `$hud_panel move_interp`
- `hud_objects.dta` derives current/next flashcards from `{$hamdirector player_song_anim 0}`

So the HUD should be driven by DTA callbacks from the active `player_song_anim`, not by ad hoc C++ timing.

### 2. The real sync bug was the wrong anim being populated

In perform mode, `merge_moves=1` means the game should populate the routine-builder anims:

- `player_1_routine_builder.anim`
- `player_2_routine_builder.anim`

However, native/web `HamDirector::SongAnim()` had a fallback that returned the authored difficulty `song.anim` when the routine-builder anim had no clip keys yet. That fallback was useful for clip playback, but it also caused `MoveMgr::InsertMoveInSong()` to write the initial remixer keys into the authored `song.anim` instead of the routine-builder anim.

Result:

- `player_song_anim` stayed effectively empty for `move`
- HUD `move_interp` never got the intended routine timeline
- flashcard transitions drifted or stalled

### 3. Native fix applied

In `src/system/hamobj/MoveMgr.cpp`, under `#ifdef HX_NATIVE`, `InsertMoveInSong()` now writes directly to the routine-builder anim when `merge_moves=1`, instead of going through `SongAnim()`.

This is guarded and documented as a temporary native/web hack:

- `src/system/hamobj/MoveMgr.cpp`

The existing C++ HUD refresh workaround in `src/lazer/game/GamePanel.cpp` was also tightened so it only runs when the routine-builder `move` keys are actually missing. Once the real timeline exists, the hack backs off automatically.

Relevant files:

- `src/system/hamobj/MoveMgr.cpp`
- `src/lazer/game/GamePanel.cpp`
- `src/system/hamobj/HamDirector.cpp`
- `src/lazer/game/GamePanel.h`

All hacks added during this investigation were kept under `#ifdef HX_NATIVE` with cleanup comments.

## Native Verification

Native Better Off Alone now behaves correctly enough to establish a good reference:

- full three-card flashcard stack is visible again
- top card is present
- routine-builder anim is populated
- HUD transitions are driven from the routine timeline instead of the authored fallback

HTTP debug checks on native showed:

- `{{$hamdirector player_song_anim 0} num_keys $hamdirector (move)}` => populated
- `{{$hamdirector player_song_anim 0} num_keys $hamdirector (clip)}` => populated

A native screenshot captured after the fix shows the expected left/right flashcard stacks and top cards.

## Web Verification

Web repro steps used:

- existing server on port `8420`
- `node scripts/web/gameplay.mjs --port 8420 --song-index 3 --timeout 90 --hang-timeout 20 --out /tmp/dc3-web-gameplay-boa`

Current song index `3` maps to Better Off Alone on this checkout.

The browser run reached gameplay successfully and produced:

- screenshot: `/tmp/dc3-web-gameplay-boa/gameplay.png`
- logs: `/tmp/dc3-web-gameplay-boa/console.jsonl`

### What the web screenshot shows

- score numbers render
- world render is fine
- character FX render
- flashcards do not render

So the web bug is not "HUD missing entirely". It is narrower: the score HUD draws, but the flashcard dock/cards do not.

### Important log observations

At `world_panel` entry, web still logs:

- `SongAnim(0): routine builder empty, falling back to expert anim`
- repeated `cur_move` script errors from `ui/hud/hud_objects.dta`

However, native shows the same early messages and still recovers to the correct on-screen flashcards later. That means these initial logs are not sufficient to explain the remaining browser-only failure by themselves.

## Current Interpretation

The original timing bug and the remaining browser render failure are related, but not identical.

What is now most likely:

1. The remixer/routine-builder sync issue was real and is fixed for the shared native/web path.
2. Native now proves that the flashcard logic can recover and render correctly after the early fallback noise.
3. Web still fails to display the flashcards even though the rest of the HUD is drawing.

That strongly suggests the remaining issue is in the flashcard-specific display/render path on web, not in the top-level HUD merge or score HUD path.

## Narrowed Remaining Hypotheses

### Hypothesis A: flashcard objects are present but not visually drawing on web

Evidence:

- score HUD renders
- browser screenshot shows gameplay HUD is active
- only flashcards are absent

Possible causes:

- flashcard dock child objects are hidden or animated to a bad state on web
- flashcard materials/textures are not resolving or not presenting
- a flashcard-specific render state differs between native desktop and Emscripten/WebGPU

### Hypothesis B: flashcard cards exist logically, but their move object binding stays invalid longer on web

Evidence:

- DTA `update_flashcard_move` requires `[cur_move]` to be a real move object
- web still logs repeated `cur_move` failures

Counterpoint:

- native logs the same early errors and still eventually shows the cards

So if this is still part of the problem, it is likely an ordering difference after panel entry, not the initial fallback itself.

## Useful References

- `docs/debugging/web.md`
- `docs/tools/HTTP_DEBUG_SERVER.md`
- `src/system/hamobj/MoveMgr.cpp`
- `src/system/hamobj/HamDirector.cpp`
- `src/lazer/game/GamePanel.cpp`
- `orig-assets/extracted/world/world_objects.dta`
- `orig-assets/extracted/ui/hud/hud_objects.dta`

## Recommended Next Step

The next pass should focus only on the browser-specific flashcard display path:

1. instrument flashcard dock/card visibility and current move assignment on web
2. confirm whether the flashcard objects are being updated but not drawn, or never updated into a visible state
3. compare flashcard-specific object state between native-good and web-bad runs

At this point, the remaining problem is narrow enough that more work on generic DTA trigger timing is unlikely to pay off first.
