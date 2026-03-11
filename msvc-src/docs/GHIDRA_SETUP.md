# Ghidra Setup for c2.dll Analysis

## Overview

We use [pyghidra](https://github.com/NationalSecurityAgency/ghidra/tree/master/Ghidra/Features/PyGhidra) to analyze `c2.dll` — the MSVC PPC backend compiler used to build DC3. This enables decompilation, callgraph analysis, and raw memory reads of c2.dll internals (allocation order tables, register descriptor tables, etc.).

## Prerequisites

```bash
# Ghidra 12.0.3+ installed at /opt/ghidra (or set GHIDRA_INSTALL_DIR)
export GHIDRA_INSTALL_DIR=/opt/ghidra

# pyghidra (from pip or from Ghidra source tree)
pip install pyghidra
```

c2.dll location: `build/compilers/X360/16.00.11886.00/c2.dll`

## Direct Access (Recommended)

The primary tool is `msvc-src/tools/c2_decompile.py`. It uses `pyghidra.open_program()` directly — no server needed. Ghidra auto-analyzes the binary on first open (~30s), then caches the project.

```bash
# Decompile a function at address
python3 msvc-src/tools/c2_decompile.py decompile 0x10bc6487

# Decompile multiple functions
python3 msvc-src/tools/c2_decompile.py decompile 0x10bc6487 0x10bc62b6 0x10bc58d5

# List callees/callers
python3 msvc-src/tools/c2_decompile.py callees 0x10bc6487
python3 msvc-src/tools/c2_decompile.py callers 0x10bc6487

# Function metadata
python3 msvc-src/tools/c2_decompile.py info 0x10bc6487

# Search strings in binary
python3 msvc-src/tools/c2_decompile.py strings "register"

# Read raw bytes (hex dump)
python3 msvc-src/tools/c2_decompile.py read-bytes 0x10c3b6a0 256
```

## MCP Server (Alternative)

The `msvc-src/scripts/c2_ghidra_server.sh` script manages a pyghidra-mcp server instance for c2.dll on port 8001 (port 8000 is reserved for the DC3 XEX Ghidra instance). The companion client is `msvc-src/scripts/c2_query.py`.

**Note**: The MCP SSE transport has reliability issues — the direct approach above is preferred for most work.

## Key Addresses

### COLOR Register Allocator (`regasg.c`)

| Address | Size | Name | Role |
|---------|------|------|------|
| `0x10bc6487` | 23b | `color_init` | Entry point (lock→dispatch→unlock) |
| `0x10bc62b6` | 465b | `color_dispatch` | Clears state, selects table, iterates IL |
| `0x10bc514a` | 842b | `color_alloc_simple` | Simple allocation strategy |
| `0x10bc5494` | 1089b | `color_alloc_complex` | Complex allocation strategy |
| `0x10bc58d5` | 1891b | `color_select_reg` | Register selection (advancing pointer) |
| `0x10bc4be9` | 220b | `color_spill_cost` | Spill cost = distance to next use |
| `0x10bc6038` | 387b | `color_resolve_conflict` | Conflict resolution + spill code gen |
| `0x10bc61bb` | 251b | `color_assign_regs` | Assigns physical regs to IL node |
| `0x10bc4ded` | 354b | `color_process_node` | Builds interference data |

### Key Globals

| Address | Description |
|---------|-------------|
| `DAT_10c3d730` | Register state buffer (1428 bytes, 357 × 4) |
| `DAT_10c2f088` | Register descriptor table (stride 0x60) |
| `DAT_10c6fdf4` | Pointer to selected allocation order table |
| `DAT_10c3b6a0` | GPR table variant 3 (Xenon + flag) |
| `DAT_10c3bee0` | FPR allocation order table |
| `DAT_10c3b8c0` | VMX128 allocation order table |

### Inliner

| Address | Size | Name | Role |
|---------|------|------|------|
| `0x10ba347b` | 109b | `inline_top` | Top-level inliner entry |
| `0x10ba32fc` | 383b | `inline_dispatch` | Dispatcher, sets flags |
| `0x10ba1eca` | 1368b | `inline_cost` | Cost calculator (threshold = 150 IL nodes) |
| `0x10ba1c2d` | 597b | `inline_execute` | Performs inlining |
| `0x10b32533` | 3b | `inline_node_weight` | Per-node weight (STUB = 0) |
| `0x10ba1e82` | 27b | `inline_mark_always` | Linear flow: always inline |

### G3P2 Record-Form Fusion

| Address | Size | Name | Role |
|---------|------|------|------|
| `0x10c0f14e` | 159b | `record_form_dispatch` | G3P2 entry: iterates IL, calls worker |
| `0x10c0d57e` | 3899b | `record_form_worker` | Main opcode switch, transforms IL |
| `0x10c123b9` | 92b | `record_form_eligible` | Checks opcode+constant compatibility |
| `0x10c39b18` | — | `opcode_class_table` | Maps IL opcodes to internal classes |

### Other Compiler Passes

| Address | Size | Name |
|---------|------|------|
| `0x10b3421b` | — | G5P10 (PPC code generator) |
| `0x10b71d8f` | — | Xenon scheduler |

## Full Analysis Results

- `msvc-src/docs/COLOR_RE.md` — COLOR register allocator (linear scan, not graph coloring)
- `msvc-src/docs/INLINER_RE.md` — Inliner cost model (threshold = 150 IL nodes)
- `msvc-src/docs/G3P2_RECORD_FORM_RE.md` — Record-form fusion (`subf.`, `add.`, etc.)
