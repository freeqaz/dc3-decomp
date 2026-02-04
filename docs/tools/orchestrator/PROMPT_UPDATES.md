# Orchestrator Master Prompt Updates

**Date**: 2026-01-25
**File**: `scripts/master_agent_prompt.md`
**Status**: Updated with Tier 1 & 2 improvements

---

## Summary

The master agent prompt that gets passed to all subagents has been updated to reflect the massive tooling improvements from Tier 1 & 2. Agents now have:

- ✅ **6x faster iteration** (5s instead of 95s per cycle)
- ✅ **Better verdicts** with actionable recommendations
- ✅ **Improved tools** (mangled symbols, unit discovery, incremental builds)
- ✅ **Updated strategy** for fast-iteration workflows

---

## Key Changes Made

### 1. Prominent Tooling Notification (Top of Prompt)

**Added**: Highlighted notice about improvements

```markdown
**IMPORTANT: Tooling significantly improved (2026-01-25)**
- ✅ Incremental builds default (2-4s instead of 88s)
- ✅ Better verdicts with actionable patterns
- ✅ Faster unit discovery with `--list-units`
- ✅ Caching for repeated queries (200x speedup on hits)
```

**Why**: Agents need to understand the new reality immediately so they can use it effectively.

---

### 2. Phase 2: Analyze Current State (Updated)

**Before**:
```bash
./bin/analyze-function "{symbol}" -f json
```

**After**:
```bash
# NEW: Incremental builds enabled by default (2-4s total)
./bin/analyze-function "{symbol}" -f json
```

Plus added context:
```
**Note:** With incremental builds, you can iterate faster - each cycle takes
~5 seconds instead of ~95 seconds. This means you can try more variations
and learn faster.
```

**Why**: Agents need to know iteration is now dramatically faster and should take advantage.

---

### 3. Phase 4: Verify and Iterate (Updated)

**Before**:
```bash
./bin/objdiff-cli diff -p . "{symbol}" --build --verdict
```

**After**:
```bash
# NEW: Incremental builds enabled (fast path, 2-4s per build)
./bin/objdiff-cli diff -p . "{symbol}" --build --verdict
```

Plus added context:
```
**Performance:** With incremental builds, each cycle takes ~5 seconds
(was ~95 seconds). Use this to iterate faster and try more variations.
```

**Why**: Agents need reminders to leverage the speed advantage for faster experimentation.

---

### 4. Phase 5: Respond to Verdict (Enhanced)

**Before**: Simple verdict table with basic actions

**After**: Enhanced table with typical path info

```markdown
| Verdict | Action | Typical Path |
|---------|--------|--------------|
| **COMPLETE** | ... | Victory - function perfect |
| **LIKELY_FIXABLE** | ... | Usually fixes in 1-3 tries |
| **MAYBE_FIXABLE** | ... | Moderate difficulty, try 2-5 variations |
| **AT_LIMIT** | ... | Stop here - patterns verified unfixable |
```

Plus added efficiency tips:
```
**Iteration Efficiency Tips:**
- With incremental builds (2-4s per cycle), you can try 10+ variations
  in the time a single full build takes
- Use this speed advantage to test multiple hypotheses
- Pattern-based verdicts now tell you exactly what to try next
```

**Why**: Agents need to understand how to use speed advantage strategically.

---

### 5. Phase 6: Know When to Stop (Updated Strategy)

**Before**: Soft stop at 5 iterations

**After**: Adjusted for new speed reality

```markdown
**Never give up too early on:**
- `LIKELY_FIXABLE` - these usually respond to control flow changes
  (1-3 iterations typical)
- `MAYBE_FIXABLE` - variable reordering often helps here
  (2-5 iterations typical)

**NEW Advantage:** With 5-second iterations instead of 95-second,
you have much more tolerance for experimentation. Trying 10 variations
is now practical.
```

Plus updated guidance:
```
**NEW:** Because iteration is now fast (5s), you can be more thorough
in exploration before declaring stuck. Push to 8-10 iterations on
MAYBE_FIXABLE verdicts.
```

**Why**: The old 5-iteration limit was based on ~95s builds. With 5s builds, agents can afford to be more thorough.

---

### 6. New Tool Reference Section

**Added**: Complete updated tool documentation

```markdown
## Tool Reference

### NEW (2026-01-25): Tier 1 & 2 Improvements

**Bug Fixes:**
- Mangled symbol handling fixed
- Build path resolution fixed
- Unit discovery improved

**Performance Improvements:**
- Incremental builds: 88s → 0.94s (94x faster)
- Default behavior changed to incremental
- Use `--full-build` flag for validation builds

### Tool Commands

./bin/analyze-function --list-units      # New discovery
./bin/analyze-function --list-units pattern  # Filter
./bin/objdiff-cli report function ... "??0Foo@@QAA@XZ"  # Mangled now work
```

**Why**: Agents need to know about new capabilities and how to use them.

---

### 7. New Iteration Strategy Section

