# Cascading ~ObjectDir Teardown Fix (Bug 2)

**Date**: 2026-03-20
**Status**: Core fix committed. Remaining ASAN edge case documented.
**Commits**: `365019af3` (core fix), `41822cdfe` (perf + SafeRelease)
**Builds on**: [2026-03-19-merge-parity-fix.md](2026-03-19-merge-parity-fix.md)

## The Problem

When `delete worldRoot` triggers cascading `~ObjectDir` destruction, sibling
objects read from each other's internals — ObjRef ring entries, ObjPtrList
containers, std::set nodes — causing heap-use-after-free under ASAN. The test
is `MergeScopeParityTest.VenueProxyMergeIntoWorldRoot`.

## How the Xbox 360 Original Works (and the UB it tolerates)

On Xbox 360, `~ObjectDir::DeleteObjects()` does a simple loop:

```cpp
for (ObjDirItr<Hmx::Object> it(this, false); it != nullptr; ++it) {
    if (it != this) delete it;
}
```

This has several latent UB patterns that happen to work on the console:

1. **Cross-object reads from freed memory**: When `delete` destroys object A,
   A's destructor may read from object B (a sibling in the same dir). If B was
   already deleted earlier in the loop, this reads freed memory. On Xbox,
   `MemFree` returns memory to a pool — the bytes aren't immediately
   poisoned or reused, so the reads succeed with stale-but-intact data.

2. **ObjRef ring walks through freed nodes**: `~Object()` calls
   `ReplaceRefs(nullptr)` which walks the ObjRef ring. Ring entries may reside
   in already-freed objects (ObjPtrVec buffers, ObjPtrList nodes). On Xbox,
   pool memory preserves the bytes, so the ring walk completes. On native with
   ASAN, freed memory is quarantined and poisoned — any access is flagged.

3. **Destructor side effects on destroyed siblings**: Classes like
   `FileMerger::Merger::Clear` do `while (!mLoadedObjects.empty()) delete front;`
   — deleting objects that are ALSO in the parent dir's iteration list. On Xbox,
   `RemoveFromDir()` removes them from the hash table, and the iterator skips
   them. The double-lifetime-management is UB but benign.

4. **Static/global ObjRefs with stale ring pointers**: Objects like
   `gImpostorMat` (static RndMat*) survive dir destruction. Their member
   ObjPtrs were never notified (ReplaceRefs skipped), so `next`/`prev` point
   into freed ring entries. On Xbox, subsequent `AddRef` reads the stale
   pointers from pool memory without issue.

## Architecture: The ObjRef Ring

Every `Hmx::Object` has an `ObjRef mRefs` sentinel node forming a circular
doubly-linked ring. Every `ObjPtr`/`ObjPtrVec::Node`/`ObjPtrList::Node` that
references the object is linked into this ring via `AddRef`/`Release`.

```
mRefs ↔ ObjPtr_A ↔ ObjPtrVec::Node_B ↔ ObjPtrList::Node_C ↔ mRefs
```

When an object is destroyed, `~Object()` calls `ReplaceRefs(nullptr)` which
walks the ring and calls `Replace(nullptr)` on each entry. This nullifies all
references to the dying object.

During cascade destruction, this ring walk is unsafe because:
- Ring entries may be in freed `std::vector` buffers (from `~ObjPtrVec`)
- Replace callbacks may write to freed objects (e.g., `CharBonesMeshes::Replace`
  calls `Set(it, sDummyMesh)` → `AddRef` writes to freed Node memory)
- The ring traversal itself reads `next` pointers from freed entries

## What We Implemented

### Core Fix (commit `365019af3`)

**8 files modified**, all changes under `#ifdef HX_NATIVE`:

