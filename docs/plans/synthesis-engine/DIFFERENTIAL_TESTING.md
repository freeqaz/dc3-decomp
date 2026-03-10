# Differential Testing Framework

## Concept

The fastest way to understand c2.dll's codegen decisions WITHOUT decompiling it:
compile carefully crafted test cases with `/FAcs`, extract per-function assembly,
and diff the output across source variations.

This is the "black box" approach — treat the compiler as an oracle and systematically
probe its behavior with controlled inputs.

## Architecture

```
Test Case (C++ source variants)
    |
    v
Compiler (/FAcs)
    |
    v
Assembly Extractor (parse .asm listing)
    |
    v
Function Isolator (extract per-function asm)
    |
    v
Differ (structural diff, not textual)
    |
    v
Decision Map (source pattern -> asm pattern)
```

## Test Suites

### 1. Register Allocation Order

**Question**: How does declaration order affect callee-saved register assignment?

```cpp
// Variant A
void func() {
    int x = get_x();  // -> r31?
    int y = get_y();  // -> r30?
    use(x, y);
}

// Variant B
void func() {
    int y = get_y();  // -> r31?
    int x = get_x();  // -> r30?
    use(x, y);
}
```

Vary: declaration order, type (int/float/ptr), number of variables, function calls
between declarations, scope nesting.

**Expected output**: `{n_callee_saved_vars: N, assignment_order: "first_decl_highest"}`

### 2. BSF Graph Coloring Threshold

**Question**: At what point does the allocator switch from linear scan to graph coloring?

```cpp
// Vary N from 1 to 15
void func() {
    int v1 = get(1); int v2 = get(2); ... int vN = get(N);
    use(v1, v2, ..., vN);
}
```

Observe when register assignment order stops being strictly first-decl-highest.

### 3. Inlining Threshold

**Question**: What's the instruction count threshold for "too big to inline"?

```cpp
// Callee with N statements
inline int callee() {
    int x = 1;
    x += 2;  // repeat N times
    ...
    return x;
}

void caller() {
    int r = callee();  // will this inline?
}
```

Vary N, observe when callee stops being inlined (visible in caller's assembly).

### 4. Peephole Patterns

**Question**: What source patterns trigger specific PPC peepholes?

```cpp
// NOR trigger
unsigned char x = get();
unsigned char result = x ^ 0xFF;     // -> NOR?
unsigned int result2 = (unsigned int)x ^ 0xFF;  // -> xori?

// Boolean materialization
bool a = get_a();
bool b = get_b() > 1;
bool r1 = a && b;           // -> branch
bool r2 = a && (bool)b;     // -> branchless (subfc/eqv/srwi)
bool r3 = a & b;            // -> branchless (and.)
```

### 5. Branch Polarity

**Question**: When does the compiler emit beq vs bne for if/else?

```cpp
// Variant A: if/else
if (x == 0) { do_a(); } else { do_b(); }

// Variant B: negated
if (x != 0) { do_b(); } else { do_a(); }

// Variant C: early return
if (x == 0) { do_a(); return; }
do_b();
```

### 6. Float Precision

**Question**: When does DOUBLETOSINGLE fire?

```cpp
float x = 0.001;   // double literal assigned to float
float y = 0.001f;  // float literal
static const float z = 0.001f;  // static const
```

## Output Format

Each test produces a decision record:

```json
{
    "test": "regalloc_order",
    "variant": "A",
    "source_hash": "abc123",
    "functions": {
        "func": {
            "prologue": "__savegprlr_28",
            "callee_saved": ["r31", "r30", "r29", "r28"],
            "instructions": 42,
            "assembly_hash": "def456"
        }
    },
    "diff_vs_baseline": {
        "changed_registers": {"r31": "r30", "r30": "r31"},
        "changed_instructions": 0,
        "structural_match": true
    }
}
```

## Implementation

Tool: `msvc-src/tools/diff_test.py`

```
python3 msvc-src/tools/diff_test.py --suite regalloc --variants 10
python3 msvc-src/tools/diff_test.py --suite inline_threshold --range 5-50
python3 msvc-src/tools/diff_test.py --suite peephole --pattern nor
```

Uses the same wibo + cl.exe toolchain as the main build. Compiles standalone
test files (no PCH needed) to isolate specific codegen decisions.

## Integration with Synthesis Engine

The decision maps feed directly into the compiler model:

1. **Permuter constraint**: "Don't try reordering these declarations — the regalloc
   test shows it won't change the register assignment for <5 variables"
2. **Guided mutation**: "The inline threshold test shows inlining stops at 47 instructions.
   This function is 49 — try splitting it to get under the threshold"
3. **Pattern library**: "The peephole test confirms `(bool)(x > 1)` triggers branchless
   materialization. Apply this transformation wherever we see the branch pattern"
