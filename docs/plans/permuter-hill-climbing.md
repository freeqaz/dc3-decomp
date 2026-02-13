# Permuter Hill-Climbing Pipeline

Batch triage + iterative permutation to crack register swap mismatches at scale.

## Context

The permuter evolution (Phases 1-3) is complete: 12 patterns, composition via `--compose`, diagnosis-guided filtering, 82 tests. The question is how to leverage these primitives for maximum decomp progress.

### The Landscape (as of 2026-02-13)

```
Total: 47,124 functions
  100%:      23,943  (50.8%)
  95-99.9%:     793  (1.7%)  ← PRIMARY TARGET
  90-94.9%:     262  (0.6%)  ← SECONDARY TARGET
  80-89.9%:     412  (0.9%)
  50-79.9%:     297  (0.6%)
  0-1%:      21,248  (45.1%) — undecompiled stubs
```

**~1,055 functions** in the 90-99.9% range. Many marked AT_LIMIT from before composition existed.

### Sample Diagnosis

| Function | Match | Diagnosis |
|----------|-------|-----------|
| LabelNumberTicker::Poll | 99.5% | **Pure register swaps** (r9↔r10↔r11 rotation) |
| HamCharacter::OnCamTeleport | 99.5% | 2 register swap pairs + minor offset shift |
| MetaPerformer::OnReviewMovePassed | 94.9% | 5 swap pairs + offset shifts + insert/delete |
| BustAMovePanel::SetFlashcardText | 94.8% | Insert/delete cluster + offsets |

**Register swaps dominate the near-matches.** They are fixable — the compiler assigns callee-saved registers (r14-r31) based on declaration order and first-use order. The permuter's `declaration_reorder`, `variable_extraction`, `commutative_swap`, and `inline_assignment` patterns all shift register allocation.

---

## Phase 1: Batch Triage

Diagnose all ~1,055 functions in the 90-99.9% range. Classify each by mismatch type.

### Classification Categories

| Category | Criteria | Permuter Strategy |
|----------|----------|-------------------|
| **REGSWAP_ONLY** | 100% of diff_arg explained by register swaps | Highest ROI. Composition sweep. |
| **REGSWAP_PLUS** | Register swaps + minor offset/symbol diffs | Worth trying. May need manual assist after. |
| **STRUCTURAL** | insert/delete/replace dominates | Skip for permuter. Manual work. |
| **NOISE** | Unicorn-equivalent (behavioral match) | Mark as acceptable/SKIP. |

### Implementation

Extend `scripts/permuter/batch_validate.py` or create `scripts/permuter/batch_triage.py`:

```python
def triage_function(symbol: str) -> TriageResult:
    """Run diff_inspect diagnose on a function and classify it."""
    diagnosis = run_diagnosis(symbol)

    total_mismatches = diagnosis.diff_arg + diagnosis.replace + diagnosis.insert + diagnosis.delete
    regswap_count = sum(pair.count for pair in diagnosis.register_swaps)

    if total_mismatches == 0:
        return TriageResult(symbol, "NOISE", diagnosis)

    regswap_ratio = regswap_count / total_mismatches

    if diagnosis.insert == 0 and diagnosis.delete == 0 and diagnosis.replace == 0:
        if regswap_ratio > 0.5:
            return TriageResult(symbol, "REGSWAP_ONLY", diagnosis)
        else:
            return TriageResult(symbol, "REGSWAP_PLUS", diagnosis)
    elif diagnosis.insert + diagnosis.delete + diagnosis.replace > total_mismatches * 0.3:
        return TriageResult(symbol, "STRUCTURAL", diagnosis)
    else:
        return TriageResult(symbol, "REGSWAP_PLUS", diagnosis)
```

**Output**: JSON report mapping each function to its category + swap pair details.

---

## Phase 2: Targeted Composition Sweep

For REGSWAP_ONLY and REGSWAP_PLUS functions, run the permuter with composition enabled.

### Register-Shifting Pattern Matrix

These patterns affect register allocation:

| Pattern | How It Shifts Allocation |
|---------|------------------------|
| `declaration_reorder` | Directly changes order of callee-saved register assignment |
| `variable_extraction` | Introduces new temporaries, pushes other allocations later |
| `commutative_swap` | Swaps which operand lands in which register |
| `inline_assignment` | Removes temporaries, shifts allocation earlier |
| `argument_swap` | Reorders function call arguments |

### High-Value Composition Pairs for Register Swaps

| Stage A | Stage B | Rationale |
|---------|---------|-----------|
| `variable_extraction` | `declaration_reorder` | Extract creates new decl; reorder positions it to fix allocation |
| `commutative_swap` | `declaration_reorder` | Swap operand changes reg for one value; reorder fixes the cascade |
| `inline_assignment` | `declaration_reorder` | Fold removes a temporary; reorder adjusts remaining |
| `variable_extraction` | `commutative_swap` | Extract isolates a value; swap changes which register holds it |

### Batch Sweep Script

```bash
# For each REGSWAP_ONLY function from triage:
python -m scripts.permuter \
    --symbol "$SYMBOL" \
    --source "$SOURCE" \
    --function "$FUNC" \
    --compose \
    --patterns declaration_reorder,variable_extraction,commutative_swap,inline_assignment \
    --max-variants 200
```

---

## Phase 3: Iterative Hill-Climbing

The key unlock beyond single-round composition. Current `--compose` does A→B pairs. Hill-climbing does:

```
Round 1: Permute original → find best improvement → apply
Round 2: Permute improved source → find best improvement → apply
Round 3: Permute improved source → find best improvement → apply
...until plateau (no improvement for N rounds)
```

