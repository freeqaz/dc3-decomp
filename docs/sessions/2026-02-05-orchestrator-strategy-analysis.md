# Orchestrator Strategy Analysis

**Date**: 2026-02-05

## Analysis Summary

We analyzed 19,306 historical attempts and found data quality issues. Then re-analyzed the **last 72 hours (2,996 attempts)** which has clean data (no gitkeep bug).

## Critical Discovery: Data Quality Issues (Historical)

Historical analysis revealed **massive data quality problems**:

### 1. Patch Data Collection Had Bugs (Now Fixed)

| Patch Type | Attempts | Completions | Status |
|------------|----------|-------------|--------|
| gitkeep_deletion | 11,792 | 1,537 | Bug (Jan 26-31) |
| no_patch | 6,917 | 2,668 | Real work, patch not captured |
| **real_code_change** | **597** | **57** | Correctly captured (Feb 2+) |

**Timeline shows the bug was fixed Feb 2:**
- Jan 27-31: Gitkeep patches captured, real patches lost
- Feb 2+: Zero gitkeep, real patches correctly captured

**The 2,668 no-patch completions are likely real work** - notes show substantial implementations like "Implemented full Poll() function matching RB3 reference" with actual code changes.

**Corrected real completion count: ~2,725** (2,668 no-patch + 57 real_code)

### 2. Model Data is Incomplete
- 4,633 attempts (24%) have `model = 'unknown'`
- Model escalation analysis is unreliable due to confounding

### 3. Missing Fields
- 36% of attempts missing patch data
- 45% missing notes

## Corrected Empirical Findings (Final)

### Real Success Rates by Starting % (with no-patch = real work)
| Starting % | Attempts | Real Completions | Success Rate |
|------------|----------|------------------|--------------|
| **0%** | 11,357 | 2,293 | **20.19%** (BEST!) |
| 95%+ | 3,399 | 232 | 6.83% |
| 80-94% | 1,670 | 113 | 6.77% |
| 50-79% | 912 | 40 | 4.39% |
| 1-49% | 1,968 | 47 | 2.39% (worst) |

**Key Insight**: 0% starts have the HIGHEST success rate! Many 0% functions are fresh implementations that succeed on first try.

### Best Units (Final Corrected)
| Unit | Attempts | Success Rate |
|------|----------|--------------|
| **Crowd** | 127 | **73.23%** |
| Cache_Xbox | 44 | 68.18% |
| MemHeap | 40 | 62.5% |
| BinkMovieImpl | 66 | 54.55% |
| Synapse_dsp | 55 | 52.73% |

**Insight**: synth_xbox and world/ units have very high success rates.

---

## RECENT DATA (Last 72 Hours) - Clean Data, Better Signal

### Starting % Success (Recent)
| Starting % | Attempts | Completions | Success % |
|------------|----------|-------------|-----------|
| **0%** | 769 | 685 | **89.08%** |
| 1-49% | 14 | 4 | 28.57% |
| 50-79% | 23 | 4 | 17.39% |
| 95%+ | 1,585 | 88 | 5.55% |
| 80-94% | 605 | 24 | 3.97% |

**Confirmed: 0% starts are best (89% success)** - strategy is targeting fresh, easy functions.

### Attempt Number (Recent vs Historical)
| Attempt # | Recent (72h) | Historical |
|-----------|--------------|------------|
| **1st** | **75.05%** | 0.03% |
| 2-4 | 59.56% | 8.37% |
| 5-11 | 5.85% | 1.0% |
| 12+ | 2.58% | 0.17% |

**First attempts now succeed 75%!** Current strategy effectively targets easy wins.

### Trajectory (Recent)
| Trajectory | Success % |
|------------|-----------|
| Big gain 20%+ | **87.59%** |
| No change | 16.88% |
| Regression | 3.11% |

Regression is now **worst** (not best like historical). Pattern reversed.

### Unit Success (Recent - 100% rates!)
| Unit | Success Rate |
|------|--------------|
| PlatformMgr_Xbox | 100% (6/6) |
| AmbientOcclusion | 100% (7/7) |
| BinkIntegration | 100% (21/21) |
| Cache_Xbox | 100% (22/22) |
| Crowd | 95.29% (81/85) |

