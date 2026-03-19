# P0: MergeDirs Parity Failures

**Date**: 2026-03-19
**Status**: Blocked — all 5 real-venue merge tests crash with heap corruption
**Priority**: P0 — blocks DTA pipeline convergence, venue component loading, and cleanup
of manual workarounds in App.cpp

## Problem

The `MergeScopeParity` test suite validates that native merge infrastructure matches Xbox
behavior. All 5 real-venue tests (Tier 2) crash with `corrupted double-linked list`
during `DirLoader::LoadObjects` of actual venue `.milo_xbox` files. The synthetic tests
(Tier 1) pass individually but segfault during GPU teardown (cosmetic, not blocking).

## How to Reproduce

```bash
# Build tests
cmake --build native/build --target milo-tests -- -j$(nproc)

# Run all merge parity tests
cd native/build && ctest --output-on-failure -R MergeScopeParity

# Run a single failing test under GDB/ASAN
cd native/build && ./milo-tests --gtest_filter=MergeScopeParityTest.VenueProxyMergeIntoWorldRoot
```

### Test Results (2026-03-19)

| Test | Result | Notes |
|------|--------|-------|
| SyntheticNonProxyMergeFlattensContent | PASS (segfault on teardown) | Test logic passes, GPU cleanup crashes |
| SyntheticProxyMergeAddsSubdir | PASS (segfault on teardown) | Same teardown issue |
| SyntheticNameCollisionRedirectsRefs | PASS (segfault on teardown) | Same teardown issue |
| SyntheticSequentialNonProxyThenProxy | PASS (segfault on teardown) | Same teardown issue |
| SyntheticCrossRefsPreservedAcrossMerge | PASS (segfault on teardown) | Same teardown issue |
| **VenueProxyMergeIntoWorldRoot** | **ABORT** | `corrupted double-linked list` |
| **VenueNonProxyMergeFlattensIntoTarget** | **ABORT** | `corrupted double-linked list` |
| **VenueMergeSubdirObjectsFindableFromTop** | **ABORT** | `corrupted double-linked list` |
| **SequentialMergesIntoSameWorldRoot** | **ABORT** | `corrupted double-linked list` |
| **RepeatedVenueMergeAfterClear** | **ABORT** | `corrupted double-linked list` |

## Root Cause Analysis

The `corrupted double-linked list` error comes from glibc's internal heap allocator
detecting a corrupted free list. This indicates use-after-free or double-free in the
ObjRef ring manipulation code during merge operations on real venue data.

### Key Code Paths

1. **`MergeDirs`** (`src/system/obj/Utl.cpp:417`) — entry point, calls `MergeObjectsRecurse`
2. **`MergeObjectsRecurse`** (`Utl.cpp:353`) — walks `fromDir` hash table and subdirs,
   calls `MergeObject` for each object pair. The inner loop at lines 367-380 manipulates
   the ObjRef ring directly (Release + AddRef) which is fragile
3. **`MergeObject`** (`Utl.cpp:91`) — calls `ReplaceRefs` (ring walk) and `Copy`
4. **`ObjRef::ReplaceList`** (`Object.cpp:21`) — walks the ref ring calling `Replace()`
   on each node, with a force-unlink safety net. Sets `gInReplaceList` to suppress
   ObjPtrVec erasure during walk
5. **`ObjPtrVec::ReplaceNode`** (`ObjPtr_p.h:213`) — when `gInReplaceList` is true,
   suppresses vector erase (logs "ReplaceNode: suppressed erase"). This avoids iterator
   invalidation but may leave dangling entries

### Likely Failure Scenario

With real venue data (hundreds of objects with complex cross-references), the ref ring
walk encounters a case where:

1. `ReplaceList` walks the ring, calling `Replace()` on each ObjRef
2. A `Replace()` call triggers `ReplaceNode` which — due to `gInReplaceList` — skips
   the vector erase, leaving a node pointing at freed memory
3. Later access to this dangling pointer corrupts the heap

The synthetic tests work because they have trivial ref topologies (2-3 objects, simple
linear references). Real venues have deeply nested subdirs, hundreds of cross-references
between meshes/materials/textures, and ObjPtrVec/ObjPtrList collections with complex
ownership patterns.

### Observable Symptoms

At runtime, the venue proxy merge produces suspiciously sparse results:

```
DC3 OnFileLoaded(venue) dir=... venue=... 'glitterati' hash=1 subdirs=1
```

A properly merged venue should have hundreds of objects in its hash table, not 1. This
suggests the merge either crashes partway through or objects aren't being properly
registered.

## What Needs to Happen

### Phase 1: Diagnose the crash site

1. **Build with AddressSanitizer** to get the exact crash stack:
   ```bash
   cmake -S native -B native/build-asan -G Ninja \
     -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
     -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
   cmake --build native/build-asan --target milo-tests -- -j$(nproc)
   cd native/build-asan
   ./milo-tests --gtest_filter=MergeScopeParityTest.VenueProxyMergeIntoWorldRoot
   ```
   ASAN will pinpoint the exact use-after-free or double-free with a full stack trace.

2. **Run under GDB** if ASAN is inconclusive:
   ```bash
   gdb --args ./milo-tests --gtest_filter=MergeScopeParityTest.VenueProxyMergeIntoWorldRoot
   (gdb) run
   # When it aborts, get backtrace:
   (gdb) bt
   ```

