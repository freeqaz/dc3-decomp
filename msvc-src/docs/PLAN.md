# MSVC Xbox 360 PPC Compiler — Reverse Engineering Plan

## Goal

Reverse-engineer the Xbox 360 MSVC cross-compiler (specifically c2.dll, the PPC back-end)
to understand its code generation decisions. The ultimate aim is to produce annotated source
that explains — and eventually replicates — the compiler's behavior, enabling:

1. **Targeted decomp fixes**: Understanding WHY the compiler makes specific choices
   (register allocation order, inlining thresholds, branch polarity, peephole patterns)
2. **Compiler telemetry**: Instrumenting c2.dll to log internal decisions during compilation
3. **Long-term**: A buildable source tree that produces byte-identical output

## Target Binaries

From Xbox 360 XDK, version 16.00.11886.00 (build 78379):

| Binary     | Size   | Role                          | Est. Functions |
|------------|--------|-------------------------------|----------------|
| cl.exe     | 97 KB  | Driver (dispatches to DLLs)   | ~150           |
| c1.dll     | 485 KB | C front-end                   | ~500           |
| c1xx.dll   | 1.7 MB | C++ front-end                 | ~2,000         |
| c2.dll     | 1.3 MB | **PPC back-end / optimizer**  | ~1,430         |
| link.exe   | 594 KB | Linker                        | ~800           |
| msdisXXX.dll| 85 KB | Disassembler library          | ~100           |

**Priority**: c2.dll (the code generator). This is where all codegen decisions happen.
c1xx.dll is secondary (front-end IR generation, template instantiation, inlining prep).

## What We Know

### Architecture
```
Source → c1xx.dll (parse/sema) → IL file → c2.dll (optimize/codegen) → .obj
                                     ↑
              cl.exe orchestrates via InvokeCompilerPass()
```

### c2.dll Exports (only 4)
- `DllGetObjHandler` — returns handler object
- `_InvokeCompilerPass@12` — main entry: receives IL, produces .obj
- `_InvokeCompilerPassW@16` — wide-char variant
- `_AbortCompilerPass@4` — cancel compilation

### Known Internal Passes (from string extraction)
- `COLOR` — register allocation (graph coloring)
- `CONSTANT_FOLDING`
- `DEAD_CODE_ELIMINATION`
- `CODE_MOTION` — loop-invariant code motion
- `COMMON_SUBEXP` — CSE
- `COPYPROP` — copy propagation
- `PARTIAL_RED_ELIMINATION` — PRE
- `NORMALIZE_CASTS`
- `DOUBLETOSINGLE` — float precision demotion
- `HOIST_EXCEPT` — exception hoisting
- `CONTRACTION` — FMA contraction
- `FACTORING_DISTRIBUTION` / `FACTORING_INVERSE`
- `FPMOV_TO_INTMOV` — FP→int register transfer optimization
- `MUL_DIV_BY_ONE` — strength reduction
- `CTIME_EVAL` — compile-time evaluation
- `KEEP_USER_CASTS`
- `AVOID_BADFPENTERLOADS`

### PDB Info (no public symbols available)
- c2.dll: `78379\vctools\compiler\be\p2\c2\obj\i386\c2.pdb` — GUID `0D71D537...`
- c1xx.dll: `79\vctools\compiler\fe\cxx\obj\i386\c1xx.pdb` — GUID `10A9BECC...`
- PDBs NOT on Microsoft's public symbol server (Xbox SDK internal)

### PPC-Specific Features
- All PPC instruction mnemonics embedded as strings (assembler/disassembler)
- VMX (Altivec/vector) save/restore helpers: `__restvmx_100` through `__restvmx_124`
- `/QVMXReserve` flag for vector register reservation
- `nopvmxperm`, `nopvmxsimp` — VMX optimization controls
- Estimated VMX IPC calculation in diagnostics

### Compiler Lineage
- Derived from MSVC PowerPC cross-compiler (WinCE PPC → Xbox 360 fork)
- Original PPC support: VC++ 4.x for NT4 PowerPC (1996), then WinCE cross-compilers
- Xbox 360 fork modernized ~2003-2005 on top of VS 2005+ infrastructure
- This version (16.00) corresponds to VS 2010 era front-end with PPC-specific back-end

## Prior Art

### Geoff Chappell's MSVC Studies (gold standard)
- Documented module pipeline, C2 options, CL dispatching for VS .NET 2003 (v13.00)
- URL: geoffchappell.com/studies/msvc/
- Our version is 16.00 — architecture likely similar, implementation evolved

### Lectem's Hidden Flags (VS 2017)
- Extracted all `/d1` and `/d2` undocumented flags via IDA/OllyDbg
- Flag names map internal subsystems: inlining, devirtualization, vectorization, PGO
- URL: lectem.github.io/msvc/reverse-engineering/build/2019/01/21/MSVC-hidden-flags.html

### assarbad/msvc-undoc
- Structured YAML database of undocumented MSVC/linker options
- GitHub: assarbad/msvc-undoc

### Microsoft Phoenix (defunct)
- Research compiler framework that shared c2.dll's back-end
- April 2008 CTP SDK — if obtainable, contains interface definitions
- Discontinued ~2008, never shipped as production replacement

### microsoft/microsoft-pdb
- Partial PDB source on GitHub — reveals internal data structures

