# NullifyObj Cascade Fix & Remaining Crashes

**Date**: 2026-03-20
**Status**: Partial fix committed. Remaining crash in TaskTimeline::Poll.
**Commits**: `2daf36863` (NullifyObj + Wgpu web guard), `c6dad61c8` (destructor guards)
**Builds on**: [2026-03-20-cascade-destructor-guard-audit.md](2026-03-20-cascade-destructor-guard-audit.md)

## Problem Solved: Stale ObjPtr After Cascade

During cascade `~ObjectDir` destruction, `~Object()` previously skipped
`ReplaceRefs(nullptr)` entirely because Replace callbacks write to freed
ObjPtrVec buffers. This left surviving ObjPtrs (in globals like TaskMgr,
sMetaMaterials) with dangling `mObject` pointers to destroyed-and-freed objects.

### Approach 1: NullifyObj (commit `2daf36863`)

Added `ObjRef::NullifyObj()` virtual (native-only) that nulls `mObject` and
self-loops ring `next`/`prev` without triggering Replace callbacks. In
`~Object()` during cascade, SnapshotRing + NullifyObj replaced the old
skip-ReplaceRefs approach. Opus verification passed all 15 checks.

**Issue discovered**: NullifyObj during `~Object()` can cause reentrant
nullification. When `mSubDirs.clear()` in `~ObjectDir()` destructs an
ObjDirPtr, the DirPtr-specific destructor checks `mObject` (non-null), then
the nested destruction calls NullifyObj which nulls `mObject`, then
`mObject->HasDirPtrs()` dereferences null → SIGSEGV at nil.

### Approach 2: Three-Phase DeleteObjects (current)

Another agent refined the approach: move ReplaceRefs to Phase 0 of
`DeleteObjects()`, BEFORE any destructors run. At this point all memory is
valid, so ring traversal is safe:

```cpp
void ObjectDir::DeleteObjects() {
    // Snapshot all objects
    std::vector<std::pair<void*, Hmx::Object*>> todo;
    for (ObjDirItr<Hmx::Object> it(this, false); it != nullptr; ++it)
        if (it != this) todo.push_back({dynamic_cast<void*>((Hmx::Object*)it), it});

    // Phase 0: nullify all ref rings while memory is valid
    for (auto &[block, obj] : todo)
        obj->ReplaceRefs(nullptr);

    // Phase 1: destroy all (memory stays valid for sibling destructors)
    for (auto &[block, obj] : todo)
        obj->~Object();

    // Phase 2: defer frees until outermost ~ObjectDir completes
    for (auto &[block, obj] : todo)
        DeferFree(block);
}
```

`~Object()` during cascade skips ReplaceRefs (already done in Phase 0).

### ObjDirPtr::HasDirPtrs Null Check

Fixed `Dir.h` line 101: `mObject->HasDirPtrs()` → `mObject && !mObject->HasDirPtrs()`.
During cascade, NullifyObj (or ReplaceRefs in Phase 0) can null `mObject` between
the initial `if (mObject)` guard and this dereference. The null re-check prevents
the SIGSEGV at nil.

## Remaining Crash: TaskTimeline::Poll

### Symptom

```
Signal: 11 (SIGSEGV) at address 0x557f1d3ba
Stack: App::RunWithoutDebugging → TaskMgr::Poll → TaskTimeline::Poll
```

Deterministic — every boot hits it during `title_screen` Enter /
`HamUI::ForceLetterboxOff()`.

### Root Cause

`TaskTimeline::Poll()` (Task.cpp:378) iterates `mTasks` (list of TaskInfo).
Each `TaskInfo::mTask` is an `ObjPtr<Task>`. When a panel unloads and
its ObjectDir is destroyed, tasks in that dir are destroyed. Despite Phase 0
calling `ReplaceRefs(nullptr)` on each task before destruction, the `ObjPtr<Task>`
in the global TaskMgr's TaskTimeline still holds a stale pointer and
dereferences it.

### Why Phase 0 ReplaceRefs Doesn't Fix It

Phase 0 calls `obj->ReplaceRefs(nullptr)` for each object in the dir.
`ReplaceRefs` walks the object's ObjRef ring and calls `Replace(nullptr)` on
each entry. For the `ObjPtr<Task>` in TaskTimeline (which IS in the ring),
`Replace(nullptr)` should call `SetObj(nullptr)` → `SetObjConcrete(nullptr)`.

During cascade (`InDeleteObjects()` == true), `SetObjConcrete` has a guard:

```cpp
if (mObject) {
    if (ObjectDir::InDeleteObjects())
        goto skip_release;    // ← skips to: mObject = obj (null)
    ...
    mObject->Release(this);
}
mObject = obj;
...
skip_release:
mObject = obj;              // ← mObject = nullptr ✓
```

This SHOULD work — `mObject` is set to null via the skip_release path. The
question is whether something else is interfering. Possible issues:

1. **ReplaceList force-unlink guard**: `ReplaceList` (Object.cpp:56-74) has a
   guard that force-unlinks entries where `Replace` didn't advance the cursor.
   During cascade, `SetObjConcrete`'s skip_release path doesn't call `Release`
   (which normally unlinks from the ring), so the entry stays in the ring.
   `ReplaceList` detects this (cur == next after Replace) and force-unlinks.
   But the ObjPtr's `Replace` calls `SetObj` which calls `SetObjConcrete` which
   during cascade goes to `skip_release` which just sets `mObject = null`. The
   ObjPtr is NOT unlinked from the ring. ReplaceList's force-unlink then runs,
   but the entry's `mObject` was already nulled. This should be fine.

