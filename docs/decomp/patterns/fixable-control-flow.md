# Fixable Patterns: Control Flow

Patterns related to conditionals, loops, and branch structures.

---

## Explicit Conditional vs Max()

**Impact:** +35%
**Success Rate:** HIGH
**Time:** 5 minutes

Replace `Max()` macro/function with explicit `if` statement.

### Symptom

objdiff shows different branch structure or function call where inline code expected.

### Why It Works

The `Max()` macro may expand differently, or the compiler may inline it differently than an explicit branch.

### Fix

```cpp
// Before
i1 = Max(i1, 1);

// After
if (i1 < 1) {
    i1 = 1;
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| RndFlare::SetSteps | 64.8% | 100% | +35.2% | `Max(i1, 1)` to `if (i1 < 1) i1 = 1` |
| ClosestPoint | 50.7% | 100% | +49.3% | Nested if-else with implicit fallthrough to early-return |

---

## Ternary vs If-Else

**Impact:** +5-10%
**Success Rate:** 75%
**Time:** 10 minutes

Ternary operators often match better than if-else for simple conditionals.

### Symptom

objdiff shows extra branches in simple conditional assignments.

### Fix

```cpp
// Before - generates extra branches
bool ret;
if (progress) {
    ret = progress->IsEraComplete();
} else {
    ret = false;
}

// After - cleaner codegen
bool ret = progress ? progress->IsEraComplete() : false;
```

### When to Use

- Simple boolean or value selection
- Single expression result
- No side effects in branches

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| DxRnd::DrawSafeArea | 24.98% | 98.8% | +73.8% | Ternary to if/else for targetAspect |
| BufStream::Eof | 98.0% | 100% | +2.0% | Ternary with operand order swap |

---

## Loop Structure

**Impact:** Variable
**Success Rate:** MEDIUM
**Time:** 10 minutes

Different loop forms generate different code.

### Symptom

objdiff shows different loop setup/termination code.

### Fix

Try different loop forms:

```cpp
// Form 1 - standard for
for (int i = 0; i < n; i++) { ... }

// Form 2 - while
int i = 0;
while (i < n) { ...; i++; }

// Form 3 - for with external init
for (; i < n; i++) { ... }

// Form 4 - for with Symbol init (affects register allocation)
for (Symbol s = sym; a2->FindArray(s)->...;) {
    s = ...;
}
```

### Real Example

```cpp
// Before (97.5% match) - modifies parameter directly
while (a2->FindArray(sym)->...) {
    sym = ...;
}

// After (100% match) - uses local variable in for loop
for (Symbol s = sym; a2->FindArray(s)->...;) {
    s = ...;
}
```

---

## Sequential If vs If-Else

**Impact:** Variable
**Success Rate:** MEDIUM
**Time:** 5 minutes

Use sequential `if` with `return` instead of `if-else` chains.

### Symptom

objdiff shows branch structure differences in early return patterns.

### Fix

```cpp
// Before
if (x) { y; } else if (z) { ... }

// After
if (x) { y; return; }
if (z) { ... }
```

### Real Example

```cpp
// CORRECT (99.7% match) - threshold check outside HasSong block
if (thresh >= prereqNum) {
    return true;
}
if (HasSong(sym)) {
    // other logic
}

// BROKEN (99.3% match) - moved threshold check inside
if (HasSong(sym)) {
    if (thresh >= prereqNum) {
        return true;
    }
    // other logic
}
```

### Important

Even logically equivalent control flow changes can break matches. The compiler generates different branch structures.

---

## Nested If to Combined Condition

**Impact:** +1-3%
**Success Rate:** HIGH

Combining nested `if` statements into a single `&&` condition can fix cmpwi/cmplwi mismatches.

### Symptom

objdiff shows cmpwi vs cmplwi differences inside nested null checks.

### Fix

```cpp
// Before - generates cmpwi/cmplwi mismatch
if (mMultiMesh) {
    if (mesh) {
        mesh->Draw();
    }
}

