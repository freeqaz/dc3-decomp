# Xenia Boot Validation for DC3 Decomp

## Status: Infrastructure Complete, Runtime Validation Pending

**Date:** 2026-02-16

---

## Summary

Successfully built both Xenia emulator from source and the decompiled DC3 hybrid XEX. The validation infrastructure is complete. Runtime boot testing is blocked by Xenia's GUI dependencies, which will be addressed by modifying Xenia to run in headless mode.

---

## Completed Tasks

### 1. Xenia Source Build ✅

**Repository:** `xenia-project/xenia` cloned to `/tmp/claude/xenia`

**Build Configuration:** Release mode (x64 Linux)

**Issues Fixed:**
| Issue | Location | Fix |
|-------|-----------|-----|
| Missing `unistd.h` | `third_party/premake-core/contrib/libzip/mkstemp.c`, `zip_close.c`, `zip_fdopen.c` | Added `#include <unistd.h>` on non-Windows |
| Missing `unistd.h` | `third_party/premake-core/contrib/libzip/zipint.h` | Added `#include <unistd.h>` on non-Windows |
| Integer overflow warning | `third_party/cxxopts/include/cxxopts.hpp:488` | Changed `-(std::numeric_limits<T>::min)()` to `std::numeric_limits<T>::max()` |
| Deprecated literal operator syntax | `third_party/fmt/include/fmt/format.h` | Changed `operator"" _u/a` to `operator""_u/a` |
| Deprecated literal operator syntax | `third_party/date/include/date/date.h` | Changed `operator"" _d/y` to `operator""_d/y` |
| Deprecated literal operator syntax | `third_party/half/include/half.hpp` | Changed `operator"" _h` to `operator""_h` |
| Non-trivially copyable memset/memcpy | `third_party/imgui/imgui.h`, `imgui_internal.h` | Added `reinterpret_cast<void*>` cast |
| Non-trivially copyable memset | Multiple xenia source files | Added `reinterpret_cast<void*>` cast |
| `extern "C"` on main | `console_app_main_posix.cc`, `windowed_app_main_posix.cc` | Removed `extern "C"` wrapper |
| Missing `uint32_t` | `src/xenia/cpu/backend/assembler.h` | Added `#include <cstdint>` |

**Built Binaries:**
```
/tmp/claude/xenia/build/bin/Linux/Release/
├── xenia                         (56.9 MB) - Main emulator
├── xenia-cpu-tests
├── xenia-gpu-vulkan-trace-viewer
├── xenia-ui-window-vulkan-demo
├── xenia-hid-demo
├── xenia-cpu-ppc-tests
├── xenia-gpu-shader-compiler
├── xenia-vfs-dump
└── xenia-base-tests
```

### 2. Hybrid XEX Build ✅

**Script:** `scripts/build_xex.py`

**Input:** `build/373307D9/default.exe` (19.6 MB hybrid PE)

**Output:** `build/373307D9/default.xex` (19.6 MB unencrypted XEX)

| Build | Size |
|-------|------|
| Original XEX | 16,887,808 bytes |
| Decompiled XEX | 19,611,648 bytes |

**Size Difference Explanation:**
- Original: Optimized retail build
- Decompiled: Debug build with symbols
- ~16% size difference expected

---

## Blocked Issues

### Xenia Runtime Dependencies

Xenia fails to initialize due to missing GUI dependencies:

```
Failed to initialize GTK+
```

**Missing Symbols:**
```
glxewInit                           → GLXew (OpenGL X extension wrapper)
cgGLEnableProgramProfiles             → NVIDIA Cg (C for Graphics)
ssgInit                              → SSG (Simple Scene Graph)
gtk_progress_get_type                → GTK+ internals
```

**Required Components:**
- X11 or Wayland display server
- GTK3 (installed)
- NVIDIA Cg toolkit (deprecated, unmaintained)
- GLXew library
- SSG scene graph library
- Full GPU graphics stack

---

## Next Steps

### Immediate: Headless Xenia Modification

**Goal:** Modify Xenia to run without GUI dependencies for automated boot testing.

**Approach:**
1. Identify minimal components needed for boot validation
2. Conditionally compile out GTK/UI dependencies
3. Add headless mode option
4. Implement console-based status output

**Modified Xenia Location:** Will work from `/tmp/claude/xenia`

### After Headless Fix

1. **Boot Original XEX Baseline**
   ```bash
   xenia-canary --debug --headless orig/373307D9/default.xex
   ```
   Save output to `orig_boot.log`

2. **Boot Decompiled XEX**
   ```bash
   xenia-canary --debug --headless build/373307D9/default.xex
   ```
   Save output to `decomp_boot.log`

3. **Compare Results**
   - Check if decompiled XEX reaches mainCRTStartup
   - Compare crash addresses (if any)
   - Map crashes to functions using `build/373307D9/default.map`

---

## Critical Files

| File | Purpose |
|-------|---------|
| `/tmp/claude/xenia/build/bin/Linux/Release/xenia` | Xenia emulator binary |
| `/home/free/code/milohax/dc3-decomp/build/373307D9/default.xex` | Decompiled hybrid XEX |
| `/home/free/code/milohax/dc3-decomp/build/373307D9/default.map` | Address-to-function mapping |
| `scripts/build_xex.py` | XEX generator from PE |
| `tools/project.py` | Linker configuration |

---

## Verification Checklist (Pending Headless Fix)

- [ ] Original XEX boots successfully on headless Xenia
- [ ] Decompiled XEX either boots OR specific fixable error identified
- [ ] Crash analysis documented (if crashes occur)
- [ ] Fix iteration notes saved

---

## Build Notes

### Xenia Build Environment
- Compiler: Clang (latest)
- Build system: premake5 → gmake2
- Target: x86_64-linux-gnu
- Configuration: Release

### Patch History

All patches applied to `/tmp/claude/xenia`:

1. **libzip headers** - Added `unistd.h` includes
2. **cxxopts** - Fixed integer overflow expression
3. **fmt** - Fixed literal operator syntax
4. **date** - Fixed literal operator syntax
5. **half** - Fixed literal operator syntax
6. **imgui** - Fixed memset casts (multiple locations)
7. **xenia source** - Fixed memset/memcpy casts (dozens of files)
8. **console_app_main** - Removed `extern "C"` from main
9. **windowed_app_main** - Removed `extern "C"` from main
10. **assembler.h** - Added `cstdint` include

---

## Alternative Validation Methods (If Headless Fails)

1. **Static Analysis**
   - Compare XEX PE/XEX headers with `hexdump`
   - Verify section alignments and exports

2. **Symbolic Execution**
   - Use Unicorn engine for function-by-function testing
   - Already integrated into decomp workflow

3. **Different Emulator**
   - Research other X360 emulators (limited options)
   - QEMU PPC backend (experimental)

---

## References

- Xenia: https://github.com/xenia-project/xenia
- Plan source: `docs/plans/XENIA_BOOT_VALIDATION_PLAN.md`
