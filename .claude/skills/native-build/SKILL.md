---
name: native-build
description: Build the native port (x86_64 Linux, Clang). Targets include dc3-native, milo-viewer, render-test, milo-tests, milo2gltf. Use when verifying native compilation or running native tests.
argument-hint: "[target] (default: all)"
allowed-tools: Bash(cmake *), Bash(ninja *), Read, Grep, Glob
---

# Native Build Skill

Build the native x86_64 Linux port of the DC3 decomp.

## Arguments

`$ARGUMENTS`

## Build Directory

The native build uses CMake + Ninja with Clang:
- **Source**: `/home/free/code/milohax/dc3-decomp/native/`
- **Build dir**: `/home/free/code/milohax/dc3-decomp/native/build/`
- **Generator**: Ninja

## Steps

1. **Parse arguments.** The argument is an optional target name. If empty, build all targets.

   Available targets:
   - `dc3-native` — main engine executable
   - `milo-viewer` — .milo file viewer
   - `render-test` — rendering tests
   - `milo-tests` — unit tests
   - `milo2gltf` — milo-to-gltf converter
   - `milo-tex-export` — texture exporter
   - `milo-mat-export` — material exporter
   - `wgpu-window-test` — WebGPU window test
   - (no target / `all`) — builds everything

2. **If build dir doesn't exist**, configure first:
   ```bash
   cmake -S /home/free/code/milohax/dc3-decomp/native -B /home/free/code/milohax/dc3-decomp/native/build -G Ninja
   ```

3. **Build the target:**
   ```bash
   cmake --build /home/free/code/milohax/dc3-decomp/native/build --target TARGET -- -j$(nproc)
   ```
   Or for all targets:
   ```bash
   cmake --build /home/free/code/milohax/dc3-decomp/native/build -- -j$(nproc)
   ```

4. **If build fails**, show the first error with context. Common issues:
   - Missing `#ifdef HX_NATIVE` guards for PPC-specific code
   - STL API differences between MSVC PPC and modern Clang
   - Missing stub implementations in `engine_stubs_generated.cpp`
   - Sandbox blocking Vulkan/GPU access — use `dangerouslyDisableSandbox: true`

5. **Run tests** (if target is `milo-tests`):
   ```bash
   cd /home/free/code/milohax/dc3-decomp/native/build && ctest --output-on-failure
   ```

## Tips

- The native build uses Clang with MSVC compatibility flags (`-fms-extensions`, `-fms-compatibility`)
- `HX_NATIVE` is defined for native builds — use `#ifdef HX_NATIVE` for platform-specific code
- Functions only in PPC .obj files won't link in native — add source implementations or stubs
- GPU rendering requires `dangerouslyDisableSandbox: true` due to Vulkan ICD access
