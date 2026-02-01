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
| [Ghidra + pyghidra-mcp](GHIDRA.md) | Binary analysis and decompilation via MCP | [GHIDRA.md](GHIDRA.md) |
| [XEXLoaderWV](XEXLOADERWV.md) | Ghidra extension for Xbox 360 XEX files | [XEXLOADERWV.md](XEXLOADERWV.md) |
| [m2c](m2c.md) | Machine code to C decompiler | [m2c.md](m2c.md) |

## Project Scripts

| Script | Description |
|--------|-------------|
| `tools/decompile.sh` | **Combined m2c decompilation workflow** (objdiff → m2c) |
| `tools/objdiff_to_m2c.py` | Convert objdiff JSON to m2c assembly format |
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

## Quick Commands

```bash
# Build the project
ninja

# Generate progress report
ninja build/373307D9/report.json

# Find near-match functions (90-99%)
objdiff-cli report query build/373307D9/report.json --functions --min-percent 90 --max-percent 99

# Check a specific function
objdiff-cli report function build/373307D9/report.json "Game::Poll"

# Quick m2c decompilation from target binary
tools/decompile.sh "CharClip::SetFlags"

# m2c with Ghidra type context
tools/decompile.sh "CharMirror::Load" --context

# Full analysis with m2c included
./bin/analyze-function "Game::Poll" --m2c

# Manual m2c pipeline (alternative)
python3 tools/asm_to_m2c.py build/373307D9/asm/path/File.s -f FuncName | \
    python3 ~/code/milohax/m2c/m2c.py -t ppc -

# Generate decomp.me context
python3 tools/decompctx.py src/path/to/file.cpp -I include -I src
```

## Experimental Tools

| Tool | Description | Doc |
|------|-------------|-----|
| C++ Permuter | Source permutation for register allocation issues | [../permuter/INDEX.md](../permuter/INDEX.md) |

## Archived Tools

| Tool | Description | Doc | Notes |
|------|-------------|-----|-------|
| decomp-permuter | Original C permutation fuzzer | [permuter.md](permuter.md) | C only, not C++ compatible |

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
