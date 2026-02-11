# Phase 3: Agentic Automation

Automate target selection, model escalation, and parallel agent orchestration.

## Goal

Move from manual agent deployment to automated, continuous progress with minimal intervention.

---

## 1. Automated Target Selection

### Target Selector Script

```python
#!/usr/bin/env python3
"""select_targets.py - Automatically select N non-conflicting high-priority targets"""

import sqlite3
import argparse

def select_targets(db_path, count=8, min_priority=20):
    """Select top N targets ensuring no file conflicts."""
    conn = sqlite3.connect(db_path)

    cursor = conn.execute("""
        SELECT symbol, demangled, unit, size, current_percent, priority_score
        FROM v_priority_queue
        WHERE priority_score >= ?
          AND symbol NOT IN (SELECT symbol FROM functions WHERE locked_by IS NOT NULL)
        ORDER BY priority_score DESC
    """, (min_priority,))

    targets = []
    used_units = set()

    for row in cursor.fetchall():
        symbol, demangled, unit, size, pct, priority = row

        # Skip if unit already assigned (avoid conflicts)
        if unit in used_units:
            continue

        targets.append({
            'symbol': symbol,
            'demangled': demangled,
            'unit': unit,
            'size': size,
            'current_percent': pct,
            'priority_score': priority
        })
        used_units.add(unit)

        if len(targets) >= count:
            break

    conn.close()
    return targets

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-n', '--count', type=int, default=8)
    parser.add_argument('-m', '--min-priority', type=float, default=20)
    parser.add_argument('--db', default='decomp.db')
    parser.add_argument('--format', choices=['text', 'json'], default='text')
    args = parser.parse_args()

    targets = select_targets(args.db, args.count, args.min_priority)

    if args.format == 'json':
        import json
        print(json.dumps(targets, indent=2))
    else:
        print(f"Selected {len(targets)} targets:\n")
        for i, t in enumerate(targets, 1):
            print(f"{i}. {t['demangled'] or t['symbol']}")
            print(f"   Unit: {t['unit']}")
            print(f"   Size: {t['size']}B, Match: {t['current_percent']}%, Priority: {t['priority_score']:.1f}")
            print()

if __name__ == '__main__':
    main()
```

### Locking Mechanism

```sql
-- Lock a function to prevent concurrent work
UPDATE functions SET locked_by = 'agent_001', locked_at = datetime('now')
WHERE symbol = ?;

-- Release lock
UPDATE functions SET locked_by = NULL, locked_at = NULL
WHERE symbol = ?;

-- Find stale locks (> 2 hours)
SELECT symbol, locked_by, locked_at
FROM functions
WHERE locked_by IS NOT NULL
  AND locked_at < datetime('now', '-2 hours');

-- Clear stale locks
UPDATE functions
SET locked_by = NULL, locked_at = NULL
WHERE locked_at < datetime('now', '-2 hours');
```

---

## 2. Model Escalation

### Escalation Strategy

| Attempt | Match % | Model | Rationale |
|---------|---------|-------|-----------|
| 1 | Any | Haiku | Cheap exploration |
| 2 | < 70% | Haiku | Still exploring |
| 2 | >= 70% | Sonnet | Worth investing |
| 3+ | < 90% | Sonnet | Main workhorse |
| 3+ | >= 90% | Opus | Final push on near-matches |
| 5+ | Any | Skip | Give up, need manual review |

### Model Selector

```python
def select_model(func):
    """Select appropriate model based on function state."""
    pct = func['current_percent'] or 0
    attempts = func['attempt_count'] or 0
    verdict = func['verdict']

    # Skip conditions
    if verdict == 'AT_LIMIT':
        return None  # Cannot fix
    if verdict == 'COMPLETE':
        return None  # Already done
    if attempts >= 5:
        return None  # Give up

    # Escalation logic
    if attempts == 0:
        return 'haiku'  # First try: cheap
    elif attempts == 1:
        return 'haiku' if pct < 70 else 'sonnet'
    elif attempts >= 2:
        if pct >= 90:
            return 'opus'  # Near-match: worth opus
        else:
            return 'sonnet'  # Keep trying with sonnet

    return 'sonnet'  # Default
```

### Cost Tracking

```sql
-- Track cost per function
SELECT
    symbol,
    SUM(actual_cost_usd) as total_cost,
    COUNT(*) as attempts,
    MAX(end_percent) as best_match
FROM attempts
GROUP BY symbol
ORDER BY total_cost DESC
LIMIT 20;

-- Cost efficiency: $ per % improvement
SELECT
    symbol,
    SUM(actual_cost_usd) as cost,
    MAX(end_percent) - MIN(start_percent) as improvement,
    CASE
        WHEN MAX(end_percent) > MIN(start_percent)
        THEN SUM(actual_cost_usd) / (MAX(end_percent) - MIN(start_percent))
        ELSE NULL
    END as cost_per_percent
FROM attempts
GROUP BY symbol
HAVING improvement > 0
ORDER BY cost_per_percent ASC
LIMIT 20;
```

