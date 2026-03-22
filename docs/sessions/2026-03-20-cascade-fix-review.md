# Cascade Fix Independent Review & Multi-Pass Phase 0 Proposal

**Date**: 2026-03-20
**Status**: Review complete. Recommended fix: multi-pass Phase 0.
**Reviewer**: Opus subagent (independent analysis)
**Builds on**: [2026-03-20-nullifyobj-and-remaining-crashes.md](2026-03-20-nullifyobj-and-remaining-crashes.md)

## Context

Two fixes were proposed to resolve the remaining TaskTimeline::Poll SIGSEGV
during cascading `~ObjectDir` teardown. An Opus subagent was asked to
independently analyze both fixes, identify risks, and recommend the best path.

## Fix A: NullifyObj Fallback in ~Object During Cascade

In `~Object()` during cascade, if the ring is non-empty after Phase 0, use
SnapshotRing + NullifyObj to clean up stragglers.

```cpp
if (ObjectDir::InDeleteObjects()) {
    if (mRefs.next != &mRefs) {
        std::vector<ObjRef *> snapshot;
        SnapshotRing(&mRefs, snapshot);
        for (ObjRef *ref : snapshot)
            ref->NullifyObj();
    }
} else
    ReplaceRefs(nullptr);
```

### Verdict: FRAGILE

**What it does right:**
- Avoids Replace callbacks entirely (NullifyObj is purely mechanical)
- Self-looping isolates the ref without writing to neighbors
- Alive sentinel check in SnapshotRing correctly detects freed ObjRefs

**Critical risk: SnapshotRing reads freed memory during Phase 1.**

During Phase 1, sibling destructors have already run. Consider:
1. Object A has a ring containing `ObjPtrVec<Foo>::Node` entries in Object B's ObjPtrVec
2. Phase 1 destroys B first. B's `~ObjPtrVec` destroys the `std::vector<Node>`
   internal buffer via `std::allocator::deallocate()` — this is NOT deferred,
   only Object blocks are deferred (Phase 2)
3. When Phase 1 destroys A, `~Object()` runs SnapshotRing. The ring's `next`
   pointer may point into B's freed vector buffer
4. SnapshotRing reads `mAliveSentinel` and `next` from freed memory

The sentinel check works for determining if the NODE is dead (`~ObjRef` clears
sentinel before the buffer is freed). But following the `next` pointer through
freed memory to continue the ring walk is undefined behavior. The
`no_sanitize("address")` suppresses ASAN but doesn't make the read safe.

Under glibc malloc, the first 16 bytes of freed chunks are overwritten with
internal metadata (fd/bk pointers). The `next` pointer at offset 8 in an
ObjRef at the start of a freed chunk could be corrupted. ObjRefs deeper in
the allocation are typically safe, but this is platform-dependent.

**Bottom line:** Works in practice under glibc but is fundamentally unsound.
A different allocator (tcmalloc, jemalloc) could crash.

## Fix B: Null Re-Check in ObjDirPtr::operator=

Add `mObject &&` before `!mObject->HasDirPtrs()` in Dir.h:

```cpp
if (mObject) {                              // line 91 — original guard
    DirPtrRefCounts()[(const void*)mObject]--;
    if (!ObjectDir::InDeleteObjects())
        mObject->Release(this);
    if (mObject && !mObject->HasDirPtrs()) { // added "mObject &&"
        // ... destroy and free ...
    }
}
```

### Verdict: KEEP (defense-in-depth)

The stated scenario (NullifyObj nulling mObject on the current ObjDirPtr
mid-execution) is unlikely through the described mechanism — NullifyObj
operates on a different ObjRef in the ring, not the one currently executing
operator=. The re-check prevents a null deref that probably can't happen
through the described path.

Nevertheless, it's a trivially cheap defensive check with zero performance
cost. Keep it as defense-in-depth against any future code path that could
null mObject between lines 91 and 101.

## TaskMgr::Start Cascade Guard

```cpp
void TaskMgr::Start(Task *t, TaskUnits u, float f) {
    if (ObjectDir::InDeleteObjects()) {
        delete t;
        return;
    }
    mTimelines[u].AddTask(t, f);
}
```

### Verdict: KEEP

Tasks created during cascade reference objects about to be destroyed. Running
them would crash. Deleting immediately is correct.

