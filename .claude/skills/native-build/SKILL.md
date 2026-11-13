---
name: native-build
description: Configure, build and test the native port (Linux, Clang, WebGPU/Dawn) in the current checkout or worktree. Targets include dc3-native, milo-viewer, render-test, milo-tests, milo2gltf, milo-tex-export, milo-mat-export. Use when verifying native compilation or running the native test gate.
argument-hint: "[target] (default: the test gate's required targets)"
allowed-tools: Bash(scripts/native_configure.sh *), Bash(scripts/native_test.sh *), Bash(scripts/check_native_compiles.sh *), Bash(cmake --build *), Bash(git rev-parse *), Read, Grep, Glob
---

# Native Build Skill

Configure, build and test the native port of the DC3 decomp.

## Arguments

`$ARGUMENTS`

## Work in the right tree

Run every command from the root of the checkout you are actually working in:
the worktree root if you are in one, the main checkout only if you are working
there. Find it with `git rev-parse --show-toplevel`. Never point a command at
another checkout's `native/build`. A CMake build dir records absolute source
paths, so building another tree's dir compiles *that* tree's sources and gates
the wrong code.

- **Source**: `native/` (it links the shared engine at `../milo-native-engine`,
  resolved from the main checkout even inside a worktree)
- **Build dir**: `native/build/` (override with `MILO_TEST_BUILD_DIR`)
- **Generator**: Ninja, compiler clang. `scripts/native_configure.sh` sets both.

## Steps

1. **Parse arguments.** The argument is an optional target name. Targets that
   build in `native/build` (from the `add_executable` lines in
   `native/CMakeLists.txt`):
   - `dc3-native`: the game
   - `milo-viewer`: .milo file viewer
   - `render-test`: programmatic render test scenes
   - `milo-tests`: GTest suite (only when `BUILD_TESTS=ON`, the default)
   - `milo2gltf`, `milo-tex-export`, `milo-mat-export`: export tools

   (`wgpu-window-test` was deleted on 2026-11-13; it had not compiled since
   `GpuDevice.h` moved into the shared engine.) `dc3-web` exists only in an
   Emscripten build; build it with `scripts/web/build.sh`, never in
   `native/build`.

2. **Configure if `native/build` has no `CMakeCache.txt`.** Do not run a bare
   `cmake -S native -B native/build -G Ninja`: it cannot find Dawn.
   ```bash
   scripts/native_configure.sh            # default build dir native/build
   ```
   This derives `Dawn_DIR` (the sibling `dc3-decomp-deps` repo), `ncnn_DIR` and
   `ENABLE_NCNN`, clang, Ninja, `CMAKE_BUILD_TYPE=RelWithDebInfo` and
   `CMAKE_EXPORT_COMPILE_COMMANDS` from the main checkout. Override with
   `DAWN_DIR`, `NCNN_DIR`, `CC`/`CXX` or `MILO_BUILD_TYPE` if needed.
   Exit 9 means a required dependency (Dawn, clang, cmake) is missing; the
   script names every path it probed. Exit 10 means cmake itself failed.

3. **Build one target:**
   ```bash
   cmake --build native/build --target TARGET
   ```
   With no target argument, prefer step 5 (the test gate builds its own
   required targets: `milo-tests dc3-native milo-viewer`). For a compile-only
   check, use `scripts/check_native_compiles.sh` (`--configure` for a fresh
   build dir).

4. **If the build fails**, show the first error with context. Common causes:
   - PPC-only code without an `HX_NATIVE` guard
   - STL API differences between MSVC/STLport and modern libstdc++/libc++
   - The linker refuses undefined symbols (`-Wl,--no-undefined`): a missing
     definition needs real source or a typed stub in `native/src/xbox_link_stubs.cpp`,
     `native/src/engine_stubs_generated.cpp` or a `native/src/platform/*_Stub.cpp`.
     See `docs/native/MISSING_DEFINITIONS.md`.
   - The system toolchain moved under the build dir (a package upgrade).
     `scripts/native_test.sh` detects and repairs this; a bare build does not.
   - Sandbox blocking Vulkan/GPU access: use `dangerouslyDisableSandbox: true`.

5. **Run tests with the wrapper, never bare `ctest`:**
   ```bash
   scripts/native_test.sh                 # builds the required targets, then runs ctest
   scripts/native_test.sh -R SomeRegex    # extra args pass through to ctest
   scripts/native_test.sh --all-gates     # also DC3_GAMEPLAY_TESTS / audio / long tiers (slow)
   ```
   Bare `ctest` counts skipped tests as passes and tests whatever binary is
   already built. The wrapper reports executed, passed, failed and skipped
   separately and enforces the skip budget in `native/tests/skip_budget.txt`.
   Key exit codes (the full table is in the script header):
   - `0`: pass
   - `2`: more skips than budget (coverage shrank)
   - `3`: fewer skips than budget (lock the gain in)
   - `5`: budget file missing or unparseable
   - `6`: build failed
   - `8`: ctest's own "tests failed"
   - `9`: no configured build, **zero tests ran**
   - `10`: toolchain moved and could not be refreshed
   - `11`: another run holds this build dir

   Do not edit `skip_budget.txt` to make a run pass.

## Tips

- The native build compiles decomp sources with clang's MSVC compatibility
  flags (`-fms-extensions`, `-fms-compatibility`).
- `HX_NATIVE` is defined for native builds. Guard native-only code with
  `#ifdef HX_NATIVE`.
- GPU rendering requires `dangerouslyDisableSandbox: true` (Vulkan ICD access).
- Testing details: `docs/native/TESTING.md`.
