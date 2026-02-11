# SQL Queries for Prioritization

Ready-to-use queries against `decomp.db`. Copy-paste into `sqlite3 decomp.db`.

**Important**: All queries should include `WHERE excluded = 0` to filter out XDK and other non-decompilable code.

---

## Pattern-Aware Queries (Recommended)

These queries use pattern detection to find functions that can actually reach 100%.

### Functions That Can Reach 100%

```sql
-- Find targets that can actually achieve perfect match
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent,
    primary_pattern
FROM functions
WHERE reachable_100 = 1
  AND current_percent >= 80
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC, size ASC
LIMIT 30;
```

### Pattern Distribution Summary

```sql
-- Understand what's blocking progress
SELECT
    CASE
        WHEN has_linker_merged THEN 'LINKER_MERGED (unfixable)'
        WHEN has_bool_mask THEN 'BOOL_MASK (unfixable)'
        WHEN has_assert_revs THEN 'ASSERT_REVS (unfixable)'
        WHEN reachable_100 = 1 THEN 'CAN_REACH_100'
        ELSE 'UNANALYZED'
    END as category,
    COUNT(*) as count,
    ROUND(AVG(current_percent), 1) as avg_match
FROM functions
WHERE current_percent >= 80 AND excluded = 0
GROUP BY category
ORDER BY count DESC;
```

### Near-Complete That Are Actually Done

```sql
-- Functions at 99%+ that are limited by patterns (accept and move on)
SELECT symbol, demangled, current_percent,
    CASE
        WHEN has_linker_merged THEN 'LINKER_MERGED'
        WHEN has_bool_mask THEN 'BOOL_MASK'
        WHEN has_assert_revs THEN 'ASSERT_REVS'
    END as blocker
FROM functions
WHERE current_percent >= 99
  AND reachable_100 = 0
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 30;
```

### True Quick Wins (Fixable Patterns)

```sql
-- High-match functions with fixable patterns
SELECT symbol, demangled, size, current_percent, primary_pattern
FROM functions
WHERE reachable_100 = 1
  AND current_percent >= 90
  AND primary_pattern IN ('CONTROL_FLOW', 'COMPARISON_STYLE')
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;
```

---

## Exclusion Queries

### Mark XDK as Excluded

```sql
-- Run once to exclude SDK code
UPDATE functions SET excluded = 1
WHERE unit LIKE 'xdk/%' OR unit LIKE 'xdk_ham/%';
```

### Check Exclusion Status

```sql
SELECT
    CASE WHEN excluded THEN 'excluded' ELSE 'included' END as status,
    COUNT(*) as count
FROM functions
GROUP BY excluded;
```

### Verify No XDK in Results

```sql
-- Should return 0 rows
SELECT COUNT(*) as xdk_in_results
FROM functions
WHERE excluded = 0 AND unit LIKE 'xdk/%';
```

---

## Basic Queries

### Top Priority Functions (Simple)

```sql
-- Quick priority based on match % and size
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent,
    verdict
FROM functions
WHERE current_percent >= 80
  AND current_percent < 100
  AND excluded = 0
  AND (verdict IS NULL OR verdict NOT IN ('AT_LIMIT', 'COMPLETE'))
ORDER BY current_percent DESC, size DESC
LIMIT 30;
```

### Fresh Targets (Never Attempted)

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE attempt_count = 0
  AND current_percent > 50
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC, size DESC
LIMIT 30;
```

### Large Unmatched Functions

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE current_percent < 100
  AND size > 500
ORDER BY size DESC
LIMIT 30;
```

---

## Type Anchor Queries

### Constructors

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE (
    demangled LIKE '%::%::%(%'
    OR symbol LIKE '%??0%'
    OR demangled LIKE '%__ct%'
)
AND current_percent < 100
ORDER BY current_percent DESC
LIMIT 30;
```

### Destructors

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE (
    demangled LIKE '%::~%'
    OR symbol LIKE '%??1%'
    OR demangled LIKE '%__dt%'
)
AND current_percent < 100
ORDER BY current_percent DESC
LIMIT 30;
```

### Virtual Functions

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE (
    demangled LIKE '%virtual%'
    OR demangled LIKE '%vftable%'
)
AND current_percent < 100
ORDER BY current_percent DESC
LIMIT 30;
```

---

## Subsystem Queries

### Functions by Subsystem

```sql
-- Replace 'system/math' with target subsystem
SELECT
    symbol,
    demangled,
    size,
    current_percent
FROM functions
WHERE unit LIKE 'system/math%'
  AND current_percent < 100
ORDER BY current_percent DESC;
```

### Subsystem Progress Summary

```sql
SELECT
    CASE
        WHEN unit LIKE 'system/%' THEN substr(unit, 1, instr(substr(unit, 8), '/') + 6)
        WHEN unit LIKE 'lazer/%' THEN substr(unit, 1, instr(substr(unit, 7), '/') + 5)
        ELSE 'other'
    END as subsystem,
    COUNT(*) as total,
    SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as matched,
    ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) / COUNT(*), 1) as pct
FROM functions
GROUP BY subsystem
ORDER BY pct DESC;
```

---

## File Completion Queries

### Near-Complete Files (80%+)

```sql
SELECT
    unit,
    COUNT(*) as total,
    SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as matched,
    ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) / COUNT(*), 1) as pct
FROM functions
GROUP BY unit
HAVING pct >= 80 AND pct < 100
ORDER BY pct DESC, total ASC
LIMIT 30;
```

### Remaining Functions in a Unit

```sql
-- Replace 'system/math/Trig' with target unit
SELECT
    symbol,
    demangled,
    size,
    current_percent
