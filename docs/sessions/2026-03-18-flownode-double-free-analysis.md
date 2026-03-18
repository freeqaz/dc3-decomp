# FlowNode Double-Free During ObjectDir::DeleteObjects

**Date**: 2026-03-18
**Status**: Analysis complete, fix recommended
**Related**: `docs/sessions/2026-03-18-venue-merge-crash-ring-corruption.md`

## Crash Summary

SIGSEGV / ASan heap-use-after-free in `ObjRefConcrete<FlowNode>::~ObjRefConcrete()` at ObjPtr_p.h:35 (`mObject->Release(this)`) during `UIPanel::Unload` teardown. A `FlowWhile` (344 bytes) is freed by a nested `DeleteObjects()`, then a sibling `Flow`'s `~ObjPtrVec<FlowNode>` destructor tries to Release from the dead object's ObjRef ring.

## ASan Trace (current reproduction)

**Crash site** (read of freed memory):
```
#0  ObjRefConcrete<FlowNode>::~ObjRefConcrete()   ObjPtr_p.h:35
#1  ObjPtrVec<FlowNode>::Node::~Node()            Object.h:243
#2-#6  std::vector destruction chain
#7  ObjPtrVec<FlowNode>::~ObjPtrVec()             ObjPtr_p.h:223
#8  FlowNode::~FlowNode()                         FlowNode.cpp:39
#9  FlowQueueable::~FlowQueueable()
#10 Flow::~Flow()                                  Flow.cpp:53
#13 ObjectDir::DeleteObjects()                     Dir.cpp:708
#14 ObjectDir::~ObjectDir()                        Dir.cpp:59
#15-#17 Flow::~Flow chain (parent)
#18-#19 ObjectDir::DeleteObjects/~ObjectDir (grandparent)
```

**Freed-by** (three levels of nested DeleteObjects):
```
#3  FlowWhile::~FlowWhile()
#4  ObjectDir::DeleteObjects()       — sibling Flow's hash table
#5  ObjectDir::~ObjectDir()
#6  Flow::~Flow()                    — sibling Flow
#9  ObjectDir::DeleteObjects()       — parent dir
```

**Key observation**: The crash is in `~ObjPtrVec` (member destruction), NOT in the explicit `while (!mChildNodes.empty()) { delete cur; }` loop. A Node survives with a non-null `mObject` pointing to freed memory.

## Root Cause Analysis

### 1. Double-Ownership Design Flaw

`Flow` inherits from both `ObjectDir` and `FlowNode` (via `FlowQueueable`):

```cpp
class Flow : public FlowQueueable,   // -> FlowNode -> virtual Hmx::Object
             public ObjectDir,        // -> virtual Hmx::Object
             public FlowLabelProvider,
             public RndPollable { ... };
```

C++ destructor order for multiple inheritance (reverse declaration, virtual bases last):

1. `Flow::~Flow()` — deactivates, cancels commands
2. `RndPollable::~RndPollable()`
3. `FlowLabelProvider::~FlowLabelProvider()`
4. **`ObjectDir::~ObjectDir()`** — calls `mSubDirs.clear()` then `DeleteObjects()`
5. `FlowQueueable::~FlowQueueable()`
6. **`FlowNode::~FlowNode()`** — while loop over `mChildNodes`, then member destructors (`~ObjPtrVec`)
7. `Hmx::Object::~Object()` — `RemoveFromDir` + `ReplaceRefs`

**Step 4 runs before step 6.** `DeleteObjects()` frees all objects in the Flow's hash table. Each freed object's `~Object::ReplaceRefs(nullptr)` walks its ref ring and nulls/erases Nodes in `mChildNodes`. By step 6, `mChildNodes` should only contain objects NOT in this Flow's hash table.

On Xbox, all FlowNode children were registered in the Flow's own hash table, so by step 6, `mChildNodes` was always empty — the while loop was a no-op. On native, cross-dir references from merges mean `mChildNodes` can still have entries pointing to objects owned by other dirs.

### 2. Merge Creates Duplicate ObjPtrVec Entries

`MergeObject` (Utl.cpp:111) calls:
```cpp
o1->ReplaceRefs(o2);  // redirect ALL refs from o1 to o2
```

This walks o1's ref ring and calls `Replace(o2)` on each ObjRef. For an `ObjPtrVec::Node`, this calls `ReplaceNode` which does `SetObj(o2)` — changing the Node's `mObject` from o1 to o2 and relinking in o2's ring.

**If an ObjPtrVec already had a Node pointing to o2 AND another Node pointing to o1**, after the merge both Nodes point to o2. `ObjPtrVec` has **zero duplicate checking** in `push_back`, `insert`, `Load`, or `ReplaceNode` (the only guard is null prevention for `kObjListNoNull`).

### 3. How Duplicates Cause the Stale Node

When the merged-to object (o2) is later destroyed with **immediate vector erase** during `ReplaceRefs`:

1. `ReplaceRefs(nullptr)` splices o2's ring into temporary `other`, clears `mRefs`
2. `ReplaceList` processes the first duplicate Node:
   - `Replace(nullptr)` → `SetObj(nullptr)` → `Release` from `other` ring
   - `ReplaceNode` → immediate **erase** from vector
3. The erase **shifts** subsequent elements via `CopyRef` → `SetObjConcrete(newObj)`
4. If the second duplicate Node is adjacent, the shift copies it to the erased position:
   - `SetObjConcrete(o2)` on the destination: `mObject = nullptr` (from step 2) → skip Release → `mObject = o2` → **`AddRef` to o2's cleared `mRefs`** (NOT the `other` ring)
