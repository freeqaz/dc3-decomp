# MSVC Xbox 360 Compiler Architecture

## Pipeline Overview

```
                    cl.exe (driver)
                    ┌─────────────────────────────┐
                    │  Parse command line           │
                    │  Dispatch to front-end + back-end │
                    └──────────┬──────────────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                                 ▼
        c1xx.dll (C++ FE)                 c1.dll (C FE)
        ┌──────────────────┐         ┌──────────────────┐
        │  Preprocessing    │         │  Preprocessing    │
        │  Parsing          │         │  Parsing          │
        │  Template inst.   │         │  Type checking    │
        │  Overload res.    │         │  IL generation    │
        │  IL generation    │         └────────┬─────────┘
        └────────┬─────────┘                   │
                 │                              │
                 └──────────┬───────────────────┘
                            ▼
                     IL file (temp)
                            │
                            ▼
                    c2.dll (PPC back-end)
                    ┌──────────────────────────────┐
                    │  IL ingestion                  │
                    │  Optimization passes:           │
                    │    CONSTANT_FOLDING             │
                    │    DEAD_CODE_ELIMINATION        │
                    │    COMMON_SUBEXP (CSE)          │
                    │    COPYPROP                     │
                    │    CODE_MOTION (LICM)           │
                    │    PARTIAL_RED_ELIMINATION      │
                    │    NORMALIZE_CASTS              │
                    │    DOUBLETOSINGLE               │
                    │    CONTRACTION (FMA)            │
                    │    FACTORING_DISTRIBUTION       │
                    │    MUL_DIV_BY_ONE               │
                    │    CTIME_EVAL                   │
                    │    FPMOV_TO_INTMOV              │
                    │    COLOR (register allocation)  │
                    │  Instruction selection           │
                    │  Peephole optimization           │
                    │  Prologue/epilogue generation    │
                    │  COFF .obj emission              │
                    └──────────────────────────────┘
                            │
                            ▼
                      .obj (COFF, PPC)
```

## Module Interface

cl.exe loads c2.dll dynamically and calls its exports:

### c2.dll Exports
```c
// Get the handler object (vtable-based interface)
HRESULT DllGetObjHandler(/* out */ IObjHandler** ppHandler);

// Main compilation entry — receives IL, produces .obj
int __stdcall InvokeCompilerPass(
    /* IL data or path? */,
    /* options? */,
    /* output path? */
);

// Wide-char variant
int __stdcall InvokeCompilerPassW(
    /* wchar_t IL path? */,
    /* wchar_t options? */,
    /* wchar_t output? */,
    /* extra param? */
);

// Cancel compilation
void __stdcall AbortCompilerPass(int reason);
```

The exact parameter types need to be determined by RE of `InvokeCompilerPass`.

## IL File Format

The front-end (c1xx.dll) writes an intermediate file that c2.dll reads. Key facts:

- CL normally deletes this file after compilation
- The `/B2` flag can redirect c2.dll loading (replace with a wrapper)
- The `/FAs` flag may preserve some intermediate info
- The IL is NOT the same as MSIL/.NET IL — it's a proprietary native IL
- Format is undocumented; must be captured and reverse-engineered

### Capturing IL Files

Use `/Bx` (replace processing phase) or environment hooks to intercept:
1. Replace c2.dll with a wrapper that saves the IL before forwarding
2. Or use `/FA` + `/Fa` to get assembly listing as a proxy

## Key Data Structures (Hypothesized)

Based on string analysis and common compiler architecture:

### Basic Block / Flow Graph
- Functions are decomposed into basic blocks with a CFG
- References to "flow" and block counts in diagnostic strings
- `DO_REJECTED_FG_BLK_CNT_TOO_BIG` suggests block count thresholds

### Tuples / IR Nodes
- `DO_REJECTED_CONTAINS_INADMISSABLE_TUPLE` — IR uses "tuple" terminology
- Each operation is likely a tuple (opcode, operands, type, flags)
- This is consistent with classical compiler IR design (quadruples/triples)

### Register Descriptors
- `__gregister_get` / `__gregister_set` — general register access
- Separate GPR and FPR pools (consistent with our empirical observations)
- VMX register pool (`__restvmx_*`, `/QVMXReserve`)

## Undocumented Flags (Relevant to Our Work)

These flags control c2.dll behavior and reveal internal architecture:

### Optimization Control
- `/d2Zi+` — enhanced debug info
- `/d2nopvmxperm` — disable VMX permutation optimization
- `/d2nopvmxsimp` — disable VMX simplification
- `/d2cgsummary` — print code generation summary (Aras Pranckevičius)

### Inlining
- Format strings reveal: `INL:`, `INF:`, `ERR:`, `WRN:` prefixed diagnostics
- `%s won't be inlined (too big)` — size threshold exists
- `inline candidate %s` — candidate selection logic
- `[force inline]`, `[normal inline]`, `[vcall inline]` — inline categories

### Loop Optimization
- `DO_REJECT_*` flags control loop optimization decisions
- `ONE_IF_ITERATION`, `HOPELESS`, `DUMB_WHILE`, `DOWHILE` — loop classification
- `loop_end : no inversion` — loop inversion decisions

## Version Details

```
Compiler: Microsoft (R) Optimizing Compiler Version 16.00.11886.00
Build:    78379
Source:   vctools\compiler\be\p2\c2\
Platform: x86 host → PPC target (cross-compiler)
SDK:      Xbox 360 XDK (date from PE timestamp: 0x4C7F7BA8 = 2010-09-02)
```
