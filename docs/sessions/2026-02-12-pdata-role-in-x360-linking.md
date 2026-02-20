# Understanding .pdata's Role in X360 Linking

**Date**: 2026-02-12
**Status**: Research complete

## 1. What is .pdata?

### Format

On Xbox 360 (and Windows CE PowerPC), `.pdata` contains an array of `IMAGE_CE_RUNTIME_FUNCTION_ENTRY` structures. Each entry is **8 bytes** (big-endian on X360):

```c
typedef struct _IMAGE_CE_RUNTIME_FUNCTION_ENTRY {
    uint32_t FuncStart;           // Address of function's first instruction
    uint32_t PrologLen    : 8;    // Prologue length in instructions
    uint32_t FuncLen      : 22;   // Total function length in instructions
    uint32_t ThirtyTwoBit : 1;    // 1 = 32-bit instructions (always 1 on PPC)
    uint32_t ExceptionFlag: 1;    // 1 = PDATA_EH precedes function in .text
} IMAGE_CE_RUNTIME_FUNCTION_ENTRY;  // 8 bytes total
```

Source: [MS docs for _IMAGE_CE_RUNTIME_FUNCTION_ENTRY](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms879748(v=msdn.10)), confirmed by `XenonRecomp/XenonUtils/xbox.h:143-160`.

For Xbox 360 PowerPC (32-bit instructions, ThirtyTwoBit=1):
- Function size in bytes = `FuncLen * 4`
- Prologue size in bytes = `PrologLen * 4`

### Exception Handler Data (PDATA_EH)

When `ExceptionFlag == 1` (or `FuncLen == 0`), an 8-byte `PDATA_EH` structure is embedded in `.text` **immediately before** the function:

```c
struct PDATA_EH {
    uint32_t* pHandler;      // Address of exception handler function
    uint32_t* pHandlerData;  // Address of exception handler data record
};
```

Source: [MS docs for PDATA_EH](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms864326(v=msdn.10))

For C++ exception handling, `pHandler` points to `__CxxFrameHandler` (at `0x8299E5E0` in ham_xbox_r.exe) and `pHandlerData` points to the `__ehfuncinfo` record for that function.

This is confirmed by the jeff dtk source (`src/util/xex.rs:1036-1093`) which reads these 8 bytes when `func_type == 3` (ThirtyTwoBit=1, ExceptionFlag=1), and by the unicorn runner docs noting "~95% of ADDR32 relocations in `.text` are C++ exception handling headers at offset 0-7 of COMDAT sections."

### Size in DC3

| Section | Size | Entries (approx) | Source |
|---------|------|-------------------|--------|
| `.pdata` (original PE) | 469 KB | ~60,000 functions | Original ham_xbox_r.exe |
| `.pdata` (linked PE) | 14 KB | ~1,800 functions | From decomp-compiled objects only |
| `.pdat0` (linked PE) | 453 KB | ~58,000 functions | From dtk split objects (renamed) |

## 2. How the X360 Kernel Uses .pdata

### Kernel Exports

The Xbox 360 kernel (xboxkrnl.exe) exports three functions for .pdata-based exception handling:

| Export | Ordinal | Purpose |
|--------|---------|---------|
| `RtlLookupFunctionEntry` | 0x131 | Binary-search .pdata to find the RUNTIME_FUNCTION for a given PC |
| `RtlUnwind` | 0x147 | Walk the stack using .pdata, calling exception handlers |
| `RtlUnwind2` | 0x148 | Extended version of RtlUnwind |

Source: `XenonRecomp/XenonUtils/xbox/xboxkrnl_table.inc:319,341-342`

### Exception Dispatch Flow

When an exception occurs (C++ `throw`, hardware fault, SEH `RaiseException`):

