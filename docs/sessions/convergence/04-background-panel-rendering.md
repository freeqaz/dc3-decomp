# 04 — Can background_panel Render on Native?

**Date**: 2026-03-21
**Status**: Analysis complete
**Verdict**: YES, with caveats

## What background_panel Contains

`orig-assets/extracted/ui/background/background.dta` defines 6 UIPanel objects:

```
background_panel           → background.milo
background_left_panel      → background_left.milo
background_right_panel     → background_right.milo
background_confirmation_panel → background_confirmation.milo
background_endgame_panel   → background_endgame.milo
background_pause_panel     → background_pause.milo
```

Each is a plain `UIPanel` (not a MetaPanel or GamePanel). They load `.milo` files
through the standard `DirLoader` → `PanelDir` pipeline.

### The turbo_shell Scene (background.milo)

The turbo_shell is a PanelDir containing flat 2D/3D geometry for the menu
background. From the convergence session doc:

- **Meshes**: Gradients (`bg_gradient.mesh`), frames/chrome (`frames_bg01-04.mesh`,
  `frames_glow_01-04.mesh`), surface effects (`bg_diagonal_bars.mesh`,
  `surface_scanline_lft.mesh`, `surfaceTelelines_lft.mesh`)
- **Materials**: `shell_basic.mmat`, `shell_basic_wrap.mmat`, per-element mats
- **Cameras**: `turbo_shell.cam` (main), `turbo_shellbg.cam` (background),
  `camera1.cam`
- **Animations/Flows**: `bump.anim`, `diagonal.anim`, `eq4.anim`, `pulse.anim`,
  `fade.anim`, `overlay_colorswitch.anim`
- **Post-processing**: `PostProc_Blacklight.pp` with `Environ` for lighting
- **Type**: The `shell` type in `world_objects.dta:1880` configures PostProc —
  on `enter`, it calls `{$this update_postproc}` which does
  `{[postprocess] select}` to activate the PostProc object

### Where background_panel Is Used

`background_panel` appears in ~30+ screens. The canonical example:
```
main_screen: (panels meta background_panel main_panel main_menu_wait_for_content_panel)
```
`HamUI::Draw()` also draws `mBackgroundPanel` during dialog events.

## The Rendering Pipeline

### How UIPanel::Draw Works

```
UIScreen::Draw()
  → for each panel in mPanelList:
      if active && showing && ShouldDrawPanel:
        panel->Draw()

UIPanel::Draw()  [src/system/ui/UIPanel.cpp:96-100]
  if (mFinalDrawPassFlag == sIsFinalDrawPass && mDir && !mLoaded):
    mDir->DrawShowing()    ← calls PanelDir::DrawShowing

PanelDir::DrawShowing()  [src/system/ui/PanelDir.cpp:384-417]
  1. if mCanEndWorld: TheRnd.EndWorld()     ← triggers post-processing
  2. if camOverride != current: camOverride->Select()
  3. if !mEnv: select TheUI->GetEnv()
  4. draw mBackPanels
  5. RndDir::DrawShowing()                  ← draws all drawables
  6. draw mFrontPanels
  7. restore previous camera

RndDir::DrawShowing()  [src/system/rndobj/Dir.cpp:305-313]
  RndEnvironTracker tracker(mEnv, &WorldXfm().v)
  for each drawable in mDraws: drawable->Draw()
```

### How the Native Renderer Integrates

The main loop in `src/App.cpp:1141-1158`:
```
TheRnd.BeginDrawing()     → WgpuRnd creates encoder, shadow pass, begins frame pass
  [venue hack draw]       → menuVenue->DrawShowing() (TO BE REMOVED)
  TheUI->Draw()           → UIScreen → UIPanel → PanelDir → RndDir::DrawShowing
TheRnd.EndDrawing()       → WgpuRnd runs PostProcPass, presents frame
```

The key insight: `TheUI->Draw()` already calls the full panel hierarchy. When
`background_panel` is loaded and its owning screen is active, it will draw
through the normal path. No special native code is needed.

## Feature-by-Feature Support Assessment

### 1. Mesh Drawing — SUPPORTED

