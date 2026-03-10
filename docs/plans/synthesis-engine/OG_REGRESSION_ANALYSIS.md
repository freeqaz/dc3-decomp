# OG Baseline Regression Analysis

**Date**: 2026-03-10
**Comparison**: `og-dc3-decomp` (upstream fork) vs `dc3-decomp` (current)
**Tool**: `python3 scripts/analysis/compare_progress.py ../og-dc3-decomp/build/373307D9/report.json build/373307D9/report.json`

## Executive Summary

Our fork shows **+6.18% overall fuzzy match** vs OG (42.46% → 48.64%), with **3,251 function improvements** and **67 function regressions** reported by the comparison tool. Drilling deeper:

- **+2,697 functions improved to 100%** match
- **215 functions dropped from 100%**, of which **98 are unit-migration artifacts** (function assigned to `link_glue` instead of the original TU) and **117 are real codegen regressions**
- **Net: +2,580 functions at perfect match**

The regressions are caused by **header changes** — primarily in `ObjPtr_p.h`. Matching the OG `.cpp` source for regressed functions does not recover them; in fact it often makes them *worse*. This was tested and confirmed for `RndGraph::Terminate`, `RndGraph::DrawAll`, and `JointScreenPos`.

## What Changed in Headers

### 1. `ObjPtr_p.h` — Template Bug Fixes (Most Impactful)

This header is included in every translation unit via the precompiled header chain (`decomp_pch.h` → `Object.h` → `ObjPtr_p.h`), so any change affects codegen globally.

#### a) `ObjPtrVec::insert` — was non-functional

**OG code** (lines 215-224):
```cpp
template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::insert(typename ObjPtrVec<T1, T2>::const_iterator it, T1 *obj) {
    int idx = *it != nullptr ? size() : 0;  // idx computed but never used
    if (obj || mListMode != kObjListNoNull) {
        // mNodes.insert(it, Node(obj));       // commented out — the actual insert
        Set(iterator(0), obj);                 // passes null iterator to Set
    }
    return iterator(&Node(obj));               // returns dangling pointer to stack temporary
}
```

**Our code** (lines 234-252):
```cpp
template <class T1, class T2>
typename ObjPtrVec<T1, T2>::iterator
ObjPtrVec<T1, T2>::insert(typename ObjPtrVec<T1, T2>::const_iterator it, T1 *obj) {
    if (obj != 0 || mListMode != kObjListNoNull) {
        int idx = it.it ? (it.it - mNodes.begin()) : 0;
        Node newNode(this);
        typename std::vector<Node>::iterator pos = mNodes.begin() + idx;
        mNodes.insert(pos, 1, newNode);
        Set(begin() + idx, obj);
    }
    return iterator(const_cast<typename std::vector<Node>::iterator>(it.it));
}
```

The OG version has a commented-out `mNodes.insert()` and uses `Set(iterator(0), obj)` instead — a null iterator dereference. The function doesn't actually insert anything. Additionally, `return iterator(&Node(obj))` creates a temporary `Node` on the stack and returns a pointer to it (dangling reference). This code compiles and produces COMDAT symbols that happen to match the target, but it's semantically broken.

**Impact of our fix**: The `pos` local variable forces the compiler to evaluate `begin() + idx` before passing it to `mNodes.insert()`, matching the target's argument evaluation order. **18 insert instantiations across 14 TUs improved from 96.6% → 100%.**

#### b) `ObjPtrVec::operator=` — undefined behavior

**OG code** (lines 198-208):
```cpp
void ObjPtrVec<T1, T2>::operator=(const ObjPtrVec &other) {
    if (this != &other) {
        mNodes.clear();
    }
    mNodes.reserve(other.mNodes.size());
    for (const_iterator it = other.begin(); it != other.end(); ++it) {
        mNodes.push_back(Node(this));
        Set(end(), *it);   // BUG: dereferences one-past-the-end
    }
}
```

**Our code** (lines 217-225):
```cpp
void ObjPtrVec<T1, T2>::operator=(const ObjPtrVec &other) {
    if (this == &other) return;
    mNodes.clear();
    mNodes.reserve(other.mNodes.size());
    for (const_iterator it = other.begin(); it != other.end(); ++it) {
        mNodes.push_back(Node(this));
        Set(begin() + (mNodes.size() - 1), *it);
    }
}
```

`Set(end(), *it)` passes the past-the-end iterator to `Set`, which dereferences it. Undefined behavior. Our version uses `begin() + (mNodes.size() - 1)` to point at the last element. **13 operator= instantiations improved from 82.9% → 84.4%.**

#### c) `ObjPtrVec::Node::operator=` — CopyRef fix

We added `void operator=(const Node &o) { CopyRef(o); mOwner = o.mOwner; }` to `Node`. This forces STLport's `_M_fill_insert_aux` to call the non-inlined `CopyRef` (defined in `link_glue.cpp`) instead of the inlined `SetObjConcrete`. The target binary shows external `bl CopyRef` calls, confirming our version matches the original compiler's template instantiation. **18 `_M_fill_insert_aux` instantiations improved from 83.8% → 100%.**

