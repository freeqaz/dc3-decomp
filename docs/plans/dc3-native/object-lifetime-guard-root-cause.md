# Native Object Lifetime Guard Root Cause Plan

## Goal

Document the current `HX_NATIVE` lifetime guards in the object/subdir path, what
behavior they are preserving, how the native tests exercise that behavior, and
what underlying bug we still need to fix so the guards can be removed instead
of papering over corruption.

This note is specifically about the object lifetime and subdir ownership path:

- `ObjDirItr`
- `ObjDirPtr`
- `ObjectDir::~ObjectDir`
- `ObjectDir::DeleteObjects`
- `MergeDirs` / `MergeObjectsRecurse`

It is not a general `HX_NATIVE` inventory.

## Current Status

Validated on native with:

```sh
ninja -C native/build milo-tests
timeout 120s native/build/milo-tests '--gtest_filter=ObjectLifetimeTest.*:DirLoaderTest.*:MiloDiagnostic.*'
timeout 180s native/build/milo-tests '--gtest_filter=ObjectLifetimeTest.*:DirLoaderTest.*:MiloDiagnostic.*:BoneGroundTruth.*:ClipPoseFixture.*:MainMiloLoadTest.*'
```

These slices currently pass.

Additional focused repros now exist in
[`native/tests/test_object_lifetime.cpp`](/home/free/code/milohax/dc3-decomp/native/tests/test_object_lifetime.cpp):

- `DeleteOrderDoesNotRequireTopologicalSortForObjPtr`
  - proves the basic `ObjPtr` contract survives plain delete ordering
- `DeleteAutosaveWarningRawDir`
  - loads `ui/title/gen/autosave_warning.milo_xbox` from `MILO_LIB` and then
    hangs on raw `ObjectDir` deletion
- `DeleteAutosavingIconSubdirOnly`
  - detaches the `autosaving_icon` child dir from that asset and still hangs on
    deleting the isolated subdir

This matters because it narrows the modern unload blocker to a small raw asset
teardown repro, rather than requiring a full UI screen transition.

Current outcome:

- the destructor-side `gSuppressDirPtrDelete` guard in `src/system/obj/Dir.cpp`
  is no longer required for the validated slices after the fixes documented below
- `ObjDirItr` dead-entry filtering in `src/system/obj/Dir.h` is not required by
  the current real test workload; the old failing oracle was injecting a stale
  freed pointer directly into the hash table

Concrete fixes that changed the result:

1. The `kMoveAllSubdirs` subdir-transfer path in `MergeObjectsRecurse` no longer
   creates a temporary `ObjDirPtr<ObjectDir>` when moving a subdir from source
   to destination. It now appends the existing source `ObjDirPtr` directly
   before erasing the source vector entry.
2. `ObjectDir::RemoveSubDir` no longer compares `ObjDirPtr` internals using a
   hard-coded `+0xc` offset. It now compares actual `ObjectDir *` values.

Important current nuance:

- the top-level `!b` / `kMergeReplace` path in
  [`src/system/obj/Utl.cpp`](/home/free/code/milohax/dc3-decomp/src/system/obj/Utl.cpp)
  still constructs a temporary `ObjDirPtr<ObjectDir>` before `AppendSubDir()`
- the "temporary ObjDirPtr eliminated" conclusion is therefore only fully true
  for the moved-subdir loop, not globally for every merge/subdir path

## Test Architecture

The primary oracle is `native/tests/test_object_lifetime.cpp`.

The tests are intentionally not all the same kind of coverage. They split into
three buckets:

### 1. Synthetic safety tests

These inject corruption directly to verify that iteration and teardown do not
touch freed objects.

Former synthetic test:

- `ObjectLifetimeTest.ObjDirItrSkipsDeadHashEntries`

Architecture:

- Create an `ObjectDir`
- Create an object in it
- Delete the object
- Manually reinsert the stale pointer into the dir hash entry
- Iterate the dir with `ObjDirItr<Hmx::Object>`

