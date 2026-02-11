# Phase 1: Quick Wins

Immediate actions using **existing tools only**. No infrastructure changes required.

## Goal

Use current tooling more effectively to select higher-value decomp targets.

---

## 0. Pre-Flight: Triage NEAR_COMPLETE Functions

**Before starting target selection**, run batch pattern analysis on the 606 NEAR_COMPLETE functions (99%+ match). This determines which can actually reach 100% vs which are permanently limited.

### Run Batch Triage

```bash
# Analyze all 99%+ functions with pattern detection
./bin/objdiff-cli report analyze build/373307D9/report.json \
    --min-percent 99 --max-percent 100 \
    --limit 1000 \
    -f json-pretty > /tmp/near_complete_triage.json

# Summary by verdict
jq '.summary.by_verdict' /tmp/near_complete_triage.json

# Extract truly fixable functions
jq '.results.LIKELY_FIXABLE[] | {name: .demangled, pct: .fuzzy_match_percent, pattern: .primary_pattern}' \
    /tmp/near_complete_triage.json
```

### Expected Outcomes

Based on pattern analysis (~80% have LINKER_MERGED):

| Category | Expected Count | Action |
|----------|---------------|--------|
| LIKELY_FIXABLE | ~120 (20%) | Priority targets for 100% |
| MAYBE_FIXABLE | ~60 (10%) | Worth attempting |
| AT_LIMIT | ~420 (70%) | Mark as done, skip |

### Mark AT_LIMIT Functions

```bash
# After triage, update database
sqlite3 decomp.db "
UPDATE functions
SET verdict = 'AT_LIMIT'
WHERE symbol IN (
    SELECT symbol FROM functions
    WHERE current_percent >= 99
    AND verdict = 'NEAR_COMPLETE'
    -- Add pattern-based filtering here
);
"
```

### Why This Matters

Without triage, you'll waste expensive model time on functions that *cannot* reach 100% due to linker optimizations. A 99.5% function with LINKER_MERGED is done—accept it and move on.

---

## 0.5 Pre-Flight: Mark Excluded Code

Mark XDK SDK and other non-decompilable code as excluded.

```bash
# Add excluded column if not exists
sqlite3 decomp.db "ALTER TABLE functions ADD COLUMN excluded BOOLEAN DEFAULT 0;"

# Mark XDK modules (external SDK - not part of game code)
sqlite3 decomp.db "
UPDATE functions SET excluded = 1
WHERE unit LIKE 'xdk/%'
   OR unit LIKE 'xdk_ham/%';
"

# Verify exclusion count
sqlite3 decomp.db "
SELECT
    CASE WHEN excluded THEN 'excluded' ELSE 'included' END as status,
    COUNT(*) as count
FROM functions
GROUP BY excluded;
"
```

**Expected**: ~1,000+ functions excluded (nuispeech, xgraphics, d3d9i, etc.)

All subsequent queries should include `WHERE excluded = 0`.

---

## 1. SQL-Based Target Selection

Query the database directly for high-priority functions.

### Find Near-Matches (90%+ that aren't AT_LIMIT)

```bash
sqlite3 decomp.db "
SELECT symbol, demangled, size, current_percent, verdict
FROM functions
WHERE current_percent >= 90
  AND current_percent < 100
  AND excluded = 0
  AND (verdict IS NULL OR verdict NOT IN ('AT_LIMIT', 'COMPLETE'))
ORDER BY size DESC
LIMIT 20;
"
```

### Find Fresh Large Functions (Never Attempted)

```bash
sqlite3 decomp.db "
SELECT symbol, demangled, size, current_percent
FROM functions
WHERE (attempt_count = 0 OR attempt_count IS NULL)
  AND size > 200
  AND current_percent > 50
  AND excluded = 0
ORDER BY current_percent DESC, size DESC
LIMIT 20;
"
```

### Find Type Anchors by Name Pattern

```bash
# Constructors
sqlite3 decomp.db "
SELECT symbol, demangled, size, current_percent
FROM functions
WHERE demangled LIKE '%::%'
  AND (demangled LIKE '%ctor%' OR (demangled LIKE '%::%(%' AND demangled NOT LIKE '%~%'))
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;
"

# Destructors
sqlite3 decomp.db "
SELECT symbol, demangled, size, current_percent
FROM functions
WHERE (demangled LIKE '%~%' OR demangled LIKE '%dtor%')
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC
LIMIT 20;
"
```

---

## 2. objdiff-cli Report Analysis

Use the built-in analysis for verdict-based filtering.

### Find LIKELY_FIXABLE Functions

```bash
./bin/objdiff-cli report analyze build/373307D9/report.json \
    --min-percent 80 --max-percent 99 \
    --limit 50 \
    -f json-pretty | jq '.results.LIKELY_FIXABLE[:20]'
```

