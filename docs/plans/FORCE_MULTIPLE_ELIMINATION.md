# Eliminating `/FORCE:MULTIPLE` — Clean Link Plan

**Date:** 2026-02-28
**Status:** Planning — strategies identified, not yet implemented
**Depends on:** [CLEAN_LINK_PROJECT.md](CLEAN_LINK_PROJECT.md) (M4 complete)

## Problem Statement

The hybrid link produces a working executable with **0 errors**, but requires `/FORCE:MULTIPLE` to suppress **13,400 LNK4006** duplicate symbol warnings. The MSVC PPC linker (link.exe 10.0.11886.0) does not silently handle `IMAGE_COMDAT_SELECT_ANY` deduplication — it reports every duplicate as a warning.

These warnings are cosmetic (the linker picks one definition and discards the rest, which is correct behavior), but `/FORCE:MULTIPLE` also suppresses real `LNK2005` hard-duplicate errors that would indicate actual linking bugs. Removing it gives us a stronger correctness guarantee.

**Goal:** Eliminate `/FORCE:MULTIPLE` from the link flags, achieving a truly clean link with 0 warnings.

---

## Link Architecture

Understanding why duplicates occur requires knowing how three object types interact per translation unit.

### Three Object Types Per Unit

For each of the ~2,045 units in the project:

| Type | Source | Path Pattern | Contents |
|------|--------|-------------|----------|
| **Decomp** (`src/`) | Compiled from C++ source | `build/.../src/system/foo/Bar.obj` | Decomp functions + compiler-emitted templates, RTTI, vtables |
| **Split** (`obj/`) | Jeff-split from original XEX | `build/.../obj/system/foo/Bar.obj` | Original code and data, all symbols as SELECT_ANY COMDATs |
| **Data stub** (`data/`) | Generated from split obj | `build/.../data/system/foo/Bar.obj` | Data sections + COMDAT code sections (no main .text) |

### Which Objects Get Linked

The selection logic lives in `tools/project.py` `add_unit()` (lines 1168-1184):

```
For each unit:
  if unit is Matching (has decomp source):
    link src/ object           ← decomp-compiled code
    link data/ object          ← supplement: data sections + COMDAT code from original
    skip obj/ object           ← original code excluded to avoid address overlap
  else (NonMatching):
    link obj/ object           ← original split object (all code + data)
```

### Why Duplicates Occur

When `src/system/foo/Bar.obj` is linked alongside `data/system/foo/Bar.obj`:

1. **The decomp object** emits templates, RTTI, vtables, and inline functions as COMDATs (MSVC default behavior — `SELECT_NODUPLICATES` for the primary definition, `SELECT_ANY` for secondary emissions in other TUs)

2. **The data stub** contains the same COMDAT code sections extracted from the original binary by `create_data_stubs.py` (all marked `SELECT_ANY` by jeff)

3. **The linker sees two definitions** of every shared COMDAT symbol and reports LNK4006 for each one

4. **Cross-unit COMDATs** also duplicate: if `src/system/foo/Bar.obj` and `data/system/baz/Qux.obj` both contain `std::vector<int>::push_back`, the linker reports that too

### Warning Breakdown (13,400 LNK4006)

| Source | Est. Count | Description |
|--------|-----------|-------------|
| Same-unit decomp vs data stub | ~4,000 | Templates/RTTI/vtables present in both `src/` and `data/` for same TU |
| Cross-unit decomp vs data stub | ~6,000 | Templates emitted into multiple TUs (STL, ObjPtr, etc.) |
| Cross-unit data stub vs data stub | ~2,500 | Same templates in multiple original split objects |
| Self-duplicates (within data stubs) | ~400 | Rare: same symbol in two COMDAT sections of one object |
| Decomp vs SDK/library | ~500 | Decomp templates matching SDK library definitions |

---

## Strategies

### Strategy 1: Smart Data Stubs (Recommended First Step)

**Idea:** Modify `create_data_stubs.py` to exclude COMDAT code sections when the symbol is already defined in the corresponding decomp `src/` object.

**How it works:**
1. For each Matching unit, parse the decomp `src/` object's symbol table
2. Collect all COMDAT symbols exported by the decomp object
3. When creating the data stub from the split `obj/`, skip any COMDAT section whose symbol is already in the decomp object

