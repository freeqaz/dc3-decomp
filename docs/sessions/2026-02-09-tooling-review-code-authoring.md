# Tooling Review: Code-Authoring Phase Options (2026-02-09)

## Context

Review and fact-check of `docs/sessions/2026-02-08-decomp-tooling-options-for-code-writing.md`, which proposed 7 external tools for the C++ code-authoring phase. This document captures research findings, corrected assessments, and concrete next steps.

### Project-specific constraints

- **Compiler**: MSVC for Xbox 360 (`cl.exe` v16.00.11886.00 via wibo on Linux)
- **Architecture**: PowerPC 64-bit big-endian (Xenon), with VMX128 custom SIMD extensions (128 vector registers, 62 non-standard instructions)
- **Object format**: COFF (not ELF)
- **Language**: C++ (not C)
- **Ecosystem position**: DC3 is the only known Xbox 360 matching decompilation project. No community infrastructure exists for this target.

---

## 1) C++ Permuter: Value and Lifecycle Position

### What exists today

- **decomp-permuter** ([simonlindholm/decomp-permuter](https://github.com/simonlindholm/decomp-permuter)): The standard tool in the GC/Wii/N64 community. 177 stars, actively maintained. Supports PowerPC via Metrowerks CodeWarrior.
- **Status in DC3**: Already evaluated and **archived** (`docs/tools/permuter.md`) as "C only, not C++ compatible." Three incompatibilities identified:
  1. Uses pycparser (C-only parser) -- cannot handle C++ classes, templates, virtual functions
  2. Expects ELF object format -- DC3 produces COFF
  3. Assumes gcc/mwcc build syntax -- DC3 uses MSVC `/Fo` flags
- **Experimental C++ Permuter**: Listed in `docs/tools/INDEX.md` as in-progress. Works with the existing objdiff pipeline.

### Why a permuter matters (even with AI assistance)

The AI-assisted orchestrator loop (Claude + objdiff) works well for structural code authoring but has economics that don't favor late-stage matching:

- **Cost**: Each AI iteration consumes tokens for context, analysis, and generation. For a function at 95%+ where you're tweaking expression forms and declaration order, this is expensive.
- **Speed**: AI round-trips take 30-60 seconds. A permuter can test hundreds of variants per minute against objdiff.
- **Determinism**: A permuter systematically explores a defined transformation space. AI exploration is heuristic and may re-try the same patterns.

### Where it fits in the decomp lifecycle

| Match % | Primary tool | Permuter role |
|---------|-------------|---------------|
| 0-30% | Ghidra/m2c + AI | Not useful -- structure is wrong |
| 30-70% | AI orchestrator | Not useful -- too many structural issues |
| 70-90% | AI orchestrator | Marginal -- AI handles this well |
| 90-95% | AI + permuter | **Highest value** -- AI identifies the shape, permuter finds the exact expression form |
| 95-100% | Permuter-first | **Primary tool** -- systematic exploration of bool masks, cast forms, declaration ordering faster and cheaper than AI |

### What to build

A C++ permuter needs to solve the three decomp-permuter incompatibilities:

1. **C++ parsing**: Use clang AST or text-based pattern rewriting (not pycparser). Constrained transformations only:
   - Bool shaping (`!!x`, `x != 0`, `x > 0` for unsigned)
   - Signed/unsigned comparison forms
   - Guard placement/hoisting
   - Declaration and temporary-lifetime ordering
   - Expression splitting/combining
   - Ternary vs. if/else
2. **COFF + objdiff scoring**: Score variants using the existing `objdiff-cli diff` pipeline instead of objdump
3. **MSVC build integration**: Compile via the existing ninja/wibo/cl.exe toolchain

### Effort estimate

- Minimal prototype (text-based rewriting, 5-10 rules, objdiff scoring): 1-2 weeks
- Production quality (clang AST, 20+ rules, parallel evaluation): 4-8 weeks

---

## 2) BinDiff/BinExport: Structural Correlation

### What it is

[BinDiff](https://github.com/google/bindiff) compares two binaries at the function level using graph-based matching algorithms. It works with IDA Pro, Ghidra, or Binary Ninja via [BinExport](https://github.com/google/binexport) protocol buffers.

**PPC support**: Yes -- BinDiff is architecture-agnostic; it operates on the structural representation exported by the disassembler. Since Ghidra already analyzes the DC3 binary with PPC support, BinDiff works out of the box.

### What it does for this project

| Use case | How BinDiff helps |
|----------|-------------------|
| **Cross-version comparison** | Compare DC3 binary against RB3 Wii binary to find structurally similar functions in the shared Milo engine |
| **Progress triage** | Identify functions where decomp output is structurally close but instruction-level divergent -- these are good permuter candidates |
| **Regression detection** | After source changes, verify that function-level structure hasn't regressed even if instruction match % shifts |
| **Intent recovery** | When Ghidra/m2c output is ambiguous, find structurally analogous already-matched functions to guide source shape |

### What it doesn't do

- Does not compare at instruction level (that's objdiff's job)
- Does not generate source code
- Cannot tell you *why* two functions differ, only *that* they differ and *how similar* their structure is

### Integration path

1. Install BinExport Ghidra plugin
2. Export DC3 binary functions to BinExport format
3. Export decomp build outputs (or RB3 binary) to BinExport format
4. Run `bindiff` to generate similarity scores
5. Script to map BinDiff similarity scores onto objdiff function list

**Effort**: 1-2 days for basic integration, 1 week for scripted pipeline.

---

## 3) QEMU / Unicorn: Fast Execution Backend

### The proposal (corrected)

The original doc proposed "QEMU TCG plugin tracer as secondary runtime backend." This framing is wrong -- QEMU cannot run Xbox 360 binaries (no Xenon machine model exists). But the underlying need is real: **a fast execution backend for testing permuted code variants**.

### Three options, different tradeoffs

**Option A: Unicorn Engine** ([unicorn-engine.org](https://www.unicorn-engine.org/)) -- *Most practical for permuter integration*

A CPU emulator library based on QEMU, designed for executing isolated code snippets. Unlike QEMU, Unicorn doesn't need a full system image -- you call `uc_open()`, `uc_mem_map()`, `uc_mem_write()`, `uc_reg_write()`, `uc_emu_start()`, `uc_reg_read()`.
- PPC support added in Unicorn 2.x
- Standard AltiVec/VMX supported via QEMU's PPC backend
- VMX128 **not** supported (would need to be added)
- Python and C/C++ bindings available
- No ELF wrapping needed -- map memory, write code bytes, set registers, run, read registers
- Fast per-invocation (no process startup cost)

**Option B: QEMU with TCG plugins** -- *Best for tracing and instrumentation*

Full system or user-mode emulation with instrumentation plugins ([docs](https://www.qemu.org/docs/master/devel/tcg-plugins.html)). Rich plugin API:
- `qemu_plugin_register_vcpu_insn_exec_cb()` -- per-instruction execution callback
- `qemu_plugin_register_vcpu_mem_cb()` -- memory access callback (address, size, direction)
- `qemu_plugin_read_register()` / `qemu_plugin_regs_load()` -- register state dumping (QEMU 9+)
- `qemu_plugin_read_memory_vaddr()` / `write_memory_vaddr()` -- guest memory read/write
- Scoreboard API for lock-free per-vCPU counters (inline instrumentation, very low overhead)

Overhead characteristics:
| Instrumentation mode | Approximate overhead |
|---------------------|---------------------|
| Inline counters only (scoreboards) | 1.5-3x slowdown |
| Per-instruction execution callbacks | 5-20x slowdown |
| Memory access callbacks on every load/store | 10-50x slowdown |
| Per-instruction register dumping | Highest (expensive per call) |

Constraint: QEMU user-mode (`qemu-ppc64`) requires Linux ELF binaries -- cannot run raw code snippets or XEX executables directly. Semihosting for PPC is not supported. Each test would need an ELF wrapper harness.

**Option C: Custom lightweight PPC interpreter** -- *Fastest inner loop*

If the basic blocks being tested are small (tens of instructions) and use a limited instruction subset, a custom interpreter targeting only encountered instructions could be the fastest option. Reference Xenia's `ppc_emit_altivec.cc` for VMX128 semantics.
- Fastest possible per-invocation. Zero framework overhead. Full VMX128 control.
- Significant implementation effort. Bug-prone. Limited to instructions you implement.
- Best suited as a complement to Unicorn: use Unicorn for general PPC, custom interpreter for VMX128 hot paths.

### What this enables for DC3

The key use case is **permuter + execution verification in a tight loop**:

1. Permuter generates C++ variant
2. MSVC cross-compiler produces `.obj` with PPC code
3. Extract function bytes from `.obj`
4. Run in Unicorn with mocked memory/registers
5. Compare register/memory state against expected output from original binary
6. Score: objdiff match % + execution equivalence

This is faster than Xenia because:
- No boot sequence, no OS, no graphics pipeline
- Microsecond-scale function execution vs. seconds for Xenia startup
- Can run thousands of variants in the time Xenia runs one

### VMX128 gap and what it would take

Xbox 360's VMX128 has 62 custom instructions with a split register encoding scheme (uses bits from the standard opcode space differently to address 128 registers instead of 32). The encoding uses `VD128 = (VDh << 5) | VD_lower5` where `VDh` is 2 bits placed elsewhere in the instruction word, and similarly for `VA128` (7-bit) and `VB128` (7-bit).

**For QEMU** (which also benefits Unicorn): The PPC target lives under `target/ppc/`. Key files:

| File | Purpose |
|------|---------|
| `target/ppc/translate.c` | Main instruction translation, opcode dispatch |
| `target/ppc/translate/vmx-impl.c.inc` | AltiVec/VMX translation handlers |
| `target/ppc/cpu.h` | `CPUPPCState` structure (registers, SPRs, vector regs) |
| `target/ppc/cpu-models.c` | CPU model definitions |
| `target/ppc/insn32.decode` | Decodetree specification (modern approach, preferred for new instructions) |

Implementation steps:
1. **Extend `CPUPPCState`**: Expand vector register file from 32 to 128 entries. Add `PPC_VMX128` feature flag. Define Xenon CPU model.
2. **Add instruction decoders**: Use QEMU's decodetree format (declarative bit patterns, auto-generated decoder). Custom field extraction for 7-bit register reassembly from scattered bits.
3. **Implement translation handlers**: Many VMX128 instructions are standard AltiVec with extended register encoding -- reuse existing TCG code. Truly custom instructions (~10-15 of 62) need fresh implementations.
4. **Extend TCG globals**: `ppc_translate_init()` needs 128 vector register globals instead of 32.
5. **Create CPU model**: `init_proc_xenon()` in `translate_init.inc.c`.

Reference implementations:
- Xenia (`src/xenia/cpu/ppc/ppc_emit_altivec.cc`) -- most complete VMX128 translation
- VMX128 spec ([biallas.net](http://biallas.net/doc/vmx128/vmx128.txt), [Xenia docs](https://github.com/xenia-project/xenia/blob/master/docs/ppc/vmx128.txt))
- This project's own Ghidra and m2c VMX128 extensions

**Estimated effort for VMX128 in QEMU/Unicorn**: 2-4 weeks for an experienced QEMU developer (the split encoding is well-documented, most instruction semantics are reusable from standard AltiVec). 4-8 weeks if less familiar with TCG internals.

### QEMU vs. Xenia: complementary roles

| Capability | Xenia | QEMU/Unicorn |
|-----------|-------|-------------|
| Full game execution | Yes | No |
| Breakpoint + memory probes | Yes (with `--store_all_context_values`) | Yes (TCG plugins / Unicorn hooks) |
| Speed for isolated function testing | Slow (full boot required) | Fast (microseconds per call) |
| VMX128 support | Yes (complete) | No (would need to add) |
| Deterministic replay | Limited (no savestate pipeline) | Natural (you control all inputs) |
| Permuter integration | Poor (too slow for inner loop) | Good (designed for embedding) |

**Recommendation**: Use Xenia for full-game runtime validation (the plan in the runtime validation docs). Use Unicorn for fast permuter-driven function-level verification. Consider a lightweight custom interpreter for VMX128-heavy hot paths if Unicorn modification is too costly.

---

## 4) angr: Symbolic Equivalence Checking

### What angr provides (ignoring VMX128)

[angr](https://angr.io/) is a binary analysis framework with symbolic execution. For scalar PPC code:

- **VEX IR lifting**: angr uses Valgrind's VEX to lift PPC to an intermediate representation. VEX supports "almost all integer, floating point and Altivec instructions" for PowerPC, including standard AltiVec/VMX SIMD.
- **PPC32 and PPC64 support**: Both via `archinfo.ArchPPC32` and `archinfo.ArchPPC64`. Big-endian is the default (`Iend_BE`). All 32 GPRs, vector/FP registers (vsr0-vsr63), LR, CTR, CR0-CR7, and XER fields are modeled.
- **SimProcedures**: Replace external function calls with Python models during analysis. This isolates a single function for testing without needing the full binary environment.

**Known PPC issues**:
- **No Unicorn fallback on PPC**: `archinfo` explicitly comments `# Unicorn not supported` for PPC32. All execution goes through the slower pure-Python SimEngine (no fast concrete execution path).
- **ABI issues**: [angr/angr#1685](https://github.com/angr/angr/issues/1685) -- PPC `__libc_start_main` argument passing is incorrect. Not relevant for our use case since we use `call_state` to bypass libc startup.
- **PPC64 function descriptors**: PPC64 ABIv1 has indirect function pointers requiring special handling.

### Where it fits: symbolic function equivalence

The highest-value use case for decomp work is **proving two implementations are equivalent** (or finding a counterexample):

1. Lift the original binary function to VEX IR via angr
2. Lift the recompiled function to VEX IR
3. Create `call_state` for each with the same symbolic arguments (`claripy.BVS` bitvectors)
4. Symbolically execute both to collect path constraints and output expressions
5. Ask Z3: `original_return != decomp_return` under the same path constraints -- if UNSAT, equivalent on that path; if SAT, Z3 provides a concrete counterexample input

This is exactly what **[D-Helix](https://github.com/purseclab/D-helix)** does (USENIX Security 2024, BSD-3-Clause). D-Helix has three components:

1. **RECOMPILER**: Takes decompiled C and recompiles it. Successfully recompiles 72.4% of Ghidra output (vs ~25% with raw clang).
2. **SYMDIFF**: Core verification engine -- lifts both binaries to VEX IR, symbolically executes with same inputs, queries SMT solver for divergence.
3. **TUNER**: Automatically adjusts decompiler heuristics to fix detected bugs.

D-Helix found 4,515 incorrectly decompiled functions across Ghidra and angr on 93K test functions, and discovered 17 previously unknown decompiler bugs.

**D-Helix limitations**: Only compares return values (not full memory side effects). Global variables treated as constants. Tested primarily on x86/x86-64. Loop handling requires bounds.

**[SEI CMU](https://www.sei.cmu.edu/annual-reviews/2022-research-review/semantic-equivalence-checking-of-decompiled-binaries/)** has a related project using SeaHorn for equivalence checking of decompiled code via LLVM IR comparison.

### SimProcedures: the integration work

Since DC3 functions call into Milo engine internals, every external call needs either a SimProcedure stub or an unconstrained symbolic return:

```python
class MockStrLen(angr.SimProcedure):
    def run(self, s):
        result = claripy.BVS('strlen_result', 32)
        self.state.solver.add(result >= 0)
        self.state.solver.add(result <= 255)
        return result

proj.hook_symbol('strlen', MockStrLen())
```

Minimum stubs needed for DC3: `operator new`/`operator delete`, `Symbol::Find`, `DataNode::Handle`, virtual method dispatch, and various Milo engine accessors. This is where most integration effort goes.

### Performance expectations

| Scenario | Expected time | Notes |
|----------|--------------|-------|
| Small function (<20 basic blocks, no loops) | 1-10 seconds | Practical for interactive use |
| Medium function (20-50 BBs, bounded loops) | 10-60 seconds | Still usable interactively |
| Complex function (50+ BBs, nested loops) | Minutes to never | Path explosion risk |
| Function with 5+ nested loops | >60 min / timeout | angr cannot solve in reasonable time |

**No Unicorn fallback on PPC** is the key performance limiter -- on x86, angr uses Unicorn for fast concrete paths, but PPC runs everything through pure Python. Mitigations: use PyPy for ~10x speedup, `FAST_MEMORY`/`FAST_REGISTERS` state options, concretize known arguments, bound loops.

**Practical sweet spot**: Use objdiff for fast iterative development (~1 second). Use angr as a **post-verification step** to confirm that functions at 90-98% match are semantically equivalent despite instruction-level differences. Run batch verification overnight on all modified functions.

### Decomp lifecycle position

| Use case | When to use angr |
|----------|-----------------|
| **Branch divergence diagnosis** | When objdiff shows a branch mismatch at 85%+ and you can't tell if it's semantic or codegen |
| **AT_LIMIT confidence** | Before declaring a function at-limit, verify symbolic equivalence of scalar paths |
| **Batch verification** | After a large set of changes, run symbolic equivalence on all modified functions overnight |
| **Regression detection** | Nightly check that previously-matched functions haven't diverged semantically |

### Effort estimate

- Basic PPC symbolic comparison (single function, no VMX128): 1-2 weeks
- SimProcedure library for core Milo engine functions: 2-3 weeks
- D-Helix-style pipeline adapted for DC3: 4-8 weeks (D-Helix source is BSD-3-Clause, adaptable)
- VMX128 VEX extension: 6-12 weeks (larger scope than QEMU because VEX IR semantics are more complex)

---

## 5) Capstone + Unicorn: Per-Function Micro-Testing

### What "micro-testing" means

Test individual compiled functions in isolation by:
1. **Disassemble** the function from the `.obj` file using [Capstone](https://www.capstone-engine.org/) (confirmed PPC64 big-endian support, `ppc64be` mode)
2. **Emulate** the function in Unicorn with controlled register/memory inputs
3. **Compare** output registers and memory state against the original binary's function

This is "micro" because:
- One function at a time (not the whole program)
- Controlled inputs (not dependent on game state)
- Millisecond execution (not seconds)

### Concrete workflow

```
[decomp source] --MSVC--> [.obj] --extract--> [PPC bytes]
                                                    |
[original binary] --extract--> [PPC bytes]         |
                                    |               |
                              [Unicorn: run with    |
                               same inputs]    [Unicorn: run with
                                    |           same inputs]
                                    v               v
                              [register state] == [register state]?
```

### What Capstone provides

- Disassembly with semantic detail (implicit register reads/writes, instruction groups)
- Can identify which registers a function touches without running it
- Feed disassembly into analysis tools or generate test harnesses

### Limitations

- Standard Capstone PPC does **not** decode VMX128 instructions (Microsoft-proprietary encoding)
- Unicorn PPC support in v2.x may have edge cases -- needs testing
- Mocking: external function calls need stub handlers (Unicorn hook on `bl` instructions)

### Effort estimate

- Prototype (scalar PPC functions, manual setup): 1 week
- Automated pipeline (extract from `.obj`, generate test harness, compare): 3-4 weeks

---

## 6) Binary Ninja

**Dropped.** Non-OSS, $299-$2499 licensing. Ghidra + m2c already cover static analysis needs.

---

## 7) RetDec: LLVM IR Oracle

### Current state

[RetDec](https://github.com/avast/retdec) is an LLVM-based decompiler (8.5k stars, Avast/Gen Digital). Current PPC support:
- **PPC32**: Partial -- ~130 instruction variants via `capstone2llvmir/powerpc/powerpc.cpp`, covering integer, logical, load/store, branch, CR ops, and shift/rotate. 67 translation methods.
- **PPC64**: Scaffolding only -- `CS_MODE_64` is declared and 64-bit data layout is set, but **no 64-bit-specific instructions are implemented** (no `ld`/`std`, no `rldicl`/`rldimi`, no `mulld`/`divd`).
- **FPU**: **Entirely missing.** No `fadd`, `fmul`, `lfs`, `stfs`, `lfd`, `stfd`, `fmr`, `fcmp`, `fsel`, or any other FP instruction. This is the critical gap -- most game functions touch floats.
- **VMX/VMX128**: Entirely absent.
- **Maintenance**: "Limited maintenance mode." PRs reviewed with priority, issues may take a quarter. No active feature development.

### The LLVM IR comparison angle -- assessed and found impractical

The original proposal suggested comparing RetDec's lifted LLVM IR against LLVM IR from compiling our C++ with clang. This was the SEI CMU approach ([link](https://www.sei.cmu.edu/annual-reviews/2022-research-review/semantic-equivalence-checking-of-decompiled-binaries/)).

**This does not work in practice:**

1. **RetDec's IR is not recompilable** ([issue #45](https://github.com/avast/retdec/issues/45)). It models CPU registers as global variables, uses pseudo-branch calls instead of real control flow, and contains decompilation artifacts. This is structurally incomparable to compiler-produced IR.
2. **Different compilers, different IR**. DC3 uses MSVC (`cl.exe`), not Clang/LLVM. Getting "compiled LLVM IR" would require rewriting the build system around Clang, which produces fundamentally different code.
3. **Semantic gap**. Lifted IR operates at the register/memory level (loads from specific offsets, explicit stack manipulation). Compiled IR operates at the SSA/type level (named variables, typed operations). Bridging this requires research-grade normalization.
4. **Assembly comparison already exists**. objdiff compares at the ground-truth level (machine code). An IR intermediate adds complexity without improving the comparison.

### What it would actually take

The gap is much larger than "add PPC64 instructions":

| Component | Effort | Notes |
|-----------|--------|-------|
| 64-bit load/store (`ld`/`std` + variants) | 2-3 days | ~8 instructions, straightforward |
| 64-bit arithmetic (`mulld`, `divd`, etc.) | 1-2 days | ~6 instructions, trivial extensions |
| 64-bit shift/rotate (`rldicl`, `rldimi`, etc.) | 2-3 days | Complex rotate semantics |
| **Floating-point (entire FPU)** | **2-3 weeks** | **~40+ instructions, FPR register model, FPSCR** |
| VMX128 | Months | 128 vector registers, proprietary encoding |
| ABI/calling convention | 1-2 days | Stack frame, parameter passing |
| Tests | 1-2 weeks | Matching existing 32-bit test suite |

The FPU gap is the real blocker. Without it, RetDec cannot decompile any function that touches floats.

### RetDec vs Ghidra for this project

| Feature | RetDec | Ghidra |
|---------|--------|--------|
| LLVM IR output | Yes (non-recompilable) | No |
| C output quality | Lower ([DecompileBench](https://arxiv.org/html/2505.11340v1)) | Higher |
| PPC32 support | Partial (no FPU) | Full |
| PPC64 support | Scaffolding only | Full |
| VMX128 support | No | Partial (community plugins + our extensions) |
| Batch/headless | Native CLI | analyzeHeadless |
| Library API | C++ library (embeddable) | Java API / Ghidra scripts |
| Maintenance | Limited | Active (NSA + community) |

### Verdict

**Not recommended.** The investment does not provide meaningful value:
- The FPU gap alone is 2-3 weeks, and without it RetDec can't decompile useful game functions
- The LLVM IR comparison angle is impractical given structural mismatches and our use of MSVC
- Ghidra already provides full PPC64 support with our VMX128 extensions
- m2c is purpose-built for the matching decomp use case
- objdiff already compares at the ground-truth (assembly) level

RetDec's only unique value -- embeddable C++ library with batch CLI -- does not justify the cost of bringing its PPC support up to parity with Ghidra.

---

## Open Questions: Linker Support and Packaging

### Current state

`tools/project.py` line 1115:
```python
# TODO: add this functionality back when you have a few objs together you can work with (X360)
```

The `LinkStep` class (lines 821-902) outputs DOL/REL/ELF/PLF formats using the `mwld` linker -- entirely GC/Wii oriented. There is no Xbox 360 link emission. The MSVC `link.exe` path exists (line 1105) but all link rules are commented out.

### What "packaging" looks like as we approach 100%

1. **Function-level patching** (current plan): Use Xenia's `.xexp` patch loading to replace individual functions in the original binary. Documented in `docs/sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md`. This works without full relinking.

2. **Full relink**: Produce a complete Xbox 360 executable from all decomp `.obj` files + original binary `.obj` stubs for undecompiled functions. Requires:
   - MSVC `link.exe` for Xbox 360 (exists in the XDK, path confirmed at `build/compilers/X360/16.00.11886.00/link.exe`)
   - Linker script matching original binary layout
   - XEX packaging (the `jeff` tool has XEX parsing but not generation)
   - Significant reverse engineering of the original link map

3. **Hybrid approach**: Link decomp objects into a partial image, then binary-patch remaining functions from the original. This is what XenonRecomp does for static recompilation (different goal, but similar infrastructure).

### Research needed

- Can `jeff` be extended to generate XEX images (not just parse them)?
- What format does the MSVC Xbox 360 linker expect for its input configuration?
- Is there a way to partially relink (replace some objects) without reproducing the entire link map?
- Could we use the XenonRecomp infrastructure for producing a runnable binary from mixed decomp + original code?

---

## Revised Priority

1. **BinDiff/BinExport** -- Immediately actionable, 1-2 day integration. Do this now.
2. **C++ Permuter improvements** -- Continue the experimental permuter. Focus on MSVC-aware transformations and objdiff scoring. Highest day-to-day value for 90%+ functions.
3. **Unicorn micro-testing** -- Prototype per-function execution comparison. Validates permuter output and supports AT_LIMIT decisions.
4. **angr symbolic verification** -- Batch equivalence checking for high-value functions. D-Helix-style pipeline.
5. **QEMU VMX128** -- Long-term investment. Only needed when Unicorn micro-testing hits VMX128 functions frequently enough to justify the effort.
6. ~~**RetDec PPC64**~~ -- **Dropped.** FPU gap is too large, LLVM IR comparison is impractical, and Ghidra already covers this role.

---

## References

### Tools
- decomp-permuter: <https://github.com/simonlindholm/decomp-permuter>
- BinDiff: <https://github.com/google/bindiff>
- BinDiffHelper (Ghidra): <https://github.com/ubfx/BinDiffHelper>
- angr: <https://angr.io/>
- D-Helix: <https://github.com/purseclab/D-helix>
- Capstone: <https://www.capstone-engine.org/>
- Unicorn: <https://www.unicorn-engine.org/>
- RetDec: <https://github.com/avast/retdec>
- QEMU TCG plugins: <https://www.qemu.org/docs/master/devel/tcg-plugins.html>
- QEMU decodetree: <https://www.qemu.org/docs/master/devel/decodetree.html>
- XenonRecomp: <https://github.com/hedge-dev/XenonRecomp>

### Research
- D-Helix (USENIX Security 2024): <https://www.usenix.org/conference/usenixsecurity24/presentation/zou>
- SEI CMU semantic equivalence: <https://www.sei.cmu.edu/annual-reviews/2022-research-review/semantic-equivalence-checking-of-decompiled-binaries/>
- SEDiff (FSE 2022): <https://doi.org/10.1145/3540250.3549080>

### QEMU internals
- TCG plugin API: <https://github.com/qemu/qemu/blob/master/docs/devel/tcg-plugins.rst>
- PPC target translate.c: <https://github.com/qemu/qemu/blob/master/target/ppc/translate.c>
- PPC CPU state (cpu.h): <https://github.com/qemu/qemu/blob/master/target/ppc/cpu.h>
- Airbus SecLab TCG deep dive: <https://airbus-seclab.github.io/qemu_blog/tcg_p1.html>

### VMX128 specifications
- biallas.net VMX128 docs: <http://biallas.net/doc/vmx128/vmx128.txt>
- Xenia VMX128 docs: <https://github.com/xenia-project/xenia/blob/master/docs/ppc/vmx128.txt>
- Xenia VMX128 implementation: <https://github.com/xenia-project/xenia/blob/master/src/xenia/cpu/ppc/ppc_emit_altivec.cc>

### RetDec internals
- RetDec PPC translation source: <https://github.com/avast/retdec/blob/master/src/capstone2llvmir/powerpc/powerpc.cpp>
- RetDec wiki -- adding architectures: <https://github.com/avast/retdec/wiki/How-to-add-support-for-a-new-architecture>
- RetDec wiki -- Capstone2LlvmIr: <https://github.com/avast/retdec/wiki/Capstone2LlvmIr>
- RetDec LLVM IR recompilation issue: <https://github.com/avast/retdec/issues/45>
- DecompileBench (RetDec quality evaluation): <https://arxiv.org/html/2505.11340v1>

### angr PPC
- archinfo PPC32 source: <https://github.com/angr/archinfo/blob/master/archinfo/arch_ppc32.py>
- angr PPC ABI issue: <https://github.com/angr/angr/issues/1685>
- angr SimProcedures docs: <https://docs.angr.io/en/latest/extending-angr/simprocedures.html>
- angr speed optimization: <https://docs.angr.io/en/latest/advanced-topics/speed.html>
- D-Helix paper (PDF): <https://arslan8.github.io/dhelix.pdf>

### Project docs
- Archived decomp-permuter docs: `docs/tools/permuter.md`
- Runtime validation plan: `docs/sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md`
- VMX128 spec (Xenia): `xenia/docs/ppc/vmx128.txt`
- Xenia VMX128 implementation: `xenia/src/xenia/cpu/ppc/ppc_emit_altivec.cc`

---

## Appendix: Data-Backed Assessment (2026-02-09)

This appendix replaces estimates in the main document with concrete numbers from `decomp.db` (47,599 functions) and `build/373307D9/report.json` (2,223 compilation units), cross-referenced with binary scans, local tool clones, and source code review.

### A. Function Distribution by Match Percentage

| Band | Functions | AT_LIMIT | COMPLETE | Workable | Code (KB) |
|------|-----------|----------|----------|----------|-----------|
| NULL (no source) | 21,060 | 12 | 2,525 | 18,523 | 5,997 |
| 0% | 645 | 20 | 615 | 10 | 130 |
| 1-29% | 117 | 79 | 0 | 38 | 70 |
| 30-69% | 179 | 100 | 0 | 79 | 99 |
| 70-89% | 592 | 230 | 0 | 362 | 239 |
| 90-94% | 267 | 185 | 1 | 81 | 117 |
| 95-99% | 869 | 289 | 0 | 580 | 518 |
| 100% | 23,870 | 0 | 23,870 | 0 | 3,942 |
| **Total** | **47,599** | **915** | **27,011** | **19,673** | **11,112** |

**Key metrics**: Weighted match 43.35%. Code at 100%: 35.47%. Unimplemented (0%/NULL): 55.14%. Verdicts: 915 AT_LIMIT, 27,011 COMPLETE, 647 LIKELY_FIXABLE, 334 NEEDS_INVESTIGATION, 154 MAYBE_FIXABLE, 18,538 unclassified (NULL).

**Observations**:
- The 95-99% band is the highest-value target: **580 workable** functions, 518 KB of code.
- The 90-94% band is heavily saturated with AT_LIMIT: 185 of 267 (69.3%), making it less productive than 95-99%.
- 21,060 NULL-percent functions are SDK stubs, XDK libraries, and functions without source files.
- The 1-29% and 30-69% bands now show 0 COMPLETE (previously had some due to stale verdicts).

### B. What Blocks the 90-100% Functions

1,136 functions in the 90-100% range. 474 AT_LIMIT, 1 COMPLETE (sub-100%), **661 workable**. Of those workable: 281 clean (no linker merged), 380 have linker merged symbols.

#### By blocking pattern (workable functions only, no AT_LIMIT/COMPLETE)

| Pattern | Workable | Clean (no merged) | Has Linker Merged | Avg % |
|---------|----------|--------------------|-------------------|-------|
| LINKER_MERGED | 380 | 0 | 380 | 98.1 |
| (undiagnosed) | 158 | 158 | 0 | 98.2 |
| REGISTER_SWAP | 81 | 81 | 0 | 96.8 |
| OFFSET_SWAP | 15 | 15 | 0 | 99.0 |
| CONTROL_FLOW | 14 | 14 | 0 | 96.7 |
| COMMUTATIVE_OP_ORDER | 11 | 11 | 0 | 99.5 |
| COMPARISON_STYLE | 2 | 2 | 0 | 100.0 |

#### By pattern (all verdicts in 90-100%)

| Pattern | Total | 90-94% | 95-99% | AT_LIMIT | Workable | Avg % |
|---------|-------|--------|--------|----------|----------|-------|
| LINKER_MERGED | 494 | 47 | 196 | 114 | 380 | 97.7 |
| REGISTER_SWAP | 279 | 78 | 139 | 198 | 81 | 95.7 |
| (undiagnosed) | 268 | 51 | 83 | 109 | 158 | 97.4 |
| CONTROL_FLOW | 49 | 14 | 24 | 35 | 14 | 95.8 |
| OFFSET_SWAP | 17 | 0 | 5 | 2 | 15 | 98.5 |
| COMMUTATIVE_OP_ORDER | 16 | 0 | 3 | 5 | 11 | 99.1 |
| BOOL_MASK | 10 | 2 | 7 | 10 | 0 | 96.3 |
| COMPARISON_STYLE | 3 | 0 | 1 | 1 | 2 | 98.6 |

Note: REGISTER_SWAP saw the largest AT_LIMIT reclassification (198 AT_LIMIT vs 81 workable), confirming that many register allocation mismatches are genuinely unfixable at the source level.

#### Best candidates: 95-99%, workable, no linker merged

| Pattern | Count |
|---------|-------|
| (undiagnosed) | 136 |
| REGISTER_SWAP | 62 |
| OFFSET_SWAP | 15 |
| COMMUTATIVE_OP_ORDER | 11 |
| CONTROL_FLOW | 10 |
| COMPARISON_STYLE | 2 |

#### Permuter coverage gap

The existing permuter has **3 patterns** (variable_extraction, signed_unsigned, inline_assignment) that collectively address parts of 2 out of 7 blocking categories (REGISTER_SWAP and COMPARISON_STYLE).

| Category | Addressable by permuter? | Count |
|----------|--------------------------|-------|
| LINKER_MERGED | **No** (linker-level, not source) -- many should be reclassified AT_LIMIT | 380 |
| (undiagnosed) | **Unknown** (need classification first) | 158 |
| REGISTER_SWAP (clean) | **Partially** (existing patterns help, need variable declaration reordering) | 81 |
| OFFSET_SWAP | **No** (needs header fixes, not permuter) | 15 |
| CONTROL_FLOW | **No** (need new control flow restructuring pattern) | 14 |
| COMMUTATIVE_OP_ORDER | **No** (need new operand swap pattern) | 11 |
| COMPARISON_STYLE | **Yes** (signed_unsigned pattern) | 2 |

**Highest-priority new patterns**: (1) Variable declaration reordering, (2) commutative operand swap, (3) control flow restructuring (if-else/ternary, while/for conversion).

### C. VMX128 Density -- The Key Finding

**Zero functions in the 90-100% range use VMX128 instructions.**

Of 1,107 functions in the 90-100% range matched to binary addresses and scanned: **100% are pure scalar PPC** (integer + floating-point only). All 661 workable functions in the band are pure scalar.

VMX128 is concentrated almost entirely in Xbox 360 SDK libraries (Kinect/NUI, XHV voice chat, XDSP audio) and the Bink video codec -- none of which are decomp targets in the 90-100% range.

Only 11 functions in the entire decomp scope use VMX128:
- 7 in `system/synth_xbox/FFT` and `system/gesture/*` (Kinect skeleton processing)
- 4 in `lib/binkxenon` (Bink video codec, all marked COMPLETE)
- None in the 90-100% workable range

**Implication**: Unicorn without VMX128 support covers **100% of the permuter sweet spot**. The QEMU VMX128 extension (Section 3, estimated 2-4 weeks) is deprioritized.

### D. Unicorn PPC Feasibility

**Verdict: PARTIALLY WORKS** -- usable for scalar function testing.

Local clone at `/home/free/code/milohax/unicorn/` is Unicorn 2.1.4 (latest stable).

| Capability | Status |
|-----------|--------|
| PPC32 Big-Endian | **Works** -- tested, 5 unit tests pass |
| PPC64 Big-Endian | Code exists, compiles, zero test coverage |
| GPR r0-r31 read/write | **Available** |
| FPR f0-f31 read/write | **Available** |
| CR, LR, CTR, XER, MSR, FPSCR | **Available** |
| Vector registers (vr0-vr31) | **NOT exposed** via API (internal CPU state has them) |
| `UC_HOOK_CODE` | **Works** (fires for every instruction) |
| `UC_HOOK_INSN` for PPC | **Not supported** (only x86/RISC-V) |
| VMX128 instructions | Not supported |
| Standard AltiVec | Internally emulated, but registers not exposed via API |

**`bl` interception**: Use `UC_HOOK_CODE` + manual instruction decode (read 4 bytes, check opcode field 18 + LK bit). Alternative: pre-process code bytes to replace `bl` with `sc`, use `UC_HOOK_INTR`.

**Risk**: PPC64 mode has zero upstream test coverage. May have bugs with 64-bit register handling.

### E. BinDiff Integration Path

**Revised estimate: 1.5-2 working days** (original: "1-2 days"). Achievable but at the optimistic end.

| Component | Status |
|-----------|--------|
| BinDiff v8 source | Cloned at `/home/free/code/milohax/bindiff/`, not built |
| BinExport prebuilt | Targets Ghidra 11.0.3 |
| Local Ghidra | 12.0.1 |
| BinDiffHelper | v0.7.0 at `/home/free/code/milohax/BinDiffHelper/` |

**Main blocker**: BinExport must be built from source for Ghidra 12.0.1 compatibility. The Ghidra 12 API gap is the primary risk that could push the timeline to 3+ days.

**Output format**: SQLite database (`.BinDiff` extension) with per-function `similarity` (0.0-1.0) and `confidence` (0.0-1.0) scores. Architecture-agnostic matching confirmed for PPC.

**Value assessment revised**: The primary value is **DC3-vs-RB3 cross-binary comparison** to systematically identify shared Milo engine functions. For DC3 decomp-vs-original comparison, objdiff already provides strictly better instruction-level granularity. The existing `lookup_rb3` MCP tool and RB3 reference pairing may reduce the urgency of BinDiff integration.

### F. angr / D-Helix Assessment

**Verdict: NEEDS WORK** -- viable path for non-VMX code, 2-3 weeks.

| Aspect | Status |
|--------|--------|
| D-Helix SYMDIFF portability | Mostly architecture-agnostic; needs bitvector width changes (64->32 bit) |
| RECOMPILER bypass | **Yes** -- use our compiled `.o` files directly, skip KLEE/PROMPT dependency |
| angr PPC32 symbolic execution | Functional (VEX lifting, `call_state`, `SimCCPowerPC` with args r3-r10) |
| angr Unicorn support for PPC | Not supported (`# Unicorn not supported` in archinfo) |
| XEX loader in CLE | Not supported (need blob loading) |
| VEX PPC lifting coverage | Full for integer, FP, load/store, branch, standard AltiVec; no VMX128 |

**Recommended approach**: Skip D-Helix entirely. Build a lightweight angr-based function comparator:
1. Load both original binary and compiled `.o` as PPC32 BE blobs
2. Use `call_state` with symbolic arguments in r3-r10
3. Compare Z3 constraint trees for semantic equivalence
4. Restrict to non-VMX128 functions (100% of the 90-100% workable range)

**Effort**: 2-3 weeks for non-VMX functions. VMX128 VEX extension would require months of research-level effort.

### G. Revised Priority Order

Based on the data above, the priority order from the main document is **largely confirmed** with adjustments:

| # | Tool | Change | Rationale |
|---|------|--------|-----------|
| 1 | **C++ Permuter** | **Promoted from #2** | 580 workable functions in the 95-99% band. Immediate day-to-day value. Variable declaration reordering is highest-leverage new pattern. |
| 2 | **BinDiff/BinExport** | Demoted from #1 | Value is narrower than estimated -- only for DC3-vs-RB3 cross-reference, which the existing `lookup_rb3` tool partially covers. Ghidra 12 compat gap adds risk. |
| 3 | **Unicorn micro-testing** | Unchanged | VMX128 non-issue for permuter sweet spot (100% scalar). PPC32 works today. |
| 4 | **angr symbolic verification** | Unchanged | 2-3 week investment for non-VMX functions. Skip D-Helix, build lightweight comparator. |
| 5 | **QEMU VMX128** | **Deprioritized further** | Zero VMX128 functions in the 90-100% workable range. Only 11 VMX128 functions in entire decomp scope. |
| 6 | ~~RetDec PPC64~~ | Dropped (unchanged) | FPU gap too large, LLVM IR comparison impractical. |

### H. Key Corrections to Main Document

1. **Section 1 lifecycle table**: The "90-95%" row should note that 69.3% of functions in this range are AT_LIMIT. The true sweet spot is **95-99%** (580 workable functions, vs 81 workable in the 90-94% range).

2. **Section 3 VMX128 gap**: The urgency of VMX128 support in QEMU/Unicorn was significantly overstated. With zero VMX128 functions in the 90-100% workable range, this becomes a long-term nice-to-have rather than a near-term blocker.

3. **Section 3 Unicorn**: Vector register API is NOT exposed (no `UC_PPC_REG_VR*` constants). Internal AltiVec emulation exists but cannot be accessed from the host. Adding VR register access is ~1-2 days of work.

4. **Section 2 BinDiff effort**: "1-2 days" is achievable but at the optimistic end. Ghidra 12 compatibility gap is the main risk factor.

5. **Section 4 D-Helix**: The RECOMPILER step can be skipped entirely for our use case. We compile our own C++ source, so both sides can go through angr's VEX lifting directly.

### I. Deferred Questions — Resolved and Next Steps

**Resolved:**

- **Unicorn for decomp verification**: Not pursuing community survey. We can independently evaluate Unicorn's fitness for our use case based on the technical assessment in Section D above.

- **XenonRecomp**: **Could be useful** for DC3's packaging story. XenonRecomp's infrastructure for producing runnable binaries from mixed decomp + original code is worth a deep dive. Deferred to a future session focused on packaging/relinking.

- **XEX generation**: The `jeff` tool is the right place to build XEX generation capability. It already has deep format knowledge from parsing. MSVC's proprietary `link.exe` is a backup option, but extending `jeff` is preferred.

- **Partial relinking**: To be explored alongside `jeff` XEX generation. The question of replacing individual `.obj` files without reproducing the entire link map remains open — likely solvable once `jeff` can generate XEX images.

**Next phase — C++ Permuter Community Research:**

The question of what C++ permuter approaches exist in other decomp communities needs dedicated research. Below is a shareable brief for asking around in decomp community channels.

---

#### C++ Permuter Research Brief (shareable)

**Context**: We're working on a matching decompilation project (Xbox 360 / MSVC / PowerPC) and need a **C++ permuter** — a tool that systematically generates source code variants and scores them against the target binary to find instruction-level matches.

The standard [decomp-permuter](https://github.com/simonlindholm/decomp-permuter) doesn't work for us because:
1. It uses **pycparser** (C-only) — can't handle classes, templates, virtual functions
2. It expects **ELF** object format — we produce COFF
3. It assumes **gcc/mwcc** build syntax — we use MSVC

**What we're looking for:**
- Has anyone built or adapted a permuter for **C++ codebases**? (BotW, TPHD, Splatoon, any C++ decomp)
- Are there permuter-like tools that use **clang AST** or **tree-sitter** for source rewriting instead of pycparser?
- Has anyone integrated a permuter with a **non-ELF toolchain** (MSVC, or any COFF-producing compiler)?
- Are there text-based (regex/pattern) rewriting approaches that work well enough for C++ without full parsing?

**Our current state:**
- We have an experimental C++ permuter (text-based, 3 patterns: variable extraction, signed/unsigned, inline assignment)
- We score variants using [objdiff](https://github.com/encounter/objdiff) (instruction-level diffing)
- Our highest-value target: **580 workable functions at 95-99% match** — these need small expression-form changes, declaration reordering, bool shaping, etc.

**Specific transformations we need:**
- Variable declaration reordering (affects register allocation)
- Commutative operand swapping (`a + b` vs `b + a`)
- Bool shaping (`!!x`, `x != 0`, `x > 0` for unsigned)
- Ternary vs if/else conversion
- Expression splitting/combining (temporaries)
- Control flow restructuring (for/while, guard hoisting)

**Any pointers, prior art, or "we tried X and it didn't work" would be valuable.**

---
