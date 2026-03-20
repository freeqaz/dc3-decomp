# MergeDirs Parity Fix — Session Log

**Date**: 2026-03-19
**Status**: Bug 1 fixed. Bug 2 analysis updated (dead object tracking insufficient — see below). Bug 3 fixed.
**Builds on**: [2026-03-19-merge-parity-p0.md](2026-03-19-merge-parity-p0.md)

## What We Did

### ASAN Diagnosis

Built milo-tests with AddressSanitizer (`native/build-asan`) and ran the
`VenueProxyMergeIntoWorldRoot` test. ASAN reported:

```
heap-use-after-free in ObjRef::Release(ObjRef*) at Object.h:74
  WRITE of size 8 to freed 696-byte CharClip block
```

The crash happens during `delete worldRoot` — not during the merge itself. The
cascading `~ObjectDir()` teardown (mSubDirs.clear → DeleteObjects) frees a
CharClip in one CharClipSet subdir, then a CharClip in a *sibling* CharClipSet
tries to Release from the freed clip's ring memory.

### Subagent Investigation (3 parallel agents)

Launched 3 research agents in isolated worktrees to probe different hypotheses:

| Agent | Investigation | Result |
|-------|--------------|--------|
| Agent 1 | Ring integrity after `Transitions::Load` | **0/397 failures** — ring is clean after loading |
| Agent 2 | Ring integrity after `MergeDirs`/`MergeObjectsRecurse` | **0 failures** across all tests — merge doesn't corrupt rings |
| Agent 3 | Count and log stale refs during teardown | **~1,800 stale refs** per venue — all `ObjOwnerPtr<CharClip>` in Transitions NodeVectors |

Key finding from Agent 3: the stale refs are **still linked into the dead
object's ring** (coherent prev/next chains, not self-loops). They have
0x3C-byte spacing = exactly `NodeVector` with 1 `CharGraphNode`. This means
`ReplaceRefs(nullptr)` walked the ring and called `Replace(nullptr)` but
**the mObject was not nulled** — pointing to the `RemoveNodes` memmove
interaction with ReplaceList.

### Root Cause: Two Distinct Bugs

#### Bug 1: RemoveNodes memmove during ReplaceList (FIXED)

`Transitions::Replace` unconditionally calls `RemoveNodes` after nulling a
NodeVector's clip reference. During `ReplaceList` (ring walk), this memmove
shifts the next NodeVector into the current position. ReplaceList's
force-unlink logic then strips the shifted NodeVector from the ring **without
nulling its mObject**:

1. ReplaceList processes NV_A: `Replace(nullptr)` → `SetObj(nullptr)` nulls
   mObject, unlinks NV_A from ring
