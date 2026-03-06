# Permuter Pattern Gap Analysis

**Date**: 2026-03-05
**Context**: Analysis of HEAD~3 changes vs existing permuter patterns, identifying high-ROI rules to implement

## Current State

48 pattern files (~13K LOC), 6 Ghidra integration modules (~1.2K LOC). Ghidra-guided infrastructure exists for `and_split`, `early_return_merge`, `fma_reorder`, and `declaration_reorder`.

## Tier 1: High ROI, Easy to Implement

### 1. Guard-to-Nested-If Transform (MISSING)

**Estimated impact**: ~15-30 functions
**Effort**: ~2 hours

Neither `and_split` nor `early_return_merge` handles multi-guard-to-nested conversion:

```cpp
// Source (guard returns):
if (!mClips) return;
if (!mBones) return;
if (mTestClip && TheLoadMgr.EditMode()) { ... }

// Target (deeply nested):
if (mClips) { if (mBones) { if (mTestClip) { if (TheLoadMgr.EditMode()) { ... } } } }
```

`early_return_merge` merges guards into `||` chains. `and_split` splits single `&&` expressions. Neither converts N consecutive guard-returns into N-deep nesting.

Ghidra guidance is straightforward: if `extract_control_flow_skeleton()` shows deeply nested `if` blocks without guard returns, apply the nesting direction.

### 2. Varargs Cast Insertion (MISSING)

**Estimated impact**: ~10-15 functions
**Effort**: ~30 min

No pattern tries inserting `(char *)` on Symbol/FilePath arguments to printf-style functions:

```cpp
MILO_NOTIFY("... %s", (char *)Name());
```

Implementation: find `call_expression` nodes where the function name matches `MILO_NOTIFY`/`MILO_WARN`/`MILO_FAIL`/`MILO_ASSERT`, identify `%s` format arguments, wrap each corresponding arg in `(char *)`. Low search space.

### 3. Bool to Unsigned Char Type Change (MISSING)

**Estimated impact**: ~5-10 functions
**Effort**: ~30 min

```cpp
// bool skipOverride = false;  ->  unsigned char skipOverride;
// if (...) skipOverride = true;  ->  if (...) skipOverride = 1;
```

Find `bool` local declarations, change type to `unsigned char`, swap `true`->`1`, `false`->`0`. Affects comparison codegen (`cmpw` vs `cmplwi`).

## Tier 2: Medium ROI, Moderate Effort

### 4. General Statement Reorder (PARTIALLY EXISTS)

**Estimated impact**: ~5-10 functions
**Effort**: ~2 hours

`assignment_reorder.py` handles consecutive same-target assignments. HEAD~3 shows broader need: reordering independent statements of any type (e.g., moving `w = 0.0f;` after MILO_FAIL calls). Requires dependency analysis to verify no statement reads a variable written by another.

### 5. Math Function Cast Wrapping (MISSING)

**Estimated impact**: ~5 functions
**Effort**: ~30 min

For `ceil`/`floor`/`sqrt`/`acos` calls, try wrapping in `(float)` cast:

```cpp
int frameIdx = (int)(float)ceil(frame);
float acosDeg = (float)std::acos(clamped) * RAD2DEG;
```

`signed_unsigned` only handles comparison operands, not math return values.

### 6. And-Split with Else (GAP IN EXISTING)

**Estimated impact**: ~5-10 functions
**Effort**: ~1 hour

`and_split._split_and_conditions()` explicitly skips `if (a && b) { body } else { alt }` (line 182: "too complex"). HEAD~3 shows this case matters. Restructure as `if (a) { if (b) { body } else { alt } } else { alt }`.

## Tier 3: High ROI via Ghidra, Harder

### 7. Wire Control Flow Skeleton into Patterns

**Estimated impact**: ~20-30 functions (improved guidance)
**Effort**: ~1 hour

`extract_control_flow_skeleton()` in `ghidra_ast.py` is implemented but **no pattern uses it** (confirmed in implementation doc). Patterns use `extract_condition_structure()` instead, which only detects conjunction/disjunction/guard_return tags. The skeleton gives richer signal for guard-to-nested transforms and statement reordering.

### 8. Ghidra-Guided Null Check Detection

**Estimated impact**: ~10-20 functions (improved precision)
**Effort**: ~1 hour

`null_guard_elimination.py` operates blind. Ghidra integration would check whether the target actually has the null check, eliminating false positives.

### 9. Ghidra-Guided Variable Extraction/Elimination

**Estimated impact**: ~10-20 functions
**Effort**: ~2 hours

`variable_extraction.py` and `temp_elimination.py` operate blind. Cross-referencing Ghidra's local variable count (via `extract_variable_first_use_order`) against ours could determine which direction to try.

## Composability Note

Many functions require multiple patterns applied together. CharLipSyncDriver::Poll needed 14 changes across 6 categories. The blind combinatorial explosion makes this intractable. See `2026-03-05-constraint-directed-synthesis.md` for the constraint-solving approach to composability.

## Priority Ranking Summary

| Priority | Pattern | Functions | Effort | ROI |
|----------|---------|-----------|--------|-----|
| 1 | Guard-to-nested-if | 15-30 | 2h | Very High |
| 2 | Varargs cast insertion | 10-15 | 30min | High |
| 3 | Bool->unsigned char | 5-10 | 30min | High |
| 4 | Wire CF skeleton to patterns | 20-30 | 1h | High |
| 5 | Ghidra null check guidance | 10-20 | 1h | Medium |
| 6 | General statement reorder | 5-10 | 2h | Medium |
| 7 | Math function cast wrap | ~5 | 30min | Low |
| 8 | And-split with else | 5-10 | 1h | Medium |
| 9 | Ghidra var extraction guidance | 10-20 | 2h | Medium |
