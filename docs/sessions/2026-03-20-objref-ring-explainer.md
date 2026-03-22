# ObjRef Ring: How the Milo Engine Manages Object References

**Date**: 2026-03-20
**Purpose**: Architectural explainer for onboarding — covers what the ObjRef ring is, why it exists, how it breaks during cascade destruction, and how modern engines solve the same problem differently.

## What ObjRef Actually Is

In DC3 (and the whole Milo engine — RB1 through DC3), every game object inherits from `Hmx::Object`. Characters, meshes, materials, animations, cameras — everything. These objects live inside `ObjectDir` containers (think of them as "scenes" or "bundles").

Objects reference each other constantly. A mesh references its material. A material references its textures. An animation references the bones it drives. A camera shot references the crowd objects it controls.

The engine needs to answer one critical question: **when an object is destroyed, who else is pointing at it?**

## The Ring: A 2000s-Era Observer Pattern

The solution is the **ObjRef ring** — a circular doubly-linked list embedded in every object. Every time an `ObjPtr<Foo>` is set to point at an object, it inserts itself into that object's ring via `AddRef`. When it stops pointing at it, it removes itself via `Release`.

```
                    ┌──────────────────────────────┐
                    v                              |
 Material.mRefs --> Mesh.mMat --> Decal.mMat ------+
   (sentinel)       (ObjPtr)       (ObjPtr)
```

When the Material is destroyed, `~Object()` walks this ring and calls `Replace(nullptr)` on each entry — nullifying every `ObjPtr` that was pointing at it. No dangling pointers. No use-after-free. No reference counting bugs.

This is essentially a **manual, intrusive observer pattern** with O(1) insert/remove and O(n) notification on destruction.

### Key Types in the Ring

| Type | Role | Ring behavior |
|------|------|---------------|
| `ObjRef` | Base class. Has `next`/`prev` pointers and the `mAliveSentinel`. | Ring node. |
| `ObjRefConcrete<T>` | Adds `mObject` (typed pointer to the referenced object). | Calls `AddRef`/`Release` on set/clear. |
| `ObjPtr<T>` | Simple smart pointer. One ref to one object. | Single ring entry per pointer. |
| `ObjPtrVec<T>` | Vector of refs. Each element is a `Node` (inherits `ObjRefConcrete`). | One ring entry per element. Nodes live in a `std::vector<Node>` buffer. |
| `ObjPtrList<T>` | Linked list of refs. Each element is a `Node`. | One ring entry per element. Nodes are heap-allocated list nodes. |
| `ObjOwnerPtr<T>` | Like `ObjPtr` but delegates `Replace` to its owner object. | Allows classes to override what happens when a referenced object is destroyed. |
| `ObjDirPtr<T>` | Points to an `ObjectDir`. Has reference counting via `DirPtrRefCounts()` and a loader. | When refcount hits zero, deletes the dir. |
| `Hmx::Object::mRefs` | The sentinel node in each object's ring. | Starting point for ring traversal. Self-loop when ring is empty. |

### Ring Operations

**AddRef** — insert `ref` before `mRefs` (the sentinel):
```cpp
// Called as: ref->AddRef(&mRefs)
// 'this' = ref (the ObjPtr being added)
// 'ref' parameter = &mRefs (the sentinel)
void ObjRef::AddRef(ObjRef *ref) {
    next = ref;            // this -> sentinel
    prev = ref->prev;      // this <- old_last
    ref->prev = this;      // sentinel <- this
    prev->next = this;     // old_last -> this
}
```

**Release** — unlink `this` from whatever ring it's in:
```cpp
void ObjRef::Release(ObjRef *ref) {
    prev->next = next;     // skip over 'this' in the forward direction
    next->prev = prev;     // skip over 'this' in the backward direction
}
```

**ReplaceRefs** — walk the ring and nullify all references (called from `~Object`):
```cpp
void Hmx::Object::ReplaceRefs(Hmx::Object *obj) {
    // Snapshot ring entries into a vector (native) or walk inline (PPC)
    // For each entry: call entry->Replace(obj)
    // Replace sets mObject = obj (typically nullptr)
}
```

## Why It Exists: The Problem It Solves

The Milo engine loads `.milo` files — binary blobs containing entire scenes with hundreds of interconnected objects. When a venue is unloaded, ALL objects in that scene need to be destroyed. But objects reference each other in arbitrary graphs — circular references, cross-subdir references, references from persistent globals to per-scene objects.

