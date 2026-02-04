# Model Escalation System

> Part of the [objdiff CLI Design](./OBJDIFF_CLI_DESIGN.md) documentation.

This document describes the model escalation system for the DC3 wrapper tool. This is **project-specific** and separate from objdiff itself.

---

## Concept: Incremental Retry with Escalation

When a model fails to fix a function, record the attempt and escalate to a higher-tier model:

```
Attempt 1: Haiku tries, fails     → Record, escalate to Sonnet
Attempt 2: Sonnet tries, fails    → Record, escalate to Opus
Attempt 3: Opus tries, fails      → Mark as "stuck", needs human review
```

---

## Escalation Tiers

| Tier | Model | Criteria |
|------|-------|----------|
| 0 | Haiku | Default for small (<100b) or high match (>90%) |
| 1 | Sonnet | Failed Haiku attempt, OR medium complexity |
| 2 | Opus | Failed Sonnet attempt, OR requires deep analysis |
| 3 | Human | Failed Opus attempt, needs manual investigation |

---

## State Tracking Schema

Store in `build/agent_state.json` (or SQLite for better concurrency):

```json
{
  "functions": {
    "Game::Poll()": {
      "current_tier": 1,
      "attempts": [
        {
          "model": "haiku",
          "timestamp": "2026-01-22T10:30:00Z",
          "before_percent": 95.0,
          "after_percent": 95.0,
          "result": "no_improvement",
          "notes": "Tried reordering variables, no change"
        }
      ],
      "best_percent": 95.0,
      "status": "in_progress"
    },
    "Shuttle::SetActive()": {
      "current_tier": 0,
      "attempts": [
        {
          "model": "haiku",
          "timestamp": "2026-01-22T09:15:00Z",
          "before_percent": 85.0,
          "after_percent": 100.0,
          "result": "success"
        }
      ],
      "best_percent": 100.0,
      "status": "completed"
    }
  },
  "version": 1
}
```

---

## Attempt Result Types

| Result | Meaning | Action |
|--------|---------|--------|
| `success` | Reached 100% | Mark completed |
| `improved` | Better %, not 100% | Keep at current tier, allow retry |
| `no_improvement` | Same or worse % | Escalate tier |
| `error` | Build failed, etc. | Don't escalate, retry same tier |
| `stuck` | Opus failed | Mark for human review |

---

## DC3 Wrapper Commands

```bash
# Get next function for a specific model tier
dc3 next --model haiku
dc3 next --model sonnet
dc3 next --model opus

# Record an attempt result
dc3 attempt "Game::Poll" --model sonnet --result no_improvement \
  --before 95.0 --after 95.0 --notes "Tried ternary conversion"

# Manually escalate a function
dc3 escalate "Game::Poll" --to opus --reason "Complex control flow"

# Query functions by escalation state
dc3 query --tier 1 --status in_progress   # Sonnet-tier work
dc3 query --stuck                          # Needs human review

# Show attempt history for a function
dc3 history "Game::Poll"
```

---

## Tier Assignment Algorithm

```python
def get_tier(func_name, state, report):
    """Determine which model tier should work on a function."""

    func_state = state.get(func_name, {})
    func_report = report.get_function(func_name)

    # Already completed
    if func_report.match_percent == 100:
        return None  # No work needed

    # Check previous attempts
    attempts = func_state.get("attempts", [])
    failed_models = {a["model"] for a in attempts if a["result"] == "no_improvement"}

    # Escalate based on failures
    if "opus" in failed_models:
        return 3  # Human review
    if "sonnet" in failed_models:
        return 2  # Opus
    if "haiku" in failed_models:
        return 1  # Sonnet

    # Initial tier based on function characteristics
    size = func_report.size
    match_pct = func_report.match_percent

    if match_pct is None:  # Unimplemented
        if size < 50:
            return 0  # Haiku - trivial
        elif size < 200:
            return 1  # Sonnet - medium
        else:
            return 1  # Sonnet - complex but try first
    else:  # Partially matched
        if match_pct >= 99:
            return 2  # Opus - needs deep analysis for last 1%
        elif match_pct >= 90:
            return 0  # Haiku - likely small fix
        else:
            return 1  # Sonnet - significant work

    return 0  # Default to Haiku
```

---

## Integration with objdiff CLI

The DC3 wrapper combines objdiff queries with state tracking:

```bash
# Internally does:
# 1. objdiff report query ... --unimplemented --max-size 50
# 2. Filter out functions with failed haiku attempts
# 3. Return first available function

dc3 next --model haiku
# Output: {"function": "Foo::Bar", "unit": "src/foo.cpp", "size": 32, "tier": 0}
```

---

## Workflow Example

```bash
# Agent startup - get work for Haiku
WORK=$(dc3 next --model haiku --format json)
FUNC=$(echo $WORK | jq -r '.function')

# ... agent works on function ...

# Build and check result
ninja build/373307D9/src/path/file.obj
ninja build/373307D9/report.json
NEW_PCT=$(objdiff report function report.json "$FUNC" | jq '.matches[0].fuzzy_match_percent')

# Record attempt
if [ "$NEW_PCT" = "100" ]; then
  dc3 attempt "$FUNC" --model haiku --result success --after 100
else
  dc3 attempt "$FUNC" --model haiku --result no_improvement \
    --before $OLD_PCT --after $NEW_PCT
  # Function automatically escalated to Sonnet tier
fi
```

---

## Benefits

1. **No wasted effort** - Don't keep throwing Haiku at hard problems
2. **Incremental progress** - Each tier builds on previous attempts
3. **Cost efficiency** - Use expensive models only when needed
4. **Trackable** - Full history of what was tried
5. **Human escalation** - Clear signal when AI can't solve it