**Impact:** Eliminates same-unit duplicates (~4,000 warnings). The data stub would only provide symbols the decomp object is *missing*.

**Complexity:** Medium. Requires COFF symbol table parsing in `create_data_stubs.py` (already does COFF section parsing). The decomp object must be built before data stubs are generated, creating a build ordering dependency.

**Risks:**
- Build ordering: data stubs currently run as a post-compile step. This would need the decomp object to exist first (which it does — data stubs already run after `ninja compile`).
- If the decomp object changes (function added/removed), the data stub must be regenerated. This is already the case with the current system.

```python
# Pseudocode for smart data stub creation
def create_smart_data_stub(split_obj, decomp_obj, output_path):
    decomp_symbols = parse_coff_symbols(decomp_obj)

    for section in split_obj.sections:
        if section.is_code and section.is_comdat:
            comdat_symbol = section.get_comdat_symbol()
            if comdat_symbol in decomp_symbols:
                section.keep = False  # Skip — decomp already provides this
            else:
                section.keep = True   # Keep — decomp doesn't have this
        elif section.is_data or section.is_bss:
            section.keep = True       # Always keep data
```

### Strategy 2: Fix COMDAT Auxiliary Records

**Idea:** Ensure both decomp objects and jeff-split objects use matching COMDAT selection types, so the linker silently deduplicates instead of warning.

**Background:** MSVC uses `IMAGE_COMDAT_SELECT_NODUPLICATES` (selection=1) for primary definitions and `IMAGE_COMDAT_SELECT_ANY` (selection=2) for secondary. Jeff marks everything as `SELECT_ANY`. The linker warns when selection types disagree across objects.

**How it works:**
1. In jeff's `xex.rs`, detect whether each COMDAT symbol is a "primary" definition (the unit that originally defined it) and mark it `SELECT_NODUPLICATES`
2. Or: in decomp objects, change all COMDATs to `SELECT_ANY` to match jeff

**Impact:** May eliminate all NODUPLICATES-vs-ANY warnings. But the linker may still warn about ANY-vs-ANY duplicates (untested on MSVC PPC linker).

**Complexity:** Medium-High for jeff changes (need to determine "primary" TU per symbol). Low for decomp-side changes (compiler flag or post-processing).

**Risks:**
- Untested: we don't know how the MSVC PPC linker handles `ANY`-vs-`ANY` for duplicate symbols. It might still warn.
- NODUPLICATES-vs-NODUPLICATES for the same symbol would produce LNK2005 (error, not warning).

### Strategy 3: Eliminate Data Stubs Entirely

**Idea:** Make decomp objects export ALL symbols that the original object exported, making data stubs unnecessary.

**How it works:**
1. For each Matching unit, identify all symbols in the split `obj/` that are not in the decomp `src/`
2. Add missing implementations to the decomp source (for functions) or add explicit template instantiations (for templates)
3. Remove data stubs from the link

**Impact:** Eliminates ALL data-stub-related duplicates. Also removes the data stub build step entirely.

**Impact on stubs:** The current 397 ALTERNATENAME stubs would also need to be resolved (most are `??__E` dynamic initializers that come from data stub COMDATs).

**Complexity:** Very High. Requires implementing ~500+ functions and adding ~1,000+ template instantiations. This is essentially "finish the decomp" for every Matching unit.

**Risks:**
- Massive scope — months of work
- Some symbols may be impossible to reproduce from source (ICF-merged functions, compiler-generated RTTI)

### Strategy 4: Suppress Specific Warnings (Workaround)

**Idea:** Use linker options to suppress LNK4006 while keeping error detection for LNK2005.

**How it works:**
- The MSVC PPC linker supports `/IGNORE:4006` to suppress specific warning numbers
- Replace `/FORCE:MULTIPLE` with `/IGNORE:4006`

**Impact:** Achieves the goal of removing `/FORCE:MULTIPLE`. LNK2005 errors would still fire if genuine hard duplicates exist.

**Complexity:** Trivial — one flag change.

