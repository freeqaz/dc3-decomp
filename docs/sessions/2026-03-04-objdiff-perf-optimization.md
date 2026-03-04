# objdiff Batch Performance Optimization

**Date**: 2026-03-04
**Goal**: Speed up `python3 scripts/sync_objdiff.py --all` by improving objdiff-cli internals

## Baseline

| Config | Wall | User CPU | Sys CPU | Funcs/s |
|--------|------|----------|---------|---------|
| Original (old code, -j16) | 144s | 350s | **288s** | 224/s |

## Phase 1: Infrastructure (objdiff-cli batch mode)

### 1a. Pre-loaded symbol index
Replaced O(symbols × units) per-symbol file scanning with a one-time HashMap build.
- Opens each .obj once at startup, extracts all text symbols
- `has_function()` + `match_symbol_by_query()` replaced with O(1) HashMap lookups
- **System time: 288s → 22s** (13x reduction in kernel overhead)

### 1b. Rayon parallel unit processing
Moved per-unit diff loop from sequential to `par_iter()`.
- Enables single-process mode (-j1) with internal parallelism
- Combined with Python -j4 for best results (4 processes × rayon threads each)

### 1c. Skip alt diff when functionRelocDiffs=none
The Python script passes `-c functionRelocDiffs=none`, making the alt diff redundant.
Saved ~4% CPU.

| Config | Wall | User CPU | Sys CPU | Funcs/s |
|--------|------|----------|---------|---------|
| Phase 1, -j4 | 114s | 407s | 23s | 282/s |
| Phase 1, -j1 | 141s | 365s | 22s | 229/s |

## Phase 2: Algorithmic Diff Improvements

### Profiling Results

`perf record` on 500 symbols, single-threaded:

| Hotspot | % CPU | Description |
|---------|-------|-------------|
| `myers::find_middle_snake` | 62% | Core diff algorithm |
| `myers::V::index_mut` | 26% | Array indexing within Myers |
| `find_symbol` | 6% | Symbol matching (data diffs for compiler-generated) |
| `obj::read::parse` | 2% | Object file parsing |
| Everything else | 4% | Serialization, branch resolution, etc. |

**88% of CPU is Myers diff** (called via Patience wrapper from `similar` crate).

### Key Insight

`diff_objs()` diffs **ALL** symbols in a unit, not just the requested ones.
A unit like `Character` (326 symbols) runs Myers 326× even if we only need 5.
29,502 of 32,208 functions (91.6%) are 100% matches — running Myers on them is wasted work.

### 2a. Byte-equality fast path in `diff_code()`

Before calling `scan_instructions` + Patience/Myers, compare raw bytes + relocations:
- If `left_data == right_data` (memcmp) AND relocations match by name/offset/flags
- Return 100% immediately, skip Myers entirely
- Still builds instruction rows for display (calls `scan_instructions`)

### 2b. Selective symbol diffing (`diff_objs_filtered`)

New `diff_objs_filtered()` function accepts `symbol_filter: Option<&BTreeSet<usize>>`:
- Only runs `diff_code`/`diff_data_symbol` for filtered symbol indices
- Skips section-level diffs and mapping symbol generation
- Records `target_symbol` mappings for all matched pairs (needed for lookups)

### 2c. Filtered symbol matching

Modified `matching_symbols()` to accept the filter:
- Filtered symbols: full matching (including expensive data diffs for compiler-generated)
- Unfiltered symbols: cheap name-only matching via `find_symbol_by_name()`
- Eliminates O(n²) data diffs for compiler-generated symbols we don't care about

### Final Benchmarks

| Config | Wall | User CPU | Sys CPU | Funcs/s | Speedup |
|--------|------|----------|---------|---------|---------|
| **Original** (-j16, old code) | **144s** | 350s | 288s | 224/s | 1.0x |
| Phase 1 (-j4) | 114s | 407s | 23s | 282/s | 1.26x |
| Phase 1+2 (-j4) | **107s** | 273s | 23s | 301/s | **1.35x** |

Single-threaded comparison (measures pure algorithmic improvement):

| Config | Wall | User CPU | Notes |
|--------|------|----------|-------|
| Original single-threaded | 305s | 282s | Baseline |
| Phase 1+2 single-threaded | 249s | 226s | **20% less CPU** |

### Remaining Bottleneck

After all optimizations, the remaining time is:
- ~80% Myers diff on the ~2,500 partial-match functions (can't skip these)
- ~10% Object file parsing (opening/mmap/parsing COFF for each unit)
- ~10% Instruction scanning, branch resolution, serialization

Further speedup would require either:
1. Caching parsed Object files across runs (avoid re-parsing unchanged .obj files)
2. Replacing Myers/Patience with a faster diff algorithm for small opcode sequences
3. Moving the Python DB logic into Rust (eliminate process overhead + JSON parsing)

## Changes Summary

### objdiff-core/src/diff/code.rs
- Added byte-equality fast path before `scan_instructions` + Myers pipeline
- Compares raw bytes + relocation targets; returns 100% immediately on match

### objdiff-core/src/diff/mod.rs
- New `diff_objs_filtered()` with `symbol_filter` parameter
- Modified `matching_symbols()` to accept filter; uses cheap name-only matching for unfiltered symbols
- New `find_symbol_by_name()` for cheap matching (skips compiler-generated data diffs)
- Section diffs and mapping generation skipped when filtering

### objdiff-cli/src/cmd/diff.rs (batch mode)
- Pre-loaded symbol index: HashMap<mangled/demangled, unit> built once at startup
- Rayon `par_iter()` for parallel unit processing
- Builds `symbol_filter` set from requested symbols per unit
- Calls `diff_objs_filtered()` instead of `diff_objs()`
- Skip alt diff when `functionRelocDiffs=none`
