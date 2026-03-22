# Virtual Base Pointer Identity Comparison Audit

## Background

On Itanium ABI (native x86_64), `Hmx::Object` is a virtual base class in Milo's
multi-inheritance hierarchy. When a derived class inherits `Hmx::Object` through
multiple virtual paths (e.g., `HamDirector -> RndPollable -> Hmx::Object` AND
`HamDirector -> RndDrawable -> RndHighlightable -> Hmx::Object`), the compiler
generates vbtable-based adjustments to locate the single `Hmx::Object` subobject.

In standard C++, all paths should resolve to the same `Hmx::Object*` address since
there is exactly one virtual base subobject per most-derived object. However, the
PropAnim bug (commit `532643bf8`) demonstrated that in practice, `PropKeys::Target()`
(stored `Hmx::Object*`) and `TheHamDirector` (cast from `HamDirector*`) yielded
different `Hmx::Object*` values for the same object (offset 0x780 apart), causing
camera shot cycling to fail.

The fix uses `dynamic_cast<const void*>` to canonicalize to the most-derived address.

## Fix Pattern

```cpp
#ifdef HX_NATIVE
static inline bool SameObject(const Hmx::Object *a, const Hmx::Object *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return dynamic_cast<const void *>(a) == dynamic_cast<const void *>(b);
}
#else
#define SameObject(a, b) ((a) == (b))
#endif
```

Already applied in `src/system/rndobj/PropAnim.cpp` for `GetKeys`, `FindKeys`,
and `GetNumKeys`.

## Classification Criteria

- **Hmx::Object* vs Hmx::Object***: Both sides are `Hmx::Object*`. Safe if both
  come from the same conversion chain; potentially risky if one comes from
  deserialization/ObjRef and the other from a different implicit conversion.
- **T1* vs Hmx::Object***: Implicit conversion `T1* -> Hmx::Object*` through
  virtual base. Safe if the `Hmx::Object*` was also obtained through the SAME `T1`
  type's vbtable; risky if obtained through a different type's vbtable.
- **ObjRef* vs ObjRef***: Ring node address comparison, NOT object identity. Always safe.
- **Same-type comparisons** (e.g., `RndMesh* == RndMesh*`): No vbase adjustment
  involved. Always safe.

## Summary Table

| # | File | Line | Expression | Risk | Needs Fix? |
|---|------|------|-----------|------|------------|
| 1 | `ObjPtrVec_impl.h` | 16,27 | `*it == target` in `find()` | MEDIUM | Probably not |
| 2 | `ObjPtr_p.h` | 507,516 | `*it == target` in `ObjPtrList::find/remove` | MEDIUM | Probably not |
| 3 | `Msg.cpp` | 62 | `it->obj == o` in `HasSink` | LOW | No |
| 4 | `Msg.cpp` | 88 | `it->obj == o` in `EventSink::Remove` | LOW | No |
| 5 | `Msg.cpp` | 269 | `it->obj == obj` in `RemoveSink` | LOW | No |
| 6 | `Sound.cpp` | 303,329,359,487 | `GetEventReceiver() == obj` | MEDIUM | Monitor |
| 7 | `Group.cpp` | 329 | `mDrawOnly == obj` | LOW | No |
| 8 | `Group.cpp` | 390 | `n->Obj() == obj` in `MoveObject` | LOW | No |
| 9 | `Utl.cpp` | 115 | `*it == o` in `GroupedUnder` | LOW | No |
| 10 | `RockCentral.cpp` | 265 | `GetCallback() == obj` | MEDIUM | Monitor |
| 11 | `Object.cpp` | 405 | `RefOwner() == from` in `ReplaceRefsFrom` | HIGH | Yes |
| 12 | `PanelDir.cpp` | 311 | `mFocusComponent == o` | LOW | No |
| 13 | `PropKeys.cpp` | 246 | `mTarget.Ptr() != o` in `SetTarget` | LOW | No |
| 14 | `Std.h` | 56,85,90 | `VectorRemove`, `RemoveSwap` templates | MEDIUM | See below |
| 15 | `DefaultPhysicsManager.cpp` | 149,158,182,186,190 | `std::find` on `RndMesh*` list with `Hmx::Object*` | MEDIUM | Monitor |
| 16 | `ObjPtrVec_impl.h` | 77 | `jt->Obj() == obj` in `unique` | LOW | No |
| 17 | `TypeProps.cpp` | 186 | `current->Obj() == target` | LOW | No |
| 18 | `SoftParticleBuffer.cpp` | 133 | `static_cast<Hmx::Object*>(*it) == target` | LOW | No |
| 19 | `Instance.cpp` | 252 | `this != obj` | LOW | No |
| 20 | `CharPollGroup.cpp` | 137,180 | `mDeps[c]` map key lookup | MEDIUM | Monitor |
| 21 | `PropKeys.cpp` | 218,673+ | `mTrans != mTarget` guard | LOW | No (perf only) |

