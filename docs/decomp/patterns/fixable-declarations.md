# Fixable Patterns: Declarations

Patterns related to variable declarations, destructors, and initialization.

---

## Explicit Destructor

**Impact:** +37-70%
**Success Rate:** 100%
**Time:** 2 minutes

Define destructor explicitly, even if empty.

### Symptom

objdiff shows ~8 extra instructions (32 bytes) of atexit callback wrapper code.

### Why It Works

Without explicit destructor, compiler generates generic atexit callback wrappers. Explicit empty destructor generates direct destructor call code.

### Fix

```cpp
// In header - add declaration
~GlitchFinder();

// In cpp - add empty body
GlitchFinder::~GlitchFinder() {
}
```

### Real Examples

| Function | Before | After | Delta |
|----------|--------|-------|-------|
| GlitchFinder destructor | 29.4% | 100% | +70.6% |
| ClipDistMap destructor | 61.7% | 99.6% | +37.9% |

---

## Class-Specific Delete Lowering (`POOL_OVERLOAD`)

**Impact:** +12-22% (often fixes multiple call sites)
**Success Rate:** HIGH
**Time:** 5 minutes + rebuild dependents

If a class is allocated from `PoolAlloc`, missing class-specific `operator delete` causes `delete` sites to lower to the wrong sequence (`dtor + global delete`) instead of the target scalar-deleting-dtor path (`??_GClass` -> `PoolFree`).

### Symptom

Destructor call sites show inserts like:

- `bl ??1Class@@...`
- `bl ??3@YAXPAX@Z`

Target instead shows a single call:

- `bl ??_GClass@@QAAPAXI@Z`

This often appears as several insert/delete clusters in unrelated destructors (all `delete Class*` sites), not just in `Class::~Class`.

### Why It Works

`POOL_OVERLOAD(Class, line)` provides class-specific `operator new/delete` using `PoolAlloc/PoolFree`. Once present, MWCC can emit the scalar deleting destructor path that matches the game binary.

### Fix

```cpp
// In the class header
#include "utl/PoolAlloc.h"

class Voice {
public:
    POOL_OVERLOAD(Voice, 0x28);
    Voice(bool, int, bool);
    ~Voice();
    ...
};
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| StreamReceiver360::~StreamReceiver360 | 82.7% | 94.9% | +12.2% | `delete Voice*` sites switched to `??_GVoice` |
| MicXbox::~MicXbox | 74.3% | 96.9% | +22.6% | Same root cause, one-line class fix |
| Voice::`scalar deleting dtor` (`??_GVoice`) | missing/wrong lowering | 99.2% | n/a | Now emitted via class delete path |

### Verification Note

If only headers changed, generated build graphs may fail to rebuild all dependent wrapper units. Confirm object timestamps and force-rebuild affected `.obj` files before trusting objdiff.

---

## Variable Extraction

**Impact:** +1-35%
**Success Rate:** 95%
**Time:** 3 minutes

Store container size or method result in a local variable before use.

### Symptom

objdiff shows different register allocation or extra method calls.

### Why It Works

Storing a value in a temporary variable changes the register allocation sequence. Also, repeated method calls vs cached value generate different code.

### Fix

```cpp
// Before
if (mElements.empty())
    return 0;
MILO_ASSERT((0) <= (display) && (display) < (mElements.size()), 0x74);

// After
size_t size = mElements.size();
if (size == 0)
    return 0;
MILO_ASSERT((0) <= (display) && (display) < (size), 0x74);
```

Also works for chained calls:

```cpp
// Before - chained call
GetArray(key)->Insert(i, value);

// After - extracted variable
DataArray* arr = GetArray(key);
arr->Insert(i, value);
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| UIListLabel::ElementLabel | 64.4% | 99.3% | +34.9% | `size()` to local var |
| TypeProps::InsertArrayValue | 99.09% | 100% | +0.91% | `arr = GetArray(key); arr->Insert()` |
| HamCharacter::SetIKEffectorWeights | 97.6% | 100% | +2.4% | `CharWeightable *ptr = *it; if (ptr)` |
| BlockStatTable::Update | 96.1% | 100% | +3.9% | Extracted `maxSize` temporary |
| RndSoftParticleBuffer ctor | 99.74% | 100% | +0.26% | `w = Width(); h = Height()` in order |
| CharIKSliderMidi::Poll | 99.33% | 100% | +0.67% | `Character::Current()` into temp var |

---

## Iterator Dereference Caching

