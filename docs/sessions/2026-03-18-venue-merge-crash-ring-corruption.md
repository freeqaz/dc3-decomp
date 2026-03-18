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

### Background: the ObjRef ring

Every `Hmx::Object` has an `ObjRef mRefs` member that acts as the **sentinel** (head) of a circular doubly-linked list. When an `ObjPtr`, `ObjDirPtr`, or `ObjPtrVec::Node` starts pointing to an object, it calls `AddRef` to insert itself into that object's ring. When it stops pointing, it calls `Release` to unlink itself. The ring tracks "who is pointing at me" — used by `ReplaceRefs` to redirect all references when objects are merged or deleted.

```
Normal ring (sentinel + 2 refs):

   sentinel ←→ refA ←→ refB ←→ sentinel
     (Head)                      (back to Head)
```

### The corruption: what happens when you AddRef the same node twice

The base class `ObjRefConcrete(dir)` constructor already calls `dir->AddRef(this)`, correctly inserting this `ObjDirPtr` into the target's ring. With one ref, the ring looks like:

```
After first AddRef (correct):

   Head ←→ Ptr ←→ Head
   Head.next = Ptr     Ptr.next = Head
   Head.prev = Ptr     Ptr.prev = Head
```

The second `dir->AddRef(this)` in the constructor body calls `ObjRef::AddRef` again on the **same node that's already linked**. Here's what happens line by line:

```cpp
void AddRef(ObjRef *ref) {         // ref = &Head, this = Ptr (already in ring)
    next = ref;                    // Ptr.next = Head       — same as before, no visible change
    prev = ref->prev;             // Ptr.prev = Head.prev  — but Head.prev IS Ptr, so Ptr.prev = Ptr!
    ref->prev = this;             // Head.prev = Ptr       — same as before, no visible change
    prev->next = this;            // (Ptr).next = Ptr      — Ptr.next now points to ITSELF
}
```

```
After second AddRef (CORRUPT):

   Head ──→ Ptr ──→ Ptr ──→ Ptr ──→ ...   (Ptr.next = Ptr, infinite loop)
   Head ←── Ptr                             (Head.prev = Ptr, looks normal from Head's side)
         ↑                                  (Ptr.prev = Ptr, self-referencing)
```

The sentinel still thinks `Ptr` is its only member (`Head.next = Ptr`). But `Ptr` is now a **self-loop** — its `next` and `prev` both point to itself. It has effectively detached from the sentinel while the sentinel still holds a one-way reference to it.

### Why Release can't fix it

When the `ObjDirPtr` is later destroyed (e.g. `~ObjDirPtr → operator=(nullptr) → mObject->Release(this)`):

```cpp
void Release(ObjRef *ref) {
    prev->next = next;     // Ptr.prev->next = Ptr.next → Ptr.next = Ptr  (writes Ptr to itself)
    next->prev = prev;     // Ptr.next->prev = Ptr.prev → Ptr.prev = Ptr  (writes Ptr to itself)
}
```

Both assignments are **no-ops** because they write `Ptr` into locations that already contain `Ptr`. The sentinel is never updated — `Head.next` still points to the now-**freed** `ObjDirPtr` memory. The ring permanently holds a **dangling pointer**.

### The crash sequence

This plays out during normal engine operation when `UIPanel::Unload` tears down a panel's ObjectDir:

```
1. UIScreen::Enter → UnloadPanels → UIPanel::Unload
2. delete PanelDir → ~ObjectDir → mSubDirs.clear()
3. Each ObjDirPtr in the vector is destroyed:
     ~ObjDirPtr → operator=(nullptr) → mObject->Release(this)
4. Release is a NO-OP (self-loop) → sentinel keeps dangling pointer
5. The ObjDirPtr memory is freed by the vector deallocator
6. Later, MergeObjectsRecurse → MergeObject → ReplaceRefs → ReplaceList
     walks the target object's mRefs ring
7. Ring walk follows Head.next → hits the freed ObjDirPtr memory
8. Calls next->Replace(obj) — a virtual call through the freed ref's vtable
```

What happens at step 8 depends on the allocator:

- **glibc (native)**: Freed memory is zeroed → vtable pointer is `0x0` → virtual dispatch reads slot 5 at address `0x0 + 5×8 = 0x28` → **SIGSEGV at 0x28**
- **glibc (native, memory reused)**: Freed memory is overwritten with heap data → vtable pointer is garbage → virtual dispatch jumps to random heap address → **SIGSEGV at 0xfffffff8** (corrupt `free()` metadata) or other heap corruption
- **Xbox allocator**: Freed memory retains its content → vtable pointer still valid → virtual call "succeeds" (processes an already-freed ref, effectively a no-op) → **no visible crash** (UB that works by accident)
- **ASan**: Freed memory is quarantined but readable → vtable pointer still valid → virtual call succeeds → **zero reports** (classic "works under sanitizer" pattern)

### Why the destructor cascade hung

The self-loop also caused infinite hangs during `~ObjectDir → mSubDirs.clear()`. When `ObjDirPtr::operator=(nullptr)` calls `delete mObject` on a subdir, that subdir's destructor runs its own `mSubDirs.clear()`. If any of those nested ObjDirPtrs have self-looping ring entries, the `HasDirPtrs()` check (which walks the ring on PPC, or checks `DirPtrRefCounts` on native) could loop or produce wrong results, causing the cascade to never terminate.

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
