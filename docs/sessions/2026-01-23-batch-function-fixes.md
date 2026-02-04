# Session: Batch Function Fixes with Parallel Agents

**Date:** 2026-01-23
**Focus:** Finding and fixing near-match functions using parallel subagent analysis

## Summary

Used batch triage with `objdiff-cli report analyze` to find fixable functions, then dispatched 10 parallel Sonnet agents to analyze candidates. Fixed 5 functions to 100% match.

## Progress

- **Starting matched functions:** 21,267 / 46,958 (45.29%)
- **Ending matched functions:** 21,275 / 46,958 (45.31%)
- **Functions fixed:** +8

## Functions Fixed to 100%

### 1. Pool::Pool (99.5% → 100%)
**File:** `src/system/utl/Pool.cpp`
**Issue:** Register operand order in `add` instruction
**Fix:** Reorder variable declarations - declare `ptr` before `stride`

```cpp
// Before
int stride = (i1 + 3) & ~3;
int count = i2 / stride;
char *ptr = (char *)v;

// After
char *ptr = (char *)v;
int stride = (i1 + 3) & ~3;
int count = i2 / stride;
```

### 2. _vp_offset_and_mix (99.85% → 100%)
**File:** `src/system/oggvorbis/psy.c`
**Issue:** Branch condition `bgt` vs `bge` from max() macro
**Fix:** Replace `max()` macro with explicit ternary using `>`

```c
// Before
logmask[i]=max(val,tone[i]+toneatt);

// After
{
  float t = tone[i]+toneatt;
  logmask[i] = val > t ? val : t;
}
```

**Key insight:** The `max()` macro was defined as `((x) < (y) ? (y) : (x))` which generates different branch conditions than `((x) > (y) ? (x) : (y))`.

### 3. HamIconMan::DrawShowing (99.9% → 100%)
**File:** `src/system/hamobj/HamIconMan.cpp`
**Issue:** Floating-point multiplication order (`fmuls` operand swap)
**Fix:** Use parentheses to force evaluation order

```cpp
// Before
float uiSeconds = TheTaskMgr.UISeconds() * 0.016666668f;
beat = uiSeconds * mBPMOverride;

// After
beat = TheTaskMgr.UISeconds() * (mBPMOverride * 0.016666668f);
```

### 4. FlowEventListener::ChildFinished (99.7% → 100%)
**File:** `src/system/flow/FlowEventListener.cpp`
**Issue:** Branch condition inverted (`bne` vs `beq`)
**Fix:** Invert the boolean condition and swap if/else branches

```cpp
// Before
if (unkb4) {
    FlowQueueable::ChildFinished(node);
} else {
    FlowNode::ChildFinished(node);
}

// After
if (!unkb4) {
    FlowNode::ChildFinished(node);
} else {
    FlowQueueable::ChildFinished(node);
}
```

### 5. HamDirector::CheckBeginFatal (98.9% → 100%)
**File:** `src/system/hamobj/HamDirector.cpp`
**Issue:** Compare immediate value and branch type (`cmpwi 1` + `bgt` vs `cmpwi 2` + `bge`)
**Fix:** Change comparison from `< 2` to `<= 1`

```cpp
// Before
if (i3 < 2) {

// After
if (i3 <= 1) {
```

**Key insight:** `i3 < 2` and `i3 <= 1` are semantically identical for integers, but generate different compare/branch pairs.

## Functions Improved (Not 100%)

### KinectSharePanel::ConvertImagesForLinkPost (99.6% → 99.97%)
**File:** `src/lazer/meta_ham/KinectSharePanel.cpp`
**Fix applied:** Swapped Width/Height assignment order
**Remaining:** LINKER_MERGED function calls (unfixable)

## Patterns Identified

### Fixable Patterns
1. **Variable declaration order** - Affects register allocation
2. **Boolean inversions** - `if (x)` vs `if (!x)` swaps branch conditions
3. **Comparison style** - `< N` vs `<= N-1` affects compare immediate values
4. **Operation order** - Parentheses control evaluation order for fmuls/fadds
5. **max()/min() macros** - Definition using `<` vs `>` affects branch conditions

### Unfixable Patterns (Skip These)
1. **LINKER_MERGED** - Linker merges identical functions (very common at 99%+)
2. **Struct layout mismatches** - Requires header changes, affects many functions
3. **File path strings** - `__FILE__` macro expansion differs between builds
4. **RTTI offsets** - dynamic_cast infrastructure layout differences
5. **Bitfield packing** - Compiler-specific, needs careful experimentation

## Workflow Used

### 1. Find Candidates
```bash
# Batch triage with verdicts
objdiff-cli report analyze build/373307D9/report.json \
  --min-percent 95 --max-percent 99.9 --limit 50 -f json-pretty

# Query specific ranges
objdiff-cli report query build/373307D9/report.json \
  --functions --min-percent 99 --max-percent 99.9 --limit 30
```

### 2. Parallel Agent Analysis
Launched 10 Sonnet subagents simultaneously to analyze candidates:
- Each agent runs objdiff diff with `--verdict --include-instructions`
- Reads source file
- Provides fixability verdict: FIXABLE, MAYBE, or SKIP

### 3. Apply Fixes
Edit source files based on agent recommendations, rebuild, verify.

## Agent Efficiency

Of 10 functions analyzed in parallel:
- **5 FIXABLE** - Clear source-level fixes identified
- **3 MAYBE** - Struct/layout issues requiring more investigation
- **2 SKIP** - Blocked by linker merging (unfixable)

## Commands Reference

```bash
# Build and generate report
ninja build/373307D9/report.json

# Check specific function
objdiff-cli report function build/373307D9/report.json "FunctionName"

# Detailed diff with verdict
objdiff-cli diff -p . "FunctionName" -f markdown --verdict --include-instructions

# Build single object and diff
objdiff-cli diff -p . "FunctionName" --build -f markdown --verdict
```

## Next Steps

1. Investigate struct layout issues (SongSequence, RhythmDetector)
2. Analyze bitfield packing patterns more systematically
3. Look for more boolean inversion and comparison style fixes
4. Consider bulk-fixing all `< N` to `<= N-1` patterns
