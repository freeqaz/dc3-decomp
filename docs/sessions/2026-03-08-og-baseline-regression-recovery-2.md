# OG Baseline Regression Recovery — Session 2

**Date**: 2026-03-08
**Scope**: Continue reducing regressions against `../og-dc3-decomp/` baseline after merging upstream `main` into `dev`
**Result**: Reduced regressed functions from 163 to 62, affected KB from 62.5 to 34.5

## Context

Continuing from the 2026-03-07 session. The `og-dc3-decomp` directory is a copy of pre-merge upstream `main` — used as a baseline for comparing `dev` branch progress after merge. The comparison tool is `scripts/analysis/compare_progress.py`.

The regressions stem from a straight merge between `dev` and upstream `main`. The merge brought in readable member names, struct improvements, native port guards, and new function bodies across many headers and source files.

## Fixes Applied (Session 2a — earlier)

### FlowSetProperty (-2.59% → eliminated, 33 functions recovered)
- Moved `gEaseFuncs[35]` array back from Easing.cpp to Easing.h (og pattern)
- Made `EaseLinear` inline in header instead of out-of-line in .cpp
- Removed `static` from `sLicense` in Easing.cpp to match og
- **Root cause**: When `gEaseFuncs` was defined in Easing.cpp, the Ease function pointers forced emission into Easing.obj only. With the array in the header, each TU that includes Easing.h gets its own COMDAT copy of the Ease functions, matching the target's FlowSetProperty.obj layout.

### HamVisDir (-3 KB → eliminated, 2 functions recovered)
- Wrapped `SkeletonUpdate::HasInstance()` defensive guards in `#ifdef HX_NATIVE`
- On PPC/Xbox, the singleton lifetime isn't an issue; the guards are only needed for native port
- Constructor: 97.6% → restored to og match level
- Destructor: 100% → restored

### UIList BoundingBoxTriangles (100% → 66% → restored)
- Reverted variable extraction (`boxMinX = box.mMin.x` etc.) back to direct `box.mMin.x` access
- The variable extraction changed register allocation (6 extra callee-saved FPRs for the cached values)

### TexRenderer::Load condition flip
- Reverted `2 < d.rev` back to `d.rev > 2`
- Note: Load function has deeper issues (67.8% match) from structural changes elsewhere in the file

### MemMgr global variable layout
- Restored `int gNumHeaps;` before `MemHeap gHeaps[MAX_HEAPS];` (og position)
- Restored `bool gMemoryUsageTest;` as non-extern definition (og: COMDAT in 3 files)
- Restored og `MemResizeElem` variable declaration order (`prefixSize` before `suffixSize`)
- Also in MemTrack.cpp: restored non-extern `gMemoryUsageTest`

### Previous session fixes (from stash)
- synth/Utl: Restored `WavFileCacheHelper` class
- PanelDir: do-while+goto PanelNav with og symbol name `panel_navigated`
- UILabel::Highlight: signed modulo pattern `!(secs % 2)` instead of `secs < 0`

## Fixes Applied (Session 2b — continuation)

### Synth.cpp SynthPreInit/SynthTerminate (eliminated SynthPreInit regression)
- Removed `#ifdef HX_NATIVE` / `CreateNativeSynth()` block — og has just a commented-out `Synth::New()` call
- Removed `TheSynth->StopAllSounds()`, `delete TheSynth`, `TheSynth = nullptr` from SynthTerminate — og has these commented out as `// RELEASE(TheSynth)`
- **Impact**: SynthPreInit dropped off regression list (was -4.1%, 376 bytes)

### HamSkeletonConverter.cpp — removed ~360 lines of extra function bodies
- Removed `CalcQuatBone`, `CalcRotzBone`, `ScaleBone`, `SetArm`, `SetLeg`, `Set` function bodies (lines 195–557) — og doesn't have them
- Fixed `RotateTowards` control flow to match og (simpler `else` block, `fabsf(angle) < 1e-9` instead of NaN check + inverted branches)
- **Impact**: Didn't fix Enter regression (81.7% — header-driven register swaps) but removes inlining pressure from TU

