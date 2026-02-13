# QEMU Dynamic Analysis Research (2026-02-11)

## Context

This session investigates QEMU's feasibility as a dynamic analysis backend for DC3 decomp, as proposed in `docs/sessions/2026-02-08-decomp-tooling-options-for-code-writing.md` (Section 3: "QEMU TCG plugin tracer as secondary runtime backend").

The prior Xenia-based runtime validation plan (`docs/sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md`) identified Xenia as the primary runtime backend but noted it has significant tooling gaps. This doc evaluates whether QEMU could serve as an alternative or complement.

### Prior Research (backlinks)

- `docs/sessions/2026-02-09-tooling-review-code-authoring.md` — Already contains extensive QEMU/Unicorn evaluation for permuter integration (different use case)
- `docs/sessions/2026-02-08-onbeat-differential-runtime-validation-ideas.md` — Runtime validation concept
- `docs/sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md` — Xenia integration research

---

## Two Distinct Use Cases (Clarification)

The Feb 9 tooling review evaluated QEMU/Unicorn for **permuter integration** (fast isolated function execution). That's a different use case from what the Xenia runtime validation docs describe.

| Use Case | Goal | Tool | Status |
|----------|------|------|--------|
| **Permuter backend** | Run hundreds of function variants per minute against objdiff | Unicorn | Researched (Feb 9). Unicorn is the answer. |
| **Runtime validation** | Trace function state at entry/exit during actual game execution | Xenia | Researched (Feb 8). Tooling gaps remain. |
| **Runtime validation alternative** | Same as above, but independent of Xenia | QEMU? | **This doc.** |

---

## QEMU PPC Target Status

Source: `/home/free/code/milohax/qemu/target/ppc/`

### What Exists

- **PPC64 big-endian support**: Both system emulation (`ppc64-softmmu`) and user-mode (`ppc64-linux-user`)
- **CPU models**: PPC970/FX/MP, Cell, POWER5/5+/7/8/9
- **Standard VMX/AltiVec**: Comprehensive — 150+ instruction families in `translate/vmx-impl.c.inc` (3,468 lines), `translate/vsx-impl.c.inc` (96,996 lines), `fpu_helper.c` (147,494 lines)
- **Machine models**: Mac G5 (mac99), pSeries (POWER), PowerNV — no Xbox 360/Xenon
- **Closest CPU to Xenon**: PPC970FX (same architectural family, shares AltiVec baseline)

### What Does NOT Exist

- **No Xenon CPU model**: Zero references to "xbox", "xenon", "vmx128", "vpkd3d", "vrlimi" in the entire QEMU codebase
- **No VMX128 extensions**: No 128-register vector file, no split register encoding, no custom instructions
- **No XEX loader**: QEMU user-mode expects Linux ELF binaries; system emulation expects a full OS image
- **No Xenon board definition**: No Xbox 360 hardware model (GPU, southbridge, interrupt controller, etc.)

---

## TCG Plugin API Assessment

Source: `/home/free/code/milohax/qemu/include/plugins/qemu-plugin.h` (API v6)

The TCG plugin API is very capable — **IF you can run the code**:

### Available Hooks

| Hook | API | Notes |
|------|-----|-------|
| Per-instruction execution | `qemu_plugin_register_vcpu_insn_exec_cb()` | Fire callback on every instruction |
| Memory access | `qemu_plugin_register_vcpu_mem_cb()` | Address, size, direction, actual value |
| Register read/write | `qemu_plugin_read_register()` / `write_register()` | All GPRs, FPRs via handles |
| Guest memory read/write | `qemu_plugin_read_memory_vaddr()` / `write_memory_vaddr()` | Arbitrary guest memory access |
| Instruction metadata | `qemu_plugin_insn_vaddr()`, `insn_disas()`, `insn_symbol()` | Address, disassembly, symbol |
| Conditional execution | `qemu_plugin_register_vcpu_insn_exec_cond_cb()` | Fire only when condition met |
| Syscalls | `qemu_plugin_register_vcpu_syscall_cb()` | Intercept and filter |
| Discontinuities | `qemu_plugin_register_vcpu_discon_cb()` | Interrupts, exceptions, host calls |

### Existing Plugins (reference implementations)

