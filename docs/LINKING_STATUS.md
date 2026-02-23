# DC3 Decomp Linking Status

**Date**: 2026-02-23

Summary of where we're at with object-level linking — what works, what's left, and what it means for marking units as `"Matching"` in `objects.json`.

## Linking Infrastructure: Current State

The hybrid linking pipeline is working. The decomp XEX boots in Xenia and reaches the game's render loop.

| Component | Status |
|-----------|--------|
| **Link order** | Correct — derived from original `ham_xbox_r.map` |
| **`.text` base VA** | Correct at `0x82330000` (via `/MERGE:.xidata=.text`) |
| **COMDAT dedup (jeff)** | Phase 2 complete. LNK4006 warnings down from 5,545 to 19 |
| **`.pdata` generation (jeff)** | Fixed — `fix_pdata.py` workaround removed from pipeline. LNK1223 = 0 |
| **REL14 fixup overflow** | Fixed — LNK2013 = 0 (was 168) |
| **Linker flags** | `/FORCE:MULTIPLE` + `/FORCE:UNRESOLVED` (targeted, not blanket `/FORCE`) |
| **ICF aliases** | `link_glue.cpp` provides definitions for merged symbols |
| **XEX packaging** | `build_xex.py` produces bootable XEX |
| **PE `.text` delta** | +18.8 KB (0.15%) — compiler subsection size differences between VS2010 and our toolchain |

Without `/FORCE` at all, the link produces exactly **19 LNK2005 errors** — all `??__E` static initializer COMDAT duplicates from decomp objects that also exist in split objects. MSVC X360 emits these with `IMAGE_COMDAT_SELECT_NODUPLICATES`, which can't be overridden at the source level. `/FORCE:MULTIPLE` handles them cleanly.

## Progress Numbers

| Metric | Value |
|--------|-------|
| Already `"Matching"` in objects.json | 252 units |
| `"NonMatching"` (source exists, not yet linked) | 686 units |
| `"MISSING"` (no source at all) | 1,273 units |
| Units at >=99.5% `.text` match, not yet Matching | **47** |
| Units at 95–99.5% `.text` match, not yet Matching | **197** |
| Total near-linkable (Matching + >=99.5%) | ~299 units |

## What Each COFF Section Needs

When a unit is marked `"Matching"`, configure.py links **both** the decomp `.obj` and the split `.obj`. The linker picks the decomp definition first (`/FORCE:MULTIPLE`) and discards the duplicate from the split. Every section in the decomp `.obj` is consumed:

### `.text` — Function Code

This is the main decomp work. objdiff tracks per-function `fuzzy_match_percent`. For linking purposes, `.text` mismatches don't cause link errors — they cause runtime behavioral differences. A function at 95% will link fine but may behave wrong.

Acceptable `.text` gaps:
- **Register allocation differences** — fixable post-build with `obj_regswap_patcher.py` (patches compiled `.obj` files to swap registers)
- **ICF-merged functions** — the `.obj` code is correct; the `merged_<addr>` comparison target is a linker artifact from Identical COMDAT Folding

### `.rdata` — Read-Only Data (vtables, RTTI, strings, floats)

This section contains vtables, RTTI type descriptors (`??_R0`..`??_R4`), string literals, float/double constants, and `const` globals.

**Why `.rdata` match% is always low**: objdiff reports `.rdata` byte-level match, and it's typically 5–40% even for perfectly decomped units. This is because vtable entries contain **absolute virtual addresses** baked in by the linker. Our build has different addresses than the original (due to the +18.8 KB `.text` delta and different section layout), so every vtable pointer and RTTI cross-reference differs at the byte level. The *structure* is correct — right number of vtable slots, right function order, right class hierarchy — but the baked-in pointers are numerically different.

This is inherent to the hybrid linking approach and not fixable without a byte-identical link. It's not a problem: the linker resolves these relocations at link time, so the final binary gets correct addresses regardless.

Float constants (`__real@3f800000`, etc.) are COMDAT symbols in `.rdata`. Jeff's COMDAT Phase 2 marks these correctly, so they deduplicate across units.

### `.data` — Initialized Globals

All initialized static and global variables must exist with correct sizes and initial values. objdiff tracks `.data` match% per unit — most candidate units are at 95–100%.

Missing or wrong-sized globals cause unresolved externals or data corruption (adjacent globals overlap at runtime).

### `.bss` — Uninitialized Globals

