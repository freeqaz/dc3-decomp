# Unfixable Patterns: Compiler

These patterns are caused by compiler optimizations or heuristics we cannot control at source level.

**Action:** Accept current match percentage when these patterns are detected.

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

### What To Do

Try [Variable Declaration Order](fixable-declarations.md#variable-declaration-order) first (~30% success rate). If 10+ reordering attempts don't help, accept as permanent.

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
