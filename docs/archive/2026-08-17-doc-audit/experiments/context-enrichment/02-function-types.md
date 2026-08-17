# Experiment 2: Function Type Templates

## Status: IMPLEMENTED

**Token cost**: ~300
**Expected impact**: High - guides approach from first iteration
**Effort**: Low

## Hypothesis

Type-specific guidance for common function patterns (Load/Save/Init/Poll) improves first-pass success rate by:
1. Providing domain-specific knowledge upfront
2. Highlighting common pitfalls for each function type
3. Setting appropriate expectations (e.g., LOAD functions often have ~1% ASSERT_REVS mismatch)

## Implementation

### Files Modified
- `scripts/orchestrator/context_collector.py`:
  - Added `FUNCTION_TYPE_TEMPLATES` constant
  - Added `classify_function_type()` function
  - Added `format_function_type_guidance()` for prompt formatting
  - Updated `collect_pre_run_context()` to classify type when enabled

### Supported Function Types

| Type | Pattern | Description |
|------|---------|-------------|
| LOAD | `Load`, `::Load(` | Binary deserialization |
| SAVE | `Save`, `::Save(` | Binary serialization |
| INIT | `Init`, `Initialize` | Object initialization |
| POLL | `Poll`, `Update` | Per-frame update |
| CONSTRUCTOR | `??0` | Class constructor |
| DESTRUCTOR | `??1`, `~` | Class destructor |
| COPY | `Copy`, `::Copy(` | Deep copy |
| GENERIC | (default) | No specific guidance |

### Output Format

When enabled (treatment group) and function is typed, agents receive:

```markdown
## Function Type: LOAD (Binary Deserialization)

**Common patterns in Milo engine Load functions:**

1. **Version checking**: `LOAD_REVS(bs)` / `ASSERT_REVS(min, max)`
   - Always check if version macros match expected revision
   ...

**Expected mismatch**: 0.5-1% for ASSERT_REVS scheduling differences is normal.
```

## A/B Assignment

```python
# Control group: No function type guidance
# Treatment group: Gets function_type_guidance
enrichment_flags.get("function_types")  # True = treatment
```

## Success Metrics

### Primary: First-Iteration Success Rate
**Target**: +10% improvement on typed functions

Track success rate grouped by function type:
- LOAD functions: Often 99%+ is best achievable
- SAVE functions: Should mirror LOAD closely
- INIT functions: Usually can reach 100%
- POLL functions: Varies by complexity

### Secondary: Iterations to Match
Measure if type-specific guidance reduces tool calls.

## Results

_To be populated after collecting sufficient data_

### Data Collection Query

```sql
SELECT
  json_extract(enrichment_flags, '$.function_types') as treatment,
  COUNT(*) as n,
  AVG(CASE WHEN end_percent > start_percent THEN 1 ELSE 0 END) as success_rate
FROM attempts
WHERE enrichment_flags IS NOT NULL
  AND (
    notes LIKE '%Load%' OR
    notes LIKE '%Save%' OR
    notes LIKE '%Init%' OR
    notes LIKE '%Poll%'
  )
GROUP BY treatment;
```

## Recommendation

_To be determined after data collection_

## Notes

- Type classification runs early in context collection (no dependencies)
- GENERIC type returns empty guidance to minimize prompt size
- Templates based on observed patterns in RB3 decomp and DC3 codebase
- May need expansion for additional function types (Draw, Highlight, etc.)
