# Pattern Reference Index

Quick reference for all documented decompilation patterns in DC3 (Dance Central 3), targeting Xbox 360 / MSVC (PowerPC).

> **Data source:** `decomp.db` — 14,772 attempts across 47,371 functions, 2,149 generated patches.
> **Last updated:** 2026-01-29

## Fixable Patterns

These patterns can often be fixed with source changes. Sorted by ROI (impact x success rate).

| Pattern | Impact | Success | File |
|---------|--------|---------|------|
| Explicit Destructor | +37-70% | 100% | [fixable-declarations.md](fixable-declarations.md#explicit-destructor) |
| noreturn Attribute | +38% | 100% | [fixable-casting.md](fixable-casting.md#noreturn-attribute) |
| Float/Double Separation | +80% | 95% | [fixable-casting.md](fixable-casting.md#floatdouble-separation) |
| FMA Expression Order | +1-75% | 98% | [fixable-operators.md](fixable-operators.md#fma-expression-order) |
| Signed/Unsigned Cast | +1-50% | 100% | [fixable-comparison.md](fixable-comparison.md#signedunsigned-cast) |
| MILO_NOTIFY vs MILO_NOTIFY_ONCE | +10-35% | HIGH | [fixable-declarations.md](fixable-declarations.md#milo_notify-vs-milo_notify_once) |
| alloca vs _alloca | +10-15% | 100% | [fixable-declarations.md](fixable-declarations.md#alloca-vs-_alloca-intrinsic-stack-allocation) |
| Variable Extraction | +1-35% | 95% | [fixable-declarations.md](fixable-declarations.md#variable-extraction) |
| Explicit Conditional vs Max() | +35% | HIGH | [fixable-control-flow.md](fixable-control-flow.md#explicit-conditional-vs-max) |
| Explicit Float Cast | +35% | HIGH | [fixable-casting.md](fixable-casting.md#explicit-float-cast) |
| Unsigned Zero Comparison | +0.4-1.3% | 95% | [fixable-comparison.md](fixable-comparison.md#unsigned-zero-comparison) |
| Operator Overload Selection | +1-2% | 100% | [fixable-operators.md](fixable-operators.md#operator-overload-selection) |
| Inline Assignment | +1-2% | 95% | [fixable-operators.md](fixable-operators.md#inline-assignment) |
| Ternary vs If-Else | +5-10% | 75% | [fixable-control-flow.md](fixable-control-flow.md#ternary-vs-if-else) |
| IsNaN vs Threshold Check | +3-5% | HIGH | [fixable-comparison.md](fixable-comparison.md#isnan-vs-threshold-check) |
| Variable Declaration Order | +1-88% | 30% | [fixable-declarations.md](fixable-declarations.md#variable-declaration-order) |

### Additional Fixable Patterns

| Pattern | File |
|---------|------|
| sizeof() Signedness | [fixable-casting.md](fixable-casting.md#sizeof-signedness) |
| Loop Counter Signedness | [fixable-comparison.md](fixable-comparison.md#loop-counter-signedness) |
| String Iteration Signedness | [fixable-comparison.md](fixable-comparison.md#string-iteration-signedness) |
| empty() vs size() == 0 | [fixable-comparison.md](fixable-comparison.md#empty-vs-size) |
| Comparison Style | [fixable-comparison.md](fixable-comparison.md#comparison-style) |
| Initializer Literals | [fixable-declarations.md](fixable-declarations.md#initializer-literals) |
| Static Variable Scope | [fixable-declarations.md](fixable-declarations.md#static-variable-scope) |
| Braced vs Braceless If (Scope Counter) | [fixable-declarations.md](fixable-declarations.md#braced-vs-braceless-if-scope-counter) |
| Static Symbol Order | [fixable-declarations.md](fixable-declarations.md#static-symbol-order) |
| Offset Swap | [fixable-declarations.md](fixable-declarations.md#offset-swap) |
| sret Return Value Tracing | [fixable-declarations.md](fixable-declarations.md#sret-return-value-tracing) |
| Loop Structure | [fixable-control-flow.md](fixable-control-flow.md#loop-structure) |
| Sequential If vs If-Else | [fixable-control-flow.md](fixable-control-flow.md#sequential-if-vs-if-else) |
| Single Return for Branch Direction | [fixable-control-flow.md](fixable-control-flow.md#single-return-for-branch-direction) |
| Boolean Index | [fixable-operators.md](fixable-operators.md#boolean-index) |
| Bitwise Alignment | [fixable-operators.md](fixable-operators.md#bitwise-alignment) |
| Commutative Operand Order | [fixable-operators.md](fixable-operators.md#commutative-operand-order) |
| Comparison Operand Order | [fixable-operators.md](fixable-operators.md#comparison-operand-order) |
| Bool Mask | [fixable-bool-mask.md](fixable-bool-mask.md) |
| MILO_NOTIFY vs MILO_NOTIFY_ONCE | [fixable-declarations.md](fixable-declarations.md#milo_notify-vs-milo_notify_once) |
| IsNaN vs Threshold Check | [fixable-comparison.md](fixable-comparison.md#isnan-vs-threshold-check) |

---

## Unfixable Patterns

These patterns are usually not fixable at source level. Verify that the pattern truly applies (and isn’t mixed with fixable issues) before accepting the current match percentage.

| Pattern | Prevalence | Typical Gap | File |
|---------|------------|-------------|------|
| Linker Merged (ICF) | 400 functions | 0.5-3% | [verifiable-icf.md](verifiable-icf.md#linker-merged-icf) (verify first) |
| LTCG/Global Pooling | varies | 0.5-1% | [verifiable-icf.md](verifiable-icf.md#ltcg-global-pooling) |
| Float Constant Pooling | common | 1-2 instr | [verifiable-icf.md](verifiable-icf.md#float-constant-pooling) |
| Register Allocation | 607 functions | 1-3% | [unfixable-compiler.md](unfixable-compiler.md#register-allocation) |
| ASSERT_REVS Scheduling | ~10% | ~0.8-0.9% | [unfixable-compiler.md](unfixable-compiler.md#assert_revs-scheduling) |
| fmadds vs Separate Ops | float math | 1-3% | [unfixable-compiler.md](unfixable-compiler.md#fmadds-vs-separate-ops) |
| Commutative Register Swap | float ops | <1% | [unfixable-compiler.md](unfixable-compiler.md#commutative-register-swap) |
| 64-bit Extraction | rare | ~5% | [unfixable-compiler.md](unfixable-compiler.md#64-bit-extraction) |

---

## Harmful Patterns

These patterns make matches **worse**. Avoid them.

| Pattern | Effect | File |
|---------|--------|------|
| Member Aliasing | -6% | [harmful-avoid.md](harmful-avoid.md#member-aliasing) |
| Child Pointer in Loop | -6.5% | [harmful-avoid.md](harmful-avoid.md#child-pointer-in-loop) |
| End Iterator Explicit | -0.5% | [harmful-avoid.md](harmful-avoid.md#end-iterator-explicit) |
| Constructor Zero-Init That Doesn’t Exist in Target | -2% to -6% | [harmful-avoid.md](harmful-avoid.md#constructor-zero-init-that-doesnt-exist-in-target) |

---

## Quick Decision Tree

```
Match% < 50%?
  → Likely missing implementation. Check RB3 reference, Ghidra decompilation.

Match% 50-80%?
  → Structural issues. Try control flow, variable declarations.

Match% 80-95%?
  → Fine-tuning. Check comparison patterns, casting, operator selection.
  → Prologue mismatch with _RtlCheckStack12? Try _alloca instead of alloca.

Match% 95-99%?
  → Check for unfixable patterns first.
  → If no unfixable patterns: try variable reorder, inline assignment.

Match% 99%+ but not 100%?
  → Often unfixable (linker-merged, register allocation), but verify first.
  → Check `objdiff-cli diff --analyze --verdict` and confirm any LINKER_MERGED calls.
  → Only mark "at limit" after verification; otherwise keep investigating.
```

### Prologue Hints

When the prologue (function entry) differs significantly:
- **`_RtlCheckStack12` in target:** Use `_alloca` (intrinsic) instead of `alloca` (CRT wrapper)
- **Stack frame size differs:** Check for missing/extra local variables
- **Different save/restore pattern:** May indicate unfixable compiler optimization

**Tip:** When running `objdiff-cli diff --verdict`, the output now shows:
- 💡 Match guidance hints based on percentage
- 📖 Links to pattern documentation for each detected pattern
- Analysis summary showing patterns checked and unattributed mismatches
- Verdict factors table explaining the classification

---

## Finding Targets in decomp.db

Query the database to find functions matching specific patterns or criteria:

```sql
-- Functions that CAN reach 100% (no unfixable patterns)
SELECT symbol, current_percent, unit
FROM functions
WHERE reachable_100 = 1
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;

-- High-impact functions (many callers, worth fixing first)
SELECT symbol, fan_in, current_percent
FROM functions
WHERE fan_in >= 5
  AND current_percent < 100
  AND excluded = 0
ORDER BY fan_in DESC
LIMIT 20;

-- Fresh targets (never attempted, high match)
SELECT symbol, current_percent, unit
FROM functions
WHERE attempt_count = 0
  AND current_percent >= 90
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;

-- Type anchors (constructors/destructors for class validation)
SELECT symbol, current_percent
FROM functions
WHERE (is_constructor = 1 OR is_destructor = 1)
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;
```

See [DATABASE_SCHEMA.md](../../reference/DATABASE_SCHEMA.md) for full schema documentation.

---

## Statistics (2026-01-29)

From `decomp.db` — 47,371 functions, 14,772 attempts, 2,149 generated patches:

| Metric | Value |
|--------|-------|
| Total functions | 47,371 |
| Perfect match (100%) | 23,631 (49.9%) |
| Near-perfect (99-99.9%) | 669 (1.4%) |
| High match (95-98.9%) | 436 (0.9%) |
| Medium match (90-94.9%) | 295 (0.6%) |
| Partial (50-89.9%) | 936 (2.0%) |
| Low match (<50%) | 21,404 (45.2%) |
| Average match | 98.2% |

### Verdict Distribution

| Verdict | Count | Meaning |
|---------|-------|---------|
| COMPLETE | 22,893 | 100% match confirmed |
| AT_LIMIT | 937 | Unfixable compiler/linker artifact |
| NEAR_COMPLETE | 401 | 99%+ with minor residual mismatch |
| LIKELY_FIXABLE | 130 | Known pattern, fix not yet applied |

### Known Pattern Distribution

| Pattern | Count | Avg % | Near-perfect | Perfect | Fixable? |
|---------|-------|-------|-------------|---------|----------|
| REGISTER_SWAP | 607 | 92.3% | 92 | 11 | Rarely — compiler artifact |
| LINKER_MERGED | 400 | 96.3% | 174 | 4 | No — ICF/LTCG |
| CONTROL_FLOW | 134 | 92.4% | 21 | 8 | Sometimes |
| BOOL_MASK | 33 | 92.8% | 1 | 0 | Often |
| COMPARISON_STYLE | 7 | 93.2% | 3 | 0 | Often |

### Fine-Tuning Success Rates (90%+ to 100%)

From 143 successful fine-tuning attempts (90%+ start, 100% end):

| Pattern | Wins | Share |
|---------|------|-------|
| Variable extraction | 60 | 42% |
| Unsigned/signed comparison | 43 | 30% |
| Inline assignment | 32 | 22% |
| Operator overload | 20 | 14% |
| Float expression | 14 | 10% |
| Declaration reorder | 13 | 9% |
| Ternary / if-else | 12 | 8% |
| FMA expression order | 6 | 4% |
| Destructor | 3 | 2% |

> Totals exceed 100% because some fixes combine multiple patterns.

---

## See Also

- [fixable-comparison.md](fixable-comparison.md) — Signed/unsigned, empty vs size, zero-check
- [fixable-casting.md](fixable-casting.md) — Float cast, noreturn, float/double, sizeof
- [fixable-control-flow.md](fixable-control-flow.md) — Max/Min explicit, ternary vs if/else, loop structure
- [fixable-declarations.md](fixable-declarations.md) — Variable extraction, declaration order, destructor
- [fixable-operators.md](fixable-operators.md) — FMA order, operator overload, inline assignment
- [fixable-bool-mask.md](fixable-bool-mask.md) — Bool mask (`clrlwi`) fixes
- [unfixable-compiler.md](unfixable-compiler.md) — Register swap, ASSERT_REVS, fmadds
- [verifiable-icf.md](verifiable-icf.md) — ICF, LTCG, float constant pooling
- [harmful-avoid.md](harmful-avoid.md) — Member aliasing, child pointer in loop