## Detailed Analysis

### 1-2. ObjPtrVec::find / ObjPtrList::find / ObjPtrList::remove (MEDIUM)

**Files**: `src/system/obj/ObjPtrVec_impl.h:13-30`, `src/system/obj/ObjPtr_p.h:503-521`

```cpp
// ObjPtrVec::find
if (*it == target)  // Node -> T1* via operator T1*(), then T1* == Hmx::Object*

// ObjPtrList::find
if (*it == target)  // Node -> T1* via Obj(), then T1* == Hmx::Object*
```

These template functions compare a typed pointer (`T1*`, e.g. `RndDrawable*`) against
`const Hmx::Object* target`. The implicit conversion `T1* -> Hmx::Object*` goes
through the virtual base table.

**Why MEDIUM, not HIGH**: The `target` parameter typically comes from the same ObjRef
system (same deserialization path). All insertions and lookups go through the same
`T1*` -> `Hmx::Object*` conversion. The stored `mObject` (type `T1*`) was obtained
via `dynamic_cast<T1*>(root_obj)` in `SetObj`, and the lookup target was obtained
through a compatible path.

**Risk scenario**: If someone calls `vec.find(hmxObjPtr)` where `hmxObjPtr` was
obtained through a different intermediate class's implicit conversion than the stored
elements. This is unlikely in practice because find is typically called with the same
object reference that was used for insertion.

**Recommendation**: No fix needed now. If bugs surface in ObjPtrVec/ObjPtrList
operations on native, apply `SameObject` in these templates.

### 3-5. MsgSinks comparisons (LOW)

**File**: `src/system/obj/Msg.cpp:62,88,269`

```cpp
if (it->obj == o)   // ObjOwnerPtr<Hmx::Object> vs Hmx::Object*
```

`Sink::obj` is `ObjOwnerPtr<Hmx::Object>` which stores `Hmx::Object*`. The
comparison is `Hmx::Object* == Hmx::Object*`. Both are the same type.

The `o` parameter comes from callers like `RemoveSink(obj, ev)` where `obj` is
an `Hmx::Object*`. The stored pointer was set via `SetObjConcrete(Hmx::Object*)`
which stores the pointer as-is. Both values went through proper C++ conversion.

**Recommendation**: No fix needed. Both sides are `Hmx::Object*` obtained through
standard conversion paths.

### 6. Sound::GetEventReceiver() comparisons (MEDIUM)

**File**: `src/system/synth/Sound.cpp:303,329,359,487`

```cpp
if ((*it)->GetEventReceiver() == obj)
```

`GetEventReceiver()` returns `Hmx::Object*` from `ObjPtr<Hmx::Object>`. The `obj`
parameter is `Hmx::Object*`.

**Risk scenario**: `Sound::Play(obj)` stores the event receiver, then `Sound::Stop(obj2)`
compares against it. If `obj` and `obj2` came from different conversion paths for the
same object, the comparison fails.

In practice, callers like `FlowSound` consistently pass `this` (same FlowSound*)
for both Play and Stop, so both conversions go through the same path.

**Recommendation**: Monitor. If audio stop/volume fails for specific objects on native,
apply `SameObject` here.

### 7-8. RndGroup comparisons (LOW)

**File**: `src/system/rndobj/Group.cpp:329,390`

```cpp
if (mDrawOnly == obj && !gInReplace)  // ObjPtr<RndDrawable> vs Hmx::Object*
if (n->Obj() == obj)                  // ObjPtrList<Hmx::Object>::Node vs Hmx::Object*
```

