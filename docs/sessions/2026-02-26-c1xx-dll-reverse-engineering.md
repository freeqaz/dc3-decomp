# c1xx.dll Reverse Engineering: Anonymous Namespace Hash Generation

**Date**: 2026-02-26
**Goal**: Understand how c1xx.dll assigns anonymous namespace hashes to determine if a compile-time fix is possible

## Summary

Disassembled the Xbox 360 MSVC c1xx.dll (`build/compilers/X360/16.00.11886.00/c1xx.dll`, VS2010 era, 1.7MB PE32) to trace the anonymous namespace hash generation code path. Found that the per-file hash assignment flag is **dead code** in this compiler version — it always uses the main TU path. This contradicts our evidence of shared hashes in original .obj files, suggesting the original build used a different mechanism.

## Key Resources

- **c1xx.dll**: `build/compilers/X360/16.00.11886.00/c1xx.dll` (PE32, i386)
- **microsoft-pdb repo**: `~/code/milohax/microsoft-pdb/` — contains `SigForPbCb` source and `SzCanonFilename` header
- **SigForPbCb source**: `microsoft-pdb/langapi/shared/crc32.h` — CRC-32 with static 256-entry lookup table, reflected polynomial 0xEDB88320

## Call Chain

The anonymous namespace hash is generated through this chain:

```
0x106124ba  Scope/symbol node handler
  → 0x105d3fc0  Anonymous namespace mangler
    → 0x105d2ffb  Get current file path  ← KEY FUNCTION
    → 0x105d3e12  Build mangled symbol name
      → 0x105d32c1  CRC32(computer_name, 0xFFFFFFFF)  ← first SigForPbCb call
      → 0x105d2a41  Encode hash to base-36 mangled form
      → 0x105d3258  Encode path bytes into mangled name
      → 0x105d33d2  Optional: additional file-specific hash  ← only if filePath != NULL
```

### IAT Imports Used

| IAT Address | Function | Used In |
|-------------|----------|---------|
| `0x105011a0` | `SigForPbCb` (MSPDBXX.DLL) | 8 call sites, CRC-32 hashing |
| `0x10501084` | `GetShortPathNameW` (KERNEL32) | Path canonicalization at 0x1064c9d3 |
| `0x10501080` | `GetComputerNameW` (KERNEL32) | Computer name for CRC seed |
| `0x10501368` | `_splitpath_s` (MSVCR100) | Path normalization |
| `0x105012dc` | `_makepath_s` (MSVCR100) | Path reassembly |

## Path Normalization (0x1064c9d3)

This function canonicalizes a file path string:

1. Intern the raw string via content-hash table (`0x105e35ed` — Jenkins-like hash, LCG finalization)
2. Check cache keyed by **interned pointer** (not string content) at `0x1063adda`
3. If cache miss:
   - `_splitpath_s` to extract drive/dir/fname/ext
   - `_makepath_s(drive, dir, NULL, NULL)` to get directory prefix
   - `GetShortPathNameW` on the directory (8.3 conversion)
   - `_makepath_s(NULL, shortDir, origFname, origExt)` to reassemble
   - Intern result and cache it
4. Return the interned canonical path pointer

