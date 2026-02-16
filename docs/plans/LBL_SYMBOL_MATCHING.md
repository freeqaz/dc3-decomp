# Plan: `lbl_` Symbol Matching for Function-Local Statics

**Status:** Planning
**Priority:** Medium — impacts match% reporting accuracy for ~258 source files
**Complexity:** Medium (Phase 1), High (Phase 2)
**Related Docs:**
- [tools/OBJDIFF_LOCAL_STATIC_MATCHING.md](../tools/OBJDIFF_LOCAL_STATIC_MATCHING.md) — deep technical analysis
- [BUILD_ROADMAP.md](BUILD_ROADMAP.md) — linking and symbol integration

---

## Problem Statement

### The Issue

When functions use `static Symbol("Foo")`, MSVC generates mangled names like:
```cpp
?Foo@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z@4V2@A
```

On the target side (disassembled Xbox 360 binary), dtk cannot recover these names, so they become:
```
lbl_82F64998
```

In objdiff's `reloc_eq()` function, three matching attempts occur:

1. **Name match**: `lbl_82F64998` ≠ `?Foo@?3??...` → ❌ fails
2. **Symbol equivalences**: not populated for these → ❌ fails
3. **`address_eq()`**: compares section-relative offsets in .obj files, which differ between split-from-XEX and compiled-from-source objects → ❌ fails

Result: Every instruction referencing a function-local static is marked as a mismatch, even when the machine code is identical.

### Scale

- **258 source files** contain `static Symbol` patterns
- **Top offenders**: RhythmBattle.cpp (91), MetagameRank.cpp (73), SaveLoadManager.cpp (56), DirLoader.cpp (37)
- **Example**: `DirLoader::FixClassName` has 34 local statics, reports 90.4% match despite ~100% actual structural match — all 290 "mismatches" are just symbol relocation noise

### Impact on Decomp

- **Inflated mismatch counts** make it hard to identify real codegen differences
- **Demotivating false negatives** — functions that ARE structurally correct appear broken
- **Wasted investigation time** — agents/humans debug phantom mismatches
- **Progress reporting skew** — unit-level match% underreported

---

## What Data We Have

| Source | Entries | Coverage | Helps? |
|--------|---------|----------|--------|
| **Map file** (119K lines) | 5,655 named .data/.bss symbols with absolute VAs | Public/global statics | Partially — covers public statics, but NOT function-local statics |
| **symbols.txt** (212K lines) | 27,536 `lbl_` entries at absolute VAs | All unnamed data | These ARE the problem — unnamed placeholders |
| **objdiff `symbol_equivalences`** | Bidirectional name→name map used in `reloc_eq()` | N/A | The mechanism exists, just needs the right data |
| **objdiff `map_file` config** | Already parses MSVC map → ICF equivalences | ICF-merged symbols only | Currently only handles same-address merges, not lbl_→name mapping |

### Key Files

**DC3 Decomp:**
- `orig/373307D9/ham_xbox_r.map` — MSVC linker map with absolute VAs
- `config/373307D9/symbols.txt` — dtk symbol definitions (27,536 `lbl_` entries)
- `objdiff.json` — project config (could add `map_file` field)

**objdiff:**
- `objdiff-core/src/diff/code.rs:305` — `reloc_eq()` function (relocation comparison)
- `objdiff-core/src/diff/code.rs:275` — `address_eq()` (address-based fallback)
- `objdiff-core/src/obj/map_file.rs` — MSVC map parser (ICF equivalences)
- `objdiff-core/src/config/mod.rs:56` — `ProjectConfig.map_file` field
- `objdiff-cli/src/cmd/report.rs:313-320` — map file loading for reports

---

## Recommended Two-Phase Approach

### Phase 1: Map-based `lbl_` Renaming in `symbols.txt`

**Goal:** Rename `lbl_` symbols in symbols.txt using names from the map file, where available.

**Mechanism:**

The map file has absolute VAs in the `Rva+Base` column:
```
 0009:0005ecd8       ?sPrintTimes@DirLoader@@2_NA  82F648D8  obj:DirLoader.obj
```

