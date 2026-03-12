# Session 49 — Camera Fixes + Scene Uniform Bug (2026-03-11)

## Context

After Session 48 made the UI visible, the rendered scene was still missing ~439 mesh draw calls because the wrong camera was being used for shell content, and a rendering bug prevented camera position changes from propagating to scene uniforms.

## Changes

### 1. Scene Camera Selection (`HamNavList.cpp`)

`HamNavList::DrawShowing()` now uses the PanelDir's scene camera (`turbo_shell.cam`) instead of the default UI camera (`[ui.cam]`). This is the authored camera for the choose-mode shell and produces the correct view transform for all shell meshes.

**Status**: Kept as a real fix (not a hack).

### 2. Scene Uniform Camera Change Detection (`Rnd_Wgpu.cpp`)

`EnsureSceneUniformsCurrent()` was only checking `camPosY` to detect camera position changes. When cameras with different X/Z positions were swapped (same Y), the scene uniforms weren't re-uploaded. Fixed to check all 3 position components (X, Y, Z).

**Recovered 0 to 439 mesh draw calls.**

**Status**: Kept as a real fix (general rendering bug).

### 3. Removed `entering=true` Hack (`HamNavList.cpp`)

Main ribbon correctly passes `entering=false`, stopping beam/chevron overlay meshes. The widget system (DrawWidgets) renders the actual menu text.

**Status**: Kept — correct behavior.

## Camera Inventory (choose_mode_panel)

| Camera | World Position | FOV (rad) | Purpose |
|--------|---------------|-----------|---------|
| `[ui.cam]` | (0, -768, 0) | 0.602 | Default UI camera (centered) |
| `turbo_shell.cam` | (-125, -663.5, -63) | 0.602 | Scene camera (PanelDir.mCam) |
| `camera1.cam` | (32, -111.5, -63) | 2.127 | Close-up effect camera |
