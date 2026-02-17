# Xenia Headless Mode - Implementation Complete

**Date:** 2026-02-17
**Status:** ✅ Build Complete, ✅ Boot Testing Complete

## Goal

Create a Xenia headless binary that can boot DC3 without requiring:
- GTK+ (`libgtk-3.so.0`)
- Vulkan (`libvulkan.so.1`)
- X11 (`libX11.so.6`)
- ImGui

This enables automated boot testing in CI/CD environments without display hardware.

## Results

| Metric | Before | After |
|--------|--------|-------|
| Binary size | 162MB | 145MB |
| GTK+ dependency | ✗ Yes | ✓ None |
| Vulkan dependency | ✗ Yes | ✓ None |
| X11 dependency | ✗ Yes | ✓ None |

### Boot Test Output (DC1)

```
i> Xenia Headless Mode - Starting
i> Emulator initialized with headless backends
i>   GPU: null
i>   APU: nop
i>   HID: nop
i> Launching: /path/to/default.xex
i> Launching module game:\default.xex
BOOT: Title loaded successfully
BOOT: Title ID: 0x545607d3
BOOT: Title Name: Dance Central
BOOT: Kernel state initialized
i> Title launched, entering main loop...
```

## Implementation

### Headless Library Variants

Created `-headless` variants of libraries compiled with `XE_HEADLESS_BUILD` define:

| Library | Headless Variant | Changes |
|---------|------------------|---------|
| xenia-kernel | xenia-kernel-headless | No ImGui dialogs |
| xenia-gpu | xenia-gpu-headless | No presenter/Vulkan |
| xenia-gpu-null | xenia-gpu-null-headless | No VulkanProvider |
| xenia-core | xenia-core-headless | No window/icon code |

### Files Modified

**Premake5 Build Files:**
- `src/xenia/kernel/premake5.lua` - Added xenia-kernel-headless
- `src/xenia/gpu/premake5.lua` - Added xenia-gpu-headless (excludes SPIRV files)
- `src/xenia/gpu/null/premake5.lua` - Added xenia-gpu-null-headless
- `src/xenia/premake5.lua` - Added xenia-core-headless
- `src/xenia/app/premake5.lua` - Updated xenia-headless to link headless variants

**Source Files with XE_HEADLESS_BUILD Guards:**
- `src/xenia/kernel/xam/xam_ui.cc` - Guarded ImGui dialog code
- `src/xenia/kernel/xam/xam_nui.cc` - Guarded ImGui dialog code
- `src/xenia/gpu/graphics_system.cc` - Guarded presenter/UI code
- `src/xenia/gpu/null/null_graphics_system.cc` - Guarded Vulkan provider creation
- `src/xenia/emulator.cc` - Guarded window icon and crash dialog code

## Build Instructions

```bash
cd /tmp/claude/xenia

# Regenerate build files
python3 xenia-build premake

# Build headless binary
make -C build -j8 xenia-headless

# Verify no UI dependencies
ldd build/bin/Linux/Checked/xenia-headless | grep -E "gtk|vulkan|X11|xcb"
# (should return nothing)
```

## Usage

```bash
# Basic usage
/tmp/claude/xenia/build/bin/Linux/Checked/xenia-headless \
  --target=/path/to/game.xex \
  --headless_timeout_ms=30000

# Options:
#   --target=<path>           XEX or ISO file to launch (required)
#   --headless_timeout_ms=N   Exit after N milliseconds (0 = run forever)
#   --headless_report_boot    Report boot status (default: true)
```

## Testing

### Validation Script

```bash
# Run automated tests (requires filesystem access)
/tmp/claude/xenia/test_headless.sh
```

### Quick Test

```bash
# Test that binary runs
/tmp/claude/xenia/build/bin/Linux/Checked/xenia-headless \
  --headless_timeout_ms=1000 2>&1 | grep "headless backends"
# Should output: "Emulator initialized with headless backends"
```

## Success Criteria

- [x] `xenia-headless` binary builds
- [x] No GTK+/Vulkan/X11/ImGui link dependencies
- [x] Binary size reduced (145MB, down from 162MB)
- [x] Game boots successfully (tested with DC1)
- [x] Console output shows boot status (Title ID, Title Name, boot progress)

## Crash Detection

When a game crashes, the headless binary outputs structured crash info:

```
CRASH: PC = 0x82xxxxxx
CRASH: Registers:
  r0 = 0x00000000  r1 = 0x7xxxxxxx  r2 = 0x8xxxxxxx  r3 = 0x00000000
  r4 = 0x00000001  r5 = 0x00000002  ...
  lr = 0x82xxxxxx
  ctr = 0x82xxxxxx
  cr = 0x00000000
  xer(ca,ov,so) = (0x0,0x0,0x0)
CRASH: Title ID: 0x545607d3
```

The crash PC can be parsed with grep:
```bash
xenia-headless ... 2>&1 | grep -E "^CRASH: PC"
```

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Normal exit (title terminated naturally OR timeout) |
| 1 | Initialization failed |
| Other | Launch error (see X_STATUS codes) |

**Note:** On timeout, the process uses `_Exit(0)` because the emulator thread cannot be cleanly terminated (it's blocked in `WaitUntilExit()`). This is expected behavior.

## Known Issues

- **Timeout uses `_Exit()`**: The emulator thread blocks in `WaitUntilExit()` and cannot be interrupted. Timeout forcibly terminates the process without cleanup. This is acceptable for CI/CD testing.

## Next Steps

1. Test with DC3 ISO (when available)
2. Add crash PC parsing for automated regression detection
3. Integrate with CI/CD pipeline
