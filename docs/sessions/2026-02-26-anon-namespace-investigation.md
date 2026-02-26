# Anonymous Namespace Hash Investigation

**Date**: 2026-02-26
**Goal**: Understand and fix MSVC anonymous namespace hash mismatches between decomp and original .obj files

## Summary

MSVC generates `?A0x<HASH>@@` hashes for anonymous namespace symbols using `SigForPbCb` (CRC-32). We reverse-engineered the algorithm, found the original build machine's CRC-equivalent computer name, and built a comprehensive solution. However, we also discovered a fundamental behavioral difference between MSVC running on real Windows vs. under wibo that prevents compile-time hash matching for header-sourced anonymous namespaces.

## Algorithm

```
h1 = CRC32(computer_name, 0xFFFFFFFF)
h2 = CRC32(normalized_path, h1)
h3 = CRC32("\x00", h2)
```

- CRC-32 with reflected polynomial `0xEDB88320`, no initial/final XOR
- Computer name from `GetComputerNameA` (uppercase)
- Path normalization: directory through `GetShortPathNameW`, filename lowercased
- All reversible: h3→h2 is O(1), multi-byte reversal is O(n)

## What Was Solved

### Computer Name CRC Seed
- Brute-forced via meet-in-the-middle: `CRC32("9QVZU3", 0xFFFFFFFF) = 0x9f6add5d`
- Integrated into wibo via `WIBO_COMPUTER_NAME` env var
- Integrated into build system (`project.py` adds to MSVC commands)

### SigForPbCb Instrumentation (NEW)
- Added `WIBO_SIGFORPBCB_LOG` env var to mspdb_dll.cpp
- Logs: `init_hash result_hash length data` for every SigForPbCb call
- Confirmed exact hash chains for .cpp files match originals
- Also added `PDBOpenEx2W` stub (needed for `/Zi` flag)

### GetShortPathNameW Investigation (NEW)
- Added `WIBO_SHORTPATH_MODE` env var (0=normal, 1=fail, 2=uppercase)
- Added `WIBO_SHORTPATH_LOG` env var for path transformation logging
- Confirmed wibo returns unchanged paths (no 8.3 conversion)

## Key Discovery: Per-TU vs Per-File Hashing

### The Problem
- **Original build**: 55 .obj files share hash `c9fefd64` from Debug.h's `AddToStrings`
- **Our build**: each .obj gets a unique hash from its .cpp path
- Original uses the HEADER's path for the hash; ours uses the .cpp's path

### Evidence
- `SigForPbCb` log shows only ONE anonymous namespace hash chain per compilation
- The chain ALWAYS uses the .cpp file's path, even for header-sourced `namespace {}`
- Tested with:
  - `GetShortPathNameW` returning normal paths → .cpp path used
  - `GetShortPathNameW` returning failure → .cpp path used
  - `GetShortPathNameW` returning uppercase → .cpp path used (different hash, still .cpp)
  - `/Z7` flag → .cpp path used
  - `/Zi` flag → no anonymous namespace symbols at all (PDB-only)
  - Non-inline `AddToStrings` → symbol generated, still .cpp hash

### Root Cause (Hypothesis)
Something in wibo's environment causes c1xx.dll to use the translation unit's main file path for ALL anonymous namespace hashes, rather than tracking which file each `namespace {}` block belongs to. On real Windows, the compiler properly tracks per-file anonymous namespaces. This is likely related to how `GetShortPathNameW` or `InternString` normalizes paths — the long-to-short path conversion may serve as the canonical path identifier that enables per-file tracking.

## Hash Distribution in Original .obj Files

| Hash | Files | Source |
|------|-------|--------|
| `c9fefd64` | 55 | Debug.h (`AddToStrings`) |
| `b39b74bf` | 13 | Unknown (DebugGraph-related) |
| `81ddebd1` | 5 | Unknown (WaveFile-related) |
| `f8e4b4b5` | 4 | Unknown (Unlockable-related) |
| `8ccc14d7` | 2 | Unknown (gesture/stream) |
| `8e584365` | 2 | Unknown (NUI camera) |
| `53f5bb0a` | 2 | Unknown (DateTime) |
| (70 unique) | 1 each | .cpp-sourced |

Total: 91 files with anonymous namespace symbols, 77 unique hashes

## CRC Reversal Results

For each multi-file hash, reversed h3→h2 in O(1). Then isolated unknown path segments via suffix reversal. None of the standard 8.3 root directory patterns (`LAZER_~1` through `~9`, etc.) produced matches. Meet-in-the-middle brute force on 6-char roots found only gibberish collisions — the actual root directory on the original build machine remains unknown.

## Solution: Post-Build Patcher

`scripts/obj_anon_ns_patcher.py` — binary find-and-replace of anonymous namespace hashes in decomp .obj files.

### Coverage
| Category | Count |
|----------|-------|
| Compile-time match (.cpp) | 8 |
| Post-build patchable | 77 |
| Ambiguous (skipped) | 4 |
| No original .obj | 2 |

### Usage
```bash
ninja && python3 scripts/obj_anon_ns_patcher.py --apply
```

### Impact
- objdiff match% unaffected (already normalizes `?A0x` hashes)
- Clean linking: eliminates anonymous namespace symbol mismatches
- Must re-run after each build (~<1 second)

## Wibo Changes Made

### `dll/mspdb/mspdb_dll.cpp`
- `SigForPbCb`: conditional logging via `WIBO_SIGFORPBCB_LOG` env var
- Added `PDBOpenEx2W` stub for `/Zi` support
- `getSigLogFile()` helper for lazy file init

### `dll/kernel32/fileapi.cpp`
- `GetShortPathNameW`: `WIBO_SHORTPATH_MODE` (0=normal, 1=fail, 2=uppercase)
- `WIBO_SHORTPATH_LOG` for path transformation logging

### `dll/kernel32/winbase.cpp` (previous session)
- `GetComputerNameA`/`W`: reads `WIBO_COMPUTER_NAME` env var

## Future Work

1. **Deep c1xx.dll reverse engineering**: Disassemble the anonymous namespace hash generation code path to understand exactly how the compiler decides which file's path to use. The path normalization at `0x1064c9d3` is the entry point.

2. **Real Windows test**: Run the same MSVC compiler on real Windows (or Wine with NTFS support) to confirm that per-file anonymous namespace tracking works there but not under wibo.

3. **`InternString` investigation**: The `InternString` function in c1xx.dll canonicalizes paths. If we can make it produce the same canonical paths as on real Windows, the per-file tracking might work correctly.

## Files Modified

- `src/system/os/Debug.h` — tested non-inline `AddToStrings`, reverted
- `tools/project.py` — `WIBO_COMPUTER_NAME='9QVZU3'` in MSVC commands
- `scripts/obj_anon_ns_patcher.py` — post-build hash patcher (new)
- `docs/plans/ANON_NAMESPACE_HASH_FIX.md` — plan doc (new)