That test was useful as a hardening probe, but it was not validating a normal
engine invariant. The real invariant is narrower:

- deleting an object must null `Entry::obj` before the object is freed
- iteration must safely ignore null hash entries

The active test now is:

- `ObjectLifetimeTest.ObjDirItrIgnoresNullHashEntriesAfterDelete`

This is a direct safety test. It does not try to prove that normal gameplay
creates the corruption itself. It proves that the iterator currently needs to
defend against stale hash contents.

### 2. Merge ownership and ref-redirect tests

These verify the expected parity semantics of merge behavior.

Key tests:

- `ObjectLifetimeTest.MergeDirsNameCollisionLeavesOnlyLivePointers`
- `ObjectLifetimeTest.MergeDirsNoSubdirsDoesNotMergeSubdirContents`
- `ObjectLifetimeTest.MergeDirsAllSubdirsMergesContentsWithoutMovingSubdir`
- `ObjectLifetimeTest.MergeDirsMoveAllSubdirsTransfersOwnership`

Architecture:

- Build tiny source and destination dirs in memory
- Populate them with colliding objects and/or subdirs
- Run `MergeDirs`
- Validate:
  - source/destination ownership
  - redirected refs
  - live pointers only
  - iterator safety after merge

These are the tests that tell us whether `MergeDirs` semantics are correct.

### 3. Real fixture stability tests

These load actual archive content and then stress the same iteration/lifetime
paths after real merges.

Key tests:

- `ObjectLifetimeTest.MergeDirsRealFixturesLeaveOnlyLiveEntries`
- `ObjectLifetimeTest.RepeatedFixtureMergesKeepIteratorSafe`

Architecture:

- Load real `.milo` fixtures with `DirLoader::LoadObjects`
- Merge overlays into a base dir
- Delete source dirs
- Walk the destination hash table and iterator

Expectation:

- No dead pointers visible to tests
- No iterator crashes
- Repeated merges remain stable

This bucket is important because it tells us whether a change that passes a
synthetic unit case still survives real content.

## Guard 1: `ObjDirItr` Dead-Entry Filtering

Location:

- `src/system/obj/Dir.h`

Relevant behavior:

- Skip `nullptr` entries
- Skip objects that are not in the native live-object set
- Skip objects whose vtable pointer is null/corrupt before `dynamic_cast`

Why it existed:

- `dynamic_cast` on freed or vtable-corrupted objects is unsafe
- the old native-only code assumed the dir hash table could contain stale
  `Entry::obj` pointers during normal operation

What we now know:

- removing it does not break the current real test workload
- it only breaks the synthetic test that manually writes a freed pointer back
  into `Entry::obj`
- normal object deletion goes through `Hmx::Object::RemoveFromDir()`, which
  calls `mDir->RemovingObject(this)`, looks up the hash entry, and sets
  `entry->obj = nullptr` before the object is fully destroyed

What that means:

- the guard was hardening against arbitrary hash corruption
- it was not required to preserve the current tested engine invariants
- removing the `HX_NATIVE` split here is justified so long as we keep coverage
  on the real deletion/null-entry invariant

Important nuance:

The old synthetic test deliberately injected corruption after object deletion.
That was useful for probing robustness, but it did **not** prove that the
engine itself produced the corruption. After broader testing and code tracing,
we do not currently have evidence of a real native flow that leaves a freed
pointer in `Entry::obj`.

### Current resolution

The current resolution is:

- keep `ObjDirItr::Advance()` shared and simple
- rely on the real invariant that object removal nulls the hash entry
- validate that invariant with `ObjectLifetimeTest.ObjDirItrIgnoresNullHashEntriesAfterDelete`

If a future real workload demonstrates stale freed pointers in `Entry::obj`,
that should be treated as a producer bug in the object removal path, not as a
reason to restore a native-only iterator filter.