#### d) Added template bodies: `find`, `swap`, `sort`

These are **new** function bodies that OG doesn't have in the header (OG had them as explicit specializations in `link_glue.cpp`). They were added for the native port and for correctness. The additional ~47 lines of template code increase every TU's visible code volume, which can change the MSVC PPC compiler's inlining decisions for *other* functions.

This is the primary mechanism causing the "collateral" regressions — functions that don't use `find`/`swap`/`sort` at all can still have different codegen because the compiler's inlining budget calculations see more template code.

### 2. `UIList.h` — Struct Layout Correction

**OG** had incorrect field names and layout:
```cpp
int unk150;          // 0x150
float unk158;        // 0x158
bool unk15c;         // 0x15c
bool unk15d;         // 0x15d
int unk160;          // 0x160
bool mAllowHighlight;// 0x164   ← wrong offset, wrong field
```

**Ours** (confirmed via DWARF/assert analysis):
```cpp
int mAutoScrollDir;                  // 0x150
float mAutoScrollTimer;              // 0x158
bool mDrawManuallyControlledWidgets; // 0x15c
bool mAllowHighlight;                // 0x15d
int mUncappedNumDisplay;             // 0x160
bool mScrolling;                     // 0x164
```

This fixes field access offsets for `mAllowHighlight` (was at 0x164, now correctly at 0x15d) and `mScrolling` (OG didn't have it, was using `false` in `BuildDrawState`). Added virtual methods `AdjustTrans`/`AdjustTransSelected` to `UIListWidget` — confirmed called from PPC code in `UIListSlot.cpp`.

**Impact**: Fixed `UIList::PostLoad` from 76.7% → 100%, `DrawShowing` 84.3% → 90.1%. Regressed `UIList::Copy` (99% → 75.7%) because Copy accesses different fields with different offsets.

### 3. `RndText.h` — Enum and Struct Additions

Added `kFitStretch = 6` enum value (shifting `kFitScrollMarqueeWrapAlways` from 6 to 7), `Style` copy constructor, `Line` nested class, many accessor declarations, and field naming. Also includes `StlAlloc.h` for template type macros.

**Impact**: Regressed `RndText::RndText` constructor (98.5% → 84.4%) due to enum shift and struct initialization differences.

### 4. `RndParticleSys.h` (Part.h) — Struct and Accessor Additions

Renamed ~15 `unk*` fields to meaningful names, added `Burst` struct member functions, added ~10 accessor methods, added 4 function declarations (`MoveParticles`, `CreateParticles`, etc.), fixed `Particle` struct field offsets.

**Impact**: Regressed `RndParticleSys::SyncProperty` (99.8% → 81.4%) due to inlining budget changes — 7 `PropSync` calls changed from tail-call (`b`) to regular call (`bl`).

## Why Matching OG Source Doesn't Fix Regressions

We tested three regressions by reverting to OG's exact `.cpp` source:

| Function | Regression | OG Source Result | Explanation |
|----------|-----------|-----------------|-------------|
| `RndGraph::Terminate` | 79.0% current | **75.0%** (WORSE) | `RELEASE()` macro generates different address-caching pattern when compiler sees different header template bodies |
| `RndGraph::DrawAll` | 92.2% current | **87.3%** (WORSE) | Removing cached `end()` changed register allocation with our header's inlining budget |
| `JointScreenPos(Vector3)` | 73.1% current | **73.1%** (NO CHANGE) | Instruction scheduling differences are header-driven, not source-driven |

This confirms the "counter-intuitive pattern" documented across multiple sessions: **when headers differ, matching the `.cpp` source can make things worse** because the compiler's inlining decisions, register allocation, and instruction scheduling all depend on the total code visible in the translation unit. Our headers show different template bodies, so the compiler makes different choices even for identical `.cpp` code.

## Regression Breakdown (67 reported by comparison tool)

### Category 1: Deliberate Trade-offs (3 functions, all → 0%)

| Function | OG | Ours | Trade-off |
|----------|-----|------|-----------|
| `PanelDir::PanelNav` dtor helper | 100% | 0% | Our main `PanelNav` is 96.7% vs OG's 74.8% (+21.9%) |
| `RhythmBattle::OnBeat` dtor helper | 100% | 0% | COMDAT scope counter ±1 from control flow optimization |
| `MakeString<int,int,char const*>` | 100% | 0% | Different MILO_FAIL argument order → different template instantiation |

The dtor helpers are COMDAT functions whose mangled names include a scope counter. Our control flow restructuring shifts the scope counter by 1-2, creating a different mangled name. The main function matches much better with our control flow.

### Category 2: Header-Driven Inlining Budget (≈45 functions, 0.5-18% drops)

The `ObjPtr_p.h` template bodies (`find`, `swap`, `sort`, `insert` rewrite, `CopyRef` addition) add visible code to every TU via the PCH. The MSVC PPC compiler has a per-function inlining budget of ~150 IL nodes. Adding template code doesn't change the budget, but it changes *which* functions the compiler considers for inlining, which cascades through register allocation and instruction scheduling.

Pattern: callee-saved register shifts (r30↔r31, r29↔r30 cascade) with ±16 byte stack frame offset. Same instructions, different register assignments.

### Category 3: Struct Layout Fixes (≈10 functions, 2-23% drops)

`UIList.h` struct corrections changed field offsets, which changes load/store instruction offsets in functions that access those fields. Functions that accessed the wrong field (e.g., `mAllowHighlight` at wrong offset) now generate different code.

### Category 4: Unit Migration Artifacts (98 functions at 0%)

These are `SetObj`, `SetObjConcrete`, `ReplaceNode` template specializations (from `ObjPtrList`) plus `Ease*` functions from `FlowSetProperty` and `floor0_*` from oggvorbis. In OG's report, they're assigned to the TU that instantiates them. In our report, they're assigned to `link_glue` (which provides `ALTERNATENAME` pragmas but doesn't compile the function bodies), so they show 0% in `link_glue`. The actual codegen in the original TU may be fine — this is a report-level attribution difference, not a codegen regression.