### XenonRecomp
- Understands MSVC PPC codegen patterns for static recompilation
- Recognizes jump tables, save/restore calls, MSVC PPC idioms

## Approach: Phased, Targeted

### Phase 0: Setup & String Mining (Week 1)
- [x] Extract version info, exports, string tables
- [x] Identify PDB GUIDs, check symbol server (404 — not available)
- [ ] Load all binaries into Ghidra, auto-analyze
- [ ] Export Ghidra function list with addresses and sizes
- [ ] Cross-reference strings to functions (which function references "COLOR"?)
- [ ] Enumerate all `/d2` flags in our version specifically
- [ ] Document the IL file format (intercept with `/B2` flag replacement)

### Phase 1: Architecture Mapping (Weeks 2-3)
- [ ] Map `InvokeCompilerPass` → identify the main pipeline dispatch
- [ ] Identify pass ordering (likely a table/array of function pointers)
- [ ] Name all optimization pass entry points from string references
- [ ] Map the IR/IL data structures (nodes, operands, basic blocks)
- [ ] Identify the instruction encoding tables (PPC opcode → binary)

### Phase 2: Targeted Deep Dives (Weeks 4-8)
Focus areas ordered by impact on DC3 decomp:

#### 2a. Register Allocator ("COLOR")
- Map the graph coloring implementation
- Find the callee-saved assignment heuristic (first-decl → r31)
- Find the BSF graph coloring threshold (~7 callee-saved vars)
- Understand spill cost calculations

#### 2b. Inlining Decisions
- Find the "too big" threshold
- Map the inlining budget calculation
- Understand how function body size affects other functions' inlining
- Find the "always inline" / "never inline" logic

#### 2c. Instruction Selection & Peepholes
- Map the peephole optimization table
- Find the NOR pattern (xor 0xFF → NOR)
- Find boolean materialization patterns (subfc/eqv/srwi vs cmpwi/ble)
- Find the `subf.` loop condition pattern
- Find branch polarity decision logic

#### 2d. Prologue/Epilogue Generation
- How are `__savegprlr_N` calls chosen?
- Stack frame layout algorithm
- VMX register save decisions

### Phase 3: Instrumentation (Weeks 8-12)
- Hook c2.dll via DLL injection or `/B2` replacement wrapper
- Log register allocation decisions per function
- Log inlining decisions with cost metrics
- Log peephole pattern matches
- Feed this telemetry back into the DC3 decomp pipeline

### Phase 4: Source Reconstruction (Ongoing)
- Decompile targeted functions into readable C
- Build a compilable source tree (initially just the passes we understand)
- Validate by comparing output against known input/output pairs

## Key Advantages We Have

1. **Massive empirical dataset**: 34,000+ functions with known input (C++ source) and
   output (PPC assembly). This is essentially a test suite for the compiler.
2. **Frozen target**: This compiler version will never change. We can take our time.
3. **x86 target binary**: c2.dll is x86, which Ghidra handles well.
4. **Rich diagnostics**: The compiler has extensive internal logging (INL/ERR/WRN strings).
5. **Small-ish scope**: c2.dll is only 1.3 MB / ~1,430 functions.
6. **Known pass names**: String extraction reveals the optimization pipeline structure.

## Key Challenges

1. **No PDB symbols**: Must rely purely on Ghidra auto-analysis + manual RE
2. **Optimized binary**: c2.dll itself was compiled with optimizations
3. **Undocumented IL format**: Must reverse-engineer the front-end → back-end interface
4. **Self-referential difficulty**: Understanding an optimizing compiler that was itself optimized
5. **Scale**: Even 1,430 functions is substantial for manual RE
6. **Never been done**: No public project has successfully decompiled a production compiler back-end

## Feasibility Assessment

**Full decompilation**: Multi-person-year effort. Not realistic short-term.

**Targeted subsystem RE**: Very feasible. Understanding register allocation heuristics,
inlining thresholds, and specific peephole patterns is achievable in weeks, not years.
Each subsystem is ~50-200 functions, and we have thousands of test cases to validate
our understanding.

**Instrumentation via hooking**: Most practical near-term win. Wrap c2.dll calls to
capture internal state without fully understanding the code. This could immediately
improve the DC3 decomp workflow.

## File Organization

```
msvc-src/
├── docs/
│   ├── PLAN.md              — this file
│   ├── ARCHITECTURE.md      — compiler pipeline architecture
│   ├── IL_FORMAT.md         — front-end IL format documentation
│   ├── PASSES.md            — optimization pass catalog
│   └── PRIOR_ART.md         — links and summaries of prior work
├── analysis/
│   ├── strings/             — extracted string tables, cross-references
│   ├── exports/             — export analysis
│   └── functions/           — per-function notes from Ghidra RE
├── ghidra/
│   ├── scripts/             — Ghidra analysis scripts
│   └── exports/             — exported function lists, call graphs
├── tools/
│   ├── il_intercept/        — IL file capture tool (/B2 replacement)
│   ├── c2_hook/             — DLL injection/hooking for telemetry
│   └── string_xref.py       — string-to-function cross-reference tool
└── src/                     — reconstructed source (eventual)
    ├── c2/
    │   ├── regalloc/        — register allocator
    │   ├── inline/          — inliner
    │   ├── peephole/        — peephole optimizations
    │   └── codegen/         — instruction selection + emission
    └── common/              — shared data structures
```
