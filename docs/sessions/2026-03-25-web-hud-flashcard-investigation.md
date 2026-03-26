# Session: Web HUD Flashcard Investigation

**Date**: 2026-03-25
**Focus**: Dance move card sync and render failures on the web build

## Summary

Two distinct issues were under investigation:

1. Flashcards were not staying in sync with the song.
2. Flashcards did not render reliably on web. Better Off Alone showed no move cards in the browser repro, and prior reports said YMCA could show only the translucent container.

The sync bug is now understood and fixed in shared `HX_NATIVE` code. Native Better Off Alone now advances flashcards from the proper routine-builder anim and renders the full three-card stack again.

The remaining browser problem was diagnosed as a **move name resolution failure**: the DanceRemixer populates the routine builder anim with all-rest move keys because the move graph doesn't resolve actual dance variants during `SaveOriginalMoveParents`. A fallback was added to `MoveNameFromBeat` that checks the authored difficulty song.anim when the routine builder returns rest moves.

## Root Cause Analysis (Web Flashcard Failure)

### Diagnostic Results

Using targeted C++ diagnostics in the web (Emscripten) build, we confirmed:

1. **HUD objects exist and are valid**: `$hud_panel` is set, `hud_left`/`hud_right` exist, 4 flash_card children found, all showing=1
2. **Song anim pipeline works**: `player_1_routine_builder.anim` has 92 move keys with `interp_handler='move_interp'`, frame advances correctly
3. **All move keys are `Rest.move`**: Every one of the 92 keys in the routine builder has value `Rest.move`
4. **`InsertMoveInSong` writes rest variants**: Confirmed `clip='rest_betteroffalone_v1000'`, `move='Rest.move'` for all measures
5. **The C++ flashcard hack is not needed**: `NativeHudFlashcardHackRequired()` returns false (routine builder has keys)

### The Failure Chain

```
DanceRemixer::Reset()
  → OriginalChoreoRemixer::SelectMove(player, measure)
    → GetMoveParentsByDifficulty(difficulty)[measure]
      → Returns rest parents for ALL measures (not just intro/outro)
    → AddRoutineMove(player, measure, restParent, restVariant)
      → FillInRoutineAt → finds rest variant
      → InsertMoveInSong → writes "Rest.move" key to routine builder
```

The `GetMoveParentsByDifficulty` returns rest parents because `SaveOriginalMoveParents` failed to find actual dance variants in the move graph via `FindMoveByVariantName`. The layout has variant names per difficulty, but the move graph's `mMoveVariants` map doesn't contain the matching entries — likely because the DTA-driven initialization flow that populates the graph is incomplete on native/web.

### Why `hidden=true` on All Flashcards

In `update_flashcards` DTA handler:
```dta
{$flash_card set hidden {'||' {! $my_move} {$my_move is_rest}}}
```
When `move_from_beat` → `MoveNameFromBeat` returns "Rest.move" → `get_move` finds the Rest HamMove → `is_rest` is true → `hidden = true` → `{all.grp set_showing FALSE}`.

So the flashcard `RndDir` objects are showing=1 (the outer container), but their `all.grp` internal group is hidden because every card has a rest move assigned.

## Fix Applied

### `HamDirector::MoveNameFromBeat` fallback (HamDirector.cpp)

When the routine builder's move keys return "Rest.move" during gameplay (beat >= 0), fall back to the authored difficulty song.anim which has the original move choreography baked in:

