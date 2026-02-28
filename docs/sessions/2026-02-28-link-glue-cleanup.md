# Link Glue Cleanup & ALTERNATENAME Analysis (2026-02-28)

## Summary

Cleaned up `src/link_glue.cpp` in two phases:
1. Removed 973 dead ALTERNATENAME stubs (1,387 → 414)
2. Created `obj_dynamic_init_patcher.py` to promote `??__E` STATIC→EXTERNAL, eliminating 164 more (414 → 250)

## Phase 1: Dead Stub Removal

- **Before**: 4,876 lines, 1,387 ALTERNATENAME pragmas
- **After**: 3,903 lines, 414 ALTERNATENAME pragmas
- **Removed**: 973 lines (70% of stubs were dead weight — symbols already resolved by decomp source)
- **Verification**: Link produces identical errors (7 pre-existing LNK2001), XEX boots in Xenia

## Phase 2: Dynamic Init Patcher (`obj_dynamic_init_patcher.py`)

### Root Cause

MSVC emits `??__E` (dynamic initializer) symbols with **STATIC** storage class in `.text$yc`
COMDAT sections. The CRT init table in `auto_08_82F05C00_data.obj` has **EXTERNAL** references
to these symbols. When a unit switches from split→decomp, the split obj's EXTERNAL `??__E` is
no longer linked, and the decomp obj's STATIC `??__E` is invisible to the linker.

### Fix

Post-build patcher (`scripts/obj_dynamic_init_patcher.py`) promotes `??__E` symbols from
STATIC (storage class 3) to EXTERNAL (storage class 2) in decomp `.obj` files. Single byte
change per symbol at `entry_offset + 16` in the COFF symbol table.

- **Patched**: 209 `??__E` symbols across 113 decomp .obj files
- **Stubs eliminated**: 164 ALTERNATENAME pragmas removed from link_glue.cpp
- **Registered in build pipeline**: `configure.py` custom_build_steps, runs as post-compile step
- **Verification**: 7 pre-existing LNK2001 unchanged, XEX boots in Xenia

### Why 72 `??__E` stubs remain

The remaining 72 are for globals that are `extern`-declared in decomp source but only **defined**
in the original source. The link uses `src/<unit>.obj` (decomp) + `data/<unit>.obj` (split data),
but NOT `obj/<unit>.obj` (full split code). So the split's `??__E` initializer for those globals
isn't available.

Examples: `??__ETheLocale` (Locale has `extern Locale TheLocale;` but no definition),
`??__ETheHamUI`, `??__EgCrit`, various audio XAPO registration properties.

These can only be eliminated by adding the actual global definitions to decomp source.

## Remaining 250 Stubs — Breakdown

| Category | Count | Root Cause |
|----------|------:|------------|
| `??__E` dynamic initializers | 72 | Globals `extern`-only in decomp source |
| C++ functions (inlined in original) | 135 | Original compiler inlined; ours doesn't |
| C runtime / Bink / curl / D3D | 31 | Closed-source Xbox SDK |
| `lbl_` data / `merged_` / intrinsics | 12 | Binary addresses, ICF, compiler constants |

## Project Status

- 969 Matching objects, 0 NonMatching, 1,242 MISSING (Xbox SDK/libs)
- All decomp units are Matching — the MISSING objects are out of scope

## Files Modified

- `src/link_glue.cpp` — reduced from 1,387 to 250 ALTERNATENAME pragmas
- `scripts/obj_dynamic_init_patcher.py` — new post-build patcher
- `configure.py` — registered patcher in build pipeline