### LoadingPanel.cpp — removed extra static member definitions
- Removed `HamMaster *LoadingPanel::sLoadingMaster = nullptr;` and `SongDB *LoadingPanel::sSongDB = nullptr;` — og doesn't have them (defined elsewhere or in header)

### MakeString.cpp, Easing.cpp — whitespace cleanup
- Removed extra blank lines to match og exactly

## What Didn't Work (Session 2a)

### MemFree loop revert (reverted back to HEAD)
- OG pattern: `for (i = 0; i < gNumHeaps; i++) { if (gHeaps[i].Free(...)) break; }`
- HEAD pattern: `MemHeap *heap = gHeaps; for (i = 0; i < gNumHeaps; i++, heap++) { freed = heap->Free(...); }`
- OG pattern hurt HEAD by 30% (93.3% → 63.4%). HEAD pattern is better overall. Reverted.

### MemTruncate `/ 4` vs `>> 2` (reverted back to HEAD)
- OG had `(size + 3) / 4`, HEAD has `(size + 3) >> 2`
- Reverting hurt HEAD's MemTruncate. Kept HEAD's version.

### MakeString.h mFmt protected→private
- Can't revert: `SuperFormatString` (subclass) accesses `mFmt` and needs protected access
- OG didn't have SuperFormatString.cpp

### EaseElasticIn stub
- OG has inline stub (just returns `t`), current has full implementation in Easing.cpp
- Not worth reverting to a stub — that's going backwards on decomp progress

## What Didn't Work (Session 2b)

### TransAnim::MakeTransform `float f5 = frame;` placement (reverted)
- OG has `float f5 = frame;` before the `if (mKeysOwner != this)` check
- Our version has it inside the `else` block
- Moving it to match og's placement made things **worse** (97.5% → 95.9%) — header differences change register pressure so our placement is actually better for our headers
- **Learning**: Matching og source doesn't always improve match% when headers differ

### FlowSetProperty PropertyTask constructor init list (can't remove)
- OG has empty body `{}` with no init list — members are presumably default-initialized by og's header definitions
- Our headers require init list (ObjOwnerPtr/ObjPtr need owner pointer passed to constructor)
- Build fails without the init list (C2512: no appropriate default constructor)
- **Learning**: Some og patterns are impossible to replicate when header definitions differ

## HEAD Impact

Only 1 HEAD regression (28 bytes): PanelDir dynamic initializer for static Symbol in PanelNav. Acceptable trade-off for better PanelNav function body match.

## Remaining 62 Regressions — Root Cause Analysis

The remaining regressions are **overwhelmingly header-driven**. The merge brought readable member names and structural additions to key headers that cascade across many TUs.

### Header Changes Causing Cascading Regressions

| Header | Lines Changed | Key Additions | Impact |
|--------|--------------|---------------|--------|
| `Object.h` | 179 | Getter methods, structural changes | Affects all TUs |
| `Text.h` | 148 | Member renames, type changes | RndText codegen |
| `Rnd.h` | 83 | `ModalKeyListener` virtual dtor, getter methods, `TestPoint` decl | rndobj/ TUs |
| `Easing.h` | 56 | Parameter renames in inline functions | FlowSetProperty |
| `UIListWidget.h` | 55 | Member renames | UI TUs |
| `GamePanel.h` | 55 | Member renames | lazer/ TUs |
| `Vec.h` | 28 | `PaddedJointPos` struct, `ZeroVec()` inline | Widely included |
| `UIList.h` | 18 | Member renames | UI TUs |
| `Anim.h` | 14 | Changes | rndobj/ TUs |
| `BaseSkeleton.h` | 14 | Field order change | gesture/ TUs |

### Why These Are Unfixable From .cpp

- **Member renames** (`unkXX` → `mReadableName`): Don't affect codegen directly, but when combined with...
- **Added inline methods** (getters in Rnd.h, `ZeroVec()` in Vec.h): Add function bodies to widely-included headers. MSVC PPC may choose to inline these into callers, changing register allocation.
- **Added structs** (`PaddedJointPos` in Vec.h): Changes template instantiation patterns.
- **Constructor init lists required by our headers**: ObjOwnerPtr/ObjPtr need owner pointer, forcing init lists that og doesn't have.

### Top 10 Regressions (all header-driven)

