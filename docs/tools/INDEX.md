# Tools Index — Agent Tool Selection

Which tool to use for decomp work. For scripts, commands, and reference material, see [REFERENCE.md](REFERENCE.md).

## Start Here

| Doc | Description |
|-----|-------------|
| **[WORKFLOW.md](WORKFLOW.md)** | **Decision guide: which tool to use when** |

## MCP Orchestrator Tools (Primary Interface)

Use `mcp__orchestrator__` tools for all decomp analysis. Do not call `objdiff-cli` directly.

| Tool | Description |
|------|-------------|
| `run_objdiff` | Build + diff a function. Returns match%, verdict. Source of truth for percentages. |
| `run_diff_inspect` | Deep mismatch analysis: `diagnose`, `mismatches`, `clusters`, `regswaps`, `offsets`, `replaces`, `compare` |
| `run_analyze_function` | Combined objdiff + struct offset resolution for field-level context |
| `query_functions` | Find workable functions by unit pattern and match range |
| `lookup_rb3` | Search RB3 codebase for reference implementations (shared Milo engine) |
| `get_rb3_pair` | Get RB3 file pairing info + optional source for a DC3 unit |
| `get_rb2_class_info` | Class layout from RB2 DWARF dump (member offsets, sizes, inheritance) |
| `lookup_struct_offset` | Look up which struct field is at a given offset |
| `struct_info` | Get class/struct members, parents, inheritance chain |
| `lookup_merged_symbol` | Resolve `merged_<addr>` to actual symbol names (ICF) |

## Decompilation Tools

| Tool | Description | Doc |
|------|-------------|-----|
| [analyze-function](ANALYZE_FUNCTION.md) | Combined objdiff + Ghidra analysis (start here) | [ANALYZE_FUNCTION.md](ANALYZE_FUNCTION.md) |
| [objdiff](objdiff.md) | Assembly diffing and function matching analysis | [objdiff.md](objdiff.md) |
| diff_inspect | Deep mismatch analysis (diagnose, clusters, regswaps, offsets, replaces, compare) | [WORKFLOW.md](WORKFLOW.md#diff_inspect) |
| [Ghidra + pyghidra-mcp](GHIDRA.md) | Binary analysis, decompilation, and type seeding via MCP | [GHIDRA.md](GHIDRA.md) |
| [Ghidra Manual Setup](GHIDRA_MANUAL_SETUP.md) | GUI-only Ghidra setup (no MCP) — symbol import, fork install | [GHIDRA_MANUAL_SETUP.md](GHIDRA_MANUAL_SETUP.md) |
| [XEXLoaderWV](XEXLOADERWV.md) | Ghidra extension for Xbox 360 XEX files | [XEXLOADERWV.md](XEXLOADERWV.md) |
| [m2c](m2c.md) | Machine code to C decompiler | [m2c.md](m2c.md) |

## Ghidra CLI Analysis Tools

Require running pyghidra-mcp service (`./tools/ghidra/pyghidra-service.sh start`) and seeded DTM (`python3 tools/ghidra/batch_export_types.py --seed`). See [GHIDRA.md](GHIDRA.md#cli-analysis-tools).

| Tool | Description | Usage | Skill |
|------|-------------|-------|-------|
| `struct_check.py` | Compare header struct layouts vs Ghidra DTM | `python3 tools/ghidra/struct_check.py HamDirector` | `/ghidra-struct` |
| `pcode_inspect.py` | Switch table + cast analysis from decompiled output | `python3 tools/ghidra/pcode_inspect.py "Class::Method" --switches` | `/ghidra-decompile` |
| `code_search.py` | Semantic search over 42K+ decompiled functions (auto-filters `__unwind$` noise) | `python3 tools/ghidra/code_search.py "iterate list delete"` | `/ghidra-search` |

## Dynamic Analysis

| Tool | Description | Doc |
|------|-------------|-----|
| [Unicorn Function Runner](UNICORN_FUNCTION_RUNNER.md) | Differential function execution (Unicorn PPC32 BE) — compare decomp vs original behavior | [UNICORN_FUNCTION_RUNNER.md](UNICORN_FUNCTION_RUNNER.md) |

```bash
# Combined diagnosis with SKIP/FIX recommendations
python3 -m scripts.unicorn_runner.diagnose --unit system/meta/Profile --batch

# Multi-input probing for higher confidence
python3 -m scripts.unicorn_runner.probe --unit DirLoader --batch --runs 8

# Find functions with real behavioral bugs (logic divergences)
./bin/orchestrate divergent --limit 20

# Run batch to fix divergent functions
./bin/orchestrate batch --strategy divergent --limit 10
```

## Compiler Analysis

| Tool | Description | Doc |
|------|-------------|-----|
| [Compiler Trace](compiler-trace.md) | c2.dll instrumentation: asm diff, IL capture, perf profiling, GDB scripting | [compiler-trace.md](compiler-trace.md) |

```bash
# Compare assembly for two source variants (detects register swaps)
python -m tools.compiler_trace diff-asm test_a.cpp test_b.cpp

# Capture compiler IL temp files
python -m tools.compiler_trace capture-il test.cpp --output-dir /tmp/il_out

# Profile and diff c2.dll execution paths
python -m tools.compiler_trace callgrind-diff test_a.cpp test_b.cpp
```

## Post-Build Tools

| Tool | Description | Doc |
|------|-------------|-----|
| Register Swap Patcher | Patches .obj register fields using objdiff diff as oracle (manual, not run by default) | [REFERENCE.md](REFERENCE.md#register-swap-patcher) |

## Code Transformation Tools

| Tool | Description | Doc |
|------|-------------|-----|
| C++ Permuter | Tree-sitter based source permutation for register allocation issues | [../permuter/INDEX.md](../permuter/INDEX.md) |