Minor risk: scripts relying on task completion for cleanup (releasing locks,
sending "done" messages) would silently lose that work. But destructors
executing scripts during cascade are already in an undefined state — any
script depending on consistent engine state during teardown is broken by
definition.

## Recommended Fix: Multi-Pass Phase 0

Instead of reading freed memory during Phase 1 (Fix A), run Phase 0 multiple
times while all memory is still valid. Replace callbacks that re-add refs are
finite side effects — a second pass catches them.

```cpp
void ObjectDir::DeleteObjects() {
#ifdef HX_NATIVE
    std::vector<std::pair<void *, Hmx::Object *>> todo;
    for (ObjDirItr<Hmx::Object> it(this, false); it != nullptr; ++it) {
        if (it != this)
            todo.push_back({dynamic_cast<void *>((Hmx::Object *)it), it});
    }

    // Phase 0: nullify ref rings while memory is valid.
    // Multiple passes catch refs re-added by Replace callbacks
    // (e.g. ObjOwnerPtr::Replace creating new references as side effect).
    for (int pass = 0; pass < 3; pass++) {
        bool anyNonEmpty = false;
        for (auto &[block, obj] : todo) {
            if (obj->mRefs.next != &obj->mRefs) {
                anyNonEmpty = true;
                obj->ReplaceRefs(nullptr);
            }
        }
        if (!anyNonEmpty) break;
        if (pass == 2)
            MILO_WARN("DeleteObjects: rings still non-empty after 3 passes");
    }

    // Phase 1: destroy all (memory stays valid for sibling destructors)
    for (auto &[block, obj] : todo)
        obj->~Object();

    // Phase 2: defer frees until outermost ~ObjectDir completes
    for (auto &[block, obj] : todo)
        DeferFree(block);
#else
    for (ObjDirItr<Hmx::Object> it(this, false); it != nullptr; ++it) {
        if (it != this)
            delete it;
    }
#endif
}
```

### Why This Is Strictly Better Than Fix A

| | Fix A (NullifyObj in ~Object) | Multi-Pass Phase 0 |
|---|---|---|
| Reads freed memory | Yes (SnapshotRing during Phase 1) | No (all passes while memory valid) |
| Handles re-added refs | Yes (catches stragglers) | Yes (second pass catches re-adds) |
| Platform-dependent | Yes (relies on glibc preserving bytes) | No (standard C++) |
| ASAN clean | No (needs no_sanitize) | Yes (no freed-memory reads) |
| Convergence | N/A (one-shot) | Finite (Replace callbacks are one-shot side effects) |
| Complexity | Adds NullifyObj virtual | Uses existing ReplaceRefs |

### Edge Case: Nested Cascade

The `sDeleteObjectsDepth == 1` guard (outermost only) was suggested by the
reviewer. Consider whether inner cascades need multi-pass too:

- Inner cascade (depth > 1): subdir destruction during `mSubDirs.clear()`.
  The subdir's `DeleteObjects` runs its own Phase 0. Replace callbacks from
  the subdir's objects could re-add refs just like the outermost level.
- **Recommendation**: Run multi-pass at ALL cascade depths, not just depth 1.
  The cost is proportional to the number of re-added refs (typically zero
  after the first pass), so the second/third passes are usually no-ops.

### Edge Case: Infinite Re-Add Loop

If a Replace callback always re-adds a ref (infinite generator), the 3-pass
limit prevents an infinite loop. The MILO_WARN diagnostic on pass 3 alerts to
this scenario. In practice, Replace callbacks are finite side effects — they
create a fixed number of new references, not an unbounded stream.

## Summary of Recommendations

| Component | Action | Risk |
|-----------|--------|------|
| Fix A (NullifyObj fallback) | **Replace** with multi-pass Phase 0 | Eliminated |
| Fix B (mObject re-check) | **Keep** as defense-in-depth | None |
| TaskMgr::Start guard | **Keep** | Minimal (silent task drop during cascade) |
| Multi-pass Phase 0 | **Implement** | None (standard C++, no freed-memory reads) |
| NullifyObj virtual | **Keep as infrastructure** (don't use as primary fix) | None |
| ~Object cascade skip | **Keep** (Phase 0 handles refs; ~Object skip is correct) | None |
