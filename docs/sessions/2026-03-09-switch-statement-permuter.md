# Switch Statement Permuter Rule Research

**Date:** 2026-03-09
**Focus:** If/else chain to switch statement conversion as a permuter transformation

---

## Discovery: HamNavList::DrawDebug Case Study

`HamNavList::DrawDebug` contained a loop dispatching on an integer index variable `i` across 5 cases. The original decompiled source used an if/else-if chain:

```cpp
for (unsigned int i = 0; i < 5; i++) {
    sprintf_s(buf, "");
    if (i == 0) {
        sprintf_s(buf, "Hand height %f", mHandHeight);
    } else if (i == 1) {
        sprintf_s(buf, "ListState SelectedDisplay: %d", mListState.SelectedDisplay());
    } else if (i < 3) {
        sprintf_s(buf, "ListState FirstShowing: %d", mListState.FirstShowing());
    } else if (i == 3) {
        sprintf_s(buf, "ListState Selected: %d", mListState.Selected());
    } else {
        sprintf_s(buf, "Num selectable items: %d", NumItems());
    }
    Vector2 pos(startX, startY + i * lineStep);
    TheRnd.DrawStringScreen(buf, pos, sTextColor, true);
}
```

Converting this to a switch statement (along with a for-to-do/while loop change) improved the match from approximately 55% to 85%:

```cpp
unsigned int i = 0;
do {
    sprintf_s(buf, "");
    switch (i) {
    case 0:
        sprintf_s(buf, "Hand height %f", mHandHeight);
        break;
    case 1:
        sprintf_s(buf, "ListState SelectedDisplay: %d", mListState.SelectedDisplay());
        break;
    case 2:
        sprintf_s(buf, "ListState FirstShowing: %d", mListState.FirstShowing());
        break;
    case 3:
        sprintf_s(buf, "ListState Selected: %d", mListState.Selected());
        break;
    case 4:
        sprintf_s(buf, "Num selectable items: %d", NumItems());
        break;
    }
    Vector2 pos(startX, startY + i * lineStep);
    TheRnd.DrawStringScreen(buf, pos, sTextColor, true);
    i++;
} while ((int)i < 5);
```

Current match: 88.2% (remaining gap is register swaps and address relocation noise).

### Prior Art in this Codebase

This is not the first time this pattern has been observed:

| Function | Direction | Effect | Session |
|----------|-----------|--------|---------|
| HamNavList::DrawDebug | if/else -> switch | +30% | 2026-03-09 |
| TexBlender cases | if/else -> switch | match | 2026-03-06 |
| BustAMovePanel::OnBeat VO chains | if/else -> switch | +0.6% | 2026-02-06 |
| HttpReqCurl::Start | switch -> if/else | +1.3% (to 100%) | 2026-01-24 |

Both directions are valuable -- sometimes switch matches the target, sometimes if/else does.

---

## Technical Analysis: Why Switch Produces Different Codegen

### MSVC PPC Switch Compilation Strategy

MSVC for Xbox 360 (PowerPC) uses several strategies for switch statements depending on case density and count:

1. **Jump table** (dense, many cases): Builds a table in `.rdata` of absolute 32-bit addresses, indexes via `slwi` + `lwzx` + `mtctr` + `bctr`. Used for large dense switches like `BustAMovePanel::OnBeat` (10 cases). DC3 uses `lwzx`-based 32-bit absolute address tables (confirmed by Ghidra analysis -- see `docs/sessions/2026-02-09-ghidra-msvc-switch-detection.md`).

2. **Binary search tree** (sparse or small case count): Emits cascading `cmpwi` + conditional branches that bisect the case space. For 5 cases on values 0-4, this produces a balanced tree structure comparing against the midpoint first, then recursing into halves.

3. **Sequential compare** (2-3 cases): Simple linear `cmpwi` + `beq` chain, similar to if/else but with different register allocation behavior.

### If/Else Chain Compilation

An if/else-if chain generates strictly sequential evaluation:

```
    cmpwi   r30, 0x0        # i == 0?
    bne     .L_case1
    ... case 0 body ...
    b       .L_end
.L_case1:
    cmpwi   r30, 0x1        # i == 1?
    bne     .L_case2
    ... case 1 body ...
    b       .L_end
.L_case2:
    cmpwi   r30, 0x2        # i == 2?  (or i < 3)
    bne     .L_case3
    ...
```

