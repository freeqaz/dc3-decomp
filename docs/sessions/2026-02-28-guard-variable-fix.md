# Session: Static Guard Variable Fix (`??_B` vs `$S`)

**Date:** 2026-02-28
**Status:** In progress

## Problem

Our MSVC build under wibo always uses `$S` (unsigned int, STATIC) guard variables for static local initialization, while the original build uses `??_B` (char, EXTERNAL) guards for functions with few statics. This causes:

- **$S counter offset**: `??_B` guards don't consume `$S` slots, so subsequent `$S` numbers are shifted (e.g., target `$S4` vs our `$S3`)
- **Symbol name mismatches**: objdiff reports `diff_arg` on every guard reference
- **Scope**: 864 `??_B` guards across 397 TUs in the original

## Root Cause

The original build used `/Zi` (PDB debug info). With `/Zi`, the compiler communicates with `mspdbsrv.exe` and makes guard variables EXTERNAL with `??_B` naming. Without `/Zi`, guards default to STATIC with `$S` naming.

Our build can't use `/Zi` because it **crashes** under wibo (SIGSEGV, exit code 139) — `mspdbsrv.exe` requires Windows IPC that wibo doesn't fully implement.

## Key Discovery: Machine Code is Identical

Despite `??_B` being mangled as `char` (`@51`) and `$S` as `unsigned int` (`@4IA`), the compiler generates **byte-identical PPC instructions** for both guard types:

- `lwz` (load word) for guard checks — NOT `lbz`, even for char-typed `??_B`
- `stw` (store word) for guard sets
- Same `rlwinm` bit masks, same `ori` bit values

The ONLY differences are in the COFF symbol table:
1. **Symbol name**: `??_B?1??FuncName...@51` vs `?$S1@?1??FuncName...@4IA`
2. **Storage class**: EXTERNAL (2) vs STATIC (3)

## Evidence

### MoveVariant.obj comparison:

**Original:**
```
EXTERNAL  ??_B?1??Adjacency@MoveCandidate@@SAIVSymbol@@@Z@51   (char guard)
EXTERNAL  ?$S3@?1???0MoveVariant@@...@4IA                       ($S3, uint)
EXTERNAL  ?$S4@?1??IsRest@MoveVariant@@QBA_NXZ@4IA              ($S4, uint)
```

**Our build:**
```
STATIC    ?$S1@?1??Adjacency@MoveCandidate@@SAIVSymbol@@@Z@4IA  ($S1, uint)
STATIC    ?$S2@?1???0MoveVariant@@...@4IA                       ($S2, uint)
STATIC    ?$S3@?1??IsRest@MoveVariant@@QBA_NXZ@4IA              ($S3, uint)
```

### `/Zi` test results:
- Without `/Zi`: compiles OK, produces `$S` STATIC guards
- With `/Z7`: compiles OK, produces `$S` STATIC guards (same)
- With `/Zi`: **SIGSEGV** (exit code 139) — mspdb interaction crashes

## Wibo Fixes Applied

### 1. PDBOpenEx2W signature fix (CRITICAL)
**Root cause**: c1xx.dll calls `PDBOpenEx2W` with **7 arguments** (includes `long cbPage` as 3rd param), but our stub only had **6**. This shifted all subsequent args by one position, making `pec` read the page size value (0x1000) instead of the actual pointer.

**Fix**: Added `long cbPage` as 3rd parameter to `PDBOpenEx2W`.

**Evidence**: Decoded the caller's 32-bit x86 push sequence:
```
push [ebp-0x820]     ; arg7: ppPDB
push 0x400           ; arg6: cchErrMax = 1024
push &[ebp-0x818]    ; arg5: error buffer
push &[ebp-0x81c]    ; arg4: pec
push [ebp+0x14]      ; arg3: cbPage = 0x1000 (NEW)
push &[ebp-0x18]     ; arg2: mode = "iw"
push eax             ; arg1: PDB path
call [IAT]           ; call PDBOpenEx2W
add esp, 0x1c        ; clean 28 bytes = 7 args
```

### 2. TypesQueryTiForCVRecordEx + Types* C wrappers
c1xx.dll calls C-style wrapper functions (not vtable methods) for TPI operations. Added 13 Types* exports with auto-incrementing fake type indices.

### 3. MREngine stubs
Added `MREngine::FOpenW` and `MREngine::FOpen` stubs (Minimal Rebuild Engine) to the DEF file. These are required by c1xx.dll when `/Zi` is used but return failure to skip minimal rebuild.

### 4. Remaining crash (UNSOLVED)
After all PDB setup completes (QueryAge, OpenTpi, TypesQueryTiForCVRecordEx x15, TPI::Close, OpenIpi, Commit), c1xx.dll crashes at **PC=0x1068c890** (data section, all zeros). This is likely:
- A callback pointer that mspdb should populate but didn't
- An initialization that requires real PDB file operations
- Something in c1xx.dll's /Zi code path that depends on internal mspdb state

Investigation ongoing with GDB hardware breakpoints.

## Fix Strategy

### Primary: Fix wibo's mspdb stub for `/Zi` (IN PROGRESS)
Three of four issues fixed. Remaining crash after PDB::Commit needs deeper investigation.

### Fallback: Post-build COFF patcher
If the wibo fix requires too much additional work, a patcher (like `obj_anon_ns_patcher.py`) could:
1. Read COFF symbol table from .obj files
2. Find `$S` (STATIC) guard symbols
3. Rename to `??_B` format (EXTERNAL) with correct mangling
4. Renumber remaining `$S` guards to match original numbering
5. Change storage class STATIC → EXTERNAL

This works because the machine code bytes are proven identical.

## Files

- **Wibo source**: `/home/free/code/milohax/wibo/`
- **mspdb stub**: `/home/free/code/milohax/wibo/dll/mspdb/mspdb_dll.cpp`
- **mspdb DEF**: `/home/free/code/milohax/wibo/dll/mspdb/mspdb.def`
- **Compiler**: `build/compilers/X360/16.00.11886.00/cl.exe`
- **Test file**: `/tmp/claude-1000/guard_test.cpp`

## Impact

Fixing this would improve match% on functions across 397 TUs that reference guard variables. Functions like MoveVariant::IsRest (98.0%), MoveCandidate::Adjacency (92.0%), and ClipCollide::SyncWaypoint (98.8%) would benefit.
