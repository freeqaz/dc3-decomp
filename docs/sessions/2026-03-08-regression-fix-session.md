# Session: Regression Recovery (2026-03-08)

## Status: Complete

| Phase | Status | Description |
|-------|--------|-------------|
| Phase 1: Header fixes | Done | Removed bodyless copy ctors, restored virtuals |
| Phase 2: Structural fixes | Done | SpotlightDrawer, ObjPtrVec iterator revert |
| Phase 3: HEAD commit triage | Done | Selective revert of HEAD commit changes |
| Phase 4: Optimization | Done | Per-file cost/benefit analysis for og baseline |

## Summary

Surgically fixed HEAD~1 regressions from two sources:
1. **Header fixes** (Phase 1-2): Removed bodyless copy ctors, restored virtuals, fixed Spotlight delegation
2. **HEAD commit triage** (Phase 3-4): Selectively reverted HEAD commit (4f09bcfcf) changes that regressed HEAD~1, keeping only those that improved og baseline

**Results**: HEAD~1 regressions: 56 → 2 (200 B). OG baseline regressions: held at 90 (vs 62 for pure HEAD~1). Overall: +0.05% (48.06% → 48.11%).

The ObjPtrVec iterator revert (Object.h) was dropped — it fixed CharClipGroup HasClip but caused 19 cascading regressions across other TUs.

## Task Table

| # | Task | Status | Impact | Method |
|---|------|--------|--------|--------|
| 1 | Fix Morph regressions (4 funcs) | Done | +368 B | Remove bodyless `Pose(const Pose &)` from Morph.h |
| 2 | Fix Sfx regressions (4 funcs) | Done | +344 B | Remove bodyless `SfxMap(const SfxMap &)` from Sfx.h |
| 3 | Fix HamCamTransform (1 func) | Done | +100 B | Remove bodyless `TransformArea(const TransformArea &)` from HamCamTransform.h |
| 4 | Fix PracticeChoosePanel (1 func) | Done | +80 B | Remove bodyless `StepMoves(const StepMoves &)` from PracticeChoosePanel.h |
| 5 | Fix DancerSequence (1 func) | Done | +96 B | Remove bodyless `DancerSkeleton(const DancerSkeleton &)` from DancerSkeleton.h |
| 6 | Fix Spotlight dtor (576 B) | Done | +576 B | Call `SpotlightDrawer::RemoveFromLists` instead of empty `Spotlight::RemoveFromLists` stub |
| 7 | Fix HamUser thunk (28 B) | Done | +28 B | Restore non-const `GetRemoteUser()` to User/LocalUser/RemoteUser hierarchy |
| 8 | Fix CharClipGroup HasClip (128 B) | Reverted | 0 B | Object.h iterator revert caused 19 cascading regressions; dropped |
| 9 | Fix DebugGraph copy ctor | Done | 0 B | Remove bodyless declaration (no measurable impact) |
| 10 | Fix CacheIDXbox copy ctor | Done | 0 B | Remove bodyless declaration (no measurable impact) |
| 11 | Investigate MatAnim Load (916 B) | Done | 0 B | 95% match — volatile r10/r11 swap + cascading from added bodies. Accepted. |
| 12 | Investigate AmbientOcclusion sorts | Done | 0 B | 6 sort templates regressed from added bodies. Net positive tradeoff. Accepted. |
| 13 | Investigate top og regressions | Done | 0 B | All pre-existing header-driven cascading issues. Not from our changes. |
| 14 | Analyze last 10 commits | Done | — | Most HEAD commit changes regress HEAD~1; selective keep only |
| 15 | HEAD commit triage | Done | -54 regs | Per-file revert/keep analysis of HEAD commit |
| 16 | Object.h iterator revert analysis | Done | -19 regs | Iterator revert caused 19 cascading regressions; dropped |

## Remaining HEAD~1 Regressions (2 functions, 200 B)

Accepted tradeoffs — keeping these HEAD changes recovers 8 og baseline functions:

| Function | Unit | Current | Size | Root Cause |
|----------|------|---------|------|------------|
| `AnimPtr::~AnimPtr` | CharLipSync | 0% | 64 B | Removed to recover 5 HamDirector og functions |
| `Synth::SendToPlayHandlers` | Synth | 96.3% | 136 B | Kept HEAD Synth.cpp to recover 3 og Synth functions |

## Root Cause Pattern: Bodyless Copy Constructor Declarations

**8 of 15 fixes** shared the same root cause: a copy constructor was **declared** in a header but never **defined** — either inline or in the .cpp file.

