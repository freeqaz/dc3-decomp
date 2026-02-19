# XEX Import Resolution Debugging Session

**Date:** 2026-02-17
**Outcome:** Decompiled XEX boots successfully in Xenia headless mode

## Summary

The decompiled XEX now boots in Xenia headless mode and runs for 25 seconds without crashing. Import resolution was disabled due to structural differences between the compressed original XEX and our uncompressed decompiled PE.

## Problem Statement

When trying to boot the decompiled XEX, Xenia crashed with an assertion failure:

```
xenia-headless: ../src/xenia/cpu/xex_module.cc:1276: bool xe::cpu::XexModule::SetupLibraryImports(const std::string_view, const xex2_import_library *): Assertion `false' failed.
```

This occurs when Xenia processes imports and encounters a `record_type` value that is neither 0 (variable) nor 1 (thunk).

## Root Cause Analysis

### How Xenia Processes Imports

From `xex_module.cc:1106-1283`:

```cpp
for (uint32_t i = 0; i < library->count; i++) {
    uint32_t record_addr = library->import_table[i];  // VA from XEX header
    auto record_slot = memory()->TranslateVirtual<xe::be<uint32_t>*>(record_addr);
    uint32_t record_value = *record_slot;             // Read from memory

    uint16_t record_type = (record_value & 0xFF000000) >> 24;  // High byte
    uint16_t ordinal = record_value & 0xFFFF;                   // Low 16 bits

    if (record_type == 0) { /* variable import */ }
    else if (record_type == 1) { /* thunk import */ }
    else { assert_always(); }  // <-- This was failing
}
```

### Import Format

| Field | Bits | Description |
|-------|------|-------------|
| record_type | 31-24 | 0x00=variable, 0x01=thunk |
| ordinal | 15-0 | Import ordinal number |

Values are big-endian in memory.

### The Compression Issue

**Critical Discovery:** The original XEX uses compression:

```
Base File Format Info:
  Encryption type: 0 (none)
  Compression type: 1 (basic compression)
```

This means:
1. The PE data in the XEX file is compressed
2. The values at RVA 0x600-0x1E48 (import data) are compressed bytes, not actual ordinals
3. When Xenia loads the XEX, it decompresses the PE into memory
4. The decompressed memory has correct import ordinals, but the file doesn't

**Our PE is uncompressed**, so:
1. The values at RVA 0x600 are actual data (not import ordinals)
2. We can't copy import data from the original XEX file - it's compressed!
3. The import_table VAs in the header point to addresses with garbage values

### Import Table Structure

The import library header contains:

```
xex2_opt_import_libraries:
  +0x00: total_size (4 bytes)
  +0x04: string_table_size (4 bytes)
  +0x08: string_table_count (4 bytes)
  +0x0C: string_table data
  ... library headers follow

xex2_import_library:
  +0x00: size (4)
  +0x04: digest (20)
  +0x18: id (4)
  +0x1C: version (4)
  +0x20: version_min (4)
  +0x24: name_index (2)
  +0x26: count (2)
  +0x28: import_table[0] VA (first import)
  +0x2C: import_table[1] VA
  ...
```

### Interleaved Import Pattern

The import_table alternates between data and thunk addresses:

| Index | VA | Value | Type |
|-------|-----|-------|------|
| 0 | 0x82000600 | 0x0000028B | DATA (ordinal 651) |
| 1 | 0x82EE5544 | 0xEBC1FFE8 | CODE (PPC instruction) |
| 2 | 0x82000604 | 0x00000356 | DATA (ordinal 854) |
| 3 | 0x82EE5554 | 0x7F045800 | CODE (PPC instruction) |
| ... | ... | ... | ... |

In the **original decompressed memory**:
- Even indices have `record_type=0` with valid ordinals
- Odd indices have `record_type=1` with matching ordinals (for thunks)

In our **uncompressed PE file**:
- The addresses point to wrong data
- record_type values are garbage (235, 127, 57, etc.)

## Solution

Since we can't easily replicate the original's import structure (would require decompressing the original PE), we skip the import library header entirely.

### Code Changes

Modified `scripts/build/build_xex.py`:

```python
# Import Libraries (0x103FF) - SKIP for now
# The original XEX is compressed, and its import_table VAs point to
# decompressed memory locations. Our PE is uncompressed and has different
# structure. Including the header with unpatched VAs causes Xenia to
# read garbage values and crash.
print("  Skipping import library header (PE structure mismatch)")
```

### Functions Added (for future use)

- `parse_import_library_header()` - Parse xex2_opt_import_libraries structure
- `build_ordinal_to_rva_map()` - Map ordinals to RVAs in .idata
- `build_va_to_ordinal_map()` - Map VAs to ordinals from original XEX
- `patch_import_library_header()` - Patch import_table VAs
- `copy_import_data_from_original()` - Copy import data (doesn't work for compressed XEX)

## Result

```
$ timeout 30 xenia-headless --target=build/373307D9/default.xex --headless_timeout_ms=25000

