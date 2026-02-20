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

1. Get [XEXLoaderWV](https://github.com/zeroKilo/XEXLoaderWV) — build from source or grab a release zip
2. Copy the extension zip to `$GHIDRA_INSTALL_DIR/Extensions/Ghidra/`
3. In Ghidra: **File > Install Extensions** and enable XEXLoaderWV
4. Restart Ghidra

After this, Ghidra will recognize `.xex` files and auto-select `PowerPC:BE:64:Xenon`.

### Building XEXLoaderWV from Source

Requires **JDK 17+** (not JRE — you'll get `"Toolchain installation does not provide JAVA_COMPILER"` with JRE).

```bash
git clone https://github.com/zeroKilo/XEXLoaderWV.git
cd XEXLoaderWV/XEXLoaderWV

JAVA_HOME=/usr/lib/jvm/java-17-openjdk \
  $GHIDRA_INSTALL_DIR/support/gradle/gradlew \
  -PGHIDRA_INSTALL_DIR=$GHIDRA_INSTALL_DIR

# Output: dist/ghidra_*_XEXLoaderWV.zip
```

## Step 3: Import the Binary

### GUI Method

1. Create a new Ghidra project (or use an existing one)
2. **File > Import File** and select `orig/373307D9/default.xex`
3. XEXLoaderWV should auto-detect the format
4. In the import options, make sure **Process .pdata** is enabled (adds function symbols from exception data)
5. When prompted to analyze, click **Yes** and leave all analyzers enabled
6. Wait for auto-analysis to complete (~4 minutes)

> **Note:** `default.xex` is a debug build from a dev kit, not the retail binary.

### One-Command Headless Setup

If you prefer the command line, [`import-xex.sh`](../../tools/ghidra/import-xex.sh) does everything in one shot — imports the XEX, runs full analysis, and applies all map symbols:

```bash
./tools/ghidra/import-xex.sh
```

This creates a Ghidra project at `ghidra_projects/DC3/` that you can then open in the GUI. It runs Steps 3 and 4 together automatically.

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

## Step 5: Useful Scripts

Beyond ImportMapFile, there are two more standalone GhidraScripts for common tasks. Copy them to your scripts directory or add `tools/ghidra/` to Script Manager.

### String Search — `SearchString.java`

Search for strings by content across both defined string data and raw memory:

```bash
# GUI: Script Manager > Search category > SearchString
# Headless:
./tools/ghidra/search-string.sh "dance"
```

Finds matches in:
- **Defined strings** — strings Ghidra already identified in `.rdata`
- **Raw memory** — ASCII byte patterns that Ghidra missed

Useful for finding `MILO_ASSERT` messages, file paths, class names, and error strings referenced by functions you're decompiling.

### String Enumeration — `StringSearch.java`

Lists all memory blocks and samples defined strings. Good for verifying your import worked correctly:

```
# GUI: Script Manager > Search category > StringSearch
```

Shows memory layout (`.text`, `.rdata`, `.data` sections with sizes and permissions) and the first 50 defined strings.

## Step 6: Start Reversing

With symbols imported, the Ghidra experience is dramatically better:

### Navigate by Symbol Name

**Go To > Go To Address or Label** (G key) and type a class name like `CharBones`. All member functions show up with their real names.

### Cross-References

Right-click any function or data reference and select **References > Show References to** to find callers. With real symbol names, the call graph is actually readable.

### Decompiler

The decompiler now shows real types and names. MSVC `__thiscall` calling conventions are set, so `this` pointers are properly typed.

### String References

Many string literals in `.rdata` have MSVC mangled names from the map file (e.g., `??_C@_0O@EPEJKEFM@nar_bam_trans?$AA@`). Use `SearchString.java` (see Step 5) to search for strings by content instead of trying to decode mangled names by hand.

### Verifying Struct Layouts

The project maintains a database of 2,100+ class/struct layouts in `struct_db.sqlite` (built from our annotated C++ headers). When you're investigating offset mismatches in the decompiler:

1. Right-click an address in Ghidra > **Data > Create Structure** to see Ghidra's inferred layout
2. Compare against our headers in `include/` (e.g., `include/system/char/CharBones.h`)
3. The map file gives you member function addresses — if a function at offset `0x48` is wrong, grep the map for the class name to find what should be there

## Reading PPC Decompilation Output

Ghidra's decompiler output for PowerPC has some patterns that look confusing but have simple meanings.

### LZCOUNT Patterns = Boolean Negation

```c
// Ghidra shows:
y = LZCOUNT(x) >> 5;
// Means:
y = !x;
```

The PowerPC `cntlzw` (count leading zeros) instruction returns 32 for zero input and 0-31 for non-zero. Shifting right by 5 gives 1 for zero, 0 for non-zero — it's how MSVC compiles `!x` on PowerPC.

Variants you'll see:
- `LZCOUNT(x) >> 5` — simple `!x`
- `(uint)LZCOUNT(x) >> 5` — same thing with a cast
- `(ulonglong)(LZCOUNT(x) << 0x20) >> 0x25` — 64-bit variant, still `!x`

### Merged Symbols = Identical Code Folding

Functions named `merged_82XXXXXX` are the result of MSVC's Identical COMDAT Folding (ICF). The linker detected multiple functions with identical machine code and merged them to a single address. Common examples:

- Scalar and vector deleting destructors for the same class
- Trivial getters that return the same offset
- Empty virtual function stubs

To find what a merged symbol actually is, grep the map file by address:
```bash
grep "82331360" orig/373307D9/ham_xbox_r.map
```

### Switch Statements as If-Else Chains

Even with our fork's switch fix, some complex switches may still decompile as if-else chains. If you see a long series of comparisons against sequential integers, it's likely a switch statement in the original source. Look for `bctr` instructions in the listing view — that's the jump table dispatch.

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
| `"Toolchain does not provide JAVA_COMPILER"` | JRE installed instead of JDK | Install JDK: `pacman -S jdk-openjdk` (Arch) or `apt install openjdk-17-jdk` (Debian) |
| Missing function symbols after XEX load | `.pdata` processing disabled | Re-import with **Process .pdata** enabled in XEXLoaderWV options |

## See Also

- [GHIDRA.md](GHIDRA.md) — Full Ghidra integration docs (MCP server, type seeding pipeline, CLI tools)
- [XEXLoaderWV docs](XEXLOADERWV.md) — XEX loader build/install details
- [Ghidra fork](https://github.com/freeqaz/ghidra) — VMX128 + switch fix source
- [pyghidra-mcp fork](https://github.com/freeqaz/pyghidra-mcp) — MCP server with map file support (for AI-assisted workflows)