2. `RemoveNodes(NV_A)` shifts NV_B into NV_A's address, fixes up ring pointers
3. ReplaceList: `cur == next` (both point to NV_A's address, now holding NV_B)
4. Force-unlink: strips NV_B into a self-loop — **mObject NOT nulled**
5. NV_B has `mObject = clipX` (valid pointer) but is unreachable from any ring
6. Later: `~CharClip → Clear()` destroys NV_B → `Release` writes to freed clipX

**Fix**: Check `gInReplaceList` in `Transitions::Replace` — same pattern used
by `ObjPtrVec::ReplaceNode`. When true, skip `RemoveNodes`. The NodeVector
stays in the buffer with `mObject = nullptr` and is safely cleaned up by
`Clear()` later.

```cpp
bool CharClip::Transitions::Replace(ObjRef *from, Hmx::Object *to) {
    NodeVector *vector = reinterpret_cast<NodeVector *>(from);
    if (!vector->clip.SetObj(to)) {
        if (!gInReplaceList)    // ← NEW: suppress during ring walk
            RemoveNodes(vector);
    }
    return true;
}
```

#### Bug 2: Cascading destruction order (FIX CHOSEN, NOT YET IMPLEMENTED)

C++ destruction order: `~CharClip()` runs before `~Object()`. During
`~CharClip()`, `Transitions::Clear()` destroys NodeVectors. Each
`~ObjRefConcrete` calls `mObject->Release(this)`. If `mObject` points to a
CharClip that was already freed by a sibling subdir's `DeleteObjects`, this
writes to freed memory.

The freed clip's `~Object()` → `ReplaceRefs(nullptr)` *should* have nulled the
ref via the ring walk. With the Bug 1 fix, the ring walk no longer corrupts
itself. But the destruction cascade involves multiple levels of
`~ObjectDir → mSubDirs.clear() → DeleteObjects`, and the ordering between
sibling subdirs can still leave refs pointing to freed clips.

**Approaches tried and rejected:**

| Approach | Why it failed |
|----------|---------------|
| Recursive pre-null in `~ObjectDir` | `ReplaceRefs(nullptr)` triggers `ObjDirPtr::Replace` callbacks that cascade-delete subdirs during iteration |
| `mAliveSentinel` check in `~ObjRefConcrete` | Reads freed memory (UB), ABI divergence (ObjRef 4 bytes larger on native) |
| Skip Release when `sDeleteObjectsDepth > 0` | Too aggressive — corrupts rings for live objects outside the subtree |
| Dead object tracking set (2026-03-20) | Correctly allows Release for live targets but exposes glibc freed-neighbor-write problem (see below) |

**Dead object tracking — tested and found insufficient (2026-03-20)**

Implemented `static std::unordered_set<void*>` in `~Object()`, checked in
`~ObjRefConcrete`. More precise than `InDeleteObjects()` — correctly allows
`Release()` for live targets and skips it for dead targets. However, `Release()`
writes `prev->next = next; next->prev = prev` — the prev/next neighbors may be
ObjRefs in objects already freed by earlier `DeleteObjects` iterations. On glibc,
writing to freed heap blocks corrupts free list metadata → "free(): chunks in
smallbin corrupted". Xbox's heap allocator doesn't overwrite freed blocks
immediately, so this is tolerated on Xbox but crashes on native.

Also hit static destruction order bug: `sDeadObjects` destroyed at program exit
before global `WebSvcMgrCurl`, whose `~Object()` then reads freed hash table
memory. Fixable by guarding with `InDeleteObjects()`, but the freed-neighbor-write
problem is fundamental to the intrusive ring design.

**Recommended approach: Two-phase DeleteObjects**

Split `delete obj` into destructor + deallocation:
1. Phase 1: Call destructors on all objects (memory stays allocated)
2. Phase 2: Free memory

During Phase 1, all `~ObjRefConcrete` Release calls write to valid memory
(objects destroyed but not yet freed). After Phase 1, all rings are clean.
Phase 2 frees the memory safely.

```cpp
void ObjectDir::DeleteObjects() {
    // Phase 1: collect + destroy (ring unlinks write to valid memory)
    std::vector<void*> blocks;
    for (ObjDirItr<Hmx::Object> it(this, false); it != nullptr; ++it) {
        if (it != this) {
            Hmx::Object *obj = it;
            size_t sz = /* class-specific size */;
            obj->~Object();  // virtual dtor — runs full chain
            blocks.push_back(obj);
        }
    }
    // Phase 2: free memory (all rings already clean)
    for (void *p : blocks)
        ::operator delete(p);
}
```

Complications: `RemoveFromDir()` in `~Object()` modifies the hash table during
Phase 1, but the iterator has already collected all objects. Custom `operator
delete` overloads (OBJ_MEM_OVERLOAD) need the correct dealloc function saved
before destruction. Not yet implemented.

### Bug 3: AddNode 32→64-bit porting bug (FIXED)

`CharClip::Transitions::AddNode()` (CharClip.cpp:122-128) had hardcoded 32-bit
offsets for the ObjRef ring pointer fixup after reallocation:

```cpp
// BEFORE (wrong on 64-bit)
ObjRef **prev = (ObjRef **)((char *)it + 4);   // offset 4 = next on 32-bit
ObjRef **next = (ObjRef **)((char *)it + 8);   // offset 8 = prev on 32-bit

// AFTER (correct on all platforms)
ObjRef *clipNext = *(ObjRef **)((char *)clipRef + sizeof(void *));
ObjRef *clipPrev = *(ObjRef **)((char *)clipRef + sizeof(void *) * 2);
```

On Xbox 360 (32-bit PPC), `sizeof(void*) = 4`, so offsets 4/8 are correct. On
64-bit native, `sizeof(void*) = 8`, so the correct offsets are 8/16. The old
code was reading from the middle of the vtable pointer and doing wild writes.

Note: `RemoveNodes()` already used `sizeof(void*)` — only `AddNode` was wrong.
Venue data uses `Transitions::Load` (rev ≥ 8), not `AddNode`, so this bug
didn't directly cause the venue crashes but would hit any runtime AddNode calls.

### Test Infrastructure Fixes

The synthetic tests had several latent bugs exposed by ASAN:

| Bug | Fix |
|-----|-----|
| `RemoveSubDir(rawPtr)` creates temp ObjDirPtr that delete-on-last-ref frees the dir | Keep staging dir alive through merge |
| `const char *nameA = obj->Name()` dangling after `delete obj` | Use `std::string` |
| `SetName(name, nullptr)` asserts non-null dir | Use staging dir pattern |
| Recursive `FindObject` expected proxy-merged objects in mSubDirs | They're only in hash table; adjusted expectations |

## Test Results (with Bug 1 fix only, sentinel temporarily applied for Bug 2)

| Test | Before | After |
|------|--------|-------|
| SyntheticNonProxyMergeFlattensContent | PASS (segfault teardown) | PASS (segfault teardown) |
| SyntheticProxyMergeAddsSubdir | PASS (segfault teardown) | PASS (segfault teardown) |
| SyntheticNameCollisionRedirectsRefs | PASS (segfault teardown) | PASS (segfault teardown) |
| SyntheticSequentialNonProxyThenProxy | PASS (segfault teardown) | PASS (segfault teardown) |
| SyntheticCrossRefsPreservedAcrossMerge | PASS (segfault teardown) | PASS (segfault teardown) |
| **VenueProxyMergeIntoWorldRoot** | **ABORT** | **PASS** (8 venues, 4209-5540 objects each) |
| **VenueNonProxyMergeFlattensIntoTarget** | **ABORT** | **PASS** (8 venues, 0 corrupt, 0 unreachable) |
| **VenueMergeSubdirObjectsFindableFromTop** | **ABORT** | **PASS** |
| **SequentialMergesIntoSameWorldRoot** | **ABORT** | **PASS** |
| **RepeatedVenueMergeAfterClear** | **ABORT** | **PASS** |

The GPU teardown segfault ("GpuDevice: device lost") affects all tests in ctest
but doesn't affect test logic. Separate issue.

## What We Learned

### ObjRef Ring Mechanics

The ObjRef ring is a circular doubly-linked list per `Hmx::Object`. Every
`ObjRefConcrete`/`ObjPtr`/`ObjOwnerPtr` that references an object is linked
into that object's ring via `AddRef`. When the object is destroyed,
`ReplaceRefs(nullptr)` walks the ring and nulls every ref.

The ring is **only correct if every mutation** (AddRef, Release, memmove fixup)
properly maintains the prev/next pointers. A single corruption (wrong offset,
missed fixup) makes ReplaceRefs unable to reach some refs, which then become
dangling pointers.

### Transitions Memory Layout

CharClip::Transitions uses a custom packed memory region (`mNodeStart` to
`mNodeEnd`) containing variable-size `NodeVector` structs. Each NodeVector has
an `ObjOwnerPtr<CharClip>` at offset 0 that's linked into the referenced
clip's ring. Operations that move NodeVectors in memory (AddNode realloc,
RemoveNodes memmove, Load memcpy) must fix up ring pointers afterward.

