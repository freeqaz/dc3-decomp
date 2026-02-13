# Ghidra Type Seeding Stress Test - Testing Protocol

**Purpose**: Systematically evaluate whether type-aware Ghidra decompilation improves DC3 decomp workflow.

**Date**: 2026-02-13
**Duration**: 2 sessions (~20% setup, ~80% execution)

---

## Prerequisites

### 1. Ghidra Service Running
```bash
./tools/ghidra/pyghidra-service.sh status
# Should show: "Service running... Status: Ready"
```

If not running:
```bash
./tools/ghidra/pyghidra-service.sh start
```

### 2. Type Seeding Applied
```bash
python3 tools/ghidra/batch_export_types.py --seed
# Should show: "Applied: ~11733"
```

This seeds:
- 2105 DC3 structures from `struct_db`
- 17,706 `this` pointer types for member functions

### 3. Working Directory
```bash
cd /home/free/code/milohax/dc3-decomp
```

---

## Test Candidates (5 Functions)

| Function | Mangled Symbol | Match | Size | Key Issues |
|----------|----------------|-------|------|------------|
| **SaveLoadManager::Handle** | `?Handle@SaveLoadManager@@QAEXPAVUI@HAVUIListLabel@@HH@Z` | 95.1% | 655 insn | 112 diff_arg (struct access) |
| **UIEventMgr::TriggerEvent** | `?TriggerEvent@UIEventMgr@@QAAXABVsymbol@@PAVDataArray@@@Z` | 95.6% | 175 insn | 25 diff_arg + control flow |
| **CharBonesSamples::Load** | `?Load@CharBonesSamples@@QAAXAAVBinStream@@@Z` | 96.7% | 73 insn | Register swaps + BinStream |
| **BaseMaterial::PropValDifferent** | `?PropValDifferent@BaseMaterial@@QAA_NABVsymbol@@0@Z` | 97.7% | 102 insn | Symbol comparison |
| **RhythmBattlePlayer::Load** | `?Load@RhythmBattlePlayer@@UAA_NPAVBinStream@@_N@Z` | 97.7% | 166 insn | BinStream + offsets |

---

## Workflow (Per Function)

### Step 1: Baseline Analysis (3-5 min)

Get current state without modifications:

```bash
# Get objdiff analysis
mcp__orchestrator__run_objdiff \
  "[mangled_symbol]" \
  /home/free/code/milohax/dc3-decomp
```

**Record in findings template**:
- Match percentage
- Verdict (FIX/SKIP/etc)
- Primary mismatch patterns
- Counts (insertions, deletions, replaces)

**Save objdiff output** for comparison later.

---

### Step 2: Ghidra Analysis (5-10 min)

Get type-aware decompilation:

```bash
# Full analysis with types and cross-refs
mcp__orchestrator__run_analyze_function \
  "[mangled_symbol]" \
  /home/free/code/milohax/dc3-decomp
```

**Examine critically**:

#### Class Types
- Is `this` pointer typed correctly? (`SaveLoadManager*` vs `int*`)
- Are method calls clear? (`obj->Load(...)` vs `FUN_82XXXXXX(obj, ...)`)

#### Struct Members
- Are offsets resolved? (`this->mCurrentSave` vs `*(param_1 + 0x48)`)
- Do member names match headers?
- Are types accurate? (pointer vs int, etc)

#### Function Signatures
- Are parameter types visible?
- Are return types clear?
- Do SDK function calls show signatures?

#### Cross-References
- Callers: Who calls this function? Usage patterns?
- Callees: What does this call? Any surprises?

**Rate each aspect 1-5** in findings template.

---

### Step 3: Root Cause Analysis (5-15 min)

Compare Ghidra decompilation to our C++ source:

```bash
# Read our current implementation
# (Use Read tool on the .cpp file)
```

**Look for**:
1. **Type mismatches**: Wrong pointer types, missing casts
2. **Struct access**: Offset 0x48 is `mFoo` but we access `mBar`
3. **Member order**: Header declares X, Y, Z but Ghidra shows Y, X, Z
4. **SDK types**: Are we using correct SDK struct definitions?
5. **Control flow**: Does Ghidra suggest different if/else structure?

**Use supplemental tools**:

```bash
# Resolve specific offsets if unclear
mcp__orchestrator__lookup_struct_offset \
  "ClassName" \
  "0x48"

# Check class layout
mcp__orchestrator__struct_info "ClassName"

# Inspect specific mismatches
mcp__orchestrator__run_diff_inspect \
  "[mangled_symbol]" \
  "diagnose" \
  /home/free/code/milohax/dc3-decomp
```

**Document**: Time to identify root cause (stopwatch from Step 2 start)

---

### Step 4: Matching Attempt (10-30 min)

**IMPORTANT**: This is a stress test, not a completion sprint.

**If root cause is obvious from types** (≤5 min to identify):
- Attempt fix
- Re-run objdiff to measure improvement
- Document outcome

**If root cause remains unclear** (>5 min to identify):
- Document what's blocking
- Note what additional info would help
- Move to next function

**Budget**:
- Spend max 30 minutes per function
- Focus on learning, not perfection
- It's OK to leave functions incomplete

---

### Step 5: Documentation (5 min)

Fill out findings template sections:
- Baseline metrics
- Type quality ratings
- Key observations
- Changes attempted
- Final results
- Time to root cause
- Learnings

**Critical questions**:
1. Did type info make the issue obvious? (Yes/No/Partial)
2. What would have made it clearer?
3. Is this mismatch fixable or systemic (register allocation, etc)?

---

## Cross-Function Analysis (End of Session)

After completing 3-5 functions:

### Pattern Recognition
- What mismatches do types solve well?
- What mismatches remain unclear despite types?
- Are there systemic issues (register allocation, linker artifacts)?

### Tooling Gaps
- What information was missing?
- What was hard to access?
- What workflows were painful?

### Value Assessment
- Did type seeding pay off?
- What's the time savings estimate?
- Should we expand type coverage (SDK, more headers)?

---

## Success Criteria

This is **investigative work**, not a completion task.

✅ **Minimum success**:
- Ran workflow on all 5 functions
- Documented findings for each
- Identified 2-3 tooling improvements
- Answered: "Does type seeding help?"

🎯 **Stretch goals**:
- Fixed 1-2 functions to 100%
- Discovered reusable patterns
- Proposed concrete next steps for tooling

⚠️ **Avoid**:
- Spending >30 min per function
- Getting stuck on unfixable issues (register allocation)
- Trying to complete everything (this is a sample, not a sprint)

---

## Files Generated

All findings documented in:
```
docs/permuter/ghidra-stress-test/
├── FINDINGS_TEMPLATE.md           (this template)
├── TESTING_PROTOCOL.md            (this file)
├── findings_SaveLoadManager.md    (per-function notes)
├── findings_UIEventMgr.md
├── findings_CharBonesSamples.md
├── findings_BaseMaterial.md
├── findings_RhythmBattlePlayer.md
└── SUMMARY.md                     (cross-function conclusions)
```

---

## Emergency Troubleshooting

### Ghidra Service Down
```bash
./tools/ghidra/pyghidra-service.sh restart
# Wait 10 seconds
./tools/ghidra/pyghidra-service.sh status
```

### Type Seeding Missing
```bash
python3 tools/ghidra/batch_export_types.py --seed
# Should show "Applied: ~11733"
```

### Objdiff Failures
```bash
# Full rebuild may be needed
ninja clean
ninja
```

### Port 8000 Conflict
```bash
# Find and kill conflicting process
lsof -i :8000
kill [PID]
./tools/ghidra/pyghidra-service.sh start
```
