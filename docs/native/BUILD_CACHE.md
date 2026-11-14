# Native build cache (ccache)

Each lane builds the same ~2,600 native translation units in its own worktree.
`scripts/native_configure.sh` therefore routes compiles through **ccache** when
it can find it, with one cache shared by every worktree on the box. A second
worktree at the same commit fetches almost every object from the cache instead
of compiling it. Numbers are in [Measured](#measured) below.

## Using it

Nothing to do. When `ccache` is on `PATH`, at `~/.local/bin/ccache`, or named by
`MILO_CCACHE_BIN`, the next configure turns it on. `native_test.sh` configures
for you, and so do `setup_worktree.sh` and a toolchain change. The configure
banner says what it chose:

```
    ccache     : /home/free/.local/bin/ccache -> /home/free/.cache/milo-native-ccache (MILO_CCACHE=0 to disable)
```

An existing build dir keeps its launcher until it is reconfigured. To switch
one over by hand, run `scripts/native_configure.sh` (no arguments for
`native/build`).

| Setting | Effect |
|---|---|
| `MILO_CCACHE=0` at configure time | off: plain compiles, as before |
| `MILO_CCACHE=1` | required: configure fails (exit 9) if ccache is missing |
| unset | on when ccache is found, otherwise off (never a hard dependency) |
| `CCACHE_DISABLE=1` at build time | bypass the cache for one `ninja` run, no reconfigure |
| `MILO_CCACHE_DIR` | cache directory (default `~/.cache/milo-native-ccache`) |
| `MILO_CCACHE_MAXSIZE` | cache size limit (default `16G`; a full set of objects for one commit is ~0.7 GB) |
| `MILO_CCACHE_BIN` | which ccache to use |

Hit rate: `ccache -d ~/.cache/milo-native-ccache --print-stats`, or `-s` for the
summary. Before the first build on a new box, install ccache user-level. On the
desk it is a static 4.14.2 build, minisign-verified, at
`~/.local/opt/ccache-4.14.2/` and symlinked from `~/.local/bin/ccache`. Any
ccache ≥ 4.8 works.

## What it took to make worktrees share

Done naively, ccache shares nothing between worktrees: every TU of this build
names its own tree in some absolute path. Each item below was measured as a
cross-tree miss before it was fixed.

- **`base_dir=<tree root>`** rewrites the tree's own absolute paths in the
  command line to relative ones before hashing. The engine checkout, Dawn and
  system headers are outside the tree and stay absolute; they are the same
  files for every worktree. `hash_dir=false` stops the working directory from
  going into the hash. Side effect: `__FILE__` is now **relative**
  (`../tests/foo.cpp`). Code must not locate files from `__FILE__`. The two
  tests that did now take `DC3_DECOMP_SRC_DIR`.
- **Absolute-path `-D`s are per-file.** `DC3_DECOMP_SRC_DIR` and the
  `MILO_TEST_*_STR` gate strings used to be target-wide on `milo-tests`, which
  put the worktree path into all 903 of its TUs. They are now
  `set_property(SOURCE ...)` on the six files that read them. Only those six
  miss across trees.
- **The precompiled headers.** A `.pch` embeds absolute paths to every header
  it read, so it differs in every tree, and ccache would key every PCH user on
  it. Three changes fix this:
  - CMake's generated `cmake_pch.hxx` includes `<decomp_pch.h>` by name, not
    by absolute path.
  - `-Xclang -fno-pch-timestamp` is added when ccache is on.
  - `scripts/native_ccache_launcher.sh` builds PCHs **outside** ccache (a
    fetched PCH would name another tree's headers). It then writes
    `<pch>.sum`: a digest of the compiler args and of every header's content,
    with the tree root replaced (`scripts/native_pch_sum.py`). ccache runs
    with `pch_external_checksum=true`, so users of the PCH key on that digest.
    The `.sum` is deleted before the PCH compile and written only after it
    succeeds. A missing `.sum` falls back to hashing the `.pch`: a miss, never
    a wrong hit.
- **`depend_mode=true`.** 22 decomp files carry `#line N "e:\lazer_build..."`
  directives. They make ccache's direct mode give up ("include file does not
  exist"), and the preprocessor fallback embeds absolute paths. Depend mode
  keys on the compiler's own depfile, which works.
- **`compiler_check=string:clang-<sha16>`**, a hash of the clang driver plus
  `libclang-cpp` and `libLLVM`, computed at configure time. ccache's default
  keys on mtime, and pacman restores upstream mtimes (CLAUDE.md, "The SYSTEM
  TOOLCHAIN is an input"). A toolchain upgrade reconfigures through
  `native_toolchain_check.py`, which recomputes the hash.
- `sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime`.
  `include_file_mtime`/`ctime` matter because a worktree checkout writes every
  header "just now".

`native_toolchain_check.py` is unaffected: it fingerprints the toolchain, not
the launcher.

## Measured

On the desk (32 threads), 2026-11-14, required gate targets (`dc3-native`,
`milo-tests`, `milo-viewer`, ...: 2,575 ninja edges, 2,596 cacheable compiles),
all builds `nice -n 10`. The desk was shared with ~10 other lanes and the
**load average was 75-200 throughout**, so wall times are an upper bound, not a
benchmark.

| Build | Wall | ccache |
|---|---|---|
| Worktree A, cold cache | 658 s (load 75→164) | 2,570 misses |
| Worktree B, same commit, warm cache | **66 s** (load ~150) | **2,590 hits / 6 misses (99.8%)** |
| One `.cpp` edited in B (`ErrorNode.cpp`, in 3 targets) | 42 s (load 115) | 3 misses, then 3 relinks |
| ... and reverted | 9.8 s | 3 hits, then 3 relinks |

The 6 misses in B are exactly the six TUs that carry a worktree path in a
`-D` (`test_gates.cpp` and the five `DC3_DECOMP_SRC_DIR` users), as expected.
In the warm build most of the 66 s went to the 4 PCHs, which are never cached,
and the links. The 81 tests that read the moved defines (`TestGates.*`,
`ObjectLifetimeTest.*`, `NativeOptionsTest.*`, `NativeTreePaths*`,
`NullThis*`, `PoseScoring*`) pass in a ccache-built tree.

Unrelated trap found while measuring: a worktree whose name has **upper-case**
letters (`np-bi-cacheB`) fails `NativeOptionsTest.*` and one
`ObjectLifetimeTest` with SIGSEGV. The engine lower-cases the overlay path
(`.../np-bi-cacheb/native/dta/...`), with or without ccache. Use lower-case
worktree names.

## What not to do

- Do not share one cache directory between machines with different
  toolchains. The compiler hash keeps that safe, but it is wasted space.
- Do not add a target-wide `-D` (or include path) that contains the worktree
  path. Use `set_property(SOURCE ... COMPILE_DEFINITIONS ...)` on the files
  that need it, or every TU of the target stops sharing.
- Do not derive file locations from `__FILE__` in native code or tests.
