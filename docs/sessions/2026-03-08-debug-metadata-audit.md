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

### .XBLD section — Localized strings (not build metadata)

Contains UTF-8 Russian text (achievement descriptions). Not useful for decomp.

### .rdata$r — RTTI internal structures (154 KB)

`CompleteObjectLocator`, `BaseClassArray`, `ClassHierarchyDescriptor` tables. Cross-referenced by RTTI type descriptors. Contains vtable-to-class mappings.

## What's Untapped

### 1. Bulk mangled symbol demangling → signature verification

**37,508 function symbols** with exact type info. We could:
- Bulk-demangle all map symbols with `llvm-undname`
- Auto-verify our header declarations match (parameter types, const qualification, return types)
- Detect wrong parameter types before they cause codegen mismatches
- Build a lookup: "what's the exact signature of function X?" — no Ghidra needed

### 2. pdata prolog length → pre-screen prologue mismatches

Cross-reference `.pdata` prolog lengths against our compiled output. For every function:
- Target prolog length from `.pdata` = exact callee-saved register count
- Our compiled prolog length from our `.obj`
- If they differ → prologue mismatch guaranteed → skip or flag before wasting time

This could be integrated into `query_functions` or `run_objdiff` enrichment.

### 3. RTTI class list → header completeness check

1,080 RTTI class names vs our header files. Diff to find:
- Classes we haven't declared
- Classes with wrong names
- Inheritance chains we can verify against `.rdata$r` structures

### 4. Self-PDB generation → struct layout verification

Add `/Zi` to build → generate PDB from our objects → run `pdb-decompiler` → auto-diff struct layouts against expectations. Catches field offset mismatches before they cause assembly divergence.

### 5. Map symbol → .obj attribution for unit scoping

Every map symbol has `Lib:Object` attribution. For static symbols this tells us exactly which .obj file (compilation unit) defines them. Could improve unit-level analysis and detect symbol placement mismatches.

## Action Items

- [ ] Bulk-demangle 37K function symbols, build searchable signature database
- [ ] Cross-reference pdata prolog lengths with compiled output for pre-screening
- [ ] Diff RTTI class list against our header declarations
- [ ] Prototype `/Zi` build + pdb-decompiler struct verification pipeline
- [ ] Investigate `.rdata$r` RTTI structures for vtable-to-class mappings
