# MSVC Xbox 360 PPC Compiler — RE Roadmap

## Overview

Reverse-engineer c2.dll (the MSVC PPC back-end, 1.3 MB, ~1,430 functions) to build a
**compiler model** that predicts codegen decisions. This feeds into the synthesis engine
to transform AT_LIMIT decomp work from brute-force search to targeted, model-guided editing.

Source and tools live in `msvc-src/`. Analysis docs are here in the synthesis-engine plan.

## Target Binary

| Property | Value |
|----------|-------|
| File | `build/compilers/X360/16.00.11886.00/c2.dll` |
| Size | 1,347,072 bytes |
| Format | PE32 (x86) |
| Version | 16.00.11886.00 (build 78379) |
| Functions | ~1,430 |
| Exports | 4 (`DllGetObjHandler`, `InvokeCompilerPass`, `InvokeCompilerPassW`, `AbortCompilerPass`) |
| PDB | Not available (Xbox SDK internal, 404 on symbol server) |
| Source path | `vctools\compiler\be\p2\c2\` |

## What We Know Already

### Optimization Pass Table (discovered)
30 named passes at `.data` RVA `0x0012E9E4`. Full catalog in `msvc-src/docs/PASSES.md`.

Critical passes for DC3:
- **COLOR** (index 14) — register allocation via graph coloring
- **G5_SPECIAL** (index 19) — PPC/Xenon-specific peephole optimizations
- **DOUBLETOSINGLE** (index 26) — float precision demotion
- **FPMOV_TO_INTMOV** (index 29) — FP↔GPR register transfer decisions
- **NORMALIZE_CASTS** (index 10) — cast handling (boolean materialization trigger)

### Empirical Knowledge (from 87% decomp completion)
We already understand many codegen patterns from the source side. The RE effort
maps these to specific code paths inside c2.dll:

| Pattern | Empirical Knowledge | c2.dll Target |
|---------|--------------------|----|
| Callee-saved GPR order | first decl → r31, second → r30 | COLOR pass |
| BSF graph coloring threshold | ~7+ callee-saved vars | COLOR pass |
| NOR peephole | `x ^ 0xFF` on u8 → NOR | G5_SPECIAL pass |
| Boolean materialization | `(bool)(x > 1)` → subfc/eqv/srwi | G5_SPECIAL or NORMALIZE_CASTS |
| subf. loop condition | `high - low >= 0` → subf. | G5_SPECIAL pass |
| Float literal GPR caching | static const float → GPR for addr | FPMOV_TO_INTMOV |
| Inlining budget | body size affects neighbors | Inliner (pre-pass) |
| Branch polarity | beq vs bne for if/else | Codegen (post-COLOR) |

### Assembly Listing
`/FAcs` produces full annotated PPC assembly with machine code bytes, source line
references, and symbol names. Usable for differential testing without any RE.

### Internal Diagnostics
Rich logging infrastructure: `INL:`, `INF:`, `ERR:`, `WRN:`, `OPT:` prefixed format
strings. The inliner alone has 15+ diagnostic messages. Not CLI-accessible in our version
but reachable via binary patching or DLL hooking.

## Phases

### Phase 0: Ghidra Analysis (Current)
**Goal**: Map c2.dll's high-level structure using Ghidra auto-analysis + string xrefs.

- [ ] Load c2.dll into Ghidra, run auto-analysis
- [ ] Find and name the pass dispatch function (references pass table at 0x0012E9E4)
- [ ] Trace each pass table entry to its implementation function
- [ ] Name the 4 exported functions and their call trees
- [ ] Find the IL ingestion code (what data structure does InvokeCompilerPass receive?)
- [ ] Export annotated function list as `msvc-src/ghidra/exports/c2_functions.json`

**Deliverable**: Named function map with pass entry points identified.

### Phase 1: Differential Testing Framework
**Goal**: Build empirical codegen decision maps WITHOUT decompiling c2.dll.

Approach: Compile carefully crafted test cases with `/FAcs`, diff the assembly output
to understand what source patterns trigger what codegen changes.

- [ ] Build test harness: compile test case → extract function assembly → diff
- [ ] Test suite for register allocation order (declaration permutations)
- [ ] Test suite for inlining thresholds (vary function size)
- [ ] Test suite for peephole patterns (NOR, bool materialize, subf.)
- [ ] Test suite for branch polarity (if/else ordering)
- [ ] Test suite for float precision (literal types, static const)

**Deliverable**: Empirical decision maps as JSON: `{source_pattern} → {asm_pattern}`.

### Phase 2: Targeted Decompilation
**Goal**: Decompile the specific c2.dll functions that implement our critical passes.

Priority order (by impact on AT_LIMIT count):
1. **COLOR** (~1,218 functions blocked by regswap)
   - Register assignment heuristic
   - Spill cost formula
   - BSF threshold
2. **G5_SPECIAL** (~200+ functions blocked by peephole differences)
   - Peephole pattern table
   - Pattern matching logic
   - Replacement rules
3. **Inliner** (~unknown, but affects many functions indirectly)
   - Size threshold
   - Budget calculation
   - Cross-function effects
4. **Branch codegen** (~751 functions blocked by control flow)
   - Branch polarity selection
   - If/else vs conditional move decisions

- [ ] Decompile COLOR entry point → readable C pseudocode
- [ ] Identify the graph coloring data structures
- [ ] Find the callee-saved assignment loop
- [ ] Decompile G5_SPECIAL → identify peephole table
- [ ] Document each peephole pattern and its trigger condition

**Deliverable**: Annotated pseudocode for critical passes.

### Phase 3: Compiler Model
**Goal**: Build a predictive model of c2.dll's decisions, usable by the synthesis engine.

The model doesn't need to be a full compiler reimplementation. It needs to answer:
- "Given this IR, which register will variable X get?"
- "Will function Y be inlined into function Z?"
- "Will pattern P trigger peephole Q?"

- [ ] Register allocation predictor (input: variable list + types → output: register map)
- [ ] Inlining predictor (input: callee size + call context → output: inline yes/no)
- [ ] Peephole predictor (input: IR pattern → output: instruction sequence)
- [ ] Integrate into permuter as constraint oracle

**Deliverable**: Python module `msvc-src/model/` importable by the permuter.

### Phase 4: Instrumentation (Optional)
**Goal**: Hook c2.dll at runtime to capture actual decisions during compilation.

- [ ] Build a c2.dll wrapper DLL (intercepts InvokeCompilerPass)
- [ ] Log register allocation decisions per function
- [ ] Log inlining decisions with cost metrics
- [ ] Log peephole pattern matches
- [ ] Feed telemetry into the model for validation

This phase is optional — it provides ground truth for the model but requires DLL
injection infrastructure (feasible under wibo).

## Impact Projection

| Phase | AT_LIMIT Reduction | Timeline |
|-------|-------------------|----------|
| Phase 0 (Ghidra) | 0 (foundation) | 1-2 days |
| Phase 1 (Diff testing) | ~50-100 functions | 1-2 weeks |
| Phase 2 (Targeted RE) | ~200-500 functions | 2-4 weeks |
| Phase 3 (Compiler model) | ~500-1000 functions | 4-8 weeks |
| Phase 4 (Instrumentation) | validation only | optional |

Conservative estimate: Phases 0-2 could move ~300 functions from AT_LIMIT to COMPLETE
by revealing the exact source patterns needed for specific codegen paths.

## File Layout

```
msvc-src/
├── docs/                    ← Analysis documentation
│   ├── PLAN.md
│   ├── ARCHITECTURE.md
│   ├── PASSES.md            ★ Complete pass catalog
│   ├── FINDINGS.md          ★ Initial analysis results
│   └── PRIOR_ART.md
├── tools/                   ← Analysis tools
│   ├── extract_strings.py   ★ String xref tool (working)
│   └── capture_il.py
├── analysis/                ← Raw analysis data
├── ghidra/                  ← Ghidra project exports
│   ├── scripts/
│   └── exports/
├── model/                   ← Predictive compiler model (Phase 3)
└── src/                     ← Reconstructed source (Phase 2+)
    └── c2/
        ├── regalloc/
        ├── peephole/
        └── codegen/
```

## References

- `msvc-src/docs/PRIOR_ART.md` — all known prior work
- `msvc-src/docs/PASSES.md` — pass table analysis
- `msvc-src/docs/ARCHITECTURE.md` — compiler pipeline
- `docs/decomp/patterns/at-limit-systemic.md` — AT_LIMIT pattern catalog
- `docs/decomp/patterns/unfixable-compiler.md` — confirmed unfixable patterns
