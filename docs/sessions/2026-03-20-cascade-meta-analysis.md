# Cascade Fix Meta-Analysis

**Date**: 2026-03-20
**Status**: Final recommendation ready for implementation
**Inputs**: Review doc (multi-pass Phase 0), Analysis #1 (delete-this), Analysis #2 (mSubDirs timing), independent verification

## Background

During `~ObjectDir` cascade teardown, surviving `ObjPtr<Task>` references in
the global `TaskMgr`'s `TaskTimeline` hold stale pointers to destroyed Task
objects. When `TaskTimeline::Poll` dereferences these, SIGSEGV. The three-phase
`DeleteObjects` (Phase 0: nullify refs, Phase 1: destroy, Phase 2: defer free)
was designed to fix this, but the current Phase 0 implementation uses
`ReplaceRefs(nullptr)` which triggers Replace callbacks — including `delete this`.

## The Three Analyses

| # | Source | Core claim | Verdict |
|---|--------|-----------|---------|
| Review | Opus subagent | Multi-pass ReplaceRefs Phase 0 is strictly better than NullifyObj | **WRONG** — missed `delete this` |
| #1 | Analysis agent | `delete this` in Replace makes ReplaceRefs Phase 0 crash; use NullifyObj Phase 0 | **CORRECT** |
| #2 | Analysis agent | Multi-pass targets wrong crash; mSubDirs.clear() happens before Phase 0 | **PARTIALLY CORRECT** — timing concern is real but each nested dir runs its own Phase 0 |

## Cross-Validation: Claim by Claim

### CLAIM: "Multi-pass ReplaceRefs Phase 0 is safe" (Review)

**VERDICT: WRONG.**

The review says "Replace callbacks are finite side effects — a second pass
catches them." This is true for *re-adds*, but catastrophically wrong for
*self-deletion*. Four classes execute `delete this` in their Replace callback
when called with nullptr:

| Class | Member | Replace trigger | Source |
|-------|--------|----------------|--------|
| MessageTask | mObj (ObjOwnerPtr) | `ref == &mObj && !o` | Task.cpp:107 |
| ScriptTask | mThis (ObjOwnerPtr) | `ref == &mThis && !obj` (falls through) | Task.cpp:277 |
| PropertyTask | mTarget (ObjOwnerPtr) | `target == nullptr` | FlowSetProperty.cpp:133 |
| DirLoader | mProxyDir (ObjDirPtr) | `from == &mProxyDir` | DirLoader.cpp:102 |

**The crash chain:**

1. Phase 0 calls `X->ReplaceRefs(nullptr)` on object X
2. X's ring contains `ObjOwnerPtr mObj` belonging to MessageTask Y
3. `mObj->Replace(nullptr)` → `Y->Replace(&mObj, nullptr)` → `delete this` on Y
4. Y's memory is freed immediately (not deferred — `delete this` calls `operator delete`)
5. Phase 0 continues iterating `todo` — reaches Y's entry
6. `Y->ReplaceRefs(nullptr)` reads freed memory → **USE-AFTER-FREE**
7. Phase 1 calls `Y->~Object()` on freed memory → **DOUBLE-FREE**

Multi-pass amplifies this — pass 2 re-processes the freed object.

**This bug also exists in the current committed code.** Both the sync path
(`DeleteObjects` Phase 0) and the async path (`~ObjectDir` lines 63-68) call
`ReplaceRefs(nullptr)` which triggers the same `delete this` chain. It hasn't
crashed yet because the specific scenario (MessageTask in the same dir as its
target) may not have been exercised, or the freed memory retained valid-looking
data by chance.

### CLAIM: "NullifyObj Phase 0 is the correct fix" (Analysis #1)

**VERDICT: CORRECT.**

NullifyObj is purely mechanical: it nulls `mObject` and self-loops `next`/`prev`
with zero callbacks. No Replace fires, no `delete this`, no side effects.
At Phase 0 time, all memory is still valid — no freed-memory reads.

The ring walk is safe:

```
ObjRef *sentinel = &obj->mRefs;
ObjRef *cur = sentinel->next;
while (cur != sentinel) {
    ObjRef *nxt = cur->next;   // save before self-loop
    cur->NullifyObj();          // nulls mObject, self-loops
    cur = nxt;                  // continue with saved pointer
}
sentinel->next = sentinel;      // mark ring empty
sentinel->prev = sentinel;
```

After NullifyObj, each ref is disconnected and its `mObject` is nullptr.
Subsequent destructors (~ObjRef) on self-looped refs are no-ops.

**Critical detail**: NullifyObj does NOT currently exist in the codebase.
The commit message (2daf36863) mentions it but the virtual was never added
to Object.h. It needs to be implemented:

