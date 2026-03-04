# Build Performance Optimizations: objdiff Report Cache + wibo FS Cache

## Problem

`objdiff-cli report generate` is the bottleneck in incremental builds. For our project (2,224 units, 48,234 functions), it takes **~6s wall / 71s CPU** per invocation — and we run it twice per build (once for `report.json`, once with `functionRelocDiffs=name_address` for `report_raw.json`). That's 12s of report generation on every build, even when only 1 .obj file changed.

### Profiling

`perf report` on `report generate` shows:

```
59.55%  similar::algorithms::myers::find_middle_snake
26.30%  <similar::algorithms::myers::V as IndexMut<isize>>::index_mut
 5.17%  objdiff_core::diff::find_symbol
 1.30%  objdiff_core::obj::read::parse
 0.16%  <dyn objdiff_core::arch::Arch>::scan_instructions
```

86% of CPU is in the Patience diff algorithm (which delegates to Myers internally). The report only needs `match_percent` and `size` per function, but the full instruction-level diff with row construction is computed for every function on every run.

## Proposed Solution: Per-Unit Content-Hash Cache

The key insight: most units don't change between builds. In a typical edit-compile-check cycle, 1-5 .obj files change out of 2,224. Caching unit results by content hash makes the common case nearly free.

### Design

1. **Hash each unit's inputs** before diffing: `hash = xxh3(target_obj_bytes || 0xFF || base_obj_bytes || config_args)`
2. **Check a cache file** (`<output_path>.cache`) for a matching hash
3. **Cache hit**: deserialize the cached `ReportUnit` (protobuf), skip all parsing/diffing
4. **Cache miss**: diff normally, serialize the `ReportUnit`, store in cache
5. **Save cache** after generation (merge old + new entries)

Content hashing (not mtime) is important because file timestamps are unreliable when copying between git worktrees or CI environments.

### Cache Format

Simple binary format — no external dependencies needed:

```
u32: entry_count
For each entry:
  u64: content_hash (xxHash3)
  u32: data_length
  [u8; data_length]: protobuf-encoded ReportUnit
```

This is ~9MB for 2,224 units. Could also use a directory of individual files keyed by hash, or a sqlite DB — the format doesn't matter much since reads/writes are fast.

### Implementation Notes

- **Thread safety**: The cache lookup is read-only and safe to call from rayon's `par_iter`. New entries can be collected in a `Mutex<HashMap>` and merged at the end.
- **Config sensitivity**: Include the `DiffObjConfig` args (like `functionRelocDiffs`) in the hash key so different report configs get separate cache entries.
- **No explicit invalidation needed**: Content hashing means stale entries are simply never looked up again. The cache grows monotonically but can be deleted at any time.
- **Graceful degradation**: If the cache file is missing or corrupt, fall back to full diff (cold cache).

### Additional Optimization: Same-Length Instruction Fast Path

In `diff_instructions()`, when `left_insts.len() == right_insts.len()`, the Patience diff can be skipped entirely — just pair instructions 1:1. This is valid because:

- Same-length sequences have no insertions/deletions
- Decomp output preserves instruction order (no reordering)
- The per-instruction `diff_instruction()` comparison still runs and catches all operand/relocation differences

This helps the ~50% of functions that have the same instruction count on both sides, turning O(n log n) alignment into O(n).

## Results

Tested on our project (Xbox 360 PPC decomp, 2,224 units, 48,234 functions):

| Scenario | Before | After | Speedup |
|----------|--------|-------|---------|
| Cold cache | 6.0s | 6.4s | ~1x (hashing overhead) |
| Warm cache (nothing changed) | 6.0s | **13ms** | **460x** |
| 1 unit changed | 6.0s | **290ms** | **21x** |
| Full ninja build (nothing changed) | 9.0s | **1.7s** | **5.3x** |

Cache correctness verified: aggregate measures, per-unit match percentages, and function counts are identical between cached and uncached reports.

## Reference Implementation

We have a working implementation in our fork. The changes are:

- **`objdiff-core/src/diff/code.rs`**: ~10 lines — same-length fast path in `diff_instructions()`
- **`objdiff-cli/src/cmd/report.rs`**: ~120 lines — `ReportCache` struct + integration into `generate()`
- **`objdiff-cli/Cargo.toml`**: 1 line — `xxhash-rust = { version = "0.8", features = ["xxh3"] }`

Happy to share the diff or PR if useful. The approach is simple enough to reimplement from this description.

---

# wibo: Filesystem Caching for Case-Insensitive Path Resolution

## Problem

wibo translates Windows paths to Linux paths using case-insensitive matching, since Linux filesystems are case-sensitive but Windows isn't. The functions `resolveCaseInsensitive()` and `findCaseInsensitiveFile()` in `files.cpp` walk directory trees doing `std::filesystem::exists()` checks and `directory_iterator` scans for every path component.

