# Jeff (dtk) Linking Limitations

Date: 2026-02-20 (updated 2026-02-26)

## Context

Jeff (`../jeff`, our custom dtk fork) splits the original XEX into relocatable COFF objects for hybrid linking with decomp objects. This document tracks known issues preventing a clean link (without `/FORCE`).

## Current Link Status (2026-02-26)

**0 errors, 756 LNK4006 warnings.** `/FORCE:UNRESOLVED` has been dropped.

| Metric | Before (2026-02-20) | After (2026-02-26) |
|--------|---------------------|--------------------|
| Link errors | 666 | **0** |
| LNK4006 warnings | 275 | 756 (more Matching units = more COMDAT overlap) |
| Linker flags | `/FORCE:MULTIPLE` + `/FORCE:UNRESOLVED` | `/FORCE:MULTIPLE` only |
| Bad bl instructions | 99 | 99 |

### How We Got to 0 Errors

Three approaches combined to resolve all 666+ unresolved symbols:

1. **Data stubs** (`scripts/create_data_stubs.py`): Creates data-only .obj files from split .objs for Matching units. When Config B replaces split .obj with decomp .obj, cross-references from other split .objs that use `lbl_*` names break. Data stubs provide those `lbl_*` data symbols alongside the decomp code. Resolved 577 `lbl_*`, 3 `jumptable_*`, 28 `__real@*` symbols. (commit 01666139)

2. **ALTERNATENAME stubs** (`src/link_glue.cpp`): 72 remaining symbols (audio SDK, UIPanel vtordisp thunks, STL templates, __unwind$ records) resolved via `/ALTERNATENAME:mangled=__link_glue_noop` linker directives. (commit 6158ec49)

3. **Wibo CRC fix + path mapping**: Fixed `??_C@` string literal hashes. Wibo's `RtlComputeCrc32` now returns real CRC-32 values. `WIBO_PATH_MAP` environment variable maps `src/system` → `e:/lazer_build_gmc1/system/src` so `__FILE__` paths match the original build.

4. **Anonymous namespace patcher** (`scripts/obj_anon_ns_patcher.py`): Patches MSVC anonymous namespace hashes in compiled .obj files to match the original binary's hashes. Integrated into ninja build pipeline.

### LNK4006 Warning Breakdown (756 total)

| Category | Count | Notes |
|----------|-------|-------|
| Template instantiations / inline functions | ~654 | Inherent to hybrid linking |
| SafeName ICF copies | 54 | Same function in many TUs |
| link_glue.cpp obsolete stubs | 45 | Can be cleaned up |
| Anonymous namespace | 3 | Expected |

These are all handled by `/FORCE:MULTIPLE`. The warnings are cosmetic — the linker picks one definition and discards duplicates.

---

## Historical Fixes

### Fixed 2026-02-26 (Data stubs, ALTERNATENAME, /FORCE:UNRESOLVED dropped)
- **Data stub .obj generation**: `create_data_stubs.py` strips code from split .objs, keeps only data sections with `lbl_*` symbols. Auto-linked by `project.py` alongside decomp .objs.
- **ALTERNATENAME stubs for remaining 72 symbols**: Audio SDK (Synth360, XAPO, LEAPFX), UIPanel vtordisp thunks, STL templates, __unwind$ EH records.
- **`/FORCE:UNRESOLVED` dropped**: All symbols now resolve. Only `/FORCE:MULTIPLE` remains.
- **Anon namespace patcher in build pipeline**: Post-compile step patches `?A0x*` hashes.

### Fixed 2026-02-21 (REFHI/REFLO, linker flags, string COMDATs)
- **REFHI/REFLO immediate zeroing**: COFF additive relocations read existing instruction immediates as addends. Baked-in XEX values (e.g., `lis r11, 0x8200`) caused overflow (`0x8200 + 0x823A = 0x043A`). Fix: zero bits [15:0] in REFHI/REFLO relocation sites (`insn & 0xFFFF0000`).
- **Granular linker flags**: `/FORCE` → `/FORCE:MULTIPLE` + `/FORCE:UNRESOLVED`.
- **??_C@_ string COMDATs identified**: Hash=0 under wibo because `RtlComputeCrc32` was unimplemented. Fixed in wibo fork.
- **Configure script**: `scripts/build/configure.sh` wraps `configure.py` with custom `--dtk`, `--objdiff`, `--wibo` paths.

