# Str.h Class Hierarchy Analysis

**Date**: 2026-01-25
**Objective**: Understand and potentially fix the class hierarchy discrepancies for `FixedString`, `String`, and `StackString` in `utl/Str.h` and `utl/Str.cpp`.

---

## Executive Summary

The DC3 string classes (`FixedString`, `String`, `StackString<N>`) use a **capacity-prefixed buffer** design that differs significantly from RB3. The main matching issues stem from:

1. **Constructor ordering**: The compiler generates code with TextStream initialization before FixedString initialization, but the target binary shows the reverse order
2. **Missing default constructor**: `FixedString::FixedString()` doesn't exist as a symbol in the original binary - it's inlined
3. **MSVC optimizer reordering**: Because `TextStream::TextStream()` is empty, MSVC aggressively reorders the FixedString initialization to occur before the TextStream constructor call

---

## Current Match Status

| Function | Match % | Status |
|----------|---------|--------|
| `FixedString::FixedString(char*, int)` | 100% | Matching |
| `FixedString` methods (find, compare, etc.) | 100% | Matching |
| `String::String(void)` | **55%** | Constructor ordering issue |
| `String::String(unsigned int, char)` | **63%** | Constructor ordering issue |
| `String::String(const String&)` | **75%** | Constructor ordering issue |
| `String::~String()` | 93% | Nearly matching |
| `String::operator=(const String&)` | **0%** | Not implemented |
| `String::insert(...)` | **0%** | Not implemented |
| Most other String methods | 100% | Matching |

---

## Class Hierarchy: DC3 vs RB3

### RB3 Design (simpler)
```cpp
class String : public TextStream {
    unsigned int mCap;  // capacity as member
    char *mStr;         // string pointer
};
// No FixedString class exists
```

### DC3 Design (capacity-prefixed buffer)
```cpp
class FixedString {
protected:
    char *mStr;  // points to buffer + 4
    // Capacity stored at mStr - 4 (before the string data)

public:
    unsigned int capacity() const { return *(unsigned int*)(mStr - 4); }
};

class String : public TextStream, public FixedString {
    // Inherits mStr from FixedString
    // vtable at offset 0x0 (from TextStream)
    // mStr at offset 0x4 (from FixedString)
};

template<int N>
class StackString : public TextStream, public FixedString {
    char mStack[N];  // inline buffer at offset 0x8
};
```

### Object Layout Verification

From RTTI symbols in `ham_xbox_r.map`:
```
??_R1A@?0A@EA@String@@8         - String at offset 0 (first base)
??_R13?0A@EA@FixedString@@8     - FixedString at offset 0x4 (second base)
??_7String@@6B@                 - String vtable exists
```

This confirms:
- **TextStream** is first base class (provides vtable at offset 0x0)
- **FixedString** is second base class (provides mStr at offset 0x4)

---

## The Constructor Ordering Problem

### Target Assembly (`String::String(void)`)
```asm
; 1. FixedString initialization FIRST
addi r10, r10, 0x4          ; r10 = gEmpty + 4
stw r10, 0x4(r3)            ; this->mStr = gEmpty + 4
stw r11, gEmpty@l(r9)       ; gEmpty[0] = 0 (capacity)
stb r11, 0x0(r10)           ; *mStr = '\0'

; 2. TextStream constructor SECOND
bl merged_TextStreamCtorDtor

; 3. Vtable assignment
stw r11, 0x0(r31)           ; this->vtable = String::vtable
```

### Our Compiled Code
```asm
; 1. TextStream constructor FIRST
bl ??0TextStream@@QAA@XZ

; 2. FixedString initialization SECOND
addi r10, r10, 0x4
stw r10, 0x4(r31)
stw r11, gEmpty@l(r9)
stb r11, 0x0(r9)

; 3. Vtable assignment
stw r10, 0x0(r31)
```

### Why This Happens

Standard C++ requires base class constructors to be called in **declaration order**:
```cpp
class String : public TextStream, public FixedString
//             ^^ first         ^^ second
```

So `TextStream::TextStream()` is called before `FixedString::FixedString()`.

However, the target shows the **opposite order**. This is possible because:

1. **`TextStream::TextStream()` is empty** - it does nothing
2. **`FixedString::FixedString()` is inlined** - no separate symbol exists
3. **MSVC optimizer reorders** the operations since there's no dependency

---

## Key Evidence

### FixedString Default Constructor NOT in Binary

