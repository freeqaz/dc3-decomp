# ContentLoadingPanel Analysis Session (2026-03-06)

## Functions Analyzed

| Function | Match% | Blockers |
|----------|--------|----------|
| ShowIfPossible | 87.2% | Boolean materialization pattern |
| Poll | 84.8% | GPR vs FPR literal pool caching, prologue mismatch |
| 12 other functions | 100% | Complete |

## Pattern 1: Boolean Materialization (`subfc/eqv/srwi/addze/clrlwi.`)

### What we found

The target compiles `mContentCount > 1` as a **boolean value** (0 or 1) using a 5-instruction branchless sequence, then tests it with `beq`:

```asm
lwz  r10, 0x40, r3         ; r10 = mContentCount
li   r11, 0x1              ; r11 = 1
subfc r9, r10, r11         ; r9 = 1 - mContentCount, CA = (mContentCount <= 1 unsigned)
eqv  r11, r10, r11         ; r11 = XNOR(mContentCount, 1)
srwi r11, r11, 31          ; r11 = sign bit of XNOR result
addze r11, r11             ; r11 += carry
clrlwi. r11, r11, 31      ; r11 &= 1, set cr0
beq  skip                  ; branch if result == 0
```

Our compiler generates a simple 2-instruction branch-based comparison:

```asm
lwz   r11, 0x40, r3       ; r11 = mContentCount
cmpwi cr6, r11, 0x1       ; compare with 1
ble   cr6, skip            ; branch if <= 1
```

### Tracing the boolean materialization

To identify the comparison type, trace through several input values:

| mContentCount | subfc CA | eqv result | srwi | addze | clrlwi & 1 | Body entered? |
|---------------|----------|------------|------|-------|------------|---------------|
| -1 | 0 | 0x00000001 | 0 | 0 | 0 | No |
| 0 | 1 | 0xFFFFFFFE | 1 | 2 | 0 | No |
| 1 | 1 | 0xFFFFFFFF | 1 | 2 | 0 | No |
| 2 | 0 | 0xFFFFFFFC | 1 | 1 | 1 | Yes |
| 3 | 0 | 0xFFFFFFFD | 1 | 1 | 1 | Yes |

Result: This is **signed `> 1`** (equivalently `>= 2`). Values <= 1 and negative values all skip.

### What we tried (all produced 87.2%)

| Source expression | Generated code | Why it didn't match |
|-------------------|----------------|---------------------|
| `mContentCount != 1` | `cmpwi; beq` | Wrong semantics AND wrong codegen |
| `mContentCount > 1` | `cmpwi; ble` | Correct semantics, branch-based |
| `mContentCount >= 2` | `cmpwi; blt` | Correct semantics, branch-based |
| `(unsigned)mContentCount > 1` | `cmplwi; ble` | Correct semantics, branch-based |
| Nested ifs | Same as `&&` | Compiler optimizes identically |
| Single `&&` chain | Same | Compiler optimizes identically |
| `int var = expr; if (var)` | Materializes but moves short-circuit | 85.5% (worse) |
| `bool var = expr; if (var)` | Same as int | 85.5% (worse) |

### Root cause

The MSVC PPC compiler has two strategies for comparison in `&&` chains:
1. **Branch-based**: `cmpwi + bXX` (2 instructions) -- what our compiler always chooses
2. **Boolean materialization**: `subfc/eqv/srwi/addze/clrlwi.` (5 instructions) -- what the target uses

This is a compiler-internal optimization heuristic. No source-level change we tried could force materialization while preserving the short-circuit on `mAllowedToShow`.

When we extracted to a variable (`int/bool x = expr; if (condition && x)`), the compiler DID materialize the boolean, but reordered the checks -- it moved the materialization before the short-circuit guard, adding instructions in the wrong place.

### Verdict: Unfixable from source

The boolean materialization pattern is a compiler optimization choice. The semantics are correct (`> 1`), but the codegen strategy cannot be controlled.

### Potential permuter rule: `boolean_materialize`

A permuter pattern could detect this by looking for:
- Target has `subfc + eqv + srwi + addze` sequence (4+ consecutive instructions)
- Decomp has `cmpwi/cmplwi + bXX` at the same logical position (2 instructions)
- These appear in the context of `&&` or `||` chains

**Classification**: Mark as **unfixable compiler pattern** (like volatile regswaps). Don't waste permuter time trying alternatives. The diff should be:
- 5 deletes (the subfc/eqv/srwi/addze/clrlwi. sequence)
- 2 replaces (the load register and branch instruction)
- Total: 7 mismatched instructions

**Detection heuristic for scan**:
```
if target has consecutive [subfc, eqv, srwi, addze] opcodes
   AND base has [cmpwi|cmplwi] at similar position:
   → flag as "boolean_materialize" (unfixable)
```

## Pattern 2: GPR Literal Pool Base vs FPR Value Caching

### What we found

In `Poll()`, the target keeps the **address** of the 100.0f float literal in a callee-saved GPR (r29), loading the value from memory each time it's needed:

```asm
; Prologue: saves r29, r30, r31 (3 GPRs) + f30, f31 (2 FPRs)
bl   __savegprlr_29

; First use: load 100.0f via r29
lis  r29, lbl_821217E8      ; r29 = high bits of literal address
lfs  f0, lbl_821217E8, r29  ; f0 = 100.0f (volatile)

; ... function calls clobber f0 ...

; Later use: reload 100.0f via r29 (still in callee-saved GPR)
lfs  f1, lbl_821217E8, r29  ; f1 = 100.0f (reloaded from memory)
```

Our compiler caches the **value** of 100.0f in a callee-saved FPR (f30):

```asm
; Prologue: saves r30, r31 (2 GPRs) + f29, f30, f31 (3 FPRs)
; (no __savegprlr helper -- not enough GPRs)

; First use: load 100.0f into callee-saved f30
lis  r10, __real@42c80000
lfs  f30, __real@42c80000, r10  ; f30 = 100.0f (callee-saved)

; Later use: copy from f30 (still live)
fmr  f1, f30                    ; f1 = 100.0f (no memory reload)
```

### Impact

This single allocation decision cascades into:
- **Prologue mismatch**: Target uses `__savegprlr_29` (3 GPRs), decomp uses manual saves (2 GPRs + 1 extra FPR)
- **8 insert/delete** instructions in prologue/epilogue
- **f29 <-> f30 swap** across the function (3 instructions): currentFrame gets f30 in target, f29 in decomp
- **3 replace** instructions: `bl __savegprlr_29` vs `stw`, `lfs` vs `fmr`, `b __restgprlr_29` vs `ld`
- **4 offset shifts** (+8 bytes on stack for the extra FPR save)
- Total: ~21 mismatched instructions from one allocation choice

### When this happens

The compiler must choose between:
- **GPR strategy**: Use a callee-saved GPR to hold the literal pool base address. Requires memory reload for each use but saves an FPR slot.
- **FPR strategy**: Use a callee-saved FPR to hold the float value directly. Avoids reloads but costs an FPR slot.

The GPR strategy is chosen when:
- The float constant is used multiple times across function calls (volatile FPRs clobbered)
- GPR pressure is low enough to spare one for the literal base
- The function already saves enough GPRs to benefit from `__savegprlr_NN` helper

### What we tried (all produced 84.8%)

| Approach | Result | Why |
|----------|--------|-----|
| `(int)` casts on mContentCount | No change | Types already int |
| Ternary for final 100.0f | 76.1% (worse) | Changed control flow |
| Declaration reorder (target before currentFrame) | No change | Compiler ignores |
| Permuter (all 50 patterns) | No change | Exhaustive search found nothing |

### Verdict: Unfixable from source

The literal pool base caching strategy is a compiler-internal register allocation decision. The number and type of callee-saved registers (GPR vs FPR) is determined by the register allocator, not by source structure.

### Potential permuter rule: `literal_pool_gpr_cache`

Detect by looking for:
- Target uses `__savegprlr_NN` but decomp uses manual saves
- Target has `lis rN, <literal>` with callee-saved GPR, decomp has volatile GPR
- Same literal address used in multiple `lfs/lfd` instructions via the callee-saved GPR

**Classification**: Mark as **unfixable prologue mismatch**. When detected, the permuter should:
1. Calculate the "noise budget" from this pattern (typically 15-25 instructions)
2. Subtract from the total mismatch count
3. Report the effective match% excluding this pattern

**Detection heuristic**:
```
if target calls __savegprlr_NN AND base does not:
   count GPR saves in target vs base
   if target_gprs > base_gprs AND base_fprs > target_fprs:
      → flag as "literal_pool_gpr_cache" (unfixable prologue mismatch)
```

## Key Takeaway: Semantic Fix vs Codegen Match

Even though neither function reached 100%, the session produced a **real semantic fix**: changing `mContentCount != 1` to `mContentCount > 1`. The original source comment said "theres an extra check here" suggesting uncertainty. By tracing the target's boolean materialization sequence through multiple input values, we confirmed the actual comparison is `> 1` (signed), not `!= 1`.

This matters for:
- **Native port correctness**: The loading panel now correctly only shows for 2+ content items
- **Understanding the codebase**: The threshold is intentional (don't show loading UI for single items)

The methodology of **tracing PPC instruction sequences through concrete values** to identify comparison semantics is broadly applicable. The `subfc/eqv/srwi/addze` pattern specifically encodes signed `>` comparisons and appears elsewhere in the codebase.

## Appendix: Detecting `subfc/eqv/srwi/addze` Comparison Type

The sequence computes `a > b` (signed) as a boolean:

```
li    rB, <constant>         ; rB = b
subfc rX, rA, rB             ; CA = (a <= b unsigned)
eqv   rY, rA, rB             ; rY = XNOR(a, b)
srwi  rY, rY, 31             ; rY = (sign bits match)
addze rY, rY                 ; rY += carry
clrlwi. rY, rY, 31          ; rY &= 1
```

To decode: the constant in `li` is the comparison threshold. The result is `a > threshold` (signed). Verify by tracing threshold, threshold+1, 0, and -1.
