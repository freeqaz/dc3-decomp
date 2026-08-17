# A/B Testing Infrastructure

This document describes the A/B testing infrastructure for context enrichment experiments.

## Overview

The infrastructure enables rigorous measurement of enrichment impact through:
1. **Deterministic assignment** - Same symbol always gets same assignment
2. **Database tracking** - Each attempt records which enrichments were active
3. **Analysis tooling** - Statistical comparison of control vs treatment groups

## Database Schema

### `attempts.enrichment_flags` (v4 schema)

Added in migration v4:
```sql
ALTER TABLE attempts ADD COLUMN enrichment_flags TEXT;
-- JSON: {"diff_patterns": true, "function_types": false, ...}
```

Example value:
```json
{
  "diff_patterns": true,
  "function_types": false,
  "rb2_layouts": true,
  "attempt_diffs": false,
  "matched_siblings": true,
  "callee_signatures": false
}
```

## Assignment Logic

Located in `scripts/orchestrator/context_collector.py`:

```python
def assign_enrichment_group(symbol: str, experiment: str) -> bool:
    """
    Deterministic 50/50 A/B assignment based on symbol hash.
    """
    key = f"{symbol}:{experiment}"
    h = hashlib.md5(key.encode()).hexdigest()
    return int(h[0], 16) >= 8  # 0-7 = control, 8-f = treatment
```

### Key Properties

1. **Deterministic** - Same symbol+experiment always yields same result
2. **Independent** - Each experiment has its own assignment
3. **Balanced** - ~50/50 split due to uniform hash distribution
4. **Reproducible** - No randomness, same results on any machine

## Available Experiments

| ID | Name | Description |
|----|------|-------------|
| `diff_patterns` | Diff Pattern Classification | Pre-classify diff patterns (SCHEDULING, BOOL_MASK, etc.) |
| `function_types` | Function Type Templates | Type-specific guidance (Load/Save/Init/Poll) |
| `rb2_layouts` | RB2 Class Layouts | Pre-computed struct layouts from RB2 DWARF |
| `attempt_diffs` | Previous Attempt Diffs | Show actual code diffs from previous attempts |
| `matched_siblings` | Matched Siblings | Show 100% matched functions from same class |
| `callee_signatures` | Callee Signatures | Pre-resolved callee function signatures |

## Using the Infrastructure

### 1. Get Assignments for a Symbol

```python
from scripts.orchestrator.context_collector import get_enrichment_assignments

assignments = get_enrichment_assignments("?Load@CharMirror@@UAAXAAVBinStream@@@Z")
# Returns: {"diff_patterns": True, "function_types": False, ...}
```

### 2. Record with Enrichment Flags

```python
from scripts.orchestrator.database import record_attempt

record_attempt(
    function_id=123,
    session_id="sess_abc",
    model="haiku",
    start_percent=85.0,
    end_percent=92.0,
    exit_status="complete",
    enrichment_flags=assignments,  # Pass the dict
)
```

### 3. Analyze Results

```bash
# Analyze specific experiment
python scripts/analysis/analyze_enrichment.py --experiment diff_patterns

# Analyze all experiments
python scripts/analysis/analyze_enrichment.py --all --output docs/context-enrichment/

# JSON output for programmatic use
python scripts/analysis/analyze_enrichment.py --experiment diff_patterns --json
```

## Analysis Output

The analysis script produces:

```markdown
## Results Comparison

| Metric | Control | Treatment | Delta |
|--------|---------|-----------|-------|
| Success Rate | 15.2% | 18.7% | +3.5% |
| Avg Gain | +1.23% | +1.89% | +0.66% |
| AT_LIMIT Rate | 78.3% | 65.1% | -13.2% |

## Statistical Significance

- Z-score: 2.34
- P-value: 0.0193
- Significant at 0.05: Yes
```

## Implementation Pattern

When adding enrichment logic:

```python
def collect_pre_run_context(...):
    # Get assignments early
    assignments = get_enrichment_assignments(symbol)

    result = {...}

    # Experiment 1: Diff Pattern Classification
    if assignments.get("diff_patterns"):
        patterns = classify_diff_patterns(objdiff_result)
        result["pattern_classification"] = patterns

    # ... other experiments ...

    # Always include assignments for tracking
    result["enrichment_flags"] = assignments

    return result
```

## Metrics Tracked

### Primary (used for decisions)
- **Success rate** - % of attempts that improve match%
- **False AT_LIMIT rate** - % incorrectly declared unfixable
- **Average gain** - Mean % improvement when successful

### Secondary (informational)
- Iterations to verdict
- Token usage per attempt
- Time to completion

## Sample Size Requirements

For 95% confidence with 5% minimum detectable effect:
- ~400 samples per group needed
- At current rate (~100 attempts/day), need ~8 days per experiment
- Run multiple experiments in parallel via independent assignment

## Best Practices

1. **Don't change assignments mid-experiment** - breaks comparisons
2. **Run to completion** - partial data may be misleading
3. **Check balance** - verify ~50/50 split before analysis
4. **Document changes** - note any protocol changes in experiment docs
5. **Multiple metrics** - a single metric can be misleading

## Debugging

### Check assignment for a symbol
```bash
python -c "
from scripts.orchestrator.context_collector import get_enrichment_assignments
print(get_enrichment_assignments('?Load@CharMirror@@UAAXAAVBinStream@@@Z'))
"
```

### Query attempts by experiment
```sql
SELECT
  json_extract(enrichment_flags, '$.diff_patterns') as treatment,
  COUNT(*) as n,
  AVG(end_percent - start_percent) as avg_gain
FROM attempts
WHERE enrichment_flags IS NOT NULL
GROUP BY treatment;
```
