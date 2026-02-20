# Jeff (dtk) Linking Limitations

Date: 2026-02-19

## Context

Jeff (`../jeff`, our custom dtk fork) splits the original XEX into relocatable COFF objects for hybrid linking with decomp objects. This document tracks known issues preventing a clean link (without `/FORCE`).

## Current Link Status

**242 unresolved errors + 275 LNK4006 duplicate warnings** (178 unique unresolved symbols).
LNK4006 reduced: 5,545 → 3,562 (COMDAT Phase 1, 2026-02-19) → 275 (COMDAT Phase 2, 2026-02-20).
Remaining 275 LNK4006 are MSVC `NODUPLICATES` selection type — not fixable in jeff.

### Fixed 2026-02-20 (COMDAT Phase 2)
- **All global symbols in split objects now COMDAT**: marks every global defined symbol (not just inter-split duplicates) as `IMAGE_COMDAT_SELECT_ANY`
- **Zero-size symbol support**: infers size for `__real@*` (4/8 bytes), RTTI, string literals, vftables via distance-to-next-symbol
- **Single COMDAT symbol emission**: removed dual LOCAL+EXTERNAL pattern; now only EXTERNAL in COMDAT section
- **Rebuild script**: `scripts/build/rebuild_jeff_link.sh` — builds jeff, re-splits, links, shows error summary

### Fixed 2026-02-19 (COMDAT Phase 1, ICF, etc.)
- **ICF symbols resolved**: `link_glue.cpp` provides definitions for `operator delete`, `DataArray::Node`, `MemOrPoolFreeSTL` (eliminated 292 errors)
- **.pdata LNK1223 fixed**: `fix_pdata.py` now chains after split automatically
- **VA shift fixed**: `/MERGE:.xidata=.text` puts `.text` at correct VA `0x82330000`
- **COMDAT Phase 1**: marked global symbols with `size > 0` duplicated across split objects

## Remaining Issues

### 1. Duplicate Symbols — 275 LNK4006 warnings (down from 5,545)

**Status (2026-02-20)**: COMDAT Phase 2 complete. All jeff-fixable duplicates resolved.

Remaining 275 warnings caused by MSVC X360 compiler using `IMAGE_COMDAT_SELECT_NODUPLICATES` (selection=1) for some template specializations. Breakdown:
- 182 decomp-decomp (MSVC behavior, can't fix in jeff)
- 77 decomp-split (NODUPLICATES in decomp conflicts with ANY in split)
- 9 split-decomp/split-split (ambiguous basenames, actually decomp-involved)
- 7 unknown (SDK/lib objects)

**Impact**: `/FORCE:UNRESOLVED` should work (LNK4006 is a warning, not an error). Testing pending.

**Files**: `jeff/src/util/xex.rs` (write_coff, COMDAT extraction), `jeff/src/util/split.rs` (COMDAT marking in split_obj)

### 2. Cross-Unit Label References — 96 unresolved `lbl_*`

Jeff commit `e48b587` globalized `lbl_*` symbols within their own object, but labels referencing addresses in OTHER compilation units remain unresolved. These are intra-binary branches that span unit boundaries.

**Example**: `unit_A.obj` references `lbl_82XXXXXX` which lives in `unit_B.obj`.

**Fix options**:
1. Jeff could create extern declarations for cross-unit label references
2. Jeff could detect and merge units that share cross-unit label references
3. A post-split glue step could create forwarding stubs

### 3. CRT Dynamic Initializers — 24 unresolved `??__E*`

C++ static initializers (e.g., `??__EgOverride@@YAXXZ`) referenced from `auto_08_82F05C00_data.obj`. These are initializer functions split into separate objects from the data they initialize.

**Fix**: Jeff could either keep initializer functions with their data objects, or emit them in a CRT init section.

### 4. EH Metadata — 15 `__unwind$` + 2 `__catch$`

Exception handling unwind/catch symbols split across objects. The original linker resolves these from the same compilation unit.

**Fix**: Jeff could keep EH metadata colocated with its parent function during splitting.

### 5. ICF Merged Symbols — 10 remaining `merged_*`

Additional Identical COMDAT Folding cases beyond what `link_glue.cpp` covers. These are functions merged to a single address by the original linker.

**Fix**: Identify which canonical functions these map to and add stubs to `link_glue.cpp`, or fix in jeff's COMDAT output.

### 6. Library Cross-References — ~10 symbols

Missing vorbis (`floor0_*`, `vorbis_lpc_*`), zlib (`zcfree`, `_tr_stored_block`, `compressBound`), jpeg (`jpeg_mem_*`, `jpeg_get_*`), and other library functions.

**Root cause**: Library objects split into multiple units, with cross-references between them unresolved.

### 7. Jump Tables — 3 remaining

`jumptable_820050E8`, `jumptable_82005128`, `jumptable_8206E238` — known dtk limitation for switch statement dispatch tables.

## Workarounds In Place

| Issue | Workaround | Automated? |
|-------|-----------|------------|
| .pdata LNK1223 | `fix_pdata.py` renames .pdata→.pdat0 (see [detailed analysis](2026-02-12-pdata-role-in-x360-linking.md#8-root-cause-of-lnk1223-in-split-objects-2026-02-19)) | Yes (chained after split in build.ninja) |
| VA shift +0x1600 | `/MERGE:.xidata=.text` in ldflags | Yes (config.json) |
| ICF op delete/Node/MemPool | `link_glue.cpp` provides definitions | Yes (injected in configure.py) |
| All remaining | `/FORCE` linker flag | Yes (config.json) |

## Build Pipeline

```
dtk xex split → fix_pdata.py → configure.py (injects link_glue unit) → ninja → link with /FORCE
```

## Priority for Jeff Fixes

1. **COMDAT marking — phase 2** (handle size-0 symbols: `__real@*`, RTTI, string literals → eliminates remaining 3,562 LNK4006)
2. **.pdata content fix** (filter `__unwind$` from .pdata entries, add ADDR32 relocs to PDATA_EH blobs → eliminates LNK1223 without `fix_pdata.py`)
3. **Cross-unit label resolution** (eliminates 96 lbl_* errors)
4. **EH colocation** (eliminates 17 __unwind/__catch errors)
5. **CRT init colocation** (eliminates 24 ??__E errors)
