# 2026-02-08: ObjPtrVec Template Regression Fix

## Problem

96 function regressions (-23.2 KB) detected between HEAD~3 and working tree. Nearly all were `ObjPtrVec` and `stlpmtx_std::vector<ObjPtrVec::Node>` template instantiations that dropped from 100%/83.8% to 0% across ~20 translation units.

## Root Cause

Uncommitted experimental changes in template headers broke code generation for all ObjPtrVec instantiations project-wide.

## Reverted Files

| File | Issue |
|------|-------|
| `src/system/obj/Object.h` | Removed `friend class ObjPtrVec`, STL iterator traits, and `operator==` from iterator classes — broke all ObjPtrVec instantiations |
| `src/system/obj/ObjPtr_p.h` | Broken `operator=` (code outside braces), broken `insert()` rewrite, removed `find()` |
| `src/system/flow/FlowPtr.h` | Removed `operator<<`/`operator>>` bodies → linker errors |
| `src/system/flow/FlowAnimate.cpp` | Replaced complete `Load()` with incomplete `BEGIN_LOADS` macro |
| `src/system/math/Easing.h` | `EaseElasticIn()` stub returning `t` instead of real calculation |
| `src/system/math/Easing.cpp` | Deleted `EaseElasticIn()` implementation and `cmath` include |

All reverted to HEAD. Full diff backed up to `archive/uncommitted-changes-20260208.patch`.

## Kept Changes (not reverted)

These experimental changes were kept as potentially intentional improvements:

- `src/system/rndobj/Draw.h` / `Draw.cpp` — Inlined `Draw()` and `CollideList()`
- `src/system/flow/FlowSetProperty.cpp` — `COPY_MEMBER()` → direct assignment
- `src/system/os/Debug.h` — `SetTry()` before `try {`
- `src/system/os/ContentMgr.h` — Added `virtual` to `PreInit()`
- `src/system/rndobj/MeshDeform.h` — Removed `BoneDesc::operator=` decl
- `src/system/rndobj/Shockwave.cpp` — `0` → `0.0f` in Vector4 ctors
- `src/system/rndobj/PropAnim.cpp` — Variable scope change
- All other uncommitted `.cpp`/`.h` changes across lazer/, system/ directories

## Results

- **Before**: 96 regressions, 23.2 KB affected
- **After**: 6 regressions, 1.1 KB affected (from kept experimental changes)
- Remaining regressions are in `UILabel` (5 functions) and `PlaylistSortMgr` (1 function), likely caused by the Draw.h inlining changes

## Follow-up Items

1. **ObjPtr_p.h experiments**: The `operator=`, `insert()`, and `find()` changes were attempting to improve matches but were incorrectly implemented. Revisit with correct implementations.
2. **UILabel regressions**: Investigate whether Draw.h inlining is the cause; if so, evaluate whether the inlining improves other functions enough to justify
3. **Easing functions**: `EaseElasticIn()` was stubbed — investigate if there's a matching issue with the real implementation
4. **FlowAnimate::Load()**: The `BEGIN_LOADS` macro approach may be correct but was incomplete; revisit with proper implementation