**Risks:**
- Need to verify the MSVC PPC linker supports `/IGNORE:NNNN` syntax (standard MSVC does, but the Xbox 360 version may not)
- Suppresses ALL LNK4006, including any that might indicate real issues (though we've audited and confirmed these are all cosmetic)
- This is a suppression, not a fix — the underlying duplicates still exist

### Strategy 5: Hybrid Approach (Recommended)

**Phase A — Smart data stubs (Strategy 1):** Cut ~4,000 same-unit duplicates with minimal effort.

**Phase B — `/IGNORE:4006` (Strategy 4):** If the MSVC PPC linker supports it, suppress remaining cross-unit duplicates. Test by removing `/FORCE:MULTIPLE` and adding `/IGNORE:4006`.

**Phase C — Incremental source completion (Strategy 3):** As decomp progresses, data stubs become smaller and eventually unnecessary per-unit. Each completed unit naturally reduces the duplicate count.

---

## Current State Summary

| Metric | Value |
|--------|-------|
| **LNK4006 warnings** | 13,400 |
| **LNK4210 warnings** | 113 (.CRT section warnings) |
| **LNK2001/LNK2005 errors** | 0 |
| **ALTERNATENAME stubs** | 397 |
| **Data stubs** | 968 |
| **Matching units** | 368+ |
| **Link flags** | `/FORCE:MULTIPLE` |
| **Link produces** | Working 19.6MB PE → XEX that boots |

### ALTERNATENAME Stub Composition (397)

| Category | Count | Notes |
|----------|-------|-------|
| `??__E` dynamic initializers | ~236 | CRT static init — auto-resolve when parent TU compiles |
| Game/engine functions | ~140 | Functions in data stubs that reference transitive dependencies |
| SDK transitive deps | ~21 | Bink, D3D, FFT, wmemcpy, etc. — needed by data stub COMDATs |

---

## Recommended Execution Path

### Step 1: Test `/IGNORE:4006` (1 hour)

Before any code changes, test whether the Xbox 360 MSVC linker supports `/IGNORE:4006`:

```bash
# In configure.py or config.json, replace:
#   /FORCE:MULTIPLE
# with:
#   /IGNORE:4006
# Then rebuild and check for LNK2005 errors
```

If this works, we immediately get a cleaner link that still catches real duplicates. If not, proceed to Step 2.

### Step 2: Smart Data Stubs (1-2 days)

Modify `create_data_stubs.py` to skip COMDAT sections already in the decomp object:

1. Add COFF symbol table parsing for decomp objects
2. Cross-reference against split object COMDATs
3. Only include COMDATs that the decomp object doesn't define
4. Regenerate all data stubs, reconfigure, relink

Expected result: ~4,000 fewer LNK4006 warnings.

### Step 3: Cross-Unit COMDAT Deduplication (medium-term)

For cross-unit duplicates, the decomp-side solution is adding `#pragma comment(linker, "/ALTERNATENAME:...")` or explicit template instantiation directives in a central TU so each symbol is defined exactly once. The data-stub-side solution is more nuanced: cross-unit COMDATs are intentional (the linker is supposed to pick one and discard the rest).

The real fix is decomp progress — as more units are fully decomped, their data stubs shrink and eventually become empty.

### Step 4: Ongoing Stub Burndown

As the decomp progresses:
- Implementing functions removes ALTERNATENAME stubs
- Completing units removes data stubs entirely
- Each removed data stub eliminates all its cross-unit COMDAT duplicates

---

## Files Involved

| File | Role |
|------|------|
| `src/link_glue.cpp` | ALTERNATENAME stubs and template instantiations |
| `scripts/create_data_stubs.py` | Data stub generation from split objects |
| `tools/project.py` | Unit linking logic (add_unit, link_step) |
| `configure.py` | Build configuration, linker flags |
| `../jeff/src/util/xex.rs` | COFF generation, COMDAT marking |

## Related Documentation

| Doc | What it covers |
|-----|---------------|
| [CLEAN_LINK_PROJECT.md](CLEAN_LINK_PROJECT.md) | Full link project history (M1-M4 milestones, all resolved issues) |
| [BUILD_ROADMAP.md](BUILD_ROADMAP.md) | Path to bootable build, phase status |
| [NEXT_STEPS.md](NEXT_STEPS.md) | Active phased plan (decomp + runtime) |
| [../sessions/JEFF_LINK_LIMITATIONS.md](../sessions/JEFF_LINK_LIMITATIONS.md) | Jeff-side limitations |
