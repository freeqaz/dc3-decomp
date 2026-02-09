# Hard Decomp Patterns: Iterator Caching & Boolean Init

**Date**: 2026-02-09
**Function**: `PhysicsManager::HarvestCollidables` (86.6% -> 97.4%)

## Pattern 1: ObjDirItr Dereference Caching (+10.2%)

### The Problem

When using `ObjDirItr<T>` in a loop and accessing the underlying object via `&*it` or `it->` multiple times, the compiler reloads the `mObj` pointer from the iterator's stack storage on every access. This generates extra `lwz` instructions (vtable dereferences) and wastes a register on the reload address instead of keeping the object pointer in a dedicated register.

### The Fix

Cache the iterator dereference into a local pointer at the top of the loop body:

```cpp
// BEFORE (bad codegen - reloads mObj each time)
for (ObjDirItr<RndDrawable> it(dir, true); it != nullptr; ++it) {
    RndMesh *mesh = dynamic_cast<RndMesh *>(&*it);
    if (mesh) {
        const DataNode *prop = it->Property(collidable, false);
        // ... later ...
        AddCollidable(it, parentProxy, mesh->Showing());
    } else {
        PhysicsVolume *pv = dynamic_cast<PhysicsVolume *>(&*it);
    }
    ObjectDir *proxyProxy = dynamic_cast<ObjectDir *>(&*it);
}

// AFTER (good codegen - pointer stays in register)
for (ObjDirItr<RndDrawable> it(dir, true); it != nullptr; ++it) {
    RndDrawable *drawable = it;  // uses operator T*(), caches mObj
    RndMesh *mesh = dynamic_cast<RndMesh *>(drawable);
    if (mesh) {
        const DataNode *prop = drawable->Property(collidable, false);
        // ... later ...
        AddCollidable(drawable, parentProxy, mesh->Showing());
    } else {
        PhysicsVolume *pv = dynamic_cast<PhysicsVolume *>(drawable);
    }
    ObjectDir *proxyProxy = dynamic_cast<ObjectDir *>(drawable);
}
```

### Why It Matters

The MWCC compiler for PPC doesn't CSE (common subexpression eliminate) through the `ObjDirItr::operator*()` indirection when the result is taken by address. Each `&*it` is treated as a fresh load from the iterator struct on the stack. With 3+ uses in a loop body, this cascades into register allocation differences — the target binary keeps the pointer in a callee-saved register (e.g. r30) while our code burns temporaries on repeated loads.

In HarvestCollidables, this single change accounted for +10.2% match improvement.

### Where Else This Applies

High-impact candidates (loops with 2+ `&*it` or mixed `&*it`/`it->` uses):

| File | Loop Line | Uses | Notes |
|------|-----------|------|-------|
| `char/CharClipSet.cpp` | ~152 | 3 dynamic_casts on `&*it` | + `it->Poll()` |
| `hamobj/HamDirector.cpp` | ~1798 | 3 dynamic_casts on `&*it` | identical pattern |
| `world/CameraManager.cpp` | ~382 | 2 dynamic_casts on `&*it` | CamShot + WorldCrowd |
| `char/Character.cpp` | ~536 | dynamic_cast + method calls | RndMesh cast |
| `rndobj/AmbientOcclusion.cpp` | ~29 | 2 dynamic_casts on `&*it` | ObjectDir + template T |
| `meta_ham/HamUI.cpp` | ~153 | dynamic_cast + `it->` calls | HamScreen check |
| `obj/Dir.cpp` | ~104 | 3 uses of `&*it` | FindObject + ReplaceRefs + delete |
| `rndobj/Mat.cpp` | ~244 | 2 uses of `&*it` | Find + delete |

## Pattern 2: Boolean Initialization from Existing Value (+0.6%)

### The Problem

When a boolean controls later logic and the "true" case comes from an already-known condition:

```cpp
// BEFORE (generates extra li r3, 0x0 + mismatched branch structure)
bool u2 = false;
if (!mesh->GetKeepMeshData()) {
    RndMesh *owner = mesh->GetGeomOwner();
    if (mesh != owner) {
        u2 = HasKeepMeshData(owner);
    }
} else {
    u2 = true;
}
```

The compiler generates `li r3, 0x0` to initialize `u2`, then the `else { u2 = true }` branch sets it to 1. But the target binary never explicitly initializes `u2` — it reuses the existing register value from `prop->Int()` (which returned 1, since we're inside `if (i5 == 1)`).

### The Fix

Initialize from the value that's already in a register:

```cpp
// AFTER (reuses r3 which already holds 1 from prop->Int())
bool u2 = i5;  // i5 is known to be 1 here
if (!mesh->GetKeepMeshData()) {
    RndMesh *owner = mesh->GetGeomOwner();
    if (mesh != owner) {
        u2 = HasKeepMeshData(owner);
    } else {
        u2 = false;  // explicit false only in the "same owner" case
    }
}
// no else needed — u2 is already truthy when GetKeepMeshData() is true
```

### Why It Matters

The MWCC compiler is good at noticing when a register already holds a useful value. By writing `bool u2 = i5` instead of `bool u2 = false`, we let the compiler skip the initialization and reuse r3. The restructured if/else also eliminates the `else { u2 = true }` branch entirely, since the truthy case is the default.

This pattern is specific to cases where you're inside a conditional that already tested the value you want. Look for:
- `if (x == 1) { bool flag = false; ... else { flag = true; } }` — `flag` can init from `x`
- `if (condition) { bool flag = false; ... } else { flag = true; }` — consider inverting

## Pattern 3: Unfixable Stack Spills (Recognize and Accept)

### What It Looks Like

The target binary has `stw rN, offset(r31)` instructions that store a local variable to the stack frame, but our compiled code keeps it in a register. This shows up as 1-2 `delete` instructions in objdiff.

In HarvestCollidables, the target stores `owner` (GetGeomOwner result) to stack offset 0x54 twice — once before comparing `mesh == owner` and once in the `mesh != owner` branch. Our code keeps `owner` in a register throughout.

### Why It's Unfixable

Stack spill decisions are made by the register allocator based on:
- Register pressure at each program point
- Estimated cost of spill vs. reload
- Scheduling heuristics

These are internal compiler decisions with no source-level knob. Hoisting variable declarations, reordering code, or adding dummy uses generally doesn't help — the allocator has its own model.

### How to Recognize

- objdiff shows 1-3 `delete` instructions that are all `stw` to the stack frame
- The stored register contains a local variable that's used later
- Removing/adding code doesn't change the spill pattern
- The function is otherwise very close (97%+)

**Accept these and mark at_limit.** The ~1-2% from stack spills is not worth spending time on.
