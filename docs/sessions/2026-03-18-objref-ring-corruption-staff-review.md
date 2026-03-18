# ObjRef Ring Corruption — Staff Engineer Review

**Date**: 2026-03-18
**Status**: Fixes applied, follow-up items identified
**Related**: `docs/sessions/2026-03-18-flownode-double-free-analysis.md`

## Context

Two independent analyses investigated a SIGSEGV crash in `ObjRefConcrete<FlowNode>::~ObjRefConcrete()` during venue merge (song loading). The crash was a dangling pointer dereference — `mObject->Release(this)` on freed `FlowWhile` memory during cascading `ObjectDir` destruction.

**Analysis 1** (session doc): Deep root-cause trace of CopyRef-during-shift mechanism. Recommended preventing merge-created duplicates in `ReplaceNode`.

**Analysis 2** (agent): Found three ASAN-verified bugs, shipped practical defensive fixes. Verified 3000+ frames with zero ASAN errors.

This document consolidates both analyses and evaluates the fixes that landed.

## Three Bugs Found

### Bug 1: FileGetPath strcpy self-overlap (File.cpp)

`FileGetPathBuf(file, path)` calls `strcpy(path, file)` where `path` and `file` can alias the same static buffer. `FileGetPath()` passes `static_path` as `file` via earlier call, then passes it again as `path`.

**Fix**: `if (path != file) strcpy(path, file);` — one-line guard.

**Verdict**: Correct, clean. Original source likely had this guard.

### Bug 2: ProfileMgr::GetAlternateOutfit buffer underflow (ProfileMgr.cpp)

Hand-unrolled string copy loop had `dst = buf - 1` reset every iteration — classic decomp transcription error. Only `buf[0]` was ever written, so `bufLen` was 1, and `buf[bufLen - 2]` indexed `buf[-1]` (stack underflow).

**Fix**: Replaced manual loops with `strcpy(buf, outfitChar.Str())` and `strlen()`. This is what the original source was — the hand-unrolled version was a bad decomp, not the compiler's output.

**Verdict**: Correct decomp fix, not just a native port fix.

### Bug 3: ObjRef ring corruption during cascading directory destruction

The core issue. Multiple sub-fixes described below.

## Root Cause: Why the Ring Corrupts

### Destruction Order (Flow's Multiple Inheritance)

`Flow` inherits from both `ObjectDir` and `FlowNode` (via `FlowQueueable`):

```
Flow -> FlowQueueable -> FlowNode -> virtual Hmx::Object
     -> ObjectDir                  -> virtual Hmx::Object
     -> FlowLabelProvider
     -> RndPollable
```

C++ destructor order (reverse declaration, virtual bases last):

1. `Flow::~Flow()` — deactivates, cancels commands
2. `RndPollable::~RndPollable()`
3. `FlowLabelProvider::~FlowLabelProvider()`
4. **`ObjectDir::~ObjectDir()`** — calls `mSubDirs.clear()` then `DeleteObjects()`
5. `FlowQueueable::~FlowQueueable()`
6. **`FlowNode::~FlowNode()`** — while loop over `mChildNodes`, then `~ObjPtrVec`
7. `Hmx::Object::~Object()` — `RemoveFromDir` + `ReplaceRefs`

**Step 4 runs before step 6.** `DeleteObjects()` frees all objects in the Flow's hash table. By step 6, `mChildNodes` may still contain entries pointing to objects that were freed in step 4.

### Merge Creates Duplicate ObjPtrVec Entries

`MergeObject` (Utl.cpp) calls `o1->ReplaceRefs(o2)` which redirects ALL refs from o1 to o2. If an `ObjPtrVec` had refs to BOTH o1 and o2, after merge both Nodes point to o2. `ObjPtrVec` has zero duplicate checking in `push_back`, `insert`, `Load`, or `ReplaceNode`.

### CopyRef During Vector Shift Creates Orphaned Nodes

When a duplicated object is later destroyed with immediate vector erase during `ReplaceRefs`:

1. `ReplaceRefs(nullptr)` splices the dying object's ring into temporary `other`, clears `mRefs`
2. `ReplaceList` processes the first duplicate Node:
   - `Replace(nullptr)` -> `SetObj(nullptr)` -> `Release` from `other` ring
   - `ReplaceNode` -> immediate **erase** from vector
3. The erase **shifts** subsequent elements via `CopyRef` -> `SetObjConcrete(newObj)`
4. If the second duplicate is adjacent, the shift copies it to the erased position:
   - `SetObjConcrete(o2)` on the destination: `mObject = nullptr` (from step 2) -> skip Release -> `mObject = o2` -> **`AddRef` to o2's cleared `mRefs`** (NOT the `other` ring)
