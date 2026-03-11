# c2.dll Optimization Pass Catalog

## Pass Name Table

Found within a larger optimizer config struct at `.data` VA `0x10C2E980`.

The struct contains two contiguous arrays of pass-name string pointers:
- **Pre-table** (5 entries): offset +0x50 to +0x60, algebraic/folding passes
- **Main table** (30 entries): offset +0x64 to +0x0D8, null-terminated at +0x0DC

Total: **35 named optimization passes**.

### Pre-Table Entries (VA 0x10C2E9D0)

| Offset | Pass Name          | Category         |
|--------|--------------------|------------------|
| +0x50  | CONTRACTION        | FMA contraction  |
| +0x54  | ASSOCIATIVITY      | Algebraic        |
| +0x58  | CONSTANT_FOLDING   | Constant folding |
| +0x5C  | SPECIAL_VALUE      | Constant folding |
| +0x60  | DISTRIBUTION       | Algebraic        |

### Main Table (VA 0x10C2E9E4, 30 entries)

| Index | Pass Name                    | Category              | DC3 Decomp Relevance |
|-------|------------------------------|-----------------------|----------------------|
| 0     | FACTORING_DISTRIBUTION       | Algebraic             | Low                  |
| 1     | FACTORING_INVERSE            | Algebraic             | Low                  |
| 2     | FACTORING_INVERSE_2          | Algebraic             | Low                  |
| 3     | STORE_AND_LOAD_SINGLE        | Memory/FP             | Medium               |
| 4     | STORE_AND_LOAD_DOUBLE        | Memory/FP             | Medium               |
| 5     | AVOID_BADFPENTERLOADS        | FP correctness        | Medium               |
| 6     | SCALAR_REDUCTION             | Loop optimization     | Medium               |
| 7     | SCALAR_REPLACEMENT           | Loop optimization     | Medium               |
| 8     | COMMON_SUBEXP                | Classic optimization  | High                 |
| 9     | CTIME_EVAL                   | Constant folding      | Medium               |
| 10    | NORMALIZE_CASTS              | Type handling         | **High**             |
| 11    | CODE_MOTION                  | LICM                  | High                 |
| 12    | PARTIAL_RED_ELIMINATION      | PRE                   | High                 |
| 13    | MUL_DIV_BY_ONE               | Strength reduction    | Low                  |
| 14    | **COLOR**                    | **Register allocation** | **Critical**       |
| 15    | COPYPROP                     | Copy propagation      | High                 |
| 16    | SU_COPYPROP                  | Copy propagation (SU) | High                 |
| 17    | HOIST_EXCEPT                 | Exception handling    | Low                  |
| 18    | DEAD_CODE_ELIMINATION        | DCE                   | High                 |
| 19    | G5_SPECIAL                   | PPC G5 optimizations  | **High**             |
| 20    | TRYCATCH_EXCEPTION           | EH                    | Low                  |
| 21    | NEWFP_EXCEPTION              | FP exceptions         | Low                  |
| 22    | NEWFP_EXCEPTION_FWAIT        | FP exceptions         | Low                  |
| 23    | KEEP_USER_CASTS              | Cast preservation     | Medium               |
| 24    | SIDE_EFFECT                  | Side effect tracking  | Medium               |
| 25    | FPINLINE_INTRINSIC           | FP intrinsics        | Medium               |
| 26    | DOUBLETOSINGLE               | FP precision          | **High**             |
| 27    | FPSPECIAL                    | FP special values     | Medium               |
| 28    | SEH_WRITETHRU_OFF            | SEH                   | Low                  |
| 29    | FPMOV_TO_INTMOV              | Register transfer     | **High**             |

## Critical Passes for DC3 Decomp

### COLOR (Index 14) — Register Allocation
The graph-coloring register allocator. This is THE most important pass to understand.

**What we know empirically:**
- Callee-saved GPR: first declared variable -> r31, second -> r30, etc.
- Callee-saved FPR: first float -> f31, second -> f30, etc.
- BSF (graph coloring) kicks in at ~7+ callee-saved variables
- ~17% of AT_LIMIT functions have register allocation issues
- 1,218 functions blocked by register swap mismatches

**What we need to learn from RE:**
- Exact threshold for BSF vs linear allocation
- Spill cost calculation formula
- How declaration order maps to register assignment
- How virtual function calls affect callee-saved register pressure

**COLOR initialization (from binary analysis):**
- Entry: `fcn.10bc6487` (131 bytes actual, 207 helpers in call tree)
- Clears 1428-byte state buffer via memset
- Handles 261 registers (base PPC) or 357 (with VMX/+96)
- Exclusive to pass group 1

