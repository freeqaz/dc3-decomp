# Xbox 360 Linking Pipeline

**Date**: 2026-02-11 (updated 2026-02-12)
**Status**: Working (dtk bugs fixed upstream, fix_pdata removed)

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
- `scripts/fix_pdata.py` available as diagnostic tool (no longer in build pipeline)

### Results (hybrid link, 2026-02-12)

| Metric | Value |
|--------|-------|
| .text match | 7.98% (direct byte comparison) |
| File size | 19.6MB (orig: 17.3MB) |
| Sections | 21 (orig: 15) |
| .text VA shift | +0x1800 |
| 100% match sections | BINKCONS, RADCONST, .XBMOVIE, RADDATA |
| Objects linked | 5,417 (971 decomp + 4,446 SDK/lib) |

The low .text match % is expected — decomp-compiled objects have different sizes than originals, shifting every subsequent function's VA. All differences are from relocation fixups cascading through the VA shift. A relocation-aware comparison would show significantly higher match %.

### Link errors (all bypassed with `/FORCE`)

After jeff upstream fixes for multi-.pdata, save/restore stubs, and jump tables:

| Issue | Count | Root cause | Status |
|-------|-------|------------|--------|
| LNK1223 (multi-.pdata) | 0 | Fixed in dtk | **Resolved** |
| Duplicate symbols (LNK4006) | 5,545 | ICF-merged functions in separate .obj | Needs COMDAT marking in dtk |
| Unresolved externals (LNK2001/2019) | 930 (223 unique) | Various — see breakdown below | Mixed |
| .CRT warnings (LNK4210) | 11 | Static initializer sections in decomp .obj | Expected for hybrid |
| REL14 fixup overflow (LNK2013) | 2 | cshaderprogram.obj branch too far | dtk: needs investigation |

#### Unresolved symbol breakdown (223 unique)

| Category | Count | Example | Root cause |
|----------|-------|---------|------------|
| Local labels (lbl_*) | 96 | `lbl_82002100` | dtk: local data symbols not globalized |
| Decomp symbols | 84 | `String::String`, `DataArray::Node`, `merged_*` | Decomp .obj referencing symbols in other decomp .obj |
| .CRT dynamic initializers | 24 | `??__EkWaveChunkID@@YAXXZ` | Static init functions from decomp .obj in .CRT section |
| JPEG memory | 7 | `jpeg_get_small` | Missing jmem*.obj from split |
| Ogg/Vorbis (floor0) | 9 | `floor0_unpack`, `OggFree` | dtk: splitting gap |
| Jump tables | 3 | `jumptable_820050E8` | dtk: 3 remaining local jump table symbols |
| `__savegprlr_*` | 0 | — | **Resolved** in dtk |

### Requirements

- **wine** (not wibo) for linking — `link.exe` uses Win32 APIs (`lstrcmpiW`, `NdrClientCall2`) that wibo doesn't implement
- wibo continues to work for compilation (`cl.exe`)

## dtk Bug Status

All three critical dtk bugs that originally required `fix_pdata.py` have been fixed upstream by jeff:

| Bug | Objects affected | Status |
|-----|-----------------|--------|
| Multi-.pdata sections | 127 | **Fixed** — dtk renames to .pdat0 internally |
| Local `__savegprlr_*` stubs | 80 unresolved | **Fixed** — symbols now global |
| Local `jumptable_*` symbols | 66 unresolved → 3 | **Mostly fixed** — 3 remain |

The `fix_pdata.py` workaround has been removed from the build pipeline. The script is kept in `scripts/` as a diagnostic tool.

## Files

| File | Description |
|------|-------------|
| `tools/project.py` | `msvc_link` rule, `X360LinkStep` class, `link` phony target |
| `config/373307D9/config.json` | `ldflags` populated with X360 linker flags |
| `scripts/link_test.py` | Standalone PoC link script (useful for testing outside ninja) |
| `scripts/fix_pdata.py` | Diagnostic tool (no longer in build pipeline) |
| `scripts/compare_pe.py` | PE section comparison tool |
| `docs/sessions/2026-02-11-dtk-pdata-splitting-bug.md` | Detailed bug analysis + fix plan for dtk upstream |

## Road to 100%

### 1. Mark ICF functions as COMDAT (eliminates 5,545 LNK4006 warnings)

Functions that were ICF-merged in the original should be emitted as COMDAT sections so the linker can properly fold duplicates instead of warning.

### 2. Resolve remaining unresolved symbols (223 unique)

- **lbl_* data labels (96)**: dtk needs to globalize local data symbols referenced across objects
- **Decomp cross-refs (84)**: Expected — these resolve as more functions are decompiled
- **Static initializers (24)**: .CRT section handling
- **JPEG/Vorbis (16)**: Missing split objects or splitting gaps
- **Jump tables (3)**: 3 remaining local jump table symbols in dtk

### 3. Eliminate VA shift

The +0x1800 VA shift comes from extra sections (.pdat0, .xidata, .xedata, .CRT, .bss, .edata) that don't exist in the original. Reducing section count from 21 closer to 15 would eliminate the shift.

### 4. Relocation-aware comparison

Extend `scripts/compare_pe.py` to parse the MAP file and compare function-by-function, subtracting relocation fixups. This would show the true code match % independent of VA shift.

## Architecture

```
ninja link
  |-- compile (decomp .obj for Matching functions)
  |-- split (original .obj from dtk xex split)
  +-- msvc_link (wine + X360 link.exe -> default.exe)
         |-- inputs: hybrid .obj list via response file
         +-- flags: /MACHINE:PPCBE /SUBSYSTEM:XBOX /BASE:0x82000000
                    /NODEFAULTLIB /XEX:NO /FORCE /ENTRY:mainCRTStartup
```