| Plugin | Location | What it does |
|--------|----------|-------------|
| `execlog.c` | `contrib/plugins/` | Full instruction tracer with register change tracking + memory ops |
| `uftrace.c` | `contrib/plugins/` | Function entry/exit tracer via frame pointer |
| `cache.c` | `contrib/plugins/` | L1/L2 cache simulator |
| `cflow.c` | `contrib/plugins/` | Control flow analysis (branches, exceptions) |
| `lockstep.c` | `contrib/plugins/` | Dual-emulator lockstep debugging |

### Performance Overhead

| Mode | Overhead |
|------|----------|
| Inline counters (scoreboards) | 1.5-3x |
| Per-instruction callbacks | 5-20x |
| Memory access callbacks | 10-50x |
| Per-instruction register dumps | Highest |

### Plugin Build

Plugins can be built out-of-tree as shared libraries:
```bash
gcc -fPIC -shared -o plugin.so plugin.c \
    -I/path/to/qemu/include/plugins \
    $(pkg-config --cflags --libs glib-2.0)
```

### Verdict on TCG Plugins

The API is excellent for our use case (function entry/exit state snapshots). The `execlog.c` plugin is almost exactly the kind of probe we'd want. **But the API is only useful if QEMU can actually execute the target code** — and it can't execute Xbox 360 binaries.

---

## XEX Loading: The Core Blocker

### XEX Format (well-understood)

Multiple tools in the workspace parse XEX:
- `jeff` (`src/util/xex.rs`): Full parser, extracts embedded PE, resolves imports
- `XenonRecomp` (`XenonUtils/xex.cpp`): Production loader with `Xex2LoadImage()`
- `Xenon` emulator: Complete XEX loading pipeline
- `XEXLoaderWV`: Standalone loader

**XEX Structure:**
- Magic "XEX2", optional headers, embedded PE at offset
- AES-128 CBC encryption (retail key known), optional LZX compression
- PE sections mapped at `image_base` (typically 0x80000000-0x9FFFFFFF)
- Imports via ordinal-based thunk system from `xboxkrnl.exe`, `xam.xex`, etc.

### What You'd Need to Run XEX in QEMU

**Option A: Full Xenon system emulation (enormous effort)**
- Custom Xenon CPU model with VMX128
- Xenon board definition (GPU, southbridge, interrupt controller)
- Xbox kernel stubs for hundreds of syscalls
- This is literally building another Xbox 360 emulator — Xenia already exists

**Option B: User-mode with ELF wrapper (limited)**
- Extract function code from XEX
- Wrap in Linux PPC ELF binary with stub environment
- Run under `qemu-ppc64` with TCG plugins for instrumentation
- Problems: no VMX128, need to mock all external calls, basically Unicorn with extra steps

**Option C: Direct memory loading via custom QEMU machine (moderate effort)**
- Build a minimal QEMU machine model that just provides flat memory
- Load XEX sections directly into guest memory via machine init
- Stub kernel calls via exception handler hooks
- Run with TCG plugins for instrumentation
- Still need: VMX128 support, comprehensive kernel stubs, import resolution

### Verdict on XEX Loading

None of these options are practical compared to just using Xenia, which already loads XEX files and has breakpoint/memory probe infrastructure. Building a QEMU-based Xbox 360 execution environment would duplicate Xenia's work at higher cost.

---

## Comparison: QEMU vs Xenia vs Unicorn for DC3

| Capability | Xenia | QEMU (hypothetical) | Unicorn |
|-----------|-------|---------------------|---------|
| Full game execution | Yes | No (no Xenon model) | No |
| XEX loading | Yes | No | No |
| VMX128 support | Yes (complete) | No | No |
| Breakpoint/memory probes | Yes (needs tooling) | Yes (TCG plugins, excellent API) | Yes (hooks) |
| Function-level testing | Slow (full boot) | N/A | Fast (microseconds) |
| Deterministic replay | Limited (no savestate) | Natural (controlled inputs) | Natural |
| Permuter integration | Poor (too slow) | N/A | Good |
| Instrumentation flexibility | Limited (custom Xenia code) | Excellent (plugin ecosystem) | Moderate (hooks) |
| Effort to deploy | Medium (tooling gaps) | Very High (build emulator) | Low (works today for scalar PPC) |

---

## Key Finding: VMX128 Is Irrelevant for Practical Targets

From the Feb 9 data-backed assessment:

- **Zero functions in the 90-100% workable range use VMX128 instructions**
- All 661 workable functions in 90-100% are pure scalar PPC
- Only 11 functions in entire decomp scope use VMX128 (Kinect, Bink video)
- VMX128 support is not a blocker for any practical dynamic analysis work

