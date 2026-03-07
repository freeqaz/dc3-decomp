# OG Baseline Regression Recovery

**Date**: 2026-03-07
**Scope**: Fix regressions visible when comparing current build against `../og-dc3-decomp/` baseline
**Result**: Reduced regressed units from 25 to 19, eliminated ~3 KB of regression impact

## Context

After merging upstream changes into `dev`, comparing against the `og-dc3-decomp` baseline revealed 25 regressed units totaling -5.2 KB. This session (spanning 3 conversations) identified root causes and fixed what was fixable.

## Fixes Applied

### UILabel (-10.50% -> fixed)
- Rewrote UILabel.h and UILabel.cpp to match og baseline
- Key: `ResourceDirPtr<UILabelDir> mFontResource` (not ObjPtr with ifdefs)
- Restored `__forceinline operator=` on LabelStyle
- Removed `unk28` member and `~LabelStyle()` destructor
- Restored `BEGIN_CUSTOM_PROPSYNC(UILabel::LabelStyle)` pattern
- Restored `INIT_REVS(0x21, 1)` macro usage
- Used `gMe` static pointer (not `sLabel`)

### UIListProvider (-7.59% -> fixed)
- Restored og `DataProvider::Text` with correct parameter usage (`j` not `i`)
- Used `stlpmtx_std::list<Symbol>::iterator` instead of `auto`
- Restored fluid width update: `mWidths[j] = label->BoundsRight()`

### DancerSkeleton (-2.24% -> fixed)
- Reverted `PaddedJointPos` back to `Vector3` for `mCamJointPositions` and `mCamJointDisplacements`
- DancerSkeleton uses 12-byte Vector3 (not 16-byte PaddedJointPos)

### UIScreen (-0.80% -> fixed)
- Restored inline `EnterGlitchCB`/`UnloadGlitchCB` definitions before constructor
- Moved `BEGIN_HANDLERS(UIScreen)` back to og position (after constructor)
- Restored `sUnloadingScreen = nullptr` initialization
- Restored og `Draw()` pattern (positive `if(mShowing)`, combined condition, `prop && prop->Int() != 0`)
- Restored og `Print()` pattern (local `bool a` for register lifetime)
- Restored og `Enter()` (parameter name `scr`, `const char*` vector, `report` variable, `MILO_LOG`)
- Restored og `SetTypeDef()` (implicit null checks, `panelsArr->Type(i)`, no `class` keyword)
- Restored og `PanelRef` constructor (body init, not init-list)

### PanelDir (-0.46% -> fixed)
- Restored og `Save` (`!IsProxy()` check, separate `bs <<` statements, `mUseSpecifiedCam`)
- Restored og `PreLoad`/`PostLoad` (LOAD_REVS without semicolons, `d.stream`, `d.PushRev(this)`, `!IsProxy()` + `SetCurViewport`, braces on ifs)
- Restored `gSendFocusMsg = true` initialization
- Restored og `PanelNav` while-loop pattern (og: `while(comp = ComponentNav(...))`, not do-while+goto)
- Restored `FOREACH` macro for `GetFocusableComponentList`
- Removed `#include "os/Joypad.h"` and forward declaration

### MetagameStats (-0.02% -> fixed)
- Reverted variable extraction (`label->SetTextToken(statArr->Sym(1))` direct call)
- Restored og symbol declaration order (`stats_favorite_na`, `perform`, `practice`, `dancebattle`)
- Reverted `Localize()` variable extraction
- Reverted `(unsigned int)(int)(unsigned int)fave == i` back to `fave == i`

### Sound (partial: -1.85% -> -1.61%)
- Fixed `Clamp` argument order: `Clamp(sSpeedCaps[0], sSpeedCaps[1], CalcSpeedFromTranspose(...))`

### Other reverts (no measurable impact - header cascade)
- Fur: Reverted condition flip (`0x20 > bs.rev` back to `bs.rev < 0x20`) and cast removal
- MoggClip: Reverted `Hmx::Object::Load(bs)` back to `LOAD_SUPERCLASS(Hmx::Object)`
- UIList: Restored `mAutoScrollDir = -mAutoScrollDir` (simple negation), inline `NumData()`

## Remaining Regressions (unfixable)

All remaining regressions are caused by header cascades or correct structural changes:

| Unit | Delta | Root Cause |
|------|-------|------------|
| SongMetadata | -17.76% | MakeString.h inlining budget change |
| synth/Utl | -8.42% | `CacheFile` virtual added to FileCacheHelper |
| MeterEffectMonitor | -3.45% | Object.h header cascade (CONST_ARRAY in BEGIN_HANDLERS) |
| FlowSetProperty | -2.59% | **Not a regression** - current has real implementations vs og stubs |
| JointUtl | -2.45% | Correct PaddedJointPos type (og had wrong Vector3) |
| Emitter | -2.43% | Vec.h header cascade (Interp rewrite, PaddedJointPos) |
| CharLipSync | -2.07% | Complex permuter changes + String->FilePath type change |
| Sound | -1.61% | Header cascade affecting ObjRefConcrete template instantiations |
| Song | -0.70% | Header cascade (source identical to og) |
| UIList | -0.64% | Header cascade |
| BaseSkeleton | -0.49% | Correct PaddedJointPos type |
| Fur | -0.48% | Header cascade (LoadOld function) |
| MoggClip | -0.30% | Header cascade (Play function) |
| Small units | <-0.15% each | Header cascade (Faders, MetaMusic, PoseFatalities, HamPanel, Cheats, PlaylistSort) |

## Key Learnings

1. **Header cascades dominate**: ~80% of regressions are from changes to shared headers (Object.h, Vec.h, FileCache.h, MakeString.h). These affect every compilation unit that includes them.

2. **PaddedJointPos is correct**: The og used `Vector3` (12 bytes) for TrackedJoint fields, but the struct size annotation (0x74) only works with `PaddedJointPos` (16 bytes). The og was structurally wrong.

3. **Function position matters**: Moving `BEGIN_HANDLERS` or function definitions changes string literal/static ordering which cascades into codegen.

4. **PanelRef constructor pattern matters**: Body-init (`mLoaded = false; mActive = true;`) vs init-list (`mLoaded(false)`) generates different code for the inlined constructor at call sites.

5. **Loop style interacts with headers**: The og `while(comp = ...)` pattern matched with og headers, but with current headers the do-while+goto style may match better. Header changes can make previously-matching patterns worse.

## Remaining Work

### HEAD Regressions to Investigate
The og-baseline fixes introduced some regressions vs HEAD:
- `PanelDir::PanelNav` 96.7% -> 74.8% (og while-loop worse with current headers)
- `UILabel::CenterWithLabel` 92.1% -> 54.7% (og UILabel rewrite side-effect)
- `UILabel::Highlight` 100% -> 94.2% (og UILabel rewrite side-effect)
- `WavFileCacheHelper::CacheFile` 100% -> 0% (removed class to match og)
- ByteGrinder ops (header cascade from changes)

### Strategy for Next Session
1. **Resolve HEAD regressions**: Revert PanelNav to HEAD style (do-while+goto), investigate UILabel functions
2. **Restore WavFileCacheHelper**: Re-add the class to Utl.h since it helps HEAD without hurting og much
3. **Consider header-level fixes**: Object.h CONST_ARRAY, Vec.h Interp — evaluate if original patterns can be preserved with `#ifdef` guards
4. **CharLipSync**: Decide whether to revert permuter changes and String->FilePath type change
