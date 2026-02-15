# 2026-02-14: Batch Sweep Session & Tooling Review

## Session Summary

This was a multi-hour autonomous session focused on two activities:
1. **Batch-checking** thousands of 0% (untracked) functions that were already compiled and matching at 100%
2. **Implementing** ~21 new functions via background agents

### Progress Numbers

| Metric | Before | After | Delta |
|--------|--------|-------|-------|
| COMPLETE | ~25,500 | 26,436 | +~936 |
| AT_LIMIT | ~1,945 | 1,962 | +17 |
| Decomp progress (non-excluded) | ~79% | 82.5% | +3.5% |

### Batch-Checking Results

Background agents and manual checking confirmed 500+ functions as 100% across:
- `system/rndobj` - 175 functions (single agent sweep)
- `lazer/meta_ham` + `lazer/game` - 232 functions
- `system/flow` - ~15 functions
- `system/synth` (filterdesign, StreamReceiver, etc.) - ~20 functions
- `system/char` - 19 functions
- `system/obj` - 14 functions
- `system/ui` - ~10 functions
- `system/os` (PlatformMgr_Xbox message types) - ~10 functions

Hit rate was effectively 100% - every function checked was already matching.

### New Implementations

| Function | Match | Notes |
|----------|-------|-------|
| `RockCentral::Poll` | 99.4% | 318 instructions, complex state machine |
| `MetaPerformer::CalculatePracticeResults` | 97.7% | AT_LIMIT (merged + regswap) |
| `MetaPerformer::GetCurrentRecapMove` | 98.1% | Dead-code vector<bool> trick |
| `MetaPerformer::SetDefaultSongCharacter` | 88.9% | AT_LIMIT (merged + regswap) |
| `RndPostProcMgr::Poll` | 99.4% | Time-based interpolation |
| `RndShockwave::Load` | 97.2% | Manual gRevs[] pattern |
| `DataFactory` | 99.2% | One-liner, __FILE__ diff |
| `BufFile::Eof` | 100% | Pointer subtraction ordering |
| `Fader::SynthPoll` | 88.9% | AT_LIMIT, needed `__fsel` intrinsic |
| `MetaPanel::PickLoopIndex` | 100% | Post-increment `arr[idx++]` trick |
| `RndTransAnim::Replace` | 99.4% | dynamic_cast chain |
| `ChooseModeProvider::SetType` | 98.9% | OBJ_SET_TYPE macro |
| `DingoSvrXbox::Poll` | 100% | XLSP connection lifecycle |
| `SpotlightDrawer::SetAmbientColor` | 99.4% | |
| `SpotlightDrawer::DrawNGSpotlights` | 99.2% | |
| `InlineHelp::OldResourcePreload` | 100% | |
| `InlineHelp::UpdateTextColors` | 99.8% | ICF merged |
| `AutoTimer::ResetTimers` | 99.2% | |
| `CharClipGroup::Sort` | 100% | Named struct for comparator |
| `Synth::DrawMeterScale` | 96.7% | AT_LIMIT (merged MakeString) |
| `Synth::CullZombies` | 95.0% | AT_LIMIT (regswap) |

---

## Tooling Review

### Problem 1: No batch objdiff + auto-report (highest impact)

**What happened**: The dominant workflow was a tedious loop:
```
query_functions(unit, 0%, 0%) → manually filter junk → run_objdiff(symbol) → report_result(symbol) → repeat
```

For 500+ functions, this required ~1000 sequential MCP calls. Each `report_result` call was identical except for the symbol name. Background agents helped parallelize but each still ran the same serial loop internally.

**Proposed fix**: New MCP tool:
```python
def batch_check_unit(unit_pattern: str, auto_report: bool = True) -> dict:
    """Run objdiff on all 0%/untracked functions in a unit.
    Auto-report 100% matches as COMPLETE.
    Return summary: {checked: N, complete: N, partial: [...], failed: [...]}"""
```

This single tool would have replaced 80% of the session's manual work.

**Implementation notes**:
- Could live in `mcp_server.py` as a new MCP endpoint
- Internally: query DB for untracked functions in unit → run objdiff on each → report 100% matches
- Should have built-in filtering for boilerplate (atexit, templates, thunks)
- Return a structured summary, not raw objdiff output per function