1. **Kernel captures context** (registers, PC)
2. **`RtlLookupFunctionEntry(PC)`** binary-searches the sorted `.pdata` array to find the `IMAGE_CE_RUNTIME_FUNCTION_ENTRY` containing the faulting PC
3. If `ExceptionFlag == 1`, reads the `PDATA_EH` from `.text` (8 bytes before the function) to get the exception handler address
4. **`RtlUnwind`** walks the stack frame-by-frame:
   - Uses `PrologLen` and `FuncLen` to determine if the PC is in the prologue or body
   - Knows the frame size from the prologue instructions (save/restore patterns)
   - Calls each frame's exception handler (`pHandler`) to find a catch block
   - Continues unwinding if no handler accepts the exception

### Beyond Exception Handling

.pdata serves multiple purposes:

| Use Case | How .pdata is Used |
|----------|-------------------|
| **C++ exception handling** | `throw`/`catch` — the primary use case |
| **SEH (`__try`/`__except`)** | Same mechanism via `RtlUnwind` |
| **Debugger stack walks** | Debugger uses .pdata to walk the call stack without frame pointers |
| **Crash reporting** | Xbox Watson crash dumps use .pdata to generate stack traces |
| **`setjmp`/`longjmp`** | `longjmp` calls `RtlUnwind` internally (confirmed by XenonRecomp README) |

## 3. Does DC3 Use C++ Exceptions?

### Yes, but sparingly

| Location | Mechanism | Usage |
|----------|-----------|-------|
| `src/system/midi/MidiReader.cpp:454-455` | `MILO_TRY`/`MILO_CATCH` | Parse error recovery in MIDI reading |
| `src/system/midi/MidiParserMgr.cpp:162-163` | `MILO_TRY`/`MILO_CATCH` | Parse error recovery in MIDI parsing |
| `src/system/synth_xbox/soundtouch/.../main.cpp` | `try`/`catch` | SoundStretch error handling |
| `src/system/synth_xbox/soundtouch/.../RunParameters.cpp` | `try`/`catch` | Parameter parsing |
| `src/system/stlport/stl/_istream.c` | `try`/`catch` | STL stream operations |
| `src/macros.h:10-11` | `SEH_TRY`/`SEH_EXCEPT` macros | Defines `__try`/`__except` wrappers |

The `MILO_TRY`/`MILO_CATCH` macros (`src/system/os/Debug.h:116-126`) expand to standard C++ `try`/`catch(const char*)` with a debug flag toggle. These are used for non-critical error recovery, not hot paths.

### Implicit EH Infrastructure

Even without explicit `try`/`catch`, the MSVC compiler generates exception handling metadata for:
- Functions with local objects that have destructors (for stack unwinding during exceptions thrown by called functions)
- Functions with `MILO_ASSERT` that may call `Fail()` which could throw

The binary has `__CxxFrameHandler` at `0x8299E5E0`, and ~95% of ADDR32 relocations in `.text` COMDAT sections are C++ EH headers — indicating widespread implicit EH metadata even for functions without explicit try/catch.

## 4. Impact of the .pdat0 Workaround

### Current State

dtk renames duplicate `.pdata` sections to `.pdat0` to bypass MSVC linker validation. This means:

- **14 KB** of .pdata from decomp-compiled objects → proper `.pdata` section → kernel-visible
- **453 KB** of .pdata from split objects → `.pdat0` section → **invisible to kernel**

The kernel's `RtlLookupFunctionEntry` only searches the PE's Exception Directory, which points to `.pdata`. The `.pdat0` section has no directory entry — the kernel doesn't know it exists.

### What Breaks

| Scenario | Impact | Severity |
|----------|--------|----------|
| C++ exception thrown in a split-object function | `RtlLookupFunctionEntry` returns NULL → no handler found → unhandled exception → **crash** | **Critical** if exceptions are used |
| Debugger stack walk through split-object function | Stack walk stops at that frame → truncated call stack | Medium |
| Crash dump generation | Incomplete stack trace in Watson dump | Low |
| `longjmp` through split-object frames | `RtlUnwind` can't unwind → **crash or corruption** | **Critical** if setjmp/longjmp used |
| Normal execution (no exceptions) | **No impact** — .pdata is only consulted during exception dispatch | None |

