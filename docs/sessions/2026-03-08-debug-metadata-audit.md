# Debug Metadata Audit — 2026-03-08

Investigation of all debug/metadata available in the original DC3 debug build, what we're already using, and what's untapped.

## PDB Status

The PE binary (`ham_xbox_r.exe`) contains a CodeView RSDS debug directory entry:
```
RSDS GUID: 05ba631d-3a9a-a34d-b964-8aa706494864
Age: 1
Path: e:\lazer_build_gmc1\lazer\run\ham_xbox_r.pdb
```

**The PDB file does not exist.** It was on Harmonix's build machine and never shipped. No PDB exists anywhere across milohax repos.

### Generating our own PDB

The X360 `cl.exe` (MSVC 16.00.11886.00) supports `/Zi` (enable debugging information) and `/Z7` (old-style debug info). Currently our build uses **neither** — objects have only minimal `.debug$S` (compiler version stamps, no type info).

Adding `/Zi` would generate PDB files from our build with full type information (struct layouts, member offsets, vtable structures). The `pdb-decompiler` tool at `../pdb-decompiler` could then reconstruct C++ source from it — useful for verifying our struct definitions match what the compiler actually lays out.

### pdb-decompiler capabilities

Located at `../pdb-decompiler` (Rust, ~9.6K LOC). Extracts from MSVC PDB:
- Class layouts with field offsets, sizes, bitfields
- Base classes and inheritance chains
- Method signatures with vtable offsets
- Enums, typedefs
- Reconstructed C++ header/source files organized by compilation unit

### jeff PDB support

`../jeff` (DTK/splitter) already has PDB parsing (`src/util/xpdb.rs`):
- `jeff xex pdb <file>` — prints PDB symbol info
- `try_parse_pdb()` extracts global symbols and procedures, maps them to sections
- Used during `jeff xex split` when a `pdb` path is specified in project config
- If a PDB ever surfaces, jeff can directly consume it

### Current jeff XEX parser caveat

`../jeff` already recognizes many XEX optional header IDs beyond the ones it currently prints, but its `XexOptionalHeader::new()` logic truncates pointer-style headers whose low byte is neither `0xFF` nor `< 2`.

This affects shipped metadata such as:
- `EnabledForCallcap`
- `TLSInfo`
- `ExecutionID`
- `GameRatings`
- `LANKey`

So the metadata is present in the original XEX, but `jeff xex info` does not currently surface it correctly without parser fixes.

## Complete Metadata Inventory

### Map file (`ham_xbox_r.map`) — 119,610 lines

| Category | Count | What it encodes |
|----------|-------|-----------------|
| Public symbols | 93,979 | Address, mangled name, Lib:Object attribution |
| Static symbols | 25,565 | Address, name, .obj attribution |
| Mangled function symbols | 37,508 | Full signature: class, method, return type, all param types, const/virtual |
| Mangled data symbols | 22,702 | Type info for globals/statics |
| Unique .obj files | 2,154 | Compilation unit names |
| Section layout | 15 sections | VA, size, class |

**Key insight:** Each mangled function symbol encodes the **exact function signature** — return type, all parameter types, const qualification, virtual/non-virtual, calling convention. `llvm-undname` perfectly demangles them:

```
?AddSink@Object@Hmx@@QAAXPAV12@VSymbol@@1W4SinkMode@12@_N@Z
→ public: void __cdecl Hmx::Object::AddSink(class Hmx::Object *, class Symbol, class Symbol, enum Hmx::Object::SinkMode, bool)
```

### .pdata section — 58,522 function entries

Xbox 360 `.pdata` format (8 bytes per entry):
- Bits 0-7: **Prolog length** (in instructions)
- Bits 8-29: **Function length** (in instructions)
- Bit 30: Exception flag
- Bit 31: Function flag

Prolog length distribution:
| Prolog (instructions) | Count | Meaning |
|----------------------|-------|---------|
| 4 | 25,693 | `bl __savegprlr_XX` pattern |
| 3 | 13,128 | Shorter save pattern |
| 1 | 8,821 | Leaf or minimal frame |
| 5 | 7,791 | Extended save |
| 6 | 2,331 | FPR + GPR saves |
| 0 | 27 | No prolog (naked/leaf) |

58,516 of 58,522 functions have exception handlers (near-universal EH in debug build).

### Exception metadata adjacent to .pdata entries

For `func_type == 3` entries, the 8 bytes immediately before the function start contain:
- Exception handler function pointer
- Exception record pointer

`jeff` already materializes these during XEX processing as:
- `except_data_<func>`
- `except_record_<func>`

This is richer than just "function size + prolog length":
- Exception handler entrypoints are discoverable
- Exception records in `.rdata` can be named and cross-referenced
- Catch/unwind helper symbols (`__catch$*`, `__unwind$*`) are abundant in the symbol set

