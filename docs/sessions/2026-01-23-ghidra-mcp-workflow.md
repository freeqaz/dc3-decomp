# Session: Ghidra MCP Integration & Parallel Agent Workflow

**Date:** 2026-01-23
**Focus:** Integrating pyghidra-mcp for decompilation, testing new workflow with parallel Opus agents

---

## Summary

Integrated Ghidra via pyghidra-mcp for binary analysis. Demonstrated workflow using map file for symbol lookup + Ghidra decompilation + objdiff for matching. Ran parallel Opus agents to investigate near-match functions.

## Key Wins

| Function | Before | After | Pattern |
|----------|--------|-------|---------|
| `StrHash` | 84.7% | **100%** | `const unsigned char*` loop |
| `Pool::Free` | 83% | **100%** | Missing free-list update (bug fix!) |
| `Pool::Alloc` | ~85% | **100%** | Follow free-list pointer |
| `vector<DetectFrame>::~vector` | 99.9% | **100%** | Vector3Pad duplicate padding fix |

### StrHash Fix
Changed loop pointer from `const char*` to `const unsigned char*` to avoid sign extension:
```cpp
// Before - generates extsb (extend sign byte)
for (const char *p = str; *p != '\0'; p++) {

// After - no sign extension, uses cmplwi (unsigned compare)
for (const unsigned char *p = (const unsigned char *)str; *p != '\0'; p++) {
```

**New Pattern Discovered:** String iteration for hashing/byte operations often uses unsigned char to avoid sign extension overhead on PowerPC.

### Pool::Free & Pool::Alloc Fixes
Both functions had incomplete free-list logic:
```cpp
// Pool::Alloc - was setting mFree = nullptr, should follow pointer
mFree = *(char **)ptr;  // Follow free-list link

// Pool::Free - was missing the head update
*(void **)v = mFree;    // Store old head in freed block
mFree = (char *)v;      // Update head to freed block (WAS MISSING)
```

This was an actual **bug fix** - freed memory wasn't being properly recycled.

### vector<DetectFrame>::~vector Fix
The `Vector3Pad` struct had duplicate padding - `Vector3` already has internal 4-byte pad:
```cpp
// Before - 20 bytes per element (wrong!)
struct Vector3Pad {
    Vector3 v;    // 16 bytes (already padded)
    float pad;    // 4 bytes extra - DUPLICATE!
};

// After - 16 bytes per element (correct)
typedef Vector3 Vector3Pad;
```

This caused `DetectFrame` to be 0x4f4 bytes instead of 0x430 bytes. Fixed multiple template instantiations.

## Ghidra MCP Workflow

### Setup
1. XEXLoaderWV extension properly loads Xbox 360 XEX as PowerPC:BE
2. Binary is stripped - no debug symbols in Ghidra
3. Map file `orig/373307D9/ham_xbox_r.map` has 119K lines of symbols

### Workflow Steps
```bash
# 1. Find function address from map file
grep "FunctionName" orig/373307D9/ham_xbox_r.map
# Output: 0005:002027e8  ?FastSin@@YAMM@Z  825327e8 f  math:Trig.obj

# 2. Decompile in Ghidra MCP (use address from map)
mcp__pyghidra__decompile_function("/default.xex-997567", "0x825327e8")

# 3. Get cross-references
mcp__pyghidra__list_cross_references("/default.xex-997567", "0x825327e8")

# 4. Compare with objdiff
objdiff-cli diff -p . "FastSin" -f json --include-instructions
```

### Example: FastSin Decompilation
Ghidra output showed lookup table optimization:
```c
double FUN_825327e8(double param_1) {
  if (param_1 < 0.0) {
    return -(double)*(float *)(&DAT_82f621a0 +
           ((int)-(float)(param_1 * 40.7436637878418 - 0.49998998641967773) & 0xffU) * 8);
  }
  return (double)*(float *)(&DAT_82f621a0 +
         ((int)(param_1 * 40.7436637878418 + 0.49998998641967773) & 0xffU) * 8);
}
```
- 40.74... = 256/2π (table has 256 entries)
- Found 23 callers via cross-references