| File | Change |
|------|--------|
| `Dir.cpp` | Two-phase `DeleteObjects`: snapshot all objects, destroy all (Phase 1), then `DeferFree` all blocks (Phase 2). Memory stays alive during Phase 1 so sibling destructors can read each other's fields. |
| `Dir.h` | `DeferFree`/`FlushDeferredFrees` infrastructure (static vector, flushed when `sDeleteObjectsDepth` returns to 0). `ObjDirPtr::operator=` defers free during cascade. |
| `Object.cpp` | `~Object()` skips `ReplaceRefs` during cascade — prevents Replace callbacks from writing to freed ObjPtrVec buffers. |
| `Object.h` | `ObjRef()` constructor self-loops `next`/`prev` (Xbox zeros via MemAlloc; native doesn't). `Hmx::Object::AddRef` detects dead ring entries via sentinel check and resets `mRefs`. |
| `ObjPtr_p.h` | `~ObjRefConcrete` skips `Release` during cascade (ring neighbors may be freed). `SetObjConcrete` self-loops before `AddRef`. |
| `FileMerger.cpp` | Guard `Merger::Clear` during cascade — skip `delete` loop and subdir operations. |
| `Faders.cpp` | Guard `~FaderGroup` during cascade — skip `RemoveClient` (Fader's `std::set` may be freed). |
| `HamCamTransform.cpp` | Guard `~HamCamTransform` during cascade — skip `ClearOldCrowds`. |

### Performance Fix (commit `41822cdfe`)

- **`sRingsDirty` flag**: Only set after `FlushDeferredFrees`. Gates the
  `IsRingPrevAlive()` sentinel check and the self-loop in `SetObjConcrete`.
  During normal (non-cascade) operation, both are skipped entirely — just one
  predicted-not-taken branch.

- **`SafeReleaseFromRing`**: `no_sanitize("address")` wrapper that properly
  unlinks the ObjRef from the old ring, even when neighbors are in freed
  (quarantined) memory. Replaces the previous self-loop+skip-Release approach
  which left ghost entries causing infinite ring traversal.

## Approaches Tried and Rejected

| # | Approach | Why It Failed |
|---|----------|---------------|
| 1 | Two-phase DeleteObjects with immediate free | Missed nested calls (subdirs) |
| 2 | Global `gDeferredFrees` via MemFree | Stack overflow from vector realloc recursion in operator delete |
| 3 | Global operator delete override | ASAN poisons memory on operator delete regardless of custom impl |
| 4 | Re-enable ReplaceRefs with SnapshotRing | ObjPtrVec buffer frees during cascade corrupt ring entries; `CharBonesMeshes::Replace → Set → AddRef` writes to freed Node buffers |
| 5 | ObjDirItr direct iteration (like PPC) | Objects deleted by sibling destructors (FileMerger) cause double-destruct of `todo` list entries |
| 6 | Self-loop + skip Release in SetObjConcrete | Ghost entries: ObjRef stays in old ring AND joins new ring → infinite ring traversal |
| 7 | `CleanStaleRingEntries` ring walk | Dead entries are self-looped → `cur->next = cur` → infinite loop. AND freed memory bytes overwritten by ASAN (0xBE fill) → SEGV |

## Remaining Problems

### 1. Persistent ObjPtrList entries with stale mObject

**Symptom**: `heap-use-after-free` in `ObjPtrList::sort → SortDraws →
RndDrawable::GetOrder()` during venue2 loading under ASAN.

**Root cause**: Objects in persistent/shared dirs (like `sMetaMaterials`,
animation clip banks) survive cascade. Their `ObjPtrList` members still
contain `Node` entries where `mObject` points to destroyed-and-freed objects
from venue1. Since `ReplaceRefs` was skipped during cascade, these Nodes were
never notified. When venue2 loads and calls `SyncDrawables → sort`, the sort
comparator reads from the freed `mObject`.

**Why it works on Xbox**: Freed memory isn't poisoned; pool allocator preserves
bytes. The stale read returns a valid-looking `mOrder` value and the sort
completes.

**Impact**: Only under ASAN. Non-ASAN builds work fine (stale reads succeed
on intact quarantined memory or pool-recycled memory).

### 2. Whack-a-mole destructor guards

Three destructor sites were guarded for cascade (`FileMerger`, `FaderGroup`,
`HamCamTransform`). The subagent search found ~30 files with similar
`while(!empty()) delete front;` or `DeleteAll()` patterns. More may surface
as new venues/assets are loaded.

### 3. sRingsDirty never clears

The flag is set after `FlushDeferredFrees` and never cleared. This means after
the first cascade, every `SetObjConcrete` call pays for a branch + potential
self-loop + `SafeReleaseFromRing` forever. Negligible cost (two stores + one
`no_sanitize` read vs normal Release), but conceptually impure.

## Possible Solutions for Remaining Issues

### Option A: Lightweight ReplaceRefs during cascade

Instead of skipping `ReplaceRefs` entirely, do a "null-only" pass that sets
`mObject = nullptr` on each alive ring entry without calling `Replace`
callbacks. This would fix the stale-mObject problem.

**Challenge**: Walking the ring reads from potentially-freed entries. Would
need `no_sanitize` ring traversal like SnapshotRing, but also needs to handle
entries whose `next` pointers were corrupted by freed-memory fills. Could use
`__asan_address_is_poisoned()` to skip entries in poisoned memory.

### Option B: Per-list cleanup after cascade

After `FlushDeferredFrees`, walk all surviving ObjPtrLists/ObjPtrVecs and
remove entries where `mObject` is in freed memory. Would need a set/map of
freed addresses (collected during DeferFree) to check against.

**Challenge**: Expensive (O(total_entries × freed_count)). Need to enumerate
all surviving lists, which requires either a global registry or walking all
surviving objects' members.

### Option C: Suppress ASAN for ObjPtrList access

Mark `ObjPtrList::sort`, `ObjPtrList::front`, and similar access methods with
`no_sanitize("address")` when `sRingsDirty` is true. The stale reads are
harmless (reading intact bytes from quarantined memory) — just suppress the
ASAN report.

**Challenge**: Requires identifying all access paths that might touch stale
entries. Risk of masking real bugs.

### Option D: Re-enable ReplaceRefs with ObjPtrVec buffer protection

The root obstacle to re-enabling `ReplaceRefs` during cascade is that
`ObjPtrVec` frees its `std::vector` buffer via `std::allocator` (not through
our deferred path). If we override `ObjPtrVec`'s allocator to defer frees
during cascade, the ring entries in the buffer stay alive, and `ReplaceRefs`
can safely walk the ring and call `Replace`.

**Challenge**: Requires custom allocator for `std::vector<Node>` in ObjPtrVec.
Template-heavy, may affect PPC codegen. Also need to handle `ObjPtrList` node
allocations similarly.

### Recommended Next Step

**Option A** is the most targeted fix. Key insight: `SnapshotRing` already
reads from freed memory with `no_sanitize` and checks the sentinel. We can
reuse this infrastructure to do a null-only pass:

```cpp
// In ~Object during cascade (instead of full ReplaceRefs):
if (InDeleteObjects()) {
    std::vector<ObjRef *> snapshot;
    SnapshotRing(&mRefs, snapshot);  // already no_sanitize, skips dead entries
    for (ObjRef *ref : snapshot)
        ref->SetObj(nullptr);  // virtual call — nulls mObject, no Replace callback
}
```

This fixes stale mObject pointers in surviving lists without triggering
Replace callbacks. The `SnapshotRing` sentinel check naturally skips freed
entries. The only risk is if `SetObj(nullptr)` has side effects beyond
nulling mObject (e.g., `ObjPtrList::Node::SetObj` might try to erase from the
list). Would need testing.
