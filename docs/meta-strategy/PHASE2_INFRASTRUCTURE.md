# Phase 2: Scoring Infrastructure

Build database infrastructure to support systematic ease x impact x confidence scoring.

## Goal

Move from ad-hoc queries to computed, queryable priority scores that update automatically.

**Key Insight**: Pattern-based fixability is the most important factor. A function with LINKER_MERGED at 99% is permanently limited—no amount of effort will reach 100%.

---

## 1. Database Schema Extensions

### Add Pattern Detection Columns

```sql
-- Pattern-based fixability (CRITICAL - add first)
ALTER TABLE functions ADD COLUMN has_linker_merged BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN has_bool_mask BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN has_assert_revs BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN has_ltcg_pooling BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN primary_pattern TEXT;
ALTER TABLE functions ADD COLUMN reachable_100 BOOLEAN DEFAULT 1;

-- Exclusion tracking
ALTER TABLE functions ADD COLUMN excluded BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN exclusion_reason TEXT;

CREATE INDEX IF NOT EXISTS idx_functions_reachable ON functions(reachable_100);
CREATE INDEX IF NOT EXISTS idx_functions_excluded ON functions(excluded);
```

### Add Call Graph Table

```sql
-- Track caller/callee relationships
CREATE TABLE IF NOT EXISTS call_edges (
    caller_symbol TEXT NOT NULL,
    callee_symbol TEXT NOT NULL,
    call_count INTEGER DEFAULT 1,
    PRIMARY KEY (caller_symbol, callee_symbol)
);

CREATE INDEX IF NOT EXISTS idx_call_edges_callee ON call_edges(callee_symbol);
CREATE INDEX IF NOT EXISTS idx_call_edges_caller ON call_edges(caller_symbol);
```

### Add Scoring Columns to Functions Table

```sql
-- Add scoring columns
ALTER TABLE functions ADD COLUMN fan_in INTEGER DEFAULT 0;
ALTER TABLE functions ADD COLUMN fan_out INTEGER DEFAULT 0;
ALTER TABLE functions ADD COLUMN is_constructor BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN is_destructor BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN is_virtual BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN has_rb3_ref BOOLEAN DEFAULT 0;
ALTER TABLE functions ADD COLUMN string_ref_count INTEGER DEFAULT 0;
ALTER TABLE functions ADD COLUMN ease_score INTEGER DEFAULT 0;
ALTER TABLE functions ADD COLUMN impact_score INTEGER DEFAULT 0;
ALTER TABLE functions ADD COLUMN confidence_score INTEGER DEFAULT 0;
ALTER TABLE functions ADD COLUMN priority_score REAL DEFAULT 0;

-- Parallel agent locking (prevents conflicts)
ALTER TABLE functions ADD COLUMN locked_by TEXT DEFAULT NULL;
ALTER TABLE functions ADD COLUMN locked_at DATETIME DEFAULT NULL;

CREATE INDEX IF NOT EXISTS idx_functions_priority ON functions(priority_score DESC);
CREATE INDEX IF NOT EXISTS idx_functions_locked ON functions(locked_by);
```

---

## 2. Call Graph Extraction

### Batch Extract via Ghidra MCP

```python
#!/usr/bin/env python3
"""extract_callgraph.py - Populate call_edges table from Ghidra"""

import sqlite3
import json
from ghidra_mcp_client import GhidraMCP

def extract_call_edges(db_path, limit=5000):
    conn = sqlite3.connect(db_path)
    mcp = GhidraMCP()

    # Get unprocessed functions
    cursor = conn.execute("""
        SELECT symbol FROM functions
        WHERE fan_in = 0 AND fan_out = 0
        LIMIT ?
    """, (limit,))

    for (symbol,) in cursor.fetchall():
        try:
            xrefs = mcp.list_cross_references(symbol)

            # Insert caller edges (who calls this function)
            for caller in xrefs.get('callers', []):
                conn.execute("""
                    INSERT OR IGNORE INTO call_edges (caller_symbol, callee_symbol)
                    VALUES (?, ?)
                """, (caller['symbol'], symbol))

            # Insert callee edges (who this function calls)
            for callee in xrefs.get('callees', []):
                conn.execute("""
                    INSERT OR IGNORE INTO call_edges (caller_symbol, callee_symbol)
                    VALUES (?, ?)
                """, (symbol, callee['symbol']))

        except Exception as e:
            print(f"Error processing {symbol}: {e}")
            continue

    conn.commit()
    conn.close()

if __name__ == '__main__':
    extract_call_edges('decomp.db')
```