This is useful for destructor-path recovery, unwind analysis, and compiler-generated helper identification.

### RTTI — 1,080 unique class names

Found in `.data` section as MSVC `TypeDescriptor` records (`.?AVClassName@@` format). Examples:
```
ADSR, Accomplishment, AccomplishmentManager, Achievements,
ObjectDir, RndDrawable, UIPanel, DataNode, ...
```

### Assertion strings — 539 source files

Format strings and `MILO_ASSERT` calls embed source file names:
```
AAFilter.cpp, Accomplishment.cpp, Object.cpp, ...
```

Also 174 `Class::Method` references in debug messages.

### Target .obj files — NO debug sections

The split target `.obj` files have **zero** `.debug$S` or `.debug$T` sections. They contain:
- COFF symbols (EXTERNAL + STATIC) with mangled names
- `.text`, `.rdata`, `.data`, `.bss`, `.pdata` sections
- No type information beyond what symbol mangling encodes

### .reloc section — 1,249,792 bytes

Full relocation table for the entire binary. Used by jeff for cross-reference resolution during splitting.

### CALLCAP — Instrumented build confirmation

XEX header `ENABLED_FOR_CALLCAP` with range `0x82ee59f4 - 0x82ee5a04`. Confirms `_CAP_Start_Profiling` and `_CAP_End_Profiling` hooks are compiled in (debug/instrumented build, no LTCG).

### Additional XEX optional headers present in the shipped binary

The original `default.xex` contains 18 optional headers. In addition to the ones already listed above, the shipped binary also includes:

| Header | Value | Notes |
|--------|-------|-------|
| `TLSInfo` | `0x40, 0x0, 0x0, 0x0` | TLS slot count 64, no raw TLS payload |
| `DefaultStackSize` | `0x00040000` | Matches PE stack reserve |
| `SystemFlags` | `0x00000220` | XEX privilege/system flag bitfield |
| `Unknown30100` | `0x00002030` | Additional privilege/flag word |
| `ExecutionID` | Title ID `0x373307D9`, version `1.0.0.0`, base version `1.0.0.0` | Strong title/build identity metadata |
| `TitleWorkspaceSize` | `0x00280000` | 2.5 MiB title workspace |
| `GameRatings` | 60-byte ratings blob | Present, not yet decoded into per-region labels |
| `LANKey` | all zeroes | Present, but not useful for decomp |
| `AlternateTitleIDs` | `0x545607D3`, `0x373307D2` | Related title IDs embedded in header |

These are not PDB substitutes, but they are real shipped metadata and should be part of the inventory.

### Import libraries — shipped XEX metadata

The XEX import header names the import libraries directly. This build imports from:
- `xam.xex` — 318 records
- `xboxkrnl.exe` — 379 records
- `xbdm.xex` — 10 records

`jeff` already uses this header plus the ordinal tables in `src/util/xex_imports.rs` to reconstruct:
- `__imp_*` symbols
- import thunks
- concrete API names for external calls

This is important decomp metadata because it gives authoritative names for imported functions without relying on PDBs or guesswork.

### .XBLD section — Localized strings (not build metadata)

Contains UTF-8 Russian text (achievement descriptions). Not useful for decomp.

### .rdata$r — RTTI internal structures (154 KB)

`CompleteObjectLocator`, `BaseClassArray`, `ClassHierarchyDescriptor` tables. Cross-referenced by RTTI type descriptors. Contains vtable-to-class mappings.

### Rich header — toolchain provenance

Separate from XEX headers, the extracted PE also contains a Rich header. Prior analysis found:
- 1,871 C++ objects built with Xbox 360 MSVC 16.00.11886
- 465 C objects built with Xbox 360 MSVC 16.00.11886
- 3 older VS2005-era objects, likely Bink/RAD middleware

This does not provide symbol/type info, but it is useful provenance:
- Confirms the dominant compiler/toolchain mix
- Identifies likely third-party middleware objects
- Strengthens confidence in compiler-version matching work

Reference: `docs/sessions/2026-01-29_compiler_flag_investigation.md`

## jeff's Full Data Pipeline (second pass)

Understanding the complete data flow from XEX → split target .obj → our decomp project.

### XEX → ObjInfo extraction (`process_xex`)

1. **Decrypt XEX** — tries retail key, then devkit (zero) key. Our build is devkit, raw compression (no LZX).
2. **Parse PE sections** — maps `.text` → Code, `.rdata` → ReadOnlyData, `.data` → Data, `.bss` → Bss. Skips `.reloc`, `.XBLD`, `.idata` (partially zeroed in debug builds), `.pdata` (processed separately).
3. **Reconstruct imports** — from XEX import header:
   - Unstrips `__imp_*` slots (swaps bytes + 0x80 ordinal flag)
   - Regenerates import thunks (16-byte `lis r11/addi r11/mtspr CTR/bctr` sequences)
   - Resolves ordinals to API names via 4,334-line lookup table (`xex_imports.rs`)
   - Handles "orphan" imports not in the XEX import library but present in `.idata`/`.xidata`
