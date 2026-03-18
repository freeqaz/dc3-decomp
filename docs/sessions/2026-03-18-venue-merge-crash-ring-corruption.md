# Session: Venue Merge Crash — ObjRef Ring Corruption

**Date**: 2026-03-18

## Problem

Native port crashes during `game_screen` entry when venue `.milo` files are merged. Three symptoms, one root cause:

| Symptom | Where | Mechanism |
|---------|-------|-----------|
| `SIGSEGV at 0x28` | `ObjRef::ReplaceList` | Null vtable on freed ring entry (Replace virtual = slot 5 = 0x28) |
| `SIGSEGV at 0xfffffff8` | `CharClip::operator delete` | Double-free from stale ring walk reaching freed CharClip |
| Infinite hang | `~ObjectDir → mSubDirs.clear()` | Destructor cascade loops on self-referencing ring nodes |

Blocked all gameplay — 9/20 telemetry tests failed.

## Root Cause: Double AddRef in ObjDirPtr Constructor (Decomp Error)

The `ObjDirPtr(C*)` constructor had a **duplicate `AddRef` call** — a decompilation error not present in the Xbox original:

```cpp
// BROKEN decomp — base class already called AddRef
ObjDirPtr<C>::ObjDirPtr(C *dir) : ObjRefConcrete<C>(dir), mLoader(nullptr) {
    if (dir) {
        dir->AddRef(this);       // ← SECOND AddRef on already-linked node
        DirPtrRefCounts()[(const void *)dir]++;
    }
}
```

The base class `ObjRefConcrete(dir)` already calls `mObject->AddRef(this)`, correctly inserting the node into the target's `mRefs` ring. The second `AddRef` re-inserts the same node, creating a **self-loop**:

### How AddRef corrupts when re-inserting an existing node

Starting state (correct ring after first AddRef): `Head <-> Ptr <-> Head`

```cpp
void AddRef(ObjRef *ref) {         // ref = &Head
    next = ref;                    // Ptr.next = Head  (unchanged)
    prev = ref->prev;             // Ptr.prev = Head.prev = Ptr  (SELF!)
    ref->prev = this;             // Head.prev = Ptr  (unchanged)
    prev->next = this;            // Ptr.next = Ptr   (SELF-LOOP!)
}
```

**Result**: `Head.next = Ptr` (Head still sees Ptr), but `Ptr.next = Ptr, Ptr.prev = Ptr` (isolated self-loop).

### Why Release becomes a no-op

When the `ObjDirPtr` is later destroyed (e.g. during `mSubDirs.clear()`):

```cpp
void Release(ObjRef *ref) {
    prev->next = next;     // Ptr.next = Ptr  (no-op)
    next->prev = prev;     // Ptr.prev = Ptr  (no-op)
}
```

The self-looping node cannot unlink itself. The sentinel (`Head`) permanently holds a **dangling pointer** to the freed `ObjDirPtr` memory.

### How this caused each symptom

1. **0x28 crash**: Ring walk hits dangling pointer → freed memory zeroed by glibc → null vtable → dereference at vtable slot 5 (offset 0x28)
2. **0xfffffff8 crash**: Ring walk hits dangling pointer → freed memory reused → garbage vtable → virtual dispatch jumps to heap → eventually calls `CharClip::operator delete` on corrupt args
3. **Hang**: During `~ObjectDir → mSubDirs.clear()`, each ObjDirPtr destructor calls `operator=(nullptr)` → `HasDirPtrs()` checks `DirPtrRefCounts`. But the self-looping ObjDirPtr was never properly removed from the ring, so the ring walk during `HasDirPtrs` (on PPC) or the cascade deletion logic loops indefinitely

### Why Xbox didn't crash

Xbox's allocator doesn't zero freed memory. The stale ring entries retain their vtable pointer, so virtual calls through dangling pointers happen to succeed (the `Replace` call on a freed ref is UB that works by accident). The self-loop corruption exists on both platforms — it's just invisible on Xbox.

### Why ASan didn't detect it

ASan quarantines freed memory, keeping it readable with original content. The vtable pointer survived in quarantined memory, so zero use-after-free reports. Classic "works under sanitizer, crashes in production" pattern.

## Fix

### 1. Remove duplicate AddRef (Dir.h) — ROOT CAUSE FIX

