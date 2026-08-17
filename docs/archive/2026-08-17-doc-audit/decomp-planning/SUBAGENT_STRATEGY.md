# Subagent Strategy for Decompilation

This document describes how to effectively use parallel AI agents (subagents) to accelerate decompilation work.

---

## Overview

Running multiple subagents in parallel proved highly effective for DC3 decompilation. Example results:

**Session 2026-01-25 (8 agents, <30% functions):**
- 3 functions to 90%+ (CheatsInit 96%, RndLight::Load 95%, HolmesXboxPath 91.6%)
- 3 functions to 60-90% (RndEnviron::Save 80.5%, ClipDistMap::FindNodes 79.3%, NewShortcutNode 63.5%)
- 2 functions to 40-60% (CharMirror::Poll 49.3%, OnGetGennedBitmapPath 48.4%)
- **Total: +400% improvement across 8 functions**

**Session 2026-01-22 (15 agents, mixed targets):**
- 6 functions fixed to 100%
- 13 functions improved
- 10 functions verified as already correct

---

## Critical Safety Rules

When running multiple agents concurrently:

1. **Include safety warnings in every prompt:**
   ```
   ## CRITICAL WARNINGS
   - DO NOT run `git reset --hard` or `git checkout .`
   - DO NOT delete or corrupt files
   - Only edit the specific file(s) for your assigned function
   - If corrupted state, STOP and report
   ```

2. **Assign non-overlapping files** - Don't have two agents edit the same .cpp file
3. **Use specific object builds** - `ninja build/373307D9/src/path/File.obj` not full `ninja`
4. **Monitor for conflicts** - Check diagnostics between agent completions

---

## Agent Types and When to Use Them

### Sonnet (Primary Choice)
Best for most decomp tasks:
- Implementing stub functions (<30% match)
- Fixing near-matching functions (90-99%)
- Load/Save/Init/Poll function patterns
- Any task requiring code changes

**Cost-effective and capable enough for most decomp work.**

### Haiku (Quick Tasks)
Best for fast, simple work:
- Verifying already-implemented functions
- Research tasks (finding opportunities)
- Trivial 4-16 byte wrapper functions
- Gathering context from multiple files

### Opus (Complex Analysis)
Reserve for truly difficult problems:
- Analyzing 99%+ functions with subtle issues
- Understanding complex compiler codegen quirks
- Debugging persistent mismatches after multiple attempts

---

## Task Categories

### Category 1: Stub Implementation (<30% match) - Sonnet
Functions that are mostly unimplemented stubs.

**These are often the best targets** - high improvement potential with clear missing logic.

**Signs of a good stub target:**
- Comments like "// finish later" or "// TODO"
- Just returns 0 or empty value
- Missing function calls that should be there
- Size mismatch shows hundreds of missing bytes

**Query command:**
```bash
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 0 --max-percent 30 --min-size 50 \
  --sort-by match_percent --sort-order desc --limit 50
```

**Prompt template:**
```
You are working on DC3 decompilation. Your target:
**Function:** [function]
**Current Match:** [X]%
**Size:** [Y] bytes
**File:** [path]

## CRITICAL WARNINGS
- DO NOT run `git reset --hard` or `git checkout .`
- Only edit the specific file for your function

## Workflow
1. Read current implementation and header
2. Run: ./bin/objdiff-cli diff -p . "[function]" --verdict -f markdown
3. Check RB3 reference: ~/code/milohax/rb3/src/[similar_path]
4. Implement missing logic
5. Build: ninja build/373307D9/src/[path].obj
6. Iterate with: ./bin/objdiff-cli diff -p . "[function]" --build --verdict

Report: starting %, ending %, changes made, blockers.
```

### Category 2: Near-Match Fixes (90-99%) - Sonnet
Functions close to matching that need tweaks.

**Query command:**
```bash
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-percent 99 --limit 30
```

**Prompt template:**
```
Fix [function] currently at [X]% match.
File: [path]

Known patterns that help:
- while→for loop conversions
- Variable declaration order affects register allocation
- Ternary operators vs if-else
- 0 vs 0.0f/false in initializers
- Condition inversions (if !x vs if x)

Use: ./bin/objdiff-cli diff -p . "[function]" --build --verdict -f markdown

Stop when: 100% match, AT_LIMIT verdict, or LINKER_MERGED detected.
```

### Category 3: Function Type Targeting - Sonnet
Target specific function patterns that have predictable structures.

**Load Functions:**
```bash
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 0 --max-percent 50 | grep -i "::Load"
```

**Save Functions:**
```bash
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 0 --max-percent 50 | grep -i "::Save"
```

**Init Functions:**
```bash
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 0 --max-percent 50 | grep -i "Init"
```

### Category 4: Research (Haiku)
Finding opportunities without modifying files.

**Prompt template:**
```
Research [subsystem] status in DC3.
- Find functions <50% that look implementable
- Check RB3 reference for similar code
- Identify patterns and dependencies

Output: List of top 5-10 targets with rationale.
This is research only - do not modify files.
```

### Category 5: Deep Analysis (Opus)
For stubborn 99%+ functions.

**Prompt template:**
```
Analyze [function] at [X]% to find remaining differences.
File: [path]
Previous attempts: [what was tried]

Use: ./bin/objdiff-cli diff -p . "[function]" -f markdown --include-instructions

Compare instruction-by-instruction. Propose specific theories to test.
```

---

## Complexity Guide