// After - single combined condition
if (mMultiMesh && mMultiMesh->Mesh()) {
    mMultiMesh->Mesh()->Draw();
}
```

### Real Example

Refactoring from nested if to `&&` eliminated comparison instruction mismatches.

---

## Split && into Nested If

**Impact:** +5-18%
**Success Rate:** HIGH
**Time:** 5 minutes

The inverse of the above pattern. Split a combined `&&` condition into nested `if` statements when the binary checks each condition as a separate branch point.

### Symptom

objdiff shows the target has two separate `beq`/`bne` branches for a null check and a size check, but our code combines them with `&&` into a single branch sequence. The combined form generates different branch targets and register allocation.

### Why It Works

`if (obj && arr->Size() != 0)` evaluates both conditions in sequence with short-circuit, but the compiler may generate a single combined branch block. Separating into `if (obj) { if (arr->Size() == 0) ... else ... }` gives the compiler two explicit branch points that match the target's control flow.

### Fix

```cpp
// Before (77.9%) — combined &&, single branch sequence
if (obj && arr3->Size() != 0) {
    // loop body
} else {
    GetOrAddSinks()->AddSink(obj, gNullStr);
}

// After (96.3%) — nested ifs, separate branch points
if (obj) {
    if (arr3->Size() == 0) {
        AddSink(obj, s1, s2, kHandle, true);
    } else {
        // loop body
    }
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| Object::OnAddSink | 77.9% | 96.3% | +18.4% | Split null+size check into nested ifs |

### When to Use

- Target shows two separate `cmpwi`/`beq` sequences for conditions that your code combines with `&&`
- The `else` body differs between the two conditions (null case vs empty case)
- Each condition leads to a different code path

---

## Goto-Based Loop for Deferred Assignment

**Impact:** +2-3%
**Success Rate:** MEDIUM
**Time:** 10 minutes

Use `goto` to exit an inner loop early, deferring a boolean assignment to after the loop, when the target compiler doesn't initialize the boolean before the loop.

### Symptom

objdiff shows an extra `li rN, 0x0` (boolean initialization) before an inner search loop that the target doesn't have. The target sets the "not found" value only after the loop completes normally.

### Why It Works

A `break`-based search loop requires initializing the result boolean before the loop (`bool found = false`), which generates an `li` instruction. The target may instead use a `goto` pattern where `found = true` is set only on the matching path, and `found = false` only on the fallthrough after the loop completes. This defers the initialization, saving one instruction.

### Fix

```cpp
// Before (96.0%) — break-based, initializes before loop
for (;;) {
    bool anyDecompressing = false;  // generates li r9, 0x0
    for (int i = 2; i >= 0; i--) {
        if (mBuffersState[i] == kDecompressing) {
            anyDecompressing = true;
            break;
        }
    }
    if (!anyDecompressing) break;
    gDataProcessedEvt.Wait(-1);
}

// After (98.4%) — goto-based, defers false assignment
for (;;) {
    bool anyDecompressing;
    int i = 2;
    BufferState *statePtr = &mBuffersState[2];
    do {
        if (*statePtr == kDecompressing) {
            anyDecompressing = true;
            goto check_decomp;
        }
        i--;
        statePtr--;
    } while (i >= 0);
    anyDecompressing = false;  // set AFTER loop
check_decomp:
    if (!anyDecompressing) break;
    gDataProcessedEvt.Wait(-1);
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| ChunkStream::~ChunkStream | 96.0% | 98.4% | +2.4% | Deferred bool init to after inner loop |

---

## Control Flow Must Match Exactly

**Impact:** Can break matches
**Success Rate:** N/A - this is a warning
**Time:** N/A

Even logically equivalent restructuring breaks matches.

### Symptom

Match percentage drops after "refactoring" that preserves logic.

### Why

The compiler generates different branch structures even when the logic is equivalent. Moving a conditional inside another block changes instruction ordering.

### Examples of Breaking Changes

```cpp
// Moving an early return into a nested block
// Changing if-else-if to switch
// Combining multiple conditions into one
// Splitting one condition into multiple
```

### Rule

**Match the exact control flow structure of the original code.**

---

## Single Return for Branch Direction

**Impact:** +6%
**Success Rate:** HIGH
**Time:** 5 minutes

Pre-initialize the result before an if-block so both paths share a single `return` statement. This makes the compiler generate `beq` (skip body → fall through to return) instead of `bne` (enter body from early-return path).

### Symptom

objdiff shows CONTROL_FLOW mismatch: target has `beq` but base generates `bne` at the same branch point. Typically appears with `||` conditions that guard early returns.

### Why It Works

An `||` condition with two `return` statements (one inside, one after the if-block) generates two `bne` branches to the body. A single return with a pre-initialized result makes the compiler generate `beq` to skip the body and fall through to the shared return.

### Fix

```cpp
// Before (82.6%) - two return paths, generates bne
if (obj != 0 || mListMode != kObjListNoNull) {
    // ... body ...
    return result;
}
return fallback;

// After (88.6%) - single return, generates beq
auto result = fallback;
if (obj != 0 || mListMode != kObjListNoNull) {
    // ... body ...
    result = computed_value;
}
return result;
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| ObjPtrVec::insert | 82.6% | 88.6% | +6.0% | `beq` vs `bne` at branch index 12 |

---

## Branch Polarity Steering (beq/bne, ble/bge)

**Impact:** +0.1-1.0% (often via lower diff score, not always visible in rounded match%)
**Success Rate:** MEDIUM
**Time:** 10-20 minutes

When you only have a few stubborn `diff_op` mismatches (`beq` vs `bne`, `ble` vs `bge`), keep logic the same but steer compare polarity and branch shape.

### Symptom

- objdiff shows 1-2 `diff_op` mismatches in otherwise stable regions
- Most remaining mismatches are register/offset noise, but these `diff_op` entries keep reappearing

### Why It Works

PowerPC branch direction is tightly coupled to compare operand order and which path is fallthrough. Equivalent source code can generate:
- `cmpw a, b` + `ble`
- or `cmpw b, a` + `bge`

Likewise, whether the compiler emits `beq` or `bne` is sensitive to whether work is in the `if` body or the `else` body.

### Strategies

1. **Swap compare viewpoint without changing semantics**
```cpp
// Before
if (score0 > score1) winner = 0;

// Try
if (score1 < score0) winner = 0;
```

2. **Invert condition and swap branch bodies**
```cpp
// Before
if (cond) {
    do_work();
}

// Try
if (!cond) {
    // empty / cheap path
} else {
    do_work();
}
```

3. **Use score-gate shaping for multi-rating blocks**
- Keep shared scoring in one block
- Feed it via a local `score` value or a guarded `goto` block (if the file already uses this style)
- This often moves `beq/bne` placement without changing behavior

4. **Change one branch shape at a time**
- Do not mix branch edits with declaration/static-order edits in the same attempt
- Evaluate by `diff_op` count and `diff_score`, not just rounded match%

### Real Example

`BustAMovePanel::OnBeat`:
- Baseline: 95.3%, 2 control-flow inversions
- After branch-shape rewrite in final-sequence scoring: 95.3%, 1 inversion, lower diff score

Takeaway: even when match% does not move, reducing `diff_op` is still progress toward a cleaner final state.

---

## Multiple Early Returns to || Chain

**Impact:** +15-40%
**Success Rate:** HIGH
**Time:** 5 minutes

Combine multiple independent `if (cond) return false;` statements into a single `||` chain. This eliminates redundant bool materialization and branch target duplication.

### Symptom

objdiff shows many extra instructions with repeated patterns of `fneg`, `fmadds`, `fcmpu`, `blt`, `li r3, 0x0`, `blr` — the compiler is generating a full inline comparison + return sequence for each early return.

### Fix

```cpp
// Before (48.0% match) — 6 separate early returns, each generates full comparison+branch sequence
if (s < f.front)
    return false;
if (s < f.back)
    return false;
if (s < f.left)
    return false;
if (s < f.right)
    return false;
if (s < f.top)
    return false;
if (s < f.bottom)
    return false;
return true;

// After (87.9% match) — single || chain, shared branch target
if (s < f.front || s < f.back || s < f.left || s < f.right || s < f.top || s < f.bottom)
    return false;
return true;
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| operator>(Sphere, Frustum) | 48.0% | 87.9% | +39.9% | 6 inline `operator<` calls combined. Remaining 12% gap is register swaps from target hoisting `fneg` invariant |

### When to Use

- Multiple independent guard conditions that all return the same value
- Each condition calls the same or similar inline functions
- No computation between the conditions

---

## Bool Return Expression (if/return → return &&)

**Impact:** +5-15%
**Success Rate:** HIGH
**Time:** 5 minutes

Convert `if (cond) return false; return true;` to `return !cond;` using `&&` or `||` expressions. This changes how the compiler materializes the boolean return value.

### Symptom

objdiff shows register swap mismatches (r11 vs r3) around `li` + `clrlwi` instructions on the return path, or extra branches to separate `li r3, 0` / `li r3, 1` blocks.

### Fix

```cpp
// Before (85.5% match) — separate branches for true/false
if (ApiCall() != 0 || result == 0) {
    return false;
}
return true;

// After (100% match) — single return expression
return ApiCall() == 0 && result != 0;
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| HasKinectSharePrvilege | 85.5% | 100% | +14.5% | `if/return false/return true` → `return A == 0 && B != 0` |

### Note

The `&&` form generates branchless bool materialization that matches the target's `neg/andc/srwi` pattern. The `if/else return` form generates separate `li r3, 0` / `li r3, 1` branches with different register assignment.

---

## Bitwise & for Bool Accumulator

**Impact:** +10-15%
**Success Rate:** HIGH
**Time:** 5 minutes

Use bitwise `&` instead of logical `&&` for accumulating boolean values in a loop. The compiler generates `and` (bitwise) vs short-circuit branches for `&&`.

### Symptom

objdiff shows `and` instruction in target but branches in our build for boolean accumulation across loop iterations.

### Fix

```cpp
// Before (79.4% match) — logical &&, generates branches
allRestricted = allRestricted && userIsRestricted;

// After (94.0% match) — bitwise &, generates and instruction
allRestricted = allRestricted & userIsRestricted;
```

### When to Use

- Boolean accumulation across loop iterations (`allTrue &= check;`)
- Both operands are already bool (0 or 1)
- No short-circuit side effects needed

---

## Local Bool Extraction for Complex Conditions

**Impact:** +5-8%
**Success Rate:** HIGH
**Time:** 5 minutes

Extract a complex condition into a local `bool` variable before using it in an `if` statement. This forces bool materialization matching the target's `clrlwi` truncation pattern.

### Symptom

objdiff shows `delete` of `clrlwi` or different branch structure around complex `||`/`&&` conditions.

### Fix

```cpp
// Before (86.2% match) — condition inline in if statement
if ((mTargetShowing > mFirstShowing && i2 == 0)
    || (mTargetShowing < mFirstShowing && i2 == -1)) {
    // ...
}

// After (93.4% match) — extracted to local bool
bool shouldCheck = (mTargetShowing > mFirstShowing && i2 == 0)
    || (mTargetShowing < mFirstShowing && i2 == -1);
if (shouldCheck) {
    // ...
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| ShouldHoldDisplayInPlace | 86.2% | 93.4% | +7.2% | Extracted `||` condition to local bool |
| SetSongAndDefaults | 97.8% | 98.2% | +0.4% | Extracted `mode ==` check to local bool |

### When to Use

- Complex `||`/`&&` chains used as if-condition
- Target shows `clrlwi 24` (bool truncation) that our build lacks
- Result is used in one place (if extracting helps, inline `bool()` cast may also work)

---

## Branchless Bool Return to If/Else

**Impact:** +4-5%
**Success Rate:** MEDIUM
**Time:** 5 minutes

When the compiler generates branchless `subi+cntlzw+extrwi` for a direct comparison return (`return state == 2`), restructure into an explicit if/else to force compare-and-branch codegen.

### Symptom

objdiff shows `subi`, `cntlzw`, `extrwi` (or `rlwinm` with rotate) in the target where our code has `cmpwi` + `beq` + `li`. The branchless sequence converts an integer comparison to 0/1 arithmetically instead of via a branch.

### Why It Works

`return expr == value` generates a branchless idiom: subtract, count leading zeros, extract bit. Converting to `if (expr != value) return false; return true;` forces the compiler to use compare-and-branch, matching the target when the target also uses branching (or vice versa).

### Fix

```cpp
// Before (88.6% match) — branchless subi+cntlzw+extrwi
bool NetLoaderRef::IsDownloading() {
    MILO_ASSERT(IsValid(), 0x321);
    if (mCacheLoader) {
        return (int)mCacheLoader->mState == 2;
    }
    return true;
}

// After (93.0% match) — compare-and-branch
bool NetLoaderRef::IsDownloading() {
    MILO_ASSERT(IsValid(), 0x321);
    if (!mCacheLoader || (int)mCacheLoader->mState == 2)
        return true;
    return false;
}
```

### Real Examples

| Function | Before | After | Delta | Notes |
|----------|--------|-------|-------|-------|
| NetLoaderRef::IsDownloading | 88.6% | 93.0% | +4.4% | Remaining gap is LINKER_MERGED + BOOL_MASK + reloc noise |

### When to Use

- Direct comparison return (`return x == N`)
- objdiff shows `subi`/`cntlzw`/`extrwi` sequence vs branches
- Try both directions: if target is branchless but ours branches, try direct return; if target branches but ours is branchless, try if/else

---

## See Also

- [fixable-comparison.md](fixable-comparison.md) - Conditional expression patterns
- [harmful-avoid.md](harmful-avoid.md) - Loop patterns that make things worse
- [unfixable-compiler.md](unfixable-compiler.md#when-unfixable-may-still-move) - Triage hard 95%+ functions
