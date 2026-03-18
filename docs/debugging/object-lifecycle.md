# Object Lifecycle & Ring Debugging

Deep reference for debugging ObjRef ring corruption, object destruction cascades, and related memory issues in the native port.

**Parent guide**: [Native port debugging](native.md)

---

## ObjRef Ring System

Every `Hmx::Object` has an `ObjRef mRefs` sentinel. When an `ObjPtr`, `ObjDirPtr`, or `ObjPtrVec::Node` starts pointing to an object, it calls `AddRef` to link itself into that object's `mRefs` ring. When it stops pointing, it calls `Release` to unlink.

```
sentinel ←→ refA ←→ refB ←→ sentinel
```

`ReplaceRefs(newObj)` walks the ring and calls `Replace(newObj)` on each ref, redirecting them from the old object to the new one.

### Ring Corruption Symptoms

| Symptom | Root Cause |
|---------|-----------|
| SIGSEGV at 0x28 during `ReplaceList` | Freed ObjRef still linked — null vtable |
| SIGSEGV at 0xfffffff8 | Freed ObjRef still linked — memory reused, garbage vtable |
| Infinite hang in `~ObjectDir → mSubDirs.clear()` | Self-looping ObjRef node (next = prev = this) |
| ASan `heap-use-after-free` in destructor | Double-delete: parent + `DeleteObjects` both delete children |

### Debugging Techniques

Add targeted logging to `ReplaceList` in `Object.cpp`:

```cpp
while (next != this) {
    ObjRef *cur = next;
    fprintf(stderr, "REPLLIST: cur=%p vtable=%p obj=%p\n",
        (void*)cur, *(void**)cur, (void*)obj);
    cur->Replace(obj);
}
```

Check for self-loops (indicates double-AddRef corruption):
```cpp
for (ObjRef *it = mRefs.next; it != &mRefs; it = it->next) {
    if (it->next == it) {
        fprintf(stderr, "SELF-LOOP detected: ref=%p\n", (void*)it);
        break;
    }
}
```

Use GDB hardware watchpoints to catch the moment a ring pointer is corrupted:
```
(gdb) watch -l obj->mRefs.next
```

### Native-Only Ring Infrastructure

The native port adds infrastructure to handle behavioral differences from Xbox. **Do not remove these** — they solve real problems independent of any specific bug fix.

| Component | Purpose | Files |
|-----------|---------|-------|
| `SuppressEraseScope` | Prevents `ObjPtrVec::erase` during ring walks (would shift vector elements, invalidating prev/next pointers) | `Object.h`, `Object.cpp` |
| `gDeferredPurges` | Queues null-entry cleanup for after the walk | `Object.h`, `Object.cpp`, `ObjPtr_p.h` |
| `DirPtrRefCounts` | O(1) `HasDirPtrs()` check (replaces O(n) ring walk) | `Dir.h`, `Dir.cpp` |
| Vtable null-check in `ReplaceList` | Defense-in-depth against freed refs with zeroed vtable | `Object.cpp` |

---

## ObjectDir Destruction Order

When an `ObjectDir` is destroyed (e.g. `UIPanel::Unload`):

```
~PanelDir → ~RndDir → ~ObjectDir
  1. mSubDirs.clear()        — ObjDirPtr destructors → may delete subdirs
  2. delete mLoader
  3. DeleteObjects()          — ObjDirItr walks hash table, deletes each object
  4. DeleteSubDirs()          — iterates mSubDirs (already empty from step 1)
```

**Step 1** can trigger cascading deletes: `ObjDirPtr::operator=(nullptr)` checks `HasDirPtrs()`, and if the subdir has no remaining DirPtr refs, deletes it — triggering its own `~ObjectDir` cascade.

**Step 3** deletes objects in hash table iteration order. If an object's destructor also deletes other objects (e.g. `FlowNode::~FlowNode` deletes child FlowNodes), those children are also in the hash table. `RemoveFromDir()` nulls the hash entry, and `ObjDirItr` skips nulls — but ordering must be correct.

### Double-Ownership Pattern

Watch for classes that assume they own their members AND are in an ObjectDir:

```
ObjectDir::DeleteObjects()  → deletes parent FlowNode
FlowNode::~FlowNode()      → deletes child FlowNodes (also in hash table)
ObjectDir::DeleteObjects()  → encounters already-freed children → USE-AFTER-FREE
```

### ObjDirPtr Delete-During-Cascade

