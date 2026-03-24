# HUD Remaining Issues — 2026-03-24

## Status

The gameplay HUD renders through Xbox's native `WorldDir::mHUD` mechanism. The overlay pass works (face culling fixed, prelit forced, PostProc guard, 1x no-depth). Game flow reaches `game_stage='playing'`. DTA type system and init chain fully functional.

### What's Visible
- Flashcard panel backgrounds (blue/teal left, purple/pink right) at screen edges
- Score dock backgrounds (semi-transparent)
- Player indicator arrow
- Ham ribbon trails (cyan/magenta)
- Character feedback effects
- Score text "0" (tiny, alpha=0.5, at top corners)

### Architecture (correct, matches Xbox)
- `WorldDir::mHUD` ObjPtr → PanelDir 'hud' with type='hud', 48 mDraws
- Drawn after `EndWorld()` in `WorldDir::DrawShowing()` line 545-547
- HUD polled by WorldDir's natural poll hierarchy
- `HamDirector::OnFileMerged('game_hud')`: Enter + SetShowing + set $hud_panel + force postprocs_before_draw
- `GamePanel::SetTypeDef('perform')`: re-trigger common_reset (timing fix)
- Overlay pass: 1x no-MSAA, no depth, face culling disabled, prelit forced
- FOV correction: `viewProj[0] *= 0.49` for cylindrical HUD at ±750 X (hack, TODO)

---

## Issue 1: Move Texture Resolution (Flashcard Icons)

**Impact:** Flashcard panels show solid colors but no dance move silhouette icons.

**Root cause:** The DTA `update_flashcard_move` handler does:
```
$tex = {[cur_move] get tex}
{icon.mat set diffuse_tex $tex}
{icon.grp set_showing {! {== $tex ""}}}
```
`$tex` is empty because `HamMove::mTex` doesn't resolve. The HamMove objects are loaded from the song .milo (e.g., `songs/thehustle/thehustle.milo`) which gets merged into the main merger's dir. The `mTex` ObjPtr references a texture by name, but the texture might be in a different ObjectDir scope than where the ObjPtr resolves.

**Investigation needed:**
- Check `src/system/hamobj/HamMove.h` — `mTex` is `ObjPtr<RndTex>`. How does it resolve?
- Check if the song .milo's move textures are uploaded to GPU (search `PresyncBitmap` for move textures)
- Check if the DTA `get tex` property accessor returns null because the ObjPtr target is in a different dir
- The fix might be: after song merge, ensure move textures are registered in a scope visible to the HUD

**Files:**
- `src/system/hamobj/HamMove.h` — mTex, mSmallTex ObjPtrs
- `src/system/hamobj/HamMove.cpp` — SetTexture, texture loading
- `orig-assets/extracted/ui/hud/hud_objects.dta` — update_flashcard_move handler (line ~800+)
- `src/system/char/FileMerger.cpp` — MergeDirs, object scope after merge

---

## Issue 2: Score Always Shows 0

**Impact:** Score labels display "0" instead of an incrementing score.

**Root cause:** The scoring system requires move detection events. On Xbox, the Kinect detects player moves and generates scoring events. On native without Kinect, no moves are detected → no scoring → score stays 0.

**The DTA `set_score` handler** in hud_objects.dta is empty (no-op):
```
(set_score ...)  ; body does nothing visible
```
The actual score display is driven by `{$hud_panel set_score [score] [score] 0 1}` from `common_reset`, which sets score to 0.

**Potential fix:** Auto-scoring / simulated scoring for native. On Xbox, moves are detected by the gesture system. On native, we could either:
1. Fire fake scoring events on beat to simulate gameplay (simple but hacky)
2. Implement autoplay scoring that awards points based on song timing (more correct)

The scoring pipeline: `MoveDetector` → `RhythmBattlePlayer.mScore` → DTA `move_passed` handler → `{$hud_panel set_score}`.

**Files:**
- `src/system/hamobj/ScoreUtl.cpp` — scoring pipeline
- `src/lazer/game/GamePanel.cpp` — Poll(), game state management
- `orig-assets/extracted/ui/gameplay/perform.dta` — move_passed handler
- `orig-assets/extracted/ui/hud/hud_objects.dta` — set_score handler

---

## Issue 3: HUD Scale / Proportions

**Impact:** HUD elements appear smaller than Xbox due to resolution difference.

**Root cause:** Xbox renders internally at 768×432 and upscales to 720p. The HUD is designed for 432p — at that resolution, HUD elements at ±700 X units fill more of the viewport proportionally. Our native port renders at 1280×720 natively, making HUD elements appear ~40% smaller.

The `viewProj[0] *= 0.49` hack approximates the Xbox FOV but doesn't match exactly. The proper fix requires understanding the Xbox's D3D9 viewport chain and how the 432p internal resolution maps to the final display.

**Key data:**
- Xbox internal: 768×432 (16:9), upscaled to 1280×720 for display
- Native: 1280×720 (16:9) direct render
- HUD camera `Cam.cam`: yFov=0.6024, position (0,-768,0), near=126, far=7300
- Standard 16:9 xFov at this yFov: ~58° → covers ±424 X at 768 units
- HUD meshes at ±700-750 X → outside standard viewport
- 0.49 factor: effective xFov ~97° → covers ±866 X → panels visible

**Potential fix:** Instead of hacking viewProj, render the HUD at the Xbox's internal resolution (768×432) to a render target, then upscale. This would match Xbox's effective HUD scaling exactly. Alternatively, compute the correct FOV correction from the 432p→720p scaling ratio.

**Math:** At 432p, the engine computes `mWidth = 432/0.5625 = 768`. The HUD projection uses this width. The effective horizontal pixels per NDC unit = 768/2 = 384. At 720p native: 1280/2 = 640. Ratio: 384/640 = 0.6. So `viewProj[0] *= 0.6` might be closer than 0.49.

**Files:**
- `native/src/platform/Rnd_Wgpu.cpp` — WriteSceneUniforms, FOV hack
- `src/system/rndobj/Rnd.cpp` — mWidth, mHeight, YRatio
- `src/system/rndobj/Cam.cpp` — UpdateLocal, GetViewProjectXfms