**Added**: Updated workflow for fast builds

```markdown
## Iteration Strategy (Updated for Fast Builds)

With incremental builds now the default (5s per cycle instead of 95s):

1. **On LIKELY_FIXABLE:** Try 5-10 control flow variations
   (usually solves in 1-3)
2. **On MAYBE_FIXABLE:** Try variable reordering (2-5 variations typical)
3. **On stuck:** Before giving up, try 2-3 more creative variations
4. **Budget:** You now have time for ~10 iterations in the cost
   of 1 old iteration

Example: A function that took 45 minutes of full builds can now
be fully explored in 1 minute.
```

**Why**: Agents need a new mental model of how to approach problems with fast iteration.

---

### 8. Updated Documentation References

**Before**:
```markdown
- `CLAUDE.md` - Project overview
- `docs/tools/WORKFLOW.md` - Tool reference
- `docs/decomp/TECHNICAL_NOTES.md` - PowerPC quirks
- `docs/decomp/RB3_REFERENCE.md` - Rock Band 3 reference
```

**After**: Added new docs about improvements

```markdown
- `CLAUDE.md` - Project overview
- `docs/tools/WORKFLOW.md` - Tool reference
- `docs/decomp/TECHNICAL_NOTES.md` - PowerPC quirks
- `docs/decomp/RB3_REFERENCE.md` - Rock Band 3 reference
- **NEW:** `TOOLING_IMPROVEMENTS_SUMMARY.md` - Recent improvements (6-25x faster)
- **NEW:** `docs/TIER_1_2_COMPLETION_REPORT.md` - Full completion details
```

**Why**: Agents can now reference the comprehensive improvements documentation.

---

## Impact on Agent Behavior

### Behavioral Changes Expected

**1. More Experimentation**
- Agents will try more variations (now practical with 5s builds)
- Expected improvement in MAYBE_FIXABLE verdicts (more thorough exploration)

**2. Better Pattern Understanding**
- New actionable verdicts with recommendations
- Agents can respond more precisely to patterns

**3. Faster Completions**
- Average function time: 15 min → 3-4 min (4-5x faster)
- More functions processable in same time budget

**4. Better Decision Making**
- Clear guidance on when to stop (push to 8-10 on MAYBE_FIXABLE)
- Better cost-benefit analysis (try more before "stuck")

### Expected Outcomes

With updated prompt:
- ✅ Higher completion rate on MAYBE_FIXABLE verdicts
- ✅ Fewer "stuck" results (more exploration)
- ✅ Faster per-function time
- ✅ Better agent resource utilization

---

## Backward Compatibility

**All changes are backward compatible**:
- Existing commands still work
- Old flags still work (e.g., `--full-build`)
- Tool behavior unchanged
- Only new capabilities added

Agents using old strategies will still succeed, just less efficiently.

---

## How Orchestrator Uses This

### Current Flow (in `scripts/orchestrator/core.py`)

1. **Build prompt from template**:
   ```python
   template = self._load_prompt_template()  # Loads master_agent_prompt.md
   return template.format(symbol=func["symbol"], ...)
   ```

2. **Add build strategy hint**:
   ```python
   if use_incremental:
       build_hint = "\n**Build Strategy:** Incremental (fast, 2-4s per build)..."
   ```

3. **Pass to agent**:
   ```python
   result = await self._run_agent_process(prompt=prompt, ...)
   ```

The master prompt is the foundation, and build strategy hints are appended by orchestrator.

---

## Testing the Changes

When agents run with updated prompt, they should:

1. ✅ Understand incremental builds are default
2. ✅ Try more variations (push to 8-10 on MAYBE_FIXABLE)
3. ✅ Use `--list-units` for discovery
4. ✅ Understand mangled symbols now work
5. ✅ Iterate faster with 5s cycles

---

## Version History

| Date | Changes | Status |
|------|---------|--------|
| 2026-01-25 | Added Tier 1 & 2 improvements, updated iteration strategy | ✅ Active |
| Before | Original prompt (pre-improvements) | Superseded |

---

## Related Documents

- [TOOLING_IMPROVEMENTS_SUMMARY.md](../TOOLING_IMPROVEMENTS_SUMMARY.md) - Executive summary
- [TIER_1_2_COMPLETION_REPORT.md](TIER_1_2_COMPLETION_REPORT.md) - Detailed results
- [IMPLEMENTATION_QUICK_START.md](IMPLEMENTATION_QUICK_START.md) - Tool usage guide

---

## Files Modified

- `scripts/master_agent_prompt.md` - Updated with improvements, new strategy, tool references

## Files NOT Modified

- `scripts/orchestrator/core.py` - Still works with updated prompt
- `scripts/decomp_orchestrate.py` - Still works with updated prompt
- All other orchestrator code - Fully compatible

---

**Summary**: Master prompt now reflects the 6-25x faster tooling reality, gives agents actionable guidance for fast iteration, and teaches new workflow strategy optimized for quick cycles.