| Match % | Typical Issue | Agent | Expected Outcome |
|---------|---------------|-------|------------------|
| 0-30% | Stub/missing logic | Sonnet | +30-70% improvement |
| 30-60% | Partial implementation | Sonnet | +20-40% improvement |
| 60-90% | Missing details | Sonnet | +10-30% improvement |
| 90-95% | Control flow/registers | Sonnet | +5-10% or AT_LIMIT |
| 95-99% | Subtle differences | Sonnet | Fix or AT_LIMIT |
| 99%+ | Byte-level issues | Opus | Fix or AT_LIMIT |

---

## Parallelization Strategy

### Step 1: Query for Targets
```bash
# <30% stubs (best ROI)
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 0 --max-percent 30 --min-size 50 --limit 20

# Near-matches (quick wins)
./bin/objdiff-cli report query build/373307D9/report.json --functions \
  --min-percent 90 --max-percent 99 --limit 20

# Specific function types
./bin/objdiff-cli report query ... | grep -i "::Load\|::Save\|Init"
```

### Step 1b: Triage with Unicorn (Optional but Recommended)

Before assigning near-match (90-99%) functions to agents, run unicorn batch diagnosis to filter out false positives:

```bash
# Batch triage a unit — shows SKIP/FIX per function
python3 -m scripts.unicorn_runner.diagnose --unit system/meta/Profile --batch
```

Functions marked **SKIP** (unicorn=EQUIVALENT) have cosmetic diffs only — don't waste agent time on them. Focus agents on **FIX** functions where behavior actually diverges.

Real-world results: UITransitionHandler had 9 functions flagged by objdiff → all 9 were EQUIVALENT in unicorn. Profile had 7 flagged → 6 were equivalent. This filtering saves significant agent compute.

See [../plans/unicorn-runner-value.md](../plans/unicorn-runner-value.md) for detailed examples.

### Step 2: Select Non-Overlapping Targets
- **Don't assign two agents to the same .cpp file**
- Prefer functions in different subsystems
- Mix difficulty levels for balanced results

### Step 3: Launch 6-10 Agents in Parallel
```
Launch agents:
1. [Sonnet] RndLight::Load (28% → Load function pattern)
2. [Sonnet] CheatsInit (25% → Init function pattern)
3. [Sonnet] RndEnviron::Save (22% → Save function pattern)
4. [Sonnet] ClipDistMap::FindNodes (25% → Algorithm stub)
5. [Sonnet] CharMirror::Poll (23% → Poll function pattern)
6. [Sonnet] SongSort::NewShortcutNode (26% → Factory stub)
7. [Haiku] Research system/char opportunities
8. [Haiku] Verify small wrapper functions
```

### Step 4: Monitor and Collect
- Agents complete in ~10-20 minutes each
- Check for diagnostic errors between completions
- Fix any cleanup issues (ambiguous casts, missing includes)
- Document results in session file

---

## Best Practices

### DO:
- **Target <30% stubs** - Highest improvement potential
- **Include safety warnings** - Prevent git reset disasters
- **Use --build flag** - Faster iteration: `objdiff-cli diff ... --build --verdict`
- **Check RB3 reference** - Shared Milo engine code helps
- **Request specific reports** - Starting %, ending %, changes, blockers
- **Mix function types** - Load, Save, Init, Poll have predictable patterns

### DON'T:
- **Don't assign same file to multiple agents** - Creates conflicts
- **Don't use Opus for stubs** - Sonnet is sufficient and cheaper
- **Don't skip the verdict** - AT_LIMIT means stop trying
- **Don't ignore LINKER_MERGED** - These are unfixable
- **Don't run full ninja builds** - Use specific .obj builds

---

## Unfixable Patterns (When to Stop)

Agents should stop and report when encountering:

1. **LINKER_MERGED calls** - Compiler merged identical functions
2. **Consistent register swaps** - Compiler chose different registers throughout
3. **AT_LIMIT verdict** - objdiff-cli determined function is at practical limit
4. **Unicorn EQUIVALENT** - `diagnose.py` confirms behavioral equivalence despite instruction diffs

**BOOL_MASK patterns are often fixable** — try local `bool` variable, `(bool)` cast, or look for a missing inline before giving up. See [fixable-bool-mask.md](patterns/fixable-bool-mask.md).

---

## Patterns Discovered via Subagents

Codegen patterns identified through parallel investigation:

1. **Initializer list literals**: Use `0` not `0.0f`/`false`
2. **Boolean index expressions**: `1 - side` not `side == 0`
3. **Ternary operators**: Preferred for simple conditionals
4. **Variable declaration order**: Affects register allocation
5. **Assignment in function calls**: `func(x = getValue())`
6. **Static Symbol order**: Must match original exactly
7. **Load version checks**: Use `gRev > X` not `gRev >= X+1`
8. **Pointer arithmetic loops**: Often faster than index-based iteration

---

## Session Documentation

After each parallel session, create `docs/sessions/YYYY-MM-DD-description.md`:

```markdown
# Session: [Description]
**Date:** YYYY-MM-DD
**Focus:** [What was targeted]

## Agent Results
| Function | Before | After | Change | Notes |
|----------|--------|-------|--------|-------|
| ... | ...% | ...% | +...% | ... |

## Files Modified
- list of files

## Patterns Learned
- any new discoveries

## Next Steps
- follow-up work
```

---

## See Also

- [SUBAGENT_BASE_PROMPT.md](../sessions/SUBAGENT_BASE_PROMPT.md) - Prompt template for agents
- [TECHNICAL_NOTES.md](TECHNICAL_NOTES.md) - Compiler patterns
- [../tools/WORKFLOW.md](../tools/WORKFLOW.md) - Tool usage guide
