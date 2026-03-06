# New Permuter Patterns — Implementation Session

**Date:** 2026-03-05
**Context:** Gap analysis identified 3 high-ROI missing patterns from HEAD~3 commit analysis. These are the fastest wins: ~3h combined for 30-55 functions.

## Prior Work (This Session)

Ghidra-guided permuter Phase 2 completed:
- Control flow guided transforms (`and_split`, `early_return_merge`)
- Ghidra+ASM crossref for declaration reorder
- Preflight unfixable detection (`ghidra_preflight.py`)

## Pattern 1: Guard-to-Nested-If Transform

**Impact:** 15-30 functions | **Effort:** ~2h | **Priority:** #1

### Problem

Neither `and_split` nor `early_return_merge` handles converting N consecutive guard-returns into N-deep nesting:

```cpp
// Source (guard returns):
if (!mClips) return;
if (!mBones) return;
if (mTestClip && TheLoadMgr.EditMode()) { ... }

// Target (deeply nested):
if (mClips) {
    if (mBones) {
        if (mTestClip) {
            if (TheLoadMgr.EditMode()) { ... }
        }
    }
}
```

`early_return_merge` merges guards into `||` chains. `and_split` splits single `&&` expressions. Neither converts N consecutive guard-returns into N-deep nesting — this is a different structural transform.

### Approach

New pattern file: `scripts/permuter/patterns/guard_to_nested.py`

1. Find consecutive `if (!cond) return [val];` statements followed by a body
2. Generate nested-if version: negate each condition, wrap body in N levels of nesting
3. Also generate reverse: detect N-deep nesting with single-statement bodies, flatten to guard returns
4. Ghidra guidance: use `extract_condition_structure()` tags — if Ghidra shows `nested_if` and source has guards, only generate nesting direction (and vice versa)

Key difference from `and_split`: this handles **multiple consecutive guards** becoming **multiple nesting levels**, not just a single `&&` becoming two levels.

### Edge cases
- Guards with else clauses: `if (!cond) return; else { ... }` — append else to innermost nesting level
- Mixed return values: `if (!a) return; if (!b) return false;` — can't merge (different return values)
- Guards followed by non-guard code before the body — only process the initial consecutive run

## Pattern 2: Varargs Cast Insertion

**Impact:** 10-15 functions | **Effort:** ~30min | **Priority:** #2

### Problem

No pattern tries inserting `(char *)` on Symbol/FilePath arguments to printf-style functions:

```cpp
// Source:
MILO_NOTIFY("Keyframes in %s are out of order.", Name());

// Target:
MILO_NOTIFY("Keyframes in %s are out of order.", (char *)Name());
```

### Approach

New pattern file: `scripts/permuter/patterns/varargs_cast.py`

1. Find `call_expression` nodes where function name matches `MILO_NOTIFY`/`MILO_WARN`/`MILO_FAIL`/`MILO_ASSERT`/`TheDebug.Notify`/`TheDebug.Fail`
2. Parse the format string (first arg) to find `%s` positions
3. For each corresponding argument, if it's not already cast, generate variant with `(char *)` wrapper
4. Also try `(String &)` for FilePath-typed args

Small search space — typically 1-3 variants per function.

### Detection
- `relevant()`: check if diagnosis has any mismatches near `bl` instructions to MILO/debug functions
- Or just: check if source contains MILO_NOTIFY/MILO_WARN/MILO_FAIL calls with non-cast arguments

## Pattern 3: Bool to Unsigned Char Type Change

**Impact:** 5-10 functions | **Effort:** ~30min | **Priority:** #3

### Problem

```cpp
// Source:
bool skipOverride = false;
if (...) skipOverride = true;

// Target:
unsigned char skipOverride;
skipOverride = 0;
if (...) skipOverride = 1;
```

`bool` vs `unsigned char` affects comparison codegen (`cmpw` vs `cmplwi`).

### Approach

New pattern file: `scripts/permuter/patterns/bool_to_uchar.py`

1. Find `bool` local variable declarations
2. Generate variant: change type to `unsigned char`
3. Replace `true` → `1`, `false` → `0` in all assignments to that variable within the function
4. If declaration has initializer `= false` → change to uninitialized + separate `= 0` statement

Small, mechanical transform. 1 variant per bool variable.

## Implementation Order

1. Guard-to-nested-if (largest impact, most complex)
2. Varargs cast insertion (quick, mechanical)
3. Bool to unsigned char (quick, mechanical)

## Integration

All three patterns need:
- Registration in `scripts/permuter/patterns/__init__.py`
- `relevant()` and `priority()` methods for diagnosis-based filtering
- Composition compatibility with existing patterns

## Relationship to Constraint-Directed Synthesis

These patterns are additive — they work with the existing hill climber and will also work as "free variable generators" in the future constraint-directed system. The constraint system resolves deterministic choices; these patterns handle the remaining search space.