Each round shifts register allocation slightly. Over 3-4 rounds, the right assignment can emerge from iterative nudging.

### Why This Works

Consider a function with 5 local variables assigned to r27-r31. The target wants them in a different order. A single `declaration_reorder` might fix 2 of 5 register assignments. But the remaining 3 are now in a *different* wrong order than before — one that a second round of reordering (or extraction + reorder) can fix.

### Implementation

```python
def hill_climb(
    symbol: str,
    source_path: str,
    function_name: str,
    max_rounds: int = 5,
    patience: int = 2,  # rounds without improvement before stopping
) -> HillClimbResult:
    """Iteratively permute and apply improvements."""

    current_score = get_baseline_score(symbol)
    rounds_without_improvement = 0
    history = []

    for round_num in range(max_rounds):
        # Run permuter with composition on current source
        result = run_permuter(
            symbol=symbol,
            source=source_path,
            function=function_name,
            compose=True,
            patterns=REGISTER_SHIFT_PATTERNS,
            max_variants=200,
        )

        best = result.best_improvement()
        if best and best.score > current_score:
            # Apply the improvement (permuter does this with --apply)
            apply_variant(best)
            history.append(RoundResult(round_num, best.name, best.score))
            current_score = best.score
            rounds_without_improvement = 0

            if current_score >= 100.0:
                break  # Perfect match!
        else:
            rounds_without_improvement += 1
            if rounds_without_improvement >= patience:
                break  # Plateau

    return HillClimbResult(symbol, current_score, history)
```

### Budget and Stopping

- **Max rounds**: 5 (diminishing returns after that)
- **Patience**: 2 rounds without improvement → stop
- **Per-round budget**: 200 variants (composition expands this to ~2000 effective)
- **Build cost**: ~1s per variant × 200 = ~3min per round × 5 rounds = ~15min max per function
- **Total for 500 REGSWAP functions**: ~125 hours at 1 core; ~16 hours at 8 cores

---

## Phase 4: Register-Aware Targeting (Advanced)

Instead of blind permutation, use diagnosis data to target specific declarations.

### Concept

When diagnose reports `r24 ↔ r25`, we know two variables need to swap allocation order. PPC callee-saved registers are assigned by declaration/first-use order. So:

1. List all local variable declarations in source order
2. Map declaration position → likely register assignment (r31 = first used, r30 = second, ...)
3. Find which declarations correspond to r24 and r25
4. Generate variants that swap exactly those two declarations

This reduces the search space from N! to O(1) for each swap pair.

### Challenges

- Register assignment isn't purely by declaration order — it's by *first use* order, which depends on control flow
- Scratch registers (r3-r12) are assigned by the allocator dynamically, not by declaration order
- The mapping is approximate, not exact — need to try nearby permutations too

### Implementation Sketch

```python
def targeted_regswap_fix(
    ctx: FunctionContext,
    swap_pairs: list[tuple[str, str]],  # e.g., [("r24", "r25")]
) -> Iterator[Variant]:
    """Generate variants targeting specific register swap pairs."""

    declarations = list_declarations(ctx)  # ordered by source position

    for reg_a, reg_b in swap_pairs:
        # Estimate which declaration indices map to these registers
        # r31 = first callee-saved used, r30 = second, etc.
        idx_a = estimate_declaration_index(reg_a, len(declarations))
        idx_b = estimate_declaration_index(reg_b, len(declarations))

        # Generate variants swapping declarations near these indices
        for i in range(max(0, idx_a - 1), min(len(declarations), idx_a + 2)):
            for j in range(max(0, idx_b - 1), min(len(declarations), idx_b + 2)):
                if i != j:
                    yield swap_declarations(ctx, declarations[i], declarations[j])
```

---

## Execution Plan

| Step | What | Effort | Prereqs |
|------|------|--------|---------|
| 1 | Batch triage: diagnose all 90-99.9% functions | Script + ~30min runtime | None |
| 2 | Analyze triage results, validate categories | Review JSON output | Step 1 |
| 3 | Single-round composition sweep on REGSWAP_ONLY | Script + ~2hr runtime | Step 2 |
| 4 | Implement hill-climbing loop | ~2hr coding | Step 3 results inform design |
| 5 | Hill-climbing sweep on remaining REGSWAP functions | ~16hr runtime (8 cores) | Step 4 |
| 6 | Register-aware targeting (if needed) | ~4hr coding | Step 5 results inform need |

### Success Metrics

- **Immediate win**: Push 50+ functions from 95-99% to 100%
- **Composition win**: Push 20+ AT_LIMIT functions past their previous plateau
- **Hill-climbing win**: Crack 10+ stubborn register swap functions via iterative nudging
- **Overall**: Move decomp from 50.8% → 51.5%+ (100+ new 100% matches)

---

## Open Questions

1. **Variant budget per round**: 200 is conservative. Could go to 500 for REGSWAP_ONLY (smaller search space, higher hit rate). How much build time can we afford?

2. **Unicorn pre-filter**: Should we run unicorn on the 90-99% range first to filter out behavioral matches? Pro: avoids wasted permuter time on functions that are already correct. Con: adds a step.

3. **Beyond pairs**: Phase 3 composition does A→B. Hill-climbing effectively does A→B→C→... But should we also support explicit triple chains (A→B→C in one pass)?

4. **AT_LIMIT reset**: Many 90-95% functions are marked AT_LIMIT from manual attempts. Should we reset their status for the composition sweep? The tooling didn't exist when they were marked.
