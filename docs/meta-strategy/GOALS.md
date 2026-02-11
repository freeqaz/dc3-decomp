# Goals: Realistic Targets for DC3 Decomp

Based on pattern analysis and current project state.

## Current State (as of analysis)

| Metric | Value |
|--------|-------|
| Total functions | 47,213 |
| Matched (100%) | 21,386 (45.3%) |
| Never attempted | 46,871 (99.3%) |
| NEAR_COMPLETE (99%+) | 606 |
| AT_LIMIT (stuck) | 137 |
| XDK (excluded) | ~1,000 |

## Realistic Achievable Targets

### By Match Percentage

| Target | Functions | Realistic? | Notes |
|--------|-----------|------------|-------|
| 100% match | ~4,000 | Yes | No unfixable patterns |
| 99-99.9% | ~16,000 | Yes | LINKER_MERGED ceiling |
| 97-99% | ~3,000 | Yes | BOOL_MASK/other ceilings |
| <97% | ~2,000 | Partial | Complex issues |
| XDK | ~1,000 | No | External SDK, excluded |

### Why 100% Is Rare

**~80% of near-match functions have LINKER_MERGED issues** creating permanent 0.5-3% gaps.

| Pattern | Prevalence | Impact | Fixable |
|---------|-----------|--------|---------|
| LINKER_MERGED | 80% | 0.5-3% gap | No |
| BOOL_MASK | 5% | ~3% gap | No |
| ASSERT_REVS | 5% | ~0.9% gap | No |
| CONTROL_FLOW | 40% | 1-5% gap | 70% success |
| REGISTER_SWAP | 80% | 1-3% gap | 30% success |

## Goal Categories

### Tier 1: Perfect Matches (100%)

**Target**: ~4,000 functions that can actually reach 100%

Criteria:
- `reachable_100 = 1`
- No LINKER_MERGED, BOOL_MASK, ASSERT_REVS patterns

Priority: Highest for maintaining code quality

### Tier 2: Practical Matches (97-99.9%)

**Target**: ~19,000 functions at their practical limit

Criteria:
- Has unfixable patterns but otherwise matched
- Current % is within ~1% of theoretical ceiling

Priority: Accept as "done" and move on

### Tier 3: Bulk Progress (50-96%)

**Target**: Make progress on the ~23,000 untouched functions

Criteria:
- Never attempted (`attempt_count = 0`)
- Not excluded

Priority: Run bulk Haiku sweeps to establish baselines

### Tier 4: Excluded (Skip)

**Target**: 0 effort on ~1,000 XDK functions

Criteria:
- `excluded = 1`
- Unit starts with `xdk/`

Priority: Do not attempt

## Success Metrics

### Short-Term (Phase 1)

| Metric | Target | Measurement |
|--------|--------|-------------|
| NEAR_COMPLETE triaged | 606 functions | Pattern analysis complete |
| XDK excluded | ~1,000 functions | `excluded = 1` marked |
| True 100%-achievable identified | ~100-150 | `reachable_100 = 1` AND high match |

### Medium-Term (Phase 2)

| Metric | Target | Measurement |
|--------|--------|-------------|
| Pattern columns populated | 5,000+ functions | All 80%+ analyzed |
| Call graph validated | Decision made | >= 10 functions with 20+ callers? |
| Scoring infrastructure | Complete | Priority views working |

### Long-Term (Phase 3)

| Metric | Target | Measurement |
|--------|--------|-------------|
| Perfect matches (100%) | +500-1,000 | From 21,386 baseline |
| Practical matches (97%+) | +5,000-10,000 | Bulk progress |
| Bulk attempted | 10,000+ | First-pass coverage |

## Anti-Goals (What NOT to Do)

1. **Don't chase 100% on LINKER_MERGED functions** - Accept 99.x% as done
2. **Don't count XDK progress** - It's excluded, don't measure it
3. **Don't over-invest in call graph** if validation fails - Use simpler scoring
4. **Don't skip triage** - Pattern analysis prevents wasted effort

## Decision Framework

### "Should I work on this function?"

```
IF excluded = 1:
    SKIP (XDK)
ELIF reachable_100 = 1 AND current_percent < 100:
    YES, priority target for 100%
ELIF current_percent >= 99 AND reachable_100 = 0:
    SKIP (at practical limit)
ELIF current_percent >= 80 AND primary_pattern IN (CONTROL_FLOW, COMPARISON):
    YES, likely fixable
ELIF attempt_count = 0:
    YES, bulk sweep candidate
ELIF attempt_count >= 5:
    SKIP (stuck, needs manual review)
ELSE:
    MAYBE, use scoring model
```

### "Is this function done?"

```
IF current_percent = 100:
    YES, perfect match
ELIF current_percent >= 99 AND has_linker_merged:
    YES, at practical limit (accept)
ELIF current_percent >= 97 AND has_bool_mask:
    YES, at practical limit (accept)
ELIF verdict = 'AT_LIMIT':
    YES, marked as stuck (accept for now)
ELSE:
    NO, more work possible
```

## Tracking Progress

### Weekly Metrics

```sql
-- Weekly progress snapshot
SELECT
    date('now') as snapshot_date,
    SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as perfect_matches,
    SUM(CASE WHEN current_percent >= 99 THEN 1 ELSE 0 END) as practical_matches,
    SUM(CASE WHEN reachable_100 = 1 AND current_percent >= 100 THEN 1 ELSE 0 END) as true_100,
    SUM(CASE WHEN attempt_count > 0 THEN 1 ELSE 0 END) as attempted,
    COUNT(*) as total
FROM functions
WHERE excluded = 0;
```

### Progress by Category

```sql
SELECT
    CASE
        WHEN current_percent >= 100 THEN '100% (done)'
        WHEN current_percent >= 99 AND reachable_100 = 0 THEN '99%+ (at limit)'
        WHEN current_percent >= 99 AND reachable_100 = 1 THEN '99%+ (can improve)'
        WHEN current_percent >= 90 THEN '90-99%'
        WHEN current_percent >= 50 THEN '50-89%'
        ELSE '<50%'
    END as category,
    COUNT(*) as count
FROM functions
WHERE excluded = 0
GROUP BY category
ORDER BY
    CASE category
        WHEN '100% (done)' THEN 1
        WHEN '99%+ (at limit)' THEN 2
        WHEN '99%+ (can improve)' THEN 3
        WHEN '90-99%' THEN 4
        WHEN '50-89%' THEN 5
        ELSE 6
    END;
```