This is O(n) in the worst case: each comparison is emitted sequentially, and the variable is tested top-to-bottom.

### Switch Binary Search Tree

A switch on the same values generates a binary search tree:

```
    cmpwi   r30, 0x2        # i vs midpoint (2)
    beq     .L_case2
    bgt     .L_upper         # cases 3, 4
    cmpwi   r30, 0x0        # cases 0, 1
    beq     .L_case0
    b       .L_case1         # must be 1
.L_upper:
    cmpwi   r30, 0x3
    beq     .L_case3
    b       .L_case4         # must be 4
```

This is O(log n), but more importantly, the **branch structure is fundamentally different**: the first comparison is against the midpoint, not against 0. This produces different branch targets, different fallthrough paths, and different register pressure profiles.

### Key Codegen Differences

| Aspect | if/else chain | switch statement |
|--------|--------------|------------------|
| First comparison | Against first constant (0) | Against midpoint or pivot |
| Branch structure | Linear fallthrough | Tree-shaped |
| Branch hint bits | Uniform | Compiler may add `+`/`-` hints |
| Register allocation | Sequential re-test same var | Single var in register, tree dispatch |
| Fallthrough | Into next else-if | Into default or nearest case |
| Case reachability | Compiler must prove unreachable | Compiler knows case space from declaration |

The binary search tree structure is particularly important because it changes **which basic blocks are fallthrough vs branch targets**, which in turn affects:
- Branch prediction hints (`beq+` vs `beq-`)
- Register spill decisions (different live ranges)
- Instruction scheduling (different instruction reordering windows)

---

## Existing Implementation: `switch_if_convert.py`

An implementation already exists at `scripts/permuter/patterns/switch_if_convert.py` but is **not registered** in `scripts/permuter/patterns/__init__.py`. It handles both directions:

### Direction 1: Switch -> If/Else (lines 57-132)

Walks tree-sitter AST for `switch_statement` nodes, extracts case values and bodies, generates an equivalent if/else-if chain. Strips `break;` from case bodies and formats with proper indentation.

### Direction 2: If/Else -> Switch (lines 135-179)

Uses `_collect_if_chain()` to find consecutive if/else-if nodes comparing the same variable against constants using `==`. Requires at least 3 branches. Generates a switch with `case:` labels and a `default:` for the final `else`.

### Current Limitations

1. **Only handles `==` comparisons**: The `_collect_if_chain` function (line 264) rejects any operator other than `==`. The DrawDebug case originally used `i < 3` for one branch -- this would prevent detection.

2. **Not registered**: The pattern is not imported in `__init__.py`, so it never runs in default batch sweeps or hill climbing sessions.

3. **No `<` / `<=` inference**: Cannot infer that `i < 3` (with `i == 0` and `i == 1` already handled) means `i == 2` in the switch translation.

4. **No loop structure conversion**: The DrawDebug fix also required changing `for` to `do/while`. The switch conversion alone may not be sufficient for some cases.

5. **Case body extraction is line-based**: Uses byte-level line extraction rather than proper AST subtree replacement. This works for simple cases but may mangle complex multi-statement bodies with nested braces.

6. **Minimum 3 branches**: Requires `len(chain) >= 3`. Two-branch if/else conversions are skipped, though those are less likely to benefit from switch conversion anyway.

7. **No cast handling**: Does not handle `(unsigned int)i == 0` patterns that sometimes appear in decomp code.

---

## Detection Heuristic for Candidate If/Else Chains

### Core Pattern

An if/else chain is a candidate for switch conversion when:

1. **Same variable tested throughout**: All conditions in the chain compare the same identifier (or expression) against constants.
2. **Integer-typed constants**: The comparison values are integer literals, enum values, or character literals.
3. **Sequential if/else-if structure**: Not independent `if` statements -- must use `else if` chaining.
4. **At least 3 branches**: Below 3, the codegen difference is negligible.

### Extended Detection (for higher-value conversions)

Beyond the basic `var == const` pattern, the heuristic should detect:

#### Mixed comparison operators
```cpp
if (i == 0) { ... }
else if (i == 1) { ... }
else if (i < 3) { ... }     // equivalent to case 2 if 0,1 already handled
else if (i == 3) { ... }
else { ... }                  // default / case 4
```

The `i < 3` branch, in context of prior `i == 0` and `i == 1` checks, is semantically equivalent to `i == 2`. A smart heuristic can infer the case value from the inequality and the set of already-handled values.

