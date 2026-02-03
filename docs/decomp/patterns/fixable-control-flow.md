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

## See Also

- [fixable-comparison.md](fixable-comparison.md) - Conditional expression patterns
- [harmful-avoid.md](harmful-avoid.md) - Loop patterns that make things worse