### Update Fan-In/Fan-Out Counts

```sql
-- Compute fan-in (how many functions call this one)
UPDATE functions SET fan_in = (
    SELECT COUNT(*) FROM call_edges WHERE callee_symbol = functions.symbol
);

-- Compute fan-out (how many functions this one calls)
UPDATE functions SET fan_out = (
    SELECT COUNT(*) FROM call_edges WHERE caller_symbol = functions.symbol
);
```

---

## 3. Type Anchor Detection

### Detect Constructors/Destructors

```sql
-- Mark constructors (MSVC mangled names)
UPDATE functions SET is_constructor = 1
WHERE demangled LIKE '%::%::%(%'  -- Class::Class(...)
   OR symbol LIKE '%??0%'         -- MSVC ctor mangling
   OR demangled LIKE '%__ct%';    -- Alternative pattern

-- Mark destructors
UPDATE functions SET is_destructor = 1
WHERE demangled LIKE '%::~%'      -- Class::~Class
   OR symbol LIKE '%??1%'         -- MSVC dtor mangling
   OR demangled LIKE '%__dt%';    -- Alternative pattern

-- Mark virtual functions (heuristic: in vtable or has virtual keyword)
UPDATE functions SET is_virtual = 1
WHERE demangled LIKE '%virtual%'
   OR symbol LIKE '%?_%@%';       -- Virtual function mangling pattern
```

### Detect RB3 References

```python
#!/usr/bin/env python3
"""mark_rb3_refs.py - Mark functions with RB3 reference implementations"""

import sqlite3
import os
import glob

RB3_SRC = '../rb3/src'

def find_rb3_references(db_path):
    conn = sqlite3.connect(db_path)

    # Get all RB3 source files
    rb3_files = glob.glob(f'{RB3_SRC}/**/*.cpp', recursive=True)

    # Read all RB3 content
    rb3_content = {}
    for f in rb3_files:
        with open(f, 'r') as fp:
            rb3_content[f] = fp.read()

    # Check each DC3 function
    cursor = conn.execute("SELECT symbol, demangled FROM functions")
    for symbol, demangled in cursor.fetchall():
        # Extract class::method if available
        if demangled and '::' in demangled:
            method_name = demangled.split('(')[0].split('::')[-1]
            class_name = demangled.split('::')[0] if '::' in demangled else ''

            # Search in RB3
            for filepath, content in rb3_content.items():
                if method_name in content and class_name in content:
                    conn.execute(
                        "UPDATE functions SET has_rb3_ref = 1 WHERE symbol = ?",
                        (symbol,)
                    )
                    break

    conn.commit()
    conn.close()

if __name__ == '__main__':
    find_rb3_references('decomp.db')
```

---

## 3.5 Pattern-Based Classification (CRITICAL)

This step populates the pattern columns that drive the scoring model. Without this, we can't distinguish functions that can reach 100% from those permanently limited.

### Batch Pattern Detection

