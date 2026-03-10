# MSVC Xbox 360 PPC Compiler — RE Roadmap

## Overview

Reverse-engineer c2.dll (the MSVC PPC back-end, 1.3 MB, ~1,430 functions) to build a
**compiler model** that predicts codegen decisions. This feeds into the synthesis engine
to transform AT_LIMIT decomp work from brute-force search to targeted, model-guided editing.

Source and tools live in `msvc-src/`. Analysis docs are here in the synthesis-engine plan.

## Status Note (2026-03-10)

This document predates the current implementation state. The practical status is:

- differential testing is no longer speculative; `msvc-src/tools/diff_test.py`
  exists and produced the findings in `msvc-src/results/FINDINGS_SUMMARY.md`
- persistent `_CL_*` fixture capture now exists under
  `msvc-src/analysis/il-fixtures/`
- `msvc-src/tools/il_parser.py` exports normalized bundle JSON
- `msvc-src/tools/ppc_il_lifter.py` has started the constrained PPC->IL work
- the IL fixture corpus now includes:
  - `il_type_control_cast_vs_and`
  - `il_bool_materialization`
  - `il_switch_dispatch`
  - `il_call_return`
- the permuter-side target-facts layer can now ingest derived PPC shape facts
  from `/FAcs` listings, with default-on routing through beam + hill + batch
  reporting
- measured batch output now records real fact-driven boosts:
  `switch_if_convert`, `tail_call_reorder`, and `temp_elimination`
- the current blocker is no longer shape extraction; it is generator
  applicability on boosted call-shape targets
- `tail_call_reorder` applicability has been widened for terminal cleanup
  wrappers (`if (ptr) Call(ptr);` / `if (ptr) { Call(ptr); }`), which now
  generates real variants on destructor-style candidates
- a follow-up noise-trimming pass removed most infrastructure-only tail-call
  variants; the current plausible producers are a much smaller subset

That changes the next RE priorities:

1. improve `tail_call_reorder` / call-shape applicability on real targets
2. extend the IL corpus and PPC->IL lift for the next missing call families
3. measure the current default-on path on a larger hard slice
4. keep COLOR RE selective and demand-driven for unanswered allocator questions

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
- **FPMOV_TO_INTMOV** (index 29) — FP<->GPR register transfer decisions
- **NORMALIZE_CASTS** (index 10) — cast handling (boolean materialization trigger)

### Empirical Knowledge (from 87% decomp completion)
We already understand many codegen patterns from the source side. The RE effort
maps these to specific code paths inside c2.dll:

| Pattern | Empirical Knowledge | c2.dll Target |
|---------|--------------------|---|
| Callee-saved GPR order | first decl -> r31, second -> r30 | COLOR pass |
| BSF graph coloring threshold | ~7+ callee-saved vars | COLOR pass |
| NOR peephole | `x ^ 0xFF` on u8 -> NOR | G5_SPECIAL pass |
| Boolean materialization | `(bool)(x > 1)` -> subfc/eqv/srwi | G5_SPECIAL or NORMALIZE_CASTS |
| subf. loop condition | `high - low >= 0` -> subf. | G5_SPECIAL pass |
| Float literal GPR caching | static const float -> GPR for addr | FPMOV_TO_INTMOV |
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

### Phase 0: Ghidra Analysis (DONE — initial pass)
**Goal**: Map c2.dll's high-level structure using Ghidra auto-analysis + string xrefs.

- [x] Load c2.dll into rizin, auto-analyze (~4,746 functions identified)
- [x] Find and name the pass dispatch function -> `fcn.10b7e6af` dispatches to 5 pass groups
- [x] Trace each pass table entry to its implementation function -> 37 unique pass functions
- [x] Name the 4 exported functions and their call trees -> full pipeline traced
- [x] Identify COLOR (register allocator) -> `fcn.10bc6487`, ~207 helper functions
- [ ] Find the IL ingestion code (what data structure does InvokeCompilerPass receive?)
- [x] Export function list -> `msvc-src/analysis/all_functions.txt` (4,746 functions)

**Key findings** (see `msvc-src/docs/PIPELINE.md` for details):
- **35 named optimization passes** in two tables at VA 0x10C2E9D0/0x10C2E9E4
- **5 pass groups** called in sequence per function, containing 37 pass functions
- **COLOR entry** at `fcn.10bc6487` -> 207 functions in the register allocator subsystem
- **Source path found**: `e:\bt\278379\vctools\compiler\be\p2\misc.c`
- **Pipeline**: init -> IL load -> per-function {prep -> optimize -> codegen -> cleanup} -> emit

### Phase 1: Differential Testing Framework
Status: Implemented core harness and first suites
**Goal**: Build empirical codegen decision maps WITHOUT decompiling c2.dll.

Approach: Compile carefully crafted test cases with `/FAcs`, diff the assembly output
to understand what source patterns trigger what codegen changes.