Line 329: `mDrawOnly` is `ObjPtr<RndDrawable>` (stores `RndDrawable*`), compared
against `Hmx::Object* obj`. The conversion `RndDrawable* -> Hmx::Object*` is used.

Line 390: `mObjects` is `ObjPtrList<Hmx::Object>` so `n->Obj()` returns `Hmx::Object*`.
Same-type comparison.

**Recommendation**: No fix needed. Line 329 uses consistent conversion paths; line 390
is same-type.

### 9. GroupedUnder (LOW)

**File**: `src/system/rndobj/Utl.cpp:115`

```cpp
if (*it == o)  // ObjPtrList<Hmx::Object>::iterator -> Hmx::Object* vs Hmx::Object*
```

Same-type comparison. Both are `Hmx::Object*`.

**Recommendation**: No fix needed.

### 10. RockCentral::CancelOutstandingCalls (MEDIUM)

**File**: `src/lazer/net_ham/RockCentral.cpp:265`

```cpp
if (cur->GetCallback() == obj)
```

`GetCallback()` returns `ObjPtr<Hmx::Object>` -> `Hmx::Object*`. The `obj` parameter
is `Hmx::Object*`. Same-type comparison.

**Risk scenario**: If the callback was set from one call site and `CancelOutstandingCalls`
is called with a pointer obtained through a different path. In practice, callbacks are
typically set and cancelled by the same object (`this`).

**Recommendation**: Monitor. Low likelihood of failure.

### 11. Object::ReplaceRefsFrom (HIGH)

**File**: `src/system/obj/Object.cpp:405`

```cpp
void Hmx::Object::ReplaceRefsFrom(Hmx::Object *from, Hmx::Object *to) {
    ...
    FOREACH (it, mRefs) {
        if (it->RefOwner() == from) {
            it->Release(&other);
            other.AddRef(it);
        }
    }
    ...
}
```

`it->RefOwner()` returns `Hmx::Object*` from the ObjRef's virtual `RefOwner()`
method. For `ObjPtr<T>`, this returns `mOwner` (stored as `Hmx::Object*`). For
`ObjOwnerPtr<T>`, this returns `mOwner->RefOwner()` (goes through virtual dispatch).

The `from` parameter is `Hmx::Object*`.

**Risk scenario**: `ReplaceRefsFrom` is called from PropAnim handler:
```cpp
_msg->Obj<Hmx::Object>(2)->ReplaceRefsFrom(this, _msg->Obj<Hmx::Object>(3))
```
Here, `this` is `RndPropAnim*` implicitly converted to `Hmx::Object*`. The
`RefOwner()` of ObjRefs owned by this object also returns `Hmx::Object*`.
If these went through different conversion paths, the comparison fails.

This function is in the critical path for target replacement in animation
keyframes (the `replace_target` handler in PropAnim).

**Recommendation**: Apply `SameObject` defensively.

```cpp
#ifdef HX_NATIVE
        if (SameObject(it->RefOwner(), from)) {
#else
        if (it->RefOwner() == from) {
#endif
```

### 14. VectorRemove / RemoveSwap templates (MEDIUM)

**File**: `src/system/utl/Std.h:56,85,90`

```cpp
template <class T1, class T2>
void VectorRemove(std::vector<T1> &vec, const T2 &obj) {
    for (...) {
        if (*it == obj) {  // T1 == T2, may involve cross-type comparison
```

Called from `RndGroup::RemoveObject(Hmx::Object* obj)`:
```cpp
VectorRemove(mDraws, obj);   // vector<RndDrawable*>, Hmx::Object*
VectorRemove(mAnims, obj);   // vector<RndAnimatable*>, Hmx::Object*
```

The comparison `RndDrawable* == Hmx::Object*` requires `RndDrawable*` ->
`Hmx::Object*` conversion through virtual base.

`RemoveSwap` also uses `std::find` which does the same comparison internally.

**Risk scenario**: If `obj` (the `Hmx::Object*` parameter) was obtained through
a different vbase path than the implicit conversion from `RndDrawable*`.