`RndMesh::DrawShowing()` is fully implemented in `native/src/platform/Mesh_Wgpu.cpp`.
It handles:
- Static and bone-skinned meshes
- All blend modes (src, srcAlpha, add, subtract, multiply, dest)
- Transparent draw ordering via `TransparentQueue`
- LOD mesh filtering (skips `_lod` meshes)
- Kinect grid filtering (skips `grid_80by60`)

The turbo_shell meshes are flat screen-space geometry — the simplest case for
the mesh renderer.

### 2. Materials — SUPPORTED

`MaterialSetup.cpp` builds `MaterialUniforms` from `RndMat` properties:
- Color, alpha, blend mode, alpha cut/threshold
- Diffuse, normal, specular, emissive, rim, environ cube, detail normal textures
- TexGen modes (none, xfm, sphere, projected, xfmOrigin, environ)
- Fog, prelit, intensify, specular, anisotropy
- Multi-pass materials (next_pass chain)

`shell_basic.mmat` and `shell_basic_wrap.mmat` are standard materials that
should work without special handling.

### 3. Cameras — SUPPORTED

`PanelDir::DrawShowing()` calls `CamOverride()` which returns the panel's
camera (or TheUI->GetCam() as fallback). The native renderer handles camera
switching via `EnsureSceneUniformsCurrent()` which detects camera changes
and re-uploads scene uniforms mid-frame.

Multiple cameras per panel are supported — the turbo_shell can switch between
`turbo_shell.cam`, `turbo_shellbg.cam`, and `camera1.cam` via Flow animations.

### 4. Environ / Lighting — SUPPORTED

`RndDir::DrawShowing()` wraps draws in `RndEnvironTracker` which calls
`env->Select()`. `WriteSceneUniforms()` reads from `RndEnviron::Current()`
to populate ambient color, fog, and light arrays in the scene uniform buffer.

Up to 4 directional lights and 4 point lights are supported. The turbo_shell's
Environ will be read correctly.

### 5. Post-Processing — SUPPORTED (with integration path)

The native post-proc pipeline in `PostProcPass` supports:
- Bloom (with configurable intensity, threshold, color)
- Depth of field
- Color grading (contrast, brightness, saturation, levels)
- Vignette
- Chromatic aberration
- Posterize
- Noise/grain
- Flicker

**How it works today**: `WgpuRnd::EndDrawing()` checks `RndPostProc::Current()`
and runs `PostProcPass::Run()` which reads PostProc properties directly.

**How it will work with background_panel**: The `shell` type in
`world_objects.dta` calls `{[postprocess] select}` on enter. This calls
`RndPostProc::Select()` which sets `sCurrent = this`. Since the native
renderer checks `RndPostProc::Current()` in `EndDrawing()`, the PostProc
from `background.milo` will automatically be applied.

**EndWorld path**: `PanelDir::DrawShowing()` calls `TheRnd.EndWorld()` when
`mCanEndWorld` is true (default). This calls `DoWorldEnd()` → `DoPostProcess()`
on the base Rnd class. On native, `NgPostProc::EndWorld()` and
`NgPostProc::DoPost()` run but all their `TheShaderMgr` calls are stubbed
(WgpuShaderMgr has empty setter methods). So the Xbox-era PostProc pipeline
runs harmlessly — it sets internal state variables but produces no rendering.
The actual visual post-processing happens at end-of-frame in
`WgpuRnd::EndDrawing()` → `PostProcPass::Run()`.

### 6. Flows / Animations — SUPPORTED

Flow nodes are compiled in the native build. `PanelDir::Enter()` has an
`HX_NATIVE` block that activates game-code-triggered flows (startMode==0)
that normally fire from DTA enter scripts:

```cpp
for (ObjDirItr<Flow> it(this, true); it != nullptr; ++it) {
    if (it->GetStartMode() > 0) continue;
    if (!ShouldActivateNativeFlow(Name(), flowPath)) continue;
    if (!it->IsRunning()) it->Activate();
}
```

