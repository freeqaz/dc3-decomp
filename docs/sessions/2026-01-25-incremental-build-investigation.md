# Incremental Build Investigation Guide

**Purpose**: Determine feasibility and strategy for 88s → 3-5s build time improvement

**Status**: Pre-investigation (ready to execute)

---

## Problem Statement

- **Current**: Full rebuild takes ~88 seconds
- **Goal**: Single-file rebuild takes 3-5 seconds
- **Value**: Single function analysis goes from 2 min → 20 sec (6.4x faster)

---

## Investigation Checklist

### Part 1: Verify Ninja's Incremental Capability (30 minutes)

**Test 1.1**: Can we target individual object files?
```bash
# Full build (baseline)
time ninja

# Single file build
time ninja build/373307D9/obj/system/char/Character.obj

# Record times and compare
```

**Test 1.2**: What's the actual .obj file path pattern?
```bash
# List all .obj files
find build/373307D9 -name "*.obj" | head -20

# Expected pattern:
# build/373307D9/src/...obj or build/373307D9/obj/...obj?
```

**Test 1.3**: Check build.ninja for obj file naming
```bash
grep -n "\.obj" build.ninja | head -20

# Look for pattern like:
# build build/373307D9/obj/system/char/Character.obj: msvc ...
# or
# build build/373307D9/src/system/char/Character.obj: msvc ...
```

**Outcome**:
- [ ] Confirmed: Single file targeting works
- [ ] Measured: Time savings (e.g., 88s → 4s = 22x faster)
- [ ] Documented: Exact path pattern for obj files

---

### Part 2: Test Ninja Dependency Tracking (1 hour)

**Test 2.1**: Does ninja rebuild on header changes?
```bash
# Baseline: build Character.obj (should be cached)
time ninja build/373307D9/obj/system/char/Character.obj

# Now touch a header it includes
touch src/system/char/Char.h

# Rebuild - should be fast since we just touched the header
time ninja build/373307D9/obj/system/char/Character.obj

# Expected: Header change triggers rebuild, but it's still fast
```

**Test 2.2**: Check include dependencies
```bash
# Find what Character.cpp includes
head -20 src/system/char/Character.cpp | grep "#include"

# Then touch one of those headers
touch src/system/char/Char.h

# Rebuild and time
time ninja build/373307D9/obj/system/char/Character.obj
```

**Outcome**:
- [ ] Confirmed: Header changes trigger rebuild
- [ ] Measured: Rebuild time is still <10s
- [ ] Documented: How ninja tracks dependencies

---

### Part 3: Test Verdict Accuracy (1 hour)

**Problem**: Does verdict from incremental build match verdict from full build?

**Test 3.1**: Compare verdicts: incremental vs full

```bash
# Setup: Have a partially-built project state
# Then test a function

# Full build first
ninja  # 88s
./bin/analyze-function "System::Character::Poll" > full_build_verdict.txt

# Now incremental build on just that file
ninja build/373307D9/obj/system/char/Character.obj  # 4s
./bin/analyze-function "System::Character::Poll" > incremental_verdict.txt

# Compare
diff full_build_verdict.txt incremental_verdict.txt
# Should be identical
```

**Test 3.2**: Batch test (20 functions, incremental vs full)
```bash
# Full build
ninja
time ./bin/analyze-function --batch-stdin < function_list.txt > full_results.json

# Clean and incremental
rm build/373307D9/obj/system/char/*.obj
time ninja build/373307D9/obj/system/char/*.obj
time ./bin/analyze-function --batch-stdin < function_list.txt > incremental_results.json

# Compare match percentages
jq '.[] | {symbol, match_percent}' full_results.json > full_matches.txt
jq '.[] | {symbol, match_percent}' incremental_results.json > incremental_matches.txt
diff full_matches.txt incremental_matches.txt
# Should be identical
```

**Outcome**:
- [ ] Confirmed: Verdicts match exactly
- [ ] Measured: Time savings on batch (e.g., 1 hour → 15 min)
- [ ] Documented: Verdict accuracy validation method

---

### Part 4: Edge Case Analysis - Vtable Misalignment (2 hours)

**Problem**: When a class layout changes, its vtable breaks more than just the .obj file

