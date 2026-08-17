# Experiment 5: Matched Siblings

## Status: IMPLEMENTED

**Token cost**: ~2000 (varies by source size)
**Expected impact**: Medium - provides concrete pattern examples
**Effort**: Medium

## Hypothesis

Showing 100% matched functions from the same class provides concrete patterns that:
1. Demonstrate correct class member access
2. Show established API usage patterns
3. Guide initialization and serialization order
4. Provide working examples to emulate

## Implementation

### Files Modified
- `scripts/orchestrator/context_collector.py`:
  - Added `get_matched_siblings()` function
  - Added `format_matched_siblings()` for prompt formatting
  - Updated `collect_pre_run_context()` to include siblings when enabled

### Query Logic
```sql
SELECT symbol, demangled, current_percent, source_patch
FROM functions
WHERE current_percent >= 100
  AND symbol != :current_symbol
  AND (demangled LIKE '%ClassName::%' OR symbol LIKE '%@ClassName@%')
  AND source_patch IS NOT NULL
ORDER BY LENGTH(source_patch) ASC  -- Prefer shorter examples
LIMIT 3
```

### Token Management
- `MAX_SIBLING_SOURCE_CHARS = 2000` - limits per-sibling source size
- Max 3 siblings included by default
- Ordered by length to prefer simpler examples

### Output Format

When enabled (treatment group), agents receive:

```markdown
## Matched Sibling Functions

**Found 2 100% matched functions from the same class.**

Use these as reference patterns:

### 1. Init

**Symbol**: `?Init@CharMirror@@QAAXXZ`

**Implementation**:
\`\`\`cpp
void CharMirror::Init() {
    CharPollable::Init();
    mPosition = Vector3(0, 0, 0);
    mEnabled = true;
}
\`\`\`

### 2. Save

**Symbol**: `?Save@CharMirror@@UAEXAAVBinStream@@@Z`

**Implementation**:
\`\`\`cpp
void CharMirror::Save(BinStream& bs) {
    CharPollable::Save(bs);
    bs << mPosition;
}
\`\`\`

---
**Guidance**: Follow similar patterns from these matched siblings for class member access, initialization, and API usage.
```

## A/B Assignment

```python
# Control group: No sibling examples
# Treatment group: Gets matched_siblings_summary
enrichment_flags.get("matched_siblings")  # True = treatment
```

## Success Metrics

### Primary: Success Rate on Low-Match Functions
**Target**: +5% success rate on `<80%` functions with matched siblings

Hypothesis: Having concrete examples helps agents make bigger improvements.

### Secondary: Pattern Adherence
Track whether agent implementations follow sibling patterns.

## Results

_To be populated after collecting sufficient data_

### Data Collection Query

```sql
SELECT
  json_extract(enrichment_flags, '$.matched_siblings') as treatment,
  COUNT(*) as n,
  AVG(end_percent - start_percent) as avg_gain,
  AVG(CASE WHEN end_percent > start_percent THEN 1 ELSE 0 END) as success_rate
FROM attempts a
JOIN functions f ON a.function_id = f.id
WHERE a.enrichment_flags IS NOT NULL
  AND a.start_percent < 80
  AND EXISTS (
    SELECT 1 FROM functions f2
    WHERE f2.current_percent >= 100
      AND f2.demangled LIKE '%' || SUBSTR(f.demangled, 1, INSTR(f.demangled, '::') + 1) || '%'
  )
GROUP BY treatment;
```

## Recommendation

_To be determined after data collection_

## Notes

- Only useful if class has 100% matched functions with stored patches
- `source_patch` column must be populated by orchestrator on success
- Shorter siblings preferred to manage token budget
- May not help for classes with no matched functions

## Limitations

1. **Requires matched siblings**: No benefit if class is entirely unmatched
2. **Requires source_patch storage**: Orchestrator must store successful patches
3. **Token cost**: Multiple siblings consume significant tokens
4. **Pattern relevance**: Sibling patterns may not be relevant to current function
