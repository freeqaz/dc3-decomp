# Scoring Model: Ease x Impact x Confidence

## Overview

Each function receives three scores (0-100 scale each):
- **Ease**: How quickly can we match this?
- **Impact**: How valuable is matching this to overall progress?
- **Confidence**: How sure are we about what to do?

Plus a critical boolean:
- **Reachable100**: Can this function actually reach 100% match?

**Priority Score** = (Ease x Impact x Confidence) / 10000 x (1.5 if reachable_100 else 0.5)

Higher score = work on this first.

---

## Critical Insight: Pattern-Based Fixability

**~80% of near-match functions have unfixable compiler/linker patterns.**

Before scoring, we must classify functions by their *achievable ceiling*:

| Pattern | Fixable? | Typical Gap | Prevalence |
|---------|----------|-------------|------------|
| LINKER_MERGED (ICF) | No* | 0.5-3% | 80% of near-matches |
| BOOL_MASK | No | ~3% | 5% of functions |
| ASSERT_REVS | No | ~0.8-0.9% | Functions with revision checks |
| LTCG_POOLING | No | 0.5-1% | Varies |
| CONTROL_FLOW | Yes (70%) | 1-5% | 40% of near-matches |
| COMPARISON_STYLE | Yes (60%) | 0-1% | 24% of near-matches |
| REGISTER_SWAP | Partial (30%) | 1-3% | 80% of near-matches |

*\*LINKER_MERGED requires verification: use `mcp__orchestrator__lookup_merged_symbol` to confirm your call target is in the merged set. If NOT in set, investigate - you may be calling the wrong function.*

See [Pattern Reference](../decomp/patterns/INDEX.md) for full pattern reference.

---

## Ease Score (0-100)

Ease measures how likely we can get a clean match quickly.

### Factors

| Factor | Points | Rationale |
|--------|--------|-----------|
| **Pattern: No unfixable patterns** | +30 | Can reach 100% |
| **Pattern: Only CONTROL_FLOW/COMPARISON** | +20 | 60-70% success rate |
| **Pattern: Has REGISTER_SWAP only** | +10 | 30% success rate |
| **Pattern: Has LINKER_MERGED** | -40 | Permanent gap |
| **Pattern: Has BOOL_MASK** | -30 | Permanent gap |
| **Pattern: Has ASSERT_REVS** | -25 | Permanent gap |
| **Size < 100 bytes** | +20 | Small functions are simpler |
| **Size 100-300 bytes** | +15 | Medium complexity |
| **Size 300-500 bytes** | +5 | Larger but manageable |
| **Size > 500 bytes** | +0 | Complex, time-consuming |
| **Match >= 90%** | +15 | Near-match |
| **Match 70-89%** | +10 | Good starting point |
| **Match 50-69%** | +5 | Moderate work needed |
| **Leaf function (0 callees)** | +10 | No dependencies |
| **Few callees (1-3)** | +5 | Limited complexity |
| **Has RB3 reference** | +15 | Known implementation to follow |

### Formula

```python
def compute_ease(func):
    score = 0

    # PATTERN-BASED FIXABILITY (most important factor)
    if func.has_linker_merged:
        score -= 40  # Permanent gap from ICF
    if func.has_bool_mask:
        score -= 30  # Compiler bool handling
    if func.has_assert_revs:
        score -= 25  # Instruction scheduling

    # If no unfixable patterns, bonus for fixable ones
    if not any([func.has_linker_merged, func.has_bool_mask, func.has_assert_revs]):
        if func.primary_pattern in ['CONTROL_FLOW', 'COMPARISON_STYLE']:
            score += 20  # 60-70% success rate
        elif func.primary_pattern == 'REGISTER_SWAP':
            score += 10  # 30% success rate
        else:
            score += 30  # No detected issues

    # Size factor
    if func.size < 100:
        score += 20
    elif func.size < 300:
        score += 15
    elif func.size < 500:
        score += 5

    # Match percentage
    if func.current_percent >= 90:
        score += 15
    elif func.current_percent >= 70:
        score += 10
    elif func.current_percent >= 50:
        score += 5

    # Leaf function bonus
    if func.fan_out == 0:
        score += 10
    elif func.fan_out <= 3:
        score += 5

    # Reference implementation available
    if func.has_rb3_ref:
        score += 15

    return max(0, min(score, 100))
```

---

## Impact Score (0-100)

Impact measures how much matching this function helps overall progress.

### Factors