**Test 4.1**: Can we detect vtable issues?

```bash
# Scenario: Modify a class definition
# Example: Add/remove a member from CharBase class

# 1. Baseline full build
ninja
./bin/analyze-function "System::Character::Poll" > baseline.txt

# 2. Modify class definition (e.g., add one member)
# Edit src/system/char/Char.h: add "uint32_t new_field;" to CharBase

# 3. Rebuild only that file's .obj
ninja build/373307D9/obj/system/char/Character.obj

# 4. Reanalyze
./bin/analyze-function "System::Character::Poll" > after_vtable_change.txt

# 5. Compare verdicts
diff baseline.txt after_vtable_change.txt
# Expected: Match % changes, possibly LINKER_MERGED or AT_LIMIT verdict
```

**Test 4.2**: How often does this happen?
```bash
# Review git history for class definition changes
git log --oneline -p src/system/char/Char.h | grep "^+" | wc -l

# This tells us: in last 100 commits, how many lines were added to class headers?
# If rare: edge case is not a blocker
# If common: need periodic full builds
```

**Test 4.3**: Can we detect this and auto-escalate?
```bash
# Strategy: Check if verdict changed unexpectedly
# Example logic:
# - Expected verdict: LIKELY_FIXABLE (85% match)
# - Actual verdict: LINKER_MERGED (50% match)
# → Flag for full rebuild validation

# Can objdiff detect this pattern automatically?
objdiff-cli report function ... "System::Character::Poll" --verdict
# Look for LINKER_MERGED pattern in output
```

**Mitigation Strategy**:
- Run full rebuild every N commits (e.g., N=10)
- Or: Detect LINKER_MERGED pattern and escalate to full rebuild
- Or: Have orchestrate tool coordinate: 9 incremental builds, 1 full build per batch

**Outcome**:
- [ ] Confirmed: Vtable issues exist but are rare
- [ ] Documented: How often they occur (from git history)
- [ ] Planned: Mitigation (periodic full builds, pattern detection, orchestrate coordination)

---

### Part 5: Integration with Tools (2 hours)

**Test 5.1**: Can analyze-function use incremental builds?
```bash
# Current flow:
# 1. Edit src/system/char/Character.cpp
# 2. Run: ./bin/analyze-function "Character::Poll"
# 3. This calls: ninja (full rebuild)
# 4. Then analyzes with Ghidra

# Proposed flow:
# 1. Edit src/system/char/Character.cpp
# 2. Run: ./bin/analyze-function "Character::Poll" --incremental
# 3. This calls: ninja build/373307D9/obj/system/char/Character.obj
# 4. Then analyzes with Ghidra

# Test: Implement --incremental flag and measure time
```

**Test 5.2**: Can objdiff-cli use incremental builds?
```bash
# Current: objdiff-cli diff -p . "Foo::Bar" --build
# This calls: ninja (full rebuild)

# Proposed: objdiff-cli diff -p . "Foo::Bar" --build --incremental
# This calls: ninja build/373307D9/obj/system/foo/Bar.obj

# Test: Add --incremental flag and measure time
```

**Test 5.3**: Can orchestrate coordinate incremental + periodic full builds?
```bash
# Proposed workflow:
# 1. Run 9 incremental agent builds (each ~5s)
# 2. Merge results
# 3. Run 1 full build for validation (~88s)
# 4. Check for regressions (vtable issues, linker changes)
# 5. Repeat

# This gives: 9 functions in ~45s + 1 full build = ~130s total
# Equivalent to ~14 functions per full rebuild period

# Test: Modify orchestrate.py to implement this strategy
```

**Outcome**:
- [ ] Confirmed: Tools can be modified to use --incremental
- [ ] Documented: Changes needed in each tool
- [ ] Planned: Integration strategy (flags, orchestrate coordination)

---

### Part 6: Performance Measurement (1 hour)

**Goal**: Establish baseline metrics before implementation

**Measurement 6.1**: Single function analysis
```bash
# Before optimization:
time ./bin/analyze-function "System::Character::Poll"
# Expected: ~120 seconds

# After optimization (with incremental):
time ./bin/analyze-function "System::Character::Poll" --incremental
# Expected: ~20 seconds
# Improvement: 6.4x
```

