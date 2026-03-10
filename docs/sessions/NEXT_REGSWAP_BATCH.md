# Regswap Batch: Permuter-Driven Declaration Reorder

**Priority**: Medium-High
**Status**: Planned

## Context

1,218 AT_LIMIT functions are blocked by register swaps. Statistical analysis (`scripts/analysis/regswap_classify.py`) of 1,288 cached diffs showed:
- **12% (146 functions)**: param x param swaps — two user-controllable variables swapped. **Potentially fixable** via declaration reorder.
- **60%**: parameter x compiler-temp — unfixable from source.
- **28%**: compiler-temp x compiler-temp — unfixable from source.

The permuter infrastructure (beam search, m2c guidance, 592 tests) is mature and ready for a targeted batch run.

## Approach

### Phase 1: Identify Candidates

```bash
# Query AT_LIMIT functions with regswap mismatch class
python3 scripts/analysis/regswap_classify.py --filter param_x_param --output candidates.json
```

Or via orchestrator:
```
mcp__orchestrator__query_functions(unit_pattern="*", match_min=85, match_max=99, limit=200)
```

Filter to functions where:
- Match >= 85% (close to done, regswap is the last gap)
- Mismatch type is callee-saved register swap (r13-r31, f14-f31)
- Both swapped registers map to user-declared variables (not compiler temps)

### Phase 2: Permuter Batch

For each candidate:
1. Run `scan_and_permute.py` with `--patterns declaration_reorder` (targeted, not all patterns)
2. Use beam search (`--beam`) for multi-variable reorder exploration
3. Accept improvements that reach 100% match
4. For improvements that don't reach 100%, log the best achievable for triage

```bash
python3 scripts/permuter/scan_and_permute.py \
  --unit "path/to/Unit.cpp" \
  --function "ClassName::Method" \
  --patterns declaration_reorder \
  --beam --max-rounds 50
```

### Phase 3: Validate & Commit

- Run full `ninja` build after each batch of fixes
- Check for regressions in same TU (declaration reorder can affect inlining budget for neighboring functions)
- Commit working fixes in per-unit batches

## Expected Yield

Conservative estimate: 20-40 functions moved from AT_LIMIT to COMPLETE (14-27% of the 146 candidates). Many param x param swaps will still be unfixable because:
- The "user variable" might actually be a compiler-generated temporary that looks like a parameter in the diff
- Some swaps require reordering across scope boundaries
- Reordering may cause regressions in other functions in the same TU

Even 20 functions would be +0.06% overall (29,842 -> 29,862), but more importantly would validate the methodology for future runs.

## Key Files

- `scripts/analysis/regswap_classify.py` — classifier
- `scripts/permuter/scan_and_permute.py` — batch runner
- `scripts/permuter/patterns/declaration_reorder.py` — the pattern
- `scripts/permuter/beam_search.py` — beam search engine
- `docs/plans/REGSWAP_SOURCE_FIX_ROADMAP.md` — original roadmap

## Risk

- Declaration reorder in one function can shift inlining budget for others in the same TU
- Always check regression count after each fix (compare report.json before/after)
- If a fix causes a regression elsewhere, revert and mark as unfixable
