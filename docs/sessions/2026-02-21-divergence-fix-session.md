# 2026-02-21: Divergence Fix Session

Focused on fixing unicorn-flagged divergent functions (object_memory, return_value classes) to improve behavioral correctness.

## Strategy

Queried decomp.db for functions with unicorn divergence in fixable classes:
- **object_memory** (19 total): Constructor/destructor writing wrong values to fields
- **return_value** (17 total): Functions returning incorrect values for some inputs
- Excluded build_env (623) and regalloc (18) as unfixable

## Functions Fixed

| Function | Class | Before | After | Fix Summary |
|----------|-------|--------|-------|-------------|
| PageDirection | return_value | 98.8% | **100%** | `return act != kAction_PageUp` was wrong; needed -1/0/1 three-way return |
| Block::Block | object_memory | 86.8% | **98.8%** | `static char *gBuffers` should be `static char gBuffers[...]` (array not pointer) |
| Splash::ShowNext | return_value | 79.6% | **99.8%** | 3 bugs: wrong return value, `clear()` instead of `erase(begin())`, CritSecTracker scope |
| EraseNewerData | return_value | 90.7% | **99.8%** | Two-phase find+erase pattern instead of erase-in-loop |
| UIList::CollidePlane (private) | return_value | 3.6% | **99.8%** | Full implementation: triangle-plane collision with ternary codegen |
| UIList::CollidePlane (virtual) | return_value | 33.5% | **99.4%** | Iterator-based algorithm matching RB3 reference |
| SynapseAPOParams ctor | object_memory | 70.6% | **98.0%** | Proper SynapseBand[3] struct layout with correct field order |
| CXboxHeap ctor | object_memory | 83.8% | **93.4%** | Self-referential circular list sentinel init |
| HasKinectSharePrvilege | return_value | 80.5% | **85.5%** | Inverted return logic (was returning true regardless of privilege) |

## Functions Confirmed AT_LIMIT (Unfixable)

| Function | Class | Match | Root Cause |
|----------|-------|-------|------------|
| FitnessCalorieSort::BuildTree | - | 99.3% | Register swap r24/r25 |
| RndOverlay::CurrentLine | call_count | 99.8% | Register swap r30/r29 |
| UsbMidiGuitar::Poll | - | 99.2% | rlwinm encoding difference |
| DirLoader::FixClassName | - | 90.4% | Symbol relocation noise |
| FlangerEffect::SetParameters | object_memory | 90.7% | Float constant pooling (fdivs vs fmuls) |
| MultiTempoTempoMap dtor | object_memory | 88.1% | Vtable optimization difference |
| HamDriver::LayerClip dtor | object_memory | 77.8% | Vtable optimization + regswap |
| NetLoaderRef::IsDownloading | return_value | 88.6% | BOOL_MASK pattern |
| NetLoaderRef::NeedsToDownload | return_value | 94.3% | BOOL_MASK pattern |

## Previous Session Fixes (carried forward, uncommitted)

| Function | Class | Before | After | Fix Summary |
|----------|-------|--------|-------|-------------|
| DateTime::DateTime(uint) | object_memory | 32.2% | **97.8%** | Complete implementation with subtract-product pattern |
| BinkMovieSys ctor | object_memory | 32.2% | **99.7%** | Fixed init values, body vs init-list ordering |
| GetPctHeightFromTextSize | - | 2% | **99.1%** | RndCam WorldToScreen implementation |
| GetTextSizeFromPctHeight | - | 2% | **99.2%** | RndCam ScreenToWorld implementation |
| JointDistPoseElement ctor | object_memory | 16.2% | **59.2%** | Added missing member inits |
| Rand::Seed | object_memory | 84.5% | **75.2%** | Fixed bit manipulation bug (correct behavior, lower asm match) |

## New Code Added

- `operator<=(Vector3, Plane)` inline in `src/system/math/Mtx.h`
- `SynapseBand` struct in `src/system/synth_xbox/FxSendSynapse.h`

## Key Insights

1. **Unicorn divergence is the north star** for finding real behavioral bugs vs assembly noise
2. **object_memory** class reliably points to wrong field initializations in constructors
3. **return_value** class reliably points to logic bugs in conditionals
4. **call_count** and **stack_layout** are usually unfixable compiler artifacts
5. **BOOL_MASK** pattern (compiler generates different boolean materialization) is consistently unfixable
6. **Float constant pooling** across translation units creates unfixable fdivs/fmuls differences

## Remaining Targets

Non-build_env/regallop divergent functions not yet investigated:
- `call_count`: 476 functions (avg 79.4%) - mostly merged symbol / ICF artifacts
- `error`: 224 functions (avg 81.4%) - mostly __FILE__ path differences
- `stack_layout`: 123 functions (avg 89.5%) - compiler stack frame differences
- `fpr_precision`: 7 functions (avg 96.3%) - floating point precision differences