#### Reverse direction (switch -> if/else)
Detection is straightforward: any `switch_statement` node in the function AST. Already implemented.

#### Enum-typed variables
```cpp
if (state == kBlendNear) { ... }
else if (state == kBlendFar) { ... }
else if (state == kBlendCustom) { ... }
```

The constants may be enum identifiers rather than numeric literals. The conversion should preserve the original constant names.

### Relevance Signals from objdiff

The existing implementation uses branch opcode mismatches as a relevance signal. Additional signals that suggest switch/if conversion would help:

- **Multiple `cmpwi` mismatches** against the same set of constants (different ordering = tree vs sequential)
- **Insert/delete clusters at branch boundaries** (different block layout)
- **2+ clusters** with matched bodies between them (bodies are the same, only dispatch differs)
- **`bgt` / `blt` in target** where `beq` / `bne` in base (tree comparison vs equality test)

---

## Proposed Permuter Rule Implementation Approach

### Phase 1: Register and Enable the Existing Pattern

The immediate improvement is to **add `switch_if_convert` to `__init__.py`** and let it participate in standard hill climbing sweeps. The existing implementation handles the most common case (equality comparisons) and both directions.

Add to `scripts/permuter/patterns/__init__.py`:
```python
from . import switch_if_convert  # noqa: F401
```

### Phase 2: Extend `_collect_if_chain` for Inequality Inference

Modify `_collect_if_chain` to accept `<`, `<=`, `>`, `>=` comparisons and infer the case value:

```python
# Pseudocode for inequality inference
handled_values = set()  # values already matched by == in prior branches

if op == "==":
    case_value = const_value
    handled_values.add(const_value)
elif op == "<":
    # If all values below const are handled except one, infer that one
    candidates = set(range(min_known, const_value)) - handled_values
    if len(candidates) == 1:
        case_value = candidates.pop()
    else:
        return None  # ambiguous
```

This requires tracking the set of handled values as the chain is walked and solving for the remaining possibilities. The approach is conservative -- it only infers a value when exactly one possibility remains.

### Phase 3: Add to Composer Follow-Up Map

Add bidirectional connections in `_FOLLOW_UP_MAP` in `composer.py`:

```python
"switch_if_convert": ["branch_polarity", "declaration_reorder"],
"branch_polarity": [..., "switch_if_convert"],
```

This allows the pattern to be composed with branch polarity (since switch changes branch structure) and declaration reorder (since switch may change register allocation).

### Phase 4: Paired Loop Structure Conversion (Optional)

The DrawDebug case required both switch conversion AND for-to-do/while conversion. A more advanced version could:

1. Detect `for (type i = 0; i < N; i++)` loops containing if/else chains on `i`
2. Generate variants with both: switch + do/while, switch + for, if/else + do/while

This would be a separate pattern (`loop_switch_convert`) or an extension of the existing one.

---

## Edge Cases and Limitations

### Cases Where Switch is Worse

- **HttpReqCurl::Start** went from 98.7% to 100% by converting switch TO if/else. The bidirectional nature is essential.
- Functions where the original code used if/else (confirmed by RB3 reference or Ghidra) should stay as if/else.

### Fallthrough Semantics

Switch statements support fallthrough (no `break`), which if/else does not. When converting if/else -> switch:
- Each branch body should get an explicit `break;`
- If two if-branches have identical bodies, they could be collapsed to adjacent `case` labels (fallthrough)

The current implementation always adds `break;`.

### Side Effects in Conditions

If/else conditions can have side effects (`if (i++ == 0)`). Switch conditions are evaluated once. The pattern should reject chains where the comparison variable is modified within the condition expression.

### Non-Contiguous Constants

Switch works with any integer constants, not just contiguous ranges. However, MSVC's choice between jump table and binary search tree depends on density. For sparse constants (e.g., 0, 10, 100, 1000), the compiler may generate a binary search tree for switch, which could match or differ from if/else depending on the target.

### Default Case Handling

The `else` clause at the end of an if/else chain maps to `default:` in a switch. If there is no final `else`, the switch has no `default:` -- this is valid C++ but changes codegen (the compiler knows all cases are covered vs potentially falling through).

### Cast Expressions in Conditions

Decomp code sometimes has `(unsigned int)i == 0` or `(int)i == N` casts in conditions. The AST representation wraps the variable in a `cast_expression` node. The pattern should unwrap these to find the underlying variable for chain detection.

