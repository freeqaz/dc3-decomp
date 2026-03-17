# Session: Deferred ObjPtrVec Null Purge Refactor

**Date**: 2026-03-17

## Problem

When `ReplaceList` walks the ObjRef ring on native, it sets `gSuppressRefErase = true`
to prevent `ObjPtrVec::erase()` from shifting vector elements and invalidating ring
prev/next pointers. The side effect: `ReplaceNode` leaves null entries in `kObjListNoNull`
vectors instead of erasing them. These nulls leaked permanently into every ObjPtrVec
touched by a ReplaceList walk.

Every consumer had to defensively filter nulls — CharClipGroup alone had 4 separate
`#ifdef HX_NATIVE` null guards (GetClip, Copy, DeleteRemaining, FindClip). The
abstraction was leaky: ObjPtrVec's `kObjListNoNull` contract was silently violated,
and every caller had to know about it.

## Root Cause Chain

1. Object deleted → `ReplaceRefs(nullptr)` → `ReplaceList(nullptr)`
2. ReplaceList snapshots ring into vector, sets `gSuppressRefErase = true`
3. For each ref: `ref->Replace(nullptr)` → `ObjPtrVec::ReplaceNode`
4. `ReplaceNode` calls `node->SetObj(nullptr)` → node becomes null
5. Would normally `erase(node)` for `kObjListNoNull`, but `gSuppressRefErase` blocks it
6. Null entry persists in vector forever

On Xbox, `ReplaceList` walks the ring directly (no snapshot/suppression), so erases
happen immediately and null entries never persist.

## Solution: Deferred Purge

Instead of silently leaving nulls, `ReplaceNode` now registers the affected ObjPtrVec
for deferred cleanup. At the outermost `ReplaceList` exit, all registered vectors purge
their null entries in a single pass. Nulls never escape `ReplaceList`.

### New Infrastructure

**`DeferredPurge` struct** (`Object.h`):
Type-erased `{void* vec, void(*purge)(void*)}` pair. Each template instantiation of
`ReplaceNode` generates a unique captureless lambda that casts back to the correct
`ObjPtrVec<T1,T2>*` and calls `PurgeNulls()`.

**`ObjPtrVec::PurgeNulls()`** (`Object.h`):
Native-only method. Reverse-iterates `mNodes`, erases any node with null object.
Null node destructors are no-ops (`~ObjRefConcrete` skips `Release` when `mObject`
is null).

**`gDeferredPurges`** (`Object.cpp`):
Global vector accumulating deferred purge registrations during a `ReplaceList` walk.

### Modified Flow

```
ReplaceList(obj):
  1. Snapshot refs (same as before)
  2. {SuppressEraseScope guard}
     - For each ref: ref->Replace(obj)
       - ReplaceNode: if suppressed, register {this, PurgeNulls} in gDeferredPurges
  3. ~SuppressEraseScope restores gSuppressRefErase
  4. If outermost (gSuppressRefErase == false) and purges pending:
     - Deduplicate by vec pointer (sort + unique)
     - Execute each purge
     - Clear list
```

### Nesting Safety

`SuppressEraseScope` saves/restores the old flag value via RAII. Nested `ReplaceList`
calls restore to `true` (the outer's value), so the purge condition
(`!gSuppressRefErase`) only fires at the outermost exit. All deferred purges from all
nesting levels accumulate and execute together at the outermost level.

## Files Changed

| File | Change |
|------|--------|
| `src/system/obj/Object.h` | Added `DeferredPurge` struct, `gDeferredPurges` extern, `ObjPtrVec::PurgeNulls()` |
| `src/system/obj/Object.cpp` | Added `gDeferredPurges` definition, cleanup pass in `ReplaceList` |
| `src/system/obj/ObjPtr_p.h` | `ReplaceNode` registers deferred purge instead of silently leaving null |
| `src/system/char/CharClipGroup.cpp` | Removed 4 `#ifdef HX_NATIVE` null guards (GetClip, Copy, DeleteRemaining, FindClip) |

## Verification

- Native build: clean (672/672 objects + link)
- PPC build: `CharClipGroup.obj` and `Object.obj` clean (all changes inside `#ifdef HX_NATIVE`)
- Native tests: 227 tests, 8/8 CharClipGroup tests pass, 4 pre-existing failures (media files / timeout)
- No decomp regressions