2. **The ObjPtr is not in the ring**: If the ObjPtr<Task> was never properly
   AddRef'd into the Task's ring, Phase 0's ReplaceRefs wouldn't find it. But
   ObjPtr's constructor calls `AddRef`, so it should be in the ring.

3. **Re-added after Phase 0**: If a task is added to the TaskTimeline during
   Phase 1 (destruction), it would have a non-null ObjPtr that wasn't covered
   by Phase 0. Unlikely but possible if destructors trigger script execution.

4. **TaskTimeline is not in the dir**: The global `TheTaskMgr` is registered
   in `ObjectDir::Main()`. Its TaskTimeline members are embedded (not separate
   Objects). The ObjPtrs in TaskInfo entries are heap-allocated list nodes.
   Phase 0 only processes objects in the dir being destroyed — the TaskMgr
   itself is in Main dir, not in the panel dir. But the TASKS are in the
   panel dir. Phase 0 calls ReplaceRefs on each task → walks the task's ring →
   finds the ObjPtr in TaskTimeline → nullifies it. This should work.

### Investigation Findings (2026-03-20 continued session)

**GDB confirmed**: crash is a vtable dispatch on freed Task memory. Address in
PIE text range (`0x556xxx`). The `mov (%rdi),%rax` reads corrupted vtable,
`call *0xb8(%rax)` dispatches to invalid address.

**Diagnostic output**: `CASCADE RING NOT EMPTY: (Object) has refs after Phase 0`
fires — confirming Phase 0 leaves residual refs. This can happen when Replace
callbacks during Phase 0 re-add refs to the ring (e.g., ObjOwnerPtr::Replace
calling owner's Replace which creates new references).

**AsyncUnload path checked**: `~ObjectDir` logs show `async=0` for all panel
dirs during the crash sequence. The async path is NOT the issue — Phase 0 runs.

**Key hypothesis**: ScriptTasks created without a `name` parameter (Task.cpp:435)
are standalone heap objects with `mDir = nullptr`. They're never in any
`ObjDirItr`, so Phase 0 never processes them. They're also never freed by
`DeleteObjects` — they leak. BUT: if the task WAS named (added to dir), Phase 0
should nullify its TaskTimeline ObjPtr. The vtable corruption indicates the
task's memory WAS freed (via DeferFree → FlushDeferredFrees). This suggests the
task IS in a dir, Phase 0 ran, but Replace callbacks during Phase 0 RE-ADDED
the TaskTimeline ObjPtr to the task's ring after Phase 0 cleared it.

**TaskMgr::Start cascade guard added**: Tasks created DURING cascade are now
deleted immediately. But the crash persists — the stale task was created
BEFORE cascade.

### Fix Strategy

Two options, from pragmatic to thorough:

**Option A (quick): Add NullifyObj fallback in ~Object during cascade.**
If `mRefs.next != &mRefs` after Phase 0, use SnapshotRing + NullifyObj to
clean up stragglers. This was implemented and tested but the user may have
reverted it. The ObjDirPtr null re-check in Dir.h (`mObject && !mObject->HasDirPtrs()`)
prevents the reentrant nullification SIGSEGV that was the original blocker.

**Option B (thorough): Run Phase 0 twice.** After the first Phase 0 pass,
run a second pass to catch refs re-added by Replace callbacks. O(2n) but
handles all re-entrant cases. Alternatively, loop Phase 0 until all rings
are empty (with a safety limit).

**Option C (nuclear): Generational handles.** Eliminates the entire class of
bugs. See [2026-03-20-objref-ring-explainer.md](2026-03-20-objref-ring-explainer.md).

## Test Results (278 tests)

| Test | Status | Notes |
|------|--------|-------|
| 274 tests | PASS | All unit tests, asset loading, merge parity |
| HeadlessBootTest.SurvivesMainLoop | FAIL | TaskTimeline::Poll SIGSEGV (stale ObjPtr) |
| HeadlessBootTest.BootReachesChooseMode | FAIL | Same crash, different test wrapper |
| ObjectLifetimeTest.FlowAnimateDoubleDelete | FAIL | Pre-existing: documents known double-delete bug |
| MiloViewerScreenshot.PoseDump | FAIL | Pre-existing: Null GPU backend, no golden match |

## Infrastructure Added

### ObjRef::NullifyObj() virtual (Object.h, native-only)

```cpp
// Base: self-loops ring pointers
virtual void NullifyObj() { next = this; prev = this; }

// ObjRefConcrete: nulls mObject, then self-loops
void NullifyObj() override { mObject = nullptr; ObjRef::NullifyObj(); }
```

All ObjRef subclasses (ObjPtr, ObjPtrVec::Node, ObjPtrList::Node, ObjDirPtr)
inherit the ObjRefConcrete override. PPC build unaffected (behind `#ifdef
HX_NATIVE`). Opus review passed 15/15 checks.

Currently unused (`~Object` uses skip-ReplaceRefs, DeleteObjects uses Phase 0
ReplaceRefs). May be useful as a faster alternative if ReplaceRefs callbacks
cause issues in Phase 0.

### ObjDirPtr null re-check (Dir.h)

```cpp
// Before: mObject->HasDirPtrs()       ← null deref if NullifyObj/ReplaceRefs nulled it
// After:  mObject && !mObject->HasDirPtrs()
```