The cache uses pointer identity (the interned string's address), not string content comparison. This means two paths that intern to different pointers will always get separate cache entries, even if they'd normalize to the same canonical form.

## The Per-File Flag (0x10688aa9) — Dead Code

The function at `0x105d2ffb` determines which file's path is used:

```c
char* GetCurrentFilePath() {
    void* ctx = g_currentContext;        // global at 0x106a4824
    if (ctx != NULL && g_perFileFlag) {  // g_perFileFlag at 0x10688aa9
        return getFileFromContext(ctx);   // would return HEADER path
    }
    // Fallback: always returns main TU (.cpp) path
    FileList* list = g_anotherFlag ? g_primaryFiles : g_secondaryFiles;
    return list->first->fileInfo->pathInfo->path;
}
```

**`g_perFileFlag` at `0x10688aa9` is permanently 0:**
- Statically initialized to 0 in .data section (verified by reading raw PE bytes)
- Never written to anywhere in c1xx.dll (exhaustive search of all instruction encodings)
- Only read (compared against 0) at 4 locations
- The per-file code path (`getFileFromContext` at `0x10605c73`) is dead code

This means **this compiler version always uses the main TU path for anonymous namespace hashing**, regardless of which file the `namespace {}` block is in.

## The Contradiction

If the compiler always uses the main TU path:
- Each .cpp file should get a **unique** anonymous namespace hash
- But 55 original .obj files share hash `c9fefd64` (all containing `AddToStrings` from Debug.h)
- This is impossible — 55 different .cpp filenames can't CRC to the same value

### Possible Explanations

1. **Different compiler version**: The original DC3 build may have used a c1xx.dll where `g_perFileFlag` was enabled or the logic was different. The Xbox 360 SDK went through many versions; ours is `16.00.11886.00` but the original build machine may have had a different patch level.

2. **Build system artifact**: The original build may have used precompiled headers, response files, or a build tool that presented a different "main file" path to the compiler.

3. **Linker reprocessing**: The original .obj files under `build/373307D9/obj/` are split from the linked binary by `dtk xex split`, not directly from the original build. If the linker or splitter modified anonymous namespace symbols, the hashes we see may not reflect what the compiler originally produced.

4. **Caching side effect**: On real Windows NTFS, `GetShortPathNameW` returns actual 8.3 short names. If the path canonicalization at `0x1064c9d3` produced path strings that caused the intern table to alias different files to the same pointer, the cache would return the same canonical path for all of them — producing the same hash.

## Intern Table Details

### String Intern Table (0x105e35ed)

Hashes string content using Jenkins-like algorithm:
- Init: `0xb170a1bf`
- Per 4-byte chunk: `hash += *(uint32_t*)ptr; hash *= 0x401; hash ^= (hash >> 6)`
- Per remaining byte: `hash += byte; hash *= 0x401; hash ^= (hash >> 6)`
- Finalize: `hash *= 0x19660d; hash += 0x3c6ef35f` (LCG)
- Then looks up/inserts in a separate hash table at `0x105e329c`

### Pointer-Identity Cache (0x105d8887)

- Keys: interned string pointers (not content)
- Hash: `ptr * 0x19660d + 0x3c6ef35f` (same LCG as string intern)
- Bucket: `hash % table_size`
- Node: `{next, key_ptr, value}` (12 bytes)
- Walk chain comparing `node.key_ptr == search_ptr`

## SigForPbCb Call Sites in c1xx.dll

| Address | Context |
|---------|---------|
| `0x105d32dc` | Computer name hash init (CRC32(name, 0xFFFFFFFF)) |
| `0x105fbb16` | Unknown (general hashing) |
| `0x105fc6ec` | Unknown (general hashing) |
| `0x1063859f` | Unknown |
| `0x1064962f` | Small hash (result & 0x3FFF), not anonymous namespace |
| `0x10650d0d` | Path string hash (init=0), likely `??_C@_` string literals |
| `0x10651c92` | CRC accumulator `Feed()` method |
| `0x10651ce4` | CRC accumulator `Hash()` method |

## Computer Name Hash Init (0x105d32c1)

```c
static uint32_t g_computerNameHash;  // 0x106a6f20
static uint8_t  g_hashInited;        // 0x106a6f24

uint32_t InitComputerNameHash(const char* name, int len) {
    if (!(g_hashInited & 1)) {
        g_hashInited |= 1;
    }
    g_computerNameHash = 0xFFFFFFFF;
    g_computerNameHash = SigForPbCb(name, len, 0xFFFFFFFF);
    return g_computerNameHash;
}
```

Called from `0x105d33d2` and `0x105d3e64` (the mangling functions). The global is only written here and never read elsewhere — callers use the return value.

## Web Research Results

- No public documentation exists for `InternString` or `SigForPbCb` internals
- Clang's MSVC-compatible implementation uses xxHash64 (deliberately NOT matching MSVC)
- LLVM review D50877 confirms MSVC uses absolute file paths, Clang uses relative
- Quarkslab's XFG analysis of c1.dll is the closest public RE work, but covers XFG hashes not anonymous namespaces
- Geoff Chappell's c1xx.dll docs cover command-line options but not internal hashing

## Conclusion

The per-file anonymous namespace code path is **dead code** in this compiler version. A compile-time fix via wibo's `GetShortPathNameW` is not viable — the code path that would use per-file paths is never reached regardless of what `GetShortPathNameW` returns.

The **post-build patcher** (`scripts/obj_anon_ns_patcher.py`) remains the correct solution. It handles 77/91 files with anonymous namespace symbols, runs in <1 second, and must be re-run after each build.

## Files Examined

- `build/compilers/X360/16.00.11886.00/c1xx.dll` — primary target, disassembled with i686-w64-mingw32-objdump
- `~/code/milohax/microsoft-pdb/langapi/shared/crc32.h` — SigForPbCb source (CRC-32 implementation)
- `~/code/milohax/microsoft-pdb/PDB/include/szcanon.h` — SzCanonFilename declaration
- `~/code/milohax/microsoft-pdb/PDB/dbi/cbind.cpp` — SzCanonFilename wrapper
