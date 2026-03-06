# Ghidra-Guided Permuter — Implementation Session (2026-03-05)

## What Was Built

Implemented the full Ghidra-guided permuter pipeline from the design doc (`2026-03-05-ghidra-guided-permuter.md`).

### New Files
- `scripts/permuter/ghidra_cache.py` — Read-only accessor for `decomp.db` (18,929 cached decompilations)
- `scripts/permuter/ghidra_ast.py` — tree-sitter-c parser for Ghidra output: variable order, expression structure, control flow skeleton, savegpr counts
- `scripts/permuter/ghidra_expr_match.py` — Expression tree comparison between source and Ghidra
- `scripts/permuter/ghidra_var_match.py` — Ghidra variable → register inference for regswap targeting
- `scripts/permuter/ghidra_stats.py` — Analytics: per-run + batch stats, persisted to `permuter_cache.db`
- `scripts/permuter/experiments/validate_ghidra_parsing.py` — Validation suite (4/4 PASS)

### Modified Files
- `scripts/permuter/types.py` — Added `ghidra_code`, `ghidra_ast`, `target_var_order`, `target_gpr_saves`, `ghidra_stats` to `FunctionContext` / `HillClimbResult`
- `scripts/permuter/scorer.py` — Ghidra cache lookup in `get_baseline(ghidra=True)`
- `scripts/permuter/hill_climber.py` — `--ghidra` flag, stats tracking, result display
- `scripts/permuter/scan_and_permute.py` — `--ghidra` flag, batch stats accumulation, summary output with `[GHIDRA]` tags
- `scripts/permuter/patterns/fma_reorder.py` — `_try_ghidra_guided()`: expression-structure-guided FMA variants
- `scripts/permuter/patterns/declaration_reorder.py` — `_try_ghidra_guided()`: Ghidra variable order → targeted regswap
- `tools/compiler_trace/regmap_solver.py` — `ghidra_guided_search()` entry point

### Validation Results
- **100%** of 200 sampled decompilations parse without errors
- **89%** have extractable local variables
- Structural diff between flat `a - b + c` and paren `a - (b - c)` clearly detectable
- MetaPanel::IsLoaded: both `&&` conjunction AND guard-return patterns detected in Ghidra output
- Integration test: Ghidra-guided reorder generates 3 targeted candidates for r30↔r31 swap

### Analytics
Per-function tracking in `permuter_cache.db`:
- Was Ghidra decompilation available?
- How many guided variants generated vs total?
- Did a Ghidra-guided variant win?
- Delta attribution (Ghidra vs other patterns)

Batch summary at end of `scan_and_permute` runs shows cache hit rate, variant generation, win rate, and delta breakdown.

## Gap Analysis vs Design Doc

| Idea | Status | Notes |
|------|--------|-------|
| Expression structure matching | Done | fma_reorder `_try_ghidra_guided()` |
| Control flow structure matching | Infrastructure only | `extract_control_flow_skeleton()` exists, no pattern uses it |
| Declaration order inference | Done | `ghidra_var_match.py` + declaration_reorder |
| Live range analysis | Not done | Hard, ~150 estimated fixes |
| Variable matching by usage pattern | Positional only | Doc suggests call-return, argument, struct-offset matching |
| Cross-reference Ghidra + /FAs | Not done | Priority chain is OR, not AND |
| Re-parenthesization (flat→paren) | Not done | Only paren→flat in `is_flat_vs_paren()` |
| Unfixable early detection | Not done | Struct offset / dead register visible in Ghidra |

## Validation Data for Next Steps

### Control flow patterns in near-match functions
- **83%** of 85-99.9% functions have `&&` or `||` in Ghidra output
- Existing `and_split` pattern already does `if (a && b)` ↔ `if (a) { if (b) { ... } }` but tries **both directions blindly**
- Existing `early_return_merge` does guard↔conjunction but also tries both directions
- Ghidra guidance would tell these patterns **which direction to go**, eliminating 50% of blind variants

### HEAD~3 analysis patterns (from `2026-03-05-pattern-analysis-head3.md`)
The analysis of 3 recent commits found 86 discrete changes. Mapped to Ghidra guidance:

| Pattern | Instances | Ghidra can guide? |
|---------|-----------|-------------------|
| Control flow restructure | 20 | YES — conjunction vs nested-if visible |
| Variable declaration reorder | 11 | YES — already implemented |
| Expression rewrite | 10 | YES — already implemented |
| Comparison inversion | 10 | MAYBE — Ghidra normalizes comparisons |
| Null check removal | 5 | YES — absence of null check visible |
| Temp variable extraction | 8 | PARTIAL — extra variable visible in Ghidra |
| Type/cast correction | 8 | NO — Ghidra inserts its own casts |
| Statement reorder | 4 | PARTIAL — execution order visible |

### Struct offset detection
- Only 2% of 85-99.9% functions have `param + 0xNN` patterns in Ghidra
- Most struct-offset issues are at lower match% (below 85%)
- Still valuable as an early-rejection filter in batch runs

## Next Implementation Priorities

### 1. Control Flow Guided Transforms
Add `_try_ghidra_guided()` to `and_split.py` and `early_return_merge.py`:
- Compare control flow skeleton between Ghidra and source
- If Ghidra shows `&&`/`||` and source has nested-if → generate only the collapse direction
- If Ghidra shows nested-if and source has `&&` → generate only the split direction
- Skip blind both-direction generation when Ghidra provides a signal

### 2. Cross-reference Ghidra + ASM for compound regswap targeting
Instead of Ghidra OR ASM, use both:
- Ghidra var order → inferred target register allocation
- ASM regmap → our actual register allocation
- Direct comparison identifies which vars are in wrong registers
- Generates single, high-confidence targeted swap

### 3. Unfixable early detection via Ghidra pre-flight
Before generating any variants, scan Ghidra output for red flags:
- Struct offset access at unexpected positions → struct layout mismatch
- Missing function calls in Ghidra that we have → extra code in source
- Dead variables in Ghidra → possible dead register issue (unfixable)
Flag these for deprioritization in batch runs.