**Recommendation**: These are called from `RemoveObject` during Replace operations.
The `obj` parameter typically comes from `entry->obj` which was stored as
`Hmx::Object*` via `SetName`. The `RndDrawable*` in the vector was stored via
proper `dynamic_cast`. Both should resolve to the same address under standard
semantics. Monitor but don't fix preemptively.

### 15. DefaultPhysicsManager cross-type lookups (MEDIUM)

**File**: `src/system/world/DefaultPhysicsManager.cpp:149,158,182,186,190`

```cpp
auto it = std::find(mInactiveCollidables.begin(), mInactiveCollidables.end(), o);
// mInactiveCollidables is list<RndMesh*>, o is Hmx::Object*

std::map<Hmx::Object*, ObjectDir*>::iterator mapIt = mCollidableDirs.find(o);
// Map keyed with Hmx::Object* obtained from RndMesh* conversion
```

The `ActivateCollidable`, `DeactivateCollidable`, and `RemoveCollidable` functions
all take `Hmx::Object* o` and search through containers of `RndMesh*`.

The map was populated via `mCollidableDirs[mesh] = dir` where `mesh` is `RndMesh*`
(implicit conversion `RndMesh*` -> `Hmx::Object*` for the key).

**Risk scenario**: If the `Hmx::Object* o` parameter came from a path that produces
a different address than `(Hmx::Object*)(RndMesh*)ptr`. In `Replace` (line 74), `o`
comes from `from->GetObj()` which is the ObjRef's stored pointer. If the ObjRef was
a `ObjPtrList<RndMesh>::Node`, `GetObj()` returns `(Hmx::Object*)(RndMesh*)mObject`,
which goes through the same conversion as the map key.

**Recommendation**: Monitor. The conversion paths are consistent in the current code.

### 18. SoftParticleBuffer::Queue (LOW)

**File**: `src/system/rndobj/SoftParticleBuffer.cpp:133`

```cpp
Hmx::Object *target = static_cast<Hmx::Object *>(drawable);
if (static_cast<Hmx::Object *>(*it) == target) {
```

Both sides use `static_cast<Hmx::Object*>` from `RndDrawable*`. Same conversion
path. Safe.

### 20. CharPollableSorter::mDeps map (MEDIUM)

**File**: `src/system/char/CharPollGroup.cpp:137,180`

```cpp
std::map<Hmx::Object *, Dep> mDeps;   // keyed by Hmx::Object*

// Sort() inserts via CharPollable* -> Hmx::Object*:
Dep &dep = mDeps[c];   // c is CharPollable*

// AddDeps() looks up via Hmx::Object* from a list:
Dep *mapDep = &mDeps[cur];   // cur is Hmx::Object*
```

`CharPollable` inherits from `RndPollable : public virtual Hmx::Object`. Insertions
convert `CharPollable*` -> `Hmx::Object*` through the virtual base. Lookups from
`AddDeps` use `Hmx::Object*` from a `std::list<Hmx::Object*>` obtained from
`ChangedPollables()`.

**Risk scenario**: If the `Hmx::Object*` in the dependency list was obtained through
a different virtual base path than the `CharPollable*` insertion, the map lookup
returns a default-constructed Dep instead of the existing one, corrupting the
dependency graph.

**Recommendation**: Monitor. Failure would manifest as incorrect poll ordering
(character animation artifacts on native).

### 21. PropKeys mTrans/mTarget guard comparisons (LOW)

**File**: `src/system/rndobj/PropKeys.cpp:218,673,707,741,756,784,793`

```cpp
if (mTrans != mTarget.Ptr()) {
    mTrans = dynamic_cast<RndTransformable *>(mTarget.Ptr());
}
```

These are optimization guards to avoid redundant `dynamic_cast`. If the comparison
fails (false negative due to different vbase addresses), the code simply redoes the
`dynamic_cast` — no correctness impact, just a wasted cast per frame.

**Recommendation**: No fix needed. The redundant cast has negligible performance cost.

## Recommendations

### Immediate Fixes (HIGH risk)

1. **Object::ReplaceRefsFrom** (Object.cpp:405): Apply `SameObject` for the
   `RefOwner() == from` comparison. This is in the critical path for PropAnim
   target replacement and uses cross-object RefOwner queries.

