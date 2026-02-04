# Session: Types & Templates - 2026-01-23

## Summary

Focus on type mismatches, template instantiations, and line number fixes. Fixed 9 functions to 100%.

**Progress:**
- Milo Engine Code: 53.84% → 53.86%
- Overall: 30.75% → 30.77%

## Functions Fixed to 100%

| Function | Fix | Pattern |
|----------|-----|---------|
| `ReclaimableAlloc::ReclaimableAlloc` | `(x+15)/4` → `((x+15)>>2)&~3` | Bitwise word-aligned formula |
| `AppChild::AppChild` | `unsigned int mPort` → `unsigned short mPort` | Type sizing (stw→sth) |
| `BinStream map operator>>` (3 funcs) | `int size` → `unsigned int size` | Loop counter signedness |
| `CharUpperTwist::operator new` | `0x1D` → `0x1B` | OBJ_MEM_OVERLOAD line number |
| `CharUpperTwist::operator delete` | (same fix) | OBJ_MEM_OVERLOAD line number |
| `TexProc::operator new` | `0x1C` → `0x1D` | OBJ_MEM_OVERLOAD line number |
| `TexProc::operator delete` | (same fix) | OBJ_MEM_OVERLOAD line number |

## Key Patterns Discovered

### 1. Bitwise Word-Aligned Formulas

When computing word-aligned byte counts, the compiler sometimes uses `clrrwi` (clear right word immediate) instead of division:

```cpp
// Before: srawi + addze (signed division)
FixedSizeAlloc((x + 15) / 4, ...)

// After: srawi + clrrwi (bitwise formula)
FixedSizeAlloc(((x + 15) >> 2) & ~3, ...)
```

The `& ~3` clears the bottom 2 bits after the shift, giving a different but related calculation.

### 2. Data Type Sizing Affects Store Instructions

Member variable types affect store instruction selection:
- `unsigned int`: generates `stw` (store word, 32-bit)
- `unsigned short`: generates `sth` (store half, 16-bit)

Look for `sth` vs `stw` mismatches in objdiff.

### 3. Loop Counter Signedness

Loop counters affect comparison instruction selection:
- `int`: generates `cmpwi` (signed compare)
- `unsigned int`: generates `cmplwi` (unsigned compare)

BinStream template used signed comparison but target used unsigned. Changing `int size` to `unsigned int size` fixed all 3 map operator>> instantiations.

### 4. OBJ_MEM_OVERLOAD Line Numbers

The `OBJ_MEM_OVERLOAD(0xNN)` macro embeds a line number. It must match exactly:
- `li r5, 0x1b` vs `li r5, 0x1d` is a line number mismatch
- These show as `diff_arg` in objdiff

## Confirmed Unfixable (This Session)

- **Register allocation swaps** (`diff_arg` with r10/r11): `__adjust_heap`, `RndSoftParticleBuffer`
- **fmadd operand order**: `complex::operator*` (99.33%)
- **Branch direction (bge vs blt)**: Changing `Min` template from `(y<x)?y:x` to `(x<y)?x:y` made things worse
- **Embedded line numbers in constructors**: `HamCamShot::Target::Target` has ObjPtr initialization with line number

## Commands Used

```bash
# Find near-match functions
~/code/milohax/objdiff/target/release/objdiff-cli report query build/373307D9/report.json \
  --functions --min-percent 95 --max-percent 99 --limit 50

# Check specific function mismatches
~/code/milohax/objdiff/target/release/objdiff-cli diff -p . "FunctionName" -f json --include-instructions | \
  jq '[.instructions[] | select(.match_type == "diff_arg" or .match_type == "replace")]'

# Find functions with exactly N instruction difference
~/code/milohax/objdiff/target/release/objdiff-cli report function build/373307D9/report.json "FunctionName"
```

## Files Changed

- `src/system/utl/PoolAlloc.cpp` - ReclaimableAlloc formula fix
- `src/system/os/NetworkSocket.h` - NetAddress::mPort type change
- `src/system/utl/BinStream.h` - map operator>> unsigned counter
- `src/system/char/CharUpperTwist.h` - OBJ_MEM_OVERLOAD line number
- `src/system/rndobj/TexProc.h` - OBJ_MEM_OVERLOAD line number

## Next Steps

1. Search for more `OBJ_MEM_OVERLOAD` line number mismatches (quick wins)
2. Look for more type sizing issues (`sth` vs `stw` mismatches)
3. Check other template instantiations for signed/unsigned issues
4. Investigate `HamCamShot::Target::Target` (99.97%) - ObjPtr line number issue
