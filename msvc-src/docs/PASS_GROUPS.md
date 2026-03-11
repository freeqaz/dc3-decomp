# c2.dll Pass Group Mapping

## Overview

The per-function optimizer (`fcn.10b7e6af`) dispatches 5 pass groups in sequence.
Each group calls multiple pass implementation functions, separated by yield calls
to `fcn.10bec297` (thread synchronization, checks `data.10c37d28`).

Between each pass, `data.10c2e2ec` is zeroed (resets a per-pass state flag).

## Pass Group 1 — Register Allocation (`fcn.10b7dd2c`)

Called first. Contains COLOR and related passes.

| Order | Function | Condition | Likely Pass |
|-------|----------|-----------|-------------|
| 1 | `fcn.10bc6487` | always | **COLOR** (confirmed — register allocator) |
| 2 | `fcn.10c182b4` | `data.10c2e2fc != 0` | ? (pre/post COLOR cleanup) |
| 3 | `fcn.10bb3256` | `data.10c2e2fc && data.10c3de20 == 2` | ? (conditional, rare) |
| 4 | `fcn.10bb537d` | `data.10c2e2fc && data.10c3de20 == 2` | ? (conditional, rare) |
| 5 | `fcn.10bd1068` | always | ? |
| 6 | `fcn.10c113f3` | always | ? |
| 7 | `fcn.10c2764e` | always | ? (129KB — largest function in c2.dll!) |
| 8 | `fcn.10c2226b` | always (edx=0) | ? |

**Notes:**
- COLOR entry at `fcn.10bc6487` confirmed via initialization analysis (memset 1428 bytes, 261/357 regs)
- `fcn.10c2764e` at 129KB is enormous — likely the main codegen/instruction selection pass
- `data.10c2e2fc` is a global config flag (optimization level or feature flag)
- `data.10c3de20` checked against 2 — possibly optimization level /O2

## Pass Group 2 — Algebraic/FP Transforms (`fcn.10b7ddff`)

3 passes, likely the algebraic optimization passes.

| Order | Function | Likely Pass |
|-------|----------|-------------|
| 1 | `fcn.10b39e59` | FACTORING_DISTRIBUTION / FACTORING_INVERSE? |
| 2 | `fcn.10b3668d` | STORE_AND_LOAD_SINGLE/DOUBLE? |
| 3 | `fcn.10b3c6e5` | SCALAR_REDUCTION/REPLACEMENT? |

## Pass Group 3 — Main Optimization (`fcn.10b7de4a`)

4 passes, core optimization pipeline. **Binary patching confirmed** group 3
generates `subf.` (record-form fusion).

| Order | Function | Confirmed Effect | Likely Pass |
|-------|----------|-----------------|-------------|
| 1 | `fcn.10b37b30` | **CRASH when disabled** (essential) | COMMON_SUBEXP? |
| 2 | `fcn.10c0f14e` | **Generates `subf.`** (record-form fusion) | Record-form optimizer |
| 3 | `fcn.10ba60bf` | No peephole effect | NORMALIZE_CASTS? |
| 4 | `fcn.10be460f` | No peephole effect | PARTIAL_RED_ELIMINATION? |

**G3P2 (`fcn.10c0f14e`)**: Converts `subf + compare-to-zero` into `subf.` at the IL
level. Without this pass, `while (hi - lo >= 0)` generates `subf` + `cmpwi` instead
of `subf.` (subtract-and-record). This is the source-controllable pattern — writing
`hi - lo >= 0` instead of `hi >= lo` triggers this optimization.

## Pass Group 4 — Post-Optimization (`fcn.10b7df57`)

6 passes, post-optimization cleanup. **Binary patching confirmed**: NO pass in
group 4 affects peephole patterns (subfc, subf., eqv, srwi, not).