**Measurement 6.2**: Batch of 50 functions
```bash
# Before:
time ./bin/orchestrate batch "src/system/char/*" --max-agents 1 --limit 50
# Expected: ~100 minutes

# After:
time ./bin/orchestrate batch "src/system/char/*" --max-agents 10 --limit 50
# Expected: ~5 minutes (with 10 parallel agents, each incremental)
# Improvement: 20x
```

**Measurement 6.3**: Full build baseline
```bash
# Measure for regression testing
time ninja
# Record this time: _____ seconds

# Create baseline:
ninja baseline  # Creates build/373307D9/baseline.json
```

**Outcome**:
- [ ] Baseline measured: Full ninja takes ~88 seconds
- [ ] Single file measured: ninja build/.../obj/system/char/Character.obj takes ~___ seconds
- [ ] Improvement calculated: ___x faster
- [ ] Baseline committed: build/373307D9/baseline.json stored

---

## Investigation Phases

### Phase A: Quick Feasibility (2 hours)
- Tests 1.1-1.3: Can we target individual files?
- Tests 2.1-2.2: Do dependencies work correctly?
- Quick decision: Yes/No/Maybe on incremental approach

### Phase B: Validation (3 hours)
- Tests 3.1-3.2: Do verdicts match?
- Tests 4.1-4.3: How often do edge cases occur?
- Decision: What mitigation strategy for vtable issues?

### Phase C: Integration Planning (2 hours)
- Tests 5.1-5.3: How to integrate with tools?
- Tests 6.1-6.3: Establish metrics for regression testing
- Document: Implementation approach

**Total Investigation Time**: ~7 hours
**Decision Point**: Phase A (2 hours) can determine if this is worth pursuing

---

## Expected Outcomes

### Best Case (Likely)
- ✅ Single file targeting works
- ✅ Time savings: 88s → 3-5s (17-29x faster)
- ✅ Verdicts are accurate
- ✅ Vtable issues are rare (detect and mitigate)
- ✅ Easy integration into tools
- **Action**: Proceed with Tier 2.1b-d implementation

### Middle Case (Possible)
- ✅ Single file targeting works but slower (e.g., 15-20s)
- ⚠️ Marginal improvement (88s → 20s = 4.4x)
- ✅ Still worth doing but less impactful
- **Action**: Proceed with implementation but set expectations

### Worst Case (Unlikely)
- ❌ Single file targeting doesn't work (needs full rebuild anyway)
- ❌ Verdicts don't match reliably
- **Action**: Skip incremental builds, focus on other optimizations (caching, service reliability)

---

## Decision Tree

```
Start Investigation
├─ Phase A (2 hours): Ninja targeting works?
│  ├─ YES → Continue to Phase B
│  └─ NO → Stop, use alternative strategy (service reliability + caching)
│
├─ Phase B (3 hours): Verdicts accurate + edge cases acceptable?
│  ├─ YES → Continue to Phase C
│  └─ NO → Stop, use alternative strategy
│
└─ Phase C (2 hours): Integration feasible?
   ├─ YES → Proceed with Tier 2.1b-d implementation
   └─ NO → Redesign approach
```

---

## Next Steps

1. **Schedule investigation**: Block 7 hours (or 2 hours for Phase A)
2. **Assign investigator**: Backend engineer
3. **Document findings**: Create docs/tools/INCREMENTAL_BUILDS_RESULTS.md
4. **Decision meeting**: Review findings, decide on implementation approach
5. **Begin Tier 2.1b** if approved (usually approved after Phase A)

---

## Files to Reference

- `build.ninja` - Build configuration (understand .obj paths)
- `bin/analyze-function` - Where to add `--incremental` flag
- `tools/objdiff-cli` (or wrapper) - Where to add incremental support
- `bin/orchestrate` - Where to add coordination logic
- `.mcp.json` - Service configuration (for context)

---

## Questions Answered by Investigation

1. How much faster is single-file targeting? (2-29x?)
2. Can we reliably use incremental builds for function analysis?
3. How often do vtable misalignment issues occur?
4. What's the best mitigation strategy?
5. Should we implement this before Tier 2.2 (caching) or after?
6. How should orchestrate coordinate mixed incremental+full builds?

