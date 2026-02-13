# Wibo PDB Vtable Implementation - Session Notes (2026-02-12)

## Goal
Replace wine with wibo for running X360 `link.exe`, producing `build/373307D9/default.exe` natively on Linux.

## What Was Done

### Fake PDB Vtable Objects (mspdb.cpp rewrite)
Rewrote `/home/free/code/milohax/wibo/dll/mspdb.cpp` (878 lines) with runtime-generated x86 __thiscall stubs for fake COM-style PDB objects. The approach:

- **Runtime x86 machine code generation** via `mmap(MAP_32BIT)` — tiny stubs (6-24 bytes each) in executable memory
- **__thiscall convention**: `this` in ECX, args on stack, callee pops N args via `ret N`
- **Stub types**: `genRet(val, nargs)` returns a value, `genVoid(nargs)` returns nothing, `genOut(offset, val, nargs)` writes to an output pointer arg, `genClearBuf(offset, size, nargs)` zeroes a buffer, `genTrap(idx)` triggers int3 with identifying index
- **8 fake object types**: PDB (32 slots), DBI (64 slots), Mod (50+ slots), TPI (23 slots), GSI (11 slots), Dbg (11 slots), NameMap (15 slots), Stream (9 slots)
- All vtable slots default to `genTrap` first, then specific methods get real stubs
- PDB vtable methods like `OpenDBI`, `OpenTpi`, `OpenGsi` return pointers to the other fake sub-objects
- Modified `PDB_Open3W`, `PDB_Open2W`, `PDB_OpenValidate5`, `PDBOpen2W_C`, `NameMap_open` to return fake objects
- Guest pointer writing uses `*(uint32_t*)ppPDB` (4 bytes, not 8) for 32-bit guest interop

### Why runtime codegen instead of .S file
- Wibo's `gen_trampolines.py` supports CDECL/STDCALL/FASTCALL but NOT __thiscall
- __thiscall stubs are pure x86 (no x86→x64 transition needed for simple returns)
- Runtime generation avoids modifying the build system or trampoline generator

## Current State: ALMOST WORKING

### What works
- Wibo builds cleanly (44 warnings, 0 errors)
- Linker accepts the fake PDB object from `PDB_Open3W`
- Linker processes ALL object files successfully
- Linker prints final message: `warning LNK4088: image being generated due to /FORCE option; image may not run`
- Output file has valid MZ/PE header with correct section layout

### What's broken
- **Exit code 139 (SIGSEGV)** — crash happens AFTER the linker finishes all work, during cleanup/exit
- **Output file is 2MB** instead of expected 19.5MB
  - The initial `MapViewOfFileEx` at `0x7ee00000` was for 2,097,152 bytes
  - Linker likely needs to unmap, extend file, remap at final size — but crashes before this completes
  - Or the mapping isn't being flushed (`msync`) before the crash kills the process
- The crash likely occurs when the linker calls vtable methods during PDB Close/Commit cleanup
- Both main thread and worker thread enter `Sleep(10)` then crash

### Key log evidence
```
# Fake vtables initialized successfully
mspdb::PDB_Open3W(mode=wf)
mspdb: code page at 0x73343000, objects: PDB=0x7039cb40 DBI=0x7039cb60 Mod=0x7039cb64 TPI=0x7039cb68 GSI=0x7039cb6c
mspdb: fake vtables initialized, code used 3378/16384 bytes

# Output file mapping (only 2MB)
MapViewOfFileEx(0x28, 0xf001f, 0, 0, 2097152, 0x7ee00000)

# Linker completed its work
warning LNK4088: image being generated due to /FORCE option; image may not run

# Then both threads sleep and crash (exit 139)
```

## Next Steps (for new session)

### 1. Fix the cleanup crash
The SIGSEGV during exit is likely from a vtable method being called that hits a `genTrap` (int3) stub. Options:
- Run under GDB to find exactly which vtable slot crashes: `gdb --args ./build/tools/wibo ./build/compilers/X360/16.00.11886.00/link.exe ...`
- Look at the trap stub's `mov al, <index>` to identify which vtable method
- Add proper stubs for the missing methods

### 2. Fix the 2MB output file
Even if cleanup crashes, the output should be full-sized. Possible causes:
- The linker may remap the output file at a larger size before writing PE data — if that remapping involves a PDB vtable call that crashes, data never gets written
- The output file mapping lifecycle needs more investigation: when does the linker extend it from 2MB to 19.5MB?
- `flushAllFileViews()` in wibo's `_exit(0)` path should msync all views — but SIGSEGV may bypass this

### 3. Possibly intercept the crash
- Install a SIGSEGV handler in wibo that calls `flushAllFileViews()` + `_exit(exitCode)` instead of crashing
- This would at least save whatever data is in memory-mapped files
- Or: make the linker call `PDBClose`/`PDBCommit` before crashing, by making those stubs actually work

## Test Commands
```bash
# Build wibo
cd /home/free/code/milohax/wibo/build && make -j$(nproc)
cp wibo /home/free/code/milohax/dc3-decomp/build/tools/wibo

# Run linker test
cd /home/free/code/milohax/dc3-decomp
rm -f build/373307D9/wibo_test.exe
WIBO_DEBUG=1 ./build/tools/wibo ./build/compilers/X360/16.00.11886.00/link.exe \
  /NOLOGO /MACHINE:PPCBE /SUBSYSTEM:XBOX /BASE:0x82000000 \
  /ENTRY:mainCRTStartup /NODEFAULTLIB /XEX:NO /FORCE \
  "/OUT:build/373307D9/wibo_test.exe" "@build/373307D9/link_test.rsp" \
  1>/dev/null 2>/tmp/claude/wibo_link.log
echo "EXIT: $?"

# Check output
ls -la build/373307D9/wibo_test.exe        # Should be ~19.5MB
xxd build/373307D9/wibo_test.exe | head -5  # Should show MZ header

# Debug with GDB
gdb --args ./build/tools/wibo ./build/compilers/X360/16.00.11886.00/link.exe \
  /NOLOGO /MACHINE:PPCBE /SUBSYSTEM:XBOX /BASE:0x82000000 \
  /ENTRY:mainCRTStartup /NODEFAULTLIB /XEX:NO /FORCE \
  "/OUT:build/373307D9/wibo_test.exe" "@build/373307D9/link_test.rsp"
```

## Key Files
| File | Description |
|------|-------------|
| `wibo/dll/mspdb.cpp` | Fake PDB vtable implementation (878 lines) |
| `wibo/dll/mspdb.h` | CDECL function declarations (unchanged) |
| `microsoft-pdb/langapi/include/pdb.h` | Reference PDB interface with all vtable layouts |
| `wibo/build/generated/mspdb_trampolines.S` | Auto-generated x86↔x64 CDECL thunks |
| `wibo/tools/gen_trampolines.py` | Trampoline generator (does NOT support __thiscall) |

## Success Criteria
1. Exit code 0 (no crash)
2. `wibo_test.exe` has actual PE data (~19.5MB)
3. File content matches wine-produced `default.exe`