| Order | Function | Confirmed Effect | Likely Pass |
|-------|----------|-----------------|-------------|
| 1 | `fcn.10ba1316` | No peephole effect | COPYPROP? |
| 2 | `fcn.10b85f52` | No peephole effect | SU_COPYPROP? |
| 3 | `fcn.10bb50cc` | No peephole effect | DEAD_CODE_ELIMINATION? |
| 4 | `fcn.10c04d6d` | **No peephole effect** (NOT G5_SPECIAL) | ? |
| 5 | `fcn.10be6382` | No peephole effect | HOIST_EXCEPT? |
| 6 | `fcn.10c0a034` | No peephole effect | ? |

**Note**: G4P4 (`fcn.10c04d6d`) was originally guessed to be G5_SPECIAL based on its
position in the pass name table. Binary patching **disproved** this — disabling it has
zero effect on NOR, subfc, subf., eqv, or srwi patterns.

## Pass Group 5 — Emission/Cleanup (`fcn.10b7e032`)

10 passes, the largest group. Late-stage passes + emission prep.
**Binary patching confirmed**: ONLY pass 10 affects peephole patterns, and it
generates ALL of them (subfc, subf., eqv, srwi).

| Order | Function | Confirmed Effect | Likely Pass |
|-------|----------|-----------------|-------------|
| 1 | `fcn.10c21b03` | No peephole effect | TRYCATCH_EXCEPTION? |
| 2 | `fcn.10be46f0` | No peephole effect | NEWFP_EXCEPTION? |
| 3 | `fcn.10b3c6e5` | No peephole effect | (shared with group 2 — rerun) |
| 4 | `fcn.10b35c78` | No peephole effect | KEEP_USER_CASTS? |
| 5 | `fcn.10b9d6be` | No peephole effect | SIDE_EFFECT? |
| 6 | `fcn.10b36169` | No peephole effect | FPINLINE_INTRINSIC? |
| 7 | `fcn.10c12099` | No peephole effect | DOUBLETOSINGLE? |
| 8 | `fcn.10b821c3` | No peephole effect | FPSPECIAL? |
| 9 | `fcn.10c275a7` | No peephole effect | SEH_WRITETHRU_OFF? |
| 10 | `fcn.10b3421b` | **PPC CODE GENERATOR** — emits ALL PPC instructions | **CODEGEN** (instruction selection + Xenon scheduler) |

### G5P10 Sub-function Analysis (`fcn.10b3421b`)

382-byte dispatcher calling ~15 sub-functions. Sub-call isolation via binary patching:

| Sub-call | Target | NOP Effect |
|----------|--------|-----------|
| call_22 (0x10b3437d) | `fcn.10b71d8f` | **Removes subf. and eqv** |
| call_10 (0x10b342ba) | `fcn.10b34003` | CRASH (essential) |
| All other direct calls | various | No pattern changes |
| 4 indirect calls | via IAT ptrs | No pattern changes or CRASH |

**`fcn.10b71d8f`** is the **Xenon instruction scheduler** (~2KB function):
- Contains `/QXSTALLS` diagnostic output (instruction scheduling stall annotations)
- Pipeline hazard table at `0x10b12c78` with 11 entries (LHS, BF, LHSUSE, P, MC, S, DA, D, VQF, VQS, VQD)
- Secondary scheduling data at `0x10c2eba4` (per-instruction issue timing)
- Responsible for record-form emission (`subf.`) and `eqv` instruction generation
- References config flag `data.10c3de20` (optimization level) compared to 2

**G5P10 is the PPC code generator**: Disabling it produces ZERO code (empty listing).
It's not a peephole optimizer — it's the entire instruction selection + emission stage.
All PPC-specific patterns (subfc, eqv, srwi, subf., not, rlwinm, etc.) are instruction
selection choices made during code generation, not post-generation peephole transforms.

Sub-functions within G5P10:
- `fcn.10b71d8f` (call_22): Xenon pipeline scheduler — reorders instructions for pipeline
  efficiency, emits `subf.` (record form) and selects between `eqv` alternatives
