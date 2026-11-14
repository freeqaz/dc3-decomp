# Building the native port on tnr

`scripts/remote/tnr_build.sh` builds a worktree's native port on **tnr** (the
256-thread CMP serving host) instead of the desk, then copies the build dir
back. The fetched dir is a normal build dir on the desk: `ninja` sees nothing
to do, and `scripts/native_test.sh` runs without recompiling.

```sh
scripts/remote/tnr_build.sh ~/tmp/my-lane                # gate's required targets
scripts/remote/tnr_build.sh ~/tmp/my-lane dc3-native     # just these targets
scripts/remote/tnr_build.sh --no-fetch ~/tmp/my-lane     # compile check only
```

| Option | Effect |
|---|---|
| `--jobs N` | `ninja -j` and CPU quota on tnr (default **32**, at most 64; 32 until the cgroup-weight question below is settled) |
| `--reconfigure` | re-run `scripts/native_configure.sh` on tnr first |
| `--no-fetch` | leave the build on tnr; only the log comes back |
| `--test [ARGS]` | run `scripts/native_test.sh` on tnr; **not the gate** (see [Tests on tnr](#tests-on-tnr)) |
| `--test-args 'S'` | the same, with `S` passed as is (`'-R regex -j4'`) |
| `--sync-assets` | copy `orig-assets`, `archive`, `native/models` and `milo-rnd-library/dc3` to tnr (tests only) |
| `--build-image` | (re)build the container image on tnr |

Exit codes:

| Code | Meaning |
|---|---|
| 0 | built and fetched |
| 1 | the build failed (log tail printed) |
| 3 | sync failed |
| 4 | the desk toolchain no longer matches the image (see below) |
| 9 | missing input |
| 10 | configure failed |
| **75** | **tnr is in a measurement window or busy: try later, or build on the desk** |

The full log is `native/build/tnr-build-last.log`.

Measured timings are in [Measured](#measured).

## How it works

- **Login.** Everything runs as `decomp-ops` over `ssh tnr-decomp`, under
  `/scratch/dc3-native-build/`. That directory holds everything, and nothing
  else on tnr is touched.

  | Path | Contents |
  |---|---|
  | `trees/<key>/` | source mirror |
  | `builds/<key>/` | persistent build dir |
  | `ccache/` | tnr's cache |
  | `deps/` | Dawn, imgui |
  | `bin/ccache` | the ccache binary |
  | `assets/` | game data and pose models; only after `--sync-assets` |
  | `xdg/`, `containers/` | podman state |
  | `image/` | the Containerfile and its build log |

  `<key>` is the worktree path with `/` replaced by `_`: one build dir per
  worktree, which is one per branch with `setup_worktree.sh` names.
- **Sync.** `git ls-files -co --exclude-standard` (tracked plus
  untracked-not-ignored), the worktree's top-level symlinks, and the engine
  checkout's own file list go to tnr. Files removed on the desk are pruned
  there. A second sync is about 1 s.
- **Container.** Rootless podman, image `localhost/dc3-native-build:<date>`
  from `scripts/remote/tnr/Containerfile`. Its packages come from the **Arch
  Linux Archive snapshot of the desk's last `pacman -Syu`**, so clang, the
  libraries and the cmake modules are **byte-identical** to the desk's. The
  extra packages were derived from the desk build, not guessed: every `/usr`
  header in `ninja -t deps` was mapped through `pacman -Qqo`. That is why
  `onetbb` is there: libstdc++ `__has_include`s it.
- **Identical paths.** These are mounted at their desk paths:

  - the worktree
  - the engine checkout
  - Dawn
  - the main checkout's imgui
  - the ccache binary and its cache dir
  - the asset probe targets

  The compiler path and `MILO_ENGINE_PATH` come from a desk configure dry
  run. The resulting `CMakeCache.txt`, `build.ninja` and every compile
  command are therefore the ones the desk would have produced, and ccache
  shares across worktrees on tnr exactly as it does on the desk
  ([BUILD_CACHE.md](BUILD_CACHE.md)).
- **Asset probes.** Configure looks for `orig-assets/gen/main_xbox.hdr` and
  `milo-rnd-library/dc3`. Where the desk has them, tnr mounts the real data
  (after `--sync-assets`) or an empty stub file. The gate set, the `-D`s and
  the `CTestTestfile`s therefore match the desk's. A build never reads assets.
- **Fetch.** `rsync -a --delete` of the build dir into
  `<worktree>/native/build`. `Testing/` is kept. The script then runs
  `native_toolchain_check.py --check` and a `ninja -n` on the desk, and prints
  what the desk would still rebuild (expected: `no work to do`).

## Etiquette on tnr (read before changing anything)

tnr serves models for other people and hosts other people's measurements.
Agent CPU work there normally goes through `cmpagent.slice` (`tnr-run` /
`cmp-agent submit`; see
`nvidia-cmp-170hx/docs/server-operations/TNR-CPU-OFFLOAD-2026-10-20.md`).
That route needs the root alias, which decomp automation must not use, and
`cmpagent` cannot run podman. These builds therefore run in `decomp-ops`'s
user slice, which the owner's M-3 drop-in already keeps off the
serving-reserved CPUs. On top of that, the builds copy the agent guard's
rules:

- They refuse to **start** (exit 75) while any of these hold:
  - `/run/cmp-measure-window` exists;
  - `/run/cmp-agent-pause` exists;
  - `/run/cmp-agent/guard.json` says `frozen`.
- They **pause** the container (cgroup freezer) the moment one of those
  appears mid-build, and resume 30 s after all clear, as the guard does.
  `touch /scratch/dc3-native-build/PAUSE` does the same for dc3 builds only,
  without touching the host's flags. Exercised: ninja's edge count stayed at
  909 for the whole pause, and the build resumed 30 s after the file went
  (60 s paused, exit 0).
- They run only on the guard's measured agent CPU set
  (`32-63,96-127,160-191,224-254`), under `nice 19` + `SCHED_IDLE` + idle IO,
  with `--cpus` = jobs (default 32, at most 64), a 96 GB memory cap and no
  network.
- At most **2** builds run at once host-wide (more queue, up to 30 min, then
  exit 75), and a second build of the same worktree is refused.
- The GPUs are never touched: no `/dev/nvidia*` is passed, and tests use
  Mesa's lavapipe (a CPU Vulkan driver).

The one property not reproduced is `cmpagent.slice`'s idle weight at the
cgroup root. If that is ever needed, the owner can grant it, by letting
`decomp-ops` submit to `cmpagent.slice` or with a `CPUWeight` drop-in for
`user-960.slice` (PLAN H-37).

**Do not** on tnr:

- raise `--jobs` past 64, or make 64 a default;
- start builds from loops;
- touch the window or pause flags;
- use `/srv/decomp`, `tnr`, `tnr-lane` or the GPUs;
- copy game assets anywhere but `/scratch/dc3-native-build/assets`.

## Image identity scopes the caches

The tnr ccache lives in `ccache/<image id>/`. A build dir records the image
that built it (`native/build/.tnr-image-id`, fetched along with everything
else), and a build dir built by another image is `ninja -t clean`ed and
reconfigured before building. Without this, a rebuilt image goes unnoticed:
adding `onetbb` changed which headers the PCH read (libstdc++
`__has_include`s `<tbb/tbb.h>`), yet ninja called the old PCH current and
ccache would have served compiles made without it.

## When the desk's packages change

The fetched build is only valid while the image's toolchain is the desk's.
After a desk `pacman -Syu`:

1. `tnr_build.sh` exits 4 (the fetched dir fails `native_toolchain_check`).
2. Set `ARCH_SNAPSHOT` in the Containerfile to the upgrade date, and bump the
   image tag in `tnr_build.sh` (`TNR_IMAGE`).
3. Run `--build-image`.
4. Check the package match: `pacman -Q` on the desk vs `podman run IMAGE
   pacman -Q`; expect 0 version differences.

## Tests on tnr

**tnr cannot run the native gate green: a run on tnr is not the gate.** It
runs the gate's tests under Mesa's lavapipe (`GPU = llvmpipe`), because tnr's
GPUs are not ours. That works, but every test that renders the game frame by
frame is too slow under lavapipe to finish inside its hard-coded caps.

Setup and command:

```sh
scripts/remote/tnr_build.sh --sync-assets ~/tmp/my-lane      # once: ~24 GB, 341 s cold, 3 s after
scripts/remote/tnr_build.sh --no-fetch --test -j16 ~/tmp/my-lane
scripts/remote/tnr_build.sh --no-fetch --test-args '-R ^NativeTreePaths\. -j4' ~/tmp/my-lane
```

Measured 2026-11-14 on `np-build-infra` with the full gate, `ctest -j16` in a
32-CPU container, image `4ab03b942615`:

| | desk (GPU, serial) | tnr (lavapipe, `-j16`) |
|---|---|---|
| executed / passed / failed / skipped | 766 / 766 / 0 / 70 | 765 / 724 / **41** / 71 (70 since `native/models` is synced) |
| wall | 2,861 s (desk load 60-75) | 966 s (most of it the failures running out their caps) |

The 41 failures, plus 2 more that failed in only one of the two runs (43
tests), fall into two classes.

- **Rendered engine runs overrun their caps (42 tests).** These suites run
  `dc3-native` with rendering on: `DtaFlowTest`, `DtaFlowIdleMultiuserTest`,
  `DtaFlowSongSelectScrollTest`, `YmcaFaithfulNoFastBootTest`,
  `ControllerMode*`, `StoryTutorialGate*`, `FitnessPlaylistFlowTest`,
  `OptionsGameplay*`, `LargestSongFlowTest`, `HttpInputTest`,
  `PerformCrowdTest`, `RehearseProjectionTest`, `XeniaGolden.*`,
  `HeadlessBootTest` (4) and the windowed `NativeExitTest` cases (5).
  - Their caps are hard-coded: `kEngineWallClockSeconds` = 300, 60 s for
    `YmcaFaithful`, 30 s exit waits, and the HTTP driver's command timeout.
  - lavapipe costs about 13 ms a frame on the attract screen. The desk's GPU
    does 2,000 headless frames in about 4 s.
  - Serially, with nothing else in the container:
    - `HeadlessBootTest.SurvivesMainLoop` takes 38 s (desk: 4 s).
    - `DtaFlowTest.NoCrashCleanExit` hits its 300 s cap (desk: 44 s).
  - So lower `-j` does not rescue them.
  - The same runs pass at desk speed when they set `MILO_NORENDER=1` (median
    tnr/desk ratio 1.06 over the 24 passing tests that take over 5 s), for
    example `GestureMenusTest`, `TwoPlayerBattleTest`, `PoseDropoutTest` and
    `NoCameraBattleTest`. The slowness is the CPU rasterizer, not the box.
- **`NativeAudioSeekTest.DecodeLeadReachesTheImageSteadyState` (1 test)**
  misses its bound by 2-12 ms, both runs (1,241 / 1,251 vs 1,254 ms).
  - It is a timing test that the rendering load disturbs.
  - It passes on the desk.

**Safe in parallel.** Everything else passed at `-j16`:

- the 700-odd unit tests;
- the `MILO_NORENDER` engine flows;
- `MiloViewer*`.

Each container gets its own network namespace, so test ports cannot collide
with tnr's services or with another lane.

**Pixel tests.** The image-comparing tests that reach their comparison all
match under lavapipe:

- `MiloViewerScreenshot.PoseDumpCanMatchGoldenWithTolerance`
- `TexRendererClearTest.*`
- `NativeGlyphRender*`
- `PostProcNoiseTest`
- `CameraYuvDecode`
- the `CameraViewTest` mask

None differed. `DtaFlowSongSelectScrollTest.ScrollDownEdgeMatchesImage`
never reaches its comparison: its flow times out first.

**What tnr could still save.** One run excluded those 43 tests by name with
`-E`:

- **793 registered / 723 passed / 0 failed / 70 skipped, in 131 s end to end.**
- The same tests took 1,257 s of the desk's 2,861 s serial gate.
- The excluded 43 took the other 1,589 s, and they still need a GPU.

**Trap:** `native_test.sh` exits **0** on that run. The skip ratchet counts
skips, not exclusions, so a green subset there looks exactly like a green
gate.

Making this a supported mode needs two things, both tracked as PLAN H-36:

- the rendered tests labelled in CMake, so `-LE` can select them instead of a
  hand-written name list that would rot;
- `native_test.sh` refusing to call a filtered run the gate.

## Measured

2026-11-14, gate required targets (2,557-2,605 ninja edges), default `-j32`,
image `25e456afa07b`:

| Run | sync | tnr ninja | fetch | total | ccache |
|---|---|---|---|---|---|
| cold tree, cold cache | 0.6 s | 79 s | 3.4 s | **91 s** | 2,601 misses |
| second worktree, same commit | 1.3 s | **10 s** | (not fetched) | 17 s | 2,595 hits / 6 misses |
| `msvc_compat.h` touched (every TU misses), 60 s paused on purpose | 1.2 s | 139 s | — | — | all misses |

For comparison, the same cold build on the desk took 658 s with the desk at
load 75-200.

After every fetch:

- `native_toolchain_check.py --check` reported `toolchain current`.
- `ninja -n` on the desk reported `no work to do`.
- The 81 tests that read the per-file path defines passed on the desk
  against tnr-built binaries, including `TestGates.BuildMatchesSources`, which
  asks the desk's ninja whether the build is current.
- **The full desk gate passed on tnr-built binaries** (`scripts/native_test.sh`
  with nothing rebuilt on the desk): 836 registered, 766 passed, 0 failed,
  70 skipped (= budget), exit 0.

The image is now `4ab03b942615` (it adds the test-only layer). Its first build
found the old image's build dir, ran `ninja -t clean`, and rebuilt all 2,610
edges into a fresh cache scope, as designed.