When compiling with MSVC cl.exe under wibo, the c1xx frontend generates thousands of filesystem probes as it resolves `#include` paths across multiple `/I` search directories. For a single .cpp file with PCH:

| Syscall | Count |
|---------|-------|
| `newfstatat` | 12,998 |
| `getdents64` | 2,535 |
| `openat` | 1,395 |
| **Total** | ~31,000 |

80% of wall time is in the c1xx frontend, dominated by filesystem I/O — not C++ parsing.

### Root Cause: Cache Gap

wibo already caches several layers:

- `pathFromWindows()` — caches Windows→Linux path string translation (high hit rate)
- `canonicalPath()` — caches `weakly_canonical()` results
- `GetFileAttributesA` — caches stat results (85% hit rate)

But two hot paths have **no caching**:

1. **`resolveCaseInsensitive()`** — called from `pathFromWindows()` on first lookup of each unique path string. Walks every path component doing `exists()` + `directory_iterator`.
2. **`findCaseInsensitiveFile()`** — does a full directory listing every call. Used by `resolveModuleOnDisk()` for DLL loading and `CreateFileA` for file opens.

The `exists()` calls inside these functions go directly to the kernel — they bypass the `GetFileAttributesA` stat cache.

## Solution

Four caching layers, all gated by `WIBO_FS_CACHE=1` environment variable:

### 1. `cachedExists()` — Kernel-level stat cache

Wraps `std::filesystem::exists()` with an `unordered_map<string, bool>`. Catches ALL filesystem probing regardless of which function calls it.

```cpp
static bool cachedExists(const std::filesystem::path &p) {
    static std::unordered_map<std::string, bool> cache;
    auto key = p.string();
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    bool result = std::filesystem::exists(p);
    cache[key] = result;
    return result;
}
```

**Impact**: 9,298 hits / 1,564 misses (86% hit rate).

### 2. `cachedDirEntries()` — Directory listing cache

Caches full directory listings as `unordered_map<string, vector<directory_entry>>`. Used inside `resolveCaseInsensitive()` when it falls through to directory walking.

**Impact**: 30 directories cached, eliminates 97% of `getdents64` calls.

### 3. `resolveCaseInsensitive()` output cache

Caches the final resolved path for each input. Since `pathFromWindows()` already caches its output, this mainly helps when the same Linux path is resolved through different code paths.

**Impact**: 1,356 entries cached.

### 4. `findCaseInsensitiveFile()` output cache

Caches the result of case-insensitive file lookups keyed on `(directory, lowercase_filename)`.

**Impact**: 36 entries cached (DLL loading probes).

### 5. Route `collectSearchDirectories()` through `canonicalPath()`

In `modules.cpp`, `collectSearchDirectories()` called `weakly_canonical()` directly, causing 1,047 redundant `newfstatat` calls on parent directories (`/home`, `/home/free`, `/home/free/code`, etc.) per compile. Changed to use `files::canonicalPath()` which is already cached.

## Results

### Syscall Reduction (single file compile with PCH)

| Syscall | Before | After | Reduction |
|---------|--------|-------|-----------|
| `newfstatat` | 12,998 | 3,402 | **-74%** |
| `getdents64` | 2,535 | 64 | **-97%** |
| `openat` | 1,395 | 276 | **-80%** |
| **Total** | ~31,000 | ~6,000 | **-81%** |

### Wall-Clock Benchmark (20 files, -j1, 3 runs avg)

| Config | Per file | vs baseline |
|--------|----------|-------------|
| No PCH, old wibo | 523ms | baseline |
| PCH, old wibo | 409ms | -22% |
| **PCH, new wibo** | **359ms** | **-31%** |

### Diagnostics

Set `WIBO_FS_CACHE_STATS=1` to print cache statistics at process exit:

```
[wibo FS cache] cachedExists: 9298 hits, 1564 misses
[wibo FS cache] cachedDirEntries: 30 dirs cached
[wibo FS cache] resolveCaseInsensitive: 1356 entries
[wibo FS cache] findCaseInsensitiveFile: 36 entries
```

## Implementation Notes

- All caches are function-local `static` variables — no global state changes needed
- Gated by `WIBO_FS_CACHE=1` so existing behavior is unchanged by default
- Caches are never invalidated (single-process lifetime, files don't change during a compile)
- No thread safety concerns — wibo is single-threaded

## Files Modified

- `wibo/src/files.cpp` — `cachedExists()`, `cachedDirEntries()`, output caches for `resolveCaseInsensitive()` and `findCaseInsensitiveFile()`, `reportFilesCacheStats()`
- `wibo/src/files.h` — `reportFilesCacheStats()` declaration
- `wibo/src/modules.cpp` — route `collectSearchDirectories()` through `canonicalPath()`
- `wibo/dll/kernel32/processthreadsapi.cpp` — hook `reportFilesCacheStats()` into exit path
