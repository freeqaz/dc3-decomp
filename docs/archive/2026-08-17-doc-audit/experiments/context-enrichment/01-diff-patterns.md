# Experiment 1: Diff Pattern Classification

## Status: IMPLEMENTED

**Token cost**: ~500
**Expected impact**: High - reduces false AT_LIMIT verdicts
**Effort**: Low

## Hypothesis

Pre-classifying diff patterns saves agent analysis time and reduces false AT_LIMIT verdicts. By identifying unfixable patterns (SCHEDULING, LINKER_MERGED) upfront, agents can:
1. Avoid wasting iterations on genuinely unfixable issues
2. Focus effort on patterns that ARE fixable
3. Accept near-100% matches as functionally complete sooner

## Implementation

### Files Modified
- `scripts/orchestrator/context_collector.py`:
  - Added `DIFF_PATTERNS` constant with pattern definitions
  - Added `classify_diff_patterns()` function
  - Added `format_pattern_classification()` for prompt formatting
  - Updated `collect_pre_run_context()` to run classification when enabled

### Pattern Definitions

| Pattern | Fixability | Guidance |
|---------|------------|----------|
| SCHEDULING | UNFIXABLE | Compiler instruction ordering - cannot fix |
| LINKER_MERGED | UNFIXABLE | LTCG merged calls - cannot fix |
| ASSERT_REVS | NEAR_UNFIXABLE | Accept 99%+ as complete |
| BOOL_MASK | FIXABLE | Try casts, !!, or & 0xFF |
| REGISTER_SWAP | MAYBE_FIXABLE | Reorder declarations |
| STRUCT_OFFSET | FIXABLE | Check struct layout, use lookup tool |
| BRANCH_CONDITION | FIXABLE | Fix comparison logic |
| CONTROL_FLOW | MAYBE_FIXABLE | Restructure if/else/loops |
| STACK_FRAME | MAYBE_FIXABLE | Check locals and calling convention |

### Output Format

When enabled (treatment group), agents receive:

```markdown
## Diff Pattern Analysis

**Overall Assessment**: LIKELY_FIXABLE
**Summary**: All detected patterns appear fixable.

### Detected Patterns

| Pattern | Fixability | Description |
|---------|------------|-------------|
| STRUCT_OFFSET | FIXABLE | Wrong struct member offset |
| BRANCH_CONDITION | FIXABLE | Different branch condition |

### Suggested Actions (Priority Order)

1. [STRUCT_OFFSET] Check struct layout. Use lookup_struct_offset tool to identify field.
2. [BRANCH_CONDITION] Fix comparison logic. Common: (x != 0) vs (x > 0) for unsigned.
```

## A/B Assignment

```python
# Control group: Gets standard key_patterns list
# Treatment group: Gets enhanced pattern_classification_summary
enrichment_flags.get("diff_patterns")  # True = treatment
```

## Success Metrics

### Primary: False AT_LIMIT Rate
**Target**: -30% false positive AT_LIMIT verdicts

Baseline (from 80-95% band):
- AT_LIMIT rate: 79.4%
- Many of these are likely fixable but incorrectly abandoned

Treatment goal:
- Agents recognize fixable patterns faster
- AT_LIMIT rate drops to ~55% in 80-95% band

### Secondary: Iterations to Verdict
Track average iterations before AT_LIMIT verdict:
- Control: Baseline TBD (iteration tracking not yet implemented)
- Treatment: Should decrease if patterns identified earlier

## Results

_To be populated after collecting sufficient data_

### Data Collection Query

```sql
SELECT
  json_extract(enrichment_flags, '$.diff_patterns') as treatment,
  COUNT(*) as n,
  AVG(end_percent - start_percent) as avg_gain,
  SUM(CASE WHEN exit_status = 'at_limit' THEN 1 ELSE 0 END) * 100.0 / COUNT(*) as at_limit_rate
FROM attempts
WHERE enrichment_flags IS NOT NULL
  AND start_percent >= 80
  AND start_percent < 95
GROUP BY treatment;
```

## Recommendation

_To be determined after data collection_

Options:
- **ENABLE** - If AT_LIMIT rate significantly reduced
- **MODIFY** - If patterns need refinement
- **DISABLE** - If no measurable improvement

## Notes

- Pattern detection relies on text matching in objdiff output
- May need tuning of indicator patterns based on actual diff formats
- Consider adding confidence scores to patterns
