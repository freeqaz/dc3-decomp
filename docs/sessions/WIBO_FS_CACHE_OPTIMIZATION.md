# Wibo FS Cache Optimization

**Date**: 2026-03-04
**Status**: Implemented and validated

## Problem

Compiling a single file (HamCamShot.cpp with PCH) takes ~460ms wall time. Strace analysis shows:

| Phase | Time | % |
|-------|------|---|
| Wibo init (PE load) | ~12ms | 3% |
| **c1xx frontend** (parse, includes, templates) | **~366ms** | **80%** |
| c2 backend (PPC codegen, .obj write) | ~82ms | 18% |

The frontend is dominated by **filesystem I/O from path resolution**, not C++ parsing:

- 13K `newfstatat` calls (14ms kernel time, but causes pipeline stalls)
- 2.5K `getdents64` calls (30ms kernel time)
- 1.4K `openat` calls

### Root Cause: Cache Gap in `resolveCaseInsensitive()` and `findCaseInsensitiveFile()`

Wibo has multiple caching layers, but they don't cover the hot paths:

| Layer | Location | Cached? | Hit rate |
|-------|----------|---------|----------|
| `pathFromWindows()` | files.cpp:208 | Yes | High (exact Win path string match) |
| `canonicalPath()` | files.cpp:542 | Yes | High |
| `GetFileAttributesA` stat | fileapi.cpp:669 | Yes | 85% (7508/1305) |
| `resolvedPath()` | fileapi.cpp:336 | Yes | 0/0 (not called during compile!) |
| **`resolveCaseInsensitive()`** | files.cpp:129 | **NO** | N/A |
| **`findCaseInsensitiveFile()`** | files.cpp:514 | **NO** | N/A |

`resolveCaseInsensitive()` walks every path component doing `exists()` + `directory_iterator`. Called from `pathFromWindows()` on first lookup of each unique path string. With 197 unique headers across 7 include dirs, that's hundreds of dir walks.

`findCaseInsensitiveFile()` does a full directory listing every call. Used by `resolveModuleOnDisk()` for DLL loading and `CreateFileA` for file opens.

The `exists()` calls inside these functions go directly to the kernel — they don't go through the `GetFileAttributesA` stat cache.

### FS_CACHE effectiveness (already deployed)

| Metric | Without cache | With cache | Savings |
|--------|---------------|------------|---------|
| `getdents64` | 18,923 | 2,535 | 87% |
| `newfstatat` | 88,736 | 12,998 | 85% |
| Wall time | 3,380ms | 1,064ms | 69% |

### `weakly_canonical()` parent traversal

1,047 repeated `newfstatat` calls on `/home`, `/home/free`, `/home/free/code`, etc. per compile. This is `std::filesystem::weakly_canonical()` walking up the tree for every unique path. Each call stats every parent component.

## Plan

### Fix 1: Cache `resolveCaseInsensitive()` (HIGH impact)

Add a static `unordered_map<string, path>` cache keyed on the input path string, gated by `WIBO_FS_CACHE=1`. This function is called from `pathFromWindows()` on cache miss, so caching here prevents the per-component `exists()` + dir walk.

**Expected impact**: Eliminate ~50% of remaining `newfstatat` calls. Since `pathFromWindows` already caches its output, this mainly helps when the same Linux path is resolved through different code paths.

### Fix 2: Cache `findCaseInsensitiveFile()` (MEDIUM impact)

Add a static cache keyed on `(directory, lowercase_filename)`. This function is called from:
- `resolveModuleOnDisk()` — DLL loading (KERNEL32.dll, c1xx.dll, etc.)
- `CreateFileA` path through `processes_common.cpp`

DLL loading probes each search directory for each DLL name. Caching prevents repeated directory walks for the same lookup.

**Expected impact**: Eliminate the DLL search directory walks (~50 calls, small).

### Fix 3: Kernel-level stat cache (HIGH impact)

The real win: intercept `std::filesystem::exists()` at a lower level. Add an `unordered_map<string, bool>` that caches whether a path exists, used by:
- `resolveCaseInsensitive()` line 131 and 149
- `findCaseInsensitiveFile()` line 520, 536
- `collectSearchDirectories()` line 416

This is the most impactful change because it catches ALL filesystem probing, not just specific function outputs.

Implementation: a `cachedExists(path)` helper function that wraps `std::filesystem::exists()` with the cache.

### Fix 4: Cache `weakly_canonical()` (MEDIUM impact)

The `canonicalPath()` function already caches this, but `resolvedPath()` in fileapi.cpp and `collectSearchDirectories()` in modules.cpp call `weakly_canonical()` directly. Route these through `canonicalPath()` or add their own caches.

**Expected impact**: Eliminate the 1,047 parent-traversal stats per compile.

### Fix 5: Directory listing cache for `resolveCaseInsensitive()` (LOW-MEDIUM)

When `resolveCaseInsensitive()` falls through to the directory walk (line 153), cache the directory listing. Currently it creates a new `directory_iterator` per component per path. A `map<string, vector<string>>` of directory contents would turn O(files) dir reads into O(1) lookups.

