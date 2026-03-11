# Initial Findings — MSVC Xbox 360 PPC Compiler RE

## Binary Analysis Summary

### Target: c2.dll (PPC back-end code generator)
- **Size**: 1,347,072 bytes (1.3 MB)
- **Format**: PE32 (x86), 4 sections
- **Image base**: 0x10B00000
- **Version**: Microsoft (R) Optimizing Compiler Version 16.00.11886.00
- **Build**: 78379 (source: `vctools\compiler\be\p2\c2\`)
- **Estimated functions**: ~1,430
- **Strings**: 2,295 total
- **Exports**: 4 (`DllGetObjHandler`, `InvokeCompilerPass`, `InvokeCompilerPassW`, `AbortCompilerPass`)

### PDB Status
- **GUID**: `0D71D53710A34205A380297384C16F8D` (age 19)
- **PDB path**: `78379\vctools\compiler\be\p2\c2\obj\i386\c2.pdb`
- **Symbol server**: NOT available (HTTP 404 on all variants)
- **Implication**: Must rely on Ghidra auto-analysis + string cross-referencing

## Major Discovery: Optimization Pass Table

Found a contiguous array of 30 string pointers at `.data` RVA `0x0012E9E4`.
This is the **complete named optimization pass catalog**:

```
Index  Pass Name                    Relevance to DC3
─────  ────────────────────────────  ──────────────────
  0    FACTORING_DISTRIBUTION       Low
  1    FACTORING_INVERSE            Low
  2    FACTORING_INVERSE_2          Low
  3    STORE_AND_LOAD_SINGLE        Medium
  4    STORE_AND_LOAD_DOUBLE        Medium
  5    AVOID_BADFPENTERLOADS        Medium
  6    SCALAR_REDUCTION             Medium
  7    SCALAR_REPLACEMENT           Medium
  8    COMMON_SUBEXP                High
  9    CTIME_EVAL                   Medium
 10    NORMALIZE_CASTS              HIGH
 11    CODE_MOTION                  High
 12    PARTIAL_RED_ELIMINATION      High
 13    MUL_DIV_BY_ONE               Low
 14    COLOR ★                      CRITICAL (register alloc)
 15    COPYPROP                     High
 16    SU_COPYPROP                  High
 17    HOIST_EXCEPT                 Low
 18    DEAD_CODE_ELIMINATION        High
 19    G5_SPECIAL ★                 HIGH (PPC peepholes)
 20    TRYCATCH_EXCEPTION           Low
 21    NEWFP_EXCEPTION              Low
 22    NEWFP_EXCEPTION_FWAIT        Low
 23    KEEP_USER_CASTS              Medium
 24    SIDE_EFFECT                  Medium
 25    FPINLINE_INTRINSIC           Medium
 26    DOUBLETOSINGLE ★             HIGH (float precision)
 27    FPSPECIAL                    Medium
 28    SEH_WRITETHRU_OFF            Low
 29    FPMOV_TO_INTMOV ★            HIGH (FP↔GPR transfer)
```

## Assembly Listing Works (/FAcs)

Confirmed: compiling with `/FAcs` produces full annotated assembly with:
- PPC machine code bytes (hex)
- PPC assembly mnemonics
- Source line references (`;` line comments)
- COMDAT/section info
- Mangled + demangled symbol names

This is usable as a cross-validation tool — compile test cases and compare
the compiler's own assembly output against our expectations.

## /d2 Flag Testing

| Flag                | Result                    | Notes                        |
|---------------------|---------------------------|------------------------------|
| `/d2cgsummary`      | `fatal error C1007`       | Not supported in v16.00      |
| `/d2nopvmxperm`     | `fatal error C1007`       | Not CLI-exposed              |
| `/d2nopalign`       | `fatal error C1007`       | Not CLI-exposed              |
| `/QVMXReserve`      | Works                     | Reserves VMX registers       |
| `/Qnopalign`        | `ignored unknown option`  | cl.exe ignores, doesn't forward |
| `/FAs`              | Works                     | Assembly + source listing     |
| `/FAcs`             | Works                     | Assembly + source + hex bytes |

The `nop*` strings in c2.dll are internal configuration flags, not exposed to the CLI.
They may be accessible via:
1. Binary patching (flip the default in the .data section)
2. DLL hooking (intercept the option structure before pass execution)
3. Environment variables (unlikely but worth testing)

## Internal Architecture Clues

### c2.dll Option Parsing
- c2 receives flags as `-flagname` (stripped of `/d2` prefix)
- Flag parsing happens in the `p2` phase (confirmed by error message)
- Only a small set of flags are recognized in our version

### PPC Instruction Encoding
- All PPC mnemonics embedded as strings (assembler/disassembler built-in)
- Includes base PPC, VMX/Altivec, and Xbox 360-specific instructions
- `__restvmx_100` through `__restvmx_124` — VMX save/restore helpers
- `__savegprlr_*` and `__restgprlr_*` — GPR save/restore (called from our code)

### Inlining System (Rich Diagnostics)
```
INL: !!! InlBadCandidate said not to inline %s into %s
INL: Inlining %s (%d instrs) into ...
INF: %s won't be inlined (too big)
INF: %s has linear flow
INF: %s is a redirector function
INF: %s (redirector) is always inline by transitivity
ERR: inline candidate %s in ...
WRN: %s (%s) won't be inlined
WRN: %s has 'dangerous' inline asm, won't be profiled
```

### Profile-Guided Optimization (PGO/POGO)
- Extensive PGO infrastructure: 129 pogo-related strings
- `PogoDb*` functions for reading/writing profile data
- Not relevant to DC3 decomp (target wasn't compiled with PGO)

### Loop Optimizer
- Detailed rejection reasons with specific thresholds:
  - `DO_REJECTED_TUP_CNT_GT_50` — max 50 tuples in loop body
  - `WHILE_REJECTED_CONTAINS_CALL` — calls defeat while-loop optimization
  - `DO_REJECTED_FG_BLK_CNT_TOO_BIG` — too many basic blocks

### Switch Optimization
```
OPT: Optimized switch on line %u
OPT: Pulled out node %I64d for early comparison (%u%%)
OPT: Created default node for values %I64d..%I64d (%u%%)
```

## Next Steps

### Immediate (can do now)
1. **Load c2.dll into Ghidra** — auto-analyze, then cross-reference pass table
2. **Find the pass dispatch function** — the function that iterates the pass table
3. **Identify COLOR entry point** — follow reference from pass table[14]
4. **Map InvokeCompilerPass** — understand the main compilation pipeline

### Short-term (days)
5. **Build a /FAcs differential tool** — compile test cases with/without specific source
   patterns, diff the assembly output to understand what triggers codegen differences
6. **Identify G5_SPECIAL** — find PPC-specific peephole patterns (NOR, bool materialize)
7. **Map the IL format** — intercept the temp file between c1xx and c2

### Medium-term (weeks)
8. **Targeted decompilation of COLOR** — register allocator logic
9. **Targeted decompilation of G5_SPECIAL** — PPC peephole patterns
10. **Build DLL hook framework** — instrument c2.dll for runtime telemetry
