# Experiment 4: Previous Attempt Diffs

## Status: IMPLEMENTED

**Token cost**: ~2000 (varies by diff size)
**Expected impact**: Medium - avoids repeating failed approaches
**Effort**: Low

## Hypothesis

Showing actual code diffs from previous attempts prevents agents from repeating failed changes by:
1. Making failed approaches explicit and visible
2. Providing context on what was already tried
3. Encouraging exploration of different strategies

## Implementation

### Files Modified
- `scripts/orchestrator/context_collector.py`:
  - Added `get_previous_attempt_diffs()` function
  - Added `format_previous_attempt_diffs()` for prompt formatting
  - Updated `collect_pre_run_context()` to include diffs when enabled

### Token Management
- `MAX_DIFF_CHARS_PER_ATTEMPT = 1500` - limits diff size
- Max 3 attempts included by default
- Truncated diffs marked with indicator

### Output Format

When enabled (treatment group), agents receive:

```markdown
## Previous Attempt Diffs

**Found 2 previous attempts with code changes.**

Review these to avoid repeating failed approaches:

### Attempt 1: haiku (+2.5%)

- **Result**: 85.0% → 87.5% (complete)
- **Notes**: Tried reordering struct members...

\`\`\`diff
- old code
+ new code
\`\`\`

### Attempt 2: sonnet (-0.5%)

- **Result**: 87.5% → 87.0% (at_limit)
- **Notes**: Attempted inline assembly workaround...

\`\`\`diff
- different approach
+ that didn't work
\`\`\`

---
**Guidance**: Avoid repeating changes that didn't improve match%. Try different approaches.
```

## A/B Assignment

```python
# Control group: Only sees "Attempt N: model, X% → Y% (status)"
# Treatment group: Also sees actual code diffs
enrichment_flags.get("attempt_diffs")  # True = treatment
```

## Success Metrics

### Primary: Repeated Failed Changes
**Target**: -40% repeated failed changes

Tracking approach:
- Compare current attempt patches to previous patches
- Flag when similar changes are attempted
- Count reduction in treatment group

### Secondary: Success Rate on Retries
Track success rate specifically for functions with prior attempts.

## Results

_To be populated after collecting sufficient data_

### Data Collection Query

```sql
SELECT
  json_extract(enrichment_flags, '$.attempt_diffs') as treatment,
  COUNT(*) as n,
  AVG(CASE WHEN end_percent > start_percent THEN 1 ELSE 0 END) as success_rate
FROM attempts a
JOIN functions f ON a.function_id = f.id
WHERE enrichment_flags IS NOT NULL
  AND f.attempt_count > 1  -- Has previous attempts
GROUP BY treatment;
```

## Recommendation

_To be determined after data collection_

## Notes

- Only included when function has previous attempts
- Diffs come from `attempts.patch` column (populated by orchestrator)
- Truncation keeps token cost manageable
- May need tuning of MAX_DIFF_CHARS_PER_ATTEMPT based on actual usage

## Limitations

1. **Requires prior attempts**: No benefit for first-time functions
2. **Patch quality varies**: Some patches may be incomplete
3. **Token cost**: Large diffs consume significant tokens
4. **No semantic deduplication**: Similar diffs shown separately