```cpp
#ifdef HX_NATIVE
// HACK(native): the DanceRemixer may populate the routine builder with
// all-rest move keys when the move graph or layout doesn't resolve
// properly on native/web. Fall back to the authored difficulty song.anim
// which has the original move choreography.
static Symbol sRest("Rest.move");
if (ret == sRest && beat >= 0.0f) {
    HamPlayerData *hpd = TheGameData ? TheGameData->Player(player) : nullptr;
    if (hpd) {
        RndPropAnim *authoredAnim =
            SongAnimByDifficulty(LegacyDifficulty(hpd->GetDifficulty()));
        if (authoredAnim && authoredAnim != anim) {
            PropKeys *authoredKeys =
                authoredAnim->GetKeys(this, DataArrayPtr(Symbol("move")));
            if (authoredKeys) {
                Symbol authoredRet;
                authoredKeys->AsSymbolKeys()->AtFrame(frame, authoredRet);
                if (authoredRet != sRest && !authoredRet.Null()) {
                    ret = authoredRet;
                }
            }
        }
    }
}
#endif
```

**Limitation**: This fix assumes the authored song.anim has actual dance move names in its `move` prop keys (not rest placeholders). If perform mode songs have all-rest `move` keys in the authored anim too (since the remixer is supposed to generate them), this fallback won't help. In that case, the fix needs to go deeper — either fixing the move graph loading or bypassing the remixer entirely.

### Diagnostic added to `OriginalChoreoRemixer::SaveOriginalMoveParents`

Logs the count of null/rest/dance variants found, plus `mIntroMoveIndex` and `mFinalPoseMoveIndex`. This will help diagnose whether the move graph has the right data when the game next reaches gameplay.

## What We Confirmed

### 1. The move timeline is driven by DTA through `HamDirector.move`

This path is correct:

- `song.anim` animates `HamDirector.move`
- `HamDirector.move` has `interp_handlers move_interp`
- `world_objects.dta` forwards `HamDirector.move_interp` to `$hud_panel move_interp`
- `hud_objects.dta` derives current/next flashcards from `{$hamdirector player_song_anim 0}`

### 2. The real sync bug was the wrong anim being populated

(Same as before — see `MoveMgr::InsertMoveInSong` native hack)

### 3. The `move_interp` interp handler fires correctly on web

The routine builder anim has `interpHandler='move_interp'` on its `move` prop key (inherited from the expert song.anim copy). `OnSelectCamera` fires every frame, calling `songAnim->SetFrame()` which evaluates prop keys and fires the interp handler. This forwards to the HUD's `move_interp` → `update_flashcards`.

### 4. The flashcard objects are correctly wired on web

- `$hud_panel` is valid
- `hud_left`/`hud_right` exist with 4 flash_card children each
- `[player_huds]` is populated (HUD enter handler fires)
- The DTA `update_flashcards` → `set_move` → `update_flashcard_move` path runs

### 5. `WorldDir::Poll()` fires `select_camera` every frame on web

Confirmed via `OnSelectCamera` log output.

## Relevant Files

- `src/system/hamobj/HamDirector.cpp` — `MoveNameFromBeat` fallback
- `src/system/hamobj/MoveMgr.cpp` — `InsertMoveInSong` native hack
- `src/system/hamobj/OriginalChoreoRemixer.cpp` — `SaveOriginalMoveParents` diagnostic
- `src/system/hamobj/DanceRemixer.cpp` — `AddRoutineMove` (calls InsertMoveInSong)
- `src/lazer/game/GamePanel.cpp` — C++ flashcard refresh hack

## Recommended Next Steps

1. **Verify the authored song.anim has real moves**: Check if the expert difficulty song.anim's `move` prop keys have actual dance move names or all-rest placeholders. If they're all rest, the `MoveNameFromBeat` fallback won't help.

2. **Fix the move graph loading**: The deeper fix is to ensure the move graph's `mMoveVariants` map contains all dance variants from the song's `move_data` dir. Trace `MoveGraph::Copy(dirGraph, kCopyDeep)` to verify the deep copy populates `mMoveVariants` correctly.

3. **Alternative: bypass the remixer entirely**: For native/web, instead of relying on the DTA-driven remixer, populate the routine builder's move keys directly from the layout data during `SetupRoutineBuilderAnims` or `Init`.

4. **Fix the web navigation test**: The Playwright-based gameplay.mjs test is flaky — works for some builds but fails to navigate past song select on others. May be a timing/focus issue.
