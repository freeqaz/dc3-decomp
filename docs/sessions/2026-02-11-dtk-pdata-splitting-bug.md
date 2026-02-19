# DTK .pdata Splitting Bug

**Date**: 2026-02-11
**Status**: Workaround applied, upstream fix needed

## Problem

When `dtk xex split` splits `ham_xbox_r.exe` into ~2223 relocatable .obj files, **127 objects** end up with multiple `.pdata` sections. The X360 MSVC linker (`link.exe 10.00.11886.00`) rejects these with:

```
fatal error LNK1223: invalid or corrupt file: file contains invalid .pdata contributions
```

## What is .pdata?

On Xbox 360 (PowerPC), `.pdata` contains the **function table** — structured entries mapping each function's start/end addresses and unwind info. The MSVC linker strictly validates `.pdata` contributions from each object file.

A valid COFF object should have at most **one** `.pdata` section. Having multiple confuses the linker's pdata validation logic.

## Root Cause (Confirmed)

Traced through the jeff (dtk) source code. The bug spans two functions in `src/util/split.rs`:

**`split_pdata()` (line 838)** creates one `.pdata` split per code split, using the same `unit` name:
```rust
pdata_splits.insert(start, ObjSplit {
    unit: split.unit.clone(),  // Same unit name as the code split
    ...
});
```

If a unit (e.g. `SongSequence`) has code at 3 non-contiguous address ranges in `.text`, `split_pdata()` creates 3 separate `.pdata` splits all named `"SongSequence"`.

**`split_obj()` (line 1264)** then creates a NEW output section for every split it processes — it never checks if the output object already has a section with that name:
```rust
split_obj.sections.push(ObjSection {
    name: split.rename.as_ref().unwrap_or(&section.name).clone(), // Always ".pdata"
    ...
});
```

Result: `SongSequence.obj` gets 3 `.pdata` sections. The MSVC linker rejects this with LNK1223.

There's even a comment on line 842 acknowledging the issue:
```rust
// should we remove all pdata splits before doing this?
// so we avoid duplicates and false "overlaps with split" errors?
```

## Affected Objects (127 total)

A sample:
- `SongSequence.obj` — 3 `.pdata` sections
- `sharedgrammardata.obj` — 3 `.pdata` sections
- Most others — 2 `.pdata` sections
- Covers game code, engine, XDK, and third-party libs

## Workaround Applied

`scripts/build/fix_pdata.py` renames extra `.pdata` sections to `.pdat1`, `.pdat2` etc. This bypasses the linker validation while preserving the data in the output PE (just under a different section name).

This is lossy — the renamed sections won't be recognized as proper function table entries. However, for PoC linking and `.text` comparison, this is acceptable.

## Proper Fix

Three fix strategies, in order of preference:

### Option A: Merge sections in `split_obj()` (best)
In `split_obj()` at line 1264, before pushing a new section, check if the output object already has a section with the same name. If so, append data and relocations to the existing section (adjusting relocation offsets by the existing section's data length) and assign symbols to the existing section index.

Key changes:
- Before `let out_section_idx = split_obj.sections.next_section_index()` (line 1205), search for an existing section with the target name
- If found, use its index for symbol assignment and append data/relocations
- If not found, create new section as before

This is the most correct fix — it handles any section type, not just `.pdata`.

### Option B: Post-process merged sections
After `split_obj()` returns, iterate through each output object and merge sections with duplicate names. This is less invasive but adds a separate pass.

### Option C: Fix in `write_coff()`
Merge same-named sections during COFF generation in `write_coff()` (line 1197). Least invasive but pushes the fix to the output stage rather than fixing the data model.

The fix would be in `src/util/split.rs` (Option A/B) or `src/util/xex.rs` (Option C).

## Detection

Quick COFF header scan (Python):
```python
import struct

def count_pdata(obj_path):
    with open(obj_path, 'rb') as f:
        data = f.read()
    _, num_sections = struct.unpack_from('<HH', data, 0)
    opt_hdr_size = struct.unpack_from('<H', data, 16)[0]
    count = 0
    offset = 20 + opt_hdr_size
    for i in range(num_sections):
        name = data[offset:offset+8].rstrip(b'\x00')
        if name == b'.pdata':
            count += 1
        offset += 40
    return count
```

## Additional Linking Issues Found

During the same linking investigation:

### Local Labels Not Exported (146 unresolved)
- `__savegprlr_15` through `__savegprlr_31`, `__restgprlr_*`, `__savefpr_*`, `__restfpr_*`, `__savevmx_*`, `__restvmx_*` — PPC register save/restore stubs
- These exist in `crtgpr.obj` as **local `Label`** type symbols instead of **`External`** symbols
- Other objects reference them but can't resolve since they're local

**Root cause (confirmed)** — three-step bug chain:

1. **Analysis pass** (`src/analysis/pass.rs:163-173`, `FindSaveRestSledsXbox`): creates individual sled entry symbols with `kind: Unknown` (via `..Default::default()`) even though they have `scope: Global`:
   ```rust
   state.known_symbols.entry(addr).or_default().push(ObjSymbol {
       name: format!("{label}{i}"),
       flags: ObjSymbolFlagSet(ObjSymbolFlags::Global.into()),
       ..Default::default()  // kind defaults to ObjSymbolKind::Unknown!
   });
   ```

2. **`write_coff()`** (`src/util/xex.rs:1228-1229`): maps `Unknown + has_section` → `SymbolKind::Label`:
   ```rust
   ObjSymbolKind::Unknown => match sym.section {
       Some(_) => SymbolKind::Label,  // <-- forces Label kind
   ```

3. **`object` crate** COFF writer (`src/write/coff/object.rs:698`): maps `Label` → `IMAGE_SYM_CLASS_LABEL` (storage class 6) **unconditionally**, ignoring scope:
   ```rust
   SymbolKind::Label => coff::IMAGE_SYM_CLASS_LABEL,
   ```
   Unlike `Text`/`Data` kinds which respect scope (lines 709-720: `Compilation` → STATIC, `Linkage` → EXTERNAL), Label always becomes local.

**Fix**: Set `kind: ObjSymbolKind::Function` in the analysis pass for individual sled entries (they ARE code entry points). The parent function symbols already have `kind: Function` (line 159). Alternatively, fix `write_coff()` to use `SymbolKind::Text` instead of `Label` for globally-scoped `Unknown` symbols.

### Jump Table References (66 unresolved)
- `jumptable_XXXXXXXX` symbols referenced from `.text` but defined as `scope:local` in symbols.txt
- These are switch statement dispatch tables in `.rdata`
- The splitter doesn't export them since they're local scope

**Root cause (confirmed)**: `globalize_symbols` is `false` for XEX splitting (`src/cmd/xex.rs:275`):
```rust
let split_objs = split_obj(&module.obj, None, false)?;  // globalize_symbols = false
```

The `split_obj()` function has globalization logic (lines 1300-1380) that renames and upgrades local symbols to global when they're referenced from other split objects — but it's disabled.

**Fix options considered**:
1. ~~**Enable globalization**~~: pass `true` for `globalize_symbols` — **rejected**. This globalizes ALL local cross-object references, not just jump tables. The rename logic (`{name}_{address}`) would mangle any local symbol that doesn't already end with its address. While jump tables happen to survive (they already end with the address), other local symbols would get renamed. This is a breaking change for consumers — renamed symbols propagate into split object symbol tables and would affect any downstream tooling referencing symbols by name.
2. **Set scope at creation** (chosen): in `src/analysis/cfa.rs:258` where jump table symbols are created, set `scope: Global` instead of the default. Jump tables have unique address-based names (`jumptable_XXXXXXXX`) — no collision risk. This way they're always exported from whichever object defines them, no globalization pass needed. Same philosophy as the sled labels fix: fix classification at the source.
3. **Fix in symbols.txt**: mark jump tables as `scope:global` in the config — fragile, requires manual annotation per-project.

## Jeff Source Code Reference

Key files in `jeff` (dtk fork at `/home/free/code/milohax/jeff`, branch `dev`):

| File | Lines | Function | Role |
|------|-------|----------|------|
| `src/cmd/xex.rs` | 162-237 | `split()` | XEX split CLI entry point |
| `src/cmd/xex.rs` | 239-451 | `split_write_obj_exe()` | Orchestrates splitting pipeline |
| `src/cmd/xex.rs` | 275 | — | `split_obj()` call with `globalize_symbols=false` |
| `src/util/split.rs` | 838-891 | `split_pdata()` | Creates `.pdata` splits per code split |
| `src/util/split.rs` | 1059-1401 | `split_obj()` | Distributes sections/symbols to output objects |
| `src/util/split.rs` | 1264 | — | Creates new section per split (the pdata dup bug) |
| `src/util/split.rs` | 1300-1380 | — | Symbol globalization logic (disabled for XEX) |
| `src/util/xex.rs` | 1186-1289 | `write_coff()` | Generates COFF binary from ObjInfo |
| `src/util/xex.rs` | 1228-1229 | — | `Unknown+section` → `Label` mapping |
| `src/analysis/pass.rs` | 118-178 | `FindSaveRestSledsXbox` | Detects save/restore sleds, creates symbols |
| `src/analysis/pass.rs` | 163-173 | — | Individual sled labels: `kind=Unknown` bug |

Object crate (forked): `git+https://github.com/rjkiv/object.git@839a1c3`
- `src/write/coff/object.rs:698` — `SymbolKind::Label` → `IMAGE_SYM_CLASS_LABEL` (unconditional)
- `src/write/coff/object.rs:709-720` — `Text`/`Data` kinds respect scope for storage class

### wibo Incompatibility
- `link.exe` requires Win32 APIs not implemented in wibo: `lstrcmpiW` (kernel32), `NdrClientCall2` (rpcrt4)
- Workaround: use `wine` instead of `wibo` for linking (wine-staging 11.2 works fine)
- The compilation step (`cl.exe`) continues to work with wibo

---

## Implementation Plan

### Design Decisions

| Bug | Fix | Location | Risk | Rationale |
|-----|-----|----------|------|-----------|
| Pdata dup | Merge same-named sections in `split_obj()` | `split.rs:1205-1278` | Medium | Cleanest — fixes the data model, handles any section type. Preferred over write_coff() workaround. |
| Sled labels | Set `kind: Function` on sled entries | `pass.rs:165` | Low | One-line fix. Parent function already uses `kind: Function`. |
| Jump tables | Set `scope: Global` on jump table symbols | `cfa.rs:258` | Low | Unique address-based names prevent collisions. `globalize_symbols=true` rejected as too broad. |

**Why not `globalize_symbols=true`?** The globalization pass renames ALL local cross-object symbols with address suffixes (`{name}_{addr}`). Jump tables survive (names already end with address), but other local symbols would be mangled. This is a breaking change — renamed symbols propagate into split object symbol tables and affect downstream tooling. Fix at the source instead.

### Change 1: Pdata Section Merge (Medium)

In `split_obj()`, before creating a new section for a split, check if the output object already has a section with that name. If so, merge into it.

**Pseudocode:**
```rust
// Current (line 1205):
let out_section_idx = split_obj.sections.next_section_index();

// New: check for existing section with same name
let section_name = split.rename.as_ref().unwrap_or(&section.name);
let (out_section_idx, is_existing) = match split_obj.sections.by_name(section_name) {
    Ok((idx, _)) => (idx, true),
    Err(_) => (split_obj.sections.next_section_index(), false),
};
```

When merging into an existing section:
- Compute `data_offset = existing_section.size` (byte offset for appended data)
- Offset symbol addresses by `data_offset`
- Offset relocation addresses by `data_offset` (as u32)
- Append section data
- Update `existing_section.size += new_data.len()`
- Take the max alignment

When creating new (no change to existing logic).

**Edge cases to handle:**
- Alignment padding between merged fragments (insert zero padding to satisfy alignment)
- `virtual_address` on merged sections — use the first fragment's VA (or None)
- BSS sections (no data to append, just add size)

### Change 2: Sled Label Kind (Low)

In `FindSaveRestSledsXbox::execute()` at `pass.rs:163-173`:

```rust
// Before:
state.known_symbols.entry(addr).or_default().push(ObjSymbol {
    name: format!("{label}{i}"),
    flags: ObjSymbolFlagSet(ObjSymbolFlags::Global.into()),
    ..Default::default()  // kind = Unknown
});

// After:
state.known_symbols.entry(addr).or_default().push(ObjSymbol {
    name: format!("{label}{i}"),
    flags: ObjSymbolFlagSet(ObjSymbolFlags::Global.into()),
    kind: ObjSymbolKind::Function,  // Exported as TEXT, not LABEL
    ..Default::default()
});
```

### Change 3: Jump Table Scope (Low)

In `cfa.rs` where jump table symbols are created (around line 258):

```rust
// Before (scope defaults to Unknown/Local):
ObjSymbol {
    name: format!("jumptable_{address_str}"),
    ...
}

// After:
ObjSymbol {
    name: format!("jumptable_{address_str}"),
    flags: ObjSymbolFlagSet(ObjSymbolFlags::Global.into()),
    ...
}
```

## Testing Strategy

### Existing Test Infrastructure

Jeff has 41 inline unit tests across 9 files. No integration tests. No test fixtures directory. Helper functions exist for building test objects (`make_test_section()`, `make_test_symbol()`, etc. in `analysis/mod.rs` and `obj/relocations.rs`). CI runs `cargo test --release --all-features` on Linux/Windows/macOS.

Key gap: `split_obj()` and `write_coff()` have **zero** direct test coverage.

### New Unit Tests (in jeff)

#### Test 1: Pdata section merge — `split.rs`
Construct a minimal ObjInfo with:
- `.text` section with 2 non-contiguous splits for the same unit (e.g. `"TestUnit"` at 0x1000-0x1010 and 0x1020-0x1030)
- `.pdata` section with 2 corresponding splits (both named `"TestUnit"`)
- Symbols and relocations in each split

Run `split_obj()`, then assert:
- Output object for `"TestUnit"` has exactly **one** `.pdata` section
- Merged section data = concatenation of both fragments
- Relocation offsets for the second fragment are adjusted by the first fragment's size
- Symbols for the second fragment have addresses offset by the first fragment's size

#### Test 2: Non-duplicate sections unchanged — `split.rs`
Same setup but with units that have only one split each. Assert behavior is unchanged (no regression).

#### Test 3: Sled label COFF export — `xex.rs` or `pass.rs`
Create an ObjInfo with a symbol that has `kind: Function, scope: Global, section: Some(...)`. Run `write_coff()`, parse the output COFF bytes, verify the symbol has `IMAGE_SYM_CLASS_EXTERNAL` (not `IMAGE_SYM_CLASS_LABEL`).

#### Test 4: Jump table symbol scope — `cfa.rs`
Verify that jump table symbols created by analysis have `scope: Global`.

### Integration Validation (against dc3 XEX)

After building jeff with fixes:

1. **Re-split**: run `jeff xex split` against `ham_xbox_r.exe`
2. **Pdata check**: run `fix_pdata.py` detection logic against all ~2223 output objects — expect zero duplicates
3. **Symbol check**: inspect `crtgpr.obj` — `__savegprlr_*` symbols should have storage class EXTERNAL
4. **Jump table check**: spot-check a few objects with jump table references — symbols should resolve
5. **Link test**: attempt `link.exe` via wine — the ultimate validation. Expect the 127 LNK1223 errors, 146 save/restore unresolved, and 66 jump table unresolved to all be gone
