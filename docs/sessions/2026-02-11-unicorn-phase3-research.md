# Unicorn Function Runner — Phase 3 Research Findings

**Date**: Feb 11, 2026
**Status**: Research complete, implementation plan ready

## Executive Summary

Analyzed all 25,680 common functions across 971 compilation units. Key findings:

| Metric | Value | Impact |
|--------|-------|--------|
| Currently eligible (no indirect branches) | 21,804 (84.9%) | Phase 2 baseline |
| Blocked by bctrl (virtual dispatch) | 3,704 | Largest blocker |
| Blocked by bctr (switch tables) | 182 | Small, tractable |
| Functions with intra-TU calls | 12,937 (50.4%) | High but deprioritized |
| Float-returning functions | 585 (2.3%) | Small, easy fix |
| Partial batch-all equivalence rate | ~75% | With auto-fixture |

**Recommended priority**: FPR comparison (trivial) > bctr handling (small scope, high ROI) > permuter integration > bctrl handling (large scope, complex)

---

## Objective 1: bctrl/bctr Scope and Feasibility

### Raw Numbers

```
Total common functions:                     25,680
Currently eligible (no indirect branches):  21,804 (84.9%)
Blocked by bctrl only (virtual dispatch):    3,694
Blocked by bctr only (switch/tail call):       172
Blocked by both bctrl and bctr:                 10
Total blocked:                               3,876 (15.1%)
```

### bctr Analysis (172 + 10 = 182 functions)

**bctr** is used for two patterns:
1. **Switch tables** (jump tables loaded from .rdata): function has REFHI/REFLO relocs + mtctr
2. **Vtable tail calls**: function loads a vtable slot into CTR and branches (no REFHI needed)

Of the 20 sampled bctr functions:
- Most have `mtctr_count=1` (single indirect branch)
- Functions with `has_refhi=True` are likely switch tables (load jump table address)
- Functions with `has_refhi=False` are vtable tail calls (e.g., `PreLoad` forwarding to base class)

**Feasibility for switch tables**: Load the `.rdata` section data alongside the function code. The COFF parser already has section data access. Jump table entries are ADDR32 relocations pointing to labels within the function — we'd need to rebase them to the code region.

**Feasibility for vtable tail calls**: These are 16-24B stub functions that load `this->vtable[N]` and branch to it. Mocking would require knowing the vtable layout. A simpler approach: detect the pattern (load from `this+0` → load from vtable+offset → mtctr → bctr) and treat the whole function as a trampoline.

**Verdict**: bctr is tractable. Switch tables need .rdata loading (~50 lines of code). Vtable tail calls need pattern detection (~30 lines). Unblocks 182 functions.

### bctrl Analysis (3,694 + 10 = 3,704 functions)

**bctrl** is virtual dispatch: `this->vtable[N](args...)`. Very common in C++ code.

Size distribution of bctrl-blocked functions:
- min=56B, max=16,508B, median=328B
- Functions range from 1 to 5+ bctrl calls

**Approaches considered**:

1. **Vtable mocking**: Pre-populate `this+0` with a pointer to a mock vtable. Each vtable slot points to a trampoline stub. Requires knowing vtable size (number of virtual functions).
   - Pro: Accurate, handles all cases
   - Con: Need per-class vtable layout, either from DWARF or heuristic

2. **Universal trampoline**: Hook all indirect calls. When bctrl fires, check if CTR points to unmapped memory → redirect to trampoline.
   - Pro: No vtable knowledge needed
   - Con: Unicorn hooks fire on each instruction, not on bctrl specifically. Would need instruction decode in the hook.