5. `CopyRef` on the source position: `SetObjConcrete(next_element)` -> **Release from `other` ring** (unlinks the second duplicate)
6. `ReplaceList` can't find the second Node — it was removed from `other` by CopyRef's Release, and re-added to `mRefs` which nobody walks
7. The Node survives with `mObject` pointing to the now-freed object

The `other` ring walk is fundamentally incompatible with immediate vector erase when duplicates exist, because `CopyRef` during shift performs `AddRef` back to the dying object's cleared `mRefs` ring — a ring that `ReplaceList` never visits.

### Why Xbox Was Safe

- All FlowNode children were in their Flow's own hash table (no cross-dir references from merges), so the while loop in `~FlowNode` was always a no-op
- `RemoveFromDir` nulled hash entries, and `ObjDirItr` skipped nulls — clean single-owner semantics
- The allocator didn't zero freed memory, so stale vtable pointers didn't immediately crash

## Fixes Applied (Working Tree)

### Fix 3a: SetObjConcrete stale ref guard (ObjPtr_p.h:42-54)

```cpp
if (Hmx::Object::IsDeleting() &&
    static_cast<Hmx::Object *>(mObject) != Hmx::Object::GetDeleting()) {
    mObject = nullptr;
} else {
    mObject->Release(this);
}
```

During `~Object::ReplaceRefs(NULL)`, the dying object's ref ring may contain entries pointing to already-freed objects from prior destruction cascades. When mObject doesn't match the currently dying object, skip Release to avoid dereferencing freed memory.

**Concern**: This leaks a ring entry in the other object. If mObject points to still-alive object Y, we skip Release from Y's ring, leaving a dangling entry. In the destruction cascade this is acceptable because Y will also be destroyed shortly, but it's not safe in general. A `MILO_WARN` in debug builds would add visibility.

### Fix 3b: ReplaceNode suppressed erase (ObjPtr_p.h:232-244)

```cpp
if (!Hmx::Object::IsDeleting()) {
    erase(iterator(mNodes.begin() + (n - mNodes.data())));
}
```

During `~Object::ReplaceRefs` (sDeleting set), erasing from the vector shifts subsequent nodes, invalidating their ObjRef ring prev/next pointers. Suppress the erase; leaving a null entry is harmless — the vector will be destroyed shortly.

**Verdict**: Correct. This is the direct fix for the CopyRef-during-shift mechanism.

### Fix 3c: InDeleteObjects guard in FlowNode::~FlowNode (FlowNode.cpp:28-42)

```cpp
if (ObjectDir::InDeleteObjects()) {
    for (auto it = mChildNodes.begin(); it != mChildNodes.end(); ++it) {
        if (*it)
            it->SetObjConcrete(nullptr);
    }
    return;
}
```