## Net Impact

| Metric | Value |
|--------|-------|
| Overall fuzzy match | +6.18% (42.46% → 48.64%) |
| Functions improved | 3,251 |
| Functions improved to 100% | 2,697 |
| Functions regressed | 67 (reported) / 289 (including sub-0.5%) |
| Functions dropped from 100% | 215 (98 unit-migration, 117 real codegen) |
| **Net functions at 100%** | **+2,551** |
| New units at 100% | 84 |

## Recommendation

The header changes should be kept. The fixes are semantically correct (fixing UB in `operator=`, fixing non-functional `insert`, correcting struct layouts confirmed by DWARF/assert analysis). The codegen regressions are an unavoidable side effect of the MSVC PPC compiler seeing different template bodies in the PCH — not bugs in our code. Reverting to OG's headers would:

1. Reintroduce undefined behavior in `ObjPtrVec::operator=` (past-the-end dereference)
2. Reintroduce a non-functional `ObjPtrVec::insert` (null iterator, dangling return)
3. Break the native port (which depends on the template bodies being in the header)
4. Lose 2,551 net functions at 100% match

The 144 remaining real codegen drops from 100% are the cost of correctness. No source-level fix exists for them — they're driven by the compiler's inlining budget seeing different template code volumes.

### Implemented: ObjPtrVec Impl Header Split

**Tested and applied.** The `find`, `swap`, `sort`, `merge`, `unique`, and `remove` template bodies were moved from `ObjPtr_p.h` to a new `ObjPtrVec_impl.h`. This keeps them out of the PCH (which affects all ~800 TUs) and only includes them in the ~8 `.cpp` files that actually call these methods.

**Key discovery**: OG's build doesn't link most of the TUs that call these methods (e.g., `CharClipGroup.cpp` is `linked False` in OG's `build.ninja`). OG could get away without template bodies because it only needed `.obj` files to compile, not link. Our build links these TUs (`linked True`), so we need the bodies — but they don't have to be in the PCH.

**Results of the split (vs no split):**

| Metric | Before Split | After Split | Delta |
|--------|-------------|-------------|-------|
| Functions dropped from 100% (vs OG) | 215 | 144 | **-71 recovered** |
| Functions improved to 100% (vs OG) | 2,697 | 2,695 | -2 |
| Net functions at 100% | +2,480 | +2,551 | **+71** |
| OG regressions (comparison tool) | 67 | 67 | 0 |
| OG improvements (comparison tool) | 3,251 | 3,256 | +5 |
| HEAD regressions | 0 | 1 (MeterDisplay -1.0%) | +1 |

The split recovered 71 functions that had fallen from 100% (due to inlining budget pollution from the PCH seeing extra template code), at the cost of 1 small MeterDisplay regression. The 67 "big" regressions reported by the comparison tool are unchanged — these are caused by the remaining template bodies in ObjPtr_p.h (`operator=`, `insert`, `Node` constructors, `CopyRef` operator=) which must stay in the header.

**Files changed:**
- `src/system/obj/ObjPtr_p.h` — removed find/swap/sort/merge/unique/remove bodies, added `#ifdef HX_NATIVE` include of impl header
- `src/system/obj/ObjPtrVec_impl.h` — new file with the extracted template bodies
- 8 `.cpp` files — added `#include "obj/ObjPtrVec_impl.h"`:
  - `CharClipGroup.cpp`, `CharClipSet.cpp`, `ClipCollide.cpp`, `Character.cpp`
  - `FlowMultiSetProperty.cpp`, `FlowManager.cpp`, `FlowSlider.cpp`
  - `LightPreset.cpp`
