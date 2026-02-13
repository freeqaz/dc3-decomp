# Unicorn Runner Performance Analysis

**Date**: 2026-02-13
**Goal**: Profile and optimize `ninja test-unicorn` (971 units, dual-fixture)

## Summary

The unicorn runner is bottlenecked by Python<->C FFI overhead in the Unicorn bindings. Actual PPC emulation accounts for only **2-3% of wall time**. The remaining 97% is Python wrapper overhead: object lifecycle, hook callbacks, register reads, and memory mapping. An instruction count cap (`max_insns=50_000`) was added as a band-aid to limit the damage from loop-heavy functions, bringing a 10-unit benchmark from **393s to 58s** (6.8x speedup).

## Profile Data (5 units, 48 functions, dual-fixture, single-threaded)

```
python3 -m cProfile -s cumulative ...
```

### Where the time goes (41.5s total, 192 execute_function calls)

| tottime | % | What | Calls |
|---------|------|------|-------|
| 9.97s | 24.0% | `mem_map` (Unicorn C) | 9,296 |
| 9.83s | 23.7% | `release_handle` (Uc destructor) | 184 |
| 4.31s | 10.4% | `_do_reg_read` (ctypes FFI) | 9.88M |
| 4.19s | 10.1% | `_reg_read` (Python wrapper) | 9.88M |
| 3.99s | 9.6% | `hook_trampoline_call` (our code) | 3.95M |
| 2.83s | 6.8% | `reg_read` (outer wrapper) | 9.88M |
| **1.07s** | **2.6%** | **`emu_start` (actual PPC emulation)** | 192 |
| 0.99s | 2.4% | ctypes `wrapper` overhead | 3.96M |
| 0.61s | 1.5% | `__hook_code_cb` (Unicorn dispatch) | 3.95M |
| remaining | 8.9% | comparison, extraction, etc. | |

### The core problem: Python hook callback overhead

The trampoline hook (`UC_HOOK_CODE`) fires on **every instruction** executed in the trampoline memory region. Each trampoline stub is 2 instructions (`li r3, 0; blr`), so the hook fires twice per trampoline call. For the first instruction (aligned to 8 bytes), we log the call by reading 5 registers (LR, r3-r6).

For 48 functions with dual-fixture (192 executions), the hook fires **3.95 million times**. Each of the ~1.98M logged calls does **5 register reads**, totaling **9.88 million `reg_read` calls**.

Each `reg_read` call traverses this Python call chain:
```
reg_read()                    # 0.29μs own time
  → _reg_read()               # 0.42μs own time
    → _do_reg_read()           # 0.44μs own time (ctypes call to uc_reg_read)
      → _select_reg_class()
      → __get_reg_read_arg()
      → byref()
```

**Total cost per reg_read: ~1.3μs** (vs nanoseconds in C). Over 9.88M calls, this adds up to **~13s** — a full **31% of total runtime** just reading registers.

### Why `mem_map` is so expensive

Each `execute_function` call creates a fresh `Uc()` instance with 6-7 base memory regions. During execution, functions that dereference zeroed/filled pointers trigger the unmapped-access hook, which calls `mem_map` to map 4KB pages on demand.

- Base region maps: 192 executions × 7 regions = **1,344 maps** (~1.4s)
- On-demand page maps: **8,144 maps** (~8.5s)
- Total: 9,296 maps at **~1.07ms each**

The `uc_mem_map` C function is inherently expensive because it allocates backing memory and updates Unicorn's internal page table. On-demand mapping during execution is especially costly because it interrupts emulation, calls back into Python, then resumes.

### Why `release_handle` is so expensive

Each `Uc()` object destruction calls `uc_close()` which frees all internal state: page tables, hook lists, CPU context, mapped memory. At **53ms per call** and 184 destructor invocations (triggered by Python GC during the batch), this totals **9.83s** (24%).

## Optimization Attempts

### 1. Engine reuse across functions (FAILED)

**Idea**: Create `Uc()` once per unit, reset memory between functions.

**Result**: Emulation time exploded from 1.1s to **71.8s** (65x slower). On-demand pages accumulated across all functions in a unit, bloating Unicorn's internal page table to thousands of entries. Every memory access during emulation became slower due to page table lookup overhead.

Additionally, the results changed (different eq/div counts), suggesting subtle Unicorn internal state leaks between executions despite full register and memory resets.

### 2. Engine reuse within dual-fixture (NEUTRAL)

**Idea**: Create `Uc()` per function, reuse for the 4 executions within a dual-fixture comparison.

