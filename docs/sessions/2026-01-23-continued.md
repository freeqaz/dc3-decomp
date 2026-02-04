# Session: January 23, 2026 (Continued - objdiff Deep Dive)

## Summary

Continued work on near-match functions from previous session using **objdiff CLI instruction-level analysis**. Focused on understanding diff types and diagnosing fixability. Achieved **1 improvement** (MatShaderFlagsOK 95.3%→98.2%) and confirmed several functions at compiler/linker limits.

## Functions Improved

| File | Function | Before | After | Fix Applied |
|------|----------|--------|-------|-------------|
| Shader.cpp | `RndShader::MatShaderFlagsOK` | 95.3% | **98.2%** | Combined `CheckError()` and `!mat->FadeOut()` into single if condition |

## Functions Confirmed at Limit

| File | Function | Match | Diagnosis Method | Root Cause |
|------|----------|-------|------------------|------------|
| Mat.cpp | `RndMat::LoadOld` | 97.0% | All `diff_arg` to merged functions | Linker-merged `Read3FloatStruct`, `Read4FloatStruct` |
| Group.cpp | `RndGroup::Load` | 98.4% | Register allocation (r26↔r27) | Variable declaration order unfixable |
| Mat.cpp | `RndMat::GetRefractEnabled` | 97.1% | Extra `clrlwi` bool mask | Compiler bool return handling |

## Key Diagnostic Findings

### 1. Match Type Distribution Analysis

Used this command to understand function fixability:
```bash
objdiff-cli diff -p . "SYMBOL" -f json --include-instructions | \
  jq '.instructions | group_by(.match_type) | map({type: .[0].match_type, count: length})'
```

**Example output for LoadOld:**
```json
[
  {"type": "delete", "count": 301},
  {"type": "diff_arg", "count": 28},
  {"type": "diff_op", "count": 1},
  {"type": "equal", "count": 282},
  {"type": "insert", "count": 291},
  {"type": "replace", "count": 4}
]
```

**Interpretation:** High delete/insert counts with similar totals suggest structural alignment issues, not fundamental logic differences.

### 2. Linker-Merged Function Detection

Pattern in `diff_arg` instructions that indicates unfixable linker merging:
```json
{
  "target": {"opcode": "bl", "args": "merged_Read4FloatStruct"},
  "base": {"opcode": "bl", "args": "??5@YAAAVBinStream@@AAV0@AAVColor@Hmx@@@Z"},
  "match_type": "diff_arg"
}
```

The `merged_*` prefix in target indicates linker optimization that combined identical functions.

### 3. Bool Return Mask Pattern

**Symptom:** Extra `clrlwi r3, r11, 24` instruction in decomp
```json
{"index": 30, "target": {"opcode": "li", "args": "r11, 0x1"}, "base": {"opcode": "li", "args": "r3, 0x1"}, "match_type": "diff_arg"},
{"index": 32, "target": {"opcode": "li", "args": "r11, 0x0"}, "base": {"opcode": "li", "args": "r3, 0x0"}, "match_type": "diff_arg"},
{"index": 33, "target": {"opcode": "clrlwi", "args": "r3, r11, 24"}, "match_type": "delete"}
```

**Meaning:** Compiler is generating bool-to-byte mask that original didn't have. Often unfixable without changing function signature.

### 4. Register Allocation Patterns

When you see consistent register swaps (e.g., r30↔r31 throughout):
```json
{"target": {"args": "r31, r3"}, "base": {"args": "r30, r3"}, "match_type": "diff_arg"},
{"target": {"args": "r30, sCurrent"}, "base": {"args": "r31, sCurrent"}, "match_type": "diff_arg"}
```

**Fix attempts:**
- Reorder variable declarations
- Move declarations inside/outside if blocks
- Change pointer types

**Success rate:** ~30% - often compiler makes its own choices regardless

## Approaches Tried (Results)

### GetRefractEnabled (97.1% - No improvement)

| Attempt | Code Change | Result |
|---------|-------------|--------|
| `return 1/0` instead of `return true/false` | No change (97.1%) |
| Direct condition return `return tex && (b1 \|\| ...)` | Worse (94.5%) |
| Local `ret` variable pattern | Much worse (84.5%) |

**Conclusion:** Bool return mask is compiler behavior, not controllable from source.

### MatShaderFlagsOK (95.3% → 98.2%)

**Original:**
```cpp
if (curShader->CheckError((MatFlagErrorType)0)) {
    if (!mat->FadeOut()) {
        // fadeout checked logic
    } else if (mat->FadeOut()) {
        // fadeout unchecked logic
    }
}
```

**Fixed:**
```cpp
if (curShader->CheckError((MatFlagErrorType)0) && !mat->FadeOut()) {
    // fadeout checked logic
} else if (mat->FadeOut()) {
    // fadeout unchecked logic
}
```

**Why it worked:** Combined conditions in first if branch, changed control flow to match original's branch structure.

## Files Modified

- `src/system/rndobj/Shader.cpp` - MatShaderFlagsOK if-else restructure
- `src/system/rndobj/Mat.cpp` - LoadOld version comparison tweak (minimal impact)

## Remaining Work

| File | Function | Match | Priority | Notes |
|------|----------|-------|----------|-------|
| Mat.cpp | GetRefractEnabled | 97.1% | LOW | At compiler limit (bool handling) |
| Mat.cpp | LoadOld | 97.0% | LOW | At linker limit (merged functions) |
| Group.cpp | Load | 98.4% | LOW | At register allocation limit |

## Quick Win Candidates Found

Small functions (16-60 bytes) at 95-99%:
- `FitnessCalorieSortCmp::Compare` (16 bytes, 97.5%)
- `BufStream::Eof` (24 bytes, 98.0%)
- `Box::Volume` (48 bytes, 98.8%)
- `String::operator==` (56 bytes, 99.1%)