Uninitialized statics/globals. objdiff can only compare sizes, not content (it's all zeros at load time). All globals must be declared with the right types so sizes match.

### `.pdata` — Exception Tables (Xbox 360 / Windows CE format)

Xbox 360 uses `IMAGE_CE_RUNTIME_FUNCTION_ENTRY` — 8-byte records, not the 12-byte x64 format:

```
BeginAddress   [4 bytes]  — RVA of function start
PrologLen      [8 bits]   — prolog length in bytes
FuncLen        [22 bits]  — function length in 4-byte units
ThirtyTwoBit   [1 bit]    — always 1 for PPC
ExceptionFlag  [1 bit]    — 1 if the function has try/catch
```

The compiler generates `.pdata` deterministically from `.text`. If the function code matches, `.pdata` matches. Entries must be sorted ascending by `BeginAddress` — the Xbox 360 linker rejects unsorted `.pdata` with `LNK1223`.

For functions with `ExceptionFlag=1`, an 8-byte `PDATA_EH` blob sits in `.text` immediately before the function, containing pointers to `__CxxFrameHandler` and the function's `FuncInfo` record in `.rdata`. Jeff now generates these correctly with proper ADDR32 relocations (previously they had baked-in absolute VAs with no relocs).

**Current state**: `.pdata` generation is fixed in jeff. All split objects now have well-formed, sorted `.pdata` with correct relocs. LNK1223 = 0. The `fix_pdata.py` rename workaround has been removed from the build pipeline.

### COMDAT Sections — Templates, Inlines, RTTI

Symbols that appear in multiple translation units (template instantiations, inline functions, RTTI metadata, float constants, string literals) are emitted as COMDAT sections. The linker deduplicates them based on a selection type:

- `SELECT_ANY` — linker picks one copy silently (most COMDATs)
- `SELECT_NODUPLICATES` — linker warns/errors on duplicates (MSVC X360 uses this for some explicit template specializations and `??__E` static initializers)

Jeff's COMDAT Phase 2 marks split-object duplicates with `SELECT_ANY` correctly. LNK4006 dropped from 5,545 to 19. The remaining 19 are all `??__E` static initializer symbols from decomp objects — an MSVC compiler behavior where the decomp compiler emits `SELECT_NODUPLICATES` for these. This is harmless with `/FORCE:MULTIPLE`.

### `.text$x` / `.text$yc` / `.text$yd` — Compiler Subsections

These are COMDAT subsections generated by the compiler:
- `.text$x` — `__unwind$` exception cleanup thunks
- `.text$yc` — RTTI `complete_object_locator` data
- `.text$yd` — `??__E`/`??__F` dynamic initializer/destructor thunks

Our toolchain (VS2013-era cross-compiler) generates these differently from the original (~2010-era) compiler. This is the source of the +18.8 KB `.text` delta:

```
                 Original    Decomp      Delta
Main .text:      0xBAAB90    0xBB7160    +50,640
.text$x:         (none)      0x2898      +10,392
.text$yc:        0x5F88      0x34C       -23,612
.text$yd:        0x4A2C      0x1BC       -18,544
Total:           0xBB6B14    0xBBB4D4    +18,876
```

This is a systemic compiler artifact, not fixable in source. It causes the address drift that makes `.rdata` byte-percentages low. It doesn't affect correctness — the linker handles the different subsection sizes and resolves all relocations.

## What This Means for Marking Units

A unit is safe to mark `"Matching"` when:

1. **`.text` is >=99.5%** — all functions matched, with only register allocation or ICF-merged gaps remaining
2. **`.data` is >=95%** — globals are present with correct sizes (the gap is usually address-dependent content, not missing data)
3. **`.pdata` is ~100%** — auto-generated from `.text`, almost always matches
4. **`.rdata` can be ignored for byte-level matching** — it will always be low (5–40%) due to absolute VA differences. What matters is structural correctness: right vtable entry count, right class hierarchy, right RTTI. This is implicitly verified by the fact that the `.text` functions that call through those vtables are matching.

The 47 units currently at >=99.5% `.text` that aren't yet marked `"Matching"` are candidates for immediate promotion. The remaining gaps in those units are register allocation differences (patchable with `obj_regswap_patcher.py`) and ICF-merged functions (linker artifacts, not code issues).

## Remaining Linking Issues

| Issue | Count | Impact | Fix Path |
|-------|-------|--------|----------|
| `??__E` COMDAT duplicates (LNK2005) | 19 | Handled by `/FORCE:MULTIPLE` | MSVC compiler behavior, not fixable in source |
| `??__E` CRT initializers missing from decomp | 26 | Globals left uninitialized at runtime | Export from split `.obj` or implement in decomp source |
| `??_C@` string literal COMDAT hash mismatch | ~533 | Unresolved externals for string symbols | Hash normalization in jeff (identified, not yet implemented) |
| `lbl_*` cross-unit labels | ~195 | Cross-unit branches can't resolve | Split boundary fixes in `splits.txt` |
| `.text` delta (+18.8 KB) | 1 | Address drift → `.rdata` byte mismatch | Systemic compiler artifact, not fixable |

## Build Commands

```bash
ninja                              # Build all objects
ninja link                         # Link the hybrid PE
ninja build/373307D9/report.json   # Regenerate objdiff progress report
python3 scripts/build/build_xex.py # Package PE into bootable XEX

# Post-build register patching
ninja && python3 scripts/obj_regswap_patcher.py --batch --apply

# Standalone link test (no /FORCE, surfaces all duplicate/unresolved errors)
python3 scripts/build/link_test.py
```