### Nested if/else in Switch Cases

Converting switch -> if/else when case bodies contain their own if/else creates nested structures that may cause further codegen differences. The transformation should be applied non-recursively to only the outermost switch/if-else.

---

## Code Examples: Full Transformation

### Example 1: Basic equality chain (3 cases + default)

**Before (if/else):**
```cpp
if (state == kBlendNear) {
    nearList.push_back(ctrl);
} else if (state == kBlendFar) {
    farList.push_back(ctrl);
} else if (state == kBlendCustom) {
    customList.push_back(ctrl);
}
```

**After (switch):**
```cpp
switch (state) {
case kBlendNear:
    nearList.push_back(ctrl);
    break;
case kBlendFar:
    farList.push_back(ctrl);
    break;
case kBlendCustom:
    customList.push_back(ctrl);
    break;
}
```

### Example 2: Mixed operators with inequality inference

**Before (if/else with `<`):**
```cpp
if (i == 0) {
    doA();
} else if (i == 1) {
    doB();
} else if (i < 3) {    // only i==2 remaining
    doC();
} else if (i == 3) {
    doD();
} else {
    doE();
}
```

**After (switch with inferred case):**
```cpp
switch (i) {
case 0: doA(); break;
case 1: doB(); break;
case 2: doC(); break;  // inferred from i < 3
case 3: doD(); break;
default: doE(); break;
}
```

### Example 3: Reverse direction (switch -> if/else)

**Before (switch):**
```cpp
switch (mType) {
case 0: return "none";
case 1: return "offer";
case 2: return "bundle";
default: return "";
}
```

**After (if/else):**
```cpp
if (mType == 0) {
    return "none";
} else if (mType == 1) {
    return "offer";
} else if (mType == 2) {
    return "bundle";
} else {
    return "";
}
```

---

## Priority and ROI Assessment

### Expected Impact

Based on observed cases:
- **High impact** (+10-30%): Functions where the target used switch but our code has if/else (or vice versa) for 4+ cases. The DrawDebug case showed +30%.
- **Medium impact** (+0.5-5%): Functions with 3 cases where the branch structure is the primary remaining difference.
- **Low impact** (0%): Functions where both forms produce identical codegen (2 cases, very simple bodies).

### Frequency Estimate

A rough scan for candidate patterns:
- Functions with 3+ branch if/else-if chains on a single integer variable: likely 50-100 across the codebase
- Functions with small switch statements (3-6 cases): likely 200+ across the codebase
- Combined candidates for bidirectional conversion: ~250-300 functions

### Estimated ROI

Given that the pattern is already implemented but not registered, the highest-ROI action is simply enabling it. The pattern will be tested automatically during hill climbing, and the bidirectional nature means it can only help (if switch is worse, the original if/else remains).

---

## Summary of Recommendations

1. **Immediate**: Register `switch_if_convert` in `__init__.py` (1 line change)
2. **Short-term**: Handle `<`/`<=`/`>`/`>=` comparisons via inequality inference in `_collect_if_chain`
3. **Short-term**: Add `switch_if_convert` to the follow-up map in `composer.py`
4. **Medium-term**: Handle cast expressions in conditions (`(unsigned int)i == 0`)
5. **Low priority**: Paired loop + switch conversion for the for-to-do/while + if-to-switch combo pattern

---

## Related Sessions and Documentation

- [2026-03-06 TexBlender Vtable Overload](2026-03-06-texblender-vtable-overload.md) -- Pattern 4: Switch vs If/Else-If for Enums
- [2026-02-09 Ghidra MSVC Switch Detection](2026-02-09-ghidra-msvc-switch-detection.md) -- How MSVC generates jump tables for large switches
- [2026-02-06 BustAMovePanel OnBeat](2026-02-06-bustamove-onbeat-status.md) -- VO if/else chains converted to switch (+0.6%)
- [fixable-control-flow.md](../decomp/patterns/fixable-control-flow.md) -- General control flow patterns

## Related Source Files

- `scripts/permuter/patterns/switch_if_convert.py` -- Existing (unregistered) implementation
- `scripts/permuter/patterns/__init__.py` -- Pattern registry
- `scripts/permuter/patterns/base.py` -- Pattern base class
- `scripts/permuter/composer.py` -- Follow-up map for pattern composition
- `src/system/hamobj/HamNavList.cpp` -- DrawDebug case study (lines 1509-1561)