### Find Functions by Subsystem

```bash
# System math (well-understood, RB3 reference)
./bin/objdiff-cli report query build/373307D9/report.json \
    --functions --unit "*system/math*" \
    --min-percent 50 --max-percent 99 \
    -f csv
```

### Quick Status Check

```bash
./bin/objdiff-cli report query build/373307D9/report.json \
    --functions --min-percent 95 -f csv | wc -l
# Shows count of 95%+ functions
```

---

## 3. RB3 Reference Targeting

Prioritize functions in subsystems shared with Rock Band 3.

### High-Overlap Subsystems

| Subsystem | Overlap | Priority |
|-----------|---------|----------|
| system/math | ~90% | HIGH |
| system/utl | ~80% | HIGH |
| system/os | ~70% | MEDIUM |
| system/rndobj | ~60% | MEDIUM |
| system/char | ~50% | MEDIUM |

### Find RB3 Cross-Reference Candidates

```bash
# List DC3 functions in shared subsystems
./bin/objdiff-cli report query build/373307D9/report.json \
    --functions --unit "*system/math*" \
    --min-percent 0 --max-percent 90 \
    -f json | jq -r '.[].demangled' > /tmp/dc3_math_funcs.txt

# Check which exist in RB3
for func in $(cat /tmp/dc3_math_funcs.txt | head -20); do
    grep -l "$func" ../rb3/src/system/math/*.cpp 2>/dev/null && echo "HAS_REF: $func"
done
```

---

## 4. Manual Priority Queue

Create a simple priority list without infrastructure.

### Priority Criteria (Manual Assessment)

1. **Tier 1 - Do First**
   - 90%+ match AND LIKELY_FIXABLE
   - Small size (< 200 bytes)
   - Has RB3 reference
   - Completes a file (last function in unit)

2. **Tier 2 - Queue Next**
   - 70-89% match
   - Constructor/destructor (type anchor)
   - In well-understood subsystem

3. **Tier 3 - When Available**
   - 50-69% match
   - Large but important functions
   - DC3-specific code

### Example Workflow

```bash
# Step 1: Generate candidates
./bin/objdiff-cli report analyze build/373307D9/report.json \
    --min-percent 85 --limit 100 -f json > /tmp/candidates.json

# Step 2: Manual review and sort
cat /tmp/candidates.json | jq -r '
    .results.LIKELY_FIXABLE[] |
    "\(.match_percent)% \(.size)B \(.symbol)"
' | sort -rn | head -30

# Step 3: Pick top 8-10 for parallel agents
# (Ensure no two are in the same source file to avoid conflicts)
```

---

## 5. File Completion Strategy

Prioritize functions that complete a source file.

### Find Almost-Complete Files

```bash
sqlite3 decomp.db "
SELECT
    unit,
    COUNT(*) as total,
    SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as matched,
    ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) / COUNT(*), 1) as pct
FROM functions
WHERE excluded = 0
GROUP BY unit
HAVING pct >= 80 AND pct < 100
ORDER BY pct DESC, total ASC
LIMIT 20;
"
```

### Find the Remaining Functions in Those Files

```bash
# For a specific unit
sqlite3 decomp.db "
SELECT symbol, demangled, size, current_percent
FROM functions
WHERE unit = 'system/math/Trig'
  AND current_percent < 100
  AND excluded = 0
ORDER BY current_percent DESC;
"
```

---

## 6. Parallel Agent Deployment

Launch multiple agents on non-conflicting targets.

### Safety Rules
- Never assign two agents to the same source file
- Prefer different subsystems for maximum isolation
- Limit to 8-10 concurrent agents

### Quick Target Selection Script

```bash
#!/bin/bash
# select_targets.sh - Pick N non-conflicting targets

N=${1:-8}

sqlite3 decomp.db "
SELECT symbol, unit
FROM functions
WHERE current_percent >= 85
  AND current_percent < 100
  AND excluded = 0
  AND verdict = 'LIKELY_FIXABLE'
ORDER BY current_percent DESC
" | awk -F'|' '
{
    unit = $2
    if (!(unit in seen)) {
        seen[unit] = 1
        print $1
        count++
        if (count >= '$N') exit
    }
}
'
```

---

## Summary: Phase 1 Actions

| Action | Command/Tool | Time |
|--------|--------------|------|
| Query near-matches | `sqlite3 decomp.db` | 1 min |
| Find LIKELY_FIXABLE | `objdiff-cli report analyze` | 2 min |
| Check RB3 references | `grep` in rb3/src | 5 min |
| Select parallel targets | `select_targets.sh` | 1 min |
| Deploy agents | Manual launch | Ongoing |

**Total setup time**: ~10 minutes to get a prioritized work queue.

See [SQL_QUERIES.md](SQL_QUERIES.md) for more query examples.