The `ShouldActivateNativeFlow` filter allows flows from `"background"` dirs
(returns `true` for enter/show/activate tokens). Auto-start flows
(startMode > 0) run through the normal `Flow::Enter()` path.

PropAnims drive mesh transforms, material colors, and other properties through
the standard animation system. These work on native since `RndAnimatable`,
`RndPropAnim`, and `EventTrigger` are all compiled.

### 7. Render-to-Texture — SUPPORTED (if needed)

`WgpuRnd::SelectRenderTarget()` and `FinishRenderTarget()` support rendering
to `RndTex` objects. `DrawPreClear()` handles `RndTexRenderer` passes. The
turbo_shell may or may not use render-to-texture — if it does, the pipeline
exists.

### 8. DrawRect (2D) — SUPPORTED

`WgpuRnd::DrawRect()` is implemented for screen-space 2D drawing, used by
the overlay system and potentially by shell elements.

### 9. Text — SUPPORTED

Text rendering works through `RndText::DrawShowing()` → mesh creation →
`RndMesh::DrawShowing()`. The native build supports font map textures and
dynamic text mesh generation.

## What Happens When We Remove the Venue Hack

### Current state (with hacks):

1. `NativeVenueInit()` captures a gameplay WorldDir via `gNativeVenueDir`
2. App.cpp draws `menuVenue->DrawShowing()` directly before `TheUI->Draw()`
3. The turbo_shell scene from `background_panel` ALSO draws (via TheUI)
4. Both the gameplay venue and the turbo_shell render on top of each other

### After removing hacks:

1. No `gNativeVenueDir`, no `NativeVenueInit()`, no direct venue draw
2. `TheRnd.BeginDrawing()` → `TheUI->Draw()` → `TheRnd.EndDrawing()`
3. `main_screen` draws its panels: `meta`, `background_panel`, `main_panel`,
   `main_menu_wait_for_content_panel`
4. `background_panel` → PanelDir::DrawShowing → draws turbo_shell meshes
5. PostProc from background.milo is selected via the `shell` type enter script
6. `WgpuRnd::EndDrawing()` applies PostProc to the frame

**Result**: The turbo_shell scene renders as the menu background, with
post-processing applied. This matches the Xbox behavior.

## Potential Issues

### 1. Panel Loading Timing

`background_panel` must be loaded before `main_screen` enters. The DTA flow
does `{background_panel load}` as part of the screen's panel list. The
`DirLoader` → `PanelDir` pipeline handles this. On native, the async loader
(`TheLoadMgr.Poll()`) must complete before the panel can draw.

**Risk**: LOW. The panel loading system works on native — other panels
(main_panel, dialog_panel) load and draw successfully.

### 2. PostProc Selection Timing

The `shell` type's enter handler runs `{[postprocess] select}` which calls
`RndPostProc::Select()`. This sets `sCurrent` before DrawShowing runs.
But if the PostProc object is not yet loaded from the .milo when enter fires,
the `[postprocess]` property may be empty (`''` in the DTA default).

**Risk**: LOW. The `if_else` guard in the DTA handler checks
`[postprocess]` exists before selecting. If empty, it calls
`{rnd reset_postproc}` instead.

### 3. Clear Color / Background

When no venue is drawn, the framebuffer starts with whatever the clear color
is. Currently `WgpuRnd::BeginFramePass` clears to a configured color:
```cpp
// Default to medium-dark teal to approximate the turbo_shell venue.
```
With the turbo_shell rendering, the clear color should be black or whatever
the shell's background gradient provides. The turbo_shell meshes cover the
full screen area, so the clear color shouldn't matter.

**Risk**: NONE. The turbo_shell's bg_gradient meshes provide full-screen
coverage.

### 4. The `meta` Panel

`main_screen` has `meta` as its first panel. MetaPanel handles music and
game state. It doesn't draw anything visual — it's a logic-only panel.
No rendering concern.

**Risk**: NONE.

### 5. Camera Coordinate System

The turbo_shell cameras are authored for the Xbox's coordinate system. The
native renderer includes axis-flip correction in `WriteSceneUniforms()` via
`cam->GetViewProjectXfms()` which accounts for the Milo Y-forward to
WebGPU Z-forward conversion.