### Defensive Monitoring (MEDIUM risk)

2. **Sound event receiver comparisons**: If audio routing issues appear on native
   (sounds not stopping for specific objects), apply `SameObject` to the four
   `GetEventReceiver() == obj` sites in Sound.cpp.

3. **ObjPtrVec::find / ObjPtrList::find**: If container lookups fail on native
   (objects not found that should be present), consider adding `SameObject` to
   the template implementations. This would require making `SameObject` a
   globally accessible utility.

4. **VectorRemove cross-type instantiations**: The `RndGroup::RemoveObject` usage
   with `vector<RndDrawable*>` and `Hmx::Object*` involves cross-type comparison
   through virtual base. Monitor for group membership bugs.

5. **DefaultPhysicsManager lookups**: Monitor for physics collision failures where
   collidables are not found in the map/list.

6. **CharPollableSorter::mDeps map**: `std::map<Hmx::Object*, Dep>` keyed by
   `Hmx::Object*`. If character poll ordering is wrong (animation glitches),
   investigate whether map keys differ due to vbase offsets.

### No Fix Needed (LOW risk)

- MsgSinks comparisons (same-type `Hmx::Object*` == `Hmx::Object*`)
- RndGroup::MoveObject (same-type through `ObjPtrList<Hmx::Object>`)
- GroupedUnder (same-type through `ObjPtrList<Hmx::Object>`)
- PanelDir::RemovingObject (single-direction conversion)
- PropKeys::SetTarget (same-type, guard is just for redundant set prevention)
- SoftParticleBuffer::Queue (both sides use identical `static_cast` path)
- ObjPtrVec::unique (internal same-type comparison)
- Instance.cpp `this != obj` (same object, proper vbase dispatch)

### Architecture Note

The `SameObject` helper is currently local to `PropAnim.cpp`. If more sites need
fixing, consider promoting it to a utility in `src/system/obj/Object.h`:

```cpp
#ifdef HX_NATIVE
inline bool SameObject(const Hmx::Object *a, const Hmx::Object *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return dynamic_cast<const void *>(a) == dynamic_cast<const void *>(b);
}
#else
inline bool SameObject(const Hmx::Object *a, const Hmx::Object *b) {
    return a == b;
}
#endif
```

This would allow all sites to use it without local redefinition.

## Appendix: Virtual Inheritance Hierarchy

Classes with virtual `Hmx::Object` base (can have vbase offset issues):

```
Hmx::Object (root)
  +-- (virtual) RndHighlightable
  |     +-- (virtual) RndDrawable -> RndMesh, RndText, RndEnviron, ...
  |     +-- (virtual) RndTransformable -> RndCam, RndLight, ...
  +-- (virtual) RndAnimatable -> RndPropAnim, RndTransAnim, ...
  +-- (virtual) RndPollable -> HamDirector, Character, ...
  +-- (virtual) ObjectDir -> RndDir -> WorldDir, PanelDir, ...
  +-- (virtual) CharWeightable -> CharDriver, HamDriver, ...
  +-- (virtual) EventTrigger
  +-- (virtual) FlowNode -> FlowAnimate, FlowSound, ...
  +-- (virtual) UIPanel -> HamPanel, ...
  +-- (virtual) Sound
  +-- (virtual) CameraInput
  +-- (virtual) CharClipGroup
  +-- (virtual) CharBonesObject
  +-- (virtual) UIFontImporter
  +-- (virtual) Profile
  +-- (virtual) MetaPerformer
  +-- (virtual) HamWardrobe
  +-- (virtual) Instarank
```

Multi-inheritance classes at highest risk (multiple virtual paths to Hmx::Object):

- `RndGroup : RndAnimatable, RndDrawable, RndTransformable`
- `HamDirector : RndPollable, RndDrawable`
- `Character : RndDir (-> ObjectDir + RndTransformable + RndDrawable + RndAnimatable + RndPollable)`
- `RndText : RndDrawable, RndTransformable`
- `RndEnviron : RndTransformable, RndDrawable`
- `UIComponent : UIPanel, RndDrawable, RndTransformable` (via PanelDir)
- `RndPollAnim : RndAnimatable, RndPollable, Hmx::Object`