```python
#!/usr/bin/env python3
"""detect_patterns.py - Populate pattern columns from objdiff analysis"""

import sqlite3
import subprocess
import json

def detect_patterns(db_path, min_percent=80, limit=5000):
    """Run objdiff --analyze on functions and extract patterns."""
    conn = sqlite3.connect(db_path)

    # Get candidates (80%+ match, not excluded, not complete)
    cursor = conn.execute("""
        SELECT symbol FROM functions
        WHERE current_percent >= ?
          AND current_percent < 100
          AND excluded = 0
          AND (has_linker_merged IS NULL OR has_linker_merged = 0)
        LIMIT ?
    """, (min_percent, limit))

    for (symbol,) in cursor.fetchall():
        try:
            # Run objdiff with pattern analysis
            result = subprocess.run(
                ['./bin/objdiff-cli', 'diff', '-p', '.', symbol, '-f', 'json', '--analyze'],
                capture_output=True, text=True, timeout=30
            )

            if result.returncode != 0:
                continue

            analysis = json.loads(result.stdout)
            patterns = analysis.get('patterns', [])

            # Extract pattern flags
            has_linker = any(p['pattern'] == 'LINKER_MERGED' for p in patterns)
            has_bool = any(p['pattern'] == 'BOOL_MASK' for p in patterns)
            has_assert = any(p['pattern'] == 'ASSERT_REVS' for p in patterns)
            has_ltcg = any(p['pattern'] == 'LTCG_POOLING' for p in patterns)

            # Primary pattern (first fixability-relevant one)
            primary = None
            for p in patterns:
                if p.get('fixability') in ['likely_fixable', 'maybe_fixable']:
                    primary = p['pattern']
                    break

            # Compute reachable_100
            reachable = not any([has_linker, has_bool, has_assert, has_ltcg])

            # Update database
            conn.execute("""
                UPDATE functions SET
                    has_linker_merged = ?,
                    has_bool_mask = ?,
                    has_assert_revs = ?,
                    has_ltcg_pooling = ?,
                    primary_pattern = ?,
                    reachable_100 = ?
                WHERE symbol = ?
            """, (has_linker, has_bool, has_assert, has_ltcg, primary, reachable, symbol))

        except Exception as e:
            print(f"Error processing {symbol}: {e}")
            continue

    conn.commit()

    # Report statistics
    cursor = conn.execute("""
        SELECT
            SUM(has_linker_merged) as linker_merged,
            SUM(has_bool_mask) as bool_mask,
            SUM(has_assert_revs) as assert_revs,
            SUM(reachable_100) as can_reach_100,
            COUNT(*) as total_analyzed
        FROM functions
        WHERE has_linker_merged IS NOT NULL
    """)
    stats = cursor.fetchone()
    print(f"Pattern detection complete:")
    print(f"  LINKER_MERGED: {stats[0]}")
    print(f"  BOOL_MASK: {stats[1]}")
    print(f"  ASSERT_REVS: {stats[2]}")
    print(f"  Can reach 100%: {stats[3]} / {stats[4]}")

    conn.close()

if __name__ == '__main__':
    detect_patterns('decomp.db')
```

### Validate Pattern Detection

```sql
-- Check pattern distribution
SELECT
    CASE
        WHEN has_linker_merged THEN 'LINKER_MERGED'
        WHEN has_bool_mask THEN 'BOOL_MASK'
        WHEN has_assert_revs THEN 'ASSERT_REVS'
        WHEN reachable_100 THEN 'CAN_REACH_100'
        ELSE 'UNKNOWN'
    END as category,
    COUNT(*) as count,
    ROUND(AVG(current_percent), 1) as avg_match
FROM functions
WHERE current_percent >= 80 AND excluded = 0
GROUP BY category
ORDER BY count DESC;
```

### Expected Results

Based on empirical analysis (~80% have LINKER_MERGED):

| Category | Expected % | Implication |
|----------|-----------|-------------|
| LINKER_MERGED | ~80% | Cap at 97-99.5% |
| CAN_REACH_100 | ~20% | True 100% targets |
| BOOL_MASK | ~5% | Cap at ~97% |
| ASSERT_REVS | ~5% | Cap at ~99% |

---

## 3.6 Call Graph Validation

Before investing in full call graph infrastructure, validate that it provides value.

### Quick Validation Query

```bash
# Extract call graph for sample functions
python3 -c "
from scripts.orchestrator.mcp_server import get_cross_references
import sqlite3

conn = sqlite3.connect('decomp.db')
cursor = conn.execute('''
    SELECT symbol FROM functions
    WHERE current_percent < 100 AND excluded = 0
    LIMIT 100
''')

callee_counts = {}
for (symbol,) in cursor.fetchall():
    try:
        xrefs = get_cross_references(symbol)
        for callee in xrefs.get('callees', []):
            callee_counts[callee] = callee_counts.get(callee, 0) + 1
    except:
        pass

# Find high-impact functions (20+ callers)
high_impact = [(k, v) for k, v in callee_counts.items() if v >= 20]
print(f'Functions with 20+ callers: {len(high_impact)}')
for fn, count in sorted(high_impact, key=lambda x: -x[1])[:10]:
    print(f'  {count} callers: {fn}')
"
```

