# Xenia Headless Mode - Implementation Status

## Summary

**Status: ✅ WORKING** - The decompiled XEX boots successfully in Xenia headless mode and runs for 2+ minutes without crashes.

A `xenia-headless` binary runs Xenia without a graphical window, using null/nop backends for GPU/APU/HID. This enables automated testing of the decompiled binary without requiring a display.

## Build Location
```
/tmp/claude/xenia/build/bin/Linux/Checked/xenia-headless
```

## Files Created
- `src/xenia/app/xenia_headless_main.cc` - Main entry point
- `src/xenia/app/emulator_headless.h` - Headless wrapper header
- `src/xenia/app/emulator_headless.cc` - Headless wrapper implementation
- Modified `src/xenia/app/premake5.lua` - Added headless build target

## Usage
```bash
./xenia-headless \
    --storage_root=/tmp/storage \
    --target=/path/to/game.xex \
    --headless_timeout_ms=30000
```

## Command-Line Options
| Option | Description |
|--------|-------------|
| `--storage_root` | Path for Xenia config and data |
| `--content_root` | Path for guest content (saves) |
| `--cache_root` | Path for cache files |
| `--target` | Path to .xex or .iso file |
| `--headless_timeout_ms` | Timeout in ms (0 = indefinite) |
| `--headless_report_boot` | Report boot status to console |

## Current Status

### Working
- Binary compiles and links (145MB, no GTK+/Vulkan/X11 dependencies)
- Emulator initializes with headless backends (GPU: null, APU: nop, HID: nop)
- XEX module loads with all 293 pages mapped
- 6 threads start: GPU Commands, GPU VSync, XMA Decoder, Audio Worker, Kernel Dispatch, Main XThread
- Title loads, kernel state initializes
- **Runs for 2+ minutes without crashes or errors**
- PE structure is correct, entry point works, memory layout is valid

### Not Tested
- **Rendering** - Headless mode uses null GPU backend
- **Audio** - Headless mode uses nop audio backend
- **Input** - Headless mode uses nop HID backend
- **Import Resolution** - Partially implemented (data embedded, header skipped - see below)
- **Game Logic** - Game waits for input that never comes

### Import Resolution Status

The original XEX uses **basic compression** (type 1), which has been fully analyzed and **import resolution is now fully working**:

**What's Implemented:**
- ✅ Decompression code added to `scripts/build_xex.py` via `decompress_xex_pe()`
- ✅ Import ordinal data (RVA 0x600-0x1E48, 6216 bytes) copied from original to decompiled PE
- ✅ Import thunks in `.idata` section patched to XEX format (0x00XXXXXX)
- ✅ **Thunk section generation** - Creates `.ithunk` section with 347 thunk markers (0x01XXXXXX)
- ✅ **Import library header patching** - Updates import_table VAs to point to new thunk section
- ✅ **Full import resolution** - All 707 imports (360 variables + 347 thunks) resolve correctly

**Verification:**
```bash
# Build XEX with import resolution
python3 scripts/build_xex.py

# Test with Xenia
xenia-headless --target=build/373307D9/default.xex --headless_timeout_ms=25000

# Output shows successful import resolution:
# - d3d9: 318 imports
# - xboxkrnl: 379 imports
# - xbdm: 10 imports
# - BOOT: Title loaded successfully
```

**Implementation Details:**
The thunk section fix (lines 475-610 in `build_xex.py`):
1. Decompresses original XEX to read thunk markers from RVA 0xEE5xxx
2. Generates 347 thunk markers (0x01XXXXXX with ordinals) in new section at RVA 0x140C000
3. Patches import_table VAs to point to new thunk locations
4. Extends PE SizeOfImage to include thunk section

## Architectural Details

Due to Xenia's architecture, the headless binary still links some display-related libraries:
- `libvulkan.so.1` - Required by null graphics backend (for Vulkan provider)
- `libX11.so.6` - Required by Vulkan provider

However, no actual display is required - the null GPU backend doesn't create windows.

## Success Criteria

- [x] `xenia-headless` binary builds
- [x] No display required (uses null backends)
- [x] Game boots successfully
- [x] Console output shows boot status
- [x] No crashes during boot
- [x] Import data embedded in PE
- [x] Import library header with thunk section
- [x] Full import resolution (707 imports from d3d9, xboxkrnl, xbdm)
- [ ] Rendering (requires full Xenia with Vulkan)
- [ ] Gameplay (requires input)

## Testing Commands

```bash
# Build XEX
python3 scripts/build_xex.py

# Test decompiled XEX (30 second timeout)
/tmp/claude/xenia/build/bin/Linux/Checked/xenia-headless \
    --storage_root=/tmp/claude/xenia/scratch \
    --target=/home/free/code/milohax/dc3-decomp/build/373307D9/default.xex \
    --headless_timeout_ms=30000
```

## Related Files

- `scripts/build_xex.py` - XEX packer with decompression support
- `scripts/decompress_xex.py` - Standalone XEX decompression tool
- `docs/sessions/2026-02-17-xex-import-resolution.md` - Import debugging session

## Future Work

### ✅ COMPLETED: Full Import Resolution (Thunk Marker Fix)

**Status:** Implemented and verified working (2026-02-17)

The thunk marker implementation is complete:
1. ✅ Created `.ithunk` section at RVA 0x140C000 for 347 thunk markers (16 bytes each)
2. ✅ Generated `0x01XXXXXX` values with correct ordinals from decompressed original PE
3. ✅ Patched import_table VAs to point to new thunk section
4. ✅ Included patched import library header in XEX
5. ✅ Verified: All 707 imports resolve successfully (d3d9: 318, xboxkrnl: 379, xbdm: 10)

**Key Fixes:**
- Fixed thunk detection threshold from 0x1000000 (16MB) to 0x100000 (1MB)
- Added `orig_pe_data` parameter to `build_xex()` to enable thunk generation
- Initialized `orig_pe_data = None` before try block to handle decompression failures

### Next Steps

3. **Test with Full Xenia** (with rendering)
   - Build full Xenia with Vulkan/GPU support
   - Test visual rendering and game progression beyond boot