### Does the Kernel Crash on Missing .pdata?

No. The kernel returns NULL from `RtlLookupFunctionEntry` if no entry is found. The exception dispatch logic then treats the function as a leaf function (no frame to unwind). If the function actually has a frame, unwinding produces garbage — leading to eventual crash or undefined behavior, but not an immediate kernel panic.

### Practical Impact for DC3

The game is a **debug build** with `MILO_TRY`/`MILO_CATCH` in MIDI parsing paths. If a MIDI parse error occurs during gameplay and the exception must unwind through functions only covered by `.pdat0`, the game would crash instead of recovering gracefully.

However, for basic **runtime testing** (loading the game, playing through normal paths without triggering error recovery), the missing .pdata is unlikely to cause issues since exceptions are only used for error recovery, not normal control flow.

## 5. What Would It Take to Get Correct .pdata?

### Option A: Fix dtk to merge .pdata sections properly (Best)

**Status**: Already implemented in jeff fork.

The jeff fork now merges same-named sections in `split_obj()` instead of creating duplicates. This means `.pdata` fragments for the same translation unit are concatenated into a single `.pdata` section per output object. The MSVC linker then merges all per-object `.pdata` sections into the final PE's `.pdata` section normally.

Relevant test cases in jeff (`src/util/split.rs:1495-2040`):
- `test_split_obj_merges_duplicate_sections` — verifies pdata merge
- `test_split_obj_no_false_merge_across_units` — verifies no cross-unit merge
- `test_split_obj_merge_with_alignment_padding` — verifies alignment handling

### Option B: Post-link PE patching

Rename `.pdat0` → `.pdata` in the output PE and update the Exception Directory to point to the merged section. Issues:
- Must merge `.pdata` + `.pdat0` data into a single sorted array
- Must re-sort by `FuncStart` address (required by binary search in `RtlLookupFunctionEntry`)
- `FuncStart` addresses in .pdata entries from split objects need relocation adjustment if there's a VA shift
- The entries contain absolute addresses that were fixed up by the linker, so they should already be correct for the linked image's layout

This is feasible as a `scripts/fix_pdata_pe.py` post-processing step:
1. Find `.pdata` and `.pdat0` sections in the linked PE
2. Read all 8-byte entries from both
3. Merge and sort by `FuncStart`
4. Write merged data into a single section
5. Update the PE's Exception Directory (data directory entry 3) to point to the merged section

### Option C: Accept the workaround for now

For proof-of-concept linking and `.text` comparison, `.pdat0` is fine. The linked PE's `.text` content is the same regardless of .pdata correctness. Exception handling correctness only matters for runtime testing on hardware/Xenia.

## 6. How Other X360 Decomps Handle This

### RB3 Decomp

RB3 targets **Wii** (GameCube/PowerPC EABI), not Xbox 360. It uses DOL executables, not PE/XEX. There is **no .pdata handling** in the RB3 decomp — the concept doesn't exist on that platform. Exception handling on Wii uses the PowerPC EABI `__init_cpp_exceptions` mechanism, not Windows-style .pdata.

### XenonRecomp

[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) (Xbox 360 game recompiler) parses `.pdata` for function boundary discovery:

```cpp
auto& pdata = *image.Find(".pdata");
size_t count = pdata.size / sizeof(IMAGE_CE_RUNTIME_FUNCTION);
auto* pf = (IMAGE_CE_RUNTIME_FUNCTION*)pdata.data;
```

For functions not in `.pdata` (leaf functions without stack frames), it falls back to branch-link instruction scanning. The README notes: "Functions with stack space have their boundaries defined in the `.pdata` segment of the XEX."

XenonRecomp does **not support exceptions** in recompiled code — it redirects `setjmp`/`longjmp` to native implementations and notes that exception handlers "can jump to arbitrary code locations" which the recompiler can't handle.

## 7. Recommendations