```cpp
// BAD: Suppresses implicit copy ctor, breaks template instantiations
class Foo {
    Foo(const Foo &);  // declared but no body anywhere
};

// GOOD: Let compiler auto-generate
class Foo {
    // no copy ctor declaration — compiler generates one implicitly
};

// ALSO GOOD: Explicit body
class Foo {
    Foo(const Foo &other) : member(other.member) {}
};
```

**Why it breaks things**: When a copy constructor is declared, the compiler suppresses implicit generation. If no body is provided, any code that needs to copy the object (e.g., `std::vector::resize`, `std::uninitialized_copy`, `push_back`) fails to link — or worse, the template instantiation is silently dropped, causing the function to disappear from the object file entirely (100% → 0% match).

**Affected headers fixed this session**:
- `Morph.h` (Pose copy ctor)
- `Sfx.h` (SfxMap copy ctor)
- `HamCamTransform.h` (TransformArea copy ctor)
- `PracticeChoosePanel.h` (StepMoves copy ctor)
- `DancerSkeleton.h` (DancerSkeleton copy ctor)
- `DebugGraph.h` (DebugGraph copy ctor)
- `Cache_Xbox.h` (CacheIDXbox copy ctor)

## Other Fix Categories

### Virtual Function Restoration (HamUser)
Removing a virtual function from a base class drops its vtable entry and any adjustor thunks. The `GetRemoteUser()` non-const virtual was removed from the User hierarchy, dropping the vdelta thunk in HamUser.

### Incorrect Function Delegation (Spotlight)
The Spotlight dtor called `Spotlight::RemoveFromLists` (an empty stub that got inlined away) instead of `SpotlightDrawer::RemoveFromLists` (a real function). The target expected the latter.

### Iterator Operator Semantics (CharClipGroup)
`ObjPtrVec::iterator::operator+` was changed from mutating `*this` to returning a copy. This changes how `end()` (which calls `begin() + size()`) compiles, cascading into `find() != end()` comparisons. The original (buggy but matching) behavior was needed for PPC codegen. Fix: guard the correct version with `#ifdef HX_NATIVE`.

## Phase 3-4: HEAD Commit Triage

The HEAD commit (4f09bcfcf "more merge progress") introduced 56 regressions vs HEAD~1. We tested each changed file individually:

| File | HEAD~1 regs added | OG regs recovered | Decision |
|------|-------------------|-------------------|----------|
| UIListDir.cpp+Widget.h+State.cpp | +9 (3.4 KB) | -3 | Revert |
| UIList.cpp+.h | +5 (2.7 KB) | -8 | Revert (ratio too poor) |
| RhythmBattle.cpp | +5 (16.2 KB) | -1 | Revert |
| MemMgr.cpp | +5 (2.1 KB) | -7 | Revert |
| AmbientOcclusion.cpp | +3 (1.6 KB) | 0 | Revert |
| LockedContentPanel+AppLabel | +3 (1.4 KB) | 0 | Revert |
| CharClip.cpp | +2 (624 B) | -1 | Revert |
| MatAnim.cpp+.h | +2 (192 B) | +1 (worse) | Revert |
| **HamDirector.h+CharLipSync.cpp** | **+1 (64 B)** | **-5** | **Keep** |
| **UIScreen.cpp** | **0** | **-3** | **Keep** |
| **Synth.cpp** | **+1 (136 B)** | **-3** | **Keep** |

### Key Insight: Object.h Iterator Revert

The previous session reverted `ObjPtrVec::iterator::operator+` from copy-returning to mutating form. This fixed CharClipGroup::HasClip but caused **19 cascading regressions** across many TUs that use `ObjPtrVec::end()`. Dropped the change entirely.

## Paths Forward

### Remaining og baseline regressions (90 functions, 40.7 KB)
Most are pre-existing header-driven issues. Categories:
1. **Struct layout differences** (Vector3 vs float fields, different padding)
2. **Added function bodies** causing cascading inlining changes
3. **Template instantiation differences** from header type changes
4. **Volatile register swaps** (unfixable)

### Potential recovery strategies
1. **Audit all headers for bodyless copy ctors** — we found 7 this session, there may be more
2. **Guard header changes with HX_NATIVE** — changes needed for native port but harmful to PPC codegen
3. **Investigate per-TU regression causes** — for the top regressions, trace the transitive include chain to find which header change caused the cascade
4. **Avoid Object.h iterator changes** — proven to cascade across too many TUs
