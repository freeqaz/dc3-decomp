# Context Enrichment Investigation

This directory contains documentation for A/B testing context enrichments in the decomp agent pipeline.

## Overview

Context enrichments are pre-computed data injected into agent prompts to reduce wasteful iterations. Each enrichment is tested via A/B assignment to measure actual impact.

## Status Summary

| Experiment | Status | Token Cost | Expected Impact | Priority |
|------------|--------|------------|-----------------|----------|
| [1. Diff Patterns](01-diff-patterns.md) | IMPLEMENTED | ~500 | High | 1 |
| [2. Function Types](02-function-types.md) | IMPLEMENTED | ~300 | High | 2 |
| [3. RB2 Layouts](03-rb2-layouts.md) | IMPLEMENTED | ~1000 | Medium | 3 |
| [4. Attempt Diffs](04-attempt-diffs.md) | IMPLEMENTED | ~2000 | Medium | 4 |
| [5. Matched Siblings](05-matched-siblings.md) | IMPLEMENTED | ~2000 | Medium | 5 |
| [6. Callee Signatures](06-callee-signatures.md) | STUB | ~1000 | Low | 6 |

## Documents

| Document | Description |
|----------|-------------|
| [00-BASELINE.md](00-BASELINE.md) | Pre-enrichment metrics baseline |
| [AB-TESTING.md](AB-TESTING.md) | A/B testing infrastructure docs |
| [01-diff-patterns.md](01-diff-patterns.md) | Exp 1: Diff pattern classification |
| [02-function-types.md](02-function-types.md) | Exp 2: Function type templates |
| [03-rb2-layouts.md](03-rb2-layouts.md) | Exp 3: RB2 class layouts |
| [04-attempt-diffs.md](04-attempt-diffs.md) | Exp 4: Previous attempt diffs |
| [05-matched-siblings.md](05-matched-siblings.md) | Exp 5: Matched sibling functions |
| [06-callee-signatures.md](06-callee-signatures.md) | Exp 6: Callee signatures (stub) |

## Quick Start

### Running Analysis

```bash
# Analyze all experiments
python scripts/analysis/analyze_enrichment.py --all

# Analyze specific experiment
python scripts/analysis/analyze_enrichment.py --experiment diff_patterns

# Generate markdown reports
python scripts/analysis/analyze_enrichment.py --all --output docs/context-enrichment/
```

### Checking Assignments

```python
from scripts.orchestrator.context_collector import get_enrichment_assignments

# Get all assignments for a symbol
assignments = get_enrichment_assignments("?Load@CharMirror@@UAAXAAVBinStream@@@Z")
print(assignments)
# {'diff_patterns': True, 'function_types': False, 'rb2_layouts': True, ...}
```

### Force Enabling/Disabling

```python
from scripts.orchestrator.context_collector import collect_pre_run_context

# Force all enrichments on
context = collect_pre_run_context(
    symbol="...",
    unit="...",
    project_dir="/path/to/dc3-decomp",
    worktree_dir="/tmp/worktree",
    enrichment_overrides={
        "diff_patterns": True,
        "function_types": True,
        "rb2_layouts": True,
        "attempt_diffs": True,
        "matched_siblings": True,
    }
)
```

## Implementation Files

| File | Role |
|------|------|
| `scripts/orchestrator/context_collector.py` | All enrichment implementations |
| `scripts/orchestrator/database.py` | Schema v4 with `enrichment_flags` |
| `scripts/orchestrator/rb2_dwarf.py` | RB2 class layout parser |
| `scripts/analysis/analyze_enrichment.py` | A/B analysis script |

## Key Metrics

### Primary (used for decisions)
1. **Success rate** - % of attempts that improve match%
2. **False AT_LIMIT rate** - % incorrectly declared unfixable
3. **Average gain** - Mean % improvement when successful

### Baseline (from 00-BASELINE.md)

| Band | Attempts | Success Rate | AT_LIMIT Rate |
|------|----------|--------------|---------------|
| `<30%` | 9,247 | 15.0% | 6.6% |
| `30-80%` | 319 | 41.1% | 67.1% |
| `80-95%` | 573 | 33.7% | 79.4% |
| `95%+` | 877 | 26.5% | 77.8% |

### Targets
- Reduce false AT_LIMIT by 30% in 80%+ band
- Improve success rate by 10% on typed functions
- Reduce iterations by 2 on functions with matched siblings

## Recommended Order

1. **Collect baseline** - Already done (00-BASELINE.md)
2. **Verify A/B infrastructure** - Test assignments are deterministic
3. **Run experiments 1-2** - Low effort, high expected impact
4. **Analyze after 50+ samples** - Generate comparison reports
5. **Decide on 3-5** - Based on 1-2 results
6. **Enable winners** - Remove A/B, make enrichments default

## Next Steps

1. [ ] Run orchestrator with enrichments enabled
2. [ ] Collect 50+ samples per experiment
3. [ ] Run `analyze_enrichment.py` for each experiment
4. [ ] Update experiment docs with results
5. [ ] Make recommendation (ENABLE/DISABLE/MODIFY)
6. [ ] Enable winning enrichments by default