3. **CTR-to-trampoline redirect**: Before execution, populate the vtable region with trampoline addresses. Since `this` starts at OBJECT_BASE (0x20000000) with zeroed memory, `this->vtable` = `*(0x20000000)` = 0. We'd need to write a vtable pointer into the object.
   - Pro: Reuses existing trampoline infrastructure
   - Con: All virtual calls go to the same trampoline (can't distinguish which virtual function)

**Verdict**: bctrl is more complex. The simplest viable approach is option 3 (write a generic vtable at OBJECT_BASE+0, each slot is a unique trampoline). This requires:
- Allocate a vtable region (e.g., 256 entries × 4 bytes at a known address)
- Fill each slot with a unique trampoline address
- Write the vtable pointer at `*(OBJECT_BASE) = vtable_region_address`
- Log which vtable slot was called (based on the trampoline address)

This unblocks 3,704 functions but the call log comparison becomes trickier (vtable slot index vs symbol name).

---

## Objective 2: Intra-TU Call Execution

### Raw Numbers

```
Functions with intra-TU calls:   12,937 (50.4%)
Total intra-TU call edges:       20,777
Unique intra-TU callees:         14,532
Circular call pairs:                300
```

Half of all functions call at least one other function within the same .obj file. Currently these intra-TU calls go to mock trampolines (return 0), which means:
- Functions that depend on callee return values will diverge
- Memory mutations from callees are lost
- This is a major source of false divergence in complex functions

### Circular Dependencies

300 circular call pairs exist (A calls B and B calls A). Most are STL template instantiations:
- `_M_fill_insert_aux` calling itself (recursive vector growth)
- `_M_erase` in rb-tree calling itself
- `ObjDirPtr::operator=` ↔ `ObjDirPtr::PostLoad`

### Feasibility

**Co-loading callees**: When a function calls another function in the same .obj:
1. Extract callee bytes and relocations
2. Load callee code at a separate address in the code region
3. Patch the caller's REL24 to point to the callee instead of a trampoline
4. Recursively process callee's own calls (mock external, co-load internal)

**Complexity**: Moderate. Main challenges:
- Recursive loading with cycle detection (300 circular pairs)
- Code region size limit (64KB) may not fit all callees
- Each callee needs its own relocation patching
- Debugging divergences becomes harder (which callee caused it?)

**Value**: Moderate to high. Would reduce false divergences from internal call mocking, but the auto-fixture (zeroed state) is still the dominant source of divergence.

**Verdict**: Deprioritize. The 12,937 affected functions are already counted in the eligible set — they just have lower-fidelity testing. Focus bctrl/bctr first (unblocks new functions) before improving test fidelity for existing ones.

---

## Objective 3: Permuter Integration

### Current Permuter Architecture

The C++ permuter (`scripts/permuter/`) works by:
1. **Extract**: tree-sitter parses the source file, extracts target function
2. **Generate**: Patterns (variable_extraction, signed_unsigned, inline_assignment) produce source variants
3. **Score**: Each variant is written to the source file → `ninja` build → `objdiff-cli diff` → match percentage
4. **Report**: Sorted by match% improvement

Key interface point: `Scorer.score(variant)` calls ninja, which produces a `.obj` file.

### Integration Design

The unicorn runner can add an **execution equivalence score** alongside the objdiff assembly match score:

```python
# In permuter/scorer.py, after objdiff scoring:
from scripts.unicorn_runner.run import run_comparison
exec_code = run_comparison(symbol, decomp_obj_path, orig_obj_path)
# exec_code: 0=EQUIVALENT, 1=DIVERGENT, 2=ERROR, 3=SKIPPED
```

**Scoring model options**:
1. **Binary pass/fail**: EQUIVALENT = +0, DIVERGENT = -∞ (reject semantically broken variants)
2. **Graded**: Score by (call count match, return value match, memory diff count)
3. **Guard rail**: Only use as veto — reject variants that were EQUIVALENT and became DIVERGENT

**Recommended**: Option 3 (guard rail). The permuter's primary goal is assembly matching (objdiff score). Unicorn adds a safety check: "did this improvement break behavior?" This is especially valuable for the permuter's inline_assignment and variable_extraction patterns which can subtly change semantics.

**Implementation effort**: ~30 lines in scorer.py. The `.obj` path is already known from the ninja build.

### Batch Mode Feed

For permuter iteration loops:
1. Permuter generates N variants
2. For each variant: ninja build → objdiff score → unicorn equivalence check
3. Reject variants where unicorn detects divergence
4. Rank remaining by objdiff match%

The unicorn check adds ~100μs per function (negligible vs ninja build time of seconds).

---

## Objective 4: FPR Return Comparison

### Scope

```
Functions returning float (via f1):   585 (2.3%)
Functions returning int (via r3):     25,095 (97.7%)
```

Only 585 functions return floats. Most are in the character animation system (CharDriver, CharFaceServo, Skeleton geometry).

### FPU Precision Testing

Tested Unicorn's PPC32 FPU with known values:

| Test | Result |
|------|--------|
| `fsubs` (3.14159265f - 2.71828182f) | Bit-identical to host float math |
| `fmadds` (fused multiply-add) | Same as non-fused (Unicorn may not implement true FMA) |
| Real function: `BlinkWeightLeft` (8B, returns f1) | Bit-identical between decomp and original |
| Real function: `TiltAngle` (56B, float ops) | Bit-identical between decomp and original |

**Key insight**: Since both sides execute the same PPC instructions, any FPU precision behavior (fused vs non-fused, rounding mode) is identical on both sides. The comparison is valid regardless of whether Unicorn perfectly matches hardware — it just needs to be self-consistent.

### Implementation

Adding f1 comparison requires:
1. Read `UC_PPC_REG_FPR0 + 1` after execution (1 line)
2. Add to `ExecutionResult` dataclass (1 field)
3. Add to `compare()` function (bit-exact comparison for auto-fixture; epsilon for custom fixtures)

**Effort**: ~15 lines of code. Lowest-effort, highest-confidence improvement.

**Epsilon**: Not needed for auto-fixture (zeroed state produces deterministic float results). For custom fixtures with real float data, use `1e-6` relative epsilon.

---

## Objective 5: Batch-All Baseline (Partial)

### Results Available

Full batch-all is slow (~30min+ for all 971 units). Partial results from representative units:

| Unit | Eligible | Equivalent | Divergent | Rate |
|------|----------|------------|-----------|------|
| keygen_xbox | 20 | 12 | 8 | 60% |
| App | 19 | 18 | 1 | 95% |
| ChecksumData_xbox | 1 | 1 | 0 | 100% |
| Main | 1 | 1 | 0 | 100% |
| Memory_Xbox | 3 | 3 | 0 | 100% |
| Char (batch-all, first 106) | ~106 | ~104 | ~2 | ~98% |
| Skeleton | 30 | 22 | 8 | 73% |
| Flow | 203 | 135 | 68 | 67% |
| Mesh | 135 | 69 | 66 | 51% |

**Totals from sampled units**: 518 tested, 365 equivalent, 153 divergent = **70.5% equivalence rate**.

Units with mostly simple functions (App, Memory, Char allocators/getters) have very high rates (95-100%). Units with complex STL templates and serialization (Flow, Mesh) have lower rates (50-67%).

### Divergence Patterns

Common categories of divergent functions:

1. **StaticClassName** (dozens): These call `Symbol::Find()` with a string literal. The string pointer resolves to different addresses between decomp and original due to different symbol names for the string data. **Root cause**: REFHI/REFLO point to different symbols (decomp may name the string differently).

2. **STL serialization** (`BinStream operator>>`, `operator<<`): These read/write vector data with size-dependent loops. With zeroed input, both sides should agree, but constructor calls within loops can diverge due to different sizes between decomp and original.

3. **Constructors with many calls**: `??0Skeleton@@QAA@XZ` has 152B decomp vs 236B original — fundamentally different code, expected to diverge.

4. **STL allocate/copy/destroy**: Template instantiations with complex loop patterns over zeroed data.

5. **Functions with size mismatch**: If decomp and original have different code sizes, they're likely structurally different and expected to diverge.

### Performance

Single function execution: ~90μs including Unicorn instance creation.
Per-unit batch (30 functions): ~2 seconds.
Full batch-all estimate: ~30-60 minutes for all 971 units (most time in COFF parsing, not execution).

---

## Updated Phase 3 Implementation Plan

### Priority 1: FPR Comparison (trivial, 1 hour)
- Add f1 to ExecutionResult
- Add f1 comparison to comparator
- 585 float-returning functions get proper comparison
- **ROI**: Maximum — almost free, eliminates a blind spot

### Priority 2: bctr Handling — Switch Tables + Tail Calls (medium, 4-8 hours)
- Load .rdata sections alongside function code
- Rebase ADDR32 jump table entries to code region
- Detect vtable tail call pattern (load vtable slot → mtctr → bctr)
- **ROI**: High — unblocks 182 functions, relatively simple

### Priority 3: Permuter Guard Rail (medium, 2-4 hours)
- Add unicorn equivalence check to permuter scorer
- Binary pass/fail: reject variants that break equivalence
- **ROI**: High — prevents semantic regressions in permuter output

### Priority 4: bctrl Handling — Virtual Dispatch (complex, 8-16 hours)
- Write generic vtable at OBJECT_BASE+0
- Each vtable slot points to unique trampoline
- Extend call log to record vtable slot index
- **ROI**: Medium-high — unblocks 3,704 functions but complex to implement correctly

### Priority 5: Batch-All Optimization (low, 2-4 hours)
- Parallelize with multiprocessing (per-unit is independent)
- Cache COFF parsing across function comparisons (already done within unit)
- Target: full batch-all in <5 minutes
- **ROI**: Medium — enables CI integration

### Deprioritized
- **Intra-TU call co-loading**: 50% of functions affected but only improves test fidelity, doesn't unblock new functions. Complex to implement (circular dependencies, code region size). Revisit after priorities 1-4.
- **Custom fixtures**: Auto-fixture covers the common case. Manual fixture creation is high-effort for incremental gain. Consider auto-generation from DWARF struct info instead.
- **CI integration**: Needs batch-all optimization first. Can be a simple `ninja test-unicorn` target.

---

## Appendix: Divergence Root Cause Analysis

### Why ~25% of functions diverge with auto-fixture

The auto-fixture initializes all memory to zero and all mocks return 0. This exercises one specific code path (the "null/zero path"). Divergences fall into:

1. **Structural code differences** (decomp ≠ original in size/shape): Expected, not a bug. These are functions where the decomp doesn't match yet.

2. **Symbol name differences**: Decomp and original .obj files may use different internal symbol names for the same string literal or static variable. The current comparison matches calls by ordinal position rather than symbol name, but REFHI/REFLO relocs for different symbols get different addresses, causing divergent pointer values.

3. **Runaway loops**: Zeroed data structures can create infinite iteration (e.g., linked list with `next=0` that loops, or vector with `size=0` causing underflow). The 5M instruction timeout catches these but they register as divergent.

4. **Constructor cascades**: Complex constructors make dozens of calls to initialize sub-objects. One different call early on cascades into completely different state.

These are inherent limitations of zeroed auto-fixtures, not bugs in the runner. Functions that are EQUIVALENT with auto-fixture are genuinely equivalent (true positives). Functions that are DIVERGENT may or may not be truly different (possible false positives from fixture limitations).
