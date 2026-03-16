# Native Port: Remaining Work

**Status**: Gameplay reached! Menu flow → song select → venue load → game_screen all working.
**Last Updated**: 2026-03-16

## What's Done

- **DTA Script Execution**: Full DTA interpreter works. Menu flow, content providers, animation lifecycle all functional.
- **Text Labels**: Locale loading fixed (RegionInit + Locale::Init loads 2091 tokens). All menu text renders localized strings.
- **Content Provider Population**: 62 songs load via content system. Song select, choose mode, all menu lists populated.
- **Visual Polish**: Skinned mesh rendering (GPU skinning), post-processing (bloom, contrast, saturation, vignette, chromatic aberration, posterization), RndFlare, RndParticleSys, RndLine all working.
- **Audio**: Full MOGG decryption + Vorbis decode pipeline. Real-time song playback verified.
- **Camera**: Song.anim PropKeys drive camera cuts during gameplay.
- **Character Animation**: CharClip playback synced to beat during gameplay.
- **Web Build**: Emscripten/WASM port running in browser (`scripts/build/web.sh`). Manual navigation to gameplay works.
- **MetaMaterials**: `shell_basic.mmat` and shared MatAnim resources load correctly. DC3 logo visible on main menu.
- **Full Gameplay Navigation** (session 76): Headless input script navigates main_screen → choose_mode → song_select → multiuser → loading → game_screen. Venue renders, practice mode UI works, pause dialog functional.

## Active Workarounds / Hacks (follow-up needed)

These are temporary fixes to unblock gameplay. Each needs a proper root-cause investigation:

### 1. HamNavList::Poll IsAnimating() bypass (`HamNavList.cpp:504`)
**Hack**: Skipped `!RndAnimatable::IsAnimating()` check on HX_NATIVE in the select-completion poll.
**Why**: DTA `transition_complete` handlers that call `StopAnimation()` don't fire on native, so `IsAnimating()` stays true forever and `nav_select_done` never fires.
**Root cause to investigate**: Why don't DTA transition_complete handlers fire? The same bypass was already in the ButtonDownMsg handler (line 1513-1516) but not in Poll.
**Impact**: Without this fix, confirm on song_select (and other HamNavList screens) never completes the selection.

### 2. CharClipGroup null clip guards (`CharClipGroup.cpp`)
**Hack**: Added `#ifdef HX_NATIVE` null checks in `FindClip()` and `GetClip()` to skip null clip pointers.
**Why**: Crowd characters reference clips from subdirectories like `char/crowd/anim/shared_clips.milo`. On native, the venue loading path (App.cpp) doesn't load these subdirs, so CharClip ObjPtrVec entries resolve to null.
**Root cause to investigate**: Native venue loading should load crowd character clip subdirs. The DTA character loading pipeline (`{$hamwardrobe add_crowd $this}`) handles this on Xbox but isn't wired on native. Needed assets:
  - `world/shared/gen/crowd_plane_small.milo_xbox` (crowd mesh)
  - `char/crowd/anim/shared_clips.milo` (crowd animation clips)
  - Any other crowd character subdirs referenced by venue WorldCrowd objects
**Impact**: Without this fix, CharClipGroup::Copy crashes with null deref during venue .milo merge.

### 3. WorldCrowd rendering stubs
**Status**: `BuildBillboard()`, `DrawShowing()`, `AssignRandomColors()` etc. are still weak stubs. WorldCrowd IS used in some venues (e.g., dclive) — the Phase C "NOT APPLICABLE" conclusion was wrong. Crowd billboard rendering won't work until BuildBillboard returns a real mesh and the impostor texture pipeline is wired.

## Remaining Work — Phased

### Phase A: Move Card UI Visibility
HUD panels are active during gameplay but move card content is invisible. Likely a TexMovie or render-to-texture asset wiring issue. This is the most visible gameplay gap.

**Research needed:**
- How does the move card pipeline work? (TexMovie → texture → HUD mesh)
- What assets are involved and are they loading?
- Is the render-to-texture path firing?

### Phase B: Score / HUD Display
Score numbers not wired to gameplay HUD. Need to understand what drives score updates (OnMovePassed callbacks, gesture detection) and whether stubs are blocking it.

**Research needed:**
- What drives score accumulation? (gesture detection → scoring → HUD update)
- Which stubs need real implementations vs which can be wired with simple hooks?
- Are the HUD label elements rendering but showing 0, or not rendering at all?

### Phase C: WorldCrowd Rendering
WorldCrowd IS used in some venues (dclive has WorldCrowd objects, glitterati does not). The billboard impostor system needs:
- `BuildBillboard()` real implementation (creates 4-vert quad mesh with impostor material)
- Impostor texture pipeline (RTT for crowd characters)
- Loading crowd character subdirs during native venue load

### Phase D: Cosmetic Polish
Lower-priority visual items that improve fidelity but aren't blocking gameplay:

| Task | Notes |
|------|-------|
| Text markup `<alt>` tags | Render as literal text instead of styling |
| Projected light textures | Gobo/spotlight cookies |
| Lip sync | CharFaceServo, CharLipSyncDriver |
| Procedural blinking | CharFaceServo cosmetic |
| CharEyes gaze direction | Cosmetic |
| Exotic post-processing | Gradient map, kaleidoscope, flicker, noise |

### Phase E: Infrastructure
Non-visual features for a complete experience:

| Task | Notes |
|------|-------|
| Save/load game progress | Profile, unlocks |
| Performance optimization | Draw call batching, culling |
| macOS / Windows support | WebGPU handles backends |

## Decomp Bug Discovery Pattern

Each native port session has historically uncovered 1-2 real decomp bugs:
- Session 41: Transform::Multiply y/z swap
- Session 40: ScrollDirection missing vertical mode (66.1% → 100%)
- Session 30: ObjOwnerPtr::RefOwner() wrong member
- Session 12: FlowAnimate::Load skip mAnim at rev>=3 (85.9% → 90.7%)
- Session 70: UIListSlot async element race condition (WASM crash fix)
- Session 71: MetaMaterials disabled on native (198 shell_basic.mmat warnings)
- Session 76: CharClipGroup::FindClip null deref (null check before Name() call)

This makes native port work a force multiplier for decomp quality.

## Build & Test

```bash
# Native (x86_64 Linux)
cmake --build native/build --target dc3-native -- -j$(nproc)

# Web (Emscripten/WASM)
scripts/build/web.sh

# Screenshots — default menu flow
bash scripts/gpu/screenshot.sh -f 300,500 native/build/dc3-native

# Screenshots — gameplay flow (with input script)
MILO_FIRST_SCREEN=main_screen MILO_INPUT_SCRIPT=native/test_assets/gameplay_nav.input \
  bash scripts/gpu/screenshot.sh -f 100,400,500,800 native/build/dc3-native
```