Three different fixup implementations exist:
- **AddNode** (line 122): was broken on 64-bit (hardcoded offsets) — FIXED
- **RemoveNodes** (line 143): correct (uses `sizeof(void*)`)
- **Load** (line 248): uses Release/AddRef instead of raw fixup — verified
  correct by Agent 1 (0/397 failures)

### ReplaceList + RemoveNodes Interaction

`ReplaceList` has a force-unlink fallback: if `Replace()` doesn't advance the
ring walker (`cur == next`), it assumes the node is stuck and force-unlinks it
into a self-loop. This is correct when `Replace` fails to unlink. But when
`RemoveNodes` memmoves a *different* NodeVector into the current position and
fixes up its ring pointers to match, the walker sees `cur == next` and
force-unlinks the innocent shifted node — corrupting the ring.

The `gInReplaceList` flag was designed to prevent exactly this kind of
structural mutation during ring walks. `ObjPtrVec::ReplaceNode` already checks
it. `Transitions::Replace` didn't — now it does.

### Xbox Tolerates Stale Ring Writes

On Xbox 360, the heap allocator doesn't detect writes to freed memory. Stale
ObjRef::Release writes (`prev->next = next`) land on freed blocks silently. The
game works because the corruption only affects already-dead objects. On native
with glibc, these writes corrupt the free list → "corrupted double-linked list"
abort. With ASAN, the writes are caught immediately.

### Pre-null Approaches Are Fragile

Recursive pre-null in `~ObjectDir` (walking all objects and calling
`ReplaceRefs(nullptr)` before the cascade) was tried multiple ways:
- `ObjDirItr(this, true)` + `ReplaceRefs(nullptr)` → ObjDirPtr Replace
  callbacks trigger cascade-deletes during iteration
- Walking rings directly + `Replace(nullptr)` → same problem
- Only at `sDeleteObjectsDepth == 1` → doesn't prevent nested-level issues

The fundamental problem: `Replace(nullptr)` is virtual and can trigger arbitrary
side effects including object deletion. Any pre-null approach that uses Replace
callbacks risks modifying the data structure being iterated.

## Next Steps

### P0: Implement two-phase DeleteObjects for Bug 2
The dead object tracking approach was tested and found insufficient (see above).
The recommended fix is two-phase DeleteObjects: run all destructors first
(keeping memory valid for ring Release writes), then free memory. See the
"Recommended approach" section under Bug 2 above for pseudocode and
complications (RemoveFromDir, OBJ_MEM_OVERLOAD custom allocators).

### P1: Fix GPU teardown segfault
"GpuDevice: device lost (reason 2): Device was destroyed" during test fixture
teardown. Need `GpuDevice::Shutdown()` to run before test fixture cleanup, or
skip GPU init for non-rendering tests.

### P2: Verify ASAN-clean
Run all 10 MergeScopeParity tests under ASAN with `halt_on_error=1`. All should
pass with zero ASAN errors (excluding the GPU teardown SEGV).

## Files Changed

| File | Change |
|------|--------|
| `src/system/char/CharClip.cpp` | `gInReplaceList` check in `Transitions::Replace` (Bug 1 fix); AddNode 64-bit offset fix (Bug 3) |
| `native/tests/test_merge_scope_parity.cpp` | Fixed 4 test infrastructure bugs |
| `src/system/obj/Object.h` | Removed temporary `RefsAlive()` (sentinel approach rejected) |
