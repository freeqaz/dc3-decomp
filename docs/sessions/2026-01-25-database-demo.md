```
╔══════════════════════════════════════════════════════════════════════════════╗
║                    DC3 DECOMP DATABASE INTELLIGENCE                          ║
║                    Smart Function Targeting & Analytics                      ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

## What Is This?

The **decomp.db** database is a smart work queue system that helps you focus on the
*right* functions at the *right* time. Instead of randomly picking functions to
decompile, it uses AI-powered scoring to prioritize high-value targets.

**Key Insight:** Not all functions are equal. A 90% match constructor called by 200
other functions has higher ROI than an isolated 50% utility function.

---

## Quick Stats

```
╭─────────────────────────────────────────╮
│  Total Functions:     47,213           │
│  Complete (100%):     22,306 (47.3%)   │
│  Average Progress:    98.2%            │
│                                         │
│  Reachable 100%:      23,311 (99.8%)   │
│  At Limit:            ~800 (LTCG)      │
╰─────────────────────────────────────────╯
```

---

## Smart Views: Pre-Built Work Queues

### 1. **v_reachable_100** - The Low-Hanging Fruit

Functions that are 1-2 fixes away from completion (99%+ match).

```sql
SELECT symbol, demangled, current_percent, priority_score
FROM v_reachable_100
LIMIT 10;
```

**Output:**
```
RhythmBattlePlayer()          | 99.69% | Score: 102.0
GetSystemLocale()             | 99.98% | Score: 94.5
DataNode::Print()             | 99.95% | Score: 87.8
InterpVector()                | 99.31% | Score: 87.8
CharCollide()                 | 99.62% | Score: 79.8
Character::PreLoad()          | 99.79% | Score: 78.0
```

**Why This Matters:** These are 99%+ complete and PROVEN fixable (reachable_100=1).
One small tweak → instant completion.

---

### 2. **v_near_complete_units** - Finish What You Started

Entire source files that are 95%+ done.

```sql
SELECT unit, total_functions, complete_functions, completion_percent
FROM v_near_complete_units
LIMIT 8;
```

**Output:**
```
┌──────────────────────────────────┬───────┬──────────┬──────────┐
│ Unit                             │ Total │ Complete │ Progress │
├──────────────────────────────────┼───────┼──────────┼──────────┤
│ lazer/meta_ham/HamSongMetadata   │   49  │    48    │  98.0%   │
│ system/hamobj/Ham                │  175  │   171    │  97.7%   │
│ system/gesture/NavSkeletonDir    │   34  │    33    │  97.1%   │
│ lazer/meta_ham/Campaign          │   64  │    62    │  96.9%   │
│ system/os/ContentMgr             │   31  │    30    │  96.8%   │
│ system/char/CharBoneOffset       │   30  │    29    │  96.7%   │
│ system/utl/Symbol                │   29  │    28    │  96.6%   │
│ system/char/Char                 │  237  │   229    │  96.6%   │
└──────────────────────────────────┴───────┴──────────┴──────────┘
```

**Why This Matters:** Completing entire units = cleaner diffs, fewer merge conflicts,
psychological wins. Finish what's 97% done before starting new files.

---

### 3. **v_high_impact** - Maximum Impact Functions

Functions with high "fan-in" (called by many others) that will unlock downstream work.

```sql
SELECT symbol, fan_in, current_percent, impact_score
FROM v_high_impact
WHERE current_percent < 100
LIMIT 8;
```

**Why This Matters:** Fixing one high-impact function can unblock dozens of others.
Think dependency tree optimization.

---

### 4. **v_pattern_summary** - Know Your Blockers

Aggregates functions by unfixable patterns (LTCG, bool masks, etc.)

```sql
SELECT * FROM v_pattern_summary;
```

**Output:**
```
┌──────────────────┬───────────┬─────────────────┐
│ Pattern          │ Affected  │ Avg Match %     │
├──────────────────┼───────────┼─────────────────┤
│ BOOL_MASK        │    15     │     92.2%       │
│ CAN_REACH_100    │ 23,311    │     99.8%       │
│ LINKER_MERGED    │   802     │     94.5%       │
└──────────────────┴───────────┴─────────────────┘
```

**Why This Matters:** Don't waste time on LINKER_MERGED functions stuck at 94%.
Focus effort where it matters.

---

## Priority Scoring: How It Works

Each function gets scored across 3 dimensions:

```
Priority Score = (Ease × 0.4) + (Impact × 0.4) + (Confidence × 0.2)
```

### **Ease Score** (How hard to fix?)
- Higher current match % → easier
- Smaller function size → easier
- Common patterns (constructor, destructor) → easier

### **Impact Score** (How valuable is completion?)
- High fan-in (called by many) → more valuable
- Part of near-complete unit → more valuable
- Critical class hierarchy → more valuable

### **Confidence Score** (Will this work?)
- No unfixable patterns → higher confidence
- Previous successful attempts → higher confidence
- Similar functions fixed → higher confidence

**Example:**
```
RhythmBattlePlayer::RhythmBattlePlayer() - Score 102.0
├─ Ease:       99.7% match, 47 instructions  → 95.0
├─ Impact:     Constructor, 14 callers       → 85.0
└─ Confidence: No blockers, CAN_REACH_100    → 100.0
```

---

## Practical Workflows

### **Workflow 1: Cherry-Pick Quick Wins**

```bash
# Get top 20 high-priority near-complete functions
sqlite3 decomp.db "
  SELECT demangled, current_percent, priority_score
  FROM v_reachable_100
  WHERE priority_score > 80
  LIMIT 20
