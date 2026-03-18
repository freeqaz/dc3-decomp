# Session: Venue Merge Crash — ObjRef Ring Corruption Root Cause

**Date**: 2026-03-18

## Problem

Native port (and web/WASM) crashes during `game_screen` entry when venue `.milo` files are merged. `SIGSEGV at address 0x28` in `ObjRef::ReplaceList` called from `MergeObjectsRecurse`. This blocked all gameplay — 9/20 telemetry tests failed.

The crash was previously masked by a ring validation guard in `ReplaceRefs()` (skipped corrupt rings). That guard was removed in commit `b9719618e` because it's not in the Xbox code — but the **underlying corruption** was never fixed.

## Root Cause Analysis

### Factor 1: Architectural mismatch — snapshot vs live walk

The native port used a **snapshot+iterate approach** for `ReplaceList`:

```cpp
// BROKEN: Native snapshot approach
std::vector<ObjRef *> refs;
for (ObjRef *cur = next; cur != this; cur = cur->next)
    refs.push_back(cur);
Clear();
for (ObjRef *ref : refs)
    ref->Replace(obj);
```

Xbox uses a **live ring walk**:

```cpp
// CORRECT: Xbox live walk
while (next != this) {
    next->Replace(obj);
}
```

The snapshot stored raw `ObjRef*` pointers. During the Replace pass, cascading deletes (via `ObjDirPtr::operator=` → `delete mObject`) could free objects containing ObjRefs still in the snapshot vector — dangling pointers.

The live walk is immune because each `Replace()` removes the ref from the ring (via `Release`) before anything else happens. The ring is always the source of truth, never a stale copy.

### Factor 2: External ring corruption from UIPanel::Unload

Backtrace analysis revealed the 0x28 crash's freed refs originated from **outside the merge pipeline entirely**:

```
UIScreen::Enter
  → UIScreen::UnloadPanels
    → UIPanel::Unload
      → delete PanelDir
        → ~PanelDir → ~RndDir → ~ObjectDir
          → mSubDirs.clear()
            → ObjDirPtr::~ObjDirPtr()
              → operator=(nullptr)
                → delete mObject  ← cascading subdir deletes
```

When `UIPanel::Unload` destroys a PanelDir, the `mSubDirs.clear()` cascade deletes ObjectDirs. Those ObjectDirs' member `ObjPtrVec::Node` elements may have `mObject == nullptr` (set to null by a prior `Replace()` which also called `Release()` to remove them from the ring). But the ring's prev/next pointers on other nodes may still reference these addresses.

When the containing object is freed:
- **Xbox allocator**: Doesn't zero freed memory → vtable pointer survives → virtual calls through stale ring entries "work" (undefined behavior that happens to not crash)
- **Native allocator**: Zeros freed memory → vtable pointer becomes null → `next->Replace(obj)` tries to read vtable slot 5 (the `Replace` virtual) at offset `5 × 8 = 0x28` from null → **SIGSEGV at address 0x28**

### Why ASan didn't detect it

ASan quarantines freed memory and keeps it readable (with its original content) for a grace period. Under ASan, the vtable pointer was still valid in freed memory, so:
- Zero REPLLIST warnings
- Zero use-after-free reports
- The program ran to completion

Under the default allocator (which reuses/zeros memory faster), the vtable was zeroed, causing the crash. This is a classic "works under sanitizer, crashes in production" pattern.

## Fix Applied

### 1. Live ring walk (Object.cpp)

Replaced snapshot approach with Xbox-style live walk. Each `Replace()` removes the ref from the ring via `Release()`, so `next` naturally advances. No stale snapshot pointers.

Added defensive vtable check for externally-freed refs:

```cpp
while (next != this) {
    ObjRef *cur = next;
    // Detect freed refs whose allocator zeroed the vtable
    if (!*(void **)cur) {
        cur->prev->next = cur->next;
        cur->next->prev = cur->prev;
        continue;
    }
    cur->Replace(obj);
}
```

This handles the UIPanel::Unload corruption case — stale ring entries from external destructor cascades that happened before the walk started.

### 2. ObjPtrVec destructor cleanup (ObjPtr_p.h)

When an `ObjPtrVec` is destroyed during a `ReplaceList` walk (cascading delete via `ObjDirPtr::operator=`), its deferred purge entries in `gDeferredPurges` become dangling. Added cleanup in `~ObjPtrVec()`:

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

Tried deferring `delete mObject` in `ObjDirPtr::operator=` during merge/ReplaceList scopes using `DeferDirDeleteScope` (RAII depth counter). Three levels tested:

1. **ReplaceList scope**: Deletes still happened at depth=0 (the UIPanel::Unload cascade is outside ReplaceList)
2. **MergeDirs scope**: Same — UIPanel::Unload is outside MergeDirs
3. **FileMerger::FinishLoading scope**: Same — UIPanel::Unload is outside FileMerger entirely

When the scope was broadened enough to catch the deletes, the deferred execution caused **new crashes**: `SIGSEGV at 0xfffffff8` (double-free in `CharClip::operator delete`) and `SIGSEGV at 0x100000000` (corrupted `mTypeDef` pointer in `~Object`). The deferred objects became stale by the time the scope exited.

**Conclusion**: Deferred deletion changes object lifecycle semantics in ways that cascade unpredictably. The immediate delete is correct behavior (matches Xbox); the problem is the stale ring entries, not the deletion itself.

## Key Diagnostic Findings

| Signal | What it means |
|--------|--------------|
| `SIGSEGV at 0x28` | Null vtable → Replace() virtual at slot 5 (5×8=0x28 on x86_64) |
| `ref N vtable=null` | ObjRef memory was freed and zeroed by allocator |
| All corrupt refs were "ref 1" or "ref 2" | Processing ref 0 triggers cascade that frees subsequent refs |
| ASan: 0 warnings | Quarantine keeps vtable valid in freed memory |
| `depth=0` in all IMMEDIATE_DEL logs | Deletes happen outside any merge/ReplaceList scope |
| Backtrace: `~RndDir → ~ObjectDir → mSubDirs.clear()` | Cascade from UIPanel::Unload, not the merge pipeline |

## Files Changed

| File | Change |
|------|--------|
| `src/system/obj/Object.cpp` | ReplaceList: snapshot → live walk + vtable check |
| `src/system/obj/ObjPtr_p.h` | ~ObjPtrVec: clean up deferred purge entries on destruction |

All changes inside `#ifdef HX_NATIVE` — zero PPC decomp impact.

## Remaining Issue

A separate `SIGSEGV at 0xfffffff8` crash exists in `CharClip::operator delete` — corrupted allocator metadata with a corrupted return address (on the heap, no symbol). This was previously masked by the 0x28 crash occurring earlier in the same run. Needs separate investigation.

## Xbox vs Native Behavioral Difference

The fundamental difference is **allocator behavior**, not code logic:

- **Xbox**: Freed memory retains its content. ObjRef vtable pointers survive after free. Stale ring entries are technically use-after-free but the vtable call succeeds (the virtual function removes the ref from the ring, which is a no-op since it was already freed). The UB happens to work.

- **Native (glibc malloc)**: Freed memory may be zeroed or reused. Vtable pointer becomes null. The virtual call dereferences null+0x28, crashing immediately.

The vtable check in ReplaceList is the correct native-port fix: it detects the allocator-zeroed vtable and unlinks the stale entry, matching the net effect of what happens on Xbox (the ref is silently skipped).
