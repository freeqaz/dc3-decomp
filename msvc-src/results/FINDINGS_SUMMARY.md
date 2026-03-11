# Differential Testing Results — MSVC PPC Compiler

Date: 2026-03-10
Compiler: MSVC 16.00.11886.00 (Xbox 360 PPC cross-compiler)
Toolchain: wibo + cl.exe + /FAcs /Ox /GS-

## 1. Register Allocation Order

**Finding: Strictly first-declared → highest register for ALL tested cases.**

Variable count scaling (all callee-saved GPR, cross-call live ranges):
| N vars | Callee-saved | Prologue | Order |
|--------|-------------|----------|-------|
| 2 | r31 | (manual save) | Linear |
| 3 | r31, r30 | (manual save) | Linear |
| 4 | r31, r30, r29 | __savegprlr_29 | Linear |
| 5 | r31..r27 | __savegprlr_27 | Linear |
| 6 | r31..r26 | __savegprlr_26 | Linear |
| 7 | r31..r25 | __savegprlr_25 | Linear |
| 8 | r31..r24 | __savegprlr_24 | Linear |
| 9 | r31..r23 | __savegprlr_23 | Linear |
| 10 | r31..r22 | __savegprlr_22 | Linear |

**Declaration order swap**: Swapping `int a = get(0); int b = get(1);` to
`int b = get(1); int a = get(0);` changes which variable gets r31.

**Prologue helper threshold**: `__savegprlr_N` kicks in at N=4+ callee-saved GPRs
(below 4, compiler uses manual stw/std pairs).

## 2. BSF Graph Coloring Threshold

**Finding: Linear order holds through N=15 in the simple test pattern.**

ALL tested cases (3-15 variables, each surviving across a function call)
maintained strict first-declared=highest assignment. Graph coloring (BSF)
was NOT triggered in this test pattern.

**Advanced tests also maintain linear order:**
- Virtual function calls with 8+ variables: linear
- Loop bodies with conditional variable usage: linear (but loop control takes r31/r30)
- Conditional assignments across branches: linear

**Important nuance**: "Linear" means first-ALLOCATED-to-callee-saved gets highest.
Compiler-generated temporaries (loop counters, `this` pointers, vtable lookups)
also consume callee-saved registers and shift user variables down. In
`loop_conditional`, user variable `a` gets r29 (not r31) because loop control
variables consume r31/r30 first.

**Implication**: BSF may not trigger in this compiler version at all for
typical code patterns, or it requires specific conditions we haven't found.
The "~7 variable" regswap pattern we see in DC3 may be caused by:
1. Compiler temporaries consuming different callee-saved registers than expected
2. Different optimization passes reordering variable allocation
3. Virtual call thunk handling affecting register allocation order

This means declaration reorder is ALWAYS the right approach for regswap fixes
(not graph coloring interference) — the question is just which variable the
compiler considers "first."

## 3. Inlining Threshold

**Finding: The inliner uses a weighted cost model, NOT PPC instruction count.**

### Precise boundary measurements (with proper inlining detection):
| Test Type | Max Inlined | Min NOT Inlined | PPC insns at boundary |
|-----------|-------------|-----------------|----------------------|
| Arithmetic chain (add/mul) | N=39 (40 insns) | N=40 (41 insns) | 40 |
| If/else chain (branch-heavy) | N=5 (24 insns) | N=6 (29 insns) | 26 |

### Cost model:
The dramatic difference (40 arithmetic vs 5 branches) proves a **weighted cost model**:
- Arithmetic operations ≈ weight 1
- Branch operations ≈ weight 8
- Effective threshold ≈ **40 cost units**

Note: MSVC always emits COMDAT function bodies even when inlined. Detection
requires checking for `bl callee` in caller, not the existence of callee symbol.

### `inline` keyword:
Same threshold as no keyword. `inline` does NOT raise the limit with /Ox.

### `__forceinline`:
**Always inlines, no size limit.** Tested up to 50 function calls (still inlined).

### Key insight for DC3 decomp:
Functions NOT marked `__forceinline` but called once will be automatically
inlined if under the threshold. Adding/removing function body content in
headers can push neighboring functions over/under the inline threshold,
causing cascading codegen changes (explains "header-driven regressions").

