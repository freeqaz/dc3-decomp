# Xbox 360 Linking Pipeline

**Date**: 2026-02-11
**Status**: Working (with workarounds)

## Summary

Added the ability to link decomp-compiled .obj files with original split .obj files into a hybrid Xbox 360 PE executable via `ninja link`.

### Why this matters

1. **Runtime testing**: The hybrid PE can be loaded on hardware or in an emulator to verify that decompiled functions work correctly at runtime, not just match at the byte level.
2. **Patch generation**: Producing .xexp patch files requires diffing two linked images. Without a linker, there was no path from source code to distributable patches.

## Current State

### What works

- `ninja link` produces `build/373307D9/default.exe` (hybrid PE: decomp .obj where Matching, original split .obj elsewhere)
- `scripts/link_test.py` can produce a split-only PE for round-trip verification
- `scripts/compare_pe.py` analyzes byte-level differences between linked and original PE
- `scripts/fix_pdata.py` works around dtk .pdata bugs (integrated into ninja build via stamp file)

### Results (hybrid link)

| Metric | Value |
|--------|-------|
| .text match | 8.46% |
| File size | 19.6MB (orig: 17.3MB) |
| Sections | 21 (orig: 15) |
| .text VA shift | +0x1800 |
| 100% match sections | BINKCONS, RADCONST, .XBMOVIE, RADDATA |

The low .text match % is expected — decomp-compiled objects have different sizes than originals, shifting every subsequent function's VA. All differences are from relocation fixups cascading through the VA shift.

### Link errors (all bypassed with `/FORCE`)

After jeff upstream fixes for multi-.pdata, save/restore stubs, and jump tables:

| Issue | Count | Root cause | Status |
|-------|-------|------------|--------|
| Duplicate symbols (LNK4006) | 6,298 | ICF-merged functions in separate .obj | Needs COMDAT marking in dtk |
| Unresolved externals (LNK2001/2019) | 519 (85 unique) | Various — see breakdown below | Mixed |
| .CRT warnings (LNK4210) | 11 | Static initializer sections in decomp .obj | Expected for hybrid |
| REL14 fixup overflow (LNK2013) | 2 | cshaderprogram.obj branch too far | dtk: needs investigation |
| Invalid .pdata fixup (LNK2024) | 1 | aes.obj | dtk: needs investigation |

#### Unresolved symbol breakdown (85 unique)

| Category | Count | Example | Root cause |
|----------|-------|---------|------------|
| Decomp symbols | 39 | `String::String`, `DataArray::Node`, `merged_*` | Decomp .obj referencing symbols in other decomp .obj |
| .CRT dynamic initializers | 24 | `??__EkWaveChunkID@@YAXXZ` | Static init functions from decomp .obj in .CRT section |
| Local labels | 13 | `lbl_82002100` | dtk: local symbols not globalized |
| `__unwind$` symbols | 15 | `__unwind$100939` | dtk: unwind info symbols not exported |
| Vorbis floor0_* | 6 | `floor0_unpack` | dtk: splitting gap |
| C++ catch handlers | 2 | `__catch$100372` | dtk: exception handler symbols not exported |

### Requirements

- **wine** (not wibo) for linking — `link.exe` uses Win32 APIs (`lstrcmpiW`, `NdrClientCall2`) that wibo doesn't implement
- wibo continues to work for compilation (`cl.exe`)

## Remaining dtk Bugs (fix_pdata still needed)

### Bug: `.text$yc` sections not merged (326 objects)

**Symptom**: Split objects have both `.text` and `.text$yc` sections. The `.pdata` section has entries referencing code in both, which the MSVC linker rejects as invalid .pdata contributions (LNK1223).

**What is `.text$yc`?** MSVC uses `section$suffix` naming to control section ordering during linking. `.text$yc` contains C++ dynamic initializers (static/global constructor functions, prefixed `??__E`). The linker merges `.text$yc` into `.text` at link time, sorted by suffix.

**Root cause**: When dtk splits the XEX, code lives at different address ranges. If a unit has functions in both `.text` and `.text$yc` address regions, `split_obj()` creates separate sections for each in the output COFF object. The `.pdata` section then has entries spanning both code sections, which is invalid.

**Fix**: In `split_obj()`, merge sections with the same base name (before `$`) into a single section in the output object. E.g., `.text` and `.text$yc` should become one `.text` section. Same merge strategy as the multi-.pdata fix, but keyed on base name instead of exact name.

**Affected objects**: 326 (e.g., `apofiltermatrixmix.obj`, `Game.obj`, `DataArray.obj`)

### Bug: `__unwind$` symbols in .pdata (488 objects)

**Symptom**: `.pdata` entries reference `__unwind$NNNNN` symbols. The linker rejects these as invalid .pdata content.

**Root cause**: Not yet fully traced. These unwind info symbols may need to be in `.xdata` rather than referenced from `.pdata` in this way, or they may be local symbols that need to be exported.

**Affected objects**: 488 (non-overlapping with the multi-.text issue)

**Total**: 814 objects need fix_pdata workaround (326 + 488).

## Files

| File | Description |
|------|-------------|
| `tools/project.py` | `msvc_link` rule, `X360LinkStep` class, `fix_pdata` build step, `link` phony target |
| `config/373307D9/config.json` | `ldflags` populated with X360 linker flags |
| `scripts/link_test.py` | Standalone PoC link script (useful for testing outside ninja) |
| `scripts/fix_pdata.py` | Renames .pdata sections to bypass dtk bugs (multi-.text, __unwind$) |
| `scripts/compare_pe.py` | PE section comparison tool |
| `docs/sessions/2026-02-11-dtk-pdata-splitting-bug.md` | Detailed bug analysis + fix plan for dtk upstream |

## Road to 100%

### 1. Fix `.text$yc` merging in dtk (eliminates 326 fix_pdata objects)

Merge `section$suffix` sections in `split_obj()`. When an output object already has a `.text` section and encounters `.text$yc` data for the same unit, append the `.text$yc` data to the existing `.text` section (adjusting relocation offsets and symbol addresses).

### 2. Fix `__unwind$` handling in dtk (eliminates 488 fix_pdata objects)

Investigate how the original MSVC linker handles `__unwind$` symbols in .pdata. These may need to be emitted differently during COFF generation.

### 3. Mark ICF functions as COMDAT (eliminates 6,298 LNK4006 warnings)

Functions that were ICF-merged in the original should be emitted as COMDAT sections so the linker can properly fold duplicates instead of warning.

### 4. Eliminate VA shift

The +0x1800 VA shift comes from extra sections (.pdat0, .xidata, .xedata, .CRT, .bss, .edata) that don't exist in the original. Fixing dtk bugs should reduce section count from 21 to closer to 15, eliminating the shift.

### 5. Relocation-aware comparison

Extend `scripts/compare_pe.py` to parse the MAP file and compare function-by-function, subtracting relocation fixups. This would show the true code match % independent of VA shift.

## Architecture

```
ninja link
  |-- compile (decomp .obj for Matching functions)
  |-- split (original .obj from dtk xex split)
  |-- fix_pdata (rename .pdata in 814 objects, stamp file)
  +-- msvc_link (wine + X360 link.exe -> default.exe)
         |-- inputs: hybrid .obj list via response file
         +-- flags: /MACHINE:PPCBE /SUBSYSTEM:XBOX /BASE:0x82000000
                    /NODEFAULTLIB /XEX:NO /FORCE /ENTRY:mainCRTStartup
```