"

# Feed to orchestrator
./bin/orchestrate batch "matching/pattern/*" --limit 20
```

**Expected Outcome:** 15-18 functions reach 100% in ~5 minutes.

---

### **Workflow 2: Complete Entire Units**

```bash
# Find units 1-2 functions away from completion
sqlite3 decomp.db "
  SELECT unit, total_functions, complete_functions
  FROM v_near_complete_units
  WHERE total_functions - complete_functions <= 2
"

# Target those units specifically
./bin/orchestrate batch "system/char/CharBoneOffset.cpp"
```

**Expected Outcome:** Entire files turn green in progress reports.

---

### **Workflow 3: Avoid Known Blockers**

```bash
# Filter out LINKER_MERGED functions (unfixable)
sqlite3 decomp.db "
  SELECT symbol, current_percent
  FROM v_priority_queue
  WHERE primary_pattern IS NULL
     OR primary_pattern NOT LIKE '%LINKER_MERGED%'
  LIMIT 50
"
```

**Expected Outcome:** Don't waste time on functions at their toolchain limit.

---

### **Workflow 4: Focus High-Impact Work**

```bash
# Get functions with fan-in > 20 (unlock downstream work)
sqlite3 decomp.db "
  SELECT demangled, fan_in, current_percent
  FROM v_high_impact
  WHERE fan_in > 20 AND current_percent < 100
"
```

**Expected Outcome:** Fix blockers that unblock entire subsystems.

---

## Advanced Queries

### Find All Constructors Near Completion
```sql
SELECT demangled, current_percent
FROM v_reachable_100
WHERE is_constructor = 1
  AND current_percent > 98
ORDER BY priority_score DESC;
```

### Show Unit Completion Heatmap
```sql
SELECT
  SUBSTR(unit, 1, 20) as category,
  COUNT(*) as total,
  SUM(CASE WHEN current_percent = 100 THEN 1 ELSE 0 END) as done,
  ROUND(AVG(current_percent), 1) as avg
FROM functions
GROUP BY category
HAVING total > 50
ORDER BY avg DESC;
```

### Track Weekly Progress
```sql
SELECT
  DATE(timestamp) as day,
  COUNT(*) as functions_completed
FROM attempts
WHERE percent_after = 100 AND percent_before < 100
GROUP BY day
ORDER BY day DESC
LIMIT 7;
```

---

## How This Helps Decompilation

### ✅ **Before Database:**
- Randomly pick functions from objdiff
- No idea if a 99% function is fixable or at limit
- Work on isolated functions with low impact
- Duplicate effort (multiple agents try same unfixable functions)

### ✨ **After Database:**
- **Smart Targeting:** Focus on high-ROI functions first
- **Pattern Awareness:** Avoid known blockers (LTCG, struct offsets)
- **Impact Optimization:** Prioritize functions that unlock others
- **Unit Completion:** Finish files systematically
- **Progress Tracking:** See what works, iterate on strategy

---

## Integration with Orchestrator

The orchestrator MCP server uses decomp.db for:

```
┌─────────────────────────────────────────────────────────┐
│  MCP Tool              │  Database View Used            │
├────────────────────────┼────────────────────────────────┤
│  query_functions       │  v_priority_queue              │
│  get_attempts          │  attempts (history tracking)   │
│  report_result         │  functions + attempts (writes) │
│  run_objdiff           │  functions (percent updates)   │
│  lookup_rb3            │  file_pairs (RB3 references)   │
└─────────────────────────────────────────────────────────┘
```

**Agents** query the database → **Database** returns prioritized work →
**Agents** fix functions → **Results** feed back into database → **Loop continues**

---

## Next Steps

1. **Try the demo queries** above to see the data
2. **Run `./bin/orchestrate query`** to see orchestrator integration
3. **Pick a near-complete unit** from v_near_complete_units
4. **Batch process** with `./bin/orchestrate batch`
5. **Watch progress** as functions systematically reach 100%

---

```
╔══════════════════════════════════════════════════════════════════════════════╗
║  "Work smarter, not harder. Let the database find the best targets for you." ║
╚══════════════════════════════════════════════════════════════════════════════╝
```

**Database Schema:** See `scripts/orchestrator/database.py` for full schema
**View Definitions:** Check `scripts/orchestrator/views.sql` (if exists)
**Query Examples:** `docs/tools/orchestrator/QUICK_START.md`