## 4. Peephole Patterns (G5_SPECIAL)

### NOR peephole
| Source Pattern | Generated Code | Notes |
|---------------|---------------|-------|
| `u8 ^ 0xFF` | `clrlwi; not; clrlwi` | Uses `not` (=`nor rA,rS,rS`) with mask |
| `u32 ^ 0xFF` | `xori r3,r3,255` | Direct XOR immediate |
| `u8 widened, then ^ 0xFF` | `clrlwi; xori r3,r11,255` | XOR after widening |
| `~u8` | `clrlwi; not; clrlwi` | Same as `^ 0xFF` |

**Key**: `not` IS `nor rA,rS,rS` — confirmed by PPC ISA. The NOR peephole
fires when the operand is byte-width (u8), using complement instead of XOR.
Widening to u32 first prevents the NOR peephole.

### Boolean Materialization

**Decision tree: signedness + comparison operator + comparison constant all affect selection.**

### Category 1: Zero Tests (2 instructions)
| Source | PPC | Notes |
|--------|-----|-------|
| `(x == 0) ? 0 : 1` | `addic/subfe` | Any type |
| `(x != 0) ? 1 : 0` | `addic/subfe` | Same codegen (logically equiv) |
| `(ptr == 0) ? 0 : 1` | `addic/subfe` | Pointer = same as int |
| `(unsigned x > 0)` | `addic/subfe` | Canonicalized to `!= 0` |

### Category 2: Equality Against Non-Zero Constant (3 instructions)
| Source | PPC | Notes |
|--------|-----|-------|
| `(x == 1) ? 1 : 0` | `addi/cntlzw/rlwinm` | Normalize to 0, count leading zeros |
| `(x != 1) ? 1 : 0` | `addi/addic/subfe` | Normalize to 0, then zero-test |

### Category 3: Signed Positive Test (3 instructions)
| Source | PPC | Notes |
|--------|-----|-------|
| `(x > 0) ? 1 : 0` | `neg/andc/srwi` | Sign-bit extraction |
| `(x > 0) + (y > 0)` | `neg/andc/srwi` per operand | Same in arithmetic |
| `(x > 0) & (y > 0)` | `neg/andc/srwi` + `and` | Same in bitwise |

### Category 4: Unsigned Ordered Comparisons (3-4 instructions)
| Source | PPC | Notes |
|--------|-----|-------|
| `(unsigned x > N)` | `subfic/subfe/clrlwi` | 3 insns, any N>0 |
| `(unsigned x >= N)` | `li/li/subfc/subfze` | 4 insns |
| `(unsigned x < N)` | `li/subfc/subfe/clrlwi` | 4 insns |

### Category 5: Signed Ordered Comparisons (5-6 instructions)
| Source | PPC | Notes |
|--------|-----|-------|
| `(int x > N)` | `li/subfc/eqv/srwi/addze/clrlwi` | 6 insns, any N>0 |
| `(int x >= N)` | `li/srawi/srwi/subfc/adde` | 5 insns |
| `(int x < N)` | `li/subfc/eqv/srwi/addze/clrlwi` | 6 insns (same as >) |
| `(int)(bool)(x > N)` | same as `(x > N)` | Cast doesn't change |
| `(x > N) ? 1 : 0` | same as `int b = (x > N)` | Ternary = same |
| `a && (bool)(x > N)` | same + short-circuit branch | && adds branch |

### Category 6: No Materialization (branch-based)
| Source | PPC | Notes |
|--------|-----|-------|
| `a && x > 1` (no cast) | `cmpwi + ble` | Direct comparison branch |

**Key instruction → source mapping for DC3 decomp:**
- `addic/subfe` → zero test (any type, `== 0` or `!= 0`)
- `addi/cntlzw/rlwinm` → equality test against non-zero constant (`== N`)
- `addi/addic/subfe` → inequality test against non-zero constant (`!= N`)
- `neg/andc/srwi` → signed positive test (`> 0`)
- `subfic/subfe` → **unsigned** comparison against non-zero constant
- `subfc/eqv/srwi/addze` → **signed** comparison against non-zero constant
- `srawi/subfc/adde` → **signed `>=`** comparison
- `subfc/subfze` → **unsigned `>=`** comparison

