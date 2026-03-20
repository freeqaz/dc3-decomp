# OOM Investigation: Ring Corruption in MergeDirs Subdir Copy

**Date**: 2026-03-19
**Trigger**: `milo-tests` OOM-killed at 83 GB RSS
**Status**: Fixed (approach 3 — skip mSubDirs during merge)

## Timeline

1. `milo-tests` (PID 3503762) was OOM-killed consuming 83 GB RSS / 202 GB virtual
2. Isolated to `ObjectLifetimeTest.RepeatedFixtureMergesKeepIteratorSafe`
3. Narrowed to iteration 1: merging `skeleton_bones_resource.milo` into `main_resource`
4. Confirmed via ASan: **heap-use-after-free** in `ObjectDir::Copy`

## Reproduction

```
ulimit -v 2097152  # 2 GB cap
native/build/milo-tests --gtest_filter='ObjectLifetimeTest.RepeatedFixtureMergesKeepIteratorSafe'
```

Before fix: crashes with `std::bad_alloc` within seconds.
After fix: passes in ~4 ms.

## Root Cause

### The OOM mechanism

`SnapshotRing()` in `Object.cpp` walks an object's ref ring into a `std::vector`. When the ring is corrupted (nodes don't loop back to sentinel), the walk continues forever, growing the vector until memory is exhausted.

### How the ring gets corrupted

The corruption chain during `MergeDirs(fromDir=skeleton_bones_resource, toDir=main_resource)`:

1. **`MergeObject(fromObj, toObj, toDir, action)`** — The top-level merge copies source ObjectDir properties into the target via `Copy(kCopyDeep)`.

2. **`ObjectDir::Copy` does `COPY_MEMBER(mSubDirs)`** — This is `mSubDirs = c->mSubDirs`, a `std::vector<ObjDirPtr<ObjectDir>>` assignment. The old subdir's `ObjDirPtr` is destroyed.

3. **`ObjDirPtr::operator=(nullptr)` deletes the old subdir** — When the old `ObjDirPtr` is released and `HasDirPtrs()` returns false (refcount drops to 0), `delete mObject` fires.

4. **`~ObjectDir` → `DeleteObjects()`** — The old subdir's destructor deletes all contained objects (CharBones, RndTransformables, meshes).

5. **Freed objects leave stale ring nodes** — The freed `CharBone` objects had `mParent`/`mTarget` ObjPtrs that were linked into the ref rings of bones in `main_resource` (the parent dir). The `InDeleteObjects()` guard skips `Release()` for **all** targets during the cascade — including live parent dir objects. Their rings now have freed, unlinked nodes.

6. **Subsequent `MergeObjectsRecurse` triggers the corruption** — When `MergeObject` processes overlapping bones (e.g., `bone_head.cb` exists in both dirs), `ReplaceRefs(foundObj)` walks the ring of the source bone. Some ring nodes now point to freed memory, creating a broken chain that never returns to the sentinel.

### The design bug: subdirs are double-processed

`MergeDirs` calls `MergeObject(fromDir, toDir)` which does `Copy(kCopyDeep)` → `COPY_MEMBER(mSubDirs)`. Then `MergeObjectsRecurse` handles subdirs again via its own loop (lines 393-414 of Utl.cpp). The subdir copy in `ObjectDir::Copy` is **redundant** — it triggers destructive cascading deletion that the merge logic doesn't expect.

### ASan confirmation

```
ERROR: AddressSanitizer: heap-use-after-free on address 0x7c4be587ae80
READ of size 8 at 0x7c4be587ae80 thread T0
    #0 ObjectDir::Copy()         — Dir.cpp (reading freed CharBone)
    #1 CharBoneDir::Copy()       — CharBoneDir.cpp:65
    #2 MergeObject()             — Utl.cpp:117
    #3 MergeDirs()               — Utl.cpp:486

freed by:
    #0 CharBone::operator delete()
    #1 ObjectDir::DeleteObjects() — Dir.cpp
    #2 ObjectDir::~ObjectDir()
    #3 CharBoneDir::~CharBoneDir()
    #4 test (delete overlay)
```

