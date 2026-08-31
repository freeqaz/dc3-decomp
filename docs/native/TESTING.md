# Native Build Testing Guide

## Framework

Tests use [Google Test](https://google.github.io/googletest/) with `gtest_discover_tests`.

**Run the suite through the wrapper, never bare `ctest`** — `ctest` scores skipped
tests as passed and prints "100% tests passed" either way:

```bash
scripts/native_test.sh                    # configures if needed, builds, runs, reports skips
```

It configures a missing build dir automatically (via `scripts/native_configure.sh`,
which derives `Dawn_DIR` and the rest from the main checkout), so this works in a
fresh worktree with no manual steps. If it cannot configure one it exits **9** —
"NATIVE GATE DID NOT RUN" — rather than a generic failure, because a run that
examined zero tests must not be reportable as a pass. See the exit-code table in
the script header.

To configure or drive the binary by hand:

```bash
scripts/native_configure.sh               # cmake configure; NOT plain `cmake ..`
                                          # (find_package(Dawn REQUIRED) needs -DDawn_DIR)
cmake --build native/build --target milo-tests
cd orig-assets                            # tests resolve assets relative to cwd
../native/build/milo-tests                            # Run all tests
../native/build/milo-tests --gtest_filter='Suite.Test' # Run specific test
../native/build/milo-tests --gtest_list_tests         # List available tests
```

## Fixture Hierarchy

### `SymbolTestFixture` (lightweight)
Initializes only the Symbol/StringTable system. Use for pure unit tests that don't need engine subsystems.

### `EngineTestFixture` (full headless boot)
Runs the complete headless engine init sequence (`SystemPreInit` → `SystemInit`). Use for anything touching engine subsystems — tests the real boot path.

## Writing Tests

- **Prefer `EngineTestFixture`** for anything touching engine subsystems
- **Use `GTEST_SKIP()`** when game data or hardware is unavailable (keeps CI green)
- **Use `MemBinStream`** for serialization tests (no file I/O needed)
- **Use `WriteSyntheticMilo()`** for format tests needing .milo files
- Keep tests focused — one subsystem behavior per test
- Add timeouts on async operations (no infinite hangs)

## Test Categories

| Category | Examples | Fixture |
|---|---|---|
| Unit | BinStream round-trip, JoypadData structs | `SymbolTestFixture` or none |
| Integration | DirLoader, subsystem init, ThreadCall | `EngineTestFixture` |
| Diagnostic | Bink playback, audio devices | `EngineTestFixture` + env-var gated |

### Environment Variables

- `MILO_DIAG_FILE` — path to a .milo file for diagnostic load tests
- `MILO_TEST_BIK` — path to a .bik file for Bink tests

## Headless Boot Tests

`test_headless_boot.cpp` launches `dc3-native` as a subprocess to test full engine boot stability:

- **BootAndRun100Frames** — basic boot smoke test
- **SurvivesMainLoop** — 2000 frames, verifies main loop stability
- **InputReplayStartButton** — scripted button presses via `MILO_INPUT_SCRIPT`
- **LongRunStability** — 10000 frames (env-gated via `MILO_LONG_TEST=1`)

Tests FAIL (not skip) on crashes, with `CrashSummary()` extracting signal, assertion, last DirLoader load, and last DataNew for diagnostics.

## Debugging Tools

### AddressSanitizer (ASan)

```bash
cmake -S native -B native/build-asan -G Ninja -DENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build native/build-asan -- -j$(nproc)
```

ASan catches heap corruption, use-after-free, buffer overflows. Essential for finding real bugs hidden by memory corruption cascades. See [debugging/native.md](../debugging/native.md#addresssanitizer-asan) for the full ASan guide: suppressions, allocator behavior differences, and reading ASan output.

### Non-Fatal Assert Mode

Set `MILO_FATAL_FAILS=0` to continue past assertions (mimics Xbox 360 "Continue" dialog). Useful for exploring the full scope of boot issues without stopping at the first assert.

### Workflow

1. **ASan build** — find memory bugs early
2. **MILO_FATAL_FAILS=0** — see how far the engine gets
3. **Headless boot tests** — regression testing after fixes
4. **Input replay** — reproducible UI navigation

## Adding Tests

1. Create `native/tests/test_yourtest.cpp`
2. Add the file to `CMakeLists.txt` in the `milo-tests` sources list
3. Use the appropriate fixture
4. Tests are auto-discovered by `gtest_discover_tests`
