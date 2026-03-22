# Convergence Session 2 — 2026-03-22 Final Status

## Achievement: Native port converged with Xbox DTA flow

The native port now operates 1:1 with the Xbox version's DTA-driven pipeline.
No bypass hacks, no auto-nav, no manual venue init. Everything flows through
the same DTA handlers, FileMerger loading chain, and panel hierarchy as Xbox.

## Commits This Session

```
a4f03dc74 native: remove SetShowing hack — DTA WORLD_SETUP_CHARACTERS handles it
5e40c00c0 native: fix render target white rectangles at root cause
c613ba276 native: remove DC3_SCREEN bypass — full DTA flow is the only path
873f5eb0e native: skip consoleScreens meshes (white rectangle fix)
a8c5dbdea docs: session notes for 2026-03-22 — vbase fix, character visibility, frame capture
e4d5973aa native: force characters visible + skip venue screen meshes
97a957dd5 native: wire FrameCapture into render loop + white rectangle identified
88361058f fix: guard SameObject with HX_NATIVE to restore PPC match%
3ea7c6676 fix: remove debug fprintf from SameObject (broke PPC build)
532643bf8 native: fix camera shot cycling via PropKeys virtual base comparison
049d39723 docs: vbase pointer comparison audit — 21 sites, 1 HIGH risk
```

## Hacks Removed

| Hack | Lines Removed | Replacement |
|------|--------------|-------------|
| DC3_SCREEN bypass | -108 | Full DTA menu flow via input script |
| SetShowing(true) | -57 | DTA WORLD_SETUP_CHARACTERS in worldbase.dta |
| screen_image MeshFilter | -4 | Render target root cause fix (256x256 default) |
| consoleScreens MeshFilter | -4 | Same render target fix |

## Root Causes Fixed

| Issue | Root Cause | Fix |
|-------|-----------|-----|
| Camera stuck on intro | PropKeys vbase pointer mismatch (Itanium ABI) | SameObject() with dynamic_cast<void*> |
| White rectangles | 0x0 render targets rejected by EnsureRenderTargetData | Default to 256x256 cleared to black |
| Character visibility | DTA WORLD_SETUP_CHARACTERS already handles it | Removed redundant hack |

## Remaining for UI Parity (Phase 4)

1. **turbo_shell camera orientation** — background gradient pattern shifted vs Xbox due to camera transform + sFlipYZ axis interaction
2. **UI element positioning** — menu text positions offset ~15% vertically from Xbox
3. **PostProc flush timing** — FlushPostProcessingForOverlay needed so blacklight PostProc doesn't affect UI text overlay
4. **Gameplay HUD completeness** — score, star meters, multiplier need selective showing (currently some hidden to avoid Kinect-dependent white rects)
5. **Font3d mesh warnings** — 48 missing char_u*.mesh, non-critical (bitmap fonts work fine)

## Verified Working

- Full DTA screen chain: boot → attract → title → main → choose_mode → song_select → multiuser → loading → preloading → real_loading → game_screen
- Characters dancing on venue stage with proper lighting
- Camera shots cycling through the song
- Song audio playing with advancing beats (0 → 265+)
- Move cards cycling
- PostProc bloom/glow
- No white rectangles
- 10000 frames stable
- No DC3_SCREEN, no gNativeVenueDir, no NativeVenueInit