During `ObjectDir` teardown, `FlowNode` should not independently manage child lifetimes. Null out all ObjPtrVec nodes so the vector destructor's `~ObjRefConcrete` doesn't dereference freed pointers. Children that are still alive get their Release called (removing this parent's ref from their ring). Children already freed had their ReplaceRefs null our nodes already.

**Verdict**: Architecturally correct. This is the most important fix.

### Fix 3d: sDeleteObjectsDepth counter (Dir.cpp, Dir.h)

Tracks nesting depth of `DeleteObjects()` and `~ObjectDir()`. Incremented in BOTH `~ObjectDir()` (covers `mSubDirs.clear()`) and `DeleteObjects()` itself.

**Note**: During normal destruction, `~ObjectDir` calls `DeleteObjects()`, so the counter hits 2 at peak. `InDeleteObjects()` checks `> 0`, so this means the guard is active for the ENTIRE `~ObjectDir` destructor — broader than strictly needed but correct for the FlowNode case.

### Fix 3e: ObjDirPtr::Replace null guard (Dir.h:67-72)

```cpp
if (!ObjRefConcrete<C>::mObject) {
    ObjRef::Release(this);
    return;
}
```

When mObject is already null, the old code silently returned without unlinking from the ring. This caused `ReplaceList` to loop infinitely because `Replace` returned without advancing `next`. The fix calls `Release(this)` to unlink.

**Verdict**: Correct. Fixes a second bug (infinite loop) discovered during the same investigation.

### Fix 3f: Removed SuppressEraseScope infrastructure (Object.h, Object.cpp)

The previous fix attempt used a `SuppressEraseScope` RAII guard, `gSuppressRefErase` global, `gDeferredPurges` vector, and `PurgeNulls()` method to defer vector cleanup. This has been replaced by the simpler `IsDeleting()` check in `ReplaceNode`.

**Verdict**: Good. Simpler, less global state, fewer moving parts.

### Fix 3g: Simplified ReplaceList (Object.cpp:22-31)

```cpp
while (next != this) {
    ObjRef *cur = next;
    next->Replace(obj);
    if (cur == next) {
        MILO_FAIL("ReplaceList stuck in infinite loop");
        break;
    }
}
```

Replaced the previous version that had freed-vtable detection (`if (!*(void **)cur)`) and deferred purge coordination. The infinite-loop detection (`cur == next`) catches bugs where `Replace` fails to unlink.

**Verdict**: Correct and much simpler. The freed-vtable case is now handled upstream by fixes 3a and 3b.

## Assessment

### Strengths

- **ASAN-verified**: 3000+ frames with `halt_on_error=1`, zero errors. The fixes demonstrably work.
- **Simpler than predecessor**: The SuppressEraseScope/DeferredPurge infrastructure was complex and had its own edge cases (vector destroyed before outermost ReplaceList exits). The new approach is easier to reason about.
- **Defense-in-depth**: Three independent guards (stale ref check, suppressed erase, InDeleteObjects) mean any single fix failing doesn't cause a crash.
- **Root cause understanding**: The session doc's trace through the CopyRef-during-shift mechanism is genuinely deep and correct.

### Weaknesses

- **The session doc's recommended fix was not implemented**: Deduplicating in `ReplaceNode` at merge time would prevent the root cause (duplicate entries). The current fixes handle the destruction path but duplicates can still be created. If anything iterates an ObjPtrVec with duplicates outside of destruction (e.g., `FlowNode::Activate`), it would process the same child twice.
- **SetObjConcrete stale ref check is the weakest link**: Silently drops ring membership from objects that may still be alive. Works in the destruction cascade because those objects are about to die, but could silently corrupt state if destruction order changes.
- **No visibility when defensive paths fire**: The stale ref path, the suppressed erase path, and the InDeleteObjects path all execute silently. Adding `MILO_WARN` in debug builds would help catch unexpected triggers.

### What's Missing

1. **Deduplicate check in `ReplaceNode`** (session doc's recommendation): When a non-null replacement creates a duplicate (the new object already exists in the vector), erase the redundant node. This prevents the root cause rather than just handling the symptom. Should be native-only and follow-up work.

2. **Debug visibility**: Add `MILO_WARN` to the `SetObjConcrete` stale ref path so it's visible when firing outside the expected cascade.

3. **Test coverage**: The ASAN run covers the YMCA song loading flow. Other song flows with different venue merges may exercise different destruction orderings. Broader coverage would increase confidence.

## Recommendation

**Ship these fixes.** They're correct for the destruction scenario, ASAN-verified, and significantly simpler than what they replaced. The two analyses are complementary — the session doc explains *why*, the agent fixes *what*.

Follow-up items (not blocking):
- Add deduplicate check in `ReplaceNode` to prevent merge-created duplicates at source
- Add `MILO_WARN` to defensive paths for debug visibility
- Test additional song loading flows for broader coverage

## Key Files

| File | What changed |
|------|-------------|
| `src/system/os/File.cpp:135-139` | FileGetPath strcpy self-overlap guard |
| `src/lazer/meta_ham/ProfileMgr.cpp:984-994` | GetAlternateOutfit decomp fix |
| `src/system/obj/ObjPtr_p.h:40-63` | SetObjConcrete stale ref guard |
| `src/system/obj/ObjPtr_p.h:228-248` | ReplaceNode suppressed erase |
| `src/system/flow/FlowNode.cpp:23-48` | InDeleteObjects guard in destructor |
| `src/system/obj/Dir.cpp:48-74` | sDeleteObjectsDepth in ~ObjectDir |
| `src/system/obj/Dir.cpp:689-701` | sDeleteObjectsDepth in DeleteObjects |
| `src/system/obj/Dir.h:457-462` | InDeleteObjects() / sDeleteObjectsDepth decl |
| `src/system/obj/Dir.h:64-77` | ObjDirPtr::Replace null unlink fix |
| `src/system/obj/Object.cpp:20-31` | Simplified ReplaceList |
| `src/system/obj/Object.h` | Removed SuppressEraseScope, added IsDeleting/GetDeleting |

## Reproduction

```bash
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" \
  MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 \
  MILO_MAX_FRAMES=3000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  native/build/dc3-native
```
