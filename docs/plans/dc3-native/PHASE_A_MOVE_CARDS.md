# Phase A: Move Card UI Visibility

**Status**: Research complete, ready to implement
**Last Updated**: 2026-03-16

## Problem

During gameplay, HUD panels (game_panel, fitness_hud_panel) are active+showing but move card content is invisible. Move cards previously rendered as pink rectangles (empty TexMovie targets), and pose_flash meshes were filtered out (Session 72).

## Architecture

### Panel Hierarchy (game_screen)
```
game_screen (HamScreen)
  ├── game_panel (GamePanel : UIPanel)
  ├── world_panel
  ├── rhythm_detector_panel
  ├── bustamove_visualizer_panel
  ├── bustamove_panel (BustAMovePanel — move card display)
  ├── flashcard_dock_panel
  └── fitness_hud_panel
```

### Move Card Data Flow
```
Song plays → MoveDir::Poll() polls current move
  → DetectRange/DetectFrac calculates detection score
  → DetectFracToMoveRating() → Perfect/Awesome/OK/Bad
  → HamPhraseMeter::SetRatingFrac() updates progress bar
  → CharFeedback::UpdateLimb() flashes failing joints
  → BustAMovePanel::SetFlashcardImage/Text() → TexMovie texture
```

### TexMovie Render Pipeline (DrawToTexture)
```
TexMovie::DrawPreClear() → DrawToTexture()
  Native: FFmpegMovieImpl::HasDecodedFrame() → UploadRGBAToRndTex()
  Xbox:   MakeDrawTarget() → mMovie.Draw() → FinishDrawTarget()
```

### HUD Component Locations (in venue_world)
```
venue_world/
  player0/
    char_feedback.cf     (CharFeedback — limb flash overlay)
    phrase_meter0        (HamPhraseMeter — progress bar)
    text_feedback0       (RndDrawable — text label)
  player1/
    char_feedback.cf / phrase_meter1 / text_feedback1
```

## Root Cause Analysis

Move card content is invisible because:

1. **TexMovie targets have no content**: Flashcard geometry exists (`flashcard_default.mesh` on `Cam.cam`) with `flashcard_default.mat` referencing TexMovie render targets, but those textures are never written to.

2. **Two possible TexMovie content sources**:
   - **.bik video files**: FFmpegMovieImpl decode path works, but move cards may not use .bik files
   - **Scene rendering**: BustAMovePanel may draw move icons into TexMovie via MakeDrawTarget/FinishDrawTarget — these are **stubs** on native (`Tex_Wgpu.cpp:211-220`)

3. **MakeDrawTarget/FinishDrawTarget are stubs**: The Xbox path in DrawToTexture uses render-to-texture via `MakeDrawTarget()` / `FinishDrawTarget()`, but these are no-ops on native. If move cards use this path, content will never appear.

## Key Source Files

| File | Purpose |
|------|---------|
| `src/lazer/game/GamePanel.cpp` (1023 lines) | Main gameplay panel, Poll/Enter/Draw |
| `src/lazer/game/BustAMovePanel.cpp/h` | Move card display, SetFlashcardImage/Text |
| `src/system/hamobj/MoveDir.cpp` (1700+ lines) | Move detection, Enter() wires HUD at line 649 |
| `src/system/movie/TexMovie.cpp` (290 lines) | DrawToTexture() at line 207 |
| `src/system/rndobj/TexRenderer.cpp/h` | Alternative scene-to-texture renderer |
| `native/src/platform/Tex_Wgpu.cpp` | UploadRGBAToRndTex(), MakeDrawTarget stubs |
| `native/src/platform/FFmpegMovieImpl.cpp` (238 lines) | Bink→RGBA decode |
| `orig-assets/extracted/ui/game.dta` | game_screen panel config |
| `orig-assets/extracted/ui/hud/hud_objects.dta` | HUD panel structure |

## Implementation Plan

### Step 1: Trace BustAMovePanel content source
Read `BustAMovePanel.cpp` to understand how `SetFlashcardImage()` populates content. Determine if it uses:
- .bik video → FFmpegMovieImpl (already works)
- Scene rendering → MakeDrawTarget (needs implementation)
- Static texture swap (needs wiring)

### Step 2: Check if MakeDrawTarget needs implementation
If move cards use render-to-texture scene rendering, implement `MakeDrawTarget()` / `FinishDrawTarget()` on native to redirect the render pass to a texture target.

### Step 3: Verify .bik file availability
Check if move card .bik files are in the extracted assets. If they exist, the FFmpeg path should work — debug why they aren't opening.

### Step 4: Test and screenshot
Take gameplay screenshots to verify move card content appears. Frame ~700-900 should show gameplay with HUD.

## Revised Understanding (Research Complete)

**BustAMovePanel is NOT the regular gameplay move card system.** It's only for the "Bust A Move" freestyle dance creation mode.

**Regular gameplay move cards** are driven by:
- `MoveDir::Poll()` → gesture detection → `HamPhraseMeter::SetRatingFrac()` → progress bar
- `CharFeedback::UpdateLimb()` → limb flash overlay on character
- `text_feedback0/1` → text labels in venue world

**Key "couldn't find" warnings during gameplay**:
```
FlowMultiSetProperty couldn't find move_feedback0 in Start1.flow
FlowMultiSetProperty couldn't find phrase_meter0 in Start1.flow
FlowMultiSetProperty couldn't find text_feedback0 in Start1.flow
```

These are DTA flow scripts that try to show/hide HUD elements by name, but `FindObject()` can't locate them. The objects live in the venue world subdirs (`player0/phrase_meter0`) but the flow scripts search their own dir scope.

**Blockers:**
1. Gameplay mode requires input scripting to reach (main menu → song select → gameplay)
2. Move detection requires skeleton/gesture input (Kinect or substitute)
3. Without gesture detection, HamPhraseMeter never gets `SetRatingFrac()` called

**Recommendation:** Move card UI is a **medium-term** item that depends on gameplay input pipeline. Deprioritize in favor of items that improve the main menu and attract screens (visible without input scripting).
