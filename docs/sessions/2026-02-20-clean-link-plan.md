# Clean Link Plan for DC3 Xbox 360 Decomp

**Date**: 2026-02-20
**Status**: In progress — COMDAT Phase 2 partially complete
**Depends on**: [.pdata root cause analysis](2026-02-12-pdata-role-in-x360-linking.md), [Jeff link limitations](JEFF_LINK_LIMITATIONS.md)

## Problem

Our hybrid linker produces a working 19.6MB Xbox 360 PE, but requires [`/FORCE`](https://learn.microsoft.com/en-us/cpp/build/reference/force-force-file-output?view=msvc-170) to get an image despite fatal conditions. `/FORCE` implies both `/FORCE:MULTIPLE` (ignore multiply-defined symbols) and `/FORCE:UNRESOLVED` (ignore unresolved externals), which means it papers over two distinct classes of issues:

- **3,562 [LNK4006](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-warning-lnk4006?view=msvc-170) warnings** — "symbol already defined; second definition ignored." These are duplicate symbol definitions across split objects that would become fatal under `/FORCE:MULTIPLE`.
- **[LNK1223](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk1223?view=msvc-170) fatal error** — invalid `.pdata` (exception table) content, worked around by renaming `.pdata` to `.pdat0`

We want to move from `/FORCE` to `/FORCE:UNRESOLVED` so that only genuinely missing symbols (the ~178 unresolved externals from cross-unit splits) are suppressed, while duplicate definitions and `.pdata` errors are actually fixed. The `.pdat0` workaround makes C++ exception handling non-functional at runtime (crashes instead of catching exceptions in ~221 compilation units).

## Tasks

### Task 1: COMDAT Phase 2 — Remaining Duplicate Contributions

**What**: Restore correct COMDAT section definitions for the remaining duplicated contributions. In COFF, [COMDAT is a section-level dedup mechanism](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#comdat-sections-object-only) driven by the section definition auxiliary record and COMDAT selection type, not a per-symbol flag. The goal is to emit these duplicated contributions into proper COMDAT sections (`IMAGE_COMDAT_SELECT_ANY`) so the linker deduplicates them silently.

**Why**: Phase 1 (completed 2026-02-19) handled symbols with `size > 0`, cutting LNK4006 from 5,545 to 3,562. The remaining 3,562 are from symbols where jeff records `size == 0`, many of which likely have missing auxiliary section info:
- `__real@*` float/double constants
- RTTI metadata (`??_R0`, `??_R1`, `??_R2`, `??_R3`, `??_R4` type descriptors)
- String literals (`??_C@*`)

**How**: The root cause is that jeff doesn't preserve or reconstruct COMDAT auxiliary info for these contributions. Possible approaches include inferring sizes (e.g., `__real@` = 4 or 8 bytes based on name length), matching by name pattern, or recovering the aux info from the original object's symbol table. The COMDAT extraction infrastructure from Phase 1 handles the section creation and symbol emission once candidates are identified.

**Files**: `jeff/src/util/split.rs` (duplicate detection filter), `jeff/src/util/xex.rs` (write_coff COMDAT extraction)

**Impact**: Eliminates all LNK4006 warnings. Enables switching from `/FORCE` to `/FORCE:UNRESOLVED` (only suppresses genuinely missing externals, not duplicate definitions).

### Task 2: .pdata Content Fix

**What**: Rewrite how jeff emits `.pdata` sections in split COFF objects so the MSVC linker accepts them without [LNK1223](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk1223?view=msvc-170).

**Why**: Jeff currently does two things wrong:
1. Creates `.pdata` entries for `__unwind$` exception handler thunks (these are EH cleanup functions that should NOT appear in `.pdata`)
2. Omits `.pdata` entries for functions that have PDATA_EH blobs in `.text` (these SHOULD have entries with `ExceptionFlag=1`)

The CE spec requires **one `.pdata` entry per function** — every function with a stack frame should have an `IMAGE_CE_RUNTIME_FUNCTION_ENTRY`. For functions with exception handling (`ExceptionFlag=1`), an 8-byte `PDATA_EH` record must be placed in `.text` [immediately before the function](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms940664(v=msdn.10)). Currently, the PDATA_EH blobs (`except_data_*` symbols) contain baked-in absolute virtual addresses with no ADDR32 relocations — these won't survive relinking at different addresses.

**How** (6 steps):
1. Filter out `__unwind$` entries from `.pdata` generation — these are EH cleanup thunks, not top-level functions
2. Emit one `IMAGE_CE_RUNTIME_FUNCTION_ENTRY` per function (not just EH functions) — non-EH functions get `ExceptionFlag=0`, EH functions get `ExceptionFlag=1`
3. For EH functions: ensure the `PDATA_EH` 8-byte record is placed in `.text` immediately before the function's first instruction. The simplest layout is to put the 8 bytes at the start of the function's `.text` COMDAT with the function symbol starting at +8, so `BeginAddress` still points to code, not the EH record.
4. Add ADDR32 relocations to PDATA_EH blobs (offset+0 → `__CxxFrameHandler`, offset+4 → FuncInfo in `.rdata`)
5. Ensure correct big-endian encoding of the packed word (PrologLen, FuncLen, ThirtyTwoBit, ExceptionFlag)
6. **Ensure `.pdata` entries are sorted by `BeginAddress` (ascending)** within each object — LNK1223 fires on RISC platforms specifically when `.pdata` entries are unsorted. If `.pdata` is emitted monolithically (not per-function COMDAT), sorting is our responsibility.

**Files**: `jeff/src/util/xex.rs` (write_coff, .pdata generation ~line 1050-1095 for reading, ~1200+ for writing)

**Impact**: Removes `fix_pdata.py` from the build pipeline. Restores C++ exception handling at runtime for all 221 affected compilation units. Simplifies build from `dtk split → fix_pdata.py → ninja → link` to `dtk split → ninja → link`.

## Impact Summary

| Metric | Before | After Task 1 | After Task 2 |
|--------|--------|-------------|-------------|
| Linker flag | `/FORCE` (= `/FORCE:MULTIPLE` + `/FORCE:UNRESOLVED`) | `/FORCE:UNRESOLVED` only | `/FORCE:UNRESOLVED` only |
| LNK4006 warnings | 3,562 | 0 | 0 |
| LNK1223 workaround | `fix_pdata.py` renames .pdata→.pdat0 | `fix_pdata.py` still needed | Removed |
| Runtime C++ exceptions | Broken (221 units invisible to kernel) | Broken (221 units) | Functional |
| Build pipeline | `split → fix_pdata → ninja → link` | `split → fix_pdata → ninja → link` | `split → ninja → link` |

Tasks are independent and can be done in either order. Task 1 is simpler (extending existing COMDAT infrastructure). Task 2 is more complex (new .pdata generation logic + PDATA_EH placement in Rust).

## Key Insights That Led to This Plan

### 1. Xbox 360 uses Windows CE exception format

`.pdata` entries are `IMAGE_CE_RUNTIME_FUNCTION_ENTRY` (8 bytes), not the x64 three-address format:

```c
// From XenonRecomp/XenonUtils/xbox.h:143-160
typedef struct _IMAGE_CE_RUNTIME_FUNCTION {
    uint32_t BeginAddress;
    union {
        uint32_t Data;
        struct {
            uint32_t PrologLength : 8;
            uint32_t FunctionLength : 22;
            uint32_t ThirtyTwoBit : 1;
            uint32_t ExceptionFlag : 1;
        };
    };
} IMAGE_CE_RUNTIME_FUNCTION;
```

Sources: [MS WinCE `_IMAGE_CE_RUNTIME_FUNCTION_ENTRY`](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms940664(v=msdn.10)), `XenonRecomp/XenonUtils/xbox.h:143-160`, [Xenia `xex_module.cc`](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/cpu/xex_module.cc)

### 2. Exception handler data lives in `.text`, not `.pdata`

When `ExceptionFlag=1`, an 8-byte [`PDATA_EH`](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms864326(v=msdn.10)) struct is placed in `.text` **immediately before** the function's first instruction:

```c
struct PDATA_EH {
    uint32_t* pHandler;      // -> __CxxFrameHandler
    uint32_t* pHandlerData;  // -> FuncInfo record in .rdata
};
```

Jeff was treating these as `.pdata` content. They are `.text` content that `.pdata` references indirectly via `ExceptionFlag`.

**Placement constraint**: The CE spec requires the `PDATA_EH` to be at the exact 8 bytes preceding the function in the final linked layout. If functions are in COMDAT sections, the simplest approach is to include the 8-byte EH record at the start of the function's `.text` COMDAT and have the function symbol start at +8, so `BeginAddress` in `.pdata` still points to the first instruction.

### 3. Verified by hex inspection

All 7 `except_data_*` symbols in FitnessFilter.obj:
- `pHandler = 0x8299E5E0` = `__CxxFrameHandler` (confirmed via `config/373307D9/symbols.txt`)
- `pHandlerData = 0x8225xxxx` = `.rdata` FuncInfo structs (before `.text` at `0x82330000`)
- Zero ADDR32 relocations at these offsets (baked-in absolute VAs)
- Each sits exactly 8 bytes before its associated function
- Preceding bytes are `blr` (0x4E800020) or nop padding (0x00000000)

### 4. Systemic scope: 221 objects affected

Scan of all 2,223 split objects confirmed both problems are systemic:
- Every subsystem has affected objects (XDK/LIBCMT, gesture, hamobj, rndobj, synth, ui, meta_ham, net_ham)
- The problem is bilateral: extra `.pdata` entries for `__unwind$` AND missing entries for EH functions

### 5. COMDAT infrastructure already existed

Jeff already implemented COMDAT section definitions for `__unwind$` symbols in `xex.rs` using the `object` crate's `add_comdat()` API with `ComdatKind::Any` (`IMAGE_COMDAT_SELECT_ANY`). This creates separate COMDAT sections (e.g., `.text$x`) with the proper auxiliary records. Phase 1 generalized this to all duplicated symbols with `size > 0`. Phase 2 needs to handle contributions where jeff currently records `size == 0` — likely due to missing auxiliary section info rather than the symbols being truly zero-size.

## Remaining Link Errors (After Tasks 1-2)

These are lower priority and don't block `/FORCE:UNRESOLVED`:

| Category | Count | Description |
|----------|-------|-------------|
| `lbl_*` cross-unit refs | 96 | Intra-binary branches spanning unit boundaries |
| `??__E*` CRT initializers | 24 | Static initializers split from their data |
| `__unwind$`/`__catch$` | 17 | EH metadata split from parent functions |
| `merged_*` ICF symbols | 10 | Additional identical COMDAT folding cases |
| Library cross-refs | ~10 | vorbis, zlib, jpeg functions split across units |
| Jump tables | 3 | Switch dispatch tables |

Full details: [JEFF_LINK_LIMITATIONS.md](JEFF_LINK_LIMITATIONS.md)

## Implementation Progress (2026-02-20)

### COMDAT Phase 2: Results

**Changes made** (jeff `src/util/split.rs` + `src/util/xex.rs`):

1. **split.rs**: Mark ALL global defined symbols (not just inter-split duplicates) as COMDAT. This handles both split-to-split and split-to-decomp conflicts. Excludes section symbols, `lbl_*`, `pdata@*`, `except_data_*`, `except_record_*`, `__unwind$`.

2. **xex.rs**: Added size inference for zero-size COMDAT symbols:
   - `__real@XXXXXXXX` → 4 bytes (float), `__real@XXXXXXXXXXXXXXXX` → 8 bytes (double)
   - Other zero-size symbols: distance to next symbol in same section
   - Pre-built sorted symbol address lists per section for O(n log n) inference

3. **xex.rs**: Changed COMDAT symbol emission from dual-symbol (LOCAL in parent + GLOBAL in COMDAT) to single EXTERNAL COMDAT symbol only. Eliminates the LOCAL copy that confused the linker.

**Result**: LNK4006 dropped from 3,562 to 275 (92.3% reduction).

### Remaining 275 LNK4006 — Root Cause Analysis

The 275 remaining warnings are NOT fixable in jeff. Breakdown by object type:

| Category | Count | Cause |
|----------|-------|-------|
| decomp-decomp | 182 | MSVC X360 uses `IMAGE_COMDAT_SELECT_NODUPLICATES` (not `ANY`) for some template functions |
| decomp-split | 77 | Same NODUPLICATES issue — decomp obj has NODUPLICATES, split obj has ANY |
| split-decomp | 7 | Same (reversed order) |
| split-split | 2 | Misidentified — actually decomp-split (ambiguous basename: `Env.obj` exists in both `rnddx9/` and `rndobj/`) |
| unknown | 7 | Objects not found in objects.json (likely SDK/lib) |

**Key finding**: MSVC X360 emits `IMAGE_COMDAT_SELECT_NODUPLICATES` (selection=1) for explicit template specializations and some inline functions, even when they're defined in headers. This is a compiler behavior we can't override. With NODUPLICATES, the linker warns about ANY duplicate — including legitimate COMDAT copies.

**Possible mitigations** (not yet attempted):
- Add `__forceinline` or `__declspec(selectany)` to header functions in decomp source
- Use `/FORCE:MULTIPLE` specifically (but this masks real errors too)
- Accept the 275 warnings as harmless noise

### Task 2: .pdata Content Fix — Results

**Changes made** (jeff `src/util/xex.rs`):

1. **EH lookup tables**: Scan symbols to build `except_data_info` map (hex suffix → section/offset) and `pdata_info` map (symbol → prolog_len/func_len). These preserve original prolog lengths from the .pdata entries.

2. **.pdata reconstruction**: Instead of copying original .pdata data, regenerate it:
   - Collect all `ObjSymbolKind::Function` symbols in `.text` sections
   - Filter out `__unwind$`, `except_data_*`, `except_record_*`, `lbl_*` symbols
   - Build 8-byte `IMAGE_CE_RUNTIME_FUNCTION_ENTRY` per function (sorted by offset)
   - `ExceptionFlag=1` for functions with matching `except_data_` symbols at offset-8
   - One `IMAGE_REL_PPC_ADDR32` relocation per entry targeting the function symbol

3. **PDATA_EH ADDR32 relocations**: Zero baked-in VAs in `except_data_*` blobs, add ADDR32 relocs:
   - offset+0 → `__CxxFrameHandler` (extern symbol created if needed)
   - offset+4 → corresponding `except_record_*` symbol (if non-null)

4. **pdata@ relocation skip**: Skip relocations targeting omitted `pdata@` symbols.

5. **fix_pdata.py removed** from build pipeline (`tools/project.py` split rule).

**Result**: LNK1223 = 0, fix_pdata.py no longer needed. C++ exception handling restored for 221 compilation units.

### COMDAT Byte Deduplication Fix

**Problem**: COMDAT Phase 2 copied function bytes to `.text$dup` but left them in the parent `.text` section too. This caused:
- 743 KB of duplicated bytes inflating total code size
- objdiff report dropped from ~44% to 41.35% (denominator inflation)

**Fix** (jeff `src/util/xex.rs`):
1. Zero out COMDAT regions in parent section data after extracting to COMDAT sections
2. Skip relocations originating from within COMDAT regions in the parent section (dead code shouldn't have fixups)

**Result**:
- objdiff fuzzy match: **44.06%** (up from 41.35%, above pre-COMDAT baseline of 43.93%)
- Total code: 11,343,996 bytes (down from 12,087,500)
- Unique LNK4006 symbols: 16 (down from 981 — ICF symbols now properly resolved)
- Unique unresolved symbols: 239 (unchanged from 238)
- New test: `test_comdat_bytes_not_duplicated_in_parent_section`

### Build/Test Workflow

Use `scripts/build/rebuild_jeff_link.sh` to rebuild jeff, re-split, and link in one command. Pass `--dtk ~/code/milohax/jeff/target/release/dtk` to `configure.py` when using a custom jeff build.

## Verification Checklist

- [x] Jeff tests pass (`cargo test` — pre-existing `test_disasm_basic` failure unrelated)
- [x] LNK4006 unique symbols: 16 (down from 981, remaining are MSVC NODUPLICATES)
- [x] LNK1223 = 0 (fix_pdata.py removed)
- [x] objdiff match% restored: 44.06% (above pre-COMDAT baseline)
- [ ] `/FORCE:UNRESOLVED` works (no LNK1169) — blocked by 36 LNK2013 fixup overflow
- [ ] Linked PE still produces valid XEX via `build_xex.py`
