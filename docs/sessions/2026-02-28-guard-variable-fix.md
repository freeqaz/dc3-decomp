# Session: Static Guard Variable Fix (`??_B` vs `$S`)

**Date:** 2026-02-28
**Status:** /Zi crash FIXED, guard naming requires COFF patcher

## Problem

Our MSVC build under wibo always uses `$S` (unsigned int, STATIC) guard variables for static local initialization, while the original build uses `??_B` (char, EXTERNAL) guards for functions with few statics. This causes:

- **$S counter offset**: `??_B` guards don't consume `$S` slots, so subsequent `$S` numbers are shifted (e.g., target `$S4` vs our `$S3`)
- **Symbol name mismatches**: objdiff reports `diff_arg` on every guard reference
- **Scope**: 864 `??_B` guards across 397 TUs in the original

## Root Cause

The `??_B` naming is NOT caused by `/Zi` alone. It requires the **MREngine** (Minimal Rebuild Engine) to be functional, which needs a real `mspdbsrv.exe` process and PDB file. Even with `/Zi` working under wibo, the compiler still produces `$S` STATIC guards because our MREngine stub returns failure.

## Key Discovery: Machine Code is Identical

Despite `??_B` being mangled as `char` (`@51`) and `$S` as `unsigned int` (`@4IA`), the compiler generates **byte-identical PPC instructions** for both guard types:

- `lwz` (load word) for guard checks — NOT `lbz`, even for char-typed `??_B`
- `stw` (store word) for guard sets
- Same `rlwinm` bit masks, same `ori` bit values

The ONLY differences are in the COFF symbol table:
1. **Symbol name**: `??_B?1??FuncName...@51` vs `?$S1@?1??FuncName...@4IA`
2. **Storage class**: EXTERNAL (2) vs STATIC (3)

## Wibo Fixes Applied (All RESOLVED)

### 1. PDBOpenEx2W signature fix
c1xx.dll calls `PDBOpenEx2W` with **7 arguments** (includes `long cbPage` as 3rd param), but our stub only had **6**.

### 2. TypesQueryTiForCVRecordEx + Types* C wrappers
Added 13 Types* exports with auto-incrementing fake type indices.

### 3. MREngine stubs
Added `MREngine::FOpenW` and `MREngine::FOpen` stubs. Return failure to skip minimal rebuild.

### 4. OpenIpi 0-argument placeholder (THE /Zi CRASH FIX)
**Root cause**: The FakePDB vtable had `OpenIpi` at slot 9 with **2 stack arguments** (matching the VS2015 signature). But in the VS2008/VS2010 PDB interface used by the X360 compiler, `OpenIpi` at slot 9 is a **0-argument placeholder** (returns success as a no-op). The 2-arg version's `ret 8` over-cleaned the stack by 8 bytes, corrupting saved registers and the return address.

**Evidence chain:**
1. GDB single-step from PDB::Commit: only ~12 instructions to crash
2. Stack math: OpenIpi's `ret 8` shifts ESP +8, causing `pop esi` to get the return address and `ret` to jump to 0x1068c890 (data section)
3. After removing OpenIpi entirely (first attempt): crashed at a different point because `QueryLastErrorExW` at vtable offset 0x4C was now at the wrong slot — proving the VS2015 vtable layout (WITH OpenIpi) is correct for higher slots
4. The error function at 0x10599d6a calls `PDB vtable[0x4C]` = slot 19 expecting `QueryLastErrorExW` — matching VS2015 layout
5. The cleanup function calls slot 9 with 0 args and slot 10 with 0 args — expecting OpenIpi(0 args) and Commit(0 args)

**Fix**: Change `OpenIpi` from `int __thiscall OpenIpi(const char *, void **)` (2 stack args) to `int __thiscall OpenIpi()` (0 stack args). This keeps the VS2015 vtable slot layout intact (all higher slots unchanged) while matching the VS2008 calling convention for slot 9.

**Result**: `/Zi` compilation succeeds (exit code 0). Full PDB trace:
```
PDBOpenEx2W → QueryAge → OpenTpi → TypesQueryTiForCVRecordEx x15 → TPI::Close → OpenIpi → Commit
```

### `/Zi` test results (updated):
- Without `/Zi`: compiles OK, produces `$S` STATIC guards
- With `/Z7`: compiles OK, produces `$S` STATIC guards
- With `/Zi`: **compiles OK** (exit 0), still produces `$S` STATIC guards
- With `/Zi /Gm`: crashes (missing kernel32 functions for file I/O)

## Fix Strategy: Post-build COFF Patcher

Since `/Zi` doesn't change guard naming (MREngine would be needed), the fix is a COFF symbol table patcher similar to `obj_anon_ns_patcher.py`:

1. Read COFF symbol table from each .obj file
2. Compare with original .obj to identify which `$S` guards should be `??_B`
3. Rename `$S` → `??_B` with correct mangling (function scope → `@51` char type)
4. Change storage class STATIC (3) → EXTERNAL (2)
5. Renumber remaining `$S` guards to match original numbering

This works because the machine code bytes are proven identical.

## Files

- **Wibo source**: `/home/free/code/milohax/wibo/`
- **mspdb stub**: `/home/free/code/milohax/wibo/dll/mspdb/mspdb_dll.cpp`
- **mspdb DEF**: `/home/free/code/milohax/wibo/dll/mspdb/mspdb.def`
- **Compiler**: `build/compilers/X360/16.00.11886.00/cl.exe`

## Impact

Fixing this would improve match% on functions across 397 TUs that reference guard variables. Functions like MoveVariant::IsRest (98.0%), MoveCandidate::Adjacency (92.0%), and ClipCollide::SyncWaypoint (98.8%) would benefit.