`ObjDirPtr::operator=(nullptr)` in `Dir.h` can `delete mObject` when `HasDirPtrs()` returns false. This happens during:
- `~ObjDirPtr()` (destructor calls `*this = nullptr`)
- `mSubDirs.clear()` (vector destructor calls each element's destructor)
- `operator=(newDir)` (releases old target before assigning new)

The delete triggers a full destructor cascade. If the deleted ObjectDir has member ObjRefs in other objects' rings, those refs must be properly unlinked before their memory is freed.

---

## ASan vs Normal Allocator — Ring Implications

Ring bugs often behave differently under ASan vs a normal run:

| Scenario | Normal (glibc) | ASan |
|----------|---------------|------|
| Freed ObjRef memory | Zeroed → null vtable → SIGSEGV at 0x28 | Quarantined → vtable preserved → no crash |
| Double-free of ring node | May corrupt heap silently | Immediate `heap-use-after-free` report |
| Stale ring pointer to reused memory | Random crash or corruption | Immediate report with alloc/free stacks |

**Key insight**: If a ring bug crashes *without* ASan but NOT *with* ASan, the bug depends on allocator zeroing behavior. The vtable is null under glibc but preserved under ASan's quarantine.

---

## Case Studies

### ObjDirPtr Double-AddRef (2026-03-18)

**Symptom**: Three symptoms from one root cause — SIGSEGV at 0x28, SIGSEGV at 0xfffffff8, destructor cascade hang.

**Root cause**: Decomp error in `ObjDirPtr(C*)` constructor called `dir->AddRef(this)` in the body, but the base class `ObjRefConcrete(dir)` already called it. The double-AddRef created a self-loop (`next = this, prev = this`), making `Release` a no-op. Ring sentinels permanently held dangling pointers to freed ObjDirPtrs.

**Diagnosis path**: Normal run crashed at 0x28. ASan showed zero errors (quarantine masked it). Adding vtable null-check confirmed freed refs in ring. Backtrace of immediate deletes showed `UIPanel::Unload` cascade. Removing the duplicate `AddRef` fixed all three symptoms and improved PPC match to 100%.

**Lesson**: When ASan doesn't catch a crash that happens without ASan, the bug depends on allocator zeroing behavior. Check for stale pointers where the allocator zeroes the vtable.

Full writeup: `docs/sessions/2026-03-18-venue-merge-crash-ring-corruption.md`

### FlowNode Double-Free (2026-03-18)

**Symptom**: ASan `heap-use-after-free` in `FlowNode::~FlowNode()` at `delete cur`.

**Root cause**: `FlowNode::~FlowNode()` recursively deletes child FlowNodes via `mChildNodes`. But all FlowNodes are also in the ObjectDir hash table. `ObjectDir::DeleteObjects()` iterates the hash table and deletes every object. When it deletes a parent FlowNode, the destructor cascade deletes children. Then `DeleteObjects` encounters those already-freed children.

**Diagnosis**: ASan freed-by stack showed `FlowIf::operator delete` called from `FlowNode::~FlowNode` (parent deleting child). The crash-site stack showed the same `FlowNode::~FlowNode` at a different level trying to access the freed child.

**Lesson**: `RemoveFromDir()` must null the hash entry before the child is freed, and `ObjDirItr` must skip nulls.

### ObjDirItr Infinite Loop (2026-03-17)

**Symptom**: Game hung after ~1500 frames during song gameplay.

**Diagnosis**: CDP debugger break showed `ObjDirItr<RndLight>::Advance()` → `WgpuRnd::WriteSceneUniforms()` — recursive dir iterator on venue WorldDir every frame.

**Fix**: Removed `ObjDirItr<RndLight>(venueDir, true)` scans from `WriteSceneUniforms()`. Environment light lists + fallback defaults are sufficient.

---

## Unit Tests

Object lifetime tests in `native/tests/test_object_lifetime.cpp`:

| Test | What it covers |
|------|---------------|
| `ReplaceListLiveWalkDoesNotCrash` | Ring walk with N refs, all redirect correctly |
| `DirPtrRefCountsConsistentAfterMerge` | DirPtrRefCounts survives MergeDirs |
| `ObjDirPtrCascadeDeleteDoesNotDoubleFree` | Nested subdir cascade with cross-references |
| `RemoveSubDirReleasesDirPtrRef` | Subdir removal + owner deletion |
| `ReplaceRefsWithSelfDeletingObjDirPtr` | ObjDirPtr that triggers self-delete during Replace |
| `DeletingFlowChildRemovesFromParent` | Flow child deletion updates parent's ObjPtrVec |
| `MergeDirsNameCollisionLeavesOnlyLivePointers` | Name collision during merge redirects refs |

Run with timeout (some may hang if ring bugs are present):
```bash
timeout 15 native/build/milo-tests --gtest_filter='ObjectLifetimeTest.*'
```
