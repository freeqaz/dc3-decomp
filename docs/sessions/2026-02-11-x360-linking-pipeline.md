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
- `scripts/fix_pdata.py` works around dtk .pdata bug (integrated into ninja build via stamp file)

### Results

| Metric | Split-only link | Hybrid link |
|--------|----------------|-------------|
| .text match | 89.33% | 9.29% |
| File size | 19.5MB (orig: 17.3MB) | 19.6MB |
| Link errors (bypassed) | ~230 | ~230 |

- **Split-only 89%**: The ~11% difference is from a +0x1600 VA shift in .text, which cascades through all absolute relocations. The actual code bytes are correct; the addresses pointing to them differ.
- **Hybrid 9%**: Expected — decomp-compiled objects have different sizes than originals, shifting every subsequent function's VA. This will improve as more functions match.
- **4 sections match 100%**: BINKCONS, RADCONST, .XBMOVIE, RADDATA

### Known link errors (all bypassed with `/FORCE`)

| Issue | Count | Root cause | Upstream fix |
|-------|-------|------------|-------------|
| Duplicate symbols (LNK4006) | ~15,000 | ICF-merged functions split into separate .obj | dtk: deduplicate or mark COMDAT |
| Invalid .pdata (LNK1223) | 127 objects | dtk creates multiple .pdata sections per object | dtk: merge same-named sections in `split_obj()` |
| Unresolved save/restore stubs | ~80 | dtk emits as local Label instead of external Function | dtk: set `kind: Function` in `FindSaveRestSledsXbox` |
| Unresolved jump tables | ~66 | dtk emits as local scope | dtk: set `scope: Global` on jump table symbols |
| REL14 fixup overflow | 2 | Branch target too far in cshaderprogram.obj | dtk: needs investigation |
| Unresolved .CRT initializers | ~16 | Static initializer symbols from decomp .obj | Expected for hybrid; benign |
| Unresolved vorbis floor0_* | 6 | Missing from split objects | dtk: splitting gap |

### Requirements

- **wine** (not wibo) for linking — `link.exe` uses Win32 APIs (`lstrcmpiW`, `NdrClientCall2`) that wibo doesn't implement
- wibo continues to work for compilation (`cl.exe`)

## Files

| File | Description |
|------|-------------|
| `tools/project.py` | `msvc_link` rule, `X360LinkStep` class, `fix_pdata` build step, `link` phony target |
| `config/373307D9/config.json` | `ldflags` populated with X360 linker flags |
| `scripts/link_test.py` | Standalone PoC link script (useful for testing outside ninja) |
| `scripts/fix_pdata.py` | Renames .pdata sections to bypass dtk bug |
| `scripts/compare_pe.py` | PE section comparison tool |
| `docs/sessions/2026-02-11-dtk-pdata-splitting-bug.md` | Detailed bug analysis + fix plan for dtk upstream |

## Road to 100% Split-Only Round-Trip

The split-only link (original objects only, no decomp) should theoretically produce a byte-identical .text section. Currently at 89%. To reach 100%:

### 1. Fix dtk upstream bugs (eliminates /FORCE, fixes ~230 link errors)

Three bugs in dtk's XEX splitter produce invalid COFF objects. Fix details in [dtk-pdata-splitting-bug.md](2026-02-11-dtk-pdata-splitting-bug.md):

- **Multi-.pdata sections**: merge same-named sections in `split_obj()` (medium effort)
- **Local save/restore stubs**: set `kind: Function` on sled entry symbols (one-line fix)
- **Local jump table symbols**: set `scope: Global` on jump table symbols (one-line fix)

Fixing these eliminates the need for `/FORCE` and `fix_pdata.py`.

### 2. Eliminate VA shift (the remaining ~11%)

The +0x1600 VA shift means .text starts at a different virtual address than the original. This cascades through every absolute relocation (function pointers, vtables, string references). Possible fixes:

- **Section ordering**: ensure the linker places sections in the same order as the original. May need `/MERGE` or `/ORDER` flags, or adjusting the .obj input order.
- **Padding/alignment**: the extra .pdat1/.pdat2 renamed sections and duplicate symbols add size before .text. Fixing dtk bugs (step 1) should reduce this.
- **Relocation-aware comparison**: instead of raw byte comparison, compare with relocations resolved. `scripts/compare_pe.py` could be extended to parse the MAP file and compare function-by-function, ignoring relocation fixups.

### 3. Eliminate duplicate symbol bloat

~15,000 LNK4006 warnings from ICF-merged functions. The MSVC linker keeps the first definition and discards duplicates, but this may affect section sizes and ordering. dtk could mark these as COMDAT sections so the linker properly folds them.

### 4. Hybrid link verification

Once the split-only round-trip is clean, verify that swapping in decomp .obj for Matching functions preserves byte-identical output at those function addresses. The `add_unit()` hybrid selection logic in `project.py` already handles this — just needs validation.

## Architecture

```
ninja link
  ├── compile (decomp .obj for Matching functions)
  ├── split (original .obj from dtk xex split)
  ├── fix_pdata (rename .pdata sections, stamp file)
  └── msvc_link (wine + X360 link.exe → default.exe)
         ├── inputs: hybrid .obj list via response file
         └── flags: /MACHINE:PPCBE /SUBSYSTEM:XBOX /BASE:0x82000000
                    /NODEFAULTLIB /XEX:NO /FORCE /ENTRY:mainCRTStartup
```