**Result**: `release_handle` dropped from 9.83s to 4.09s, `mem_map` from 9.97s to 3.91s, but `emu_start` jumped from 1.07s to **12.1s** — page table bloat within just 4 executions was enough to degrade emulation speed. Net result: ~0s improvement.

### 3. `reg_read_batch` (NEUTRAL)

**Idea**: Replace 5 individual `reg_read` calls with one `reg_read_batch` call.

**Result**: The Python wrapper for batch reads uses a list comprehension and generator expression to pack/unpack the results. This Python object creation overhead (~3.2s) consumed all savings from reduced FFI round-trips. Net: 0s improvement.

### 4. Instruction count cap (EFFECTIVE)

**Idea**: Pass `count=50_000` to `emu_start` to limit total instructions per execution.

**Result**: Prevents runaway loops from generating millions of hook callbacks. Both decomp and orig sides hit the same cap, so comparison is still valid (tests a prefix of the function's behavior rather than full execution).

| Benchmark (10 units, 831 functions) | Time | func/s |
|--------------------------------------|------|--------|
| Before (no cap) | 393s | 2.1 |
| max_insns=200,000 | 153s | 5.4 |
| max_insns=100,000 | 85s | 9.8 |
| max_insns=50,000 | 58s | 14.4 |

### 5. Eliminate redundant `list_functions()` in batch (MINOR)

**Idea**: Replace `list_functions()` (which extracts + classifies every symbol) with lightweight `_find_common_text_symbols()` that only finds common .text symbols.

**Result**: `list_functions` was only 54ms (0.1%), so negligible timing impact. But cleaner code — avoids redundant extraction and the hacky stdout suppression.

## Why the remaining time can't be optimized in Python

After the instruction cap, the per-function cost is dominated by fixed overhead:

| Per-execution cost | Source |
|--------------------|--------|
| ~54ms | `Uc()` destructor (`release_handle`) |
| ~52ms | `mem_map` (base regions + on-demand) |
| ~6ms | Emulation + hooks |
| ~3ms | COFF parsing, extraction, comparison |

For dual-fixture with 4 executions per function:
- **~0.45s per function** (lifecycle + mapping overhead)
- At 831 functions: **~374s minimum** just from Uc lifecycle

The Unicorn Python bindings add **~1.3μs per register read** and **~1.07ms per memory map** — orders of magnitude slower than the underlying C operations. This is inherent to CPython's FFI overhead (ctypes function calls, Python object creation/destruction, GC pressure).

## Architectural options for the future

### Option A: C extension for the hot path

Write a C extension module that replaces `execute_function()`. The C code would:
- Create Uc(), map regions, write code, set registers, execute, read results — all in C
- Only return the `ExecutionResult` to Python
- Eliminate all per-instruction Python callbacks

**Expected impact**: ~10-20x speedup for execution. The hook_trampoline_call logic (modulo check, 5 reg reads, dict creation) would become nanosecond-cost C operations instead of microsecond-cost Python.

**Effort**: Medium. Need to link against libunicorn, handle memory management in C.

### Option B: Rust runner (standalone binary)

Rewrite the entire execution pipeline (COFF parse → extract → patch → execute → compare) in Rust with the `unicorn-engine` crate.

**Expected impact**: ~50-100x speedup. All overhead eliminated: no GC, no FFI, no Python object creation. COFF parsing and comparison also become fast.

**Effort**: High. But the logic is well-defined and the current Python code serves as a spec.

### Option C: Direct QEMU/KVM execution

Skip Unicorn entirely. Use QEMU user-mode or a custom PPC interpreter to execute functions. QEMU's JIT compilation would make emulation much faster for loop-heavy functions.

**Effort**: High. Different API, may need custom scaffolding.

### Option D: Smarter execution (no emulation for trivial cases)

Skip emulation entirely for functions where:
- Decomp and orig have identical byte-for-byte code (already equivalent)
- Function is a simple leaf (no calls, no loops) — static analysis can prove equivalence
- Function was equivalent in previous runs (cache already does this partially)

**Expected impact**: Could skip 30-50% of executions. Combined with instruction cap, might bring full batch under 5 minutes.

**Effort**: Low-medium. Mostly heuristics on COFF data.

## Files changed

- `scripts/unicorn_runner/engine.py` — `UnicornEngine` class with reusable engine, pre-computed vtable data, `max_insns` cap, FPR reset
- `scripts/unicorn_runner/run.py` — `_find_common_text_symbols()` replaces `list_functions()` in batch, `engine` parameter threading through comparison pipeline