**The `(bool)` cast does NOT trigger materialization** — any boolean context does.
Signedness is the key differentiator between carry sequences.

### subf. Loop Condition Fusion
| Source Pattern | Generated Code |
|---------------|---------------|
| `while (hi >= lo)` | `cmpw cr6,r3,r30` |
| `while (hi - lo >= 0)` | `subf. r31,r31,r3` |

**Confirmed**: The subtraction form fuses into `subf.` (subtract-and-record).
The comparison form uses separate `cmpw`. This is a source-level choice.

## 5. Branch Polarity

| Source Pattern | Branch Used | Notes |
|---------------|-------------|-------|
| `if (x == 0) A else B` | `bne` → skip A | Condition inverted, falls through to A |
| `if (x != 0) B else A` | `beq` → skip B | Condition inverted, falls through to B |
| `if (x == 0) A; return; B` | `bne` → skip A | Early return: same inversion |
| `if (x != 0) B; return; A` | `beq` → skip B | Early return: same inversion |
| Nested if/else | `bne` for both | Both conditions inverted |

**Pattern**: The compiler ALWAYS inverts the condition for if/else branches:
- `== 0` → emits `bne` (branch if NOT equal, skip the true body)
- `!= 0` → emits `beq` (branch if equal, skip the true body)

The "then" block is the fall-through. This means:
- **Target uses `beq`** = the source condition was `!= 0` (or `> 0`, `!= nullptr`, etc.)
- **Target uses `bne`** = the source condition was `== 0` (or `== nullptr`, `!x`)

## 6. Float Precision

**Finding: DOUBLETOSINGLE pass converts all double literals to float when assigned to float.**

| Source Pattern | Load Insn | Notes |
|---------------|-----------|-------|
| `float x = 0.001` (double literal) | `lfs` | Demoted to single by DOUBLETOSINGLE |
| `float x = 0.001f` (float literal) | `lfs` | Already single |
| `static const float k = 0.001f` | `lfs` | Loads from static storage |
| `static const float k = 0.001` | `lfs` | Double literal demoted in static init |
| `float x = 100.0f` (inline) | `lfs` | Loads from .rdata |
| `static const float k = 100.0f` | `lfs` | Loads from static storage |

**All cases produce `lfs`**. The DOUBLETOSINGLE pass is aggressive — it demotes
any double assigned to a float variable. No cases produced `lfd` in these tests.