- `fcn.10b34003` (call_10): Essential setup (crashes when NOP'd) — likely initializes
  the PPC instruction emitter state
- Other sub-functions: IR processing, linked list traversal, cleanup

## Pass Group 6 — VMX/Vector (conditional, `0x10b9c836`)

Only called if `data.10c6f1c8 != 0` (VMX/vector optimization enabled).
Not analyzed — likely irrelevant for DC3 which doesn't use VMX intrinsics.

## Separator Function (`fcn.10bec297`)

Called 142 times across all groups (between every pass).
Simple thread yield point:
1. Check `data.10c37d28` (debug/async mode flag)
2. If set: verify state, release semaphore, Sleep(0), set mode 0x80
3. If not set: return immediately

Source: `e:\bt\278379\vctools\compiler\be\p2\dll.cpp` (found in assert path)

## Key Data Addresses

| Address | Role |
|---------|------|
| `data.10c2e2ec` | Per-pass state flag (zeroed between passes) |
| `data.10c2e2fc` | Global config flag (gates conditional passes) |
| `data.10c3de20` | Optimization level? (compared to 2) |
| `data.10c37d28` | Debug/async mode (gates yield point) |
| `data.10c37d2c` | Codegen mode (set to 4 during emission, 3 in optimizer) |
| `data.10c37d30` | Semaphore handle (for multi-threaded compilation) |
| `data.10c6f1c8` | VMX/vector optimization enabled flag |

## Binary Patching Results Summary

Methodology: Replace first byte of each pass function with `0xC3` (RET), compile
test cases with `/FAcs /Ox /GS-`, check for presence of target PPC instructions.

### Test patterns:
- `subfc`: Boolean materialization (`(bool)(x > 1)`)
- `subf.`: Subtract-and-record fusion (`hi - lo >= 0`)
- `eqv`: Logical equivalence (boolean complement)
- `srwi`: Shift right word immediate (boolean bit extraction)

### Results matrix:

| Pass | subfc | subf. | eqv | srwi | Notes |
|------|-------|-------|-----|------|-------|
| **Original** | YES | YES | YES | YES | Baseline |
| Group 1 disabled | YES | YES | YES | YES | No effect |
| Group 2 disabled | YES | YES | YES | YES | No effect |
| **Group 3 disabled** | YES | **NO** | YES | YES | subf. only |
| Group 4 disabled | YES | YES | YES | YES | No effect |
| **Group 5 disabled** | **NO** | **NO** | **NO** | **NO** | ALL removed |
| G3P2 (`fcn.10c0f14e`) | YES | **NO** | YES | YES | Record-form fusion |
| **G5P10 (`fcn.10b3421b`)** | **NO** | **NO** | **NO** | **NO** | ALL removed |
| G4P4 (`fcn.10c04d6d`) | YES | YES | YES | YES | Not G5_SPECIAL |

### Key conclusions:

1. **Two-stage subf. generation**: G3P2 marks the IL for record-form fusion → G5P10
   emits the actual `subf.` instruction. Disabling either removes `subf.` from output.
2. **Boolean materialization is entirely in G5P10**: `subfc`, `eqv`, `srwi` are all
   generated during final emission/scheduling, not in earlier optimization passes.
3. **G5_SPECIAL name was misleading**: The pass at G4P4 (`fcn.10c04d6d`) in the
   "right position" in the pass name table has NO effect on PPC peephole patterns.
4. **Xenon scheduler generates peephole patterns**: The scheduler sub-function
   (`fcn.10b71d8f`) within G5P10 generates `eqv` and `subf.` as part of instruction
   scheduling. Boolean materialization (`subfc/srwi`) comes from a different sub-function.

## Identification Strategy

Pass names are stored in the table at `0x10C2E980+0x64` but are NOT directly
referenced by pass functions. They're only used for:
1. `/nop*` flag parsing (disable specific passes by name)
2. Diagnostic output (when logging is enabled)

### Confirmed mappings (via binary patching):
- `fcn.10bc6487` → **COLOR** (register allocator, group 1)
- `fcn.10c0f14e` → **Record-form fusion** (group 3, pass 2)
- `fcn.10b3421b` → **XENON_SCHED / Emission** (group 5, pass 10)

### Disproved hypotheses:
- `fcn.10c04d6d` is NOT G5_SPECIAL (no peephole effect)
- NOR (`not`) instruction generation is NOT a pass-level optimization —
  it appears to be generated during instruction selection (codegen), before
  the optimization passes run