| Factor | Points | Rationale |
|--------|--------|-----------|
| **High fan-in (10+ callers)** | +30 | Stabilizes many call sites |
| **Medium fan-in (5-9 callers)** | +20 | Moderate propagation |
| **Low fan-in (1-4 callers)** | +10 | Some benefit |
| **Constructor** | +25 | Anchors class layout and vtable |
| **Destructor** | +15 | Confirms class structure |
| **Virtual function** | +10 | Helps vtable resolution |
| **Large size (> 500 bytes)** | +20 | More matched code |
| **Medium size (200-500 bytes)** | +10 | Decent contribution |
| **In shared subsystem (system/*)** | +15 | Benefits RB3 cross-reference |
| **Initializes globals/vtables** | +20 | Data anchor |

### Formula

```python
def compute_impact(func):
    score = 0

    # Fan-in (callers)
    fan_in = get_fan_in(func.symbol)
    if fan_in >= 10:
        score += 30
    elif fan_in >= 5:
        score += 20
    elif fan_in >= 1:
        score += 10

    # Type anchor bonus
    if is_constructor(func.demangled):
        score += 25
    elif is_destructor(func.demangled):
        score += 15
    elif is_virtual_function(func.demangled):
        score += 10

    # Size impact
    if func.size > 500:
        score += 20
    elif func.size > 200:
        score += 10

    # Shared subsystem
    if func.unit.startswith('system/'):
        score += 15

    return min(score, 100)
```

---

## Confidence Score (0-100)

Confidence measures how sure we are about what the function does and how to match it.

### Factors

| Factor | Points | Rationale |
|--------|--------|-----------|
| **RB3 reference exists** | +30 | Known working implementation |
| **Has string references** | +15 | Anchors for understanding |
| **Clear demangled name** | +10 | Know what it's supposed to do |
| **Good Ghidra decompile** | +15 | Clear pseudocode |
| **Previous attempts = 0** | +10 | Fresh target, not stuck |
| **Previous attempts = 1-2** | +5 | Some exploration done |
| **Previous attempts >= 3** | -10 | Likely difficult |
| **Similar function matched** | +15 | Pattern to follow |
| **In well-understood subsystem** | +10 | Context available |

### Formula

```python
def compute_confidence(func):
    score = 50  # Base confidence

    # RB3 reference
    if has_rb3_reference(func.unit, func.demangled):
        score += 30

    # String references
    string_refs = count_string_references(func.symbol)
    if string_refs > 0:
        score += min(string_refs * 5, 15)

    # Demangled name quality
    if func.demangled and '::' in func.demangled:
        score += 10

    # Previous attempts
    if func.attempt_count == 0:
        score += 10
    elif func.attempt_count <= 2:
        score += 5
    elif func.attempt_count >= 3:
        score -= 10

    # Subsystem familiarity
    well_understood = ['system/math', 'system/utl', 'system/os']
    if any(func.unit.startswith(s) for s in well_understood):
        score += 10

    return max(0, min(score, 100))
```

---

## Combined Priority Score

```python
def compute_reachable_100(func):
    """Determine if function can reach 100% match."""
    unfixable_patterns = [
        func.has_linker_merged,
        func.has_bool_mask,
        func.has_assert_revs,
        func.has_ltcg_pooling,
        func.has_fmadds_issue
    ]
    return not any(unfixable_patterns)

def compute_priority(func):
    ease = compute_ease(func)
    impact = compute_impact(func)
    confidence = compute_confidence(func)
    reachable_100 = compute_reachable_100(func)

    # Base priority
    base_priority = (ease * impact * confidence) / 10000

    # Boost functions that can reach 100%, penalize those that can't
    multiplier = 1.5 if reachable_100 else 0.5
    priority = base_priority * multiplier

    return {
        'symbol': func.symbol,
        'ease': ease,
        'impact': impact,
        'confidence': confidence,
        'reachable_100': reachable_100,
        'priority': priority
    }
```

### Score Interpretation

| Priority | Meaning |
|----------|---------|
| 70+ | Excellent target - do this now |
| 50-69 | Good target - queue it up |
| 30-49 | Moderate - consider if nothing better |
| 10-29 | Low priority - skip for now |
| < 10 | Avoid - too hard or low value |

### Reachable100 Filtering

For "100% match" campaigns, filter to only `reachable_100 = TRUE` functions.

For "maximize progress" campaigns, include all functions but let priority scoring handle ordering.

---

## Special Cases

### AT_LIMIT Functions
If verdict is AT_LIMIT, set priority to 0. These functions cannot be matched better with current knowledge.

**Note**: AT_LIMIT should be set based on pattern analysis, not just attempt count. A function with LINKER_MERGED at 99% is AT_LIMIT even on first attempt.

### COMPLETE Functions
Already matched - exclude from prioritization.

### Excluded Functions
Functions in excluded units (XDK SDK, stubs) should have `excluded = TRUE` and be filtered from all queries.

### Blocked Dependencies
If a function has unmatched callees that are required for understanding, reduce confidence by 20.

### Near-Complete but Limited
Functions at 98-99.5% with unfixable patterns:
- Mark `reachable_100 = FALSE`
- Consider "done" for practical purposes
- Low priority unless impact is very high

---

## Simplified SQL Approximation

For quick queries without full infrastructure:

```sql
SELECT
    symbol,
    demangled,
    size,
    current_percent,
    reachable_100,
    -- Simplified priority approximation
    (
        -- Pattern-based ease (primary factor)
        (CASE
            WHEN has_linker_merged THEN -40
            WHEN has_bool_mask THEN -30
            WHEN has_assert_revs THEN -25
            ELSE 30
        END) +
        -- Size factor
        (CASE WHEN size < 300 THEN 20 ELSE 5 END) +
        -- Match percentage
        (CASE WHEN current_percent >= 90 THEN 15 ELSE current_percent / 7 END) +
        -- Reference available
        (CASE WHEN has_rb3_ref THEN 15 ELSE 0 END) +
        -- Fresh target bonus
        (CASE WHEN attempt_count = 0 THEN 10 ELSE 0 END)
    ) * (CASE WHEN reachable_100 THEN 1.5 ELSE 0.5 END) as priority_score
FROM functions
WHERE current_percent < 100
  AND excluded = 0
  AND (verdict IS NULL OR verdict NOT IN ('AT_LIMIT', 'COMPLETE'))
ORDER BY priority_score DESC
LIMIT 50;
```

### Pattern-Aware Quick Query (Before Infrastructure)

Until pattern columns are populated, use verdict as proxy:

```sql
SELECT symbol, demangled, size, current_percent, verdict
FROM functions
WHERE current_percent >= 90 AND current_percent < 100
  AND excluded = 0
  AND verdict IN ('LIKELY_FIXABLE', 'MAYBE_FIXABLE')
ORDER BY current_percent DESC, size ASC
LIMIT 30;
```

See [SQL_QUERIES.md](SQL_QUERIES.md) for more ready-to-use queries.