```cpp
// FIXED — single AddRef from base class only
ObjDirPtr<C>::ObjDirPtr(C *dir) : ObjRefConcrete<C>(dir), mLoader(nullptr) {
#ifdef HX_NATIVE
    if (dir) {
        DirPtrRefCounts()[(const void *)dir]++;
    }
#endif
}
```

Removing the duplicate `AddRef` fixes the PPC match to **100%** — confirming this was a decomp error, not intentional Xbox behavior.

### 2. Live ring walk (Object.cpp)

Replaced the native snapshot+iterate approach with the Xbox-style live walk. Added a defensive vtable null-check as defense-in-depth (should never fire now that the root cause is fixed):

```cpp
SuppressEraseScope guard;
while (next != this) {
    ObjRef *cur = next;
    if (!*(void **)cur) {          // defense-in-depth
        cur->prev->next = cur->next;
        cur->next->prev = cur->prev;
        continue;
    }
    cur->Replace(obj);
}
```

### 3. ObjPtrVec destructor cleanup (ObjPtr_p.h)

When an `ObjPtrVec` is destroyed during a `ReplaceList` walk, its deferred purge entries in `gDeferredPurges` become dangling. Added cleanup in `~ObjPtrVec()`:

```cpp
if (gSuppressRefErase && !gDeferredPurges.empty()) {
    gDeferredPurges.erase(
        std::remove_if(gDeferredPurges.begin(), gDeferredPurges.end(),
            [this](const DeferredPurge &p) { return p.vec == this; }),
        gDeferredPurges.end());
}
```

## What was investigated but NOT the fix

### Deferred ObjDirPtr deletion

Tried deferring `delete mObject` in `ObjDirPtr::operator=` during merge scopes using `DeferDirDeleteScope` (RAII depth counter). Three scope levels tested (ReplaceList, MergeDirs, FileMerger::FinishLoading). All failed because the deletes originated from `UIPanel::Unload` — completely outside any merge scope. Broadening the scope enough to catch them caused new crashes (double-free, stale objects). Deferred deletion changes lifecycle semantics unpredictably.

## Verification

| Test | Before | After |
|------|--------|-------|
| `ReplaceListLiveWalkDoesNotCrash` | PASS | PASS |
| `DirPtrRefCountsConsistentAfterMerge` | PASS | PASS |
| `DeferredPurgeCleanedOnObjPtrVecDestruction` | PASS | PASS |
| `ReplaceRefsWithSelfDeletingObjDirPtr` | PASS | PASS |
| `ObjDirPtrCascadeDeleteDoesNotDoubleFree` | **HANG** | **PASS** |
| `RemoveSubDirReleasesDirPtrRef` | **HANG** | **PASS** |
| game_screen venue merge (YMCA flow, 3000 frames) | **SIGSEGV** | **No crash** |
| PPC `ObjDirPtr(C*)` match | 100% | 100% |

## Files Changed

| File | Change |
|------|--------|
| `src/system/obj/Dir.h` | Remove duplicate `AddRef` in `ObjDirPtr(C*)` constructor |
| `src/system/obj/Object.cpp` | ReplaceList: snapshot → live walk + vtable check |
| `src/system/obj/ObjPtr_p.h` | ~ObjPtrVec: clean up deferred purge entries on destruction |
| `native/tests/test_object_lifetime.cpp` | 5 new ring corruption tests, cascade test re-enabled |

## Key Diagnostic Signals (reference for future ring bugs)

| Signal | What it means |
|--------|--------------|
| `SIGSEGV at 0x28` | Null vtable → `Replace()` virtual at slot 5 (5×8=0x28 on x86_64) |
| `SIGSEGV at 0xfffffff8` | Corrupted allocator metadata (double-free or heap corruption) |
| ASan: 0 warnings but crashes without ASan | Freed memory content matters (quarantine masks the bug) |
| `ref 1` or `ref 2` always corrupt, `ref 0` fine | Processing ref 0 cascades destruction that corrupts later refs |
| Destructor hang in `mSubDirs.clear()` | ObjRef self-loop prevents unlinking → infinite cascade |
| Backtrace: `~RndDir → ~ObjectDir → mSubDirs.clear()` | ObjDirPtr destructor cascade (follow the `delete mObject` chain) |