symbols.txt uses the same absolute VA format:
```
lbl_82F648D8 = .data:0x82F648D8; // type:object size:0x4 data:4byte
```

Cross-reference and rename:
```
?sPrintTimes@DirLoader@@2_NA = .data:0x82F648D8; // type:object size:0x4 data:4byte
```

**Implementation:**

Python script `scripts/rename_lbl_symbols.py`:
1. Parse map file → build `{address: name}` lookup for .data/.bss/.rdata sections
2. Read symbols.txt line-by-line
3. For each `lbl_XXXXXXXX` entry, check if address exists in map lookup
4. Replace `lbl_ADDR` with real name if found
5. Write updated symbols.txt
6. Rebuild target objects: `ninja`

**Expected Impact:**

- Renames all `lbl_` entries that correspond to **public/global symbols** in the map
- **Won't fix** function-local statics (like FixClassName's 34 statics) because MSVC doesn't export them to the map file
- Likely covers **~40-60%** of lbl_ symbols (rough estimate based on map coverage: 5,655 named / ~16,500 total .data symbols)

**Risks:**

- Low risk — symbols.txt is auto-generated from config, easily regenerated if broken
- Validate: run `ninja` and spot-check a few renamed symbols in objdiff GUI

**Effort:** ~4-6 hours (script + testing + validation)

---

### Phase 2: Positional Matching in objdiff's `reloc_eq()`

**Goal:** For remaining `lbl_` symbols (function-local statics without map entries), implement positional matching.

**Mechanism:**

In `reloc_eq()` (objdiff-core/src/diff/code.rs:305), after name/equivalence/address checks all fail, add:

```rust
// When both sides reference data symbols and names don't match,
// fall back to positional matching: the Nth data-section relocation
// in this function should correspond to the Nth on the other side.
if !names_match
    && both_symbols_in_data_sections(left_reloc, right_reloc)
    && is_local_static_pattern(left_reloc, right_reloc)
{
    let left_ordinal = data_reloc_ordinal_in_function(left_obj, left_ins, left_reloc);
    let right_ordinal = data_reloc_ordinal_in_function(right_obj, right_ins, right_reloc);
    return left_ordinal == right_ordinal;
}
```

**Helper Functions:**

1. **`is_local_static_pattern()`** — checks if symbols match the pattern:
   - Target side: `lbl_[0-9A-F]+` regex
   - Decomp side: MSVC function-local mangling `@?[0-9]??` OR any .data/.bss symbol that isn't global scope

2. **`data_reloc_ordinal_in_function()`** — counts:
   - Walk all instructions in the current function's address range
   - Count data-section relocations encountered before this instruction
   - Return ordinal (0-indexed)

**Why This Works:**

- Function-local statics are initialized in **declaration order** in C++
- The compiler emits guard-bit checks in that **same order**
- If instructions match (same opcode sequence), the Nth data reloc on each side IS the same variable
- This is structural: if declaration order differs, the function will have OTHER codegen differences anyway

**Gating:**

Add a new config field to `DiffObjConfig`:
```rust
pub struct DiffObjConfig {
    // ... existing fields ...
    pub relax_local_static_names: bool, // default: false
}
```

Only apply positional matching when `relax_local_static_names == true`.

**Implementation Plan:**

1. Add `relax_local_static_names` field to config schema (`config.schema.json`)
2. Implement helper functions in `code.rs`
3. Extend `reloc_eq()` with positional fallback (gated by config flag)
4. Add CLI arg `--relax-local-statics` to objdiff-cli
5. Add JSON config field support in objdiff.json parsing
6. Test on DirLoader::FixClassName (should go from 90.4% → ~100%)
7. Run full project `ninja report` and compare before/after match%

**Expected Impact:**

- **Complete fix** for function-local static mismatches
- DirLoader::FixClassName: 90.4% → ~100%
- Project-wide: estimated +0.5-1.5% overall fuzzy match (rough, needs measurement)
- Removes ~95% of false-positive relocation diffs in functions with `static Symbol`

**Risks:**

