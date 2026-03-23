# Native Port — Status & Plan

**Last updated**: 2026-03-23

The native port operates 1:1 with the Xbox version's DTA-driven pipeline. No bypass hacks remain. The full screen chain runs through the same DTA handlers, FileMerger loading chain, and panel hierarchy as the original game.

---

## Status: Song Audio Decryption Fixed

### What Works

| Feature | Status | Notes |
|---------|--------|-------|
| DTA screen flow | Working | boot → attract → title → main → choose_mode → song_select → multiuser → loading → game_screen |
| Venue rendering | Working | Full 3D venues with PostProc (bloom, color grading), lights, meshes |
| Character rendering | Working | Skinned meshes, outfit loading, proper lighting (RndEnvironTracker) |
| Character animation | **Working** | LP64 fix in LayerArray::Eval (offset 8→mWeight); HamDriver bootstrap for zero-init heap (2026-03-23) |
| Camera shot cycling | Working | PropKeys target matching via SameObject (vbase fix) |
| Song audio (v0xE) | **Working** | Full AES-CTR decrypt pipeline: KeyChain + GrindArray + HMXA→OggS (2026-03-23) |
| SFX audio (v0xB) | Working | Hardcoded gRB1Key, always worked |
| Beat-driven timing | **Working** | Audio drives songMs → TheTaskMgr.Beat(); wall-clock fallback when audio fails |
| Move cards | Working | Flashcard dock, choreography system |
| Async loading | Working | FileMerger, FileMergerOrganizer, DirLoader — all through DTA flow |
| Input scripting | Working | MILO_INPUT_SCRIPT drives menus via button presses |
| Menu backgrounds | Rendering | turbo_shell scene visible, camera orientation needs alignment |
| Menu text | Rendering | All items legible, positions offset ~15% from Xbox |
| Gameplay HUD | Partial | Score, song name visible; star meters, multiplier need work |

### What Was Removed

All native scaffolding hacks have been removed or replaced by the engine's own DTA paths:

| Hack | Removed | Replacement |
|------|---------|-------------|
| `gNativeVenueDir` global | Yes | `HamDirector::mVenue` set by DTA on_file_loaded |
| `NativeVenueInit()` | Yes | `HamDirector::Enter()` → `VenueEnter()` |
| App.cpp venue poll/draw | Yes | `world_panel` draws through panel hierarchy |
| `DC3_SCREEN` auto-nav | Yes | Full DTA menu flow via input script |
| `SetShowing(true)` hack | Yes | DTA `WORLD_SETUP_CHARACTERS` in worldbase.dta |
| `screen_image` MeshFilter | Yes | Render target root cause fix (256x256 default) |
| `consoleScreens` MeshFilter | Yes | Same render target fix |

### Key Fixes Applied