```cpp
// On ObjRef (base):
virtual void NullifyObj() { next = this; prev = this; }

// On ObjRefConcrete<T>:
void NullifyObj() override { mObject = nullptr; ObjRef::NullifyObj(); }
```

### CLAIM: "Phase 0 targets the wrong crash — mSubDirs.clear() runs first" (Analysis #2)

**VERDICT: PARTIALLY CORRECT.**

The destruction order in `~ObjectDir`:

```
sDeleteObjectsDepth++          // line 52
mSubDirs.clear()               // line 54 — triggers nested ~ObjectDir
delete mLoader                 // line 55
DeleteObjects()                // line 71 (or async at 63-69)
sDeleteObjectsDepth--          // line 92
FlushDeferredFrees()           // line 94 (if outermost)
```

Analysis #2 is right that `mSubDirs.clear()` runs before the parent's
`DeleteObjects()`. But each subdir's `~ObjectDir` runs its OWN
`DeleteObjects()` with its own Phase 0/1/2 cycle. So the subdir's objects
DO get Phase 0 protection — from the subdir's `DeleteObjects`, not the
parent's.

The real concern is **cross-dir references between sibling subdirs**:

1. Parent has subdirs A and B
2. A's `~ObjectDir` → `DeleteObjects` → Phase 0 → Phase 1 (A's objects destroyed)
3. B's `~ObjectDir` → `DeleteObjects` → Phase 0 → walks B's objects' rings

If A had an `ObjPtrVec<T>` pointing to B's objects, A's Phase 1 destroys
the vector buffer. But `~ObjRef` (base destructor, always runs) properly
unlinks each Node from B's ring BEFORE the buffer is deallocated. So B's
rings remain intact when B's Phase 0 runs.

**Conclusion**: The timing concern is real but handled correctly by per-dir
Phase 0 + `~ObjRef` unlinking.

### CLAIM: "depth==1 guard is correct" (Review, Analysis #2)

**VERDICT: WRONG — the guard is unnecessary and harmful.**

The comment says: "At inner levels, outer Phase 1 may have freed ObjPtrVec
vector buffers." This is incorrect because:

1. Each dir runs Phase 0 → Phase 1 → Phase 2 atomically within its own
   `DeleteObjects()`
2. Phase 0 runs BEFORE Phase 1 at every depth
3. `~ObjRef` (unguarded by cascade) properly unlinks Nodes from sibling
   dir rings before buffer deallocation
4. Sibling dirs' rings are intact when their Phase 0 runs

With NullifyObj (no callbacks), there is no scenario where Phase 0 at any
depth encounters freed memory. Remove the guard.

### CLAIM: "LiveTasks tracking is needed" (Working tree)

**VERDICT: UNNECESSARY with proper NullifyObj Phase 0.**

