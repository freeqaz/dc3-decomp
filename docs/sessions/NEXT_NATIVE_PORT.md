# Native Port: Remaining Work

**Status**: Most features complete. Polish remaining.
**Last Updated**: 2026-03-15

## What's Done

- **DTA Script Execution**: Full DTA interpreter works. Menu flow, content providers, animation lifecycle all functional.
- **Text Labels**: Locale loading fixed (RegionInit + Locale::Init loads 2091 tokens). All menu text renders localized strings.
- **Content Provider Population**: 62 songs load via content system. Song select, choose mode, all menu lists populated.
- **Visual Polish**: Skinned mesh rendering (GPU skinning), post-processing (bloom, contrast, saturation, vignette, chromatic aberration, posterization), RndFlare, RndParticleSys, RndLine all working.
- **Audio**: Full MOGG decryption + Vorbis decode pipeline. Real-time song playback verified.
- **Camera**: Song.anim PropKeys drive camera cuts during gameplay.
- **Character Animation**: CharClip playback synced to beat during gameplay.
- **Web Build**: Emscripten/WASM port running in browser (`scripts/build/web.sh`).

## Remaining Items

### Medium Priority
| Task | Notes |
|------|-------|
| Move card UI visibility | HUD panels active but move card content invisible — TexMovie asset wiring issue |
| Score display | Not yet wired to gameplay |
| Text markup `<alt>` tags | Render as literal text instead of styling |
| DC3 logo | Missing from main menu — TexRenderer or subdir not loaded |

### Low Priority
| Task | Notes |
|------|-------|
| WorldCrowd rendering | Crowd character instancing system |
| Projected light textures | Gobo/spotlight cookies |
| Lip sync | CharFaceServo, CharLipSyncDriver |
| Procedural blinking | CharFaceServo cosmetic |
| CharEyes gaze direction | Cosmetic |
| Exotic post-processing | Gradient map, kaleidoscope, flicker, noise |
| Performance optimization | Draw call batching, culling |
| Save/load game progress | Profile, unlocks |
| macOS / Windows support | WebGPU handles backends |

## Decomp Bug Discovery Pattern

Each native port session has historically uncovered 1-2 real decomp bugs:
- Session 41: Transform::Multiply y/z swap
- Session 40: ScrollDirection missing vertical mode (66.1% → 100%)
- Session 30: ObjOwnerPtr::RefOwner() wrong member
- Session 12: FlowAnimate::Load skip mAnim at rev>=3 (85.9% → 90.7%)
- Session 70: UIListSlot async element race condition (WASM crash fix)

This makes native port work a force multiplier for decomp quality.

## Build & Test

```bash
# Native (x86_64 Linux)
cmake --build native/build --target dc3-native -- -j$(nproc)

# Web (Emscripten/WASM)
scripts/build/web.sh

# Screenshots
bash scripts/gpu/screenshot.sh -f 300,400 native/build/dc3-native
```
