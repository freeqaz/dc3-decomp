# Fixable Patterns: Casting

Patterns related to type casting, float/double precision, and attributes.

---

## Explicit Float Cast

**Impact:** +35%
**Success Rate:** HIGH
**Time:** 5 minutes

Use explicit `(float)` casts and `floor()` (not `std::floor()`) for matching.

### Symptom

objdiff shows different function calls or extra precision conversion instructions.

### Why It Works

- `std::floor()` and `floor()` may resolve to different symbols
- Without explicit cast, compiler may promote to double

### Fix

```cpp
// Before
f1 = std::floor((f1 - mBStart) * mSamplesPerBeat + 0.5f);

// After
f1 = floor(((f1 - mBStart) * (float)mSamplesPerBeat) + 0.5f);
```

### Real Example

| Function | Before | After | Delta |
|----------|--------|-------|-------|
| ClipDistMap::CalcHeight | 64.4% | ~99% | +35% |

---

## noreturn Attribute

**Impact:** +38.5%
**Success Rate:** 100%
**Time:** 2 minutes

Add `__declspec(noreturn)` to functions that never return.

### Symptom

objdiff shows dead code after `exit()` or `abort()` calls that shouldn't be there.

### Why It Works

Tells the compiler that the function never returns, allowing it to eliminate dead epilogue code (stack cleanup, return instructions).

### Fix

```cpp
// In header or before use
__declspec(noreturn) void exit(int);

// Or for custom functions:
__declspec(noreturn) void FatalError(const char* msg);
```

### Real Example

| Function | Before | After | Delta |
|----------|--------|-------|-------|
| error_exit (jerror.c) | 61.5% | 100% | +38.5% |

---

## Float/Double Separation

**Impact:** +80%
**Success Rate:** 95%
**Time:** 10 minutes

Explicitly separate float and double operations with intermediate variables.

### Symptom

objdiff shows FPU register spillage or unexpected precision conversions.

### Why It Works

Mixed float/double operations cause FPU register spillage between precision modes. Separating them keeps operations in dedicated register sets.

### Fix

```cpp
// Before - mixed precision
float vorbis_fromdBlook(float a) {
    int i = vorbis_ftoi(a * ((float)INVSQ_LOOKUP_SZ) * 8.0f + 0.5f);
    // ... mixed operations
}

// After - separated precision
float vorbis_fromdBlook(float a) {
    float a8 = a * 8.0f + 0.5f;           // explicit float ops
    double dbl_val = 0.5 - (double)a8;     // explicit conversion
    // Separate float/double operations
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| vorbis_fromdBlook | 16.8% | 97.7% | +80.9% | Separated float and double intermediate vars |
| UpdateCache (RndShaderMgr) | 21.7% | 97.6% | +75.9% | Declared temp float vars for each load |
| PropKeys::Print | 99.95% | 100% | +0.05% | `frame = 0.0f` instead of `frame = 0` |

---

## sizeof() Signedness

**Impact:** Variable
**Success Rate:** HIGH
**Time:** 3 minutes

Cast `sizeof()` to `(int)` when used in signed arithmetic.

### Symptom

objdiff shows `srwi` (unsigned shift) vs `srawi+addze` (signed shift) mismatch.

### Why It Works

`sizeof()` returns `size_t` (unsigned), which promotes the entire expression to unsigned. This affects division codegen:
- **Unsigned division** by power of 2: `srwi` (shift right word immediate)
- **Signed division** by power of 2: `srawi` + `addze` (arithmetic shift + carry correction)

### Fix

```cpp
// Before - unsigned division, generates srwi
return (NumVerts() * 0x50 + NumFaces() * sizeof(Face)) / 1024;

// After - signed division, generates srawi + addze
return (NumVerts() * 0x50 + NumFaces() * (int)sizeof(Face)) / 1024;
```

### Detection

Look for `srwi` vs `srawi`/`addze` mismatch in objdiff. If return type is `int` but code generates `srwi`, cast the `sizeof()` to `int`.

---

## Data Type Sizing

**Impact:** Variable
**Success Rate:** HIGH
**Time:** 5 minutes

Member variable types affect store instruction selection.

### Symptom

objdiff shows `stw` (32-bit store) vs `sth` (16-bit store) mismatch.

### Fix

```cpp
// Before - stw instruction for mPort
class NetAddress {
    unsigned int mIP;
    unsigned int mPort;  // Wrong! Generates stw
};

// After - sth instruction for mPort
class NetAddress {
    unsigned int mIP;
    unsigned short mPort;  // Correct! Generates sth
};
```

### Detection

Check for `stw` vs `sth` or `stb` differences. This indicates the member type size is wrong.

---

## Wrapper Struct Padding

**Impact:** Variable
**Success Rate:** HIGH
**Time:** 5 minutes

Check if wrapper structs duplicate existing padding.

### Symptom

Struct size mismatch causing template/array offset issues.

### Why It Works

Some types already have internal padding. Wrapping them with additional padding creates wrong sizes.

### Fix

```cpp
// Vector3 already has internal padding
class Vector3 {
    float x, y, z;
    u32 PAD;  // Already 16 bytes total
};

// Before - wrapper adds duplicate padding!
struct Vector3Pad {
    Vector3 v;    // 16 bytes
    float pad;    // 4 bytes extra - WRONG!
};  // 20 bytes - causes wrong struct sizes downstream

// After - use the type directly
typedef Vector3 Vector3Pad;  // 16 bytes
```

### Detection

If a struct has wrong size affecting templates/arrays, check if any member types already have internal padding.

---

## See Also

- [fixable-comparison.md](fixable-comparison.md) - Signedness comparison patterns
- [unfixable-compiler.md](unfixable-compiler.md) - When casting fixes don't work
