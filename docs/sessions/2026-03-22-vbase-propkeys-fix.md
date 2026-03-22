# Virtual Base PropKeys Fix — Session 2026-03-22

**Problem**: Camera shots stuck on intro during gameplay
**Root cause**: Virtual base pointer offset mismatch in PropKeys target comparison
**Fix**: `SameObject()` helper using `dynamic_cast<void*>` in PropAnim.cpp
**Impact**: Zero decomp % change — identical behavior on PPC

---

## The Bug

`HamDirector` inherits from `RndPollable` and `RndDrawable`, both of which virtually inherit from `Hmx::Object`. On Itanium ABI (x86_64), accessing the same object through different virtual base paths yields different pointer values:

```
HamDirector* this          = 0x7db41c1f5480  (via Hmx::Object vbase)
HamDirector* via RndPollable = 0x7db41c1f5c00  (offset +0x780)
```

Only ONE HamDirector is constructed (verified with ctor logging). The `+0x780` offset is the virtual base table adjustment when the Hmx::Object pointer is obtained through a different inheritance path during deserialization.

On MSVC PPC, virtual base resolution always produces the same address for the same object. On Itanium ABI, it doesn't — `dynamic_cast<void*>` is needed to get the canonical most-derived address.

## Where It Broke

`RndPropAnim::GetKeys(obj, prop)` and `FindKeys(obj, prop)` compared `cur->Target() == obj` using raw pointer comparison. The song.anim PropKeys stored `mTarget` as an `Hmx::Object*` obtained via one virtual base path. `TheHamDirector` (passed as `obj`) was accessed via a different path. The pointers differed by 0x780 → comparison failed → GetKeys returned null → shot/clip/move keyframes never matched → camera stuck.

## The Fix

Added `SameObject()` helper to `PropAnim.cpp`:

```cpp
static inline bool SameObject(const Hmx::Object *a, const Hmx::Object *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return dynamic_cast<const void *>(a) == dynamic_cast<const void *>(b);
}
```

Applied to `GetKeys`, `FindKeys`, and `GetNumKeys`. On PPC, `dynamic_cast<void*>` returns the same address as the raw pointer (no vbase offset mismatch), so behavior is identical.

## Systemic Concern

This virtual base offset mismatch affects ANY `Hmx::Object*` identity comparison in the codebase where the two pointers were obtained through different inheritance paths. The Milo engine uses raw pointer comparison extensively for:

- PropKeys target matching (fixed)
- ObjRef ring membership checks
- Object identity in hash tables / find operations
- Message dispatch target matching

An audit should find other sites where `SameObject` (or equivalent) is needed.

## Verification

Camera shots now cycle correctly during gameplay:
```
beat=24.71   mCurShot=0x...3580  (changed from intro)
beat=49.84   mCurShot=0x...5880
beat=71.07   mCurShot=0x...5180
beat=96.37   mCurShot=0x...2080
beat=118.61  mCurShot=0x...2e80
beat=141.20  mCurShot=0x...5180
beat=163.88  mCurShot=0x...2080
```

## Files Changed

| File | Change |
|------|--------|
| `src/system/rndobj/PropAnim.cpp` | `SameObject()` helper, applied to `GetKeys`, `FindKeys`, `GetNumKeys` |

## Commits

```
532643bf8 native: fix camera shot cycling via PropKeys virtual base comparison
3ea7c6676 fix: remove debug fprintf from SameObject (broke PPC build)
```