---

## 3. Parallel Agent Orchestration

### Orchestrator Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Orchestrator                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ Target       │  │ Model        │  │ Progress     │  │
│  │ Selector     │  │ Escalator    │  │ Tracker      │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
         │                   │                   │
         ▼                   ▼                   ▼
┌─────────────────────────────────────────────────────────┐
│                    Agent Pool                            │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐       │
│  │Agent 1 │  │Agent 2 │  │Agent 3 │  │Agent N │       │
│  │(Haiku) │  │(Sonnet)│  │(Opus)  │  │(...)   │       │
│  └────────┘  └────────┘  └────────┘  └────────┘       │
└─────────────────────────────────────────────────────────┘
         │                   │                   │
         ▼                   ▼                   ▼
┌─────────────────────────────────────────────────────────┐
│                    Database                              │
│  functions | attempts | call_edges | worktrees          │
└─────────────────────────────────────────────────────────┘
```

### Batch Launcher

```python
#!/usr/bin/env python3
"""launch_batch.py - Launch parallel agents on selected targets"""

import subprocess
import sqlite3
import json
from select_targets import select_targets
from model_selection import select_model

def launch_agent(target, model, agent_id):
    """Launch a single decomp agent."""
    # Lock the function
    conn = sqlite3.connect('decomp.db')
    conn.execute(
        "UPDATE functions SET locked_by = ? WHERE symbol = ?",
        (agent_id, target['symbol'])
    )
    conn.commit()
    conn.close()

    # Build prompt
    prompt = f"""Decomp task for: {target['demangled'] or target['symbol']}

Unit: {target['unit']}
Size: {target['size']} bytes
Current match: {target['current_percent']}%

Goal: Achieve 100% match or identify why it cannot be matched.

Use analyze-function to understand the current state, then iteratively
improve the implementation until matched or AT_LIMIT."""

    # Launch (implementation depends on your agent infrastructure)
    # This is a placeholder - adapt to your actual agent launch mechanism
    print(f"Launching {agent_id} with {model} for {target['symbol']}")

    return agent_id

def launch_batch(count=8):
    """Launch a batch of parallel agents."""
    targets = select_targets('decomp.db', count=count)

    agents = []
    for i, target in enumerate(targets):
        model = select_model(target)
        if model is None:
            continue

        agent_id = f"agent_{i:03d}"
        launch_agent(target, model, agent_id)
        agents.append({
            'agent_id': agent_id,
            'target': target,
            'model': model
        })

    print(f"\nLaunched {len(agents)} agents")
    return agents

if __name__ == '__main__':
    launch_batch()
```

### Progress Monitor

```python
#!/usr/bin/env python3
"""monitor.py - Monitor agent progress and trigger rescoring"""

import sqlite3
import time

def check_progress():
    """Check for completed agents and update scores."""
    conn = sqlite3.connect('decomp.db')

    # Find recently completed attempts
    cursor = conn.execute("""
        SELECT f.symbol, f.locked_by, a.end_percent, a.exit_status
        FROM functions f
        JOIN attempts a ON f.symbol = a.symbol
        WHERE f.locked_by IS NOT NULL
          AND a.created_at > datetime('now', '-1 hour')
        ORDER BY a.created_at DESC
    """)

    for symbol, agent, end_pct, status in cursor.fetchall():
        if status in ('success', 'at_limit', 'failed'):
            # Release lock
            conn.execute(
                "UPDATE functions SET locked_by = NULL WHERE symbol = ?",
                (symbol,)
            )
            print(f"Agent {agent} completed {symbol}: {end_pct}% ({status})")

    conn.commit()
    conn.close()

def monitor_loop(interval=60):
    """Continuous monitoring loop."""
    while True:
        check_progress()
        time.sleep(interval)

if __name__ == '__main__':
    monitor_loop()
```

---

## 4. Continuous Rescoring

### Score Refresh Trigger

```python
def should_rescore():
    """Determine if scores need refresh based on recent activity."""
    conn = sqlite3.connect('decomp.db')

    # Count recent completions
    cursor = conn.execute("""
        SELECT COUNT(*) FROM attempts
        WHERE created_at > datetime('now', '-1 hour')
          AND exit_status = 'success'
    """)
    recent_successes = cursor.fetchone()[0]

    conn.close()

    # Rescore if significant progress
    return recent_successes >= 5
