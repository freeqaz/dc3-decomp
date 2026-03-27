# Session: Flashcard Crash Fixes

**Date**: 2026-03-26
**Status**: Partial — gameplay crashes fixed, rendering still black

## Summary

Fixed three crashes that prevented the native engine from reaching stable gameplay state. The engine now runs through 3000 frames of "Better Off Alone" gameplay without crashing. Move data loads correctly (92 move keys, moveInterpActive=1). However, the gameplay screen renders black — a pre-existing rendering issue, not related to flashcards or DTA.

## Crashes Fixed

### 1. WriteSceneUniforms AmbientFogOwner crash (SIGSEGV at 0x1d8)

**Root cause**: During game_screen transition, `RndEnviron::Current()` returns a valid env but its `mAmbientFogOwner` pointer is stale/invalid. `AmbientColor()`, `FogEnable()`, etc. all dereference `mAmbientFogOwner` without null checks.

**Fix**: Added `env->AmbientFogOwner()` getter and null guard in `WriteSceneUniforms`:
```cpp
if (env && env->AmbientFogOwner()) { ... }
```

**Files**: `native/src/platform/Rnd_Wgpu.cpp`, `src/system/rndobj/Env.h`

### 2. $hud_panel pointing to wrong PanelDir (SIGSEGV at 0x10)

**Root cause**: The WorldDir's inline mHUD PanelDir (which also has the "hud" type) enters and its DTA `enter` handler overwrites `$hud_panel` with itself. This mHUD lacks merged content (hud_left, hud_right, flash_cards). When `OnSelectCamera` fires `songAnim->SetFrame()`, the `move_interp` handler runs on the wrong PanelDir — one without valid `player_huds`.

**Fix**: In `OnSelectCamera`, restore `$hud_panel` to the merged game_hud PanelDir before calling `SetFrame`:
```cpp
if (mGameModeMerger) {
    FileMerger::Merger *gm = mGameModeMerger->FindMerger("game_hud", false);
    PanelDir *mergerHud = gm ? dynamic_cast<PanelDir*>(gm->MergerDir()) : nullptr;
    if (mergerHud) DataVariable("hud_panel") = (Hmx::Object*)mergerHud;
}
```

**File**: `src/system/hamobj/HamDirector.cpp`

### 3. DataGetElem null DataArray crash (SIGSEGV at 0x10)

**Root cause**: In single-player mode, `{gamedata getp 1 provider}` returns null (no second player). The `get_player_hud` function returns empty string. The `move_interp` handler then tries `{elem {$hud get (flash_cards)} $card_index}` where `$hud` is empty — `{$hud get ...}` returns null, and `{elem null ...}` crashes in `DataArray::Node()` on the null array pointer.

**Fix**: Added native-only null guard in `DataGetElem`:
```cpp
DataArray *a = array->Array(1);
if (!a) { MILO_WARN("elem: null array"); return DataNode(0); }
```

**File**: `src/system/obj/DataFunc.cpp`

## Other Fixes

- **HamListRibbon.cpp**: Added `#include "rndobj/EventTrigger.h"` (later removed — the existing code was cleaned up by concurrent work)

## What Works

- Engine boots, navigates to gameplay, and runs stably for 3000+ frames
- Move data loads correctly: 92 move keys, 36 parents, 255 dance entries
- `moveInterpActive=1` — the move_interp handler fires
- `nativeSetFrameCount` increments — the SetFrame fix from 2026-03-25 is active
- Song animation frame advances through gameplay
- No more crashes

## What Doesn't Work Yet

### Gameplay screen is black
All screenshots during gameplay show a black screen with only "SKIP" text. The venue scene and HUD meshes are not rendering despite correct game state. This is a pre-existing rendering issue — likely related to:
- Camera not being selected from the venue scene
- Environment setup being skipped (due to AmbientFogOwner guard)
- Draw call pipeline not executing venue meshes

### Flashcards not visually confirmed
Because the screen is black, we can't visually confirm flashcard rendering. The DTA data flow is correct (move_interp fires, player_huds populated), but we need the rendering pipeline working to see actual flashcard cards.

## Diagnosis Chain

The crash sequence was:
1. `WorldDir::Poll()` → `HandleType("select_camera")`
2. → `HamDirector::OnSelectCamera()` → `songAnim->SetFrame(frame, blend)`
3. → `SymbolKeys::SetFrame` → fires `move_interp` on HamDirector
4. → `HandleType` dispatches to venue type's `move_interp`
5. → `{if $hud_panel {$hud_panel move_interp ...}}` forwards to HUD PanelDir
6. → HUD's `move_interp` calls `{$this get_player_hud 1}` → null (no P2 provider)
7. → `{$hud set_anim_frame}` or `{elem {$hud get flash_cards}}` → crash

Key discovery: `$hud_panel` was being overwritten by the wrong PanelDir between `OnFileMerged` (which sets it correctly) and `OnSelectCamera` (which uses it).

## Next Steps

1. Fix the black gameplay screen — investigate why venue meshes aren't drawing
2. Once rendering works, verify flashcard cards match Xbox reference screenshots
3. Remove the GamePanel.cpp flashcard hack (may no longer be needed with SetFrame fix)
4. Fine-tune flashcard X/Z positions for proper visual layout