### Problem 2: `query_functions` returns too much noise

**What happened**: Every query returned 70-80% boilerplate:
- `??__F` - dynamic atexit destructors
- `??__E` - dynamic initializers
- `??$MakeString` - MakeString template instantiations
- `??_9` - vcall thunks
- `??_E` / `??_G` - vector constructor/destructor iterators

These are real functions but they're not interesting for decomp work. Having to mentally skip them every time slowed down function selection.

**Proposed fix**: Add parameters to `query_functions`:
```python
skip_boilerplate: bool = False  # Filter ??__F, ??__E, ??$MakeString, ??_9, etc.
name_pattern: str = None        # Regex/glob on demangled name
```

### Problem 3: No bulk verdict update

**What happened**: An earlier classifier agent found ~20K functions at 100% match that need verdict set to COMPLETE. There's no way to do this in bulk - each requires an individual `report_result` call.

Additionally, the database has a case inconsistency: 1,945 functions have verdict `AT_LIMIT` (uppercase) while 17 have `at_limit` (lowercase). The `report_result` tool stores lowercase, while some other path stores uppercase.

**Proposed fix**:
```python
def bulk_update_verdict(filter_sql: str, verdict: str) -> int:
    """Update verdict for all functions matching filter. Returns count updated."""

# Example: mark all 100% as COMPLETE
bulk_update_verdict("current_percent = 100 AND verdict IS NULL", "COMPLETE")

# Example: normalize AT_LIMIT casing
bulk_update_verdict("verdict = 'at_limit'", "AT_LIMIT")
```

### Problem 4: No built-in progress dashboard

**What happened**: Getting progress stats required writing a Python script every time, fighting shell quoting issues with SQL strings. Did this multiple times across sessions.

**Proposed fix**: New MCP tool:
```python
def get_progress() -> dict:
    """Return decomp progress summary.
    {total, complete, at_limit, excluded, remaining,
     by_unit: [{unit, total, complete, at_limit, remaining}]}"""
```

### Problem 5: Background agent fragility

**What happened**: Two background agents (`a2c0022`, `ad67471`) died mid-sweep when they hit API rate limits. Functions they'd confirmed as 100% via objdiff but hadn't yet reported were lost - the work was wasted.

**Root cause**: Agents batch their reports - they check several functions, then report them in a group. If the agent dies between checking and reporting, the results are lost.

**Proposed fix**:
- Report each result immediately after confirming (don't batch)
- Or: the batch_check_unit tool would handle this atomically - no agent needed

### What worked well

- **Background agents for sweeps**: Launching 4-5 agents in parallel across different code areas was effective. The rndobj agent alone confirmed 175 functions.
- **Implementation agents**: Focused agents for specific functions (RockCentral::Poll, MetaPerformer, etc.) produced good results with clear scope.
- **`run_objdiff` with concise=true**: Fast, reliable, perfect for batch checking.
- **`lookup_rb3`**: Useful for implementation agents finding reference code in the shared Milo engine.
- **`run_diff_inspect` diagnose mode**: Good for quickly classifying whether a mismatch is fixable.

---

## Remaining Work

### Database state as of session end

| Category | Count | Notes |
|----------|-------|-------|
| COMPLETE | 26,436 | 55.3% of total, 82.5% of non-excluded |
| AT_LIMIT | 1,962 | Unfixable (regalloc, ICF, __FILE__) |
| Excluded | 15,785 | SDK/lib code |
| NULL verdict (excluded) | 15,765 | Don't need work |
| NULL verdict (not excluded) | 3,672 | Need checking - mostly templates, atexit |

### Units with remaining NULL-verdict non-excluded functions

These are the areas where more batch-checking would yield results:
- `default/App` - general app-level template instantiations
- `system/hamobj/*` - large unit, sweep started but agent died
- `system/os/PlatformMgr_Xbox` - sweep started but agent died
- Various other units with scattered template instantiations

### Priority for next session

1. **Build the batch_check_unit tool** - highest ROI, eliminates the tedious loop
2. **Bulk mark remaining 100% functions as COMPLETE** - sweep the 3,672 remaining
3. **Normalize verdict casing** - fix AT_LIMIT vs at_limit
4. **Continue new implementations** - focus on functions identified by the "Find implementable functions" agent (12 unimplemented functions documented)