4. **Process `.pdata`** — creates `known_functions` map with exact function sizes. For `func_type == 3`, creates `except_data_*` and `except_record_*` symbols from the 8-byte exception descriptor before each function.
5. **Process `.xidata`** — validates and marks import thunk functions (size 0x10 each).
6. **Find `_RtlCheckStack`** — pattern-matches the 40-byte sled via `memmem`.

### Analysis passes (`run_cfa` + passes)

- **FindSaveRestSledsXbox** — pattern-matches 8 sled types: `__savegprlr_*`, `__restgprlr_*`, `__savefpr_*`, `__restfpr_*`, `__savevmx_*`/`__restvmx_*` (including `_upper` variants for VMX r64-r127). Creates function + label symbols for register ranges 14-32 (GPR/FPR) or 64-128 (VMX).
- **CFA (Control Flow Analysis)** — seeds from pdata functions + symbols, runs a PowerPC virtual machine executor to trace branches, discover jump tables, and merge tail-call blocks. Produces `CfaResult` with refined function boundaries and jump table locations.

### Map file processing (`apply_map_file_exe`)

- Parses all public + static symbols with address, name, unit (.obj), function flag, weak flag
- Detects ICF merged addresses (multiple symbols at same VA) — preserves original mangled names (not `merged_<addr>`)
- Skips `__imp_*`, save/restore intrinsics, `__NLG_Return`
- For pdata-known functions: preserves pdata's size (not the map's zero-size)
- Creates compilation unit splits from symbol .obj attribution

### COFF output (`write_coff`)

Generates relocatable COFF .obj files that our decomp build links against. Key details:

- **Preserves pdata prolog lengths**: Reconstructs `.pdata` section with `prolog_len | func_len | 32bit=1 | exception_flag` packed format + ADDR32 relocations to function symbols.
- **COMDAT handling**: `__unwind$*` → `.text$x` sections, globally-duplicated symbols → `.text$dup` sections, with `IMAGE_COMDAT_SELECT_ANY` semantics.
- **REL14 safety**: Functions involved in REL14 (conditional branch, ±32KB range) are kept in main `.text` even if otherwise COMDAT-eligible.
- **Relocation fixup**: Zeros out stale absolute VAs baked into the XEX. REL24 gets `-(offset_in_section)` convention. REFHI/REFLO immediates zeroed.
- **Exception data**: Zeros baked-in VAs in `except_data_*` blobs, adds ADDR32 relocations to handler/record symbols.

### What's already flowing through

| Data | Source | Preserved in target .obj? | Used by objdiff? |
|------|--------|--------------------------|-----------------|
| Function boundaries (size) | pdata → jeff | Yes (pdata entries) | Yes |
| Prolog length | pdata → jeff | **Yes** (packed in pdata word1) | **Not directly** |
| Mangled symbol names | map → jeff → symbols.txt | Yes (COFF symbols) | Yes (demangled in report.json) |
| Function addresses | map/pdata → jeff | Yes (section offsets) | Yes |
| Exception handlers | pdata type 3 → jeff | Yes (except_data/record) | No |
| COMDAT/ICF info | map merged addrs → jeff | Yes (.text$dup sections) | Partially (ICF detection) |
| Import API names | XEX imports → jeff | Yes (__imp_* symbols) | Yes |
| Relocations | CFA tracker → jeff | Yes (COFF relocs) | Yes |
| Save/restore sleds | pattern match → jeff | Yes (labeled symbols) | Yes |
| Compilation unit splits | map .obj attribution | Yes (one .obj per unit) | Yes |

### Current symbol coverage

- `config/373307D9/symbols.txt`: **211,879** symbols (69,336 functions with sizes)
- `config/373307D9/splits.txt`: **2,211** compilation units, **10,338** section ranges
- Target .obj files: **4,447** (includes SDK splits)
- Source .obj files (our build): **981**
- Map .obj → splits match rate: **2,132/2,155 (98.9%)**

### RTTI class coverage

- RTTI TypeDescriptors in binary: **1,080** classes + **36** structs
- Our header class declarations: **1,460**
- RTTI classes missing from our headers: **200** (130 after filtering SDK/NUISPEECH)
  - Many are `*Msg` classes (auto-generated message types): `ButtonDownMsg`, `SigninChangedMsg`, etc.
  - Real game classes missing: `BinkMovieLoader`, `GainEffect`, `NullLoader`, `HeadsetPlaybackEffect`, `ResourceFileCacheHelper`
  - Nested classes: `FontMap@RndText`, `Transitions@CharClip`, `SpotlightResources@NgSpotlightDrawer`
  - Middleware: `SoundTouch@soundtouch`, `FIRFilter@soundtouch`, `SynapseAPO@DSP`

