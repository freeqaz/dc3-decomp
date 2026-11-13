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
fresh worktree with no manual steps. It also **builds before it tests**, so it never
measures a stale binary, and checks the system toolchain by content hash
(`scripts/native_toolchain_check.py`), reconfiguring if a package upgrade moved it.

### Wrapper flags

| Invocation | Effect |
|---|---|
| `scripts/native_test.sh` | Default gate set; skip budget enforced |
| `scripts/native_test.sh --all-gates` | Also exports `DC3_GAMEPLAY_TESTS=1`, `DC3_AUDIO_TESTS=1`, `MILO_LONG_TEST=1`. The skip budget is **not** enforced (it describes the default set) |
| `scripts/native_test.sh -R SomeRegex` | Any argument the wrapper does not recognise passes through to `ctest` |
| `scripts/native_test.sh --no-build` | Test whatever is already built |
| `scripts/native_test.sh --no-configure` | Do not auto-configure a missing build dir |
| `SKIP_BUDGET_UPDATE=1 scripts/native_test.sh` | Rewrite `native/tests/skip_budget.txt` from this run. Loosening it (more skips) also needs `ALLOW_BUDGET_LOOSEN=1` |
| `scripts/native_test.sh --no-assets` | **Machines with no game data only** (the GitHub job). Enforces an executed-test floor instead of the skip budget; refuses to run where there is game data. See [No game data (CI)](#no-game-data-ci) |

`--all-gates` is slow because CTest runs each gtest case in its own process, so every
`GameplayTelemetryTest` case rebuilds the suite's shared engine fixture. To exercise
that tier quickly, run the binary directly so the fixture is built once:

```bash
cd orig-assets && DC3_GAMEPLAY_TESTS=1 \
    ../native/build/milo-tests --gtest_filter='GameplayTelemetryTest.*'
```

### Exit codes

Each failure mode has its own number (source of truth: the header of
`scripts/native_test.sh`).

| Code | Meaning |
|---|---|
| 0 | All executed tests passed and the skip count equals the budget |
| 2 | More tests skipped than the budget: coverage shrank |
| 3 | Fewer tests skipped than the budget: coverage improved, lock it in |
| 4 | The ctest summary line could not be parsed |
| 5 | Skip budget file missing or unparseable, or a budget loosen without `ALLOW_BUDGET_LOOSEN=1` |
| 6 | The build failed (ctest was not run against the previous binary) |
| 7 | `milo_test_required_targets.txt` missing or empty (CMake's target list did not reach the script) |
| 8 | **Reserved: ctest's own "tests failed" status**, passed through unchanged |
| 9 | No configured build, and it could not be configured — **zero tests ran; not a pass** |
| 10 | The system toolchain moved under the build dir and it could not be refreshed |
| 11 | Another gate holds this build dir's lock |
| 12 | `--no-assets` refused: the build's ctest working dir or `$DC3_DATA` has game data, or `--all-gates` was also given |
| other | ctest's own exit status |

### Skip-budget ratchet

The expected skip count for the default gate set is recorded in
`native/tests/skip_budget.txt` (`cat native/tests/skip_budget.txt` for the current
value). Exit 2 or 3 means coverage changed. Before changing the budget, check
whether the cause is the environment rather than the code:

- **`archive/` missing.** `archive/` is gitignored; `MiloViewerScreenshot.PoseDumpCanMatchGoldenWithTolerance`
  needs `archive/screenshots/pose_regression/goldens/stand_bad_mid.pose.json`.
  `scripts/setup_worktree.sh` symlinks `archive/` into worktrees, and the wrapper
  names the missing golden on exit 2.
- **GPU driver mismatch.** A driver package upgraded without a reboot leaves the
  kernel module and userspace disagreeing, no Vulkan ICD loads, and the GPU-gated
  viewer tests skip. The wrapper detects this and says so; the fix is a reboot.

**Do not raise the budget for an environment fault** — the file is the evidence.
Change it deliberately, in a commit that says why.

### Required targets

The suite drives three binaries as subprocesses. `MILO_TEST_REQUIRED_TARGETS`
in `native/CMakeLists.txt` (currently `milo-tests dc3-native milo-viewer`) is the
single list; CMake writes it to `milo_test_required_targets.txt` for the wrapper,
and `TestGates.BuildMatchesSources` asks ninja whether those targets are current
(`MILO_TEST_ALLOW_STALE_BUILD=1` skips that check).

## No game data (CI)

The GitHub `native-build` job has no `orig-assets`. It runs
`scripts/native_test.sh --no-assets`, and three pieces make that run honest:

- **An explicit absence check, not a crash.** `EnsureEngineInit()`
  (`native/tests/test_helpers.cpp`), which every engine-backed test goes
  through, first asks `NativeFindDataDir()`. This is the boot path's own probe:
  `gen/main_xbox.hdr` under `$DC3_DATA`, then `orig-assets`, `../orig-assets`,
  `../../orig-assets` or `.` from the cwd. If none has it, the function does
  not boot. It records a `GTEST_SKIP` tagged `[no-game-data]` and returns
  `false`. From `SetUpTestSuite` that skips the whole suite. A caller that
  touches the engine after it must return on `false` (or on `IsSkipped()`).
  The function is `[[nodiscard]]` so that new direct callers notice.
- **The engine reports missing data instead of crashing.** `dc3-native`, the
  viewer and the tools exit 1 with
  `DC3 Native: FATAL - no game data: <path> does not exist.` naming
  `DC3_DATA`. `NativeArchiveInit`, `native/src/platform/System_Native.cpp`,
  checks the header before reading it and checks the read afterwards. Before
  this check, the header-less `Archive` had an empty hash table, and the first
  file lookup computed `HashString(name, 0)`, a modulo by zero (SIGFPE). That
  killed about 320 tests in 58 suites on the runner, and it killed the game for
  any player with a bad `DC3_DATA`. `HeadlessBootNoGameData.ExitsWithAnErrorNotASignal`
  pins this behaviour. It is asset-free and runs everywhere.
- **A floor, not a budget.** With no data the skip budget means nothing.
  `--no-assets` enforces a minimum number of **verified** passes, recorded on the
  first line of `native/tests/no_assets_floor.txt`. A verified pass is a gtest
  case that printed its own `[       OK ]` line in ctest's `LastTest.log`. ctest
  counts a skip as a pass, so without the floor a run where everything skipped
  would be green. ctest also scores a `milo-tests` run that ran nothing as a
  pass, which is what happens when a global gtest `Environment` skips.
  - Fewer verified than the floor: exit 2, because asset-free coverage shrank.
  - Any **ghost**, meaning a gtest run scored PASSED that printed no
    `OK`/`SKIPPED`/`FAILED` line: exit 2.
  - Sabotage checks (container replay of the workflow at `9d24ed458`):
    - A global `Environment` that skips everything: 0 verified, 668 ghosts,
      exit 2, job FAILURE. ctest itself scored 670 of 672 "executed", so a floor
      on ctest's own count would have passed this.
    - `SymbolTestFixture` skipping: 167 verified against a floor of 262, exit 2,
      job FAILURE. The workflow's "nothing passed" check alone misses this one.
  - More verified: exit 0, with a notice to raise the floor. This is not a
    failure, because lanes add asset-free tests and cannot measure this number
    on a box that has the data.
  - Raise the floor with `NO_ASSETS_FLOOR_UPDATE=1`. Lowering it also needs
    `ALLOW_FLOOR_LOWER=1`. A missing or zero floor is exit 5.
  - The mode refuses to run (exit 12) where game data exists, so it cannot be
    used to dodge the budget.
  - Each run writes `native_test_summary.txt` (`NT_VERIFIED`, `NT_GHOSTS`,
    `NT_EXECUTED`, `NT_SKIPPED`, `NT_NO_DATA_SKIPS`, …) to the build dir. The workflow copies it into the job
    summary.

The workflow gates on the wrapper's exit status. It also runs its own
`LastTest.log` checks: no log, any `[  FAILED  ]`, and nothing passed.

**Measuring the floor** needs a build with no game data. On this box,
`native/build` always has the data. Use the CI log's `EXECUTED` line, or replay
the workflow in an `ubuntu:26.04` container. The H-3 lane did the replay by
turning each step of `native-build.yml` into a bash step run under
`bash -eo pipefail` with `$GITHUB_ENV` emulated, with `git archive`s of dc3 and
milo-native-engine as the checkouts.

## Test gates

`TestGates.*` prints the gate table on every run and fails if a gate CMake enabled
does not reach the test process. Gates are decided in `native/CMakeLists.txt`.

| Gate | Default | Turns on when | Covers |
|---|---|---|---|
| `MILO_LIB` | on if found | `$MILO_LIB` or `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3` contains `char/crowd/anim/gen/female_base.milo_xbox` at configure time | `CharClipGroupTest`, parts of `MiloViewerScreenshot`, bulk loads |
| `DC3_DTA_FLOW_TESTS` | on if found | The test asset dir contains `gen/main_xbox.hdr` | `DtaFlowTest`, controller-mode flow tests |
| `DC3_GAMEPLAY_TESTS` | off | `--all-gates`, or set it yourself | `GameplayTelemetryTest` (one shared long engine run), `EmptySensorGameplayTest` (a whole song with scoring off) |
| `DC3_AUDIO_TESTS` | off | `--all-gates`, or set it yourself | Real audio device tests (contend under `ctest -j`) |
| `MILO_LONG_TEST` | off | `--all-gates`, or set it yourself | `HeadlessBootTest.LongRunStability` |

## Test assets and environment

- **Working directory.** ctest runs every test with its working directory set to the
  asset dir (`MILO_TEST_ASSET_DIR` in `native/CMakeLists.txt`: the in-tree
  `orig-assets`, which is a symlink in worktrees). Run the binary by hand from
  `orig-assets/` too.
- `DC3_DATA` — extracted ark root for archive-backed `AssetLoadingTest` cases (skip
  with "set DC3_DATA" when absent).
- `MILO_LIB` — extracted milo library (see gates).
- `MILO_TEST_BIK` — `.bik` for Bink tests. Unset: the first existing file under
  `/tmp/claude-1000/bik_fixtures/` (written by the `ExtractBik` tests).
- `MILO_DIAG_FILE` — a `.milo` file for the `MiloDiagnostic` / `FlowDesync`
  load tests; `MILO_DIAG_OBJECT` selects the object index `FlowDesync.FlowAnimateFieldTrace` traces.
- `MILO_TEST_CHAR`, `MILO_TEST_CLIPS` — bone/clip dir overrides for the bone
  ground-truth tests.
- `MILO_POSE_GOLDEN_DIR` — override for the pose-dump golden directory.

Runtime variables for `dc3-native` itself are in the master table in
[debugging/native.md](../debugging/native.md#environment-variables).

## Configure and run by hand

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

Both live in `native/tests/test_helpers.h`.

## Writing Tests

- **Prefer `EngineTestFixture`** for anything touching engine subsystems
- **Use `GTEST_SKIP()`** when game data or hardware is unavailable (keeps CI green). For
  the engine boot, `EnsureEngineInit()` / `EngineTestFixture` already does this; if your
  `SetUpTestSuite` override or `TEST` body uses the engine after calling it, return when
  it reports the skip (`if (IsSkipped()) return;` / `if (!EnsureEngineInit()) return;`)
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

## Headless Boot Tests

`native/tests/test_headless_boot.cpp` launches `dc3-native` as a subprocess to test full engine boot stability:

- **SurvivesMainLoop** — main loop stability
- **InputReplayStartButton** — scripted button presses via `MILO_INPUT_SCRIPT`
- **LongRunStability** — long run (gated by `MILO_LONG_TEST=1`)

Tests FAIL (not skip) on crashes, with `CrashSummary()` extracting signal, assertion, last DirLoader load, and last DataNew for diagnostics.

## Recipes

### Bulk asset load sweep

`AssetLoadingTest.BulkLoadAllFiles` walks `MILO_LIB` and loads `.milo_xbox` files
per category (failures are non-fatal; it forces `MILO_FATAL_FAILS=0` for the sweep).
The per-category cases `AssetLoadingTest.BulkLoad_{Flow,Char,World,UI,SFX,Songs}`
load a spread-out subset of each category unless `MILO_BULK_ALL=1`.

```bash
cd native/build && ctest -R BulkLoad --output-on-failure
MILO_BULK_CATEGORY=ui ctest -R BulkLoadAllFiles --output-on-failure  # ui|world|char|sfx|flow|songs|all
MILO_BULK_LIMIT=50 ctest -R BulkLoadAllFiles --output-on-failure     # files per category (default 20; 0 = all)
MILO_BULK_ALL=1 ctest -R BulkLoad --output-on-failure                # every file, every case
```

### Bone ground truth and clip poses

`native/tests/test_bone_ground_truth.cpp` (`BoneGroundTruth`, `ClipPoseFixture`,
`MainMiloLoadTest`) checks skeleton topology, rest pose and clip evaluation numerically:

```bash
cd orig-assets
../native/build/milo-tests --gtest_filter='BoneGroundTruth.*:ClipPoseFixture.*:MainMiloLoadTest.LoadMainCharacterMilo'
../native/build/milo-tests --gtest_filter='CharBonesSamplesTest.*'
```

### Xenia goldens

`XeniaGolden.*` compares the native port against state recorded from the original
`debug.xex` under Xenia (see [plans/XENIA_ORACLE.md](../plans/XENIA_ORACLE.md)). The
bundles live in the gitignored `archive/state_diff/goldens/xenia/`; a missing bundle
or missing `dc3-native` is a skip (exit 77), and Xenia itself is not needed:

```bash
cd native/build && ctest -L XeniaGolden --output-on-failure
```

## Character-pose screenshot goldens (not webcam pose)

This is a regression suite for **character animation rendering** — skeleton, clip
evaluation and mesh skinning drawn by `milo-viewer`. It has nothing to do with the
webcam pose input.

```bash
native/scripts/pose_regression.sh                      # capture all poses
native/scripts/pose_regression.sh --capture-with-dump  # also write pose JSON sidecars
native/scripts/pose_regression.sh --compare            # byte-compare PNGs against goldens
native/scripts/pose_regression.sh --compare-pose-json  # tolerance compare of the JSON sidecars
native/scripts/pose_regression.sh --update-goldens     # copy captures over the goldens
```

- Needs a built `milo-viewer` (`cmake --build native/build --target milo-viewer`), a
  working Vulkan ICD, and the DC3 milo library at `$MILO_LIB` (default
  `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3`).
- Captures go to `archive/screenshots/pose_regression/captures/`, goldens to
  `archive/screenshots/pose_regression/goldens/`. `--compare` uses `cmp`; on a
  mismatch it writes an ImageMagick diff if `compare` is installed.
- Poses: `tpose_aubrey` (rest pose) and `crouch_great_{start,mid}`,
  `stand_bad_{start,mid}` — dance clips posed with the viewer's `--direct-pose`
  (`CharClip::PoseMeshes`) at `--frame -1` (start beat) or `-2` (midpoint). The
  JSON sidecars come from the viewer's `--pose-dump`.
- `MiloViewerScreenshot.PoseDumpCanMatchGoldenWithTolerance` (in
  `native/tests/test_milo_viewer_screenshot.cpp`) checks a fresh dump against
  `stand_bad_mid.pose.json` using `native/scripts/compare_pose_json.py`.
- Re-baseline only after an intentional change to clip evaluation, skeleton
  loading, the mesh pipeline or the viewer's pose path. A GPU driver update can
  change pixels: confirm the diff is cosmetic before re-baselining. Never
  re-baseline to hide an unexplained diff.
- GPU access from an agent needs the sandbox disabled.

## Debugging Tools

### AddressSanitizer (ASan)

```bash
ulimit -d unlimited   # ASan's shadow reservation exceeds this box's 32 GB RLIMIT_DATA
scripts/native_configure.sh "$PWD/native/build-asan" -DENABLE_ASAN=ON -DMILO_ENGINE_ENABLE_ASAN=ON \
  -DCMAKE_C_FLAGS=-fsanitize-recover=address -DCMAKE_CXX_FLAGS=-fsanitize-recover=address
ninja -C native/build-asan dc3-native milo-tests
```

ASan catches heap corruption, use-after-free, buffer overflows. Essential for finding real bugs hidden by memory corruption cascades. See [debugging/native.md](../debugging/native.md#addresssanitizer-asan) for the full ASan guide: suppressions, allocator behavior differences, and reading ASan output.

### Fatal vs non-fatal fails

Fails are **non-fatal by default** (`MILO_FATAL_FAILS` unset or `0`): `MILO_FAIL`
and `MILO_ASSERT` print `FAIL:` and continue, like the Xbox 360 "Continue" dialog.
Set `MILO_FATAL_FAILS=1` to abort at the first one.

### Workflow

1. **ASan build** — find memory bugs early
2. **Default (non-fatal) run** — see how far the engine gets
3. **Headless boot tests** — regression testing after fixes
4. **Input replay** — reproducible UI navigation

Single-shot renders for the standalone renderer are covered in [RENDER_TEST.md](RENDER_TEST.md).

## Known gaps

- `render-test` has no golden-image comparison and is not registered with ctest.
- No regression test covers the audio Suspend/Resume path.

## Adding Tests

1. Create `native/tests/test_yourtest.cpp`
2. Add the file to the `milo-tests` sources list in `native/CMakeLists.txt`
3. Use the appropriate fixture
4. Tests are auto-discovered by `gtest_discover_tests`
5. If the test needs an optional gate, skip with a message naming the gate — an
   added skip moves the skip budget (see above)
