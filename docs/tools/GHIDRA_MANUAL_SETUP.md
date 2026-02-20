# Ghidra Manual Setup for DC3 Decomp

This guide is for contributors using the **Ghidra GUI directly** (no MCP server, no pyghidra). If you're just using Ghidra to read decompiled code and cross-reference the original binary while writing decomp, this is what you need.

## What You Get

After following this guide, your Ghidra project will have:

- **~69,000 named functions** (instead of `FUN_XXXXXXXX` everywhere)
- **Full demangled signatures** with calling conventions, return types, and parameter types
- **Correct PowerPC switch table recovery** (MSVC generates these differently than GCC)
- **VMX128 instruction support** (Xbox 360 SIMD — Ghidra upstream doesn't support these)

Before/after comparison of what the decompiler shows:

```
BEFORE:  void FUN_823486e0(undefined4 param_1, undefined4 param_2)
AFTER:   void __thiscall CharBonesMeshes::PoseMeshes(CharBonesMeshes *this)
```

## Prerequisites

- **Java 17+** (OpenJDK works fine)
- **Ghidra 12.0+** (we maintain a fork with Xbox 360 fixes, see below)

## Step 1: Install Our Ghidra Fork

Stock Ghidra has two problems with Xbox 360 binaries:

1. **No VMX128 support** — Xbox 360 uses 77 custom SIMD instructions that Ghidra doesn't know about. Without this, any function using vector math shows `<UNDEFINED>` instructions.
2. **Broken MSVC switch tables** — Ghidra's PowerPC switch recovery assumes GCC-style patterns. MSVC generates different code, so Ghidra misses most switch statements and shows long if-else chains instead.

Our fork fixes both: [github.com/freeqaz/ghidra](https://github.com/freeqaz/ghidra) (`master` branch)

### Option A: Use Pre-built (Recommended)

Check the [releases page](https://github.com/freeqaz/ghidra/releases) for a pre-built zip. Extract it anywhere.

### Option B: Build from Source

```bash
git clone https://github.com/freeqaz/ghidra.git
cd ghidra
git checkout master  # has VMX128 + switch fix

# Standard Ghidra build (requires Gradle)
gradle --init-script gradle/support/fetchDependencies.gradle init
gradle buildGhidra
# Output: build/dist/ghidra_12.x_DEV_YYYYMMDD.zip
```

### What the Fork Changes

| Fix | File(s) | Issue |
|-----|---------|-------|
| VMX128 pcode semantics | `Ghidra/Processors/PowerPC/data/languages/` | 77 Xbox 360 SIMD opcodes with full pcode |
| MSVC switch recovery | `PowerPCAddressAnalyzer.java` | 3 bugs in switch table analysis ([#8963](https://github.com/NationalSecurityAgency/ghidra/issues/8963)) |
| Missing `vadduws` | Altivec instruction defs | Ghidra upstream was missing this instruction |

## Step 2: Install XEXLoaderWV

Xbox 360 executables use the XEX format. Ghidra needs an extension to load them.

1. Get [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV) (build it or grab from releases)
2. Copy the extension zip to `$GHIDRA_INSTALL_DIR/Extensions/Ghidra/`
3. In Ghidra: **File > Install Extensions** and enable XEXLoaderWV
4. Restart Ghidra

After this, Ghidra will recognize `.xex` files and auto-select `PowerPC:BE:64:Xenon`.

## Step 3: Import the Binary

1. Create a new Ghidra project (or use an existing one)
2. **File > Import File** and select `orig/373307D9/default.xex`
3. XEXLoaderWV should auto-detect the format. Accept the defaults.
4. When prompted to analyze, click **Yes** and leave all analyzers enabled
5. Wait for auto-analysis to complete (~4 minutes)

> **Note:** `default.xex` is a debug build from a dev kit, not the retail binary.

## Step 4: Import Map File Symbols

This is the key step. The binary is stripped, so Ghidra only sees auto-generated names. The linker map file (`orig/373307D9/ham_xbox_r.map`) contains ~79,000 real symbol names with addresses.

### Copy the Script

Copy [`tools/ghidra/ImportMapFile.java`](../../tools/ghidra/ImportMapFile.java) to your Ghidra scripts directory:

```bash
# Find your Ghidra scripts directory
# Default: ~/ghidra_scripts/
cp tools/ghidra/ImportMapFile.java ~/ghidra_scripts/
```

Or in Ghidra: **Window > Script Manager > Manage Script Directories** and add the `tools/ghidra/` folder from this repo.

### Run the Script

1. Open the Script Manager (**Window > Script Manager**)
2. Find `ImportMapFile` (it's in the "Import" category)
3. Run it and select `orig/373307D9/ham_xbox_r.map` when prompted
4. Wait ~1-2 minutes for all three passes to complete

The script runs three passes:

| Pass | What It Does | Expected Output |
|------|-------------|-----------------|
| 1. Create functions | Creates `Function` objects at addresses where auto-analysis missed them | ~6,000 new functions |
| 2. Rename symbols | Replaces `FUN_XXXXXXXX` names with real mangled symbol names | ~27,000 renamed |
| 3. Demangle signatures | Applies full MSVC signatures (CC, return type, all params) via `MicrosoftDemangler` | ~53,000 signatures |

After this completes, save the project (**File > Save**). You won't need to run this again unless you re-import the binary.

### Headless Mode

You can also run this without the GUI:

```bash
$GHIDRA_INSTALL_DIR/support/analyzeHeadless /path/to/project DC3 \
    -import orig/373307D9/default.xex \
    -postScript ImportMapFile.java orig/373307D9/ham_xbox_r.map
```

## Step 5: Start Reversing

With symbols imported, the Ghidra experience is dramatically better:

### Navigate by Symbol Name

**Go To > Go To Address or Label** (G key) and type a class name like `CharBones`. All member functions show up with their real names.

### Cross-References

Right-click any function or data reference and select **References > Show References to** to find callers. With real symbol names, the call graph is actually readable.

### Decompiler

The decompiler now shows real types and names. MSVC `__thiscall` calling conventions are set, so `this` pointers are properly typed.

### String References

Many string literals in `.rdata` have MSVC mangled names from the map file (e.g., `??_C@_0O@EPEJKEFM@nar_bam_trans?$AA@`). Use the [`SearchString.java`](../../tools/ghidra/SearchString.java) script to search for strings by content.

## Map File Format Reference

The linker map file (`ham_xbox_r.map`) uses standard MSVC format:

```
 Address         Publics by Value              Rva+Base       Lib:Object

 0005:000186e0   ?PoseMeshes@CharBonesMeshes@@QAAXXZ 823486e0 f   char:CharBonesMeshes.obj
 ^^^^            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^^^^ ^   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 section         mangled name                          VA       type source object file
```

- **Section `0005:`** = `.text` (code). Other sections are data.
- **Type `f`** = function, `i` = inlined
- **Address** is absolute virtual address (base `0x82000000` for Xbox 360)
- **Mangled names** follow [MSVC name mangling](https://en.wikiversity.org/wiki/Visual_C%2B%2B_name_mangling) conventions

### Quick Map File Lookups

```bash
# Find a function by name
grep "PoseMeshes" orig/373307D9/ham_xbox_r.map

# Find all functions in a class
grep "@CharBones@@" orig/373307D9/ham_xbox_r.map

# Find function at an address
grep "823486e0" orig/373307D9/ham_xbox_r.map

# Count code-section symbols
grep -c "^  0005:" orig/373307D9/ham_xbox_r.map
```

## Troubleshooting

| Problem | Cause | Fix |
|---------|-------|-----|
| Binary loads as x86/DOS | XEXLoaderWV not installed | Install extension and restart Ghidra |
| `<UNDEFINED>` instructions in decompiler | Stock Ghidra without VMX128 | Use our fork (`master` branch) |
| Switch statements show as long if-else chains | Stock Ghidra switch recovery | Use our fork (fixes MSVC patterns) |
| Script says "0 symbols parsed" | Wrong section in map file | Ensure `ham_xbox_r.map` has "Publics by Value" section |
| Script runs but names don't change | Script ran on wrong program | Make sure the XEX is the active program in the Code Browser |
| `MicrosoftDemangler` not found | Old Ghidra version | Requires Ghidra 10.0+ |

## See Also

- [GHIDRA.md](GHIDRA.md) — Full Ghidra integration docs (MCP server, type seeding pipeline, CLI tools)
- [XEXLoaderWV docs](XEXLOADERWV.md) — XEX loader build/install details
- [Ghidra fork](https://github.com/freeqaz/ghidra) — VMX128 + switch fix source
- [pyghidra-mcp fork](https://github.com/freeqaz/pyghidra-mcp) — MCP server with map file support (for AI-assisted workflows)