### Model Effectiveness (Recent)
| Model | Success Rate |
|-------|--------------|
| haiku | 36.9% |
| opus | 26.78% |
| sonnet | 10.91% |

**Haiku is best for current easy targets.** Opus good for hard functions.

---

## HISTORICAL DATA (for reference)

### Attempt Number is Critical
| Attempt # | Real Success Rate |
|-----------|-------------------|
| 1st | 0.03% |
| 2nd-4th | **8.37%** |
| 5th-11th | 1.0% |
| 12+ | 0.17% |

Functions that succeed on attempt 2-4 are "learnable" - they needed iteration but yielded.

### Regression Predicts Success (Counterintuitive!)
| Previous Attempt Result | Next Success Rate |
|-------------------------|-------------------|
| Regression (% went down) | **24.77%** |
| No change | 6.22% |
| Big gain (20%+) | 1.37% |

If an attempt caused a regression, the function is MORE likely to eventually succeed.

### Note Keywords as Predictors
| Keyword | Success Ratio |
|---------|---------------|
| "RB3" mentioned | **3.5x** more likely to succeed |
| "struct"/"offset" | **2.2x** more likely |
| "merged"/"LINKER" | 0.45x (negative) |
| "impossible"/"unfixable" | 0.40x (negative) |

### Untried Opportunities
| Bucket | Untried | With Merged | Notes |
|--------|---------|-------------|-------|
| 50-79% | 197 | 2 (1%) | **Prime targets** |
| 80-94% | 370 | 189 (51%) | Half are stuck |
| 95%+ | 27,396 | 1,089 (4%) | Bulk of work |

---

## Implementation Plan

### Phase 1: Fix Data Collection (PRIORITY)

**File**: `scripts/orchestrator/database.py`

1. **Fix `is_real_completion()` to detect gitkeep patches**:
```python
def is_real_completion(patch: str | None) -> bool:
    """Determine if patch represents real code change."""
    if not patch:
        return False
    if 'gitkeep' in patch:
        return False  # These are fake completions
    if 'diff --git' not in patch:
        return False  # Not a valid diff
    return len(patch) > 200  # Real code changes are larger
```

2. **Add `patch_type` column to attempts table**:
```sql
ALTER TABLE attempts ADD COLUMN patch_type TEXT;
-- Values: 'real_code', 'gitkeep', 'no_patch', 'unknown'
```

3. **Backfill patch_type for existing attempts**

4. **Ensure model is always captured** - investigate why 24% are 'unknown'

### Phase 2: Adaptive Strategy (Based on Recent Data)

**Priority Calculation** (based on RECENT 72h data - clean signal):
```python
score = 0

# 1. Starting % (0% is BEST at 89%!)
if current_percent == 0:
    score += 40  # 89% success rate!
elif current_percent < 50:
    score += 15  # 28.57% success
elif current_percent < 80:
    score += 10  # 17.39% success
elif current_percent < 95:
    score += 5   # 3.97% success
else:
    score += 8   # 5.55% success (95%+)

# 2. Attempt count (first attempts are best at 75%!)
if attempt_count == 0:
    score += 35  # First attempts: 75% success
elif attempt_count <= 3:
    score += 25  # 2-4 attempts: 60% success
elif attempt_count <= 10:
    score += 5   # 5-11: 6% success
else:
    score -= 20  # 12+: 2.6% - diminishing returns

# 3. Unit success rate (many units at 100%!)
score += unit_success_rate * 40  # Scaled 0-40 points

# 4. Note keywords from previous attempts
if previous_notes_mention('RB3'):
    score += 10  # Positive predictor
if previous_notes_mention('merged', 'LINKER', 'unfixable', 'impossible'):
    score -= 25  # Negative predictor

# 5. Merged symbol exclusion
if has_linker_merged:
    score -= 50  # Strong negative predictor

# 6. Function type
if is_destructor:
    score += 10  # 81.5% avg improvement
if is_constructor:
    score += 8   # 69.3% avg improvement
```

**Key strategy change**: Prioritize **fresh 0% functions** (89% success, 75% on first attempt) over retries.

### Phase 3: Batch Strategies

