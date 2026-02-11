# Tools Documentation Index

Quick reference for tools used in the DC3 decompilation project.

## Start Here

| Doc | Description |
|-----|-------------|
| **[WORKFLOW.md](WORKFLOW.md)** | **Decision guide: which tool to use when** |

## Decompilation Tools

| Tool | Description | Doc |
|------|-------------|-----|
| [analyze-function](ANALYZE_FUNCTION.md) | Combined objdiff + Ghidra analysis (start here) | [ANALYZE_FUNCTION.md](ANALYZE_FUNCTION.md) |
| [objdiff](objdiff.md) | Assembly diffing and function matching analysis | [objdiff.md](objdiff.md) |
| diff_inspect | Deep mismatch analysis (diagnose, clusters, regswaps, offsets, replaces, compare) | [WORKFLOW.md](WORKFLOW.md#diff_inspect) |
| [Ghidra + pyghidra-mcp](GHIDRA.md) | Binary analysis and decompilation via MCP | [GHIDRA.md](GHIDRA.md) |
| [XEXLoaderWV](XEXLOADERWV.md) | Ghidra extension for Xbox 360 XEX files | [XEXLOADERWV.md](XEXLOADERWV.md) |
| [m2c](m2c.md) | Machine code to C decompiler | [m2c.md](m2c.md) |

## Project Scripts

| Script | Description |
|--------|-------------|
| `tools/decompile.sh` | **Combined m2c decompilation workflow** (objdiff → m2c) |
| `tools/objdiff_to_m2c.py` | Convert objdiff JSON to m2c assembly format (with jump table resolution) |
| `tools/ghidra/export_types.py` | Export Ghidra types as m2c context headers |
| `tools/asm_to_m2c.py` | Convert DC3 dtk assembly to m2c-compatible format |
| `tools/decompctx.py` | Generate context files for decomp.me |
| `configure.py` | Generate build files (ninja) |

## Symbol Lookup (Map File)

The linker map file `orig/373307D9/ham_xbox_r.map` contains all symbol names and addresses:

```bash
# Find function address by name
grep "FastSin\|Pool::Alloc" orig/373307D9/ham_xbox_r.map

# Example output:
# 0005:002027e8       ?FastSin@@YAMM@Z           825327e8 f   math:Trig.obj
#                     ^ mangled name              ^ address    ^ source file
```

## Merged Symbol Lookup (ICF)

When objdiff shows `LINKER_MERGED` patterns with `merged_<address>` symbols, use the merged-symbols tool to identify the actual symbol names:

```bash
# Look up what symbols are at a merged address
./bin/merged-symbols 82331360

# Also accepts the merged_ prefix from objdiff output
./bin/merged-symbols merged_82331448 -v

# See statistics on all merged symbols
./bin/merged-symbols --stats -e

# Output as JSON
./bin/merged-symbols 82331360 --json
```

ICF (Identical COMDAT Folding) merges functions with identical machine code to save space. Common patterns:
- `??_G` / `??_E`: Scalar and vector deleting destructors (identical code)
- Template instantiations like `ObjRefConcrete<T>::GetObj()` (same code for different T)

## Function Database (decomp.db)

SQLite database tracking all functions, patterns, and scoring:

```bash
# Find high-priority reachable targets
sqlite3 decomp.db "SELECT symbol, current_percent FROM functions WHERE reachable_100=1 AND current_percent < 100 ORDER BY priority_score DESC LIMIT 10"

# Query functions by pattern
sqlite3 decomp.db "SELECT symbol, current_percent FROM functions WHERE has_linker_merged=1 ORDER BY current_percent DESC LIMIT 10"
```

See [../reference/DATABASE_SCHEMA.md](../reference/DATABASE_SCHEMA.md) for full schema documentation.

## Linking Tools

| Script | Description |
|--------|-------------|
| `scripts/link_test.py` | Standalone X360 link test (links split/hybrid .obj → PE) |
| `scripts/compare_pe.py` | Compare linked PE against original `ham_xbox_r.exe` |
| `scripts/fix_pdata.py` | Workaround for dtk .pdata splitting bug (integrated into `ninja link`) |

See [../sessions/2026-02-11-x360-linking-pipeline.md](../sessions/2026-02-11-x360-linking-pipeline.md) for full status and roadmap.

## Quick Commands

```bash
# Build the project
ninja

# Generate progress report
ninja build/373307D9/report.json

# Link hybrid PE (requires wine)
ninja link

# Find near-match functions (90-99%)
objdiff-cli report query build/373307D9/report.json --functions --min-percent 90 --max-percent 99

# Check a specific function (markdown output is default)
objdiff-cli diff -p . "Game::Poll" --verdict

# Diff with context around mismatches
objdiff-cli diff -p . "Game::Poll" --verdict -C 3

# Check function info from report
objdiff-cli report function build/373307D9/report.json "Game::Poll"

# Quick m2c decompilation from target binary
tools/decompile.sh "CharClip::SetFlags"

# m2c with Ghidra type context
tools/decompile.sh "CharMirror::Load" --context

# Full analysis with m2c included
./bin/analyze-function "Game::Poll" --m2c

# Manual m2c pipeline (alternative, with jump table support)
./bin/objdiff-cli diff -p . "Foo::Bar" -f json --include-instructions | \
    python3 tools/objdiff_to_m2c.py --project-dir . | \
    python3 ~/code/milohax/m2c/m2c.py -t ppc -

# Generate decomp.me context
python3 tools/decompctx.py src/path/to/file.cpp -I include -I src
```

## Code Transformation Tools

| Tool | Description | Doc |
|------|-------------|-----|
| C++ Permuter | Tree-sitter based source permutation for register allocation issues | [../permuter/INDEX.md](../permuter/INDEX.md) |

## Archived Tools

| Tool | Description | Doc | Notes |
|------|-------------|-----|-------|
| decomp-permuter | Original C permutation fuzzer | [permuter.md](permuter.md) | C only, uses pycparser which doesn't support C++ |

## Projects

| Project | Description | Doc |
|---------|-------------|-----|
| VMX128 Ghidra Support | Adding Xbox 360 SIMD instruction support to Ghidra | [../vmx128/README.md](../vmx128/README.md) |

## Compiler Documentation

| Doc | Description |
|-----|-------------|
| [PRAGMA_INDEX.md](../decomp/PRAGMA_INDEX.md) | Xbox 360 compiler pragma documentation index |
| [PRAGMA_MATCHING_CHECKLIST.md](../decomp/PRAGMA_MATCHING_CHECKLIST.md) | Step-by-step guide for using pragmas to match functions |
| [PRAGMA_CODEGEN_SUMMARY.md](../decomp/PRAGMA_CODEGEN_SUMMARY.md) | Quick reference for pragma impact on code generation |
| [XBOX360_PRAGMA_REFERENCE.md](../decomp/XBOX360_PRAGMA_REFERENCE.md) | Complete technical reference for all code-generation pragmas |

**Key pragmas for matching:**
- `#pragma fp_contract(on|off)` - Controls fused multiply-add instruction generation (fmadds)
- `#pragma optimize("u", on|off)` - Controls prescheduling (instruction ordering)
- `#pragma bitfield_order(msb_to_lsb|lsb_to_msb)` - Controls bitfield packing order

## External Resources

- [objdiff GUI](https://github.com/encounter/objdiff) - Visual diff tool
- [m2c online](https://simonsoftware.se/other/m2c.html) - Browser-based m2c
- [decomp.me](https://decomp.me) - Collaborative decompilation scratches
