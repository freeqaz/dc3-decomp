# Experiment 3: RB2 Class Layouts

## Status: IMPLEMENTED

**Token cost**: ~1000 (varies by class size)
**Expected impact**: Medium - proactive vs reactive offset resolution
**Effort**: Medium

## Hypothesis

Pre-computed struct layouts from RB2 DWARF data prevent offset mismatch debugging loops by:
1. Providing member names/types upfront for offset references
2. Allowing agents to reason about field access without lookup tool calls
3. Reducing iterations spent on "what is at offset 0x48?" questions

## Implementation

### Files Modified
- `scripts/orchestrator/context_collector.py`:
  - Added `get_class_layout()` function
  - Added `format_class_layout()` for prompt formatting
  - Updated `collect_pre_run_context()` to include layout when enabled

### Dependencies
- `scripts/orchestrator/rb2_dwarf.py` - RB2 DWARF parser
- RB2 dump file at `~/code/milohax/rb3/doc/rb2_dump.cpp`

### Output Format

When enabled (treatment group), agents receive:

```markdown
## RB2 Class Layout: CharMirror

**Total size**: 0x120 (288 bytes)
**Parents**: CharPollable, Hmx::Object

### Member Offsets

| Offset | Size | Name | Type |
|--------|------|------|------|
| 0x0 | 0x4 | __vtable | void* |
| 0x4 | 0x4 | mDir | ObjectDir* |
| 0x48 | 0x10 | mPosition | Vector3 |
| 0x58 | 0x40 | mTransform | Transform |
...

*Note: Offsets from RB2 DWARF. DC3 offsets may differ slightly.*
```

## A/B Assignment

```python
# Control group: Must use lookup_struct_offset tool reactively
# Treatment group: Gets class_layout_summary upfront
enrichment_flags.get("rb2_layouts")  # True = treatment
```

## Success Metrics

### Primary: Struct Offset Debugging Iterations
**Target**: -50% iterations spent on struct offset issues

Baseline:
- Count lookup_struct_offset tool calls per attempt
- Track iterations with "offset" in agent notes

Treatment goal:
- Agents have layout information from start
- Fewer iterations debugging offset mismatches

### Secondary: Time to Resolution
Track total time for functions with STRUCT_OFFSET diff patterns.

## Results

_To be populated after collecting sufficient data_

### Data Collection Query

```sql
SELECT
  json_extract(enrichment_flags, '$.rb2_layouts') as treatment,
  COUNT(*) as n,
  AVG(iterations) as avg_iterations
FROM attempts
WHERE enrichment_flags IS NOT NULL
  AND notes LIKE '%offset%'
GROUP BY treatment;
```

## Recommendation

_To be determined after data collection_

## Notes

- Only available for classes in RB2 DWARF dump
- RB2 and DC3 share Milo engine, layouts usually match
- Some DC3-specific classes won't have RB2 layouts
- Token cost varies: small classes ~200, large classes ~2000
- Consider caching parsed layouts in SQLite for faster access

## Limitations

1. **RB2-only coverage**: DC3-specific classes not in RB2 DWARF
2. **Version differences**: Some offsets may differ between RB2/DC3
3. **Inheritance complexity**: Deep inheritance chains may have gaps
4. **Token cost**: Large classes (100+ members) consume significant tokens
