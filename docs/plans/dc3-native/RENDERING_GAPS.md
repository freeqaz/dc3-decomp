# DC3 Native — Rendering Gap Analysis (Session 28)

## Current State

choose_mode_screen renders with geometry and text visible, but is mostly dark/desaturated compared to the reference (bright cyan neon UI).

- 50 mesh draw calls with real vertex data (compressed vertex decompression fixed)
- Text rendering working ("Jump right in and Perform!")
- Ribbon/bar geometry visible but dark purple instead of bright cyan
- Background vertical stripes visible but near-black

Reference: `archive/screenshots-old/references/dc3_main_menu.jpg`
Current: `archive/screenshots/session28/choose_mode_verts.png`

## Root Causes (Priority Order)

### 1. PropAnim not ticking — alpha stuck at 0 (CRITICAL)

Most UI elements use `kBlendSrcAlpha` (blend=3) where output = color * alpha. Material diagnostics show alpha=0.00 on nearly all glow/background layers:

| Material | Color (RGB) | Alpha | Expected |
|----------|-------------|-------|----------|
| rt_frames_glow04.mat | (0.42, 0.63, 0.64) | 0.00 | ~1.0 (animated) |
| frames_bg04.mat | (0.56, 0.58, 0.60) | 0.00 | ~1.0 (animated) |
| mainMenuMiddleShading | (0.30, 0.27, 0.30) | 0.00 | ~1.0 (animated) |
| icon.mat (perform) | (1.00, 1.00, 1.00) | 0.00 | ~1.0 (animated) |
| icon_sml.mat | (1.00, 1.00, 1.00) | 0.00 | ~1.0 (animated) |

These alphas are driven by **PropAnim** (property animation) during screen enter transitions. The animation system isn't advancing these values.

**Investigation needed:**
- Is `RndPropAnim::Poll()` / `SetFrame()` being called?
- Are the screen's animatable objects registered in the poll loop?
- Does `PanelDir::Enter()` trigger the transition animations?

### 2. Colors desaturated — not bright cyan (MEDIUM)

Even materials with alpha > 0 show muted gray-blue `(0.56, 0.58, 0.60)` instead of vibrant cyan `(0.0, 0.8, 1.0)`. The neon look comes from:
- **PropAnim-driven color values** — colors animate to cyan at runtime
- **Intensify flag** on materials (doubles color, `matUni.intensify = 2.0`)
- **Additive glow layers** stacking up brightness

This will likely fix itself once PropAnim ticking works.

### 3. Missing DANCE CENTRAL 3 logo (HIGH)

The big logo is absent from the 50 draw calls. Likely causes:
- In a subdir that didn't load (check `turbo_shell` or logo-specific panel)
- Rendered by a TexRenderer (render-to-texture) that isn't hooked up
- Part of the screen enter script that populates it

### 4. Missing text labels (MEDIUM)

Reference shows: "MAIN MENU", "EXIT CONTROLLER MODE", "SELECT", copyright block. We only see "Jump right in and Perform!". Other labels depend on:
- Screen enter DTA scripts setting text content
- UILabel visibility driven by PropAnim
- Controller mode / Kinect overlay panels

### 5. Missing player silhouette boxes (LOW)

Corner player identity boxes are Kinect UI — may not load without Kinect subsystem. Low priority.

## Assets

Two asset sources are available:
- **Extracted .milo files**: `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/`
  - `ui/choose_mode/gen/choose_mode.milo_xbox` — the screen panel
  - `ui/background/gen/bg_eq.milo_xbox` — background EQ bars
- **Ark archives**: `./orig-assets/gen/main_xbox.hdr` — full game data, 10 ark files
  - dc3-native engine extracts from ark at runtime via `NativeArkRead`

## Next Steps

1. **Investigate PropAnim ticking** — trace `RndPropAnim::Poll` / `SetFrame` to see if UI anims run
2. **Check PanelDir::Enter animation triggers** — does entering choose_mode_screen fire transition anims?
3. **Dump PropAnim targets** — list which properties are animated on the screen's objects
4. **Force alpha=1 test** — temporarily hardcode alpha=1 on SrcAlpha materials to see if colors/textures are otherwise correct
5. **Find the logo** — search for DC3 logo texture/mesh in the milo hierarchy
