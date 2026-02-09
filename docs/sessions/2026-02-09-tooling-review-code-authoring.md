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
