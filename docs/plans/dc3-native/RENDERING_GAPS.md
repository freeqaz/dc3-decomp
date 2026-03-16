# DC3 Native — Rendering Gap Analysis (Session 30 Update)

## Current State

choose_mode_screen renders with geometry, text, animated materials, and working PropAnim pipeline. UI elements visible but dim compared to reference (bright cyan neon UI).

- 51 mesh draw calls with real vertex data
- Text rendering working ("Jump right in and Perform!", "PLAYERS: 1 - 2")
- **PropAnim → Material pipeline fully operational** (fixed session 30)
- Material alpha values animate correctly (e.g., autosave_icon fades 1.0→0.08)
- Letterbox rules, vertical beam stripes, and UI geometry all visible
- Remaining dimness is shader/blend related (intensify, additive glow)

Reference: `archive/screenshots-old/references/dc3_main_menu.jpg`
Current: `archive/screenshots/session30/frame_00600.png`

## Session 30 Fixes

### ObjOwnerPtr::RefOwner() decomp bug (ROOT CAUSE)

**Bug**: `ObjOwnerPtr<T>::RefOwner()` returned `mObject ? mObject->RefOwner() : nullptr`. During `ObjRefConcrete::Load()`, `mObject` is null (not yet loaded), so `RefOwner()` returned null, preventing the dir lookup from finding the target object. Every PropKeys target resolved to null.

**Fix**: Changed to `return mOwner->RefOwner()` (matching RB3 reference) in native build (`#ifdef HX_NATIVE`). PPC decomp path unchanged.

**File**: `src/system/obj/ObjPtr_p.h:141-143`

**Impact**: All PropKeys targets (FloatKeys, ColorKeys) now resolve correctly. Material alpha/color values change at runtime via PropAnim keyframe interpolation.

### TypeProps::GetArray null typeDef guard

**Bug**: `TypeProps::GetArray()` asserts `typeDef != null` (line 190), but native build doesn't load DTA type definitions. Properties like `styles` that need array defaults from the typedef crash.

**Fix**: Added `#ifdef HX_NATIVE` null guard to return early when typeDef is missing.

**File**: `src/system/obj/TypeProps.cpp:190,204`

## What's Working (Confirmed)

1. **Full animation pipeline** — PropAnim → PropKeys → SetProperty → Material update → GPU uniform
2. **Milo file loading** — PropAnim objects loaded from milo files (7,811 across all UI milos)
3. **SyncObjects** — animatables collected into `mAnims`, target objects resolved in dir
4. **Animation startup** — `PanelDir::Enter()` auto-starts via `Animate()` on `kTaskUISeconds`
5. **Timer conversion** — correct µs→ms factor for native `__mftb()`
6. **Material property animation** — alpha, color, ambient_alpha all driven by PropAnim keyframes

## Remaining Root Causes (Priority Order)

### ~~1. Missing intensify/glow blend modes~~ (DONE)

Implemented: shader 2.0x color multiplier for `intensify` materials. PipelineManager maps Add/SrcAlphaAdd blend modes. Bright cyan neon effects now render correctly.

### ~~2. Colors desaturated~~ (DONE)

Fixed by intensify shader + additive blend pipeline.

### 3. Missing DANCE CENTRAL 3 logo (MEDIUM)

The big logo is absent. Likely rendered by a TexRenderer (render-to-texture) or loaded from a subdir not yet processed.

### ~~4. Missing text labels~~ (DONE)

Fixed: locale loading now works (RegionInit + Locale::Init populates 2091 tokens). All menu text renders localized strings.

## Stubs Status

### Critical stubs (no real implementation):
| Stub | Impact | Priority |
|------|--------|----------|
| `EventTrigger::TriggerSelf()` | Would fire anim triggers — but 0 EventTriggers exist in UI milos | Low |
| `MsgSinks::MergeSinks()` | Object merge sink propagation | Low |
| `UIPanel::SetPaused()` | Panel pause state | Low |

### Stubs with real implementations (503 total):
These are weak and correctly overridden by the real linker symbols. Not blocking anything.

### Total stubs: ~1,267 function stubs, 46 vtable stubs
Most are for systems not yet needed (Kinect, networking, advanced char animation).

## Assets

- **Extracted .milo files**: `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/`
- **Ark archives**: `./orig-assets/gen/main_xbox.hdr` — full game data
- dc3-native extracts from ark at runtime via `NativeArkRead`

## Next Steps

1. ~~Implement intensify blend mode~~ — DONE (shader 2.0x multiplier)
2. ~~Add additive blend pipeline~~ — DONE (PipelineManager blend modes)
3. **Find the logo** — search for DC3 logo texture/mesh in the milo hierarchy
4. **Move card UI** — HUD panels active but move card content invisible (TexMovie/asset wiring)
