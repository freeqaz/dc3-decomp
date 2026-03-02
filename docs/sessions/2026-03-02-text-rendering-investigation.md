# Text Rendering Investigation — 2026-03-02

## Summary

Investigated why UI text appeared invisible in dc3-native screenshots. **Conclusion: text IS rendering correctly**, but is small and subtle against the dark DC3 UI background. The earlier session incorrectly reported text as not visible.

## Key Findings

### Text Pipeline Is Working End-to-End

1. **UILabel::DrawShowing** → sets font color overrides → calls **RndText::DrawShowing**
2. **RndText::SizeCheck** → calls **UpdateText** every frame (native always rebuilds)
3. **UpdateText** → BuildFontMaps → ResetDisplayableChars → IncrementDisplayableChars → AllocateMeshes → SetupCharacter loop → CleanupSyncMeshes
4. **SetupCharacter** writes correct vertex positions and UVs:
   - Quads in XZ plane (y=0), x advances per character, z = line height
   - Example: 'T' at autosave_warning.lbl: v0=(0,0,0), v1=(11.34,0,0), v3=(0,0,-21.58)
   - Font atlas UVs correct (8bpp palette-indexed → RGBA expansion via PixelColor)
5. **DrawMesh** → **RndMesh::DrawShowing** → queued to transparent draw queue (font materials use kBlendSrcAlpha=7)
6. **FlushTransparentDraws** → **DrawMeshImmediate** → GPU upload via UnpackStaticVertices → draw call

### UI Camera Configuration

| Parameter | Value |
|-----------|-------|
| Name | `[ui.cam]` |
| Position | (0, -768, 0) |
| Rotation | Identity |
| YFov | 0.6024 rad (~34.5°) |
| Near/Far | 1.0 / 1000.0 |
| LocalProjectXfm.m.x.x | 1.8107 (X scale = cot(yfov/2)/aspect) |
| LocalProjectXfm.v.x | -3.2189 (Y scale = -cot(yfov/2)) |
| ScreenRect | (0, 0, 1, 1) — full screen |

### Why Text Appears Small

The UI camera at y=-768 looks along +Y toward the UI elements near y=0. Text mesh vertices span ~19 units tall (font size), which at 768 units distance maps to about 5% of screen height in NDC. This is correct — in the original game, many UI elements are small and positioned precisely. The autosave warning text at worldPos=(0,0,-27.6) maps to NDC y≈-0.12 (slightly below center).

### Matrix Convention (Confirmed Correct)

- C++ stores viewProj in **row-major** order
- WGSL `mat4x4f` reads **column-major** from memory → gets the transpose
- Shader does `VP * pos` which with the transpose = right-multiply `pos * VP` in row-major
- This matches the Milo engine's row-major right-multiply convention — verified working

### Font Texture Path

8bpp palette-indexed textures → `PixelColor()` → `PaletteColor(PixelIndex())` → RGBA expansion → GPU upload as RGBA8. Shader flag `useAlphaAsRGB=1.0` for text meshes: `baseColor.rgb * texColor.a, baseColor.a * texColor.a`. Glyph shape is in alpha channel.

## Visible Text in Screenshots

Frame 200 shows:
- Copyright text at bottom: "2012 Harmonix Music Systems, Inc. All rights reserved..."
- Instruction fragments: "highlight selections", "<alt>"
- Partial headings and UI text

## Issues Found (Non-Blocking)

1. **SizeCheck calls UpdateText every frame** — wasteful but correct. Original engine uses dirty-flag tracking.
2. **Some labels have alpha=0** (intentionally hidden) — `label.lbl` with alpha=0.000 is correctly skipped.
3. **Some chars bail from SetupCharacter** with `page=-1` — characters not in the font's char map (e.g., `<`, `%`, `f` when using a limited font atlas). These render as blank spaces.

## Files Involved

| File | Role |
|------|------|
| `src/system/ui/UILabel.cpp` | Label → font color override → RndText::DrawShowing |
| `src/system/rndobj/Text.cpp` | UpdateText, SetupCharacter, DrawMesh, font map management |
| `src/system/rndobj/Font.cpp` | CharWidthAdvanceCoords, CharPage, font atlas UV lookup |
| `native/src/platform/Mesh_Wgpu.cpp` | GPU upload, transparent queue, isTextMesh detection |
| `native/src/platform/Rnd_Wgpu.cpp` | WriteSceneUniforms, viewProj matrix, EnsureSceneUniformsCurrent |
| `native/src/gfx/standard_wgsl.inc` | useAlphaAsRGB shader flag, font alpha-as-grayscale |
| `native/src/gfx/TextureConvert.cpp` | 8bpp palette → RGBA expansion |
