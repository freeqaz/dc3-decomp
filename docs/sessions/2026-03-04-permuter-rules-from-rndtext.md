# Permuter Rules Derived from RndText Work

**Date**: 2026-03-04
**Context**: While working on RndText functions (UpdateText, DrawBlacklight, QueueBlacklightPacket, BuildFontMaps), several recurring mismatch patterns emerged that could be automated as permuter rules.

## Proposed Rules

### 1. `milo_str_conv` — Add .Str() to Symbol args in MILO macros

**Priority: High** — Most common, purely mechanical, high hit rate.

**Problem**: `ClassName()` returns `Symbol`. When passed to `MILO_NOTIFY`/`MILO_WARN`/`MILO_FAIL`, it instantiates `MakeString<...,Symbol,...>` instead of `MakeString<...,PBD,...>` (PBD = `char const*`). The mangled template name changes, causing a `bl` target mismatch.

**Fix**: Add `.Str()` to convert Symbol → `const char*` before passing to MILO macros.

**Detection**: Find MILO macro calls (`MILO_NOTIFY`, `MILO_WARN`, `MILO_FAIL`, `MILO_ASSERT`) where an argument is a call returning `Symbol` (primarily `ClassName()`, `Name()` on Symbol-returning methods).

**Example**:
```cpp
// Before (wrong MakeString template):
MILO_NOTIFY("...%s...", PathName(this), ClassName(), fontName);
// After (correct template):
MILO_NOTIFY("...%s...", PathName(this), ClassName().Str(), fontName);
```

**Scope**: Any function calling MILO macros with Symbol-typed arguments. The objdiff pattern `MAKESTRING_TEMPLATE_MISMATCH` already flags these.

---

### 2. `access_specifier_swap` — Try public↔protected on called functions

**Priority: Medium** — Less common but causes `bl` target mismatches that look confusing.

**Problem**: MSVC mangles access level into the symbol name. `QAAX` = public `void` thiscall, `IAAX` = protected. If a function is declared `protected` but the target has it `public` (or vice versa), the `bl` target differs.

**Fix**: Move the function declaration between `public:` and `protected:` sections in the header.

**Detection**: In the function call diff, look for pairs where one is `@@QAAX` and the other is `@@IAAX` (or `@@QAA` vs `@@IAA` for non-void). Same function name but different access specifier.

**Example**:
```
Target: bl ?WrapText@RndText@@QAAXPBG...  (public)
Base:   bl ?WrapText@RndText@@IAAXPBG...  (protected)
```

**Implementation note**: This requires modifying the header, not just the .cpp. The permuter would need to identify which header declares the function and try moving it. May be better as a standalone fixer tool than a permuter pattern.

---

### 3. `milo_call_merge` — Merge duplicate MILO macro calls via goto

**Priority: Medium** — Saves ~10-20 instructions when applicable.

**Problem**: Multiple identical `MILO_NOTIFY`/`MILO_WARN` calls with the same format string but different argument sources. The original code used a single call reached via goto, but the decomp duplicated the call for readability.

**Fix**: Extract the shared MILO call to a goto label, with if/else chains setting variables before jumping.

**Detection**: Two or more MILO macro calls with identical format strings in the same function. The function call diff shows `PathName` or `MakeString` count differences (e.g., "target 1, base 2").

**Example**:
```cpp
// Before (2 calls, 2x PathName):
if (font == 0) {
    MILO_NOTIFY("...%s %s...(%s)", PathName(this), ClassName().Str(), "NULL");
    goto do_ellipsis;
}
if (font->ClassName() != RndFont::StaticClassName()) {
    MILO_NOTIFY("...%s %s...(%s)", PathName(this), ClassName().Str(), font->Name());
    goto do_ellipsis;
}

// After (1 call, 1x PathName):
const char *fontName;
if (font == 0) {
    fontName = "NULL";
} else if (font->ClassName() != RndFont::StaticClassName()) {
    fontName = font->Name();
} else {
    continue;
}
MILO_NOTIFY("...%s %s...(%s)", PathName(this), ClassName().Str(), fontName);
goto do_ellipsis;
```

---

### 4. `vector_size_capacity` — Try capacity() instead of size()

**Priority: Low** — Rare, but impactful when it hits.

**Problem**: stlport vector stores `{_start, _finish, _end_of_storage}`. `size()` reads `_finish` (offset 0x4 from vector base), `capacity()` reads `_end_of_storage` (offset 0x8). Target code sometimes uses capacity where our code uses size.

**Detection**: Look for `lwz` offset mismatches of +4 on vector member accesses. Typically in resize/reserve guard patterns.

**Example**:
```cpp
// Before:
u32 cursize = pool.size();
// After:
u32 cursize = pool.capacity();
```

---

### 5. `objptr_access` — Try .Ptr() vs implicit conversion vs operator->

**Priority: Low** — Generates different intermediate code, but hard to detect automatically.

**Problem**: `ObjPtr<T>` has three ways to get the raw pointer:
- `objptr` (implicit `operator T*()`)
- `objptr.Ptr()` (explicit member function)
- Through `operator->()` for method calls

Each can generate slightly different code, especially around null checks and intermediate address computation.

**Detection**: Delete clusters of 4-6 instructions involving ObjPtr offset patterns (+0x34 for mFont in Style, +0xC for mObject within ObjPtr). Look for `addi rX, rY, 0x34` / `lwz rZ, 0xC, rX` patterns.

---

## Implementation Priority

1. **`milo_str_conv`** — Implement first. Highest hit rate, purely mechanical, already detected by objdiff's `MAKESTRING_TEMPLATE_MISMATCH` pattern.
2. **`milo_call_merge`** — Good bang for buck. Detectable via function call count diffs.
3. **`access_specifier_swap`** — Probably better as a standalone fixer than permuter pattern.
4. **`vector_size_capacity`** and **`objptr_access`** — Low priority, rare cases.

## Related Work

- `MAKESTRING_TEMPLATE_MISMATCH` pattern in objdiff already flags rule #1 candidates
- The permuter's existing `logswap` pattern (MILO_NOTIFY↔MILO_WARN) is related but orthogonal
- Access specifier detection could be added to `run_objdiff`'s function call diff analysis