`TaskTimeline::TaskInfo::mTask` is `ObjPtr<Task>` (Task.h:97) — it IS in
the Task's ref ring. NullifyObj Phase 0 would set its `mObject` to nullptr.
When `TaskTimeline::Poll` checks `(*it).mTask` (via ObjPtr's operator bool),
it sees nullptr and skips. No stale dereference, no need for `IsLive()`.

The `LiveTasks` unordered_set also has a theoretical ABA problem: if a new
Task is allocated at the same address as a freed one, `IsLive` returns true
for the wrong object. NullifyObj avoids this entirely by nullifying the
pointer at the source.

### CLAIM: "ObjDirPtr mObject re-check should be kept" (All analyses)

**VERDICT: CORRECT — keep as defense-in-depth.**

The `if (mObject && !mObject->HasDirPtrs())` check at Dir.h:101 is a
zero-cost guard against any future code path that nullifies `mObject`
between the outer `if (mObject)` and this dereference. All three analyses
agree.

### CLAIM: "TaskMgr::Start cascade guard should be kept" (All analyses)

**VERDICT: CORRECT — keep as defense-in-depth.**

Tasks created during cascade reference doomed objects. Deleting immediately
is correct behavior. All three analyses agree.

## The Correct Fix: NullifyObj-Based Phase 0

### Why it works

| Property | ReplaceRefs Phase 0 | NullifyObj Phase 0 |
|----------|--------------------|--------------------|
| Triggers Replace callbacks | Yes → `delete this` | No |
| Reads freed memory | No (Phase 0 time) | No (Phase 0 time) |
| Platform-dependent | No | No |
| ASAN clean | Yes (but crashes from `delete this`) | Yes |
| Multi-pass needed | N/A (broken) | No (idempotent, no side effects) |
| Re-added refs possible | Yes (Replace callbacks) | No (no callbacks) |
| Complexity | Uses existing ReplaceRefs | New NullifyObj virtual |

### What happens to `delete this` Tasks?

After NullifyObj Phase 0:
- MessageTask's `mObj` has `mObject = nullptr`. Its Replace callback never fires.
- Phase 1 calls `obj->~Object()` → `~MessageTask()` → releases `mMsg`. Clean.
- Phase 2 defers the free. Clean.

The `delete this` in Replace was the original Xbox mechanism for task cleanup
when individual objects were deleted. Under the three-phase approach, Phase 1
handles all destruction — Replace-triggered `delete this` is unnecessary and
harmful.

### Implementation plan

**Add NullifyObj virtual** (Object.h):
```cpp
// ObjRef base:
#ifdef HX_NATIVE
virtual void NullifyObj() { next = this; prev = this; }
#endif

// ObjRefConcrete<T>:
#ifdef HX_NATIVE
void NullifyObj() override { mObject = nullptr; ObjRef::NullifyObj(); }
#endif
```

**Add NullifyAllRefs method** (Object.h/cpp):
```cpp
#ifdef HX_NATIVE
void Hmx::Object::NullifyAllRefs() {
    ObjRef *sentinel = &mRefs;
    ObjRef *cur = sentinel->next;
    while (cur != sentinel) {
        ObjRef *nxt = cur->next;
        cur->NullifyObj();
        cur = nxt;
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}
#endif
```

**Replace Phase 0 in DeleteObjects** (Dir.cpp):
```cpp
// Phase 0: nullify all ref rings — NO callbacks, NO delete-this
for (auto &[block, obj] : todo)
    obj->NullifyAllRefs();
```
Remove the `sDeleteObjectsDepth == 1` guard.

**Fix async path** (Dir.cpp ~ObjectDir):
```cpp
if (TheLoadMgr.AsyncUnload()) {
    for (ObjDirItr<Hmx::Object> it(this, false); it != nullptr; ++it) {
        if (it != this)
            ((Hmx::Object *)it)->NullifyAllRefs();
    }
    new DirUnloader(this);
}
```

**Remove from working tree:**
- `LiveTasks` set, `Task::IsLive()`, Task constructor/destructor changes (Task.cpp/h)
- `ReplaceNonDirPtrRefs()` (Object.cpp/h) — not needed with NullifyObj
- `sDeleteObjectsDepth == 1` guard (Dir.cpp)

**Keep from working tree:**
- TaskMgr::Start cascade guard (defense-in-depth)
- TaskMgr::QueueTaskDelete cascade guard (defense-in-depth)
- ObjDirPtr `mObject &&` re-check (Dir.h, defense-in-depth)
- `~Object` cascade skip for ReplaceRefs (Phase 0 handles refs)

### Minor concern: DirPtrRefCounts stale entries

After NullifyObj on an ObjDirPtr, `mObject` becomes nullptr but
`DirPtrRefCounts` is not decremented (the ObjDirPtr's operator= never runs).
When `~ObjDirPtr` later runs during Phase 1, it checks `if (mObject)` → false
→ skip, so the count stays stale. The target object is being destroyed anyway,
so nobody checks `HasDirPtrs` on it. However, if address reuse occurs, a new
object could inherit a stale count. Options:

1. **Ignore** — the count only matters during `ObjDirPtr::operator=`, and
   new objects start at count 0 via `DirPtrRefCounts()` default behavior
   (std::unordered_map returns 0 for missing keys; the stale entry is for the
   OLD pointer, not the new object at the same address... actually it IS
   keyed by address, so reuse IS a problem)
2. **Clear entries in Phase 2** — when `DeferFree(block)` is called, also
   erase `DirPtrRefCounts[(const void*)obj]`
3. **Override NullifyObj on ObjDirPtr** to decrement the count

Option 2 is simplest. Add to DeleteObjects Phase 2:
```cpp
for (auto &[block, obj] : todo) {
    DirPtrRefCounts().erase((const void*)obj);
    DeferFree(block);
}
```

## Summary

| Component | Action | Status |
|-----------|--------|--------|
| NullifyObj virtual | **Add** to ObjRef + ObjRefConcrete | New infrastructure |
| NullifyAllRefs method | **Add** to Hmx::Object | New infrastructure |
| Phase 0 in DeleteObjects | **Replace** ReplaceRefs with NullifyAllRefs | Fixes `delete this` bug |
| Async path Phase 0 | **Replace** ReplaceRefs with NullifyAllRefs | Fixes same bug |
| depth==1 guard | **Remove** | Unnecessary with NullifyObj |
| LiveTasks / IsLive | **Remove** | Unnecessary with NullifyObj |
| ReplaceNonDirPtrRefs | **Remove** | Unnecessary with NullifyObj |
| TaskMgr::Start guard | **Keep** | Defense-in-depth |
| QueueTaskDelete guard | **Keep** | Defense-in-depth |
| ObjDirPtr mObject re-check | **Keep** | Defense-in-depth |
| ~Object cascade skip | **Keep** | Phase 0 handles refs |
| DirPtrRefCounts cleanup | **Add** in Phase 2 | Prevents stale entries |
