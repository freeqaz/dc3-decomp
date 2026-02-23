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
**Status:** Mechanism fully understood (Experiments 1-9). Source-level fixes work ~30% of the time. Binary patching of c2.dll coloring loop is a viable path to fix the remaining 70%.

### Symptom

Consistent register swaps (e.g., r30 vs r31, f30 vs f31) throughout function.

### Common Swaps

- r10 ↔ r11 (volatile GPR)
- r27 ↔ r28 (callee-saved GPR)
- r30 ↔ r31 (callee-saved GPR)
- f30 ↔ f31 (callee-saved FPR)

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

### Root Cause: c2.dll Register Allocator Mechanism

**Fully characterized via GDB tracing of c2.dll** (Experiments 1-9 in [compiler-instrumentation.md](../../plans/compiler-instrumentation.md)):

The MSVC Xbox 360 backend (c2.dll) uses graph-coloring register allocation:

1. **Interference graph building**: Each live range becomes a node. Nodes that overlap get interference edges.
2. **BSF-based coloring** (at c2.dll RVA `0x026780`): The allocator iterates variables by **symbol ID** (which follows declaration order in source). For each variable, it uses x86 `BSF` (Bit Scan Forward) on a bitmask of available colors to find the lowest-numbered free color.
3. **Color→Register mapping**: Colors map to PPC registers with direction depending on register class:
   - **Volatile GPR**: top-down (first color → r11, next → r10)
   - **Callee-saved GPR**: bottom-up (first color → r29, next → r30, r31)
   - **FPR**: follows similar pattern

**Key insight**: Each variable gets a **deterministic color** based on interference constraints — colors are consistent regardless of declaration order. But the **color→register mapping** depends on allocation ORDER (= declaration order). Swapping declaration order of two variables swaps which color maps to which register, but the colors themselves don't change.

This is why:
- **Source reordering works ~30% of the time**: When interference constraints allow, reordering declarations changes the color→register mapping to match the target.
- **Source reordering fails ~70% of the time**: When interference constraints force the same colors regardless of order, or when the correct mapping requires a specific symbol ID sequence that doesn't correspond to any valid declaration order.

### Evidence

| Experiment | Test | Finding |
|-----------|------|---------|
| Exp 1-3 | swap_a vs swap_b (volatile) | Declaration order determines r10↔r11 assignment |
| Exp 4-5 | callee_a vs callee_b (saved) | Declaration order determines r29↔r31 assignment |
| Exp 6-7 | callgrind-diff on BSF | Identical instruction traces except divergent BSF calls |
| Exp 8 | Full BSF trace (389 calls) | Only 6 of 389 BSF calls differ; colors consistent, mapping changes |

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
The compiler allocates GPRs and FPRs from separate pools:
```cpp
// Before - interleaved
int a; float f1; int b; float f2;

// After - grouped by type
int a; int b;
float f1; float f2;
```

**4. Try reverse order**
Callee-saved allocates bottom-up, volatile top-down:
```cpp
// If nothing else works, try reversing declaration order
float z, y, x;  // Instead of x, y, z
```

### What To Do

