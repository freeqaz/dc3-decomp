# Xenia Headless Mode - Implementation Status

## Summary

A `xenia-headless` binary has been created that runs Xenia without a graphical window, using null/nop backends for GPU/APU/HID.

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
- Binary compiles and links
- Vulkan initialization succeeds
- Emulator initializes with headless backends (GPU: null, APU: nop, HID: nop)
- Emulator thread starts and attempts to launch game
- Boot reporting callbacks are set up

### Not Working
- Game boot hangs - `on_launch` callback never fires
- Title loading appears to hang indefinitely

### Architectural Limitations
Due to Xenia's architecture, the headless binary still links:
- `libgtk-3.so.0` - Required by WindowedAppContext
- `libvulkan.so.1` - Required by null graphics backend
- `libX11.so.6` - Required by Vulkan provider
- `imgui` - Required by kernel UI callbacks

## Known Issues

1. **Boot Hanging**: The `LaunchPath` method appears to hang when loading DC3. This could be due to:
   - Missing filesystem dependencies
   - Vulkan operations blocking
   - Threading issues

2. **Dependencies**: The null GPU backend still creates a VulkanProvider, which brings in display dependencies.

## Next Steps

1. **Debug Boot Issue**: Add more logging to identify where boot hangs
2. **Test with ISO**: Try booting from an ISO file instead of loose XEX
3. **Compare with Windowed**: Run windowed Xenia with same file to compare behavior
4. **Minimal Null Backend**: Create a truly headless GPU backend that doesn't use Vulkan

## Testing Commands

```bash
# Build
cd /tmp/claude/xenia
make -C build -j8 xenia-headless

# Test (with timeout)
./build/bin/Linux/Checked/xenia-headless \
    --storage_root=/tmp/claude/xenia/scratch \
    --target=/home/free/code/milohax/dc3-decomp/orig/373307D9/default.xex \
    --headless_timeout_ms=30000
```

## Success Criteria (Partially Met)
- [x] `xenia-headless` binary builds
- [x] No display required (uses null backends)
- [ ] Game boots successfully
- [ ] Console output shows boot status/crash PC
