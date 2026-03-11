# Session 45: GPU Projection/Scale Debugging

**Date**: 2026-03-11
**Goal**: Fix UI layout/camera projection so native port matches Xbox reference screenshot
**Status**: Resolved — scale was correct all along; real issue is rendering brightness/quality

## Root Cause: NOT a Scale Issue

**The projection scale is correct.** Pixel-level analysis proves content spans the full frame in both:

| Metric | Xbox Reference | Native Port |
|--------|---------------|-------------|
| Content extent | 2.5%-97.5% (95%) | 0%-100% (100%) |
| Bright pixels | 54.4% of frame | 23.2% of frame |
| Cyan element X range | 25.1%-97.4% | 7.5%-99.9% |

The "66% scale" perception was caused by **rendering brightness**: only 23.2% of native pixels are above visibility threshold vs 54.4% on Xbox. Elements ARE correct size but rendered too dim, making them appear smaller due to faint edges blending into the dark background.

**Real issue**: rendering quality — alpha/transparency, glow/bloom, material colors.

## Xbox 360 Rendering Pipeline (Confirmed)

```
Config: height=432, aspect=kWidescreen (YRatio=0.5625)
  mHeight = 432, mWidth = 768

Projection: FOV=34.516°, ratio=0.5625
  projMtx.x.x = 0.5625/tan(17.258°) = 1.810
  projMtx.y.y = 1/tan(17.258°) = 3.218
  → Standard 16:9 perspective projection

Visible area (at distance 768): 848×477 world units

D3D Backbuffer: 768×432 (mPresentParams.BackBufferWidth = mWidth)
D3D Viewport: 768×432 (from TheDxRnd.Width() / .Height())
  → Set by DxCam::SetViewport() using mWidth/mHeight from config

Hardware Scaler (D3DVIDEO_SCALER_PARAMETERS):
  → ScaledOutputWidth = displayWidth * 0.95 (safe area)
  → ScaledOutputHeight = displayHeight * 0.95
  → Upscales 768×432 backbuffer to display resolution
  → Controlled by UpdateScalerParams()
```

## Native Port Pipeline (Current State)

```
Config: same (height=432, aspect=kWidescreen)
  mHeight = 432, mWidth = 768 (no longer overridden — fixed this session)

Projection: same as Xbox
  projMtx.x.x = 1.810, projMtx.y.y = 3.218

GPU Framebuffer: 1280×720 (WebGPU headless or windowed)
WebGPU Viewport: 1280×720 (confirmed via GPU capture — see below)

No hardware scaler — content renders directly to full framebuffer
NDC [-1,1] maps to same relative positions as Xbox (viewport-independent)
```

## GPU Capture Analysis (Frames 98-102)

Captured via GFXReconstruct, analyzed viewport settings:

| Viewport Size | Count | Purpose |
|---------------|-------|---------|
| 1280×720 | 12 | Main render pass |
| 1024×1024 | 6 | Shadow map |
| 640×360 | 24 | Post-proc downsample |
| 320×180 | 24 | Bloom mip 1 |
| 160×90 | 24 | Bloom mip 2 |
| 80×45 | 18 | Bloom mip 3 |

Draw call counts per frame: ~515 (2478 indexed + 96 non-indexed over 5 frames)

## Changes Made This Session

### 1. Removed mWidth/mHeight Override (Rnd_Wgpu.cpp)

`WgpuRnd::Init()` was overriding `mWidth=1280, mHeight=720` after `PreInit()` set them from config (768×432). Fixed by:
- Removed `mWidth = mGpu.WindowWidth()` / `mHeight = mGpu.WindowHeight()`
- Removed window-resize mWidth/mHeight update
- GPU operations use `mGpu.WindowWidth()` / `.WindowHeight()` directly

**Impact**: Rnd reports w=768 h=432 (matching Xbox). No visible change to 3D meshes (they use world coords from .milo data), but ensures code using TheRnd.Width()/Height() for screen-space calcs gets correct values.

### 2. Fixed DrawRect2D Coordinate Mapping (DrawRect2D.cpp)

DrawRect2D was converting pixel coords to NDC using GPU framebuffer dimensions (1280×720), but the engine generates coords in Rnd virtual resolution (768×432). Fixed to use `TheRnd.Width()/Height()`.

**Impact**: 2D overlays (text, helpbar, screen masks) now map to correct NDC positions. This was a real bug — a fullscreen rect at (0,0,768,432) was mapping to only 60% of NDC instead of full [-1,1].

### 3. Removed Projection Scaling Hack (Rnd_Wgpu.cpp)

Previous session added `projMtx *= designHeight/renderHeight`. This was WRONG — it reduced the projection, making objects smaller. The projection from `GetViewProjectXfms()` is already correct.

### 4. Fixed sFlipYZ PPC Initialization (Cam.cpp + link_glue.cpp)