### G5_SPECIAL (Index 19) — PPC G5/Xenon Optimizations
Xbox 360's Xenon CPU is based on PowerPC 970 (G5 derivative). This pass likely contains
Xenon-specific instruction scheduling and peephole optimizations.

**Likely contents:**
- NOR peephole (xor 0xFF -> NOR)
- Boolean materialization patterns (subfc/eqv/srwi)
- Branch prediction hints
- Paired-single optimizations
- subf. loop condition pattern

### DOUBLETOSINGLE (Index 26) — Float Precision
Controls when `double` literals/operations are demoted to `float`.
- `0.001` (double literal) -> `lfd` instruction
- `0.001f` (float literal) -> `lfs` instruction
- Mismatches here cause AT_LIMIT for many functions

### FPMOV_TO_INTMOV (Index 29) — FP<->GPR Transfer
Controls when floating-point values are transferred to/from general registers.
Relevant to our "float literal GPR caching" pattern where static const float addresses
get cached in callee-saved GPRs.

### NORMALIZE_CASTS (Index 10) — Cast Handling
Controls how type casts are represented in the IR. Relevant to:
- `(bool)` cast triggering branchless boolean materialization
- Unsigned vs signed comparison codegen differences

## Pass Group Membership (from binary analysis)

Pass groups share many functions — they are NOT independent sequential stages.
44 unique functions across 5 groups, with significant overlap.

### Group-Exclusive Functions
- **Group 1**: COLOR (`fcn.10bc6487`), `fcn.10c2764e` (129KB — largest in c2.dll), 5 others
- **Group 5**: 8 exclusive functions (likely late-stage/emission passes)
- **Groups 2-4**: Mostly shared functions with minor exclusives

## Loop Optimization Rejection Strings

Found at RVA range `0x000133C0-0x000135F4`. These strings document why the loop
optimizer rejects loops:

| String                                    | Meaning                                    |
|-------------------------------------------|--------------------------------------------|
| `DO_REJECTED_FG_BLK_CNT_TOO_BIG`         | Flow graph block count exceeds threshold   |
| `DO_REJECTED_TUP_CNT_GT_50`              | Tuple count > 50                           |
| `DO_REJECTED_STORES_EQ_LOADS`            | Store count equals load count              |
| `DO_REJECTED_ONLY_STORES_NO_LOADS`       | Only stores, no loads in loop              |
| `DO_REJECTED_NO_FLOPS_AND_NO_STORES`     | No FP ops and no stores                    |
| `DO_REJECTED_NO_FLOPS_AND_NO_LOADS`      | No FP ops and no loads                     |
| `DO_REJECTED_NO_MEAT`                    | Insufficient work in loop body             |
| `DO_REJECTED_CONTAINS_INADMISSABLE_TUPLE`| IR tuple type not supported for optimization|
| `WHILE_REJECTED_CONTAINS_CALL`           | While loop contains function call          |
| `DO_REJECT_NOT_WORTH_USING_REMAINDER_LOOP`| Remainder loop not worth generating       |
| `DO_REJECT_BLOCK_MOVE_INTRINSIC`         | Block move intrinsic in loop               |
| `NO_ZTRIP_REJECT`                        | Can't prove zero-trip                      |
| `OUTER_LOOP_REJECT`                      | Outer loop rejected                        |
| `HASTRY_REJECT`                          | Loop contains try/catch                    |
| `NRF_REJECT`                             | Non-reducible flow                         |
| `WHILE_REJECTED`                         | Generic while rejection                    |
| `DO_REJECT_REDUCES`                      | Reduction not applicable                   |
| `DO_REJECT_STRIDE`                       | Stride optimization rejected               |
| `DO_REJECTED`                            | Generic do-loop rejection                  |
| `ONE_IF_ITERATION`                       | Loop runs only once                        |
| `DUMB_WHILE`                             | Trivial while loop                         |
| `WHILE_BITCOUNT`                         | Bitcount while loop pattern                |
| `REDUCTION_NOT_FIXED`                    | Reduction variable not fixed point         |

## Inlining Diagnostics

Format string analysis reveals the inlining decision system:

### Decision Categories
- `[force inline]` — `__forceinline` or equivalent
- `[normal inline]` — standard cost-benefit inlining
- `[vcall inline]` — virtual call devirtualization + inlining

### Rejection Messages
- `%s won't be inlined (too big)` — size threshold exceeded
- `%s not allowed to be inlined (globally unreferenced)` — dead code
- `%s has 'dangerous' inline asm, won't be profiled`
- `InlBadCandidate said not to inline %s into %s` — explicit bad candidate

### Cost Metrics
- `Inlining %s (%d instrs) into ...` — instruction count is a key metric
- `%s (%d instrs)` — size measurement in "instructions" (IR tuples?)