## What's Untapped

### 1. Bulk mangled symbol demangling → signature verification

**37,508 function symbols** with exact type info. We could:
- Bulk-demangle all map symbols with `llvm-undname`
- Auto-verify our header declarations match (parameter types, const qualification, return types)
- Detect wrong parameter types before they cause codegen mismatches
- Build a lookup: "what's the exact signature of function X?" — no Ghidra needed

### 2. pdata prolog length → pre-screen prologue mismatches

**Jeff already preserves prolog lengths** in the target .obj `.pdata` sections. The packed word1 field encodes `prolog_len` in bits[7:0]. Our compiled .obj files also have `.pdata` with our prolog lengths.

We could diff these cheaply — read word1 from both target and base `.pdata` entries for each function:
- If prolog lengths differ → prologue mismatch guaranteed → skip or flag before wasting time
- If they match → function has same register save pattern → higher chance of matching

This could be integrated into `query_functions` or `run_objdiff` enrichment. The data is already in the .obj files — no new extraction needed.

### 3. RTTI class list → header completeness check

1,080 RTTI class names vs our header files. Diff to find:
- Classes we haven't declared
- Classes with wrong names
- Inheritance chains we can verify against `.rdata$r` structures

### 4. Self-PDB generation → struct layout verification

Add `/Zi` to build → generate PDB from our objects → run `pdb-decompiler` → auto-diff struct layouts against expectations. Catches field offset mismatches before they cause assembly divergence.

### 5. Map symbol → .obj attribution for unit scoping

Every map symbol has `Lib:Object` attribution. For static symbols this tells us exactly which .obj file (compilation unit) defines them. Could improve unit-level analysis and detect symbol placement mismatches.

### 6. Exception record mining

We already have enough metadata to build an exception-oriented lookup:
- function -> exception handler
- function -> exception record
- function -> `__catch$*` / `__unwind$*` helpers nearby

This would help:
- identify compiler-generated cleanup code faster
- separate source logic from EH scaffolding
- improve analysis of destructor-heavy or exception-heavy functions

### 7. Import metadata surfacing

`jeff` already reconstructs imported APIs from the XEX import header and ordinal LUTs, but we do not expose that inventory prominently in our docs/tooling.

We could:
- dump a complete import inventory for the title
- annotate decomp callsites with authoritative external API names
- use import families (`xam`, `xboxkrnl`, `xbdm`) as a fast subsystem hint during triage

### 8. XEX optional header decoding / validation

The shipped XEX carries more metadata than our current audit originally listed, and `jeff` currently under-parses some of it.

Fixing optional-header decoding would let us reliably expose:
- title identity (`ExecutionID`, alternate title IDs)
- memory model (`DefaultStackSize`, `TitleWorkspaceSize`, TLS)
- instrumentation/debug flags (`CALLCAP`)
- system capability/privilege flags

### 9. jeff's XexOptionalHeader parsing bug

`XexOptionalHeader::new()` (xex.rs:399-426) uses `mask = id & 0xFF` to determine how to read header data:
- `mask == 0xFF` → pointer to length-prefixed blob (correct)
- `mask < 2` → immediate 4-byte value (correct)
- `else` → `mask * 4` bytes at `value + 4` (**incorrect for many headers**)

For `EnabledForCallcap` (ID `0x18102`, mask=2), it reads `2*4=8` bytes at `value+4`, skipping the first 4 bytes. Similar issues for `TLSInfo` (mask=4), `GameRatings` (mask=0x10), `LANKey` (mask=4), `ExecutionID` (mask=6).

The fix is straightforward: read `mask * 4` bytes starting at `value` (not `value + 4`), since the data pointer already points to the header data, not a length-prefixed block.

## Action Items

**High-impact for decomp:**
- [ ] Bulk-demangle 37K function symbols, build searchable signature database
- [ ] Cross-reference pdata prolog lengths (already in target .obj) with compiled output for pre-screening
- [ ] Diff RTTI class list against our header declarations (130 game classes missing)

**Medium-impact tooling:**
- [ ] Prototype `/Zi` build + pdb-decompiler struct verification pipeline
- [ ] Investigate `.rdata$r` RTTI structures for vtable-to-class mappings and hierarchy validation
- [ ] Add exception-record / exception-handler mining to metadata workflows

**jeff improvements:**
- [ ] Fix `XexOptionalHeader::new()` offset-by-4 for non-0xFF pointer-style headers
- [ ] Surface XEX import library inventory and decoded imported APIs in tooling/docs
- [ ] Fold Rich header provenance into the canonical metadata inventory