FROM functions
WHERE unit = 'system/math/Trig'
  AND current_percent < 100
ORDER BY current_percent DESC;
```

### One Function Left

```sql
SELECT
    unit,
    symbol,
    demangled,
    size,
    current_percent
FROM functions f
WHERE current_percent < 100
  AND unit IN (
    SELECT unit FROM functions
    GROUP BY unit
    HAVING SUM(CASE WHEN current_percent < 100 THEN 1 ELSE 0 END) = 1
  )
ORDER BY unit;
```

---

## Verdict-Based Queries

### LIKELY_FIXABLE Functions

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE verdict = 'LIKELY_FIXABLE'
ORDER BY current_percent DESC, size DESC
LIMIT 30;
```

### AT_LIMIT Functions (Skip These)

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE verdict = 'AT_LIMIT'
ORDER BY current_percent DESC
LIMIT 30;
```

### Unknown Verdict (Need Analysis)

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent
FROM functions
WHERE verdict IS NULL
  AND current_percent >= 80
  AND current_percent < 100
ORDER BY current_percent DESC
LIMIT 30;
```

---

## Attempt History Queries

### Most Attempted (Stuck Functions)

```sql
SELECT
    symbol,
    demangled,
    attempt_count,
    current_percent,
    best_percent
FROM functions
WHERE attempt_count >= 3
ORDER BY attempt_count DESC
LIMIT 20;
```

### Recently Improved

```sql
SELECT
    f.symbol,
    f.demangled,
    a.start_percent,
    a.end_percent,
    a.end_percent - a.start_percent as improvement,
    a.model
FROM functions f
JOIN attempts a ON f.symbol = a.symbol
WHERE a.end_percent > a.start_percent
ORDER BY a.created_at DESC
LIMIT 20;
```

### Cost per Function

```sql
SELECT
    symbol,
    SUM(actual_cost_usd) as total_cost,
    COUNT(*) as attempts,
    MAX(end_percent) as best_match
FROM attempts
GROUP BY symbol
HAVING total_cost > 0
ORDER BY total_cost DESC
LIMIT 20;
```

---

## Scoring Queries (After Phase 2)

### Top Priority (Computed Scores)

```sql
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent,
    ease_score,
    impact_score,
    confidence_score,
    priority_score
FROM functions
WHERE current_percent < 100
  AND verdict != 'AT_LIMIT'
ORDER BY priority_score DESC
LIMIT 30;
```

### High Impact (Fan-In)

```sql
SELECT
    symbol,
    demangled,
    fan_in,
    current_percent,
    priority_score
FROM functions
WHERE fan_in >= 5
  AND current_percent < 100
ORDER BY fan_in DESC
LIMIT 30;
```

### Easy Wins (High Ease + High Match)

```sql
SELECT
    symbol,
    demangled,
    size,
    current_percent,
    ease_score
FROM functions
WHERE ease_score >= 60
  AND current_percent >= 80
  AND current_percent < 100
ORDER BY ease_score DESC, current_percent DESC
LIMIT 30;
```

---

## Parallel Agent Queries

### Select Non-Conflicting Targets

```sql
-- Get one high-priority function per unit
WITH ranked AS (
    SELECT
        symbol,
        demangled,
        unit,
        current_percent,
        priority_score,
        ROW_NUMBER() OVER (PARTITION BY unit ORDER BY priority_score DESC) as rn
    FROM functions
    WHERE current_percent < 100
      AND verdict NOT IN ('AT_LIMIT', 'COMPLETE')
      AND locked_by IS NULL
)
SELECT symbol, demangled, unit, current_percent, priority_score
FROM ranked
WHERE rn = 1
ORDER BY priority_score DESC
LIMIT 10;
```

### Currently Locked Functions

```sql
SELECT
    symbol,
    demangled,
    locked_by,
    locked_at
FROM functions
WHERE locked_by IS NOT NULL
ORDER BY locked_at DESC;
```

### Stale Locks (> 2 hours)

```sql
SELECT
    symbol,
    demangled,
    locked_by,
    locked_at,
    ROUND((julianday('now') - julianday(locked_at)) * 24, 1) as hours_locked
FROM functions
WHERE locked_by IS NOT NULL
  AND locked_at < datetime('now', '-2 hours');
```

---

## Quick Stats

### Overall Progress

```sql
SELECT
    COUNT(*) as total_functions,
    SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as matched,
    ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) / COUNT(*), 2) as match_pct,
    SUM(size) as total_bytes,
    SUM(CASE WHEN current_percent >= 100 THEN size ELSE 0 END) as matched_bytes,
    ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN size ELSE 0 END) / SUM(size), 2) as bytes_pct
FROM functions;
```

### Progress by Range

```sql
SELECT
    CASE
        WHEN current_percent >= 100 THEN '100% (matched)'
        WHEN current_percent >= 90 THEN '90-99%'
        WHEN current_percent >= 70 THEN '70-89%'
        WHEN current_percent >= 50 THEN '50-69%'
        WHEN current_percent >= 0 THEN '0-49%'
        ELSE 'unknown'
    END as range,
    COUNT(*) as count,
    SUM(size) as bytes
FROM functions
GROUP BY range
ORDER BY
    CASE range
        WHEN '100% (matched)' THEN 1
        WHEN '90-99%' THEN 2
        WHEN '70-89%' THEN 3
        WHEN '50-69%' THEN 4
        WHEN '0-49%' THEN 5
        ELSE 6
    END;
```

### Verdict Distribution

```sql
SELECT
    COALESCE(verdict, 'unanalyzed') as verdict,
    COUNT(*) as count
FROM functions
WHERE current_percent < 100
GROUP BY verdict
ORDER BY count DESC;
```