This overlaps with Fix 3 — if `cachedExists()` prevents the fallthrough, the dir walk never happens.

## Implementation Priority

1. **Fix 3** (cachedExists) — single change, biggest syscall reduction
2. **Fix 1** (resolveCaseInsensitive cache) — prevents dir walks entirely
3. **Fix 4** (weakly_canonical cache) — eliminates parent traversal
4. Fixes 2, 5 are lower priority / may be unnecessary after 1+3

## Benchmark Protocol

```bash
# Single file with PCH, 3 runs, -j1
cd src/system/hamobj
time wibo ... cl.exe /Yu"decomp_pch.h" ... HamCamShot.cpp

# Syscall count
strace -c -f wibo ... cl.exe ...

# FS cache stats
WIBO_FS_CACHE_STATS=1 wibo ... cl.exe ...
```

Baseline (current): ~460ms/file with PCH, ~13K newfstatat, ~2.5K getdents64

## Results

### Changes Made

**`wibo/src/files.cpp`**:
1. `cachedExists()` — wraps `std::filesystem::exists()` with `unordered_map<string, bool>`. 9298 hits / 1564 misses (86% hit rate)
2. `cachedDirEntries()` — caches full directory listings. 30 directories cached, eliminates 2500+ `getdents64` calls
3. `resolveCaseInsensitive()` — output cache + uses cached helpers internally. 1356 entries cached
4. `findCaseInsensitiveFile()` — output cache + uses cached helpers. 36 entries cached

**`wibo/src/modules.cpp`**:
5. `collectSearchDirectories()` — routes through `canonicalPath()` (already cached) instead of raw `weakly_canonical()`

**`wibo/dll/kernel32/processthreadsapi.cpp`**:
6. Hooks `files::reportFilesCacheStats()` into exit path for `WIBO_FS_CACHE_STATS=1`

### Syscall Reduction (HamCamShot.cpp with PCH)

| Syscall | Before | After | Reduction |
|---------|--------|-------|-----------|
| `newfstatat` | 12,998 | 3,402 | **-74%** |
| `getdents64` | 2,535 | 64 | **-97%** |
| `openat` | 1,395 | 276 | **-80%** |
| **Total** | ~31K | ~6K | **-81%** |

### Wall-Clock Benchmark (20 files, -j1, 3 runs avg)

| Config | Time | Per file | vs baseline |
|--------|------|----------|-------------|
| No PCH, old wibo | 10.46s | 523ms | baseline |
| PCH, old wibo | 8.18s | 409ms | -22% |
| **PCH, new wibo** | **7.18s** | **359ms** | **-31%** |

### Projected Full Rebuild Impact

- 647 PCH-eligible files x 50ms savings = **~32s saved** on -j1 rebuild
- Combined PCH + wibo cache: **~106s saved** vs original (-j1)
- Match% unchanged: verified HamCamShot::SetType (99.6%), RndMesh::Print (99.0%)

## Report Generation Cache (objdiff)

### Problem

`ninja` generates two progress reports (`report.json` and `report_raw.json`) after every build. Each runs `objdiff-cli report generate` which diffs all 2224 units (48,234 functions) using Patience diff. Profiling shows:

- 6s wall / 71s CPU per report (12s total for both)
- 86% of CPU in `similar::algorithms::myers::find_middle_snake` (Patience diff's internal Myers)
- No fast path for identical or nearly-identical functions

### Solution: Content-hash based unit-level cache

Added xxHash3-based caching to `report generate`:

1. For each unit, hash the target+base .obj file contents + config args with xxHash3 (31 GB/s)
2. Check against a binary cache file (`<output>.cache`) storing `hash → protobuf-encoded ReportUnit`
3. Cache hit: skip all parsing, diffing, and row construction
4. Cache miss: diff normally, add result to cache
5. Cache saved after generation with old+new entries merged

Also added a same-length fast path in `diff_instructions()` to skip Patience diff entirely when both sides have the same instruction count (valid since decomp preserves instruction order).

### Results

| Scenario | Before | After | Speedup |
|----------|--------|-------|---------|
| Cold cache (first run) | 6.0s | 6.4s | ~same (hashing overhead) |
| Warm cache (no changes) | 6.0s | **0.013s** | **460x** |
| 1 file changed | 6.0s | **0.29s** | **21x** |
| Full `ninja` (no changes) | 9.0s | **1.7s** | **5.3x** |
| Full `ninja` (1 file) | 9.0s | **1.7s** | **5.3x** |

Cache correctness verified: measures, unit counts, and fuzzy match percentages are identical between cached and fresh reports.

### Files Modified

- `objdiff-core/src/diff/code.rs` — same-length instruction fast path in `diff_instructions()`
- `objdiff-cli/src/cmd/report.rs` — `ReportCache` struct with xxHash3 content hashing, integrated into `generate()`
- `objdiff-cli/Cargo.toml` — added `xxhash-rust` dependency