```

### Incremental Rescore

```python
def incremental_rescore():
    """Rescore only functions affected by recent changes."""
    conn = sqlite3.connect('decomp.db')

    # Get recently matched functions
    cursor = conn.execute("""
        SELECT symbol FROM attempts
        WHERE exit_status = 'success'
          AND created_at > datetime('now', '-1 hour')
    """)
    recently_matched = [row[0] for row in cursor.fetchall()]

    # Find their callers (neighbors in call graph)
    affected = set(recently_matched)
    for symbol in recently_matched:
        cursor = conn.execute(
            "SELECT caller_symbol FROM call_edges WHERE callee_symbol = ?",
            (symbol,)
        )
        for (caller,) in cursor.fetchall():
            affected.add(caller)

    # Rescore affected functions
    for symbol in affected:
        rescore_function(conn, symbol)

    conn.commit()
    conn.close()

    print(f"Rescored {len(affected)} functions")
```

---

## 5. SCC (Strongly Connected Component) Analysis

For handling mutually recursive function clusters.

### SCC Detection

```python
#!/usr/bin/env python3
"""detect_scc.py - Find strongly connected components in call graph"""

import sqlite3
from collections import defaultdict

def tarjan_scc(graph):
    """Tarjan's algorithm for finding SCCs."""
    index_counter = [0]
    stack = []
    lowlink = {}
    index = {}
    on_stack = {}
    sccs = []

    def strongconnect(v):
        index[v] = index_counter[0]
        lowlink[v] = index_counter[0]
        index_counter[0] += 1
        stack.append(v)
        on_stack[v] = True

        for w in graph.get(v, []):
            if w not in index:
                strongconnect(w)
                lowlink[v] = min(lowlink[v], lowlink[w])
            elif on_stack.get(w, False):
                lowlink[v] = min(lowlink[v], index[w])

        if lowlink[v] == index[v]:
            scc = []
            while True:
                w = stack.pop()
                on_stack[w] = False
                scc.append(w)
                if w == v:
                    break
            sccs.append(scc)

    for v in graph:
        if v not in index:
            strongconnect(v)

    return sccs

def build_call_graph(db_path):
    """Build adjacency list from call_edges table."""
    conn = sqlite3.connect(db_path)
    graph = defaultdict(list)

    cursor = conn.execute("SELECT caller_symbol, callee_symbol FROM call_edges")
    for caller, callee in cursor.fetchall():
        graph[caller].append(callee)

    conn.close()
    return graph

def find_and_store_sccs(db_path):
    """Find SCCs and store in database."""
    conn = sqlite3.connect(db_path)

    # Create SCC table
    conn.execute("""
        CREATE TABLE IF NOT EXISTS function_scc (
            symbol TEXT PRIMARY KEY,
            scc_id INTEGER,
            scc_size INTEGER
        )
    """)
    conn.execute("DELETE FROM function_scc")

    # Find SCCs
    graph = build_call_graph(db_path)
    sccs = tarjan_scc(graph)

    # Store results
    for scc_id, scc in enumerate(sccs):
        for symbol in scc:
            conn.execute(
                "INSERT INTO function_scc VALUES (?, ?, ?)",
                (symbol, scc_id, len(scc))
            )

    conn.commit()

    # Report multi-function SCCs
    cursor = conn.execute("""
        SELECT scc_id, scc_size, GROUP_CONCAT(symbol, ', ')
        FROM function_scc
        WHERE scc_size > 1
        GROUP BY scc_id
        ORDER BY scc_size DESC
        LIMIT 20
    """)

    print("Multi-function SCCs (mutual recursion clusters):")
    for scc_id, size, symbols in cursor.fetchall():
        print(f"  SCC {scc_id}: {size} functions")
        print(f"    {symbols[:100]}...")

    conn.close()

if __name__ == '__main__':
    find_and_store_sccs('decomp.db')
```

### SCC-Aware Prioritization

```sql
-- Prioritize SCCs where partial progress exists
SELECT
    s.scc_id,
    s.scc_size,
    AVG(f.current_percent) as avg_match,
    SUM(CASE WHEN f.current_percent >= 100 THEN 1 ELSE 0 END) as matched_count,
    GROUP_CONCAT(f.symbol, ', ') as symbols
FROM function_scc s
JOIN functions f ON s.symbol = f.symbol
WHERE s.scc_size > 1
GROUP BY s.scc_id
HAVING matched_count > 0 AND matched_count < scc_size
ORDER BY avg_match DESC;
```

---

## Implementation Checklist

- [ ] Implement target selector with locking
- [ ] Add model escalation logic
- [ ] Create batch launcher
- [ ] Set up progress monitor
- [ ] Implement continuous rescoring
- [ ] Add SCC detection and tracking
- [ ] Test full orchestration loop

**Estimated effort**: 2-4 weeks of development and tuning