## Guard 2: `gSuppressDirPtrDelete` During Subdir Clear

Locations:

- `src/system/obj/Dir.cpp`
- `src/system/obj/Dir.h`

Relevant path:

1. `ObjectDir::~ObjectDir()` clears `mSubDirs`
2. each `ObjDirPtr<ObjectDir>` destructor runs
3. `ObjDirPtr::operator=(nullptr)` calls `mObject->Release(this)`
4. then it decides whether to delete the target subdir by calling
   `mObject->HasDirPtrs()`

Expected behavior:

- clearing a dir's subdir vector should release ownership cleanly
- if no dir pointers remain, the subdir may be deleted
- no freed/ref-corrupted `ObjRef` nodes should be traversed

Original observed failure when the guard was removed:

- `ObjectLifetimeTest.MergeDirsMoveAllSubdirsTransfersOwnership` crashed

Observed stack:

- `ObjectDir::HasDirPtrs()`
- `ObjDirPtr<ObjectDir>::operator=(nullptr)`
- `ObjDirPtr<ObjectDir>::~ObjDirPtr()`
- `std::vector<ObjDirPtr<ObjectDir>>::clear()`
- `ObjectDir::~ObjectDir()`

This was the working hypothesis before the root cause was proven. The current
state is different: the destructor-side suppression is no longer needed for the
validated slices, because the crash was being caused earlier by the bugs in the
move/remove path below.

## Relationship To The Merge Fix

The current shared `MergeDirs` ownership fix is still correct:

- for `kMoveAllSubdirs`, the source parent removes the moved subdir from
  `fromDir->mSubDirs`
- ownership is transferred to `toDir`

That fix addressed a logic bug: moved subdirs were appended to the destination
without being erased from the source parent vector.

The remaining problem is different:

- after ownership is correct, native teardown may still encounter a corrupted
  or pathologically expensive subdir/object teardown path during destruction

So:

- merge ownership bug: fixed
- subdir teardown/ref-ring corruption: still unresolved

## Proven Root Causes

### Root Cause 1: transient `ObjDirPtr` in `MergeObjectsRecurse`

The old move-subdir code did this in the `kMergeReplace` case:

```cpp
ObjDirPtr<ObjectDir> dirPtr(sd);
toDir->AppendSubDir(dirPtr);
```

This created a temporary extra `ObjDirPtr` node in the moved subdir's ref ring.
On native, that temporary's destructor ran during `MergeObjectsRecurse` itself,
inside the move path, before the source vector entry was erased.

`gdb` showed the crash here:

- `ObjDirPtr<ObjectDir>::operator=(nullptr)`
- called from `ObjDirPtr<ObjectDir>::~ObjDirPtr()`
- called from `MergeObjectsRecurse(...)`

The fix was to avoid the temporary entirely:

```cpp
toDir->AppendSubDir(subDirs[i]);
```

That reuses the existing source `ObjDirPtr` as the thing being copied into the
destination vector, so there is no transient extra ref node to destruct.

### Root Cause 2: `RemoveSubDir` was using a 32-bit layout hack on native

`ObjectDir::RemoveSubDir` used this comparison:

```cpp
*(u32 *)((u8 *)&(*it) + 0xc) == *(u32 *)((u8 *)&dPtr + 0xc)
```

That is a 32-bit layout assumption. On native 64-bit, it is not a valid way to
compare the underlying `ObjectDir *` targets.

Symptoms:

- `RemoveSubDir` could fail to remove the requested subdir
- ownership/lifetime tests on native could observe stale subdir membership

The fix was to compare the actual pointed-to dirs:

```cpp
if ((ObjectDir *)*it == (ObjectDir *)dPtr)
```

### Result

After both fixes:

- `ObjectLifetimeTest.RemoveSubDirReleasesDirPtrRef` passes
- `ObjectLifetimeTest.MergeDirsMoveAllSubdirsTransfersOwnership` passes
- the destructor-side `gSuppressDirPtrDelete` wrapper around `mSubDirs.clear()`
  can be removed for the validated native slices

