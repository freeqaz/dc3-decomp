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
| `--test [ARGS]` | run `scripts/native_test.sh` on tnr (see [Tests on tnr](#tests-on-tnr)) |
| `--sync-assets` | copy `orig-assets`, `archive` and `milo-rnd-library/dc3` to tnr (tests only) |
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
  | `assets/` | game data; only after `--sync-assets` |
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
`user-960.slice`.

**Do not** on tnr:

- raise `--jobs` past 64, or make 64 a default;
- start builds from loops;
- touch the window or pause flags;
- use `/srv/decomp`, `tnr`, `tnr-lane` or the GPUs;
- copy game assets anywhere but `/scratch/dc3-native-build/assets`.

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

See the section added by the test-host work below.

## Measured
