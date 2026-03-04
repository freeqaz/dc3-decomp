# PCH Build Optimization

**Date**: 2026-03-04
**Status**: IMPLEMENTED — ninja rules added, validated byte-identical .text

## Summary

Precompiled headers (PCH) can safely be added to the ninja build for a 25-40% compile speedup per file. Testing confirms byte-identical `.text` sections and unchanged objdiff match percentages.

## Test Results

### Code Identity: CONFIRMED

All `.text` sections are **byte-identical** between normal and PCH builds. Verified on:
- `HamCamShot.cpp` (19 includes, 1483 sections) — 3/3 `.text` sections match
- `Mesh.cpp` (25 includes) — cross-directory PCH usage works

Differences are limited to metadata sections only:

| Section | Difference | Decomp Impact |
|---------|-----------|---------------|
| `.debug$S` | Output filename + PCH signature bytes | None — objdiff ignores |
| `.XBLD$W` | Compiler pass ID (`C1` vs `C2`) | None |
| `.drectve` | `/alternatename` directives in swapped order | None — linker order-independent |
| `.bss` | 2-byte metadata diff | None |

Symbol table: same symbols, different `$M`/`$T` numbering (compiler temporaries). No functional symbols added or removed.

**objdiff verification**: `HamCamShot::SetType` = 99.6% with both normal and PCH .obj — unchanged.

### Performance

| File | Normal | PCH | Savings |
|------|--------|-----|---------|
| HamCamShot.cpp (19 includes) | 47ms | 35ms | **26%** |
| HamCharacter.cpp (38 includes) | 44ms | 35ms | **20%** |
| Mesh.cpp (25 includes) | 25ms | 15ms | **40%** |
| PCH creation (one-time) | — | 45ms | — |

Savings are proportionally larger on smaller files where preprocessing dominates.

### Cross-Directory: WORKS

PCH created from `src/system/hamobj/` works from `src/system/rndobj/` without issues. All `/I` paths are absolute or wibo-mapped, so header resolution is CWD-independent.

### COFF Patchers: SAFE

All four post-build patchers operate only on COFF symbol table and/or `.text` machine code — unaffected by PCH metadata:

| Patcher | Operates On | PCH Impact |
|---------|------------|------------|
| `obj_anon_ns_patcher.py` | Raw byte find/replace of `?A0x<hash>` | None |
| `obj_guard_patcher.py` | COFF symbol table entries | None |
| `obj_regswap_patcher.py` | `.text` section instructions | None |
| `obj_dynamic_init_patcher.py` | COFF symbol storage class byte | None |

## PCH Header Selection

`obj/Object.h` is the best candidate — included in 369/875 engine `.cpp` files and transitively pulls in most top-10 headers:

| Header | Direct includes | Covered by Object.h |
|--------|----------------|---------------------|
| `obj/Object.h` | 369 | yes (itself) |
| `os/Debug.h` | 350 | yes (via Data.h) |
| `obj/Data.h` | 193 | yes (direct) |
| `utl/BinStream.h` | 147 | yes (direct) |
| `os/System.h` | 124 | no |
| `obj/Dir.h` | 91 | no |
| `utl/Symbol.h` | 90 | yes (direct) |
| `utl/MemMgr.h` | 68 | yes (direct) |
| `utl/Str.h` | 66 | yes (via Data.h) |

Adding `os/Debug.h` explicitly ensures coverage even if the transitive chain changes.

### Force-Include Safety

Using `/FI"decomp_pch.h"` on files that **don't** include `obj/Object.h` could introduce extra template instantiations. Two approaches:

1. **Safe (recommended)**: Only apply PCH to files that already include `obj/Object.h` (~370 files, 42% of build)
2. **Aggressive**: Apply to all files, verify with full `sync_objdiff.py` sweep

Start with approach 1.

## Implementation (DONE)

### Ninja Rules (`tools/project.py`)

Two new rules added after the existing `msvc` rule:

- **`msvc_pch_create`**: Compiles the PCH source with `/Yc"decomp_pch.h" /Fp$pch_out`
- **`msvc_pch`**: Compiles with `/Yu"decomp_pch.h" /FI"decomp_pch.h" /Fp$pch_file`

Both rules derive from the existing `msvc_cmd` string via `.replace()` to ensure identical flags.

### PCH Header

**`src/system/decomp_pch.h`**: `#include "obj/Object.h"` + `#include "os/Debug.h"`
Located on the wibo-mapped include path so it resolves from any source directory.

### File Classification

Files in PCH-eligible directories use `msvc_pch` rule with implicit dependency on the `.pch` file. Non-eligible files use the standard `msvc` rule.

### Storage

```
build/373307D9/pch/system.pch       # ~8MB PCH binary (generated)
build/373307D9/pch/decomp_pch.obj   # throwaway .obj from /Yc step
src/system/decomp_pch.h             # PCH boundary header (checked in)
src/system/decomp_pch.cpp           # PCH source (checked in)
```

## Expected Impact

- **Per-file savings**: 25-40% faster compile for ~370 files (42% of codebase)
- **Full rebuild**: ~15-20% faster overall (non-PCH files unchanged)
- **Incremental rebuild**: Same savings on touched files
- **PCH rebuild**: Only needed when `decomp_pch.h` or its transitive includes change (rare)
- **Combined with wibo dir+stat cache**: up to 50-60% faster per compile

## Risks and Mitigations

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| `.text` differs on some file | Very low (tested on 3 files) | Run `sync_objdiff.py --all` after enabling, verify no regressions |
| Linker issues from `.drectve` ordering | Very low | `/alternatename` order doesn't matter to MSVC linker |
| PCH stale after header change | Medium | Same as existing issue — ninja doesn't track header deps. `ninja -t clean` or touch PCH source |
| Files that don't include Object.h get wrong codegen with /FI | N/A | Only apply PCH to files that already include it |
