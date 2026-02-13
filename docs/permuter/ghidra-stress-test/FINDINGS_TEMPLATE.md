# Ghidra Type Seeding Stress Test - Findings Template

**Date**: 2026-02-13
**Tester**: Claude Sonnet 4.5
**Goal**: Evaluate whether type-aware Ghidra decompilation improves decomp workflow

---

## Function: [Function Name]

**Symbol**: `[Mangled name]`
**File**: `[src/path/to/file.cpp:line]`
**Subsystem**: [meta_ham / char / rndobj / etc]
**Size**: [N instructions]

---

### Baseline (Before Analysis)

| Metric | Value |
|--------|-------|
| **Match %** | X.X% |
| **Primary Mismatch Type** | [diff_arg / diff_replace / regswap / etc] |
| **Mismatch Count** | [N insertions, M deletions, K replaces] |
| **Verdict** | [FIX / SKIP / CHECK_BEHAVIOR / etc] |

**Objdiff Summary**:
```
[Paste key patterns from objdiff output]
```

---

### Ghidra Analysis (With Type Seeding)

**Command Used**:
```bash
mcp__orchestrator__run_analyze_function \
  "[mangled_symbol]" \
  /home/free/code/milohax/dc3-decomp
```

#### Type Information Quality

| Aspect | Rating (1-5) | Notes |
|--------|--------------|-------|
| **Class types visible** | [1-5] | Was `this` pointer typed correctly? |
| **Struct members named** | [1-5] | Were offsets resolved to member names? |
| **Function signatures** | [1-5] | Were parameter/return types clear? |
| **Cross-references** | [1-5] | Did callers/callees reveal patterns? |

#### Key Observations

**What was immediately clear from types?**
- [e.g., "Offset 0x48 is `mCurrentSave` - wrong type in source"]
- [e.g., "Parameter 2 should be `Symbol*` not `int`"]

**What remained unclear?**
- [e.g., "Register allocation still mysterious despite correct types"]
- [e.g., "Control flow mismatch - types didn't help"]

**Ghidra Decompilation Snippet** (if helpful):
```c
// Paste relevant portion showing typed decompilation
```

---

### Matching Attempt

**Changes Made**:
1. [Description of fix attempt 1]
2. [Description of fix attempt 2]
3. ...

**Result**:
- **Final Match %**: X.X%
- **Improvement**: +Y.Y%
- **Status**: [Fixed / Partial / Blocked]

**Time to Root Cause**: [X minutes] - How long from analysis start to identifying the core issue?

---

### Learnings

**Type Seeding Helpfulness**: [1-5 scale]

**Specific Value Add**:
- ✅ [What type info made obvious]
- ✅ [Another helpful insight]

**Gaps Identified**:
- ❌ [What was missing or unclear]
- ❌ [Tooling improvement needed]

**Pattern Recognized**:
- [e.g., "All BinStream calls have register swaps - likely unfixable"]
- [e.g., "Struct member reordering fixes offset mismatches"]

---

### Verdict

**Should pursue 100% match?** [Yes / No / Maybe]

**Reasoning**:
[Why this function is worth/not worth completing]

**Recommended Next Steps**:
- [If pursuing: specific actions]
- [If blocking: what needs to be fixed first]

---

## Cross-Function Patterns (Fill after analyzing multiple functions)

**Common themes across functions?**
- [Pattern observed in 2+ functions]

**Tooling requests?**
- [Missing features that would help]

**Overall assessment of type seeding value?**
- [Summary of whether this investment paid off]