- [x] Build test harness: compile test case -> extract function assembly -> diff
- [x] Test suite for register allocation order (declaration permutations)
- [x] Test suite for inlining thresholds (vary function size)
- [x] Test suite for peephole patterns (NOR, bool materialize, subf.)
- [x] Test suite for branch polarity (if/else ordering)
- [x] Test suite for float precision (literal types, static const)
- [x] rlwinm fusion / IL-type-control suite
- [ ] extend suites for switch lowering and call/return shape

**Deliverable**: Empirical decision maps as JSON: `{source_pattern} -> {asm_pattern}`.
Current outputs exist under `msvc-src/results/*.json`.

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

- [ ] Decompile COLOR entry point -> readable C pseudocode
- [ ] Identify the graph coloring data structures
- [ ] Find the callee-saved assignment loop
- [ ] Decompile G5_SPECIAL -> identify peephole table
- [ ] Document each peephole pattern and its trigger condition

**Deliverable**: Annotated pseudocode for critical passes.

### Phase 3: Compiler Model
Status: Started via atlas, normalized IL bundles, and constrained PPC->IL lifting
**Goal**: Build a predictive model of c2.dll's decisions, usable by the synthesis engine.

The model doesn't need to be a full compiler reimplementation. It needs to answer:
- "Given this IR, which register will variable X get?"
- "Will function Y be inlined into function Z?"
- "Will pattern P trigger peephole Q?"

- [ ] Register allocation predictor (input: variable list + types -> output: register map)
- [ ] Inlining predictor (input: callee size + call context -> output: inline yes/no)
- [ ] Peephole predictor (input: IR pattern -> output: instruction sequence)
- [ ] IL/PPC shape predictor for fused vs separate byte operations
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
├── docs/                    <- Analysis documentation
│   ├── PLAN.md
│   ├── ARCHITECTURE.md
│   ├── PASSES.md            * Complete pass catalog
│   ├── FINDINGS.md          * Initial analysis results
│   └── PRIOR_ART.md
├── tools/                   <- Analysis tools
│   ├── extract_strings.py   * String xref tool (working)
│   └── capture_il.py
├── analysis/                <- Raw analysis data
├── ghidra/                  <- Ghidra project exports
│   ├── scripts/
│   └── exports/
├── model/                   <- Predictive compiler model (Phase 3)
└── src/                     <- Reconstructed source (Phase 2+)
    └── c2/
        ├── regalloc/
        ├── peephole/
        └── codegen/
```

## Current Status

### Completed
- Full pipeline traced from `InvokeCompilerPass` export through to `.obj` emission
- 35 named optimization passes identified and cataloged
- COLOR (register allocator) entry point found at `fcn.10bc6487` with 207 helper functions
- Pass group structure mapped: 5 groups containing 37 unique pass functions
- `/FAcs` assembly listing confirmed working for differential testing
- Source file reference found: `e:\bt\278379\vctools\compiler\be\p2\misc.c`
- Tools built: `extract_strings.py` (string xref), `capture_il.py` (IL capture prototype)

### Blockers
- No PDB symbols (must rely on auto-analysis + manual annotation)
- `/d2` diagnostic flags not supported in our version (v16.00.11886.00)
- Ghidra instance loaded with DC3 binary (need separate project for c2.dll x86 analysis)

### Immediate Next Steps
1. **Fix tail-call applicability on boosted targets**:
   - collect 10-20 `tail_call_reorder` candidates that emit `call_shape`
   - classify which generated variants are actually sensible vs noisy
   - keep expanding the pattern only for real missed tail-position families
2. **Measure the current default-on path on a larger slice**:
   - run `scan_and_permute.py --json` on 20-30 call/switch/temp targets
   - record `codegen_shape`, `fact_boost_counts`, `fact_suppress_counts`
   - compare proposal ordering and wins before vs after shape-fact routing
3. **Lift the next IL families**:
   - virtual dispatch detail: slot load, `bctrl` vs `bctr`, tail vs non-tail
   - argument materialization: temp extraction vs inline arg formation
   - inline wrappers: trivial forwarding calls, cached-return wrappers
   - switch edge cases: sparse tables, default hoists, mixed fallthrough
4. **Add fixtures before adding new routing**:
   every new IL-derived fact should land with a captured `_CL_*` bundle and a
   PPC comparison fixture.
5. **Disassemble COLOR helpers selectively**:
   only for allocator questions still unanswered by the differential harness.

## References

- `msvc-src/docs/PRIOR_ART.md` — all known prior work
- `msvc-src/docs/PASSES.md` — pass table analysis
- `msvc-src/docs/ARCHITECTURE.md` — compiler pipeline
- `docs/decomp/patterns/at-limit-systemic.md` — AT_LIMIT pattern catalog
- `docs/decomp/patterns/unfixable-compiler.md` — confirmed unfixable patterns
