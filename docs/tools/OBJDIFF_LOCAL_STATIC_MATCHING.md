# objdiff: Function-Local Static Symbol Matching

## Problem

When a function uses `static Symbol("Foo")` inside a function body, MSVC generates a
function-local static with a mangled name like `?Foo@?3??FixClassName@DirLoader@@...`.
On the target (disassembled) side, these appear as raw address labels like `lbl_82F64998`.

objdiff compares relocations by symbol name (`reloc_eq()` in `diff/code.rs`). Since
`lbl_82F64998 != ?Foo@?3??FixClassName@...`, every instruction referencing a local static
is marked as a mismatch — even when the actual machine code is identical.

This inflates the mismatch count significantly. For `DirLoader::FixClassName` (34 local
statics), the function reports 90.4% despite having zero structural differences — all 290
"mismatches" are symbol relocation noise.

## How objdiff Matches Relocations Today

The chain is:

1. **`diff_code()`** (`diff/code.rs`) — aligns instructions by opcode via patience diff
2. **`diff_instruction()`** — compares each aligned pair arg-by-arg
3. **`arg_eq()`** → **`reloc_eq()`** (`diff/code.rs:305`) — the relocation comparator

`reloc_eq()` checks:
- `left.symbol.name == right.symbol.name` (primary)
- `symbol_equivalences` map (ICF/merged symbols)
- `address_eq()` fallback (raw address comparison — fails because target uses absolute VAs
  while source uses section-relative offsets)

There is also a **`find_symbol()`** function (`diff/mod.rs:719`) that pairs symbols at the
top level. It has a special case for `CompilerGenerated` symbols (like `@1234`): these are
matched by **data content** rather than name. This is the closest existing precedent.

## Proposed Solutions

### Option A: Extend `CompilerGenerated` Content Matching to `lbl_` Symbols

**Where:** `find_symbol()` in `objdiff-core/src/diff/mod.rs:719`

**Idea:** The `CompilerGenerated` path already matches symbols by data content (using
`diff_data_symbol()` to find the best content match). Extend this to also cover target-side
symbols whose names match `lbl_[0-9A-F]+` — these are raw address labels from the
disassembler that have lost their original names.

```rust
// In find_symbol(), after the CompilerGenerated block:
let is_raw_label = in_symbol.name.starts_with("lbl_")
    && in_symbol.name[4..].chars().all(|c| c.is_ascii_hexdigit());

if (in_symbol.flags.0.contains(SymbolFlag::CompilerGenerated) || is_raw_label)
    && matches!(section_kind, SectionKind::Data | SectionKind::ReadOnlyData)
{
    // existing content-matching logic: diff_data_symbol(), pick closest >= 50%
}
```

**Pros:** Minimal code change, reuses proven logic, works for any data symbol (not just
statics). Would match `lbl_82F64998` to `?CharClipSamples@...` if they contain the same
Symbol struct data.

**Cons:** Depends on data content being available in both objects. Function-local statics
for `Symbol` contain just a 4-byte hash — many will have different runtime values since they
aren't initialized until first call. May produce false matches if multiple statics have
similar uninitialized data.

**Verdict:** Probably won't work well for `static Symbol` because the .data content is
just zeroed/uninitialized padding — the actual string value is set at runtime by the
constructor.

### Option B: Positional Matching in `reloc_eq()`

**Where:** `reloc_eq()` in `objdiff-core/src/diff/code.rs:305`

**Idea:** When both sides reference symbols in the same section kind (.data/.bss) and
name matching fails, fall back to positional matching: if this is the Nth data relocation
within the current function's address range, and the other side also has its Nth data
relocation at the same instruction position, treat them as equivalent.

```rust
// After names_match fails in reloc_eq():
if !names_match
    && both_in_data_section
    && is_local_static_pattern(&left_symbol.name, &right_symbol.name)
{
    // Count: how many data-section relocations appear before this one
    // in the current function's instruction range?
    let left_pos = reloc_ordinal_in_function(left_obj, left_section, left_ins, left_symbol);
    let right_pos = reloc_ordinal_in_function(right_obj, right_section, right_ins, right_symbol);
    positional_match = left_pos == right_pos;
}
```

Where `is_local_static_pattern()` checks:
- Left: `lbl_[0-9A-F]+` pattern (raw disassembler label)
- Right: `?..@?[0-9]??...` pattern (MSVC function-local static mangling) or any
  symbol whose section is `.data`/`.bss` and not a global