Try [Variable Declaration Order](fixable-declarations.md#variable-declaration-order) with the heuristics above. If 10+ reordering attempts don't help, the register assignment is fixed by interference constraints.

**Future**: Binary patching of c2.dll's coloring loop (RVA `0x026780`) could reverse the BSF scan direction or reorder the color assignment, fixing all register swap functions at once. See [compiler-instrumentation.md](../../plans/compiler-instrumentation.md) for the full mechanism and address map.

### Real Example

| Function | Match | Attempts | Result |
|----------|-------|----------|--------|
| FastInvert | 99.45% | 10+ | AT_LIMIT (f30/f31 swap) |
| CharBonesMeshes::PoseMeshes | 99.24% | 5+ | AT_LIMIT (r10/r9, r28/r30) |
| DxTex::ResetSurfaces | 98.4% | verified | AT_LIMIT (r28/r29 swap after extrwi fix) |

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

## Stack Spill Scheduling

**Prevalence:** Functions with high register pressure
**Typical Gap:** ~1-2%

The target binary spills a local variable to the stack frame, but our code keeps it in a register.

### Symptom

objdiff shows 1-3 `delete` instructions that are all `stw rN, offset(rFP)` to the stack frame. The stored register contains a local variable that's used later in the function.

### Why Unfixable

Stack spill decisions are made by the register allocator based on register pressure, estimated spill/reload cost, and scheduling heuristics. These are internal compiler decisions with no source-level knob. Hoisting declarations, reordering code, or adding dummy uses generally doesn't change the spill pattern.

### Detection

- 1-3 `delete` instructions, all `stw` to stack frame offsets
- The function is otherwise very close (97%+)
- Removing/adding code doesn't change the spill pattern

### What To Do

Accept the ~1-2% gap and mark at_limit.

### Real Example

| Function | Match | Gap | Notes |
|----------|-------|-----|-------|
| PhysicsManager::HarvestCollidables | 97.4% | ~2.6% | Target spills `owner` to stack 0x54 twice |

---

## When "Unfixable" May Still Move

Large functions (especially 95%+ matches) often look dominated by compiler noise, but there is usually a small actionable subset.

### Practical Triage

1. **Separate global noise from local structure**
- Treat broad `diff_arg` drift (register swaps, symbol relocation, global stack deltas) as background.
- Prioritize `diff_op`, then small insert/delete clusters, then offset swaps.

2. **Fix one local shape at a time**
- Apply a single control-flow rewrite in one region.
- Re-run objdiff immediately.
- Keep changes that reduce `diff_op` or `diff_score` even if match% is unchanged.

3. **Use branch-polarity steering before declaration churn**
- Try compare viewpoint swaps (`a > b` vs `b < a`), condition inversion, and if/else body flips.
- Only after that, try variable declaration reordering for register swaps.

4. **Stop based on signal, not effort alone**
- If 3-5 branch-shape attempts do not reduce `diff_op`/`diff_score`, that region is likely compiler-fixed.
- Move to the next actionable region rather than broad refactors.

### Why This Matters

Functions tagged `AT_LIMIT` can still improve incrementally. A common pattern is:
- one control-flow inversion fixed,
- rounded match% unchanged,
- but diff score and mismatch quality improve.

That is real progress and lowers the chance of regressions in future attempts.

---

---

## Hard But Not Truly Unfixable

Some AT_LIMIT functions have patterns that are theoretically fixable but resist simple source-level changes. Future tooling (e.g., c2.dll patching, custom pragma support) may unlock these.

### Large Offset Addressing (lis+ori+lwzx vs addis+subi)

**Typical Gap:** ~30%
**Status:** Hard — no known source fix, may yield to compiler binary patching

When struct members are at offsets > 0x7FFF from the base pointer, the compiler must use multi-instruction addressing. The target compiler chose `lis+ori+lwzx` (build address in register, indexed load), while ours uses `addis+subi` (adjusted base, displacement load). Both reach the same memory, different instruction sequence.

| Function | Match | Root Cause |
|----------|-------|------------|
| StreamReceiver360::Tag | 70.2% | mVoice at offset 0x803c uses lwzx vs addis+subi |

### Scalar Deleting Destructor (??_G vs ~T + operator delete)

**Typical Gap:** ~10%
**Status:** Hard — compiler-generated function pattern

The target generates a "scalar deleting destructor" (??_G) wrapper for `delete obj`, while our compiler emits separate `~T()` + `operator delete()` calls. This is a compiler code generation choice for polymorphic delete expressions.

| Function | Match | Root Cause |
|----------|-------|------------|
| StreamReceiver360::Poll | 90.9% | `delete v` generates separate destructor + delete |

### cmplwi vs cmpwi for Pointer Null Checks

**Typical Gap:** ~1.5%
**Status:** Hard — compiler type-sensitivity for pointer comparisons

The target uses `cmplwi` (unsigned compare) for pointer null checks, while our compiler generates `cmpwi` (signed compare). Explicit casts to `(unsigned int)` do not affect the compare instruction selection. May require compiler-level changes.

| Function | Match | Root Cause |
|----------|-------|------------|
| SfxInst::IsRunning | 98.5% | `if (ptr->GetStream())` — cmplwi vs cmpwi |

---

## See Also

- [verifiable-icf.md](verifiable-icf.md) - ICF/linker-side verifiable patterns
- [fixable-declarations.md](fixable-declarations.md#variable-declaration-order) - When register issues are fixable
- [fixable-control-flow.md](fixable-control-flow.md#branch-polarity-steering-beqbne-blebge) - Branch-shape steering tactics
