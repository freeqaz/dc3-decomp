# Unicorn Runner Performance Profiling

## Problem
`ninja test-unicorn` (971 units, dual-fixture, -j8) is too slow for practical CI/dev use.

## Context & Related Docs

### Architecture & Design
- [UNICORN_FUNCTION_RUNNER.md](../tools/UNICORN_FUNCTION_RUNNER.md) — Main design doc: phases, memory map, components, CLI usage
- [PHASE1_DESIGN.md](../archive/2026-08-17-doc-audit/experiments/PHASE1_DESIGN.md) — Deep dive into COFF extraction, relocation patching (5 types), execution engine, comparator. **Archived 2026-08-17** — this is the pre-build design document, not the runner's documentation; see [tools/UNICORN_FUNCTION_RUNNER.md](../tools/UNICORN_FUNCTION_RUNNER.md)

### Strategy & Value
- [unicorn-roadmap.md](unicorn-roadmap.md) — Strategic assessment: what works (false positive filtering), what doesn't (bug localization), improvement tiers
- [unicorn-runner-value.md](unicorn-runner-value.md) — Real-world examples: UITransitionHandler saved 11 functions, Profile saved 6/7

### Research & Stats
- [2026-02-11-unicorn-phase3-research.md](../sessions/2026-02-11-unicorn-phase3-research.md) — Scope stats: 21,804 eligible (84.9%), 3,704 blocked by bctrl, ~75% equivalence rate

### Unicorn Engine Fork
- `~/code/milohax/unicorn/` — Our fork of the Unicorn CPU emulator (PPC32 BE backend). Relevant for investigating engine-level performance (Uc() init cost, memory mapping overhead, hook dispatch)

### Source Code
- Entry point: `scripts/unicorn_runner/run.py` — CLI, batch orchestration, multiprocessing
- COFF parser: `scripts/unicorn_runner/coff.py` — PE/COFF header/section/symbol/reloc parsing
- Extraction: `scripts/unicorn_runner/extractor.py` — Function bytes + relocs from decomp/original .obj
- Execution: `scripts/unicorn_runner/engine.py` — Unicorn PPC32 BE emulation (~90us/function)
- Build pipeline: `scripts/unicorn_runner/builder.py` — Relocation patching, switch table prep
- Co-loading: `scripts/unicorn_runner/coloader.py` — Intra-TU callee discovery + layout
- Caching: `scripts/unicorn_runner/cache.py` — Result cache for batch runs
- Diagnostics: `scripts/unicorn_runner/diagnose.py` — Dual-fixture diagnosis, SKIP/FIX recommendations

## Suspected Bottlenecks (to verify with profiling)

### 1. objdiff.json re-parse per worker
- `get_all_units()` reads/parses the full objdiff.json once in main
- But each `_process_unit` worker calls `run_batch()` which calls `list_functions()` → full COFF parse + symbol iteration per unit
- objdiff.json itself is only read once, but COFF parsing happens per-unit (expected)

### 2. COFF parsing overhead
- `COFFParser.__init__()` in `coff.py` reads entire .obj file + parses all sections/symbols/relocations
- Called twice per unit (decomp + orig), each worker does this independently
- For 971 units × 2 sides = 1,942 COFF parses

### 3. Dual-fixture doubles execution
- Every function runs twice (zero fill + 0xCD fill) in dual-fixture mode
- Unicorn engine init/teardown happens per execution (`execute_function` in `engine.py` creates a fresh `Uc()` each call)
- Engine creation overhead may dominate for small functions

### 4. Unicorn engine per-function overhead
- `execute_function()` creates a new Uc() instance every call
- Maps memory, writes code, sets registers, runs, reads back — all per function
- Could reuse engine with memory reset between functions in same unit

### 5. list_functions() scans all symbols twice
- Iterates all symbols in both COFFs to find .text symbols
- Then extracts + classifies every common symbol even though `run_batch()` will do this again

## Optimization Ideas

### Quick wins
- **Reuse Unicorn engine within a unit**: Reset memory instead of creating new Uc() per function
- **Skip dual-fixture for cached results**: Already done, but cache miss path is 2×
- **Eliminate list_functions() in batch**: `run_batch()` could iterate symbols directly without the intermediate list step
- **Lazy classify_indirect_branch in list_functions**: Currently classifies but doesn't use the result

### Medium effort
- **Pre-filter objdiff.json**: Only pass units that have both .obj files present (skip missing at main level)
- **Serialize COFF parse results**: Cache parsed COFF data to avoid re-parsing unchanged .obj files
- **Reduce worker count for I/O bound phase**: COFF parsing is I/O bound, execution is CPU bound

### Larger refactors
- **Streaming architecture**: Parse COFF → yield functions → execute, instead of list-then-execute
- **Batch Unicorn execution**: Map all functions in a unit at once, execute sequentially with memory resets
- **Profile-guided skip list**: Skip functions known to be equivalent from previous runs (cache already does this partially)

## Profiling Plan
1. Run 10-unit subset with `time` and `cProfile` to get function-level timing
2. Identify whether COFF parsing, Unicorn init, or execution dominates
3. Apply targeted fix based on results