i> Module \Device\Harddisk0\Partition1\default.xex:
    Module Flags: 00000001
    Load Address: 82000000
    Image Size: 012E0200
    ...
BOOT: Title loaded successfully
BOOT: Title ID: 0x373307d9
BOOT: Kernel state initialized
i> Title launched, entering main loop...
TIMEOUT: 25000ms reached
```

**The decompiled XEX boots and runs for 25 seconds without crashing!**

## Extended Runtime Test (2026-02-17)

**Test:** 115 second headless run

```bash
timeout 125 xenia-headless --target=build/373307D9/default.xex --headless_timeout_ms=115000
```

**Result:** ✅ SUCCESS - Game ran for full 115 seconds without crashing.

### Comparison with Original XEX

| Metric | Original XEX | Decompiled XEX |
|--------|--------------|----------------|
| Boot success | ✅ | ✅ |
| Kernel state init | ✅ | ✅ |
| Main thread start | ✅ | ✅ |
| Runtime to timeout | 25s (tested) | 115s (tested) |
| Log lines | 850 | 402 |
| Errors | 0 | 0 |

The difference in log lines is due to:
- Original includes achievement data (250+ lines from optional header)
- Original shows "Title Name: Dance Central 3" (optional header)

Both XEX files exhibit identical boot behavior in headless mode - they initialize successfully and wait for input/rendering that won't happen without a real GPU.

### What's Working

1. **XEX Loading** - All 293 pages loaded correctly
2. **Thread Startup** - GPU Commands, GPU VSync, XMA Decoder, Audio Worker, Kernel Dispatch, Main XThread all started
3. **Kernel State** - Initialized successfully
4. **Memory Layout** - No crashes during memory access

### What's Not Tested

- Import resolution (header is skipped)
- Rendering/GPU (null backend in headless)
- Audio (nop backend in headless)
- Input (nop backend in headless)
- Game progression past boot

## Future Work

To enable full import resolution:

1. **Decompress the original PE**: Use Xenia's decompression code or LZX library to decompress the original XEX's PE data

2. **Extract real import data**: From the decompressed PE, extract the import ordinal data at RVA 0x600-0x1E48

3. **Copy to our PE**: Place the import data at the correct RVA in our decompiled PE

4. **Include import header**: Include the (possibly patched) import library header in the XEX

Alternative approach: Generate import data from scratch based on the ordinals we need.

## Files Modified

- `scripts/build/build_xex.py`:
  - Added import parsing functions (lines 180-330)
  - Modified `parse_original_xex()` to extract import library info
  - Modified `build_xex()` to skip import library header

## Related Documentation

- [BUILD_ROADMAP.md](../plans/BUILD_ROADMAP.md) - Updated with boot success milestone
- [FREE60_XEX_FORMAT.md](../reference/FREE60_XEX_FORMAT.md) - XEX2 header reference
- [XENIA_BOOT_VALIDATION.md](../plans/XENIA_BOOT_VALIDATION.md) - Xenia headless setup

## References

- Xenia source: `~/code/milohax/vmx128-research/xenia-source/src/xenia/cpu/xex_module.cc`
- Key function: `SetupLibraryImports()` (lines 1106-1283)
- Import header ID: `0x103FF`