## Functions Analyzed

### Confirmed AT_LIMIT

| Function | Match | Root Cause |
|----------|-------|------------|
| `Box::Volume` | 98.83% | Instruction scheduling (Y/Z load order) |

### Already at 100% (stale report data)

| Function | Notes |
|----------|-------|
| `FxSendMeterEffect::ChannelData` | Was 98.9%, now 100% |
| `GestureMgr::GetActiveSkeletonIndex` | Was 90%, now 100% |

### Fixed This Session

| Function | Before | After | Fix |
|----------|--------|-------|-----|
| `vector<DetectFrame>::~vector` | 99.9% | **100%** | Vector3Pad had duplicate padding |

### Already at 100% (stale report)

| Function | Notes |
|----------|-------|
| `BaseSkeleton::BoneLength` | Manual sqrt(zz+yy+xx) decomposition |
| `RatioToDb` | Static const float zero pattern |

### Confirmed AT LIMIT

| Function | Match | Root Cause |
|----------|-------|------------|
| `PresenceMgr::GetPresenceMode` | 99.5% | Linker-merged functions, symbol naming |

## Documentation Updates

Updated tool documentation with Ghidra MCP integration:

### docs/tools/INDEX.md
- Added Ghidra + pyghidra-mcp to tools table
- Added "Symbol Lookup (Map File)" section
- Moved decomp-permuter to "Archived Tools" (C only, not C++)

### docs/tools/GHIDRA.md
- Added map file as primary symbol source
- Complete MCP tool list (decompile, xrefs, callgraph, etc.)
- Workflow examples for decompiling unknown functions
- Troubleshooting table

## Patterns Catalog Update

### Unsigned Char for String Iteration
```cpp
// Use unsigned char to avoid sign extension (extsb instruction)
for (const unsigned char *p = (const unsigned char *)str; *p != '\0'; p++)
```
- Generates `cmplwi` (unsigned compare) vs `cmpwi` (signed)
- Common in hash functions, string processing

### Free-List Pattern
```cpp
// Alloc: follow the link pointer
void *ptr = mFree;
mFree = *(char **)ptr;  // NOT mFree = nullptr
return ptr;

// Free: insert at head
*(void **)v = mFree;    // Store old head in block
mFree = (char *)v;      // Update head
```

## Session Statistics

| Metric | Value |
|--------|-------|
| Functions fixed | **4** (StrHash, Pool::Free, Pool::Alloc, vector destructor) |
| Confirmed AT_LIMIT | 2 (Box::Volume, GetPresenceMode) |
| Already matching | 4 (ChannelData, GetActiveSkeletonIndex, BoneLength, RatioToDb) |
| Docs updated | 3 (INDEX.md, GHIDRA.md, TECHNICAL_NOTES.md) |
| Progress improvement | Milo Engine: 53.86% → 53.91% (+17 functions) |

## Patterns Discovered

### 1. Unsigned Char for String Iteration
Avoids sign extension (`extsb` instruction):
```cpp
for (const unsigned char *p = (const unsigned char *)str; *p; p++)
```

### 2. Static Const for Float Comparisons
Forces memory load instead of immediate:
```cpp
static const float zero = 0.0f;
return (ratio <= zero) ? -96.0f : ...;
```

### 3. Struct Padding Awareness
Check if wrapper structs duplicate existing padding in member types.

### 4. Manual sqrt Decomposition
Sometimes `Length(v)` helper doesn't match; manual `sqrt(zz + yy + xx)` with specific order does.

## Next Steps

1. Look for more string iteration functions that might benefit from unsigned char fix
2. Search for other Vector3Pad-style wrapper structs with duplicate padding
3. Consider bulk-fixing functions using discovered patterns
4. Update WORKSESSION.md with session results
