# Systemic Header Fixes: Mining AT_LIMIT for Hidden Patterns

**Priority**: Medium (speculative but huge payoff per find)
**Status**: Planned

## Context

Every systemic fix found so far has fixed 3-25+ functions at once:
- DataNode(float/double) constructor: 25+ functions, +1% overall
- ShaderMgr.h vtable reorder: 11 functions (NgPostProc, NgSpotlightDrawer, NgMat)
- StandingStillGestureFilter unk44: 3 functions to 100%
- TexRenderer::Load 2 real bugs: 68.7% -> 99.6%
- AmbientOcclusion sort templates: 14 VectorSort templates to 100%

4,403 AT_LIMIT functions is a large pool. There may be patterns hiding in the mismatch data that we haven't spotted yet.

## Approach

### 1. Cluster Analysis on Mismatch Signatures

Group AT_LIMIT functions by their mismatch fingerprint (instruction sequence at divergence point). Functions with identical mismatch signatures likely share a common root cause.

```bash
# Extract mismatch signatures for all AT_LIMIT functions
python3 scripts/analysis/function_health.py --unit "*" --top 4403 --format json \
  | jq 'group_by(.mismatch_signature) | sort_by(-length) | .[0:20]'
```

Large clusters (10+ functions with same signature) are prime candidates for systemic fixes.

### 2. Header-Driven Mismatch Hunting

For each large cluster:
1. Check if all functions share a common header include
2. Look at the divergent instruction sequence — does it correspond to an inlined header function?
3. Compare the header function body against RB3 reference (`/rb3-pair`)
4. Try the fix in a worktree, measure impact across all affected functions

Common header suspects not yet fully audited:
- `ObjPtr_p.h` template bodies (operator+, iterator methods)
- `DataArray.h` inline accessors
- `Symbol.h` construction/comparison
- `String.h` / `PoolString.h` construction
- `Key.h` / `PropKeys.h` template specializations

### 3. Vtable Order Audit

The ShaderMgr vtable reorder fix proved that MSVC PPC can reverse virtual method order relative to declaration order. Other classes with multiple overloaded virtuals may have the same issue.

Scan for classes where:
- Multiple virtual functions share a name (overloaded)
- AT_LIMIT functions in that class call those virtuals
- The mismatch is at a vtable dispatch site (lwz from vtable offset)

Tools: `/vtable` skill dumps actual slot layout from COFF .obj files.

### 4. Struct Layout Verification

Use `/ghidra-struct` and `/rb2-class` to compare struct layouts against ground truth. A single wrong member offset can cascade through all methods of a class.

Priority targets: classes with 5+ AT_LIMIT methods, especially if the mismatches are at member access instructions (lwz/stw with specific offsets).

### 5. Cross-Unit Pattern Detection

Some AT_LIMIT functions may share a pattern that only becomes visible when comparing across units. For example:
- Same mismatch in `FooA::Load` and `FooB::Load` might indicate a shared base class issue
- Consistent +4/-4 offset errors across unrelated classes might indicate a type size disagreement

## Expected Yield

Highly variable. Could find nothing new, or could find another DataNode-class fix worth 20+ functions. Historical hit rate: ~1 systemic fix per 2-3 dedicated analysis sessions.

## Key Tools

- `scripts/analysis/function_health.py` — unified diagnostic
- `scripts/analysis/regswap_classify.py` — mismatch classification
- `/ghidra-struct` — struct layout comparison
- `/vtable` — vtable slot dumping
- `/rb3-pair` — RB3 reference code
- `/rb2-class` — RB2 DWARF ground truth layouts
- `mcp__orchestrator__query_functions` — find functions by match range
- `mcp__orchestrator__run_diff_inspect` — deep mismatch analysis