**Risk**: LOW. Existing panels (main_panel with UI elements) already render
correctly through the camera system.

### 6. mCanEndWorld PostProc Flush Timing

`PanelDir::DrawShowing()` calls `TheRnd.EndWorld()` when `mCanEndWorld` is
true (default). On Xbox, this flushes the 3D world's post-processing before
drawing UI elements on top. On native, `EndWorld()` runs the stubbed Xbox
PostProc chain (harmless), but doesn't trigger the WebGPU PostProcPass
(that only runs in `EndDrawing()`).

**Implication**: If the turbo_shell's PostProc is supposed to affect ONLY the
background meshes (not the UI overlay), the current integration is correct —
PostProc runs at end-of-frame, affecting everything drawn. However, if the
intent is for PostProc to be applied before UI text/buttons draw on top,
the `FlushPostProcessingForOverlay()` mechanism exists for exactly this case.

**Risk**: MEDIUM. May need to call `FlushPostProcessingForOverlay()` from
`PanelDir::DrawShowing()` when `mCanEndWorld` is true, so that PostProc
applies to the background but not to the UI elements drawn afterward. This
can be added as a refinement.

## Conclusion

**Can background_panel render on native today? YES.**

The full rendering pipeline from UIPanel through PanelDir to RndDir to
RndMesh/RndMat/RndTex is implemented and working. Every feature the
turbo_shell uses — meshes, materials, cameras, environ/lighting, PostProc,
Flow animations — is supported by the native WebGPU renderer.

### What's Already Working (no changes needed):
- Panel loading via DirLoader
- PanelDir::DrawShowing with camera override and environ tracking
- RndDir::DrawShowing iterating drawables
- RndMesh::DrawShowing with full material pipeline
- PostProc selection via RndPostProc::Select() and rendering via PostProcPass
- Flow activation on panel enter (HX_NATIVE block in PanelDir::Enter)
- PropAnim-driven mesh/material animation
- Multi-camera switching with scene uniform re-upload

### What May Need Refinement (post-removal):
- **PostProc flush timing**: Add `FlushPostProcessingForOverlay()` call in the
  EndWorld path so background PostProc doesn't affect UI overlay panels
- **Clear color**: Verify default clear color is appropriate (black) when the
  turbo_shell provides full-screen coverage
- **Flow filter tuning**: Verify `ShouldActivateNativeFlow("background", ...)`
  correctly activates all turbo_shell animations

### Files Examined

| File | Purpose |
|------|---------|
| `orig-assets/extracted/ui/background/background.dta` | Panel definitions |
| `src/system/ui/UIPanel.cpp` | Panel draw path |
| `src/system/ui/PanelDir.cpp` | PanelDir::DrawShowing (camera, environ, EndWorld) |
| `src/system/rndobj/Dir.cpp` | RndDir::DrawShowing (drawable iteration) |
| `src/system/rndobj/Rnd.cpp` | EndWorld, DoWorldEnd, DoPostProcess |
| `src/system/rndobj/PostProc.cpp` | RndPostProc::Select, ::Current |
| `src/system/rndobj/PostProc_NG.cpp` | NgPostProc::DoPost (stubbed on native) |
| `native/src/platform/Rnd_Wgpu.cpp` | WebGPU renderer, EndDrawing PostProc |
| `native/src/platform/Rnd_Wgpu.h` | Renderer header, uniform structs |
| `native/src/gfx/PostProcPass.cpp` | Native PostProc (bloom, DOF, color grading) |
| `native/src/platform/Mesh_Wgpu.cpp` | Mesh drawing |
| `native/src/platform/MaterialSetup.cpp` | Material uniform setup |
| `src/system/ui/UI.cpp` | UIManager::Draw |
| `src/system/ui/UIScreen.cpp` | UIScreen::Draw (panel iteration) |
| `src/App.cpp` | Main loop, venue hacks |
| `orig-assets/extracted/world/world_objects.dta` | shell type PostProc config |
| `docs/sessions/2026-03-20-dta-venue-flow-convergence.md` | Full convergence analysis |