### Decision Point

If **< 10 functions have 20+ callers**, deprioritize call graph infrastructure. Focus on pattern-based scoring instead.

If **>= 10 functions have 20+ callers**, proceed with full fan-in/fan-out computation.

---

## 4. Score Computation

### Compute All Scores

```python
#!/usr/bin/env python3
"""compute_scores.py - Calculate ease, impact, confidence, priority"""

import sqlite3

def compute_ease(row):
    """Ease = how quickly can we match this?"""
    score = 0
    size = row['size'] or 0
    pct = row['current_percent'] or 0
    fan_out = row['fan_out'] or 0
    verdict = row['verdict']

    # PATTERN-BASED FIXABILITY (most important factor)
    if row['has_linker_merged']:
        score -= 40  # Permanent gap from ICF
    if row['has_bool_mask']:
        score -= 30  # Compiler bool handling
    if row['has_assert_revs']:
        score -= 25  # Instruction scheduling

    # If no unfixable patterns, bonus for fixable ones
    if row['reachable_100']:
        primary = row['primary_pattern']
        if primary in ['CONTROL_FLOW', 'COMPARISON_STYLE']:
            score += 20  # 60-70% success rate
        elif primary == 'REGISTER_SWAP':
            score += 10  # 30% success rate
        else:
            score += 30  # No detected issues

    # Size factor
    if size < 100: score += 20
    elif size < 300: score += 15
    elif size < 500: score += 5

    # Match percentage
    if pct >= 90: score += 15
    elif pct >= 70: score += 10
    elif pct >= 50: score += 5

    # Leaf function bonus
    if fan_out == 0: score += 10
    elif fan_out <= 3: score += 5

    # Reference implementation available
    if row['has_rb3_ref']: score += 15

    return max(0, min(score, 100))

def compute_impact(row):
    """Impact = how valuable is matching this?"""
    score = 0
    size, fan_in = row['size'], row['fan_in']
    is_ctor, is_dtor, is_virtual = row['is_constructor'], row['is_destructor'], row['is_virtual']
    unit = row['unit'] or ''

    # Fan-in
    if fan_in >= 10: score += 30
    elif fan_in >= 5: score += 20
    elif fan_in >= 1: score += 10

    # Type anchor bonus
    if is_ctor: score += 25
    elif is_dtor: score += 15
    elif is_virtual: score += 10

    # Size impact
    if size > 500: score += 20
    elif size > 200: score += 10

    # Shared subsystem bonus
    if unit.startswith('system/'): score += 15

    return min(score, 100)

def compute_confidence(row):
    """Confidence = how sure are we?"""
    score = 50  # Base
    has_rb3, string_refs, attempt_count = row['has_rb3_ref'], row['string_ref_count'], row['attempt_count']
    demangled, unit = row['demangled'], row['unit'] or ''

    # RB3 reference
    if has_rb3: score += 30

    # String references
    if string_refs: score += min(string_refs * 5, 15)

    # Demangled name quality
    if demangled and '::' in demangled: score += 10

    # Previous attempts
    if attempt_count == 0: score += 10
    elif attempt_count <= 2: score += 5
    elif attempt_count >= 3: score -= 10

    # Well-understood subsystems
    if any(unit.startswith(s) for s in ['system/math', 'system/utl', 'system/os']):
        score += 10

    return max(0, min(score, 100))

def update_scores(db_path):
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    cursor = conn.execute("""
        SELECT symbol, size, current_percent, fan_in, fan_out,
               is_constructor, is_destructor, is_virtual,
               has_rb3_ref, string_ref_count, attempt_count,
               demangled, unit, verdict,
               has_linker_merged, has_bool_mask, has_assert_revs,
               has_ltcg_pooling, primary_pattern, reachable_100, excluded
        FROM functions
        WHERE (current_percent < 100 OR current_percent IS NULL)
          AND excluded = 0
    """)

    for row in cursor.fetchall():
        ease = compute_ease(row)
        impact = compute_impact(row)
        confidence = compute_confidence(row)

        # Base priority
        base_priority = (ease * impact * confidence) / 10000

        # Apply reachable_100 multiplier
        reachable = row['reachable_100']
        multiplier = 1.5 if reachable else 0.5
        priority = base_priority * multiplier

        # AT_LIMIT functions get 0 priority
        if row['verdict'] == 'AT_LIMIT':
            priority = 0

        conn.execute("""
            UPDATE functions
            SET ease_score = ?, impact_score = ?, confidence_score = ?, priority_score = ?
            WHERE symbol = ?
        """, (ease, impact, confidence, priority, row['symbol']))

    conn.commit()
    conn.close()

if __name__ == '__main__':
    update_scores('decomp.db')
```