**Impact:** +5-10%
**Success Rate:** LOW (~20%)
**Time:** 3 minutes

Cache `ObjDirItr` dereferences into a local pointer before using the object in dynamic_casts or method calls.

### Symptom

Multiple `&*it` or `it->` in an `ObjDirItr` loop body. objdiff shows repeated `lwz` from the iterator stack slot and register allocation cascade — the target keeps the pointer in a callee-saved register (e.g. r30), but our code burns temporaries on repeated loads.

### Why It Works

MWCC doesn't CSE (common subexpression eliminate) through `ObjDirItr::operator*()` indirection. Each `&*it` is treated as a fresh load of `mObj` from the iterator struct on the stack. With 3+ uses in a loop body this cascades into register allocation differences.

### When It Helps

This pattern only helps when the cached pointer is used in **diverse ways** across the loop body — dynamic_casts, method calls on the result, AND passing to other functions. Simple loops that only use `&*it` in dynamic_cast arguments don't benefit; the extra local variable changes register allocation for the worse.

Look for loops where the same object is:
1. Cast via `dynamic_cast<>(&*it)`, AND
2. Used for method calls via `it->Method()`, AND
3. Passed as an argument to other functions

### Fix

```cpp
// Before - reloads mObj each time
for (ObjDirItr<RndDrawable> it(dir, true); it != nullptr; ++it) {
    RndMesh *mesh = dynamic_cast<RndMesh *>(&*it);
    if (mesh) {
        const DataNode *prop = it->Property(collidable, false);
        AddCollidable(it, parentProxy, mesh->Showing());
    } else {
        PhysicsVolume *pv = dynamic_cast<PhysicsVolume *>(&*it);
    }
    ObjectDir *proxyProxy = dynamic_cast<ObjectDir *>(&*it);
}

// After - pointer stays in register
for (ObjDirItr<RndDrawable> it(dir, true); it != nullptr; ++it) {
    RndDrawable *drawable = it;  // uses operator T*(), caches mObj
    RndMesh *mesh = dynamic_cast<RndMesh *>(drawable);
    if (mesh) {
        const DataNode *prop = drawable->Property(collidable, false);
        AddCollidable(drawable, parentProxy, mesh->Showing());
    } else {
        PhysicsVolume *pv = dynamic_cast<PhysicsVolume *>(drawable);
    }
    ObjectDir *proxyProxy = dynamic_cast<ObjectDir *>(drawable);
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| PhysicsManager::HarvestCollidables | 86.6% | 97.4% | +10.8% | 4x `&*it` + method calls + function args |
| CharClipSet::SetFrame | 99.0% | 96.8% | **-2.2%** | 3x dynamic_cast only — reverted |
| Character::CalcBoundingSphere | 98.4% | 96.4% | **-2.0%** | dynamic_cast + `it->` only — reverted |
| HamDirector::PoseIconMan | 96.6% | 94.0% | **-2.6%** | 3x dynamic_cast only — reverted |

### Warning

**This pattern can make things worse.** If the loop body only uses `&*it` for dynamic_casts (no mixed method calls or function arg passing), the extra local variable changes register allocation unfavorably. Always test with objdiff before committing.

---

## Boolean Init from Existing Register

**Impact:** +0.5-1%
**Success Rate:** MEDIUM
**Time:** 5 minutes

Initialize a boolean from a value already in a register rather than a literal, when inside a conditional that already tested that value.

### Symptom

Extra `li r3, 0x0` before a conditional that sets the bool. The target reuses a register already holding a truthy value from the enclosing condition.

### Why It Works

MWCC notices when a register already holds a useful value. Writing `bool u2 = i5` instead of `bool u2 = false` lets the compiler skip the initialization and reuse the register. The restructured if/else also eliminates the `else { u2 = true }` branch.

### Fix

```cpp
// Before - generates extra li r3, 0x0
bool u2 = false;
if (!mesh->GetKeepMeshData()) {
    RndMesh *owner = mesh->GetGeomOwner();
    if (mesh != owner) {
        u2 = HasKeepMeshData(owner);
    }
} else {
    u2 = true;
}

