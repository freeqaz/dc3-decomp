# ObjectDir Cascade Destruction SIGSEGV Analysis

## Summary

The native `ObjectDir` cascade teardown regressed after a set of unstaged
changes that tried to make destruction more Xbox-like by:

- removing `SafeReleaseFromRing` during cascade,
- moving ref cleanup to a `ReplaceRefs(nullptr)` pre-pass in `DeleteObjects`,
- and keeping `gInReplaceList = true` across the whole `~ObjectDir` scope.

That combination is unsound for the current native allocator model. The
`mSubDirs` vector buffer is released immediately by `std::vector::clear()`,
but its `ObjDirPtr` nodes were left linked in the target `ObjectDir` ref
rings. The next ring walk can then follow `next`/`prev` pointers into freed
vector storage and crash in `SnapshotRing` / `ReplaceRefs`.

The safest design is not the current "ReplaceRefs pre-pass after
`mSubDirs.clear()`" approach. The safest design is:

1. mechanically nullify all ref rings before `mSubDirs.clear()`,
2. keep `SafeReleaseFromRing` for cascade and post-cascade cleanup,
3. keep `gInReplaceList` scoped to actual ring walks only,
4. and treat retained `kMergeReplace` subdirs as whole retained subtrees in
   the merge flatten pass.

## What The Current Native Code Was Trying To Fix

The native code was attempting to fix real problems:

- sibling destructors could observe freed object memory,
- `ObjPtrVec` / `ObjPtrList` erase operations during `ReplaceRefs` could
  mutate containers mid-ring-walk,
- some persistent external refs needed to be nulled during dir teardown,
- and the merge pipeline needed help flattening `kMergeMerge` subdir content.

Those goals are valid. The regression came from solving them at the wrong
layer:

- `ReplaceRefs(nullptr)` is callback-heavy and assumes the ref ring is still
  structurally valid.
- `mSubDirs.clear()` destroys `ObjDirPtr` nodes in ordinary heap storage, not
  in `DeferFree` storage.
- suppressing all list erases via `gInReplaceList` changes semantics far
  outside the narrow ring-walk window it was meant for.

## Validated Crash Sequence

The crash report is correct, and the current worktree matched it.

1. `~ObjectDir()` starts and increments `sDeleteObjectsDepth`.
2. `mSubDirs.clear()` destroys `ObjDirPtr<ObjectDir>` elements.
3. During those `ObjDirPtr` destructors, cascade mode skips ring unlink, so
   the target child `ObjectDir` still has ring entries pointing at the soon to
   be freed vector buffer.
4. `std::vector::clear()` frees the backing buffer immediately with the normal
   allocator.
5. `DeleteObjects()` later runs `ReplaceRefs(nullptr)` on objects while those
   dangling ring entries are still present.
6. `SnapshotRing` reads `next` from freed vector memory, sees allocator
   metadata or reused bytes instead of a valid `ObjRef`, and eventually
   crashes.

This is the critical point: `DeferFree` protects object blocks, but it does
not protect `std::vector` backing storage. The current fix-forward design
implicitly assumed it did.

## Additional Problems In The Attempted Fix

### 1. `gInReplaceList` was widened too far

The old code only used `gInReplaceList` while actively walking a ring. The
new code kept it set for all of `~ObjectDir`, which suppresses legitimate
`kObjListNoNull` cleanup in unrelated replace paths and changes observable
behavior.

### 2. The subdir `ReplaceRefs` pass after `mSubDirs.clear()` is too late

If ref cleanup depends on ring traversal, it must happen before the subdir
vector releases its storage. Running it afterward guarantees a use-after-free
risk.

### 3. Removing the `sRingsDirty` path made post-cascade cleanup weaker

After deferred frees are flushed, surviving refs may still need
ASAN-suppressed unlink against neighbors that were part of a just-finished
cascade. The `sRingsDirty` fast path exists for that reason and should stay.

### 4. Merge flatten semantics need subtree awareness

The `FileMerger` flatten pass was right to stop hoisting objects from retained
`kMergeReplace` subdirs into the parent hash table. But that rule needs to
apply to the entire retained subtree, not only direct child objects. If a
shared subdir stays scoped, its descendants must stay scoped too.

## Recommended Design

### Teardown

Use a mechanical two-stage teardown, not a callback-heavy one:

1. At the outermost `~ObjectDir`, recursively walk reachable `ObjectDir`
   objects and call `NullifyAllRefs()` before `mSubDirs.clear()`.
2. Let `ObjDirPtr` and `ObjRefConcrete` keep using `SafeReleaseFromRing`
   during cascade and while `sRingsDirty` is set.
3. Keep `DeleteObjects()` in the old "nullify first, destroy second,
   defer-free third" shape.
4. Do not set `gInReplaceList` around the whole destructor. Only set it inside
   actual ring walks (`ReplaceRefs` / `ReplaceList`).

This is not just a revert for revert's sake. It matches the current native
memory model:

- `NullifyAllRefs()` is mechanical and does not depend on replace callbacks.
- The ring is cleaned while all `ObjDirPtr` storage still exists.
- `SafeReleaseFromRing` keeps the ring structurally consistent when later
  destructors run.
- `DeferFree` still solves the original object-block lifetime problem.

### Future Xbox-Convergent Work

If we want a fully callback-driven `ReplaceRefs(nullptr)` teardown in the
future, we need to first make subdir ref storage cascade-safe. Any of these
would be acceptable prerequisites:

- defer freeing `mSubDirs` backing storage during cascade,
- store subdir refs in a container whose node lifetime participates in
  `DeferFree`,
- or move the subdir-storage release until after all ring-walking phases are
  finished.

Without one of those, `ReplaceRefs(nullptr)` after `mSubDirs.clear()` remains
structurally unsafe regardless of local guard code.

### Merge Behavior

Keep the flatten-pass rule:

- flatten `kMergeMerge` content into the merge target,
- do not flatten `kMergeReplace` content into the parent,
- and treat a retained `kMergeReplace` subdir as a retained subtree.

That gives consistent lookup rules:

- direct target lookup finds flattened content,
- recursive lookup finds retained shared-subdir content,
- and nested descendants under retained shared subdirs do not get hoisted into
  the parent unexpectedly.

## Code Review Outcome

The concrete fixes to keep are:

- `ObjOwnerPtr::RefOwner()` returning `mOwner->RefOwner()`,
- the `DirPtrRefCounts()` optimization,
- the retained-subtree-aware merge flattening behavior,
- and the existing merge parity tests in `native/tests/test_merge_scope_parity.cpp`.

The concrete changes to reject are:

- skipping `SafeReleaseFromRing` in cascade,
- using `ReplaceRefs(nullptr)` as the primary cascade phase after
  `mSubDirs.clear()`,
- and widening `gInReplaceList` to the entire `~ObjectDir` scope.

## Regression Tests

The test coverage added for this analysis should enforce both sides of the fix:

- a cascade teardown regression test that deletes a parent dir with named
  subdirs and an external `ObjDirPtr`, using an `ASSERT_EXIT` harness so a
  SIGSEGV becomes a normal test failure instead of taking down the whole
  runner,
- and merge-scope parity tests that verify retained shared-subdir descendants
  stay scoped within the retained subtree instead of being flattened into the
  parent hash table.