A modern engine might use:
- **Reference counting** (`shared_ptr`/`weak_ptr`) — but circular references leak without a GC
- **A garbage collector** — but this is a 2005-era Xbox 360 game with 512MB RAM and no room for GC pauses
- **An entity-component system** with IDs instead of pointers — but the Milo engine predates ECS popularity
- **Handle tables** (index into a pool, generation counter to detect stale handles) — this is what most modern engines do

The ObjRef ring is none of these. It's a **direct pointer with an embedded notification list**. It gives you:
- Zero-cost dereference (it's just a raw pointer)
- Automatic dangling-pointer prevention (Replace nullifies on destruction)
- No reference counting overhead
- No GC pauses
- Works with arbitrary object graphs including cycles

## Is It Good Design or Jank?

**It's clever 2000s-era design with a fatal flaw.**

The clever part: the intrusive ring has zero allocation overhead. Each `ObjPtr` IS a ring node (it has `next`/`prev` pointers built in). No separate subscription lists, no heap allocations for observer registration, no hash maps. For a memory-constrained console, this is efficient.

The fatal flaw: **destruction order matters, and the ring doesn't protect against it.**

When `ObjectDir` destroys its objects, it iterates the hash table and deletes them one by one. Each delete triggers `~Object()` -> `ReplaceRefs()` -> walks the ring -> calls `Replace()` on each ref. But those refs may be inside OTHER objects in the same dir that were ALREADY destroyed. The ring walk reads `next`/`prev` from freed memory. The `Replace` callback writes to freed objects.

## How the Xbox 360 Gets Away With It

On Xbox 360, this undefined behavior "works" because:

1. **`MemFree` returns to a pool** — freed memory isn't unmapped or zeroed. The bytes sit in a free-list, intact, until reused. Reading stale data from freed pool memory gives you valid-looking values.

2. **No memory poisoning** — there's no ASAN, no debug heap fills, no guard pages. The stale reads silently succeed.

3. **Pool reuse is delayed** — during a single destruction cascade, the pool doesn't reuse blocks fast enough for a stale read to see corrupted data.

This is textbook **undefined behavior that happens to work on the target platform**. Harmonix engineers probably never saw crashes here because the Xbox memory system is forgiving. The code shipped, sold millions of copies, and nobody noticed.

## How It Breaks on Native (x86_64 Linux)

When we run this same code on x86_64 Linux with ASAN:

1. `free()` poisons memory immediately (ASAN shadow bytes)
2. Any read from freed memory is flagged as `heap-use-after-free`
3. Any write to freed memory is flagged
4. Memory is quarantined (never reused quickly), so stale pointers always point to poisoned regions

The cascade looks like:

```
~ObjectDir()
  +-- mSubDirs.clear()            <-- destroys subdirs via ObjDirPtr
  |     +-- ~SubDir()
  |           +-- DeleteObjects()  <-- destroys subdir's objects
  |                 +-- ~FileMerger()
  |                       +-- delete mLoadedObjects[0]   <-- frees object
  |                             +-- ~Object()
  |                                   +-- ReplaceRefs()  <-- walks ring into freed memory
  +-- DeleteObjects()              <-- destroys parent's objects
        +-- ~HamCamTransform()
              +-- ClearOldCrowds() <-- reads from destroyed sibling CamShots
```

Every layer of this cascade has objects reading from siblings that were freed earlier in the same loop. On Xbox, the reads return stale-but-valid data. On native, ASAN catches them.

## What We Did to Fix It (Native Port)

We couldn't fix the underlying design (that would mean rewriting the engine's object model). Instead we made the cascade destruction safe for native:

1. **Deferred free** — don't `free()` object blocks until the outermost `~ObjectDir` finishes. Memory stays allocated so sibling destructors can read from it.

2. **Lightweight ReplaceRefs during cascade** — instead of skipping `ReplaceRefs` entirely or running full Replace callbacks, do a SnapshotRing pass that just nullifies `mObject` on surviving refs. This prevents stale-pointer crashes without triggering unsafe callbacks.