**Pros:** Doesn't depend on data content. Works even for uninitialized statics. Captures
the structural relationship (Nth static in function = Nth static in function).

**Cons:** Fragile if declaration order differs between source and target (but if order
differs, the function has real codegen differences anyway). Requires passing function
context into `reloc_eq()` (currently only has instruction-level context). More invasive.

**Verdict:** More robust for this use case but requires threading function bounds through
the diff pipeline.

### Option C: Explicit Symbol Mapping File

**Where:** `MappingConfig::mappings` in `objdiff-core/src/diff/mod.rs:539`

**Idea:** Use the existing `apply_symbol_mappings()` mechanism to provide an explicit
map from target labels to source symbol names. This could be auto-generated from a
script that parses both object files.

```json
{
  "mappings": {
    "lbl_82F64998": "?CharClipSamples@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z@4V2@A",
    "lbl_82F6499C": "?CharClip@?3??FixClassName@DirLoader@@AAA?AVSymbol@@V2@@Z@4V2@A"
  }
}
```

**Pros:** Zero changes to objdiff. Fully explicit, no heuristics. Can be generated by
a helper script that correlates guard-bit patterns or string references.

**Cons:** Requires maintaining a mapping file per function. Doesn't scale. Manual.

**Verdict:** Good for one-off verification but not a general solution.

### Option D: Section-Relative Address Normalization

**Where:** `reloc_eq()` in `objdiff-core/src/diff/code.rs`

**Idea:** Instead of comparing absolute addresses, normalize both sides to
section-relative offsets. If target `lbl_82F64998` is at offset 0x18 within its .data
section, and source `?CharClipSamples@...` is also at offset 0x18 within its .data
section, they match.

**Pros:** Simple concept.

**Cons:** Section layouts will differ between target and source (different link order,
different padding). Unlikely to produce matching offsets.

**Verdict:** Won't work — section layouts diverge.

## Recommended Approach

**Option B (positional matching)** is the most promising for correctness. The key insight
is that function-local statics are initialized in declaration order, and the compiler
emits guard-bit checks in that same order. So the Nth local-static relocation in the
target function corresponds to the Nth local-static relocation in the source function.

Implementation steps:

1. **Detect local-static symbols:** On the target side, identify `lbl_` symbols that are
   referenced from a function's code section and live in `.data`/`.bss`. On the source
   side, identify MSVC function-local statics by the `@?[0-9]??` mangling pattern.

2. **Build relocation ordinal maps:** For each function being diffed, walk its relocations
   and assign ordinal numbers to local-static references (skipping code relocations,
   string literal relocations, etc.).

3. **Use ordinals in `reloc_eq()`:** When name matching fails and both symbols look like
   local statics, compare their ordinals instead.

4. **Gate behind a config flag:** Add a `relax_local_static_diffs` option (similar to the
   existing `relax_reloc_diffs` / `FunctionRelocDiffs` enum) so this is opt-in.

## Key Files in objdiff

| File | Purpose |
|------|---------|
| `objdiff-core/src/diff/code.rs:305` | `reloc_eq()` — relocation comparison |
| `objdiff-core/src/diff/code.rs:363` | `arg_eq()` — instruction argument comparison |
| `objdiff-core/src/diff/code.rs:435` | `diff_instruction()` — per-instruction diff |
| `objdiff-core/src/diff/code.rs:58` | `diff_code()` — top-level code diff |
| `objdiff-core/src/diff/mod.rs:719` | `find_symbol()` — top-level symbol pairing |
| `objdiff-core/src/diff/mod.rs:539` | `apply_symbol_mappings()` — explicit mapping |
| `objdiff-core/src/diff/data.rs:38` | `symbol_name_matches()` — name comparison |
| `objdiff-core/src/obj/mod.rs:272` | `Symbol` struct definition |
| `objdiff-core/src/obj/mod.rs:394` | `Relocation` struct definition |
| `objdiff-core/src/obj/read.rs:41` | `get_normalized_symbol_name()` — name normalization |
| `objdiff-core/src/obj/read.rs:90` | `is_symbol_name_compiler_generated()` — flag assignment |

## Affected Functions in DC3

`DirLoader::FixClassName` is the most extreme case (34 local statics, 90.4% reported
vs ~100% actual). Any function using `static Symbol("...")` patterns will be affected
to some degree. A quick grep for `static Symbol` in the codebase would identify all
affected functions.