| Fix | File | Impact |
|-----|------|--------|
| **Mogg v0xE decrypt** (2026-03-23) | `ByteGrinder.cpp`, `VorbisReader.cpp` | Song audio decrypts: pure C++ GrindArray, native magicHash |
| **Beat-freeze fix** (2026-03-23) | `Game.cpp` | `mRealTime=true` on audio fail → beats advance from wall clock |
| `SameObject()` vbase comparison | `PropAnim.cpp` | Camera shots cycle (PropKeys target matching) |
| Render target 0x0 default | `Tex_Wgpu.cpp` | No white rectangles from empty render targets |
| `HamPanel::Exiting()` | `HamPanel.cpp` | Menu transitions work (gesture animations don't block) |
| `RndEnvironTracker` for characters | `Character.cpp` | Characters lit correctly during gameplay |
| `CharBones::Zero()` null guard | `CharBones.cpp` | No crash from uninitialized bone data |
| `Flow::~Flow()` cascade guard | `Flow.cpp` | No use-after-free during panel unload |
| `UIScreen::UnloadPanels` batch delete | `UIScreen.cpp` | Panels unload without cascade corruption |
| `DirLoader::Find` path normalization | `DirLoader.cpp` | Shared subdirs (director.milo) resolve correctly |
| `ReplaceRefsFrom` vbase fix | `Object.cpp` | ObjRef ring walks handle vbase offsets |

---

## Next Steps

### Priority 1: Character Dance Animation — FIXED (2026-03-23)
**Root cause**: LP64 bug in `HamDriver::LayerArray::Eval()` — raw pointer access `*(float*)((char*)(*it) + 8)` reads `mBeat` on 64-bit (vtable=8 bytes) instead of `mWeight` (offset 12). Fixed to `(*it)->mWeight`. PPC codegen identical (100% match preserved). Also: `HamDriver::Poll()` bootstrap needed for zero-initialized heap (Xbox gets garbage non-zero mWeight from pool allocator). Diagnostic logging (`ANIM-DIAG`, `CLIP-DIAG`, `SHOT-DIAG`, `REMIXER-DIAG`) removed.

### Priority 2: turbo_shell Camera Orientation
The menu background renders but the gradient pattern is shifted/rotated vs Xbox.

**Files**: `src/system/rndobj/Cam.cpp` (GetViewProjectXfms, sFlipYZ), `src/system/ui/PanelDir.cpp` (CamOverride)

### Priority 3: UI Text Positioning
Menu text positions are offset ~15% vertically from Xbox.

**Files**: `src/system/ui/UI.cpp` (Draw, camera selection), `native/src/platform/Rnd_Wgpu.cpp` (viewport)

### Priority 4: PostProc Flush Timing
PostProc runs at end-of-frame, affecting UI text. Xbox flushes before UI overlay.

**Files**: `src/system/ui/PanelDir.cpp` (DrawShowing, mCanEndWorld), `native/src/platform/Rnd_Wgpu.cpp`

### Priority 5: Cleanup diagnostic printfs — DONE (2026-03-23)
All `ANIM-DIAG`, `CLIP-DIAG`, `SHOT-DIAG`, `REMIXER-DIAG`, and `HamDirector::Enter` debug printfs removed from HamDirector.cpp, ClipPlayer.cpp, HamCamShot.cpp, OriginalChoreoRemixer.cpp.

---

## Test Commands

```bash
# Full DTA flow — boot to gameplay (no bypasses)
MILO_HEADLESS=1 MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=10000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  DC3_DATA=orig-assets native/build/dc3-native

# With screenshots during gameplay
MILO_SCREENSHOT_DIR=/tmp/screenshots MILO_SCREENSHOT_FRAMES=8000,8500 \
  # add to above

# Frame capture (dumps all draw calls for a specific frame)
MILO_CAPTURE_FRAME=8000 \
  # add to above

# UI flow debugging
MILO_DEBUG_UI_FLOW=1 \
  # add to above

# ASan build
cmake -S native -B native/build-asan -DENABLE_ASAN=ON && \
  cmake --build native/build-asan --target dc3-native
```

## Reference Docs

| Document | Content |
|----------|---------|
| `docs/sessions/convergence/01-hx-native-audit.md` | 865 HX_NATIVE blocks cataloged |
| `docs/sessions/convergence/02-boot-to-gameplay-flow.md` | Xbox vs Native flow trace |
| `docs/sessions/convergence/03-dependency-graph.md` | Hack dependency map |
| `docs/sessions/convergence/04-background-panel-rendering.md` | turbo_shell rendering check |
| `docs/sessions/convergence/05-filemerger-async-pipeline.md` | Async loading analysis |
| `docs/sessions/convergence/06-synthesis-final.md` | Implementation guide |
| `docs/sessions/convergence/07-vbase-comparison-audit.md` | Virtual base pointer audit |
| `docs/sessions/2026-03-21-convergence-session.md` | Session 1 notes |
| `docs/sessions/2026-03-22-vbase-propkeys-fix.md` | Virtual base fix details |
| `docs/sessions/2026-03-22-convergence-final.md` | Session 2 final status |
| `docs/debugging/native.md` | Debugging techniques (ASan, ObjRef rings) |
| `archive/screenshots/references/` | Xbox target screenshots |
