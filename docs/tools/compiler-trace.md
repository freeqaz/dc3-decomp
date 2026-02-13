# Compiler Trace — c2.dll Instrumentation Tooling

Tools for analyzing the MSVC X360 compiler's (c2.dll) register allocation decisions by comparing compilation of source variants.

## Quick Start

```bash
# Compare assembly output of two source variants
python -m tools.compiler_trace diff-asm test_a.cpp test_b.cpp

# Capture intermediate language files
python -m tools.compiler_trace capture-il test.cpp --output-dir /tmp/claude/il_out

# Profile two variants and diff execution paths
python -m tools.compiler_trace callgrind-diff test_a.cpp test_b.cpp

# Generate GDB script with breakpoints from accumulated evidence
python -m tools.compiler_trace gdb-attach --print-only
```

## Tools

### diff-asm

Compiles two source variants with assembly listings, normalizes them, and diffs. Automatically detects consistent register swap patterns.

```bash
python -m tools.compiler_trace diff-asm test_a.cpp test_b.cpp
python -m tools.compiler_trace diff-asm test_a.cpp test_b.cpp --listing-type /FAs
python -m tools.compiler_trace diff-asm test_a.cpp test_b.cpp --output-dir /tmp/asm_out

# Extract and diff a single function from a full TU
python -m tools.compiler_trace diff-asm src/system/rnddx9/Tex.cpp variant.cpp -f ResetSurfaces
```

Output includes:
- Register swap detection (e.g., `r10 <-> r11`)
- Semantic diff count after normalization
- Full unified diff of normalized assembly

### capture-il

Captures the compiler's intermediate language temp files (`_CL_*`) using strace's `inject` feature to prevent their deletion.

```bash
# Single capture
python -m tools.compiler_trace capture-il test.cpp --output-dir /tmp/il_out

# Diff mode: capture both and hex-diff
python -m tools.compiler_trace capture-il test_a.cpp --output-dir /tmp/il_diff --diff test_b.cpp
```

IL file types:
| Extension | Content |
|-----------|---------|
| `sy` | Symbol table |
| `ex` | Expression tree |
| `gl` | Globals |
| `in` | Includes |
| `db` | Debug info |

### callgrind-diff

Profiles two compilations using `perf` and compares per-address execution counts within c2.dll's `.text` section. Divergent addresses indicate compiler code paths sensitive to source differences.

```bash
python -m tools.compiler_trace callgrind-diff test_a.cpp test_b.cpp
python -m tools.compiler_trace callgrind-diff test_a.cpp test_b.cpp --output-dir /tmp/perf_out
```

Results are written to `tools/c2_funcmap.json`. Run multiple experiments to build up evidence — addresses that diverge consistently across experiments are strong register allocator candidates.

**Note:** Uses `perf` (not valgrind). Valgrind/callgrind is incompatible with wibo due to x86-64 segment register manipulation for Windows TEB emulation.

### rr-record

Records compilation under [rr](https://rr-project.org/) for deterministic replay debugging.

```bash
python -m tools.compiler_trace rr-record test.cpp --trace-dir /tmp/rr_trace
python -m tools.compiler_trace rr-record test_a.cpp --trace-dir /tmp/rr_both --both test_b.cpp
```

**Known limitations:**
- AMD Zen CPUs need SpecLockMap disabled
- wibo's 32/64-bit mode switching may cause rr assertion failures

### gdb-attach

Generates GDB scripts for debugging c2.dll, with breakpoints from the accumulated funcmap.

```bash
# Print GDB script
python -m tools.compiler_trace gdb-attach --print-only

# Launch GDB with live compilation
python -m tools.compiler_trace gdb-attach test.cpp

# Launch GDB with rr replay
python -m tools.compiler_trace gdb-attach --rr-trace /tmp/rr_trace/trace
```

Custom GDB commands:
- `pe-regs` — dump x86-32 register state
- `bt32` — manual EBP-chain backtrace with c2.dll offset annotation
- `c2-addr <rva>` — convert c2.dll RVA to virtual address

## c2.dll Address Space

| Property | Value |
|----------|-------|
| ImageBase | `0x10b00000` |
| .text start | `0x10b01000` |
| .text size | `0x12cc7c` (~1.23MB) |
| .text end | `0x10c2dc7c` |

## Workflow

1. Create test variants (e.g., swap declaration order of two variables)
2. Run `diff-asm` to confirm the register swap in assembly
3. Run `capture-il` with `--diff` to see IL-level differences
4. Run `callgrind-diff` to identify divergent c2.dll code paths
5. Repeat steps 1-4 with different variants to build evidence
6. Run `gdb-attach` to generate breakpoint script from accumulated data
7. Use GDB (or rr replay) to step through the identified c2.dll functions

## Architecture

```
tools/compiler_trace/
    __init__.py          # Package docstring
    __main__.py          # CLI entry point (argparse subcommands)
    invoker.py           # CompilerInvoker: wraps wibo + cl.exe
    asm_diff.py          # diff-asm: assembly listing diff
    callgrind_diff.py    # callgrind-diff: perf-based profiling diff
    il_capture.py        # capture-il: IL file capture via strace
    rr_record.py         # rr-record: deterministic replay recording
    gdb_script.py        # gdb-attach: GDB script generation
    funcmap.py           # C2FuncMap: address knowledge base
tools/c2_funcmap.json    # Persistent c2.dll address database
```
