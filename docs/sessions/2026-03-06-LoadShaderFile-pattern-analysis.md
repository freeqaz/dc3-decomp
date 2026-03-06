# Session: DxShaderMgr::LoadShaderFile — Pattern Analysis

Date: 2026-03-06

## Summary

Decompiled `DxShaderMgr::LoadShaderFile` from 84.1% to 89.6%. The function loads
pre-compiled Xbox 360 shaders from a binary cache file, registering pixel/vertex
shader pairs with the GPU via `XGRegisterPixelShader`/`XGRegisterVertexShader`.

## Key Transformation: `if (k - 1)` — Arithmetic-as-Boolean Condition

The most impactful fix was changing `if (k != 0)` to `if (k - 1)`. This generates
completely different codegen on MSVC PPC:

| Source | Assembly |
|--------|----------|
| `if (k != 0)` | `cmpwi cr6, r28, 0` + `beq/bne` |
| `if (k - 1)` | `subi r8, r28, 1` + `cntlzw r6, r8` + `extrwi. r7, r6, 1, 26` + `beq` |

The `subi + cntlzw + extrwi.` sequence is MSVC's way of converting a subtraction
result to a boolean: count leading zeros of (k-1), then extract bit 5 to check if
the count was 32 (meaning k-1 was zero, i.e., k==1).

### Semantics

For a loop `for (int k = 0; k < 2; k++)`, k is in {0, 1}:
- `k - 1`: when k=0, result=-1 (truthy); when k=1, result=0 (falsy)
- Equivalent to `k == 0` for this domain

### Why This Matters

No existing permuter pattern covers this transformation:
- `branch_polarity` — swaps if/else bodies, doesn't change condition expression form
- `comparison_equivalence` — changes `< N` to `<= N-1`, doesn't remove comparison operators
- `signed_unsigned` — wraps in casts, swaps `!= 0` <-> `> 0`
- `bool_cast` — wraps in `bool()`, doesn't use arithmetic subtraction

## Proposed New Pattern: `condition_arithmetic`

### Transformations

**Always safe (any integer x):**
1. `if (x != 0)` <-> `if (x)` — implicit boolean
2. `if (x == 0)` <-> `if (!x)` — implicit boolean negated

**Safe in bounded loops (x in {0, ..., N}):**
3. `if (x != N)` <-> `if (x - N)` — subtract-then-test
4. `if (x == N)` <-> `if (!(x - N))` — subtract-then-test negated

### Detection Signals

The pattern is relevant when the diagnosis shows:
- `extrwi` / `rlwinm` in diff_ops (target uses bit-extract for boolean test)
- `cntlzw` in replace instructions
- `subi` + `cntlzw` cluster in target-only instructions
- `cmpwi`/`cmplwi` in base that doesn't appear in target (comparison removed)

### Implementation Sketch

```python
class ConditionArithmeticPattern(Pattern):
    name = "condition_arithmetic"

    def relevant(self, diagnosis):
        # Look for extrwi/cntlzw/rlwinm in diff_ops or replaces
        for d in diagnosis.diff_ops:
            if d.target_opcode in ("rlwinm", "extrwi", "cntlzw"):
                return True
        for r in diagnosis.replaces:
            if "cntlzw" in r.target_text or "extrwi" in r.target_text:
                return True
        return False

    def generate(self, ctx):
        for stmt in ctx.statements:
            for if_node in find_if_else_and_if_only(stmt):
                condition = get_condition_expr(if_node)
                # Transform: if (x != 0) -> if (x)
                # Transform: if (x == 0) -> if (!x)
                # Transform: if (x != N) -> if (x - N) [when in bounded for loop]
                # Transform: if (x == N) -> if (!(x - N))
                # Also reverse: if (x) -> if (x != 0), if (x - N) -> if (x != N)
```

### Scope & Safety

Forms 1 and 2 are always semantically safe and should be tried broadly. They
only change *how* the compiler tests zero, not *what* is tested.

Forms 3 and 4 change the expression value (from boolean to arbitrary int). They
are safe ONLY when the subtraction result is used purely as a boolean (in an
if-condition). The permuter's hill-climbing naturally validates correctness via
objdiff scoring, so even if a semantically-unsafe variant is generated, it will
be discarded if the match% doesn't improve.

## Other Changes Made

| Change | Effect |
|--------|--------|
| Separate `pixelShaders[2]`/`vertexShaders[2]` -> unified `bases[4]` | Fixed array layout to match target |
| k=0 -> pixel shader, k=1 -> vertex shader | Fixed swapped if/else logic |
| Both XGRegister calls take `bases[k+2] + ibc` | Was incorrectly passing 0 for vertex shader |
| `unsigned int` for `num` and `alloc` | Generates `cmplwi + beq` instead of `cmpwi + ble` |
| `pPS` declared before `pVS` | Fixed one register swap pair |

## Remaining Gaps (89.6%)

| Issue | Instructions | Fixability |
|-------|-------------|------------|
| r29<->r30 register swap | 15 | Unlikely — `physAddr` variable forces extra callee-saved |
| Other register swaps | 19 | Unlikely — callee-saved allocation |
| Allocation loop `stwx` vs `stwu` | ~10 | Compiler chose pointer-increment optimization |
| MakeString template size | 2 calls | Unfixable — different string literal sizes |
| Address relocations | 10 | Unfixable noise |