3. **Enable merge debug logging** to trace object-by-object merge:
   ```bash
   MILO_DEBUG_MERGE=1 ./milo-tests --gtest_filter=...
   ```
   This activates per-object logging in `MergeObject` (Utl.cpp:96-108).

### Phase 2: Fix the ObjRef ring manipulation

The `MergeObjectsRecurse` ring manipulation (Utl.cpp:367-379) is the most suspicious code:

```cpp
for (ObjRef *it = fromDir->mRefs.next; it != &fromDir->mRefs;) {
    Hmx::Object *owner = it->RefOwner();
    if (owner && owner->Dir() == fromDir) {
        ObjRef *prevRef = it->prev;
        it->Release(nullptr);       // unlinks from fromDir's ring
        it->AddRef(&tempRefs);       // links into temp ring
        it = prevRef;               // BUG? prevRef may have been invalidated
    }
    it = it->next;                   // advances — but after the if-block already set it
}
```

Issues to investigate:
- The `it = prevRef` followed by `it = it->next` means when the if-branch fires, the
  loop advances to `prevRef->next`. But if `Release` or `AddRef` modified `prevRef->next`,
  this skips or revisits nodes
- The loop runs on `fromDir->mRefs` while simultaneously modifying it via Release/AddRef
- With hundreds of refs (real venue data), any ring corruption here cascades

### Phase 3: Fix the `gInReplaceList` suppression

The `ReplaceNode` suppression (`ObjPtr_p.h:224-228`) warns but doesn't actually clean up:

```cpp
if (!gInReplaceList) {
    mNodes.erase(mNodes.begin() + (n - mNodes.begin()));
} else {
    MILO_WARN("ReplaceNode: suppressed erase during ReplaceList (owner=%s)", ...);
    // node left in vector pointing at nullptr or freed object
}
```

After `ReplaceList` completes, these suppressed-erase nodes are never cleaned up. They
become dangling pointers that corrupt the heap on next access. Fix: collect nodes to erase
in a deferred list, then erase them after the ring walk completes.

### Phase 4: Verify fix with tests

```bash
# Run all merge tests
cd native/build && ctest --output-on-failure -R MergeScopeParity

# Run the full test suite to check for regressions
ctest --output-on-failure

# Runtime verification with a real venue
DC3_VENUE=glitterati DC3_SCREEN=game_screen DC3_HEADLESS=1 DC3_MAX_FRAMES=200 \
  ./dc3-native 2>&1 | grep -E "venue|hash|corrupt|error"
```

Expected after fix:
- All 10 MergeScopeParity tests pass cleanly (no segfault, no abort)
- Venue proxy merge produces a dir with hundreds of objects (not `hash=1`)
- No "corrupted double-linked list" errors
- No "ReplaceNode: suppressed erase" warnings during normal operation

### Phase 5: Fix GPU teardown segfault (low priority)

The synthetic tests pass but segfault during GPU device teardown. This is a test
infrastructure issue (GpuDevice cleanup order), not a merge bug. Fix by ensuring
`GpuDevice::Shutdown()` runs before test fixture teardown, or by skipping GPU init for
non-rendering tests.

## Why This Is P0

1. **Venue draw architecture** — We removed the explicit venue draw (Phase 1A of the draw
   fix), so the venue now renders through `world_panel` → `HamDirector`. This path works,
   but the venue proxy merge is broken (`hash=1` after merge), meaning cross-references
   between venue objects may be corrupt

2. **extras.fm is dead in DC3** — Investigation confirmed DC3 venues don't have
   `extras.fm` (unlike RB3). Component loading stays manual. But fixing the merge
   infrastructure is still required for the venue proxy merge to work correctly

3. **DTA pipeline convergence** — The long-term goal is to rely on DTA lifecycle hooks
   instead of hardcoded workarounds. The merge system is the foundation — if `MergeDirs`
   corrupts the heap, nothing downstream works reliably

4. **crowd_clips.fm is working** — The FileMerger pipeline itself functions correctly
   (crowd animation clips load via `crowd_clips.fm`). The bug is specifically in
   `MergeDirs` / `MergeObjectsRecurse` when handling complex real-world object graphs

## Files

| File | Role |
|------|------|
| `native/tests/test_merge_scope_parity.cpp` | Test suite (Tier 1 synthetic + Tier 2 real venue) |
| `src/system/obj/Utl.cpp` | `MergeDirs`, `MergeObjectsRecurse`, `MergeObject`, `ReplaceObject` |
| `src/system/obj/Object.cpp` | `ObjRef::ReplaceList`, `Object::~Object` (ReplaceRefs) |
| `src/system/obj/Object.h` | ObjRef ring (prev/next), `gInReplaceList` flag |
| `src/system/obj/ObjPtr_p.h` | `ObjPtrVec::ReplaceNode` (suppressed erase), `ObjPtrList::ReplaceNode` |
| `src/system/obj/Dir.cpp` | `ObjectDir::SetName`, `AppendSubDir`, `RemoveSubDir` |
| `src/system/char/FileMerger.cpp` | `FinishLoading` (proxy/non-proxy merge paths) |

## Related

- [2026-03-19-venue-draw-investigation.md](2026-03-19-venue-draw-investigation.md) — venue draw architecture investigation
- [../native/FILEMERGER_CONVERGENCE.md](../native/FILEMERGER_CONVERGENCE.md) — FileMerger convergence status