This means:
- Unicorn (standard AltiVec only) covers 100% of the permuter sweet spot
- For function-level differential testing, VMX128 absence is acceptable
- QEMU VMX128 extension remains deprioritized

---

## Conclusions

### QEMU as full runtime validation backend: NOT VIABLE

Building Xbox 360 execution support in QEMU would duplicate Xenia at higher cost. QEMU's strength (excellent plugin instrumentation) is gated behind its weakness (no Xenon support). Xenia remains the correct tool for full-program runtime validation.

### QEMU's TCG plugin API: EXCELLENT but inaccessible

The plugin API (`execlog.c`, register/memory reads, conditional callbacks) is exactly what we'd want for function state snapshots. If QEMU could run Xbox 360 code, it would be strictly better than Xenia's breakpoint infrastructure. But it can't.

### Unicorn for function-level differential testing: ALREADY THE ANSWER

The Feb 9 research correctly identified Unicorn as the right tool for isolated function execution. This session confirms that QEMU adds nothing beyond what Unicorn provides for this use case — Unicorn is built on QEMU's TCG but strips away the system emulation layer we don't need.

### Where QEMU TCG knowledge IS useful

1. **Contributing VMX128 upstream to QEMU/Unicorn**: The TCG internals research (decodetree format, `CPUPPCState` extension, vector register file expansion) maps directly to adding VMX128 to Unicorn. Same codebase, same translation layer.

2. **Understanding Unicorn's capabilities and limits**: Unicorn inherits QEMU's PPC translation layer, so QEMU's instruction coverage directly predicts Unicorn's.

3. **Future: if Xenon system emulation becomes viable**: The TCG plugin infrastructure would be immediately applicable. But that's not a near-term scenario.

---

## Revised Tool Roles for DC3 Dynamic Analysis

| Tool | Role | Priority |
|------|------|----------|
| **Xenia** | Full-program runtime validation (state snapshots at function entry/exit) | P0 — primary runtime backend |
| **Unicorn** | Fast function-level differential testing (permuter verification, AT_LIMIT decisions) | P1 — complementary to Xenia |
| **QEMU** | Reference codebase for Unicorn/VMX128 work; no direct use | P3 — long-term reference only |
| **angr** | Symbolic equivalence checking for high-value functions | P2 — post-verification layer |

---

## What To Do Next (updated from Feb 8/9 plans)

### For runtime validation (Xenia path)
Continue the plan from `2026-02-08-onbeat-runtime-validation-tooling-handoff.md`:
1. Trace schema + comparator
2. Xenia breakpoint probe prototype
3. `.xexp` patch generation in `jeff`

### For function-level testing (Unicorn path)
Continue from `2026-02-09-tooling-review-code-authoring.md`:
1. Prototype Unicorn-based function runner for scalar PPC
2. Integrate with permuter for variant scoring
3. Add vector register API exposure (currently internal-only in Unicorn)

### For QEMU specifically
- No immediate work needed
- Keep as reference for Unicorn VMX128 extension (if ever prioritized)
- The `vmx128-research/qemu-reference/` copy is sufficient for this purpose

---

## References

### Local Code
- QEMU source: `/home/free/code/milohax/qemu/`
- QEMU reference (for VMX128): `/home/free/code/milohax/vmx128-research/qemu-reference/`
- Unicorn: `/home/free/code/milohax/unicorn/`
- jeff (XEX parser): `/home/free/code/milohax/jeff/`
- XenonRecomp: `/home/free/code/milohax/XenonRecomp/`
- Xenon emulator: `/home/free/code/milohax/xenon/`
- Xenia source: `/home/free/code/milohax/vmx128-research/xenia-source/`

### Key QEMU Files
- TCG plugin header: `include/plugins/qemu-plugin.h`
- PPC target: `target/ppc/translate.c`, `target/ppc/cpu.h`
- VMX translation: `target/ppc/translate/vmx-impl.c.inc`
- Instruction tracer plugin: `contrib/plugins/execlog.c`
- Function tracer plugin: `contrib/plugins/uftrace.c`
- Plugin docs: `docs/devel/tcg-plugins.rst`

### Prior Session Docs
- `docs/sessions/2026-02-08-onbeat-differential-runtime-validation-ideas.md`
- `docs/sessions/2026-02-08-onbeat-runtime-validation-tooling-handoff.md`
- `docs/sessions/2026-02-08-decomp-tooling-options-for-code-writing.md`
- `docs/sessions/2026-02-09-tooling-review-code-authoring.md`
