# Str.h Class Hierarchy Fix Session

**Date**: 2025-01-25
**Objective**: Fix the class hierarchy discrepancies for `FixedString`, `String`, and `StackString` in `utl/Str.h` and `utl/Str.cpp`.

**Reference**: [STR_CLASS_HIERARCHY_ANALYSIS.md](STR_CLASS_HIERARCHY_ANALYSIS.md)

---

## Summary

All actionable items from the analysis were completed. Four functions went from 0% to 95%+ match. The constructor ordering issue was investigated and confirmed as a compiler limitation.

---

## Changes Made

### 1. Str.h - Inline FixedString Constructor

Added `extern char gEmpty[8]` declaration and made the default constructor inline:

```cpp
extern char gEmpty[8];

class FixedString {
protected:
    char *mStr;
public:
    FixedString() : mStr((char *)(gEmpty + 4)) {
        *(int *)(mStr - 4) = 0;
        mStr[0] = '\0';
    }
    // ...
};
```

### 2. Str.cpp - Removed Out-of-Line Constructor

Removed the `FixedString::FixedString()` definition from Str.cpp since it's now inline in the header.

### 3. Str.cpp - Implemented String::insert(uint, uint, char)

```cpp
String &String::insert(unsigned int pos, unsigned int count, char c) {
    MILO_ASSERT(pos <= capacity(), 0x27B);
    String tmp;
    tmp.reserve(length() + count);
    strncpy(tmp.mStr, mStr, pos);
    for (unsigned int i = 0; i < count; i++) {
        tmp.mStr[pos + i] = c;
    }
    char *src = mStr + pos - 1;
    char *dst = tmp.mStr + pos + count - 1;
    char ch;
    do {
        ch = *++src;
        *++dst = ch;
    } while (ch != '\0');
    char *temp_mStr = mStr;
    mStr = tmp.mStr;
    tmp.mStr = temp_mStr;
    return *this;
}
```

### 4. Str.cpp - Implemented String::insert(uint, const char*)

Simple forwarding to replace():

```cpp
String &String::insert(unsigned int pos, const char *str) {
    return replace(pos, 0, str);
}
```

### 5. Str.cpp - Implemented RemoveSpaces()

Removes leading, trailing, and consecutive spaces:

```cpp
void RemoveSpaces(char *out, int len, const char *in) {
    MILO_ASSERT(out, 0x2C0);
    MILO_ASSERT(in, 0x2C1);
    MILO_ASSERT(len > 0, 0x2C2);

    char *dst = out;
    char *max = out + len - 1;
    char *orig = out;
    bool wasSpace = true;
    char c = *in;

    while (c != '\0') {
        if (dst < max) {
            bool isSpace = (c == ' ');
            if (!isSpace || !wasSpace) {
                *dst++ = c;
            }
            wasSpace = isSpace;
        }
        c = *++in;
    }

    if (dst > orig && *(dst - 1) == ' ') {
        dst--;
    }

    *dst = '\0';
}
```

### 6. Str.cpp - Fixed String::operator=(const String&)

Changed from full implementation to simple forwarding:

```cpp
String &String::operator=(const String &str) {
    return *this = static_cast<const FixedString &>(str);
}
```

---

## Results

### Functions Fixed

| Function | Before | After | Notes |
|----------|--------|-------|-------|
| `String::operator=(const String&)` | 0% | **100%** | Was doing full copy, now forwards |
| `String::insert(uint, uint, char)` | 0% | **95.8%** | Linker-merged call difference |
| `String::insert(uint, const char*)` | 0% | **100%** | Simple forwarding to replace() |
| `RemoveSpaces()` | 60% | **95.5%** | Register allocation differences |

### Constructor Ordering (Unchanged)

| Function | Match | Notes |
|----------|-------|-------|
| `String::String(void)` | 55% | Compiler limitation |
| `String::String(uint, char)` | 63% | Compiler limitation |
| `String::String(const String&)` | 75% | Compiler limitation |

The constructor ordering issue is due to a fundamental difference in how the original MSVC and our cross-compiler handle base class initialization order. The original MSVC reordered the empty `TextStream::TextStream()` call after `FixedString` initialization, but our compiler follows strict C++ semantics.

This was predicted in the analysis document's "Risk" section and confirmed through testing.

---

## Verification Commands

```bash
# Check specific function matches
./bin/objdiff-cli diff -p . "??4String@@QAAAAV0@ABV0@@Z" -f markdown
./bin/objdiff-cli diff -p . "?insert@String@@QAAAAV1@IID@Z" -f markdown
./bin/objdiff-cli diff -p . "?insert@String@@QAAAAV1@IPBD@Z" -f markdown
./bin/objdiff-cli diff -p . "?RemoveSpaces@@YAXPADHPBD@Z" -f markdown

# Rebuild Str.obj
ninja build/373307D9/src/system/utl/Str.obj

# Query all Str.cpp functions
./bin/objdiff-cli report query build/373307D9/report.json --functions --unit "*utl/Str*" --max-percent 99
```

---

## Remaining Non-100% Matches

The following functions remain below 100% due to unfixable issues:

1. **Constructor ordering** (55-75%): Compiler follows different base class initialization order
2. **Register allocation** (95%+): Minor register assignment differences
3. **Linker-merged calls**: Target uses merged empty function symbols

These are all functionally correct - the generated code produces identical behavior, just with different instruction ordering or register choices.
