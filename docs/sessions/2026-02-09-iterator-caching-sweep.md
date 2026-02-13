# Iterator Caching Pattern: Docs, Candidate Sweep, and Batch Triage Planning

**Date**: 2026-02-09

## Overview

Session set out to document the Iterator Dereference Caching pattern discovered in HarvestCollidables, sweep candidate functions for the same fix, and then pivot to planning a large-scale batch triage of unclassified functions. The caching sweep found zero improvements across 3 tested candidates (all regressed), narrowing the pattern's success rate to ~20%. The session then explored automated pattern scanning approaches and concluded with a plan for triaging 399 unclassified + 102 LIKELY_FIXABLE functions.

---

## Part 1: Pattern Documentation

Three new patterns were added to `docs/decomp/patterns/`:

### Iterator Dereference Caching (`fixable-declarations.md`)

Caching `ObjDirItr` dereferences into a local pointer (`T *obj = it;`) before using the object in dynamic_casts, method calls, and function arguments.

- **Impact:** +5-10%
- **Success Rate:** LOW (~20%) -- downgraded after sweep
- **Symptom:** Multiple `&*it` or `it->` in an ObjDirItr loop; objdiff shows repeated `lwz` from iterator stack slot
- **Why:** MSVC does not CSE through `operator*()` indirection; each `&*it` reloads `mObj` from stack
- **Fix:** `T *obj = it;` at loop top, replace all `&*it` and `it->` with `obj` / `obj->`
- **Origin:** PhysicsManager::HarvestCollidables 86.6% -> 97.4%

### Boolean Init from Existing Register (`fixable-declarations.md`)

Initializing a bool from a value already in a register to avoid an extra `li r3, 0x0`.

- **Impact:** +0.5-1%
- **Fix:** `bool u2 = i5;` when inside `if (i5 == 1)`, reusing the register
- **Origin:** HarvestCollidables +0.6%

### Stack Spill Scheduling (`unfixable-compiler.md`)

1-3 missing `stw` instructions where the target binary spills a local to the stack frame but our compiled code keeps it in a register. Unfixable, typically ~1-2% gap. Accept and mark AT_LIMIT.

---

## Part 2: Candidate Sweep Results

Tested the iterator caching pattern on all candidates identified in the earlier hard-decomp-patterns session:

| Function | Before | After | Delta | Action |
|----------|--------|-------|-------|--------|
| CameraManager::SyncObjects | 100% | -- | -- | Already complete, skipped |
| HamUI::Init | 100% | -- | -- | Already complete, skipped |
| SyncSubDir (obj/Dir.cpp) | N/A | -- | -- | Not a tracked symbol |
| AmbientOcclusion (GatherObjectsFromDir) | 77.0% | -- | -- | Template, AT_LIMIT, skipped |
| **CharClipSet::SetFrame** | **99.0%** | **96.8%** | **-2.2%** | Reverted |
| **Character::CalcBoundingSphere** | **98.4%** | **96.4%** | **-2.0%** | Reverted |
| **HamDirector::PoseIconMan** | **96.6%** | **94.0%** | **-2.6%** | Reverted |

All three attempted fixes made things worse. Zero net improvements.

### Why the Pattern Is Narrow

The iterator caching pattern only helped in HarvestCollidables because the cached pointer was used in **diverse ways** within the same loop body:
- `dynamic_cast<>(&*it)` -- cast to different types
- `it->Property(...)` -- virtual method call on the base type
- `AddCollidable(it, ...)` -- passed as argument to another function

The three functions that regressed only used `&*it` for `dynamic_cast` arguments (plus one `it->Poll()` call). In these simpler loops, the extra local variable changes register allocation for the worse -- the compiler was already handling the `&*it` pattern efficiently.

**Conclusion:** The pattern's success rate is ~20%, not the HIGH originally estimated. It should be tried as a heuristic when stuck on ObjDirItr-heavy functions with diverse usage, not applied as a blanket fix.

---

## Part 3: Automated Pattern Scanning Discussion

Explored approaches for detecting decomp patterns at scale:

### Source-side (grep/AST)
- Simple grep for `&*it` has poor signal-to-noise
- Would need AST-level analysis to classify usage diversity per loop body
- Not worth it for a ~20% success rate pattern

### Binary-side (Ghidra scripting)
- Find functions with `ObjDirItr` constructor xrefs, identify loop back-edges, count repeated `lwz` from iterator stack offset
- Most robust approach -- Ghidra has CFG, dataflow, and decompiler available
- Heavyweight setup for a narrow pattern

### objdiff extension
- Detect "repeated stack loads within a loop region" as a new pattern tag
- Integrated into existing workflow but requires objdiff modifications

### Highest-ROI patterns for automation
The patterns worth automating are the high-success-rate ones:
- **Explicit destructors**: grep for classes without `~ClassName` (100% success rate)
- **MILO_NOTIFY vs MILO_NOTIFY_ONCE**: static guard pattern in binary (already swept in phases 1-3)
- **alloca vs _alloca**: look for `_RtlCheckStack12` xrefs (mostly done)

---

## Part 4: Batch Triage Opportunity

Database analysis revealed the real scale of untapped opportunity:

| Category | Count | Action |
|----------|-------|--------|
| Unclassified 95-100% | 120 | Triage with objdiff -- classify |
| Unclassified 90-95% | 47 | Triage with objdiff -- classify |
| Unclassified 80-90% | 232 | Triage with objdiff -- classify |
| LIKELY_FIXABLE (all ranges) | 102 | Fix -- already identified as fixable |
| Fixable destructors | 8 | Fix -- trivial, 100% success rate |

**Total untriaged:** 399 functions in the 80-100% range that have never been classified.

A plan was drafted ("High-ROI Pattern Scan & Batch Fix Campaign") to:
1. Batch-triage the 95-100% unclassified pool first (highest ROI, closest to matching)
2. Fix the 8 destructor candidates (~2 min each, +37-70%)
3. Batch-fix the 102 LIKELY_FIXABLE functions using parallel agents on non-overlapping .cpp files
4. Triage the 80-95% pool for additional candidates

Expected yield: ~30-40 fixes from the LIKELY_FIXABLE pool alone.

---

## Files Modified

Documentation only (all code changes were reverted):
- `docs/decomp/patterns/fixable-declarations.md` -- 2 new pattern sections
- `docs/decomp/patterns/unfixable-compiler.md` -- 1 new entry
- `docs/decomp/patterns/INDEX.md` -- 3 new patterns in tables