- **Fragile if declaration order differs** — but if order differs, the function has real codegen differences anyway (different guard-bit check order, different memory layout)
- **Requires passing function context** to `reloc_eq()` — currently only has instruction-level context. Need to thread function bounds through the diff pipeline (moderately invasive)
- **False positives if non-local-statics accidentally match the pattern** — mitigate with strict `is_local_static_pattern()` checks

**Effort:** ~12-20 hours (design + implementation + testing + upstream PR)

---

### Alternative: Extend `parse_msvc_map()` to Load Equivalences File

Instead of modifying symbols.txt (Phase 1) or adding positional matching (Phase 2), extend the map file parser to also read a supplementary equivalences file:

**Format** (plain text TSV):
```
lbl_82F64998	?Foo@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z@4V2@A
lbl_82F6499C	?CharClip@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z@4V2@A
```

**Changes:**
1. Script to generate equivalences file from Ghidra analysis or manual mapping
2. Extend `map_file.rs` to parse second file format (or add `--equivalences-file` arg)
3. Feed equivalences into `symbol_equivalences` map

**Pros:**
- Less invasive to objdiff than Phase 2
- Explicit, verifiable mappings
- Works for any symbol renaming case, not just function-local statics

**Cons:**
- Requires maintaining external equivalences file
- Doesn't scale — 27,536 `lbl_` entries would need manual/scripted mapping
- Still need a way to GENERATE the equivalences (Ghidra? Source-code static analysis?)

**Verdict:** Good for one-off verification, but Phase 2 (positional matching) is more general and doesn't require external files.

---

## Rollout Plan

### Stage 1: Quick Win (Phase 1 only)
- Implement map-based renaming script
- Rename symbols.txt, rebuild
- Measure impact on match% for top 10 affected units
- **Timeline:** 1 week

### Stage 2: Complete Fix (Phase 2)
- Design positional matching in objdiff
- Implement, test locally
- Prepare upstream PR for objdiff repo
- Integrate into DC3 build pipeline
- **Timeline:** 2-3 weeks

### Stage 3: Validation
- Run full `ninja report` comparison (before/after)
- Spot-check 20-30 functions with high `static Symbol` counts
- Document new config option in project docs
- **Timeline:** 1 week

---

## Success Metrics

| Metric | Before | Target (Phase 1) | Target (Phase 2) |
|--------|--------|------------------|------------------|
| DirLoader::FixClassName match% | 90.4% | ~92-95% | ~100% |
| Project-wide fuzzy match | 43.67% | +0.3-0.5% | +0.5-1.5% |
| False reloc diffs in affected functions | ~95% | ~60% | ~5% |
| `lbl_` symbols renamed | 0 | ~10,000-16,000 | ~27,000+ (via equivalence) |

---

## Next Steps

1. **Prioritize Phase 1 first** — quick win, no objdiff changes, low risk
2. **Design review for Phase 2** — need upstream objdiff maintainer buy-in for positional matching approach
3. **Document findings** — update [tools/OBJDIFF_LOCAL_STATIC_MATCHING.md](../tools/OBJDIFF_LOCAL_STATIC_MATCHING.md) with implementation details
4. **Add to BUILD_ROADMAP.md** — Phase 1 is a prerequisite for accurate linking validation

---

## Related Work

- [tools/OBJDIFF_LOCAL_STATIC_MATCHING.md](../tools/OBJDIFF_LOCAL_STATIC_MATCHING.md) — deep technical analysis of reloc_eq() and find_symbol()
- [BUILD_ROADMAP.md](BUILD_ROADMAP.md#symbol-resolution) — symbol resolution in the linking pipeline
- objdiff upstream issue (TBD) — positional matching feature request

---

## References

- objdiff `reloc_eq()`: `/home/free/code/milohax/objdiff/objdiff-core/src/diff/code.rs:305`
- objdiff `address_eq()`: `/home/free/code/milohax/objdiff/objdiff-core/src/diff/code.rs:275`
- objdiff `parse_msvc_map()`: `/home/free/code/milohax/objdiff/objdiff-core/src/obj/map_file.rs:10`
- DC3 map file: `orig/373307D9/ham_xbox_r.map` (119,610 lines)
- DC3 symbols.txt: `config/373307D9/symbols.txt` (211,879 lines, 27,536 `lbl_` entries)