// After - reuses r3 which already holds 1 from enclosing if (i5 == 1)
bool u2 = i5;  // i5 is known to be 1 here
if (!mesh->GetKeepMeshData()) {
    RndMesh *owner = mesh->GetGeomOwner();
    if (mesh != owner) {
        u2 = HasKeepMeshData(owner);
    } else {
        u2 = false;
    }
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| PhysicsManager::HarvestCollidables | 96.8% | 97.4% | +0.6% | `bool u2 = i5` inside `if (i5 == 1)` |

---

## Variable Declaration Order

**Impact:** +1-88%
**Success Rate:** 30%
**Time:** 10 minutes

The order of variable declarations affects register allocation.

### Symptom

objdiff shows consistent register swaps (r30/r31, f30/f31) throughout function.

### Why It Works

The compiler assigns registers based on declaration order. Changing declaration order changes the entire register allocation scheme.

### Fix

```cpp
// Before - x,y,z,w order
quat.x = x * 3.051851e-05f;
quat.y = y * 3.051851e-05f;
quat.z = z * 3.051851e-05f;
quat.w = w * 3.051851e-05f;

// After - z,w,y,x order with intermediates
float z_val = z * 3.051851e-05f;
float w_val = w * 3.051851e-05f;
float y_val = y * 3.051851e-05f;
float x_val = x * 3.051851e-05f;
quat.z = z_val;
quat.w = w_val;
quat.y = y_val;
quat.x = x_val;
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| ShortQuat::ToQuat | 11.5% | 99.88% | +88.4% | z,w,y,x order with intermediates |
| MemResizeElem | 99.82% | 100% | +0.18% | `suffixSize` before `prefixSize` |
| MemRealloc | 99.10% | 100% | +0.90% | `sizeInWords = (size + 3) >> 2` separate |

### Warning

**Moving a simple `int x = 0` can break 96% match down to 91%!**

```cpp
// CORRECT (96.1% match) - total declared before pointer
int total = 0;
CampaignEraSongProgress *pEraSongProgress = GetEraSongProgress(name);

// BROKEN (91.8% match) - total moved after pointer
CampaignEraSongProgress *pEraSongProgress = GetEraSongProgress(name);
int total = 0;  // This breaks register allocation!
```

### Success Rate

Only ~30% success rate. If 10+ reordering attempts don't help, the function is likely at its limit due to [Register Allocation](unfixable-compiler.md#register-allocation).

---

## Initializer Literals

**Impact:** Variable
**Success Rate:** HIGH
**Time:** 2 minutes

Use `0` instead of `0.0f` or `false` in initializer lists.

### Symptom

objdiff shows different register allocation in constructor initializer sequence.

### Fix

```cpp
// Before - different literal types
Shuttle::Shuttle() : mMs(0.0f), mEndMs(0.0f), mActive(false), mController(0) {}

// After - all use integer literal 0
Shuttle::Shuttle() : mMs(0), mEndMs(0), mActive(0), mController(0) {}
```

---

## Static Variable Scope

**Impact:** Variable
**Success Rate:** HIGH
**Time:** 3 minutes

Keep static variables in their original scope.

### Symptom

objdiff shows different initialization guard patterns.

### Why It Works

Even small scope changes affect the initialization guard patterns the compiler generates.

### Fix

```cpp
// CORRECT (99.6% match) - static in block scope
{
    static int _x = MemFindHeap("physical");
    MemHeapTracker mem(_x);
    // use mem...
}

// BROKEN (99.5% match) - static moved outside block
static int _x = MemFindHeap("physical");
{
    MemHeapTracker mem(_x);
    // use mem...
}
```

---

## Braced vs Braceless If (Scope Counter)

**Impact:** Fixes all diff_arg/replace for static locals with wrong `?N?`
**Success Rate:** 100%
**Time:** 2 minutes

MSVC's `?N?` scope number in mangled static local names is a sequential counter of `{}` blocks opened before the declaration point. A braced `if` counts as one more scope than a braceless `if`.

### Symptom

Static local variables have the wrong `?N?` scope number in their mangled names. objdiff shows `diff_arg` on every reference to the static (guard variable, atexit destructor, data accesses) with a scope number off by 1 or more:

```
Target: ??__FmyMsg@?6??MyFunc@@QAAX_N@Z@YAXXZ    (scope 6)
Base:   ??__FmyMsg@?7??MyFunc@@QAAX_N@Z@YAXXZ    (scope 7)
```

### Why It Works

MSVC counts every `{}` block opening as a new scope, incrementing a sequential counter that never resets within the function. The counter increases even for blocks that contain no static locals. A braceless `if`/`for`/`while` does NOT increment the counter.

### How to Diagnose

1. Check the `?N?` in the mangled names -- target vs base
2. If base is higher by K, you have K extra `{}` blocks before the static declaration
3. Count all `{}` blocks in the function up to the static declaration point
4. Look for `if` statements with unnecessary braces around single statements

### Fix

Remove braces from single-statement `if`/`for`/`while` blocks that appear before the static declaration:

```cpp
// WRONG - scope ?7? (braces create an extra scope)
if (b2) {
    unk64.insert(std::make_pair(job->unkb0, unk58));
}
// ...
static Message msg("loaded");  // gets scope 7

// CORRECT - scope ?6? (no extra scope from braceless if)
if (b2)
    unk64.insert(std::make_pair(job->unkb0, unk58));
// ...
static Message msg("loaded");  // gets scope 6
```

### Verification Method

Empirically test by removing code blocks and observing the scope number change:
- Remove a braced `if` body entirely: scope drops by N (where N = scopes within that block including templates)
- Remove just the braces from `if (cond) { stmt; }` to `if (cond) stmt;`: scope drops by exactly 1

### Real Examples

| Function | Before | After | Fix |
|---|---|---|---|
| Leaderboards::ReadScoresComplete | ?7? (mismatch) | ?6? (match) | Removed braces from `if (b2) { insert(...) }` |
| MoveDir::ClosestMoveFrame | ?4? (mismatch) | ?3? (match) | Moved struct definition out of if-block |

### Notes

- Template function bodies inlined at the call site also count as scopes (e.g., `Find<T>` inlines ~4 scopes from its body, MILO_FAIL expansion, etc.)
- Function call arguments that involve copy constructors do NOT create scopes (they're function calls, not inline blocks)
- The `vector::clear()` -> `erase()` inline expansion's `if (__first == __last)` check does NOT count as a scope in practice

---

## Static Symbol Order

**Impact:** Variable
**Success Rate:** HIGH
**Time:** 5 minutes

Order of static Symbol declarations must match the original.

### Symptom

objdiff shows `ori` bit flags in wrong order (0x1, 0x2, 0x4...).

### Why It Works

MSVC uses bit flags for static local variable initialization:

```cpp
static Symbol foo("foo");  // Uses bit 0x1
static Symbol bar("bar");  // Uses bit 0x2
static Symbol baz("baz");  // Uses bit 0x4
```

Assembly pattern:
```asm
ori r11, r11, 0x1   ; First static
ori r11, r11, 0x2   ; Second static
ori r11, r11, 0x4   ; Third static
```

### Fix

Reorder static Symbol declarations to match the bit flag order shown in objdiff.

---

## Function Definition Order (TU-Wide Static Guard Counters)

**Impact:** +3-5%
**Success Rate:** 100%
**Time:** 15 minutes

The order of **function definitions** in a translation unit determines the `$S#` static guard counter numbering across all functions in that TU.

### Symptom

objdiff shows `rlwinm.` / `ori` bit mismatches on static local initialization guards — e.g., the function checks bit 2 (`0x4`) but should check bit 9 (`0x200`). The `$S#` variable in mangled names has the wrong number.

### Why It Works

MSVC assigns a sequential counter (`$S1`, `$S2`, ...) to each runtime-initialized static local across the **entire translation unit**, in the order it encounters them during compilation. Each function containing `static Symbol _s(...)` or similar gets its guards numbered based on where it appears relative to other functions with statics.

### Detection

1. Check symbol table for `$S#` guard variables and their addresses
2. Map which function each `$S#` belongs to
3. Compare the expected order vs actual function definition order in source

### Fix

Reorder function definitions to match the original binary's `$S#` numbering:

```cpp
// Object.cpp — correct order based on $S counter analysis:
// $S1 = InitObject, $S2 = OnGetTypeList, ...
// $S9 = SyncProperty, ... $S12 = Handle

void Object::InitObject() { ... }        // $S1
DataNode Object::OnGetTypeList() { ... }  // $S2
// ...
bool Object::SyncProperty() { ... }      // $S9 — MUST come before Handle
// ...
DataNode Object::Handle() { ... }        // $S12 — MUST come last
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| Object::SyncProperty | 96.4% | 99.8% | +3.4% | Moved Handle/BEGIN_HANDLERS after SyncProperty |

### Note: Header Functions Can Steal Guard Slots

Non-inline functions defined in headers that get compiled into the TU will claim `$S#` slots, shifting all subsequent guards. Adding `inline` prevents this:

```cpp
// Before — PropSync_p.h function claims $S1 in Object.obj
bool PropSync(Key<Hmx::Color>& key, DataNode& val, DataArray* prop, int i, PropOp op) { ... }

// After — inline prevents guard slot allocation
inline bool PropSync(Key<Hmx::Color>& key, DataNode& val, DataArray* prop, int i, PropOp op) { ... }
```

---

## Hoist Loop Variable for sret Register Matching

**Impact:** +6%
**Success Rate:** HIGH
**Time:** 5 minutes

Pre-declare a variable before a loop body instead of declaration-initialization inside the loop when the variable receives an sret (struct return) value.

### Symptom

Insert/delete cluster inside a loop body, typically 1-2 extra `lwz`/`stw` instructions. Target uses a register dereference (`lwz r5, 0x0, r3`) while base uses a stack slot (`lwz r5, 0x50, r1`).

### Why It Works

With `Symbol s = a->Sym(i)` (declaration+init inside loop), the compiler copies the sret result to a named stack slot and frees r3, then uses r3 for the next member load. With `s = a->Sym(i)` (assignment to pre-declared variable), the compiler keeps r3 live as a pointer to the Symbol, using r11 as scratch for the next load — matching the target's instruction pattern.

### Fix

```cpp
// Before (93.6%) — declaration inside loop, compiler uses stack slot
for (int i = 3; i < a->Size(); i++) {
    Symbol s = a->Sym(i);  // sret → stack copy → free r3
    if (mSinks)
        mSinks->RemoveSink(obj, s);
}

// After (99.6%) — pre-declared, compiler keeps r3 as pointer
Symbol s;
for (int i = 3; i < a->Size(); i++) {
    s = a->Sym(i);          // sret → r3 stays live
    if (mSinks)
        mSinks->RemoveSink(obj, s);
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| Object::OnRemoveSink | 93.6% | 99.6% | +6.0% | Hoisted `Symbol s` before for loop |

---

## Offset Swap

**Impact:** +1-5%
**Success Rate:** 60%
**Time:** 10 minutes

Symmetric offset swaps between two instructions indicate field access order issues.

### Symptom

objdiff detects `OFFSET_SWAP` pattern showing two instructions with swapped offsets:
- Instruction A: offset `0x4(r31)` in target vs `0x8(r31)` in base
- Instruction B: offset `0x8(r31)` in target vs `0x4(r31)` in base

### Why It Happens

The compiler accesses struct fields in a different order, causing symmetric offset swaps. This can happen due to:
- Different field access order in code
- Different struct layout assumptions
- Compiler optimization choices

### Fix

**1. Change field access order:**

```cpp
// Before - accesses x then y
pos.x = val1;
pos.y = val2;

// After - accesses y then x
pos.y = val2;
pos.x = val1;
```

**2. Check struct layout:**

If offsets consistently differ by the same amount, verify your struct definition matches the original layout. Use `--resolve-offsets` flag to identify which fields are involved.

**3. Try intermediate variables:**

```cpp
// Before - direct assignment
obj->fieldA = src->fieldA;
obj->fieldB = src->fieldB;

// After - cache values first
auto a = src->fieldA;
auto b = src->fieldB;
obj->fieldB = b;
obj->fieldA = a;
```

### Detection

objdiff shows `OFFSET_SWAP` pattern with details like:
```
swapped_offsets: [(instr 15: 0x4 vs 0x8), (instr 23: 0x8 vs 0x4)]
```

---

## sret Return Value Tracing

**Impact:** +7-8%
**Success Rate:** HIGH
**Time:** 10 minutes

When a function returns a struct by value (sret), trace which register is stored to the sret pointer at the end. If it's an unmodified parameter register, the original code returns the parameter directly — not a computed value.

### Symptom

Extra `stw`/`mr` instructions around the sret pointer, or match% stuck despite correct logic. The function computes a value for the return but the target assembly stores a parameter register unchanged.

### Diagnostic

1. Find `stw rN, 0(sret_reg)` at the function epilog
2. Trace rN backward through the function body
3. If rN is set only once from a parameter (`mr rN, paramReg`) and never updated in the body, the function returns the parameter unchanged
4. Restructure to return the parameter directly instead of a computed value

### Fix

```cpp
// Before (88.6%) - computes begin()+idx for return value
if (obj != 0 || mListMode != kObjListNoNull) {
    int idx = it.it ? (it.it - mNodes.begin()) : 0;
    Node newNode(this);
    mNodes.insert(mNodes.begin() + idx, 1, newNode);
    iterator result = begin() + idx;
    Set(result, obj);
    return result;
}
return iterator(const_cast<...>(it.it));

// After (96.2%) - returns it.it directly in both paths
if (obj != 0 || mListMode != kObjListNoNull) {
    int idx = it.it ? (it.it - mNodes.begin()) : 0;
    Node newNode(this);
    mNodes.insert(mNodes.begin() + idx, 1, newNode);
    Set(begin() + idx, obj);
}
return iterator(const_cast<...>(it.it));
```

### Why It Works

The target assembly shows r26 (holding `it.it` from entry) stored to the sret pointer at the end, with no intervening write to r26 in the body. The `begin() + idx` is computed only into r4 as the `Set()` call argument and is never saved. Computing a separate return value generates extra register moves and sret writes that don't appear in the target.

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| ObjPtrVec::insert | 88.6% | 96.2% | +7.6% | Eliminated computed return, used parameter directly |

---

## MILO_NOTIFY vs MILO_NOTIFY_ONCE

**Impact:** +10-35%
**Success Rate:** HIGH
**Time:** 5 minutes

The target binary may use `MILO_NOTIFY_ONCE` instead of `MILO_NOTIFY`. These generate very different code.

### Symptom

objdiff shows:
- Static guard variable (`ori r11, r11, 0x1`) and `atexit` call patterns
- Call to `merged_AddToStrings` or `AddToStrings`
- Static `std::list<String>` initialization code
- Significant size difference (100+ bytes larger for MILO_NOTIFY_ONCE)

### Detection

Look for these patterns in Ghidra decompilation:
```c
if ((DAT_xxxxxxxx & 1) == 0) {
    DAT_xxxxxxxx = DAT_xxxxxxxx | 1;
    // list initialization
    atexit(...);
}
// AddToStrings call
if (cVar != '\0') {
    Notify(...);
}
```

This is the `MILO_NOTIFY_ONCE` pattern with static list guard.

### Fix

```cpp
// Before - generates simple Notify call
MILO_NOTIFY("error: bad value %f", val);

// After - generates static list + guard + AddToStrings pattern
MILO_NOTIFY_ONCE("error: bad value %f", val);
```

### Duplicate Static Declaration

If a function has BOTH an explicit `static std::list<String>` AND uses `MILO_NOTIFY_ONCE`, you'll get two static lists. Remove the explicit declaration - the macro creates its own.

```cpp
// WRONG - two static lists generated
void Func() {
    static std::list<String> _dw;  // Remove this!
    if (error) {
        MILO_NOTIFY_ONCE("error");  // Macro creates its own _dw
    }
}

// CORRECT - only macro's static list
void Func() {
    if (error) {
        MILO_NOTIFY_ONCE("error");
    }
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| BinStream::Read | 63.6% | 95.4% | +31.8% | Removed duplicate static list |
| CharInterest::ComputeScore | 80.8% | 91.6% | +10.8% | Changed MILO_NOTIFY to MILO_NOTIFY_ONCE |

---

## alloca vs _alloca (Intrinsic Stack Allocation)

**Impact:** +10-15%
**Success Rate:** 100%
**Time:** 1 minute

Use `_alloca` (intrinsic) instead of `alloca` (CRT wrapper) when the target binary uses stack probing.

### Symptom

objdiff shows prologue mismatch with `_RtlCheckStack12` in target but not in your build, or vice versa:

```asm
# Target - uses intrinsic _alloca with stack probe
bl _RtlCheckStack12
sub r1, r1, r0

# Your build - uses alloca CRT wrapper
bl alloca
```

### Why It Works

- `alloca()` is a CRT library wrapper that handles stack allocation
- `_alloca()` is a compiler intrinsic that generates inline stack probe code (`_RtlCheckStack12`)
- They produce identical results but very different code paths

### Detection

Look for these clues in objdiff or Ghidra:
- Prologue shows `bl _RtlCheckStack12` call
- Stack frame setup differs significantly at function start
- Match% stuck at ~90% despite correct logic

### Fix

```c
// Before - CRT wrapper (no stack probe)
float *buffer = alloca(n * sizeof(*buffer));

// After - intrinsic with stack probe
float *buffer = _alloca(n * sizeof(*buffer));
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| seed_chase | 89.8% | 100% | +10.2% | Changed `alloca` to `_alloca` |

### Notes

- MSVC's `_alloca` is defined in `<malloc.h>`
- The intrinsic generates stack probing code to handle large allocations safely
- Check the prologue carefully - this pattern is easy to miss

---

## See Also

- [fixable-operators.md](fixable-operators.md) - Assignment patterns
- [unfixable-compiler.md](unfixable-compiler.md#register-allocation) - When reordering doesn't help