### Short Term (Current)
**Accept the .pdat0 workaround.** For `.text` comparison and progress tracking, .pdata correctness doesn't matter. The linked PE's code content is identical regardless of .pdata state.

### Medium Term (Runtime Testing)
**Write `scripts/fix_pdata_pe.py`** to post-process the linked PE:
- Merge `.pdata` + `.pdat0` into a single sorted `.pdata`
- Update the Exception Directory
- This enables basic runtime testing on Xenia without risking crashes from missing EH metadata

### Long Term (Correct Link)
**Use jeff fork's section merging.** The upstream fix properly merges duplicate .pdata sections during COFF splitting, producing objects that the MSVC linker handles correctly. Once all jeff fork changes are integrated, the `.pdat0` workaround becomes unnecessary.

## 8. Root Cause of LNK1223 in Split Objects (2026-02-19)

### Why jeff's .pdata Triggers LNK1223

The MSVC linker (for RISC platforms including PowerPC) validates `.pdata` contributions from each object file. LNK1223 fires when `.pdata` entries are **unsorted** by `BeginAddress` or contain **invalid entries**.

Jeff's `write_coff()` produces `.pdata` sections with two problems:

#### Problem 1: `__unwind$` Functions as .pdata Entries

Jeff treats `__unwind$` functions like normal functions and generates `.pdata` entries for them. But `__unwind$` functions are **exception handler thunks** — they don't need their own `.pdata` entries. Their exception handling context is provided by the parent function's `PDATA_EH` blob.

When `.pdata` contains entries for both normal functions and `__unwind$` functions interleaved, the linker's sort validation can fail because:
- Each object's `.pdata` entries reference symbols via relocations (`func_rva=0` + reloc)
- After relocation, `__unwind$` entries may not maintain sorted order relative to the normal function entries
- LNK1223 fires: "invalid or unsorted .pdata contributions"

#### Problem 2: Missing ADDR32 Relocations on PDATA_EH

The `except_data_*` symbols in `.text` contain the PDATA_EH blobs (8 bytes before functions with `ExceptionFlag=1`). These blobs contain **absolute virtual addresses** baked in from the original binary:

```
except_data_82DF7860 at .text+0x58:
  pHandler    = 0x8299E5E0  (__CxxFrameHandler)
  pHandlerData = 0x82252EE8  (FuncInfo in .rdata)
```

But there are **no ADDR32 relocations** at these offsets. The addresses only work if the final PE has the same VA layout as the original. In our hybrid link, these addresses will be wrong because:
- `__CxxFrameHandler` may be at a different address in our linked PE
- The `.rdata` FuncInfo structs are at shifted addresses

### Verified Data (FitnessFilter.obj)

All 7 `except_data_*` symbols in this split object share:
- `pHandler = 0x8299E5E0` = `__CxxFrameHandler` (confirmed via `symbols.txt`)
- `pHandlerData = 0x8225xxxx` range = `.rdata` section (before `.text` at `0x82330000`)
- Zero relocations at the PDATA_EH offsets
- Each sits exactly 8 bytes before its associated function
- Preceding bytes show `blr` (0x4E800020) or nop padding (0x00000000)

### Scope of the Problem (Across All Split Objects)

A scan of all 2,223 split objects found **221 objects** with `except_data_*` symbols where the associated function either has NO `.pdata` entry or the entry's ExceptionFlag is not set. This means the PDATA_EH data exists correctly in `.text` but `.pdata` doesn't reference it. The problem is **bilateral**:

| Issue | Count | Description |
|-------|-------|-------------|
| `__unwind$` in `.pdata` | All objects with `__unwind$` | Jeff creates `.pdata` entries for EH thunks (shouldn't exist in `.pdata`) |
| Missing `.pdata` for EH functions | 221 objects | Functions with `except_data_*` / PDATA_EH blobs lack corresponding `.pdata` entries with ExceptionFlag=1 |

Objects span every subsystem: XDK/LIBCMT, gesture, hamobj, rndobj, synth, ui, meta_ham, net_ham, etc. This is a systemic issue in how jeff emits `.pdata` during `write_coff()`, not limited to specific compilation units.

### The Fix Path

To generate correct `.pdata` in jeff:

1. **Filter out `__unwind$` entries from .pdata**: These are exception handler thunks that should not have their own .pdata entries. Only emit `.pdata` entries for actual functions.

2. **Generate .pdata entries for functions with `except_data_*`**: When a function has a PDATA_EH blob at offset-8, generate a proper `IMAGE_CE_RUNTIME_FUNCTION_ENTRY` with `ExceptionFlag=1`. Jeff already knows about these (it reads them in `xex.rs:1036-1093`).

3. **Add ADDR32 relocations to PDATA_EH blobs**: Each `except_data_*` symbol needs two ADDR32 relocations:
   - Offset+0: reloc to `__CxxFrameHandler` symbol
   - Offset+4: reloc to the appropriate FuncInfo symbol (or section+offset)

4. **Ensure .pdata entry endianness**: The packed word (PrologLen, FuncLen, ThirtyTwoBit, ExceptionFlag) must be in the correct endianness for the target platform (big-endian for PPC).

5. **Validate .pdata sorting**: Entries within each split object's `.pdata` must be sorted by `BeginAddress` (ascending).

### References

- [IMAGE_CE_RUNTIME_FUNCTION_ENTRY (MS WinCE docs)](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms879748(v=msdn.10))
- [PDATA_EH (MS WinCE docs)](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms864326(v=msdn.10))
- [LNK1223 documentation](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk1223?view=msvc-170)
- `XenonRecomp/XenonUtils/xbox.h:143-160` — `IMAGE_CE_RUNTIME_FUNCTION` struct definition
- `xenia/src/xenia/cpu/xex_module.cc:826` — Confirms EXCEPTION directory = `IMAGE_CE_RUNTIME_FUNCTION_ENTRY[]`

## Key Files Referenced

| File | Relevance |
|------|-----------|
| `XenonRecomp/XenonUtils/xbox.h:143-160` | `IMAGE_CE_RUNTIME_FUNCTION` struct definition |
| `XenonRecomp/XenonUtils/xbox/xboxkrnl_table.inc:319,341-342` | Kernel exports for EH |
| `XenonRecomp/XenonRecomp/recompiler.cpp:177-194` | .pdata parsing for function discovery |
| `jeff/src/util/xex.rs:995-1095` | dtk .pdata parsing with EH data extraction |
| `jeff/src/util/split.rs:838-891` | `split_pdata()` — maps pdata entries to code splits |
| `jeff/src/util/split.rs:1190-1208` | Section merge logic (the fix) |
| `dc3-decomp/src/macros.h:10-11` | `SEH_TRY`/`SEH_EXCEPT` macros |
| `dc3-decomp/src/system/os/Debug.h:116-126` | `MILO_TRY`/`MILO_CATCH` macros |
| `dc3-decomp/src/system/midi/MidiReader.cpp:454-455` | Exception usage in MIDI parsing |
| `dc3-decomp/docs/sessions/2026-02-11-dtk-pdata-splitting-bug.md` | Original bug analysis |
| `dc3-decomp/docs/sessions/2026-02-11-x360-linking-pipeline.md` | Current pipeline state |

## Sources

- [IMAGE_CE_RUNTIME_FUNCTION_ENTRY (MS docs)](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms879748(v=msdn.10))
- [PDATA_EH (MS docs)](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms864326(v=msdn.10))
- [PE Format specification (MS docs)](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [x64 exception handling (MS docs)](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64?view=msvc-170) — analogous mechanism for x64
- [Use of Windows Exception Handling Metadata (Leviathan Security)](https://www.leviathansecurity.com/blog/use-of-windows-exception-handling-metadata)
- [XenonRecomp (GitHub)](https://github.com/hedge-dev/XenonRecomp) — X360 recompiler using .pdata
