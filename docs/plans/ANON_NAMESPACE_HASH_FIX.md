# Anonymous Namespace Hash Fix

## Background

MSVC generates anonymous namespace hashes (`?A0x<HASH>@@`) based on the build machine's computer name and the source file path. Our decomp build environment produces different hashes than the original binary because:

1. Different computer name
2. Different source file paths (directory structure)

These mismatched hashes cause symbol name differences in the linked binary, affecting relocation arguments (`diff_arg` noise in objdiff) even though objdiff normalizes `?A0x` hashes for symbol pairing (PR #325 by @rjkiv).

## Hash Algorithm

Discovered by reverse-engineering `SigForPbCb` in `mspdb80.dll`:

```
h1 = CRC32(computer_name, 0xFFFFFFFF)
h2 = CRC32(source_path, h1)
h3 = CRC32("\x00", h2)
final_hash = h3
```

- CRC-32 with reflected polynomial `0xEDB88320`
- No initial/final XOR inversions (raw CRC state, not standard CRC-32)
- Computer name: uppercase ASCII (from `GetComputerNameA`)
- Source path: lowercased filename+extension only; directory case preserved

### Path Normalization (c1xx.dll @ 0x1064c9d3)

The compiler normalizes paths before hashing:
1. `_splitpath_s` to extract drive/dir/fname/ext
2. `_makepath_s(drive, dir, NULL, NULL)` to get directory prefix
3. `GetShortPathNameW` on the directory (8.3 conversion if available)
4. `_makepath_s(NULL, shortDir, origFname, origExt)` to reassemble
5. `InternString` (which lowercases the result)

Key: Only the filename+extension are lowercased. Directory case is preserved from the filesystem (or 8.3 conversion).

## What Was Solved

### Computer Name (CRC Seed)

Brute-forced via meet-in-the-middle CRC reversal:
- Original h1 = `0x9f6add5d`
- Collision: `CRC32("9QVZU3", 0xFFFFFFFF) = 0x9f6add5d`
- Not the actual computer name, but produces identical hashes

### Wibo Integration

Modified `wibo/dll/kernel32/winbase.cpp`:
- `GetComputerNameA`/`W` now read `WIBO_COMPUTER_NAME` env var
- Falls back to `"COMPNAME"` if unset

### Build System Integration

Modified `tools/project.py`:
- Added `WIBO_COMPUTER_NAME='9QVZU3'` to all MSVC build commands
- Regenerated `build.ninja` via `scripts/build/configure.sh`

### Result: 8 .cpp Files Match

Files whose anonymous namespace comes only from the .cpp file itself now produce matching hashes:
- ChunkStream, MidiReader, HttpReqCurl, Memcard_Xbox, and 4 others
- 65 anonymous namespace symbols match exactly

## Remaining Problem: Header-Sourced Hashes

Of 91 total files with anonymous namespace symbols:
- **8 match** — hash comes from .cpp file path (solved by computer name fix)
- **77 patchable** — hash comes from included headers (solved by post-build patcher)
- **4 ambiguous** — multiple non-common hashes, skipped
- **2 no original** — no corresponding original .obj file

### Why Header Hashes Don't Match

The original source headers contained anonymous namespace content (non-inline functions/variables) that produced hashes based on the header's path. Our decomp headers either:
1. Don't have the same anonymous namespace content
2. Are at different filesystem paths
3. May use 8.3 short path names (from `GetShortPathNameW`) that we can't reproduce

### Investigation: Exhaustive Header Path Search

Tried all 1,338 known header filenames from the binary × 25 directory prefixes × 6+ format variations = **0 matches**.

Format variations tested:
- Backslash vs forward slash separators
- With/without drive letter (`e:\` prefix)
- Lowercase vs original case directories
- Full paths vs relative paths
- With/without trailing components

The .cpp path format works perfectly but no header path format produces any matching hash. This strongly suggests 8.3 short path conversion is the differentiator — the original build machine's NTFS filesystem returned specific short names that we cannot reproduce.

## Solution: Post-Build .obj Patcher

**`scripts/obj_anon_ns_patcher.py`** — binary find-and-replace of anonymous namespace hashes in decomp .obj files to match originals.

### Usage

```bash
# Dry run (show what would be patched)
python3 scripts/obj_anon_ns_patcher.py --verbose

# Apply patches
python3 scripts/obj_anon_ns_patcher.py --apply

# Verify (re-run shows 0 to patch)
python3 scripts/obj_anon_ns_patcher.py --verbose
```

### How It Works

1. Scans original .obj files (`build/373307D9/obj/`) for `?A0x<HASH>@@` patterns
2. Scans decomp .obj files (`build/373307D9/src/`) for the same patterns
3. For each decomp file with exactly 1 hash:
   - If original also has 1 hash: direct replacement
   - If original has N hashes: heuristic — replace with the "unique" hash (not a common header hash)
   - Common header hashes (appearing in >5 original files) are excluded from unique set
4. Binary replace all occurrences of the decomp hash with the target hash

### Properties

- **Idempotent**: Safe to re-run; already-matching files are skipped
- **Non-destructive**: `--apply` flag required; dry run by default
- **Post-build only**: Patches are lost on rebuild; must re-run after `ninja`

### Integration Plan

Add as a post-build step after `ninja`:
```bash
ninja && python3 scripts/obj_anon_ns_patcher.py --apply
```

Or integrate into the ninja build as a post-link step (if the linker consumes these .obj files).

## Impact Assessment

### On objdiff Match%

**Minimal.** objdiff already normalizes `?A0x` hashes in symbol names for pairing purposes (PR #325). The patcher fixes `diff_arg` noise in instructions that reference anonymous namespace symbols, but this is cosmetic — it doesn't change the structural match percentage.

### On Linked Binary

**Significant for clean linking.** When the linker resolves symbols, mismatched anonymous namespace hashes cause:
- `LNK2001` unresolved externals (decomp references hash X, original defines hash Y)
- `LNK4006` multiply defined symbols (both hashes define the same logical symbol)

The patcher eliminates these link errors for anonymous namespace symbols.

### Current Numbers

| Category | Count |
|----------|-------|
| Total files with anon ns | 91 |
| Compile-time match (.cpp) | 8 |
| Post-build patchable | 77 |
| Ambiguous (skipped) | 4 |
| No original .obj | 2 |

## Root Cause: Per-TU vs Per-File Hash Assignment (Updated 2026-02-26)

Investigation with `WIBO_SIGFORPBCB_LOG` instrumentation revealed:

- **MSVC under wibo** assigns ONE anonymous namespace hash per translation unit, always from the **.cpp file's path**
- **Original MSVC on Windows** assigns hashes per-file: header `namespace {}` blocks use the **header's path**
- This explains why 55 original .obj files share `c9fefd64` (Debug.h's path) while our build gives each file a unique .cpp-based hash

All attempted fixes failed to change this behavior:
- `GetShortPathNameW` returning failure/unchanged/uppercase → still .cpp path
- `/Z7` debug info → still .cpp path
- `/Zi` debug info → no anonymous namespace symbols at all
- Removing `inline` from `AddToStrings` → symbol generated but still .cpp hash

The root cause is likely in how c1xx.dll's `InternString` path canonicalization interacts with wibo's filesystem layer. On real Windows, `GetShortPathNameW` returns canonical 8.3 paths that let the compiler uniquely identify each source file. Under wibo, the identity function may cause the compiler's file-tracking to fall back to per-TU hashing.

## Recommendation: Post-Build Patcher (Permanent Solution)

The patcher is simple, reliable, and solves the problem completely. The only downside is it must re-run after every build, but this takes <1 second.

The wibo-level fix would require either:
- Deep reverse engineering of c1xx.dll's file-tracking/InternString code
- Running the compiler on real Windows (or Wine with NTFS support)
- Neither is cost-effective given the patcher works perfectly

## Wibo Instrumentation Added

For future investigation, these env vars are now available:
- `WIBO_SIGFORPBCB_LOG=<path>` — log all SigForPbCb calls (path, hash, data)
- `WIBO_SHORTPATH_MODE=0|1|2` — control GetShortPathNameW (normal/fail/uppercase)
- `WIBO_SHORTPATH_LOG=<path>` — log GetShortPathNameW in/out transformations
- `WIBO_COMPUTER_NAME=<name>` — set computer name for CRC seed
