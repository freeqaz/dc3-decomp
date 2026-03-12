# Session 54 — Step 3: Visibility Mask Removal (2026-03-12)

Step 3 of the [UI Animation Unwind Plan](../native/UI_ANIMATION_STATUS.md).

## Summary

Removed two of three visibility hacks. The third (zero-alpha floor) is still needed. The Kinect mesh filter was also narrowed by removing its overlay sub-check.

## A/B Test Results

| Hack | Visual effect when removed | Action |
|------|---------------------------|--------|
| Zero-alpha floor (`Mesh_Wgpu.cpp`) | Background teal bars disappear | **Keep** — still load-bearing |
| MeshFilter: Kinect names | White rectangle appears (unfiltered Kinect mesh) | **Keep** — still needed |
| MeshFilter: overlay (small-tex) | **No regression** — overlays now animated by flows | **Removed** |
| HelpBarPanel voice-tip hide | **No regression** — redundant with MeshFilter | **Removed** |
| All three removed | White rectangle + lost background | Not viable |

## Changes

### 1. Removed HelpBarPanel Voice-Tip Hiding (`HelpBarPanel.cpp`)

Deleted the entire `#ifdef HX_NATIVE` block that iterated 17 named drawables (voice_tip.lbl, grey_alpha.mesh, warning_*.mesh, shield_*.mesh, geo_ray_*.mesh) and hid them. These are already filtered by `MeshFilter::ShouldSkipMesh()` at the draw level, making the per-frame `ObjDirItr` traversal redundant.

### 2. Removed MeshFilter Overlay Check (`MeshFilter.cpp`)

Deleted the PropAnim-driven overlay filter that caught meshes with small (<=8x8) textures, srcAlpha blend, and alpha > 0.99. This filter was added because these overlays defaulted to opaque white rectangles when flows weren't animating them. Now that Flow::Enter() auto-starts properly (Session 53), the PropAnims run and set correct alpha values — the overlays render correctly without the filter.

### 3. Remaining: Kinect Mesh Filter (kept)

The name-based Kinect mesh filter remains. It catches:
- Player indicator elements (ui_blank, silhouette_guy, buffer_*, _crown)
- Microphone/voice control UI (mic_*, geo_mic*)
- Hand gesture icons (shield_hand)
- Tutorial/gesture overlays (tutorial, gesture, spotlight, nav_tut)
- Voice-tip/speech warnings (grey_alpha, warning_*)

Without this filter, a white opaque rectangle appears from an unfiltered Kinect mesh that neither PanelDir::Enter() dir-hiding nor the flow system handles.

### 4. Remaining: Zero-Alpha Floor (kept)

The alpha floor in Mesh_Wgpu.cpp is still needed. Without it, decorative background meshes (teal glow bars, diagonal stripes) remain invisible because their material alpha stays at 0. These meshes are animated by DTA scripts on Xbox that don't have native equivalents yet.

## Visual Impact

Final state has slightly MORE visual content than the pre-session baseline (778K vs 768K file size). The previously-filtered PropAnim overlays now render correctly with authored alpha, contributing subtle visual detail.

## Verification

- Frame 220: 439 draw calls, matches baseline
- PPC build clean
- Enter animation frames 3→10→50 all render correctly

## Screenshots

Archived: `archive/screenshots/session54/`