### Fixed 2026-02-20 (REL24, CRT stubs, COMDAT Phase 2)
- **REL24 displacement convention**: MSVC PPC linker uses `disp = (S + A) - section_VA`, not `(S + A) - instruction_VA`. Jeff now sets A = `-(offset_in_section)` for all REL24 relocation sites.
- **CRT save/restore stub exclusion**: `__savegprlr`/`__restgprlr`/`__savefpr`/`__restfpr` excluded from COMDAT extraction to preserve fall-through chains.
- **All global symbols now COMDAT**: `IMAGE_COMDAT_SELECT_ANY` for all globals in split objects.
- **.pdata reconstruction**: jeff generates correct `.pdata` entries with ADDR32 relocs.

### Fixed 2026-02-19 (COMDAT Phase 1, ICF, etc.)
- **ICF symbols resolved**: `link_glue.cpp` provides definitions for `operator delete`, `DataArray::Node`, `MemOrPoolFreeSTL`
- **VA shift fixed**: `/MERGE:.xidata=.text` puts `.text` at correct VA `0x82330000`

---

## Remaining Issues (Potential Jeff Improvements)

### 1. Duplicate Symbols — 756 LNK4006 warnings

Most are inherent to hybrid linking (template instantiations in multiple TUs). Not fixable in jeff. Handled by `/FORCE:MULTIPLE`.

**Reducible subset**: 45 link_glue.cpp stubs that duplicate Matching unit symbols (cleanup task, not jeff).

### 2. EH Metadata Colocation (nice-to-have)

17 `__unwind$` + 2 `__catch$` symbols currently resolved via ALTERNATENAME stubs. Jeff could keep EH metadata colocated with parent functions during splitting to eliminate the need for stubs.

**Priority**: Low — stubs work fine.

### 3. SafeName ICF Deduplication (nice-to-have)

54 LNK4006 from `SafeName(Hmx::Object*)` emitted into many split .objs. Jeff could detect and deduplicate these.

**Priority**: Low — cosmetic warning reduction only.

### 4. Jeff CFA Infrastructure

Recent jeff commits (2026-02-24+) redesigned the Control Flow Analysis system:
- `AnalyzerState` → `CfaConfig` + `CfaResult`
- VM2 architecture with register state tracking and provenance
- Hardened against out-of-bounds symbol addresses

These are infrastructure improvements for future work, not directly related to current link issues.

---

## Workarounds In Place

| Issue | Workaround | Automated? |
|-------|-----------|------------|
| VA shift +0x1600 | `/MERGE:.xidata=.text` in ldflags | Yes (config.json) |
| ICF op delete/Node/MemPool | `link_glue.cpp` provides definitions | Yes (injected in configure.py) |
| Multiply-defined symbols | `/FORCE:MULTIPLE` linker flag | Yes (config.json) |
| lbl_* data symbols | `create_data_stubs.py` (post-compile) | Yes (ninja pipeline) |
| Audio SDK / EH / template symbols | ALTERNATENAME stubs in link_glue.cpp | Yes (compiled) |
| Anon namespace hashes | `obj_anon_ns_patcher.py` (post-compile) | Yes (ninja pipeline) |
| String literal hashes | Wibo CRC fix + WIBO_PATH_MAP | Yes (build env) |

## Build Pipeline

```
dtk xex split → configure.py (injects link_glue unit)
  → ninja compile
  → anon_ns_patcher (post-compile)
  → create_data_stubs (post-compile)
  → ninja link with /FORCE:MULTIPLE
```

## Priority for Remaining Jeff Fixes

1. **EH colocation** — eliminates 17 ALTERNATENAME __unwind stubs (medium effort)
2. **SafeName deduplication** — eliminates 54 LNK4006 warnings (low effort)
3. **CFA improvements** — general infrastructure for future analysis

All critical link errors are resolved. Remaining work is polish.
