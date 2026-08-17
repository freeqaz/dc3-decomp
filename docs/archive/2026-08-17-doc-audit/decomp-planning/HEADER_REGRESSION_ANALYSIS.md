# Header-Driven Regression Analysis

> **Date**: 2026-03-10
> **Baseline**: `b14f7df76` (og-dc3-decomp) vs current `dev` branch
> **Status**: 72 regressions (36.1 KB affected) vs og baseline

## Executive Summary

523 headers differ between the og baseline and current codebase. These changes -- primarily variable renames, new function declarations, added inline bodies, native port guards, and iterator/operator modifications -- cause cascading codegen regressions across translation units. The MSVC PPC compiler changes inlining decisions based on visible function bodies in headers, meaning even "safe" changes like variable renames in inline function bodies or adding new inline methods can shift register allocation and instruction scheduling in unrelated functions within the same TU.

**Key finding**: The problem is NOT primarily from `#ifdef HX_NATIVE` guards (only 62 of 523 changed headers, <1% of diff lines). The dominant sources are:
1. New function declarations and bodies (308 headers with new declarations, 68 with new virtuals, 53 with new bodies)
2. Iterator/operator semantic changes in Object.h (included by PCH, affects every TU)
3. Variable renames in inline function bodies (92 headers with pure renames that shouldn't matter, but do when they occur inside inline function bodies)

## 1. Scope of the Problem

### Regression Statistics

| Metric | Value |
|--------|-------|
| Total functions regressed vs og | 72 |
| Functions regressed from 100% | 19 |
| Functions regressed by >10% | 14 |
| Total regressed bytes | 36.1 KB |
| Headers changed vs og | 523 |
| New virtual declarations added | 158 across all headers |

### Most Affected TUs

| TU | Regressed Functions | Changed Headers / Total Headers |
|----|--------------------|---------------------------------|
| system/ui/UIList | 8 | 43/73 (59%) |
| system/utl/MemMgr | 3 | 4/7 (57%) |
| system/char/CharEyes | 1 | 72/119 (61%) |
| lazer/meta_ham/MetaPanel | 2 | 264/544 (49%) |
| lazer/meta_ham/LoadingPanel | 2 | 84/248 (34%) |
| system/rndobj/Graph | 2 | 27/41 (66%) |
| system/rndobj/Part | 1 | 19/29 (66%) |
| system/rndobj/Text | 1 | 33/61 (54%) |

### Worst Regressions (from 100%)

| Function | TU | Was | Now | Delta |
|----------|----| ----|-----|-------|
| `vector<UIListWidget*>::...` | UIList | 100% | 0% | -100% |
| `MakeString<int,int,char*>` | CharClip | 100% | 0% | -100% |
| `RhythmBattle::OnBeat (local)` | RhythmBattle | 100% | 0% | -100% |
| `PanelDir::PanelNav (local)` | PanelDir | 100% | 0% | -100% |
| `CharClipGroup::HasClip` | CharClipGroup | 100% | 55.4% | -44.6% |
| `UIList::BoundingBoxTriangles` | UIList | 100% | 66.0% | -34.0% |
| `UIList::PostLoad` | UIList | 100% | 76.7% | -23.2% |
| `_MemAllocTemp` | MemMgr | 100% | 92.0% | -8.0% |
| `UIListDir::Save` | UIListDir | 100% | 92.2% | -7.8% |

## 2. Root Cause Analysis

### 2.1 PCH Contamination (Object.h)

The precompiled header (`decomp_pch.h`) includes `Object.h`, which has **229 lines of diff** vs og. This means every single TU in the project is affected by Object.h changes. Key changes:

- **`ObjPtrVec::iterator::operator+` changed from mutating to copy-returning**: This is the single most impactful change. It modifies the codegen of `end()` which uses `begin() + size()`, cascading into every `find() != end()` pattern project-wide.
- **New `friend` declarations** (MergeFilter, ObjRefRelinkRing): Forward declarations visible to the compiler change symbol table state.
- **New iterator typedefs** (iterator_category, value_type, etc.): Template metadata changes visible to the compiler.
- **`private:` changed to `protected:`** in ObjPtr: Access specifier changes affect vtable layout considerations.
- **`DeferOwner` struct added**: New nested type definition.
- **`ObjPtrVec::end()` body changed**: From simple `begin() + size()` to a pattern with explicit local variables to control register allocation.

### 2.2 Inline Body Additions

New inline function bodies make code visible to the compiler's inlining heuristic for the first time. MSVC PPC has an inlining budget -- adding bodies can push other functions over or under the threshold. Examples:

- `RndCam::ProjectZ(float) { return 0; }` -- added to Cam.h, included by many TUs
- `RndText::SetAltStyle()`, `GetFitType()`, `SetFitType()`, `Indentation()` -- new inline getters in Text.h
- `RndText::ComputeCharWidthsForText()` -- new declaration
- `vector::data()` -- added to STLport `_vector.h`

### 2.3 Enum Value Changes

Changes like `RndText::Style::kFitStretch = 6` being inserted (shifting `kFitScrollMarqueeWrapAlways` from 6 to 7) change constant values embedded in switch statements, affecting code layout.

### 2.4 .cpp File Changes Interacting with Headers

Many regressions come from .cpp changes that alter which functions have bodies visible in the TU. For example:

- `MemMgr.cpp`: Added `MemUseLowestMipException()` body, changed `MemFree()` logic, added gMemTracker tracking -- all change the inlining budget for other functions
- `Graph.cpp`: `RELEASE(sGraphs)` macro expanded to explicit code, `DrawFixedZ` body added -- these change the inlining decisions for `Terminate()` and `DrawAll()`
- `UIList.cpp`: Major additions including `Copy()`, `PreLoad()`, `PostLoad()` bodies

### 2.5 Variable Renames in Inline Bodies

Renaming variables inside inline function bodies (e.g., `f3` to `period` in Easing.h, `unk0` to `mIndex` in kdTree.h) should NOT affect codegen. However, MSVC PPC's debug symbol generation creates different string constants for debug info, which can affect `.data` section layout and, in rare cases, string literal deduplication patterns.

## 3. Categorization of 523 Changed Headers

| Category | Count | Description | Impact on Codegen |
|----------|-------|-------------|-------------------|
| New declarations/types | 308 | New function declarations, enums, structs, friend decls | Medium -- affects symbol table, may trigger different template instantiations |
| Pure renames | 92 | `unk*` to meaningful names, comment changes | **None** for most; slight for inline bodies (debug info) |
| New virtual declarations | 68 | New virtual function declarations/bodies | **High** -- changes vtable layout, affects inlining decisions |
| New function bodies | 53 | New inline function implementations | **High** -- directly changes inlining budget |
| Native-only | 2 | Pure `#ifdef HX_NATIVE` additions | **None** for PPC builds |

## 4. Potential Solutions

### Strategy A: Selective Header Reversion (HIGHEST ROI)

**Concept**: Revert the specific header changes that cause the most cascading regressions while keeping .cpp fixes.

**Top candidates for reversion**:

1. **Object.h `iterator::operator+` semantics** -- Revert to mutating version (og behavior). This single change cascades through every TU via the PCH. However, **REGRESSION_ROADMAP.md explicitly warns**: "Reverting Object.h iterator revert is Toxic -- causes 19 cascading regressions across TUs that use ObjPtrVec::end()." The current version was chosen to minimize total regressions, not eliminate them.

2. **Cam.h `ProjectZ` virtual** -- Remove the inline body, declare-only or move to .cpp. Any TU including Cam.h that doesn't call ProjectZ won't be affected.

3. **Text.h new inline getters** -- Move bodies to Text.cpp. Getters like `SetAltStyle()`, `GetFitType()` etc. are only called from Text.cpp anyway.

4. **`_vector.h` `data()` addition** -- Guard with `#ifndef HX_XBOX` if only needed for native.

**Estimated recovery**: 10-20 functions, ~5-10 KB
**Effort**: Low (1-2 hours per header, careful regression testing needed)
**Risk**: Each reversion must be tested for cascading effects in both directions

### Strategy B: .cpp File Regression Repair (HIGH ROI)

**Concept**: For TUs where the .cpp changed significantly (UIList, MemMgr, Graph), carefully compare the .cpp diff and identify which specific additions caused regressions.

**Candidates**:
- `UIList.cpp`: 1197 lines of diff. The added `Copy()`, `PreLoad()`, `PostLoad()` bodies change inlining decisions for `SetProvider()`, `OnSetSelected()`, `LimitCircularDisplay()` etc.
- `MemMgr.cpp`: 220 lines of diff. Added `MemUseLowestMipException()` and modified `MemFree()` change budget for `_MemAllocTemp()` and `MemOrPoolAllocSTL()`.
- `Graph.cpp`: 45 lines of diff. `RELEASE` macro expansion and `DrawFixedZ` body change `Terminate()` and `DrawAll()`.

**Approach**: Wrap new function bodies with `#ifdef HX_NATIVE` where they are only needed for native port. For bodies needed for PPC matching, investigate if the body itself can be made to match rather than removed.

**Estimated recovery**: 15-25 functions, ~10-15 KB
**Effort**: Medium (2-4 hours, requires per-function objdiff verification)
**Risk**: Low -- `#ifdef HX_NATIVE` is a proven pattern

### Strategy C: TU-Specific Include Control (MEDIUM ROI)

**Concept**: Use `#define` guards to control which inline bodies are visible per-TU.

```cpp
// In problematic header:
#ifndef SUPPRESS_INLINE_BODIES
inline float EaseStairstep(...) { /* full body */ }
#else
float EaseStairstep(float, float, float); // declaration only
#endif
```

Then in TUs that regressed:
```cpp
#define SUPPRESS_INLINE_BODIES
#include "math/Easing.h"
#undef SUPPRESS_INLINE_BODIES
```

**Estimated recovery**: 5-10 functions
**Effort**: High (many headers to modify, fragile build system interaction)
**Risk**: High -- PCH complicates this because PCH headers are compiled once for all TUs. Only works for non-PCH headers.

### Strategy D: Baseline Object Pinning (LOW ROI, HIGH EFFORT)

**Concept**: For TUs at 100% match, snapshot the .obj file and skip rebuilding unless the .cpp file itself changed (ignore header changes).

**Implementation**: Modify build.ninja to use a custom rule that checks .cpp mtime only (not header deps). This breaks the fundamental build system contract.

**Estimated recovery**: Preserves existing 100% matches
**Effort**: Very High (custom build system logic, maintenance burden)
**Risk**: Very High -- masks real regressions, makes debugging harder, diverges from actual compiler behavior

### Strategy E: Header Bifurcation (LOW ROI)

**Concept**: Maintain two versions of critical headers -- one for PPC matching, one for native.

```
src/system/obj/Object.h          # Native version (current)
src/system/obj/Object_ppc.h      # PPC-matching version (og)
```

With a selection header:
```cpp
#ifdef HX_NATIVE
#include "obj/Object.h"
#else
#include "obj/Object_ppc.h"
#endif
```

**Estimated recovery**: Could recover all 72 regressions
**Effort**: Extremely High (523 headers need bifurcation, ongoing maintenance nightmare)
**Risk**: Extremely High -- two codebases to maintain, easy to diverge

### Strategy F: Accept and Document (PRAGMATIC)

**Concept**: Accept the 72 regressions (36.1 KB) as the cost of having readable, maintainable code with a working native port. The net gain is +364.3 KB and +2237 functions -- a **10:1 improvement-to-regression ratio**.

**Estimated recovery**: 0 functions
**Effort**: None
**Risk**: None

## 5. Ranked Recommendations

| Rank | Strategy | Recovery | Effort | Risk | Recommendation |
|------|----------|----------|--------|------|----------------|
| 1 | **B: .cpp regression repair** | 15-25 fn | Medium | Low | **Do first**. Wrap new PPC-unnecessary function bodies in `#ifdef HX_NATIVE`. Highest ROI with lowest risk. |
| 2 | **A: Selective header reversion** | 10-20 fn | Low | Medium | **Do second**, but carefully. Focus on non-PCH headers (Cam.h, Text.h, Easing.h). Avoid touching Object.h (documented toxic). |
| 3 | **F: Accept and document** | 0 fn | None | None | **Default stance** for remaining regressions after A+B. The 10:1 improvement ratio justifies the cost. |
| 4 | **C: TU-specific include control** | 5-10 fn | High | High | **Only if specific high-value TUs remain**. PCH limitation makes this impractical for most headers. |
| 5 | **D: Baseline object pinning** | Preservation | Very High | Very High | **Do not implement**. Breaks build system integrity. |
| 6 | **E: Header bifurcation** | All 72 fn | Extreme | Extreme | **Do not implement**. Maintenance cost far exceeds benefit. |

## 6. Prevention Strategy

To prevent future header-driven regressions:

### 6.1 Guard Rule for New Bodies

Any new function body added to a header that is NOT needed for PPC matching MUST be guarded with `#ifdef HX_NATIVE`. This is already documented in CLAUDE.md ("New function bodies MUST be `#ifdef HX_NATIVE` guarded") but enforcement has been inconsistent.

### 6.2 Pre-commit Regression Check

Before committing header changes, run:
```bash
python3 scripts/analysis/compare_progress.py \
    ../og-dc3-decomp/build/373307D9/report.json \
    build/373307D9/report.json \
    --regressions --functions --limit 20
```

If regression count increases, the header change needs review.

### 6.3 Header Change Classification

Before modifying a header, classify the change:

| Change Type | Safe? | Action |
|-------------|-------|--------|
| Variable rename (member) | Yes | No guard needed -- affects only debug info |
| Comment change | Yes | No guard needed |
| New `#ifdef HX_NATIVE` block | Yes | Invisible to PPC compiler |
| New inline function body | **NO** | Must be `#ifdef HX_NATIVE` guarded unless needed for PPC |
| New virtual declaration | **NO** | Can change vtable layout -- verify with objdiff |
| Iterator/operator semantic change | **NO** | Extremely dangerous -- cascades through templates |
| Access specifier change | **NO** | Can affect compiler optimization decisions |
| Enum value insertion/reorder | **NO** | Changes constants in switch statements |

### 6.4 PCH Headers are Sacred

`Object.h` and `Debug.h` (the two PCH headers) should have an absolute minimum of changes. Every change to these files rebuilds every .obj and risks project-wide regressions. Any native-port changes to these headers MUST be wrapped in `#ifdef HX_NATIVE`.

## 7. Specific Action Items

### Immediate (Strategy B)

1. [ ] `MemMgr.cpp`: Guard `MemUseLowestMipException()` body with `#ifdef HX_NATIVE`
2. [ ] `MemMgr.cpp`: Guard gMemTracker tracking code with `#ifdef HX_NATIVE`
3. [ ] `Graph.cpp`: Guard `DrawFixedZ()` body expansion and `DrawString::DrawFixedZ` with `#ifdef HX_NATIVE`
4. [ ] `Graph.cpp`: Revert `RELEASE(sGraphs)` expansion back to macro
5. [ ] Verify: `UIList.cpp` changes -- which added bodies are PPC-necessary vs native-only?

### Short-term (Strategy A)

6. [ ] `Cam.h`: Move `ProjectZ` body to Cam.cpp, keep only declaration in header
7. [ ] `Text.h`: Move `ComputeCharWidthsForText` declaration and new inline getters behind `#ifdef HX_NATIVE` or to .cpp
8. [ ] `_vector.h`: Guard `data()` addition with `#ifdef HX_NATIVE`
9. [ ] `Easing.h`: Guard the added `inline` specifier on line 342-344 with `#ifdef HX_NATIVE`

### Audit (ongoing)

10. [ ] Review all 68 headers with new virtual declarations -- which are PPC-necessary?
11. [ ] Review all 53 headers with new function bodies -- which are PPC-necessary?
12. [ ] After each fix, re-run baseline comparison to verify recovery

## Appendix: Headers with Largest Diffs

| Lines Changed | Header | Category |
|---------------|--------|----------|
| 359 | `obj/ObjPtr_p.h` | Iterator semantics, native guards, insert logic |
| 239 | `math/kdTree.h` | Struct members, algorithm body, type changes |
| 229 | `obj/Object.h` | PCH -- iterator, operator+, friend, access |
| 161 | `rndobj/Text.h` | New enums, struct members, inline bodies |
| 135 | `rndobj/ShaderMgr.h` | Enum constant definitions |
| 123 | `synth/EQEffect.h` | Variable renames, body changes |
| 122 | `utl/trie.h` | Variable renames, logic changes |
| 122 | `game/PartyModeMgr.h` | Variable renames, new declarations |
| 119 | `os/Joypad.h` | Variable renames, new declarations |
| 115 | `utl/JobMgr.h` | Variable renames, new declarations |