### Corrupted objects identified

After the skeleton_bones merge, these mesh objects in the unnamed subdir have >100k ring nodes (should be 1-5):

- `bone_spine3.mesh` (Trans)
- `bone_neck.mesh` (Trans)
- `bone_head.mesh` (Trans)
- `bone_L-clavicle.mesh` (Trans)
- `bone_L-ankle.mesh` (Trans)
- `bone_L-foreArm.mesh` (Trans)

All are `RndTransformable` objects whose `mParent` ObjOwnerPtrs participated in the ref rings of the deleted CharBones.

### Why iteration 0 doesn't crash but iteration 1 does

- Iteration 0 (viseme_resource): `COPY_MEMBER(mSubDirs)` replaces the original subdir. The original subdir's bones don't have complex cross-references, so deletion is clean. Ring corruption occurs but only affects 3 objects.
- Iteration 1 (skeleton_bones_resource): The subdir being replaced is the one copied from viseme_resource. It contains objects with `mParent`/`mTarget` pointers into `main_resource`'s bones (established during iteration 0's merge). When these objects are freed, their ring nodes corrupt `main_resource`'s bone rings. `SnapshotRing` then OOMs.

## Investigation Methodology

1. **Isolated the test** — `journalctl -k` identified `milo-tests`, `--gtest_list_tests` + `--gtest_filter` isolated the specific test.
2. **Memory-capped runs** — `ulimit -v` prevented full OOM, forced fast `bad_alloc`.
3. **Bisected within the test** — `fprintf` instrumentation pinpointed iteration 1, `MergeDirs`, then `ObjectDir::Copy`, then `COPY_MEMBER(mSubDirs)`.
4. **Ring integrity checks** — Added ring-walk counters at key points (pre/post merge, per-object) to identify which operations corrupt which objects.
5. **Per-merge-object tracking** — Found the exact 3 bones (`bone_head.cb`, `bone_spine3.cb`, `bone_neck.cb`) whose merge corrupts the mesh rings.
6. **ASan build** — `native/build-asan/milo-tests` confirmed heap-use-after-free with exact allocation/free/use stack traces.
7. **Both ReplaceRefs paths tested** — Corruption occurs with both the native snapshot approach and the original `ReplaceList` approach, proving the snapshot isn't the cause.

## Fix Applied: Skip mSubDirs During Merge (Approach 3)

### The fix

Added a `sInMergeDirs` flag to `ObjectDir`. Set to `true` around the `MergeObject` call in `MergeDirs`. `ObjectDir::Copy` checks this flag and skips the `COPY_MEMBER(mSubDirs)` block when true. All changes are `#ifdef HX_NATIVE` — zero PPC decomp impact.

```cpp
// Dir.h — new flag
static bool InMergeDirs() { return sInMergeDirs; }
static void SetInMergeDirs(bool v) { sInMergeDirs = v; }

// Dir.cpp — ObjectDir::Copy guards mSubDirs
if (!InMergeDirs()) {
    for (int i = 0; i < mSubDirs.size(); i++)
        RemovingSubDir(mSubDirs[i]);
    COPY_MEMBER(mSubDirs)
    for (int i = 0; i < mSubDirs.size(); i++)
        AddedSubDir(mSubDirs[i]);
}

// Utl.cpp — MergeDirs sets flag
ObjectDir::SetInMergeDirs(true);
MergeObject(fromObj, toObj, toDir, ...);
ObjectDir::SetInMergeDirs(false);
```

### Defense-in-depth: SnapshotRing safety limit

`SnapshotRing()` in `Object.cpp` has a 100k-node safety limit:

```cpp
if (++count > kMaxRingSize) {
    MILO_LOG("SnapshotRing: RING CORRUPTION — walked %zu nodes ...\n", ...);
    break;
}
```

This prevents OOM if ring corruption occurs from other sources.

### Why approach 3 is correct

- `MergeObjectsRecurse` already handles subdirs via its own loop with proper `FilterSubdir` logic (`kMergeKeep`, `kMergeMerge`, `kMergeReplace`)
- `COPY_MEMBER(mSubDirs)` wholesale replaces subdirs — ignoring the merge filter's `kNoSubdirs` / `kAllSubdirs` setting
- Skipping it makes `ObjectDir::Copy` consistent with what the merge filter requested
- Non-merge callers of `ObjectDir::Copy` (e.g., `CopyObject`) still get the subdir copy

## Approaches Tried and Rejected

### Dead object tracking set

Per-object `unordered_set<void*>` in `~Object()`, checked in `~ObjRefConcrete`. More precise than `InDeleteObjects()` — correctly allowed `Release()` for live targets. But exposed a deeper problem:

`Release()` writes to `prev->next = next; next->prev = prev`. During a cascade, the prev/next neighbors may be ObjRefs in objects already freed by earlier `DeleteObjects` iterations. On glibc, writing to freed heap blocks corrupts free list metadata → "free(): chunks in smallbin corrupted". On ASAN, it's a heap-use-after-free WRITE.

Xbox tolerates this (simpler heap allocator that doesn't immediately overwrite freed blocks). Native glibc does not.

Also hit a static destruction order bug: global objects (`WebSvcMgrCurl`) destroyed at `exit()` try to insert into `sDeadObjects` after the set itself is destroyed → heap-use-after-free in the hash table. Fixed by guarding insert/lookup with `InDeleteObjects()`, but the freed-neighbor-write problem remained.

### Two-phase DeleteObjects

Run all destructors first (without freeing memory), then free memory. Keeps ring neighbor memory valid during Release calls. Correct in theory, but invasive — `delete obj` is atomic (destructor + dealloc), splitting requires `obj->~Object()` + `operator delete(obj)` with custom allocator awareness. Deferred for future consideration.

## Test Results

| Test | Before | After |
|------|--------|-------|
| `RepeatedFixtureMergesKeepIteratorSafe` | **OOM** (83 GB) | **PASS** (4 ms) |
| `MergeDirsRingIntegrityOnOverlappingBones` | PASS | PASS |
| All 17 ObjectLifetimeTest | PASS | PASS |
| All 17 ObjectLifetimeTest under ASAN | — | PASS (0 heap-use-after-free) |
| MergeScopeParityTest.Synthetic* (5 tests) | PASS | PASS |
| PPC decomp build | 73.00% | 73.00% (no regression) |

## Remaining: Bug 2 (Cascading Destruction Order)

The `VenueProxyMergeIntoWorldRoot` test still crashes during `delete worldRoot` teardown — this is "Bug 2" from [2026-03-19-merge-parity-fix.md](2026-03-19-merge-parity-fix.md). It's a separate issue (sibling subdir destruction order causes writes to freed CharClip ring memory). See that doc for analysis and the recommended two-phase DeleteObjects fix.

## Files Modified

| File | Change |
|------|--------|
| `src/system/obj/Dir.cpp` | `InMergeDirs()` guard around mSubDirs copy; `sInMergeDirs` static |
| `src/system/obj/Dir.h` | `InMergeDirs()` / `SetInMergeDirs()` / `sInMergeDirs` declaration |
| `src/system/obj/Utl.cpp` | Set `sInMergeDirs` flag in `MergeDirs` |
| `src/system/obj/Object.cpp` | `SnapshotRing` safety limit (100k nodes) |
| `native/tests/test_object_lifetime.cpp` | `MergeDirsRingIntegrityOnOverlappingBones` test + `CountRingNodes` helper + `MultiRefHolder` class |