3. **Guard destructors** — classes like `FileMerger` and `FaderGroup` that `delete` their contents in destructors skip that during cascade (the parent dir's `DeleteObjects` handles cleanup).

4. **Ring cleanup for survivors** — objects in persistent dirs (like MetaMaterial banks) survive cascade with stale ring entries. `SafeReleaseFromRing` with `no_sanitize` properly unlinks them when they're next used.

5. **sRingsDirty flag** — gates all ring cleanup logic behind a single bool. During normal (non-cascade) operation, zero overhead — just one predicted-not-taken branch.

See [2026-03-20-cascade-teardown-fix.md](2026-03-20-cascade-teardown-fix.md) for the full session log with implementation details and remaining edge cases.

## Modern Alternatives: What Would a Rewrite Look Like?

The ObjRef ring solves a real problem (dangling pointer prevention in an object graph with arbitrary topology). Any replacement needs to handle the same cases:

- Circular references (A -> B -> A)
- Bulk destruction (unload an entire scene of 5000 objects at once)
- Cross-scope references (persistent globals referencing per-scene objects)
- Notification on destruction (mesh needs to know its material died)
- Zero or near-zero dereference cost (called millions of times per frame)

Here are three approaches, from least to most invasive.

### Option 1: Generational Handles (Least Invasive)

Replace raw `Hmx::Object*` pointers with indirect handles that go through a lookup table.

```cpp
struct ObjectHandle {
    uint32_t index;       // slot in the pool
    uint32_t generation;  // incremented when slot is recycled
};

class ObjectPool {
    struct Slot {
        Hmx::Object *ptr;
        uint32_t generation;
    };
    std::vector<Slot> slots;
    std::vector<uint32_t> freeList;

public:
    ObjectHandle add(Hmx::Object *obj) {
        uint32_t idx = freeList.back();
        freeList.pop_back();
        slots[idx].ptr = obj;
        return {idx, slots[idx].generation};
    }

    void remove(ObjectHandle h) {
        slots[h.index].ptr = nullptr;
        slots[h.index].generation++;  // invalidates all existing handles
        freeList.push_back(h.index);
    }

    Hmx::Object *resolve(ObjectHandle h) const {
        auto &slot = slots[h.index];
        return (slot.generation == h.generation) ? slot.ptr : nullptr;
    }
};
```

**How destruction works**: Call `pool.remove(handle)`. Done. No ring walk, no notification, no ordering concerns. Every existing handle to that object now resolves to `nullptr` automatically on next access.

**Tradeoff**: Every dereference pays for an indirection (array lookup + generation check). On modern CPUs with L1 cache, this is ~1-2ns. The Milo ring gives you a raw pointer dereference (~0.3ns) but pays O(n) on destruction. Generational handles pay O(1) on both dereference AND destruction, but the constant is higher on dereference.

**Migration path**: Replace `ObjPtr<T>` with a `TypedHandle<T>` that wraps `ObjectHandle` and does `dynamic_cast` on resolve. Keep the same API surface (`operator->`, `operator T*`). The ring machinery (`AddRef`/`Release`/`ReplaceRefs`) is deleted entirely. This could be done incrementally — have `ObjPtr<T>` internally use a handle but keep the same external interface.

**What you lose**: Destruction notification. With the ring, a class can override `Replace()` to react when a referenced object dies (e.g., `CharBonesMeshes::Replace` swaps in a dummy mesh). With handles, the reference just silently becomes null. You'd need a separate event system for the ~5 classes that actually use Replace callbacks.

**Who does this**: Bevy (Rust ECS), EnTT, Flecs, most modern game engines. Unreal Engine's `TWeakObjectPtr` is a variation (uses a global serial number table).

### Option 2: Weak Pointer with Invalidation Set (Middle Ground)

Keep direct pointers for dereference speed, but replace the ring with a central invalidation set.

```cpp
class WeakRef {
    Hmx::Object *ptr;        // direct pointer for fast access
    uint64_t     stamp;       // snapshot of object's alive-stamp at time of assignment

public:
    Hmx::Object *get() const {
        // Object stores its own stamp; if it changed, object was destroyed and
        // the slot was recycled. Return nullptr.
        return (ptr && ptr->aliveStamp == stamp) ? ptr : nullptr;
    }
};
```

Each `Hmx::Object` gets a monotonically-increasing `aliveStamp` assigned at construction. When destroyed, the stamp is invalidated (set to 0 or incremented). Any `WeakRef` holding the old stamp returns nullptr.

**How destruction works**: Set the object's stamp to 0. All weak refs become stale. No traversal needed.

**Tradeoff**: Dereference is almost as fast as raw pointer (one extra comparison). But this ONLY works if the memory at `ptr` is still readable after destruction — otherwise reading `ptr->aliveStamp` is UB. You'd need deferred free (like we already have) or a pool allocator that preserves the stamp field after free.

**Migration path**: `ObjPtr<T>` stores both a `T*` and a `uint64_t stamp`. `operator->` checks the stamp. If you already have deferred free infrastructure (we do), this is straightforward.

**Who does this**: Variation of Unreal's `TWeakObjectPtr` (uses global index + serial number). Also similar to Godot's `ObjectID` system.

### Option 3: ECS with Archetype Storage (Most Invasive)

Replace the entire `Hmx::Object` hierarchy with an Entity-Component-System. Objects become entity IDs (plain integers). Components are stored in contiguous arrays grouped by archetype.

```cpp
using Entity = uint64_t;

// Instead of: mesh->SetMat(material);
// You write:  world.get<MeshComponent>(meshEntity).material = materialEntity;

// Instead of: ~Object() { ReplaceRefs(nullptr); }
// You write:  world.destroy(entity);
//             // All components referencing this entity check on next access
```

**How destruction works**: `world.destroy(entity)` removes the entity from all archetype tables. References are entity IDs — they're just integers, never dangling pointers. Systems check `world.alive(entity)` before dereferencing.

**Tradeoff**: Complete rewrite. The Milo engine's deep inheritance hierarchy (`RndDrawable` -> `RndMesh` -> `WorldCrowd` -> ...) doesn't map naturally to flat component tables. You'd need to decompose every class into components. The `.milo` file format is built around the object hierarchy — the serialization layer would need rewriting too.

**Who does this**: Bevy, Unity DOTS, Flecs, EnTT. Generally not retrofitted onto existing OOP engines — it's a ground-up architectural decision.

### Comparison Table

| | ObjRef Ring (current) | Generational Handle | Weak Ref + Stamp | ECS |
|---|---|---|---|---|
| Dereference cost | ~0.3ns (raw ptr) | ~1-2ns (array + branch) | ~0.5ns (ptr + branch) | ~2ns (table lookup) |
| Destroy cost | O(n) ring walk | O(1) increment | O(1) set stamp | O(1) remove |
| Bulk destroy | Broken (cascade UB) | Free (handles auto-stale) | Free (stamps auto-stale) | Free (IDs auto-stale) |
| Notification on destroy | Built in (Replace) | Need separate events | Need separate events | Need separate events |
| Memory overhead per ref | 16 bytes (next + prev) | 8 bytes (index + gen) | 12 bytes (ptr + stamp) | 8 bytes (entity ID) |
| Circular refs | Handled | N/A (no ownership) | N/A (no ownership) | N/A (no ownership) |
| Migration effort | N/A (current) | Medium (replace ObjPtr) | Medium (replace ObjPtr) | Total rewrite |

### Recommendation for the Native Port

**Option 1 (Generational Handles)** is the sweet spot if we ever want to eliminate the cascade UB entirely. It could be done incrementally:

1. Add `ObjectPool` alongside existing system
2. Give each `Hmx::Object` a handle at construction
3. Replace `ObjPtr<T>` internals to use handle resolution instead of raw pointer + ring
4. Delete ring machinery (`AddRef`/`Release`/`ReplaceRefs`/`SnapshotRing`)
5. Add an event bus for the ~5 classes that need destruction notification (`CharBonesMeshes`, `FileMerger`, etc.)

The `.milo` loading code stays the same (it creates objects and sets up references). The serialization format doesn't change. Only the reference-holding mechanism changes.

That said, the current fix (deferred free + lightweight ReplaceRefs + guarded destructors) is sufficient for the native port. A full handle migration would be a significant project with risk of introducing new bugs in the ~16,000 functions that use ObjPtr.

## Key Files

| File | What's there |
|------|-------------|
| `src/system/obj/Object.h` | `ObjRef`, `ObjRefConcrete`, `ObjPtr`, `ObjOwnerPtr`, `ObjPtrVec`, `ObjPtrList`, `Hmx::Object` |
| `src/system/obj/ObjPtr_p.h` | Template implementations for `ObjRefConcrete`, `ObjPtrVec`, `ObjPtrList` |
| `src/system/obj/Object.cpp` | `~Object()`, `ReplaceRefs()`, `SnapshotRing()` |
| `src/system/obj/Dir.h` | `ObjectDir`, `ObjDirPtr`, `ObjDirItr`, `DeferFree`/`FlushDeferredFrees` |
| `src/system/obj/Dir.cpp` | `~ObjectDir()`, `DeleteObjects()` |
