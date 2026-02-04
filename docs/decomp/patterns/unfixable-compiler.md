# Unfixable Patterns: Compiler

These patterns are caused by compiler optimizations or heuristics we cannot control at source level.

**Action:** Confirm the pattern actually applies (and that no fixable issues are mixed in). If verified, accept the current match percentage.

---

## ASSERT_REVS Scheduling

**Prevalence:** Functions with ASSERT_REVS macro (~10%)
**Typical Gap:** ~0.8-0.9%

Instruction scheduling differs around ASSERT_REVS calls.

### Symptom

Same instructions, different order around assert code.

### Why Unfixable

The compiler schedules instructions differently:
- Target computes `gRevs[2]` address before stack variables
- Our build computes stack variables before `gRevs[2]`
- Same instructions, different order - compiler heuristic

### Detection

Instruction count matches but order differs around assert code in second MILO_FAIL call.

### What To Do

All Load functions with ASSERT_REVS will have ~0.8-0.9% gap. Accept as effectively matched.

---

## fmadds vs Separate Ops

**Prevalence:** Functions with float math
**Typical Gap:** 1-3%

Compiler chooses fused vs separate floating-point operations.

### Symptom

`fmadds` in target vs `fmuls` + `fadds` in our build.

### Why Unfixable

This is controlled by compiler optimization flags we don't have access to. Both are mathematically equivalent but have slightly different rounding behavior.

### Example

```asm
# Original (fused multiply-add)
fmadds f0, f11, f11, f0

# Our build (separate multiply and add)
fmuls f11, f11, f11
fadds f0, f0, f11
```

### What To Do

Accept 1-3% gap on math-heavy functions.

---

## Register Allocation

**Prevalence:** 607 functions tagged REGISTER_SWAP (most common pattern)
**Typical Gap:** 1-3% (avg 92.3%)

The compiler's liveness analysis chooses registers differently.

### Symptom

Consistent register swaps (e.g., r30 vs r31, f30 vs f31) throughout function.

### Why Unfixable

The compiler's register allocation is based on liveness analysis - a heuristic we cannot influence from source code in most cases.

### Common Swaps

- r10 ↔ r11
- r27 ↔ r28
- r30 ↔ r31
- f30 ↔ f31

### Detection

All mismatches are `diff_arg` with register operands swapped:

```
| fmr f31, f1 | fmr f30, f1 |
| fmr f30, f1 | fmr f31, f1 |
```

**Finding register swap functions in decomp.db:**
```sql
SELECT symbol, current_percent
FROM functions
WHERE primary_pattern = 'REGISTER_SWAP'
  AND excluded = 0
ORDER BY current_percent DESC;
```

### Variable Reordering Heuristics

When REGISTER_SWAP is detected, try these strategies (~30% success rate overall):

**1. Group by usage pattern**
Variables used together should be declared together:
```cpp
// Before - scattered declarations
float x = GetX();
int count = 0;
float y = GetY();  // Used with x but declared far apart

// After - grouped by usage
float x = GetX();
float y = GetY();  // Now adjacent to x
int count = 0;
```

**2. Order by first use**
Declare variables in the order they're first read:
```cpp
// If function does: read a, read b, read c, write a
// Declare in order: a, b, c
```

**3. Separate integer and float declarations**
The compiler may allocate GPRs and FPRs from separate pools:
```cpp
// Before - interleaved
int a; float f1; int b; float f2;

// After - grouped by type
int a; int b;
float f1; float f2;
```

**4. Try reverse order**
Sometimes the compiler allocates from the end:
```cpp
// If nothing else works, try reversing declaration order
float z, y, x;  // Instead of x, y, z
```

### What To Do

Try [Variable Declaration Order](fixable-declarations.md#variable-declaration-order) with the heuristics above. If 10+ reordering attempts don't help, accept as permanent.

**Important:** The detection currently doesn't identify *which* variables correspond to swapped registers. You'll need to trace register usage manually in objdiff to identify candidates.

### Real Example

| Function | Match | Attempts | Result |
|----------|-------|----------|--------|
| FastInvert | 99.45% | 10+ | AT_LIMIT (f30/f31 swap) |
| CharBonesMeshes::PoseMeshes | 99.24% | 5+ | AT_LIMIT (r10/r9, r28/r30) |

---

## Commutative Register Swap

**Prevalence:** Float operations
**Typical Gap:** <1%

Commutative operation with swapped operand registers.

### Symptom

Same operation, operands in different order:

```
| fmuls f11, f0, f13 | fmuls f11, f13, f0 |
```

### Why Unfixable

Same mathematical result, different register order. No source-level control.

### What To Do

Accept as functionally identical.

---

## 64-bit Extraction

**Prevalence:** Rare
**Typical Gap:** ~5%

Different extraction methods for 64-bit to 16-bit conversions.

### Symptom

Target uses `lhz` (load halfword) vs our `ld` + bit masking.

### Why Unfixable

Compiler optimization choice for how to extract 16-bit slices from 64-bit values.

### What To Do

Accept ~5% gap.

---

## Branch Offsets

**Prevalence:** Common
**Typical Gap:** 0%

Branch target addresses differ due to code layout.

### Symptom

Branch instructions have different immediate offset values.

### Why Unfixable

Branch offsets are calculated based on code layout. Different instruction placement = different offsets.

### What To Do

These don't affect match percentage in objdiff scoring. Ignore.

---

## See Also

- [verifiable-icf.md](verifiable-icf.md) - ICF/linker-side verifiable patterns
- [fixable-declarations.md](fixable-declarations.md#variable-declaration-order) - When register issues are fixable