Searching the map file:
```bash
$ grep "FixedString@@QAA@XZ" ham_xbox_r.map
# NO RESULTS - only FixedString(char*, int) exists
```

Only `FixedString::FixedString(char*, int)` appears:
```
0005:0049d9d8  ??0FixedString@@QAA@PADH@Z  827cd9d8  utl:Str.obj
```

This proves the default constructor is **inlined into callers**.

### TextStream Constructor is Empty

```cpp
// TextStream.cpp
TextStream::TextStream() {}
```

This allows the compiler to reorder without observable side effects.

### Merged Functions

The map shows `TextStream::TextStream()` merged with multiple StackString destructors:
```
0005:00269e60  ??0TextStream@@QAA@XZ               82599e60
0005:00269e60  ??1?$StackString@$0MBI@@@UAA@XZ     82599e60  (same address!)
0005:00269e60  ??1?$StackString@$0CAA@@@UAA@XZ     82599e60  (same address!)
```

They're all essentially no-ops, merged to the same code.

---

## Potential Fixes

### Option 1: Make FixedString Constructor Inline (Recommended)

Move the default constructor from `Str.cpp` to `Str.h`:

**Current (Str.cpp):**
```cpp
FixedString::FixedString() : mStr((char *)(gEmpty + 4)) {
    *(int *)(mStr - 4) = 0;
    mStr[0] = '\0';
}
```

**Proposed (Str.h):**
```cpp
class FixedString {
public:
    FixedString() : mStr((char *)(gEmpty + 4)) {
        *(int *)(mStr - 4) = 0;
        mStr[0] = '\0';
    }
    // ...
};
```

This would:
- Eliminate the separate `FixedString::FixedString()` symbol (matches binary)
- Allow compiler to inline into `String::String()`
- Potentially enable the optimizer to reorder operations

**Risk**: Modern compilers might still follow strict C++ semantics and not reorder.

### Option 2: Explicit String Constructor Body

If Option 1 doesn't match, we could try manual initialization:

```cpp
String::String() {
    // Manually initialize mStr before TextStream would normally be called
    // This is non-standard but might match MSVC's codegen
    mStr = (char *)(gEmpty + 4);
    *(int *)(mStr - 4) = 0;
    mStr[0] = '\0';
}
```

**Risk**: This duplicates FixedString's logic and doesn't match C++ semantics.

### Option 3: Accept ~55% Match

Given that:
- The difference is purely register allocation/instruction ordering
- The logic is semantically identical
- This is a constructor called frequently but with correct behavior

We could accept the current match and focus elsewhere.

---

## StackString Template Instantiations

Found in the codebase:

| Size | Source Files |
|------|--------------|
| 32 | DataNode.cpp |
| 100 | Msg.cpp |
| 128 | HamCharacter.cpp |
| 256 | Utl.cpp, Debug.cpp |
| 512 | Debug.cpp |
| 1024 | Shader.cpp |
| 2048 | DataFunc.cpp |
| 3096 | Debug.cpp |
| 4096 | Debug.cpp |

Each has a separate vtable in the binary.

---

## Functions Needing Implementation

| Function | Size | Notes |
|----------|------|-------|
| `String::operator=(const String&)` | 20 bytes | Very small, likely forwards to another operator |
| `String::insert(uint, uint, char)` | 308 bytes | Full implementation needed |
| `String::insert(uint, const char*)` | 12 bytes | Small wrapper |
| `RemoveSpaces` | 340 bytes | Stubbed, needs full implementation |

---

## Recommendations

1. **Try Option 1 first**: Make `FixedString()` inline in the header. This is the most likely solution based on the evidence.

2. **Implement missing functions**: `String::insert()` and `RemoveSpaces()` have 0% match and need implementation.

3. **Consider partial acceptance**: The ~55% match on constructors may be the best achievable given compiler differences. The code is functionally correct.

4. **Document the design difference**: DC3's capacity-prefixed buffer design is more space-efficient than RB3's approach but creates matching challenges.

---

## Test Plan

1. Move `FixedString::FixedString()` to header as inline
2. Rebuild and check `String::String()` match percentage
3. If no improvement, try compiler flags or pragma ordering hints
4. Implement `String::insert()` methods
5. Implement `RemoveSpaces()`

---

## References

- Target assembly: `build/373307D9/asm/system/utl/Str.s`
- Map file: `orig/373307D9/ham_xbox_r.map`
- RB3 reference: `~/code/milohax/rb3/src/system/utl/Str.h`