**Implication**: The `lfd` instructions we see in AT_LIMIT functions must come from
contexts where the value stays as double (double variables, double parameters, or
expressions that aren't narrowed).

## 7. Pass Identification (Binary Patching)

**Methodology**: Replace first byte of each pass function with `0xC3` (RET),
compile test cases, check which PPC instructions disappear from the listing.

### Key findings:

**Record-form fusion** (Group 3, Pass 2 — `fcn.10c0f14e`):
- Fuses `subf + compare-to-zero` into `subf.` at the IL level
- Without it: `subf` + `cmpwi` (separate compare)
- With it: `subf.` (subtract-and-record, single instruction)
- Source trigger: `while (hi - lo >= 0)` vs `while (hi >= lo)`

**Xenon scheduler / Final emission** (Group 5, Pass 10 — `fcn.10b3421b`):
- 382-byte dispatcher calling ~15 sub-functions
- Generates ALL PPC-specific patterns: subfc, subf., eqv, srwi
- Contains the Xenon pipeline scheduler (`fcn.10b71d8f`) with `/QXSTALLS` diagnostics
- Pipeline hazard table: LHS, BF, LHSUSE, P, MC, S, DA, D, VQF, VQS, VQD (11 entries)
- The scheduler specifically generates `eqv` and `subf.` (call_22 → 0x10b71d8f)
- Boolean materialization (`subfc/srwi`) generated by a different sub-function

**Disproved**: `fcn.10c04d6d` (Group 4, Pass 4) was originally guessed as G5_SPECIAL
based on pass table position. Binary patching shows it has ZERO effect on peephole patterns.

**NOR instruction**: Not generated by any optimization pass — appears in output regardless
of which pass is disabled. Generated during instruction selection (before optimization passes).

### Architecture: G5P10 is the PPC Code Generator

Disabling G5P10 produces ZERO code — it's not a peephole optimizer, it's the
**entire PPC instruction selection and emission stage**. All PPC-specific patterns
are instruction selection choices, not post-generation transforms.

### Instruction selection pipeline:

| Pattern | IL-level prep | Code generator |
|---------|--------------|----------------|
| `subf.` | G3P2 marks record-form fusion | G5P10 emits `subf.` |
| `subfc/eqv/srwi` | IL bool expr with `(bool)` cast | G5P10 selects carry-based sequence |
| `neg/andc/srwi` | IL comparison in arithmetic context | G5P10 selects sign-bit sequence |
| `not` (NOR) | IL byte-width XOR 0xFF | G5P10 selects complement |
| `rlwinm` | IL shift+mask on same register | G5P10 combines into rotate |
| `extsb/extsh` | IL sign extension | G5P10 emits sign-extend |

### Complete PPC pattern catalog from differential testing:

| Source Pattern | PPC Instructions | Notes |
|---------------|-----------------|-------|
| `(x == 0) ? 0 : 1` | `addic/subfe` | Zero test, 2 insns, any type |
| `(x != 0) ? 1 : 0` | `addic/subfe` | Same as `==0` (logically equiv) |
| `unsigned x > 0` | `addic/subfe` | Canonicalized to `!= 0` |
| `(x == 1) ? 1 : 0` | `addi/cntlzw/rlwinm` | Normalize + count-leading-zeros |
| `(x != 1) ? 1 : 0` | `addi/addic/subfe` | Normalize + zero-test |
| `(x > 0) ? 1 : 0` (signed) | `neg/andc/srwi` | Sign-bit extraction, 3 insns |
| `(x > 0) + (y > 0)` | `neg/andc/srwi` per operand + `add` | Sign-bit in arithmetic |
| `(unsigned x > N)` | `subfic/subfe/clrlwi` | Unsigned ordered, 3 insns |
| `(unsigned x >= N)` | `li/li/subfc/subfze` | Unsigned ordered, 4 insns |
| `(unsigned x < N)` | `li/subfc/subfe/clrlwi` | Unsigned ordered, 4 insns |
| `(int x > N)` (N>0) | `li/subfc/eqv/srwi/addze/clrlwi` | Signed ordered, 6 insns |
| `(int x >= N)` (N>0) | `li/srawi/srwi/subfc/adde` | Signed ordered, 5 insns |
| `(int x < N)` (N>0) | `li/subfc/eqv/srwi/addze/clrlwi` | Signed ordered, 6 insns |
| `a && (bool)(x > 1)` | `subfc/eqv/srwi/addze/clrlwi` + branch | Short-circuit + carry |
| `a && x > 1` (no cast) | `cmpwi + ble` | Branch-based, no materialization |
| `if (x) r = 1` | `cmplwi/li/li/bnelr` | Branch-based (not branchless) |
| `while (hi-lo >= 0)` | `subf.` (loop check) | Record-form subtract |
| `while (hi >= lo)` | `cmpw` (loop check) | Direct compare (no fusion) |
| `u8 ^ 0xFF` | `clrlwi/not/clrlwi` | NOR peephole (byte-width) |
| `u32 ^ 0xFF` | `xori` | Direct XOR (word-width) |
| `(x >> 4) & 0xFF` | `rlwinm` | Rotate-and-mask |
| `(int)char_val` | `extsb` | Sign extension |

## Actionable Findings for DC3 Decomp

1. **Register allocation**: For functions with <15 callee-saved vars in simple
   patterns, declaration order = register order. Reordering declarations is
   the correct fix for register swaps in these cases.

2. **BSF threshold**: Needs more complex test cases to find. Real DC3 functions
   with ~7+ variables likely have overlapping live ranges that trigger it.

3. **Inlining**: Weighted cost model (~40 cost units). Arithmetic=1, branch=8.
   `__forceinline` bypasses all limits.
   Header function body size directly affects neighboring function codegen.

4. **NOR peephole**: Widening u8 to u32 before XOR prevents NOR generation.
   This is the fix for `u8_val ^ 0xFF` → NOR mismatches.

5. **Boolean materialization** (comprehensive catalog):
   - `addic/subfe` in target → source tests `== 0` or `!= 0` (any type)
   - `addi/cntlzw/rlwinm` → source tests `== N` (non-zero constant)
   - `addi/addic/subfe` → source tests `!= N` (non-zero constant)
   - `neg/andc/srwi` → source tests signed `> 0`
   - `subfic/subfe` → source uses **unsigned** comparison (`> N`, N>0)
   - `subfc/eqv/srwi/addze` → source uses **signed** comparison (`> N`, N>0)
   - `srawi/subfc/adde` → source uses **signed `>=`**
   - `subfc/subfze` → source uses **unsigned `>=`**
   - Signedness is the key variable — changing `int` to `unsigned` or vice versa
     completely changes the instruction sequence.

6. **subf. fusion**: Write `hi - lo >= 0` instead of `hi >= lo` to get `subf.`.

7. **Branch polarity**: Compiler ALWAYS inverts condition for the branch. If
   target shows `beq`, the source condition tested `!=`. If target shows `bne`,
   the source condition tested `==`.

8. **IL type system encodes signedness**: The IL type prefix explicitly marks
   signed vs unsigned (int=`86 41 74`, uint=`86 42 75`). c2.dll uses this
   to select signed (subfc/eqv/srwi/addze) vs unsigned (subfic/subfe) PPC
   sequences. Same IL opcode (GT) produces different PPC based on operand type.

9. **IL integer promotion**: Small types (char, short) get CAST to int before
   arithmetic, then CAST back. This adds IL nodes that may affect inlining budget.

10. **Pass architecture**: The compiler uses a two-stage model for PPC-specific patterns:
    - IL-level passes (groups 1-4) operate on the intermediate language
    - The final emission pass (G5P10) converts IL to PPC, applying peephole optimizations
    - The Xenon scheduler within G5P10 handles pipeline scheduling AND instruction selection
    - This means most "peephole" patterns can't be controlled by manipulating optimization
      passes — they're generated during final emission from the IL representation

## 8. rlwinm Fusion (2026-03-10)

**Finding: Source type controls whether G5P10 fuses shift+mask into `rlwinm`.**

### Right shift (srwi vs extrwi/rlwinm):

| Source pattern | Result | PPC |
|---------------|--------|-----|
| `u8 b = val; b >> 2` | FUSED | `rlwinm r3,r3,30,26,31` |
| `u32 v = val & 0xFF; v >> 2` | FUSED | `rlwinm r3,r3,30,26,31` |
| `u32 v = val; (v >> 2) & 0x3F` | FUSED | `rlwinm r3,r3,30,26,31` |
| `unsigned long v = val & 0xFF; v >> 2` | FUSED | `rlwinm r3,r3,30,26,31` |
| `(unsigned char)val >> 2` | FUSED | `rlwinm r3,r3,30,26,31` |
| `int v = val & 0xFF; v >> 2` | **SEPARATE** | `clrlwi` only (no shift!) |

**Signedness is the differentiator**: `int` (signed) with `& 0xFF` mask
produces `clrlwi` but no shift fusion. All unsigned variants fuse.

### Rotation decomposition:

| Source pattern | Result | PPC |
|---------------|--------|-----|
| `u8 b; (b >> 2) \| (b << 6)` | 2 fused `rlwinm` | extrwi + clrlslwi |
| `u32 v = val & 0xFF; (v >> 2) \| (v << 6)` | SEPARATE | `clrlwi` + `slwi` + `srwi` + `clrlwi` |
| `unsigned long v = (u8)val; (v >> 2) \| (v << 6)` | 2 fused `rlwinm` | extrwi + clrlslwi |

**Key finding**: `u32` with explicit `& 0xFF` mask produces SEPARATE `slwi`/`srwi`,
while `u8` and `unsigned long` with CAST produce FUSED `rlwinm`. The difference
is mask (`& 0xFF`) vs CAST (`(unsigned char)`):
- Mask: compiler sees `AND` in IL, keeps value as full-width, generates separate shifts
- Cast: compiler sees `CAST` in IL, narrows the type, generates fused rlwinm

### u8 mask placement (early vs late):

| Source type | Mask placement | clrlwi count | XOR position |
|------------|---------------|-------------|-------------|
| `unsigned char a, b` | BOTH (before AND after XOR) | 3 | between masks |
| `unsigned int a, b` | LATE (only after XOR) | 1 | before mask |
| `unsigned long a, b` | LATE (only after XOR) | 1 | before mask |

**u8 variables generate redundant masks**: The compiler masks both operands to
8-bit before XOR, then masks the result again. `u32`/`unsigned long` variables
produce one XOR followed by one final mask — which is what the target generates.

### Implication for DC3 matching:

To match the target's separate `srwi`/`slwi` pattern in byte-rotation functions:
- Use `u32` or `unsigned long` for intermediate values
- Apply `& 0xFF` mask (not `(u8)` cast) to narrow the value
- For XOR/OR operations, use `u32`/`unsigned long` operands and defer u8
  truncation to the final `return u8(result)`

Conversely, if the target uses fused `rlwinm`, use `u8` typed intermediates.

**Proven on**: 20 ByteGrinder byte-rotation functions (op15-op39).
Using `unsigned long` with explicit rotation matched target's `srwi`/`slwi`.
Using `u8` types generated fused `rlwinm` (extrwi/clrlslwi).

## 9. FPR Allocation Interaction (2026-03-10)

**Finding: FPR callee-saved follows the same f31-first, descending pattern as GPR.**

### FPR scaling:
| N floats | Callee-saved FPR | Prologue | Notes |
|----------|-----------------|----------|-------|
| 1-4 | (none) | (none) | Fit in volatile f1-f4 |
| 5 | f31..f28 | __savefpr_28 | First callee-saved at N=5 |
| 6 | f31..f27 | __savefpr_27 | Linear descent |
| 7 | f31..f26 | __savefpr_26 | Linear descent |

**Negative result — FPR and GPR are independent**: Adding float variables does NOT
shift GPR assignments. Mixed int+float functions allocate GPR and FPR independently.
The `2int_1float` variant adds one GPR (r30) for the float function call's return
address, not for the float itself (the float stays in volatile f1).

**Float parameter vs local**: Both produce identical codegen — no callee-saved FPR
needed when the float is used once (stays in volatile fN).

### Actionable:
- FPR regswap fixes follow the same declaration-reorder strategy as GPR
- Adding/removing float variables does NOT affect int register assignments
- FPR callee-saved threshold is higher than GPR (N=5 vs N=2) due to more volatile FPRs

## 10. Template-Instantiation Signedness (2026-03-10)

**Finding: Template instantiations correctly propagate type signedness to codegen.**

### Comparison template:
| Instantiation | PPC comparison | Widening | Instruction count |
|--------------|---------------|----------|------------------|
| `int` | `subfc` (signed) | none | 18 |
| `unsigned` | `subfc` (signed — inlined!) | none | 16 |
| `short` | `subfc` (signed) | extsb/extsh | 20 |
| `unsigned short` | `subfc` (signed) | clrlwi | 18 |

**Negative result**: When the template comparison is inlined, the outer function
context determines the comparison type, not the template parameter. All variants
use `subfc` because the template return type is always `int`.

### Arithmetic template (widening behavior):
| Type | Has `clrlwi` mask | Has `extsh` | Instruction count |
|------|------------------|------------|------------------|
| `unsigned char` | Yes | No | 17 |
| `unsigned short` | Yes | No | 17 |
| `unsigned int` | No | No | 14 |
| `int` | No | No | 14 |

**Confirmed**: Sub-word types (u8, u16) get integer promotion masks (`clrlwi`) even
inside templates. Full-width types (u32, int) don't. This is the standard C++
integer promotion rule applied at the IL level.

### Actionable:
- Template instantiation does NOT produce surprising codegen differences
- The same u8/u16→int promotion rules apply inside templates as outside
- Signedness of the RESULT type matters more than the parameter type

## 11. Cross-Call Live Range (2026-03-10)

**Finding: Compiler correctly tracks live ranges — dead-after-call variables don't get callee-saved.**

### Variable survival across calls:
| Use pattern | Callee-saved GPR | Instruction count |
|------------|-----------------|------------------|
| `v = get(); call(); use(v)` | r31 | 14 |
| `v = get(); use(v); call()` | (none) | 10 |
| `v = get(); call()` (unused after) | (none) | 9 |

**Key finding**: Moving `use(v)` before `call()` eliminates the callee-saved register
entirely (v stays in volatile r3). This is pure live-range analysis — the compiler
knows v doesn't survive the call.

### Scaling: One variable across N calls needs exactly ONE callee-saved (r31) regardless
of how many calls it crosses. The number of intervening calls doesn't increase register
pressure.

### Declaration order:
| Variant | GPR | Hash |
|---------|-----|------|
| `a = get(); b = get(); call(); use(a,b)` | r31, r30 | 0ae484 |
| `b = get(); a = get(); call(); use(a,b)` | r31, r30 | 0f4cfa |

Swapping declaration order produces pure register swap (r30↔r31), confirming
first-declared→r31 even with cross-call live ranges.

### Actionable:
- If target has fewer callee-saved regs, check if a variable can be used BEFORE a call
- Declaration order controls which variable gets r31 vs r30 across calls
- Adding more calls between def and use doesn't increase register pressure

## 12. Scope Nesting (2026-03-10)

**Finding: Scope nesting has ZERO effect on codegen — the compiler flattens all scopes.**

### Nesting depth:
| Depth | GPR | Hash | Same as depth=0? |
|-------|-----|------|-------------------|
| 0 | r31 | 212392 | — |
| 1 | r31 | 212392 | Yes |
| 2 | r31 | 212392 | Yes |
| 3 | r31 | 212392 | Yes |
| 4 | r31 | 212392 | Yes |

**Identical hash across all nesting depths** — extra `{}` braces produce no codegen change.

### Scope placement:
| Variant | GPR | Hash |
|---------|-----|------|
| `outer_scope` (var in function scope) | r31 | 212392 |
| `inner_scope` (var in `{}`-wrapped block) | r31 | 212392 |
| `split_inner` (use before call, separate scope) | (none) | c79be4 |

`split_inner` differs because the live range doesn't cross the call, not because
of scope boundaries.

### Disjoint scopes:
| Variant | GPR | Stack | Hash |
|---------|-----|-------|------|
| `sequential_scopes` (`{a; call; use;} {b; call; use;}`) | r31 | 96 | — |
| `merged_scope` (same code, no `{}`) | r31 | 96 | same |
| `three_disjoint` | r31 | 96 | — |

**Confirmed**: Compiler reuses the same callee-saved slot (r31) for disjoint scopes.
Sequential and merged produce identical codegen.

### Negative result:
Scope nesting cannot be used to reduce register pressure or change allocation.
The compiler's SSA-based analysis ignores C++ scope boundaries entirely.

## 13. Static Local Guard (2026-03-10)

**Finding: Each static local gets a separate guard (lbz + branch per static).**

### Guard count:
| N statics | Guard loads | Branches | Total instructions |
|-----------|------------|----------|-------------------|
| 1 | 1 | 1 | 26 |
| 2 | 2 | 2 | 42 |
| 3 | 3 | 3 | 43 |

Each additional static local adds ~1 guard load + 1 conditional branch.

### Static in if-branch vs outer scope:
| Variant | Instructions | Hash |
|---------|-------------|------|
| `outer_scope` (static at function level) | 26 | 15d29f |
| `if_branch` (static inside if) | 27 | bd8628 |
| `two_in_if` (two statics in if) | 39 | 6c5893 |

Different guard code layout — `outer_scope` checks guard before the if,
`if_branch` moves guard inside. This matches the known DC3 issue: target's
`??_B` combined guard vs our separate `$S` guards.

### Static const:
| Variant | Has guard? | Instructions |
|---------|-----------|-------------|
| `static int s = get()` | Yes | 25 |
| `static const int s = get()` | Yes | 25 |
| `static const int s = 42` | **No** | 3 |

Only literal-initialized `static const` elides the guard. Runtime-initialized
`static const` still needs a guard (const-ness doesn't help when the init value
comes from a function call).

### Negative result:
Cannot reduce guard count with source-level changes — each static local with
a runtime initializer will always generate its own guard. The ??_B combined
guard in the target binary is a linker-level or older compiler behavior that
our compiler version doesn't replicate.