sFlipYZ is a BSS static at 0x830A18A8, runtime-initialized by `??__EsFlipYZ@@YAXXZ` (0x82EDCAE0). Was previously guarded by `#ifdef HX_NATIVE` with an ALTERNATENAME stub for PPC.

Fixed:
- Made initializer unconditional: `static Transform sFlipYZ(Hmx::Matrix3(1, 0, 0, 0, 0, 1, 0, 1, 0), Vector3(0, 0, 0));`
- Removed ALTERNATENAME stub from link_glue.cpp
- Dynamic initializer matches 100% (verified via objdiff)

### 5. Cleaned Up Debug Logging

Removed all `gDebugFrameID`-based diagnostic logging added during investigation:
- `native/src/platform/Rnd_Wgpu.cpp` — global definition, frame counter assignment
- `native/src/platform/Mesh_Wgpu.cpp` — text mesh bounds, card background diagnostics
- `src/system/ui/PanelDir.cpp` — camera dump in DrawShowing
- `src/system/ui/UIPanel.cpp` — draw pass logging
- `src/system/ui/UIScreen.cpp` — panel draw iteration logging
- `src/system/rndobj/Group.cpp` — group draw enumeration
- `src/system/rndobj/Text.cpp` — text mesh/fontmap diagnostics, DebugChooseModeText helper
- `src/system/hamobj/HamNavList.cpp` — navlist state + camera logging
- `src/system/hamobj/HamListRibbon.cpp` — ribbon draw diagnostics

## Camera Analysis (Frame 250)

| Camera | Position | Role |
|--------|----------|------|
| `[default cam]` | (0, 0, 0) | Default (rarely renders content) |
| `turbo_shellbg.cam` | (-125.6, -1563.5, -60.7) | Background (swirls, lines) |
| `turbo_shell.cam` | (-125.0, -663.5, -63.0) | Foreground shell (cards, menu) |
| `[ui.cam]` | (0, -768, 0) | Overlays (helpbar, letterbox, etc.) |

All share FOV=0.6024 rad (34.516°) and aspect ratio (kWidescreen, YRatio=0.5625).

## Key Technical Details

### sFlipYZ Transform
Milo Y-forward/Z-up → WebGPU Z-forward/Y-up. Applied in `GetViewProjectXfms()`:
```cpp
Multiply(mInvWorldXfm, sFlipYZ, viewXfm);
// sFlipYZ.m = {{1,0,0}, {0,0,1}, {0,1,0}}  — Y↔Z swap
```

### Matrix Convention
- Engine: row-major, `clip = pos * VP`
- WGSL: column-major, `clip = VP * pos`
- Writing row-major VP to flat array, WGSL reads as transpose → mathematically equivalent

### Xbox DxCam::SetViewport()
```cpp
width = TheDxRnd.Width();    // = mWidth = 768
height = TheDxRnd.Height();  // = mHeight = 432
vp.Width = width * screenRect.w;   // 768 * 1.0 = 768
vp.Height = height * screenRect.h; // 432 * 1.0 = 432
```

## Screenshots

- `archive/screenshots/session45/frame_00100.png` — frame 100, title screen ("DANCE CENTRAL 3" logo)
- `archive/screenshots/session45/frame_00250.png` — frame 250, choose_mode_screen (main menu)
- `archive/screenshots/session45/frame_00500.png` — frame 500, choose_mode_screen (steady state)

## Next Steps (Rendering Quality)

- [ ] Investigate alpha/transparency handling (white rectangles should be transparent)
- [ ] Check material blend modes (additive, SrcAlpha, etc.)
- [ ] Verify glow/bloom post-processing compositing is correct (bloom passes ARE running)
- [ ] Compare material colors between Xbox and native

## Files Modified

- `src/system/rndobj/Cam.cpp` — unconditional sFlipYZ initializer, removed debug logging
- `src/link_glue.cpp` — removed sFlipYZ ALTERNATENAME stub
- `native/src/platform/Rnd_Wgpu.cpp` — removed mWidth/mHeight override, projection scaling hack, debug logging
- `native/src/platform/Mesh_Wgpu.cpp` — removed debug logging
- `native/src/gfx/DrawRect2D.cpp` — fixed coordinate mapping to use Rnd virtual resolution
- `src/system/ui/PanelDir.cpp` — removed debug logging
- `src/system/ui/UIPanel.cpp` — removed debug logging
- `src/system/ui/UIScreen.cpp` — removed debug logging
- `src/system/rndobj/Group.cpp` — removed debug logging
- `src/system/rndobj/Text.cpp` — removed debug logging + DebugChooseModeText helper
- `src/system/hamobj/HamNavList.cpp` — removed debug logging
- `src/system/hamobj/HamListRibbon.cpp` — removed debug logging