**Strategy: `fresh` (recommended default)**
- Target 0% functions first (89% success rate)
- Use haiku model (36.9% success, cheapest)
- Exclude merged symbols

**Strategy: `adaptive`**
- Mix fresh 0% with 2-4 attempt retries
- Balance exploration and exploitation

**Strategy: `batch-95`**
- Group similar 95%+ functions by pattern
- Use opus for final push
- Skip functions with merged symbols

---

## Files to Modify

1. `scripts/orchestrator/database.py`
   - Add `patch_type` column and backfill
   - Add `is_real_completion()` with gitkeep detection
   - Add regression tracking columns
   - Fix model capture to eliminate 'unknown'

2. `scripts/orchestrator/core.py`
   - Revised `get_next_function_adaptive()`
   - Add `--strategy adaptive|batch-95|retry-only`

3. `scripts/orchestrator/note_parser.py` (new file)
   - Extract keywords from previous attempt notes
   - Detect RB3 mentions, struct/offset patterns, negative indicators

---

## Verification

1. **Strategy Comparison**:
```bash
./bin/orchestrate targets --strategy fresh --limit 20
# Should show: 0% functions, no merged symbols
```

2. **Unit Success Rate Check**:
```bash
sqlite3 decomp.db "
SELECT unit, COUNT(*),
       ROUND(100.0*SUM(CASE WHEN exit_status='complete' THEN 1 ELSE 0 END)/COUNT(*),1)
FROM attempts a JOIN functions f ON a.function_id=f.id
WHERE started_at >= datetime('now','-72 hours')
GROUP BY unit HAVING COUNT(*)>=5 ORDER BY 3 DESC LIMIT 10"
```

3. **Fresh vs Retry Comparison**:
```bash
./bin/orchestrate batch --strategy fresh --limit 10 --dry-run
./bin/orchestrate batch --strategy adaptive --limit 10 --dry-run
```

---

## Key Corrections from Original Plan

| Original Claim | Final Corrected Finding |
|----------------|-------------------------|
| 300-byte threshold for "real work" | **Wrong** - gitkeep bug in Jan, no-patch = real work |
| CharEyes 42% success | **Wrong** - Crowd (73%), Cache_Xbox (68%) are top units |
| 1-79% starts are best | **Wrong** - **0% starts are BEST (20.19%!)** |
| Model escalation works | **Unknown** - 24% 'unknown' model makes analysis unreliable |
| Patch size = quality | **Wrong** - check for gitkeep content, not size |
| Higher % = harder | **Wrong** - 0% functions often fresh implementations that succeed |

---

## Summary of Validated Findings (from Recent 72h Data)

| Finding | Recent Data | Action |
|---------|-------------|--------|
| **0% starts** | 89% success | **Prioritize fresh functions** |
| **First attempts** | 75% success | Don't wait for retries |
| **2-4 attempts** | 60% success | Still good |
| **12+ attempts** | 2.6% success | Deprioritize heavily |
| **Big gains (20%+)** | 87.6% success | Target functions where progress is possible |
| **Regression** | 3.1% success | **Not** a positive signal (contradicts historical) |
| **Destructors** | 81.5% avg improvement | Prioritize |
| **Haiku** | 36.9% success | Best for easy targets |
| **Opus** | 26.78% success | Use for hard functions |
| **Sonnet** | 10.91% success | Worst performer |

---

## Strategic Recommendations

1. **Target fresh 0% functions first** - 89% success rate, far better than retries
2. **Use haiku for 0% functions** - Cheap and effective (36.9%)
3. **Escalate to opus only for hard functions** (80%+ that need final push)
4. **Avoid 12+ attempt functions** - Only 2.6% success, waste of resources
5. **Track unit success rates** - Some units at 100%, others stuck
6. **Deprioritize sonnet** - Lowest performer at 10.91%

---

## Next Steps (Implementation)

1. **Add `unit_success_rate` to functions table** - Cache from recent data
2. **Implement revised priority calculation** - Based on recent findings
3. **Fix model selection** - Use haiku default, opus for hard functions only
4. **Add `--prefer-fresh` flag** - Explicitly target 0% functions
5. **Monitor recent success rate** - Track last 72h for adaptive learning