| Function | Unit | Base | Curr | Change | Root Cause |
|----------|------|------|------|--------|------------|
| SongMetadata::SongMetadata | meta/SongMetadata | 97.6% | 46.4% | -51.3% | MakeString.h mFmt protected |
| ChallengeResultPanel::UpdateList | meta_ham/ChallengeResultPanel | 89.7% | 54.4% | -35.3% | Header field names |
| RndTexRenderer::Load | rndobj/TexRenderer | 97.8% | 68.7% | -29.2% | Header cascade |
| DxRnd::SetVertShaderTex | rnddx9/Rnd | 73.9% | 47.9% | -26.0% | Rnd.h structural changes |
| RndParticleSys::SyncProperty | rndobj/Part | 99.8% | 81.4% | -18.4% | Header cascade |
| HamSkeletonConverter::Enter | hamobj/HamSkeletonConverter | 100.0% | 81.7% | -18.3% | Header cascade + regswaps |
| Font map::operator[] | rndobj/Font | 65.6% | 47.6% | -18.0% | Header cascade |
| FlowSetProperty::ReActivate | flow/FlowSetProperty | 99.1% | 82.0% | -17.2% | PropertyTask init list required |
| JointScreenPos | gesture/JointUtl | 88.8% | 73.1% | -15.7% | Vec.h PaddedJointPos |
| RndText::RndText | rndobj/Text | 98.5% | 84.4% | -14.1% | Text.h member renames |

## Key Learnings

1. **COMDAT array placement matters hugely**: Moving `gEaseFuncs[]` from header to .cpp eliminated 33 functions from FlowSetProperty.obj (6.6 KB). MSVC emits inline function bodies as COMDAT when their address is taken in the same TU.

2. **`#ifdef HX_NATIVE` wrapping is safe for defensive guards**: Guards that protect against singleton lifetime issues on native don't need to exist on PPC. Wrapping them recovers matching without losing native safety.

3. **Variable extraction is not free**: Extracting `box.mMin.x` to `float boxMinX` seems harmless but adds 6 callee-saved FPR allocations, bloating the prologue and shifting all register assignments.

4. **Some MemMgr patterns are HEAD-better**: The `freed` variable + `heap` pointer pattern in MemFree matches HEAD at 93.3% but og only at ~63%. When HEAD and og disagree, HEAD wins (it's the actual working state).

5. **Non-extern global COMDAT is valid**: MSVC handles `bool gMemoryUsageTest;` defined in multiple .cpp files via COMDAT merge. Using `extern` changes which TU "owns" the symbol, affecting BSS layout.

6. **Matching og source ≠ matching target binary**: When headers differ between og and dev, the same .cpp code produces different codegen. Sometimes our "wrong" code accidentally compensates for header differences and produces better codegen than og's "correct" code. (TransAnim `float f5` placement: og position → 95.9%, our position → 97.5%.)

7. **Header-driven regressions are the long tail**: After fixing all .cpp-level differences, 62 regressions remain, all caused by header changes (readable names, added inline methods, struct additions). These can't be fixed without reverting the header improvements.

8. **Extra function bodies in a TU change inlining for all functions**: Removing 360 lines of unneeded function bodies from HamSkeletonConverter reduced the TU size but didn't fix Enter's regression — because the regression is from header-level changes, not from the extra bodies in the same file.

## Next Steps

### Accept the remaining 62 as the cost of readable headers
The header improvements (readable member names, type-safe getters, PaddedJointPos) are net positive for the project even though they cause 62 function regressions (34.5 KB affected). The regressions are in match% against og, not in functional correctness.

### Potential targeted header investigations
If specific regressions need to be recovered:
- **Rnd.h getters**: Could make them non-inline (out-of-line in Rnd.cpp) to avoid inlining into callers
- **Vec.h ZeroVec()**: Could make it non-inline or move to Vec.cpp
- **ModalKeyListener virtual dtor**: Could remove if not needed for PPC builds
- These would need careful testing — each change cascades to dozens of TUs

### CharLipSync String→FilePath (from Session 2a, still open)
- 14 template instantiations lost (2.9 KB)
- Could revert `mVisemes` to `vector<String>` if FilePath was wrong
- Need to verify which type the target binary actually uses (check DWARF/struct offsets)