---

## 5. Priority Views

### Create Useful Views

```sql
-- Top priority functions (all, for general progress)
CREATE VIEW IF NOT EXISTS v_priority_queue AS
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent,
    ease_score,
    impact_score,
    confidence_score,
    priority_score,
    reachable_100,
    verdict
FROM functions
WHERE excluded = 0;

-- Functions that can reach 100% (for perfect-match campaigns)
CREATE VIEW IF NOT EXISTS v_reachable_100 AS
SELECT * FROM v_priority_queue
WHERE reachable_100 = 1
  AND current_percent < 100
  AND (verdict IS NULL OR verdict NOT IN ('AT_LIMIT', 'COMPLETE'))
ORDER BY priority_score DESC;

-- Top priority functions (original definition)
CREATE VIEW IF NOT EXISTS v_priority_queue_active AS
SELECT
    symbol,
    demangled,
    unit,
    size,
    current_percent,
    ease_score,
    impact_score,
    confidence_score,
    priority_score,
    reachable_100,
    verdict
FROM functions
WHERE current_percent < 100
  AND verdict != 'AT_LIMIT'
ORDER BY priority_score DESC;

-- Type anchors (constructors/destructors)
CREATE VIEW IF NOT EXISTS v_type_anchors AS
SELECT * FROM v_priority_queue
WHERE (is_constructor = 1 OR is_destructor = 1)
  AND current_percent < 100;

-- High-impact utilities (high fan-in)
CREATE VIEW IF NOT EXISTS v_high_impact AS
SELECT * FROM v_priority_queue
WHERE fan_in >= 5
  AND current_percent < 100
ORDER BY fan_in DESC, priority_score DESC;

-- Near-complete files (excluding XDK)
CREATE VIEW IF NOT EXISTS v_near_complete_units AS
SELECT
    unit,
    COUNT(*) as total,
    SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as matched,
    ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) / COUNT(*), 1) as completion_pct
FROM functions
WHERE excluded = 0
GROUP BY unit
HAVING completion_pct >= 70 AND completion_pct < 100
ORDER BY completion_pct DESC;

-- Pattern distribution summary
CREATE VIEW IF NOT EXISTS v_pattern_summary AS
SELECT
    CASE
        WHEN has_linker_merged THEN 'LINKER_MERGED'
        WHEN has_bool_mask THEN 'BOOL_MASK'
        WHEN has_assert_revs THEN 'ASSERT_REVS'
        WHEN reachable_100 THEN 'CAN_REACH_100'
        ELSE 'UNANALYZED'
    END as category,
    COUNT(*) as count,
    ROUND(AVG(current_percent), 1) as avg_match
FROM functions
WHERE excluded = 0 AND current_percent >= 80
GROUP BY category;
```

---

## 6. Refresh Workflow

### After Each Decomp Session

```bash
#!/bin/bash
# refresh_scores.sh - Update scores after progress

# 1. Regenerate report
ninja build/373307D9/report.json

# 2. Import new verdicts (if using report ingest)
python scripts/ingest_report.py

# 3. Recompute scores
python scripts/compute_scores.py

# 4. Show new top priorities
sqlite3 decomp.db "SELECT * FROM v_priority_queue LIMIT 20"
```

---

## Implementation Checklist

- [ ] Add schema extensions (call_edges table, scoring columns)
- [ ] Run call graph extraction (requires Ghidra MCP)
- [ ] Update fan-in/fan-out counts
- [ ] Detect type anchors (constructors, destructors)
- [ ] Mark RB3 references
- [ ] Implement score computation
- [ ] Create priority views
- [ ] Test refresh workflow

**Estimated effort**: 1-2 weeks of intermittent work
