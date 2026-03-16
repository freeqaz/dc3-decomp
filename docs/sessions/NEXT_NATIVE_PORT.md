# Native Port: Remaining Work

**Status**: Core features complete. Visual polish phases in progress.
**Last Updated**: 2026-03-16

## What's Done

- **DTA Script Execution**: Full DTA interpreter works. Menu flow, content providers, animation lifecycle all functional.
- **Text Labels**: Locale loading fixed (RegionInit + Locale::Init loads 2091 tokens). All menu text renders localized strings.
- **Content Provider Population**: 62 songs load via content system. Song select, choose mode, all menu lists populated.
- **Visual Polish**: Skinned mesh rendering (GPU skinning), post-processing (bloom, contrast, saturation, vignette, chromatic aberration, posterization), RndFlare, RndParticleSys, RndLine all working.
- **Audio**: Full MOGG decryption + Vorbis decode pipeline. Real-time song playback verified.
- **Camera**: Song.anim PropKeys drive camera cuts during gameplay.
- **Character Animation**: CharClip playback synced to beat during gameplay.
- **Web Build**: Emscripten/WASM port running in browser (`scripts/build/web.sh`).
- **MetaMaterials**: `shell_basic.mmat` and shared MatAnim resources load correctly. DC3 logo visible on main menu. (198 warnings → 0)

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

### ~~Phase C: WorldCrowd Rendering~~ — NOT APPLICABLE
DC3 does not use WorldCrowd. All 6 venues checked — zero WorldCrowd objects. DC3 has abstract dance stages without audience sections (unlike Rock Band). The WorldCrowd system is inherited from the shared Milo engine but unused in DC3.

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

This makes native port work a force multiplier for decomp quality.

## Build & Test

```bash
# Native (x86_64 Linux)
cmake --build native/build --target dc3-native -- -j$(nproc)

# Web (Emscripten/WASM)
scripts/build/web.sh

# Screenshots
bash scripts/gpu/screenshot.sh -f 300,500 native/build/dc3-native
```
