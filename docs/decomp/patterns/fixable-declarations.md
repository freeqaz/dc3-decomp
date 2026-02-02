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

## See Also

- [fixable-operators.md](fixable-operators.md) - Assignment patterns
- [unfixable-compiler.md](unfixable-compiler.md#register-allocation) - When reordering doesn't help