## Remaining Root Cause Hypotheses

These are the concrete hypotheses to test, in order.

### Hypothesis A: a real native path still leaves stale freed pointers in `Entry::obj`

Possible mechanism:

- some real native path still leaves stale pointers in `ObjectDir::Entry::obj`
- `ObjDirItr` currently avoids crashing by checking liveness/vtable state before
  `dynamic_cast`

Why this now looks weaker:

- removing the iterator-side filter did not break the broader native suite
- the old failing test was synthetic and not representative of normal removal
- code tracing shows `Hmx::Object::RemoveFromDir()` nulls the hash entry before
  the object is fully destroyed

## What Is Expected To Be True After The Real Fix

To keep the destructor guard removed cleanly, all of the following must stay true:

1. Moving a subdir transfers ownership exactly once
2. The moved subdir's `mRefs` ring contains only valid live nodes
3. Destroying the parent dir can clear `mSubDirs` without suppressing delete
   decisions
4. `ObjectDir::HasDirPtrs()` can answer safely during teardown
5. `ObjectLifetimeTest.MergeDirsMoveAllSubdirsTransfersOwnership` passes without
   `gSuppressDirPtrDelete`

To remove the iterator guard cleanly, all of the following must be true:

1. No real flow leaves stale pointers in `ObjectDir::Entry::obj`
2. `ObjDirItr` never sees freed objects in real fixture merges
3. coverage proves the real invariant instead:
   `ObjectLifetimeTest.ObjDirItrIgnoresNullHashEntriesAfterDelete`

## Recommended Debugging Plan

### Phase 1: keep the move/remove regressions locked down

Protect the proven fixes with tests:

- `ObjectLifetimeTest.RemoveSubDirReleasesDirPtrRef`
- `ObjectLifetimeTest.MergeDirsMoveAllSubdirsTransfersOwnership`

### Phase 2: investigate stale hash entries

Focus on the iterator-side filter:

- identify real non-synthetic flows that can leave stale `Entry::obj` pointers
- prove whether those flows are still possible after the current merge/remove
  fixes

### Phase 3: only then re-attempt `ObjDirItr` guard removal

Do not treat the iterator guard like the destructor guard. The destructor guard
was removable because the validated bug was in the move/remove path and could be
fixed directly. The iterator guard should only be removed once we can prove the
stale-entry invariant itself has been eliminated.

Also do not over-fit later unload issues to "hash-table order requires
topological deletion" without a fresh repro. `Hmx::Object::~Object()` already
calls `ReplaceRefs(nullptr)`, which is specifically intended to make simple
object-teardown order irrelevant. If a real workload still breaks, that points
to a producer bug in ref cleanup or a separate teardown-cost problem, not yet to
an architectural requirement for dependency-ordered deletion.

## Concrete TODOs

- [x] Eliminate the temporary `ObjDirPtr` from `MergeObjectsRecurse` move path
- [x] Replace `RemoveSubDir`'s native-invalid `+0xc` pointer comparison
- [x] Remove the destructor-side `gSuppressDirPtrDelete` wrapper and revalidate the native slices
- [ ] Identify real flows that can still leave stale `ObjectDir::Entry::obj` pointers
- [ ] Re-evaluate whether iterator liveness filtering can become shared behavior, or whether the real invariant is "stale hash entries must never persist"

## Acceptance Criteria

This bug is fixed for the validated scope because:

- `ObjectLifetimeTest.RemoveSubDirReleasesDirPtrRef` passes
- `ObjectLifetimeTest.MergeDirsMoveAllSubdirsTransfersOwnership` passes without
  the destructor-side `gSuppressDirPtrDelete`
- the broader native slice still passes
- the explanation now points to concrete bookkeeping bugs in the move/remove
  path, not to an irreducible native-only requirement