5. Meanwhile, `CopyRef` on the source position: `SetObjConcrete(next_element)` → **Release from `other` ring** (unlinks the second duplicate)
6. `ReplaceList` can't find the second Node — it was removed from `other` by the CopyRef's Release, and re-added to `mRefs` which nobody walks
7. The Node survives with `mObject` pointing to the now-freed object

The `other` ring walk is fundamentally incompatible with immediate vector erase when duplicates exist, because `CopyRef` during the shift performs `AddRef` back to the dying object's cleared `mRefs` ring — a ring that the `ReplaceList` walk never visits.

### 4. The Cascade Trigger

In the actual crash scenario, the destruction cascade is:

1. PanelDir → `mSubDirs.clear()` → destroys a child dir
2. Child dir → `DeleteObjects` → deletes Flow_A (a sibling of Flow_B)
3. Flow_A → `ObjectDir::~ObjectDir` → `DeleteObjects` → deletes FlowWhile
4. FlowWhile's `ReplaceRefs(nullptr)` tries to clean up Nodes in Flow_B's `mChildNodes`
5. If duplicates exist (from prior merge), the orphaned Node mechanism above leaves a stale pointer
6. Later, Flow_B is destroyed → `FlowNode::~FlowNode` → `~ObjPtrVec` → `~ObjRefConcrete` reads freed FlowWhile → crash

## Why Xbox Was Safe

On Xbox:
- The SuppressEraseScope infrastructure deferred vector erase during `ReplaceList`, preventing the CopyRef-during-shift that causes orphaning
- All FlowNode children were in their Flow's own hash table (no cross-dir references from merges), so the while loop in `~FlowNode` was always a no-op
- `RemoveFromDir` nulled hash entries, and `ObjDirItr` skipped nulls — clean single-owner semantics

## Attempted Fixes (working tree)

Several patches have been applied concurrently:

1. **`IsDeleting` guard in `ReplaceNode`** — tombstone (don't erase) during `ReplaceRefs`. Prevents the CopyRef-during-shift orphaning.
2. **Stale ref detection in `ReplaceList`** — checks `GetObj() != GetDeleting()` and unlinks mismatched refs.
3. **`InDeleteObjects()` guard in `~FlowNode`** — during `ObjectDir` teardown, null all Nodes via `SetObjConcrete(nullptr)` instead of running the while loop.

These address the symptoms but patch around the deeper issue.

## Recommended Fix

### Fix the root cause: prevent merge-created duplicates

In `ObjPtrVec::ReplaceNode`, when a non-null replacement creates a duplicate (the new object already exists in the vector), erase the redundant node:

```cpp
template <class T1, class T2>
void ObjPtrVec<T1, T2>::ReplaceNode(Node *n, Hmx::Object *obj) {
    if (mListMode == kObjListOwnerControl) {
        mOwner->Replace(n, obj);
    } else {
        Hmx::Object *oldObj = n->SetObj(obj);
        if (!oldObj && mListMode == kObjListNoNull) {
            erase(iterator(mNodes.begin() + (n - mNodes.data())));
        }
#ifdef HX_NATIVE
        // Merge operations (MergeObject → o1->ReplaceRefs(o2)) redirect refs
        // from o1 to o2. If this vector had refs to BOTH o1 and o2, the
        // redirect creates a duplicate. Erase the redundant node to prevent
        // orphaned refs during later destruction (the CopyRef during vector
        // erase can AddRef back to a dying object's cleared mRefs ring,
        // escaping the ReplaceList cleanup walk).
        else if (obj && mListMode == kObjListNoNull) {
            T1 *typed = dynamic_cast<T1 *>(obj);
            if (typed) {
                for (size_t i = 0; i < mNodes.size(); i++) {
                    if (&mNodes[i] != n && mNodes[i].Obj() == typed) {
                        n->SetObj(nullptr);
                        erase(iterator(mNodes.begin() + (n - mNodes.data())));
                        break;
                    }
                }
            }
        }
#endif
    }
}
```

This eliminates the duplicate at creation time, so the orphaned-Node mechanism can never trigger.

### Keep the InDeleteObjects guard as defense-in-depth

The `InDeleteObjects()` check in `~FlowNode` is architecturally correct — it acknowledges that during `ObjectDir` teardown, `FlowNode` should not independently manage child lifetimes. Keep it as a safety net, but with the duplicate fix, it should never be needed for correctness.

### Remove the SuppressEraseScope infrastructure

With duplicates prevented at source, the `SuppressEraseScope`, `gDeferredPurges`, `PurgeNulls`, and deferred purge infrastructure are no longer needed. The simple immediate-erase path is correct when duplicates don't exist.

## Key Files

| File | What to look at |
|------|-----------------|
| `src/system/flow/Flow.h:14-17` | Flow's multiple inheritance declaration |
| `src/system/flow/FlowNode.cpp:23-42` | `~FlowNode` destructor with while loop |
| `src/system/obj/ObjPtr_p.h:212-233` | `ReplaceNode` — where to add duplicate check |
| `src/system/obj/Object.cpp:22-31` | `ReplaceList` — ring walk |
| `src/system/obj/Object.cpp:307-316` | `ReplaceRefs` — splices ring into `other`, clears `mRefs` |
| `src/system/obj/Utl.cpp:91-119` | `MergeObject` — `o1->ReplaceRefs(o2)` creates duplicates |
| `src/system/obj/Dir.cpp:676-703` | `DeleteObjects` — hash table iteration with delete |

## Reproduction

```bash
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" \
  MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 \
  MILO_MAX_FRAMES=3000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  native/build/dc3-native
```
