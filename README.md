Dance Central 3 — AI-Assisted Fork
==================================

> ⚠️ **Unofficial fork.** This repository is **not** the canonical DC3 decomp. It is a personal experiment by [@freeqaz](https://github.com/freeqaz) exploring how far AI agents can push a clean-room Xbox 360 decompilation project. Code, commits, decisions, tooling, and most of the documentation in this fork are **AI-assisted** (primarily via Claude Code). Do not assume anything here represents the views, code quality bar, or roadmap of the upstream maintainers.
>
> The **official, human-curated** project lives at **[rjkiv/dc3-decomp](https://github.com/rjkiv/dc3-decomp)**. If you want to contribute to or follow the canonical effort, please go there instead.
>
> No game assets, no Xbox 360 assembly, and no copyrighted binaries are stored in this repo. An existing copy of the game is required to do anything useful with it.

[![Discord Badge]][discord]

[Discord Badge]: https://img.shields.io/discord/727908905392275526?color=%237289DA&logo=discord&logoColor=%23FFFFFF
[discord]: https://discord.gg/milohax

What this fork is for
=====================

A decompilation of Dance Central 3 (build Sep 16 2012) for the Xbox 360, used here as a testbed for:

1. **AI-driven matching decomp** — Can a swarm of LLM agents, given enough structured tooling (Ghidra, objdiff, m2c, Unicorn, source permuter, RB3/RB2 ground truth, DWARF cross-reference), produce byte-matching MSVC-PPC C++ source at scale?
2. **A native port of the Milo engine** — A WebGPU (Dawn / Emscripten) port living in `native/` runs the decompiled engine on x86_64 Linux and in the browser, with real audio, DTA script execution, and the full UI flow. The native port exists partly because it surfaces correctness bugs in the decomp that pure objdiff matching cannot see.

Status snapshot (auto-generated, may be stale):

- ~**52%** fuzzy match across the whole binary, ~**58%** function count match.
- ~**93%** fuzzy match on cross-platform engine/game code (the subset that actually runs in the native port). Xbox 360 XDK code is excluded since the native port replaces it.
- Native port boots, renders, plays audio, and reaches gameplay venues with characters and IK.

For live numbers run `scripts/measure_progress.sh --functions --detailed HEAD`.

> **Correction (2026-08-17).** The snapshot above is stale *and* was measured on
> a different ruler. Measured 2026-08-17 on a fresh build: **91.21 % of
> authorable functions** (29,383 / 32,213) and **77.41 % of authorable code
> bytes**; whole-binary (XDK-diluted) **60.81 % functions / 43.18 % bytes**. The
> byte figures dropped ~2 pp in 2026-08 because the report switched from
> `functionRelocDiffs=None` to `name_check`, a stricter relocation ruler — that
> was a measurement correction, not lost code. Numbers from before the switch
> cannot be differenced against numbers after it. See
> [`docs/STATE_OF_THE_DECOMP.md`](docs/STATE_OF_THE_DECOMP.md).

Sister project
==============

Counterpart Wii decomp at **[freeqaz/rb3](https://github.com/freeqaz/rb3)** (Rock Band 3, Gekko/MWCC, full DWARF). Same AI-assisted methodology, shared Milo engine codebase. Most of the tooling described below was built here on the harder target — Xbox 360 MSVC PPC, no DWARF, ICF, and link-time pragmas — and ports to RB3 with minor adaptation. See [docs/SYNC_WITH_DC3.md in RB3](https://github.com/freeqaz/rb3/blob/master/docs/SYNC_WITH_DC3.md) for the cross-project sync plan.

How the AI tooling works
========================

The fork is organized around a small number of services that give LLM agents structured, reproducible access to the binary, the build, and prior-art codebases. Agents do not "look at assembly and guess" — they call typed tools that report match percentages, behavioral verdicts, struct offsets, and cross-references.

Decomp services
---------------

- **Orchestrator MCP** (`scripts/orchestrator/`) — central tool surface exposed to agents. Wraps objdiff, the build, the Ghidra service, Unicorn, m2c, and the RB3/RB2 cross-reference databases. Sample tools: `run_objdiff`, `run_diff_inspect`, `run_analyze_function`, `query_functions`, `lookup_rb3`, `lookup_struct_offset`, `get_rb2_class_info`, `lookup_merged_symbol`, `mark_patch_result`.
- **Ghidra MCP service** (`tools/ghidra/`, pyghidra-based) — headless Ghidra over HTTP. Provides decompiled C, switch-table/cast analysis (`switch_cast_inspect.py`, formerly `pcode_inspect.py`), genuine HIGH/RAW P-code export (`pcode_export.py` via `pcode-export.sh`), semantic search across 42k+ functions (`code_search.py`), and DTM-vs-header struct diffs (`struct_check.py`).
- **m2c** — machine-code-to-C decompiler for PPC, called by `run_analyze_function` to seed first-pass C from the original asm.
- **Unicorn function runner** (`scripts/unicorn_runner/`) — differential execution: runs target and decomp side-by-side on Unicorn PPC32 BE with synthesized inputs, classifies divergences as `logic` / `build_env` / `regalloc`. This is how the agents tell "real bug" from "harmless codegen drift."
- **Source permuter** (`scripts/permuter/`) — tries signed/unsigned, variable extraction, and other source variations to close the last few percent on stubborn functions.
- **RB2 DWARF + RB3 reference** — RB2 Wii ships with full DWARF, and RB3 (Wii) is ~54% matched in MWCC. Both are queried as ground truth for class layouts, local variable names/types, and shared engine implementations.
- **Compiler trace** (`msvc-src/`) — instrumented MSVC `c2.dll` for IL capture and codegen diffing on specific functions where output diverges in non-obvious ways.

Agent harness
-------------

- **`.claude/skills/`** — ~25 slash-command skills wrap the tools above into agent-callable verbs: `/recon`, `/permute`, `/batch-check`, `/ghidra-decompile`, `/ghidra-struct`, `/unicorn-query`, `/rb2-class`, `/rb3-pair`, `/vtable`, `/resolve-vcall`, `/stack-layout`, `/compare-asm`, `/refactor-staff`, `/screenshot`, `/gpu-capture`, etc.
- **Worktree pool** (`scripts/setup_worktree.sh`, `worktree_pool.py`) — concurrent agents work on isolated git worktrees that share the configured ninja build, tools, compilers, and target objects via symlinks. Avoids serializing on a single working tree.
- **Persistent memory** (`.claude/projects/.../memory/`) — index of prior-session findings (vtable layouts, AT_LIMIT divergence classes, known bugs, workflow lessons) that future sessions read before starting work.
- **Orchestrator batch loops** (`bin/orchestrate`) — strategies like `divergent` (fix Unicorn-flagged logic bugs first) or `batch --strategy` to drive sustained agent work without per-function prompting.

Native port debug surface
-------------------------

- **HTTP debug server** (`scripts/dc3-agent-test.sh`) — sets `DC3_HTTP=1 DC3_FAST_BOOT=1 DC3_TEL=1` and exposes `localhost:9090/api/{health,dta/eval,screenshot,...}` so agents can poke a running engine without a window manager. See [`docs/tools/HTTP_DEBUG_SERVER.md`](docs/tools/HTTP_DEBUG_SERVER.md).
- **ImGui debug overlay** — tilde toggles in-game; camera blend / FOV / clip planes / aspect ratio / view-space offset sliders.
- **DTA overlay system** — native-only DTA files in `native/dta/` shadow the `.ark` archive transparently. No asset patching. See [`docs/native/dta/USAGE_GUIDE.md`](docs/native/dta/USAGE_GUIDE.md).
- **GFXReconstruct + RenderDoc capture** (`/gpu-capture`, `/gpu-debug`, `/gpu-inspect`) — headless or windowed Vulkan capture and inspection. Used to compare native port rendering against Xenia traces.
- **Xenia harness** (`/xenia-gameplay`) — runs the original Xbox 360 debug XEX under Xenia (Linux/Vulkan) for ground-truth captures.

Where to read more
------------------

- [`docs/STATE_OF_THE_DECOMP.md`](docs/STATE_OF_THE_DECOMP.md) — **where the project actually is**: both headline metrics with their denominators, how to regenerate them, and what the remaining gap is made of.
- [`docs/decomp/REMAINING_WORK.md`](docs/decomp/REMAINING_WORK.md) — how to find work: queries, not worklists.
- [`docs/INDEX.md`](docs/INDEX.md) — full docs sitemap.
- [`docs/tools/INDEX.md`](docs/tools/INDEX.md) — agent tool selection guide and workflow.
- [`docs/decomp/TECHNICAL_NOTES.md`](docs/decomp/TECHNICAL_NOTES.md) — compiler quirks and matching patterns.
- [`docs/plans/dc3-native/STATUS.md`](docs/plans/dc3-native/STATUS.md) — native port status.

Dependencies
============

Windows
-------

On Windows, native tooling is **highly recommended**. WSL and msys2 are **not** required. Under WSL, [objdiff](#diffing) cannot get filesystem notifications for automatic rebuilds.

- Install [Python](https://www.python.org/downloads/) and add it to `%PATH%`.
  - Also available from the [Windows Store](https://apps.microsoft.com/store/detail/python-311/9NRWMJP3717K).
- Download [ninja](https://github.com/ninja-build/ninja/releases) and add it to `%PATH%`.
  - Quick install via pip: `pip install ninja`

macOS
-----

- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages):

  ```sh
  brew install ninja
  ```

- Install [wine-crossover](https://github.com/Gcenx/homebrew-wine):

  ```sh
  brew install --cask --no-quarantine gcenx/wine/wine-crossover
  ```

After OS upgrades, if macOS complains about `Wine Crossover.app` being unverified, unquarantine it with:

```sh
sudo xattr -rd com.apple.quarantine '/Applications/Wine Crossover.app'
```

Linux
-----

- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages).
- For non-`x86_64` platforms: install wine from your package manager.
- For `x86_64`: [wibo](https://github.com/decompals/wibo), a minimal 32-bit Windows binary wrapper.

  The auto-download is **dead code on Linux.** `configure.py` always sets
  `--wrapper`/`--wibo` (default `../wibo/build/release/wibo`), which makes
  `use_wibo()` false, which removes the download edge — so `wibo_tag` is never
  consulted and `build/tools/wibo` is never fetched. Clone
  <https://github.com/freeqaz/wibo> next to this repo and build it:
  `cmake --preset release64-clang && cmake --build --preset release64-clang`,
  then install the result as `build/release/wibo`. Pass `--wibo /path/to/wibo`
  to use one elsewhere.

  Use the **fork**, not stock upstream: the generated `msvc` rule relies on
  `WIBO_FS_CACHE`, `WIBO_REWRITE_SHOWINCLUDES` (this is what makes ninja's
  `/showIncludes` header tracking work without `transform_dep.py`),
  `WIBO_PATH_MAP` and `WIBO_COMPUTER_NAME`, none of which exist upstream. The
  binary in use on the reference machine is `wibo 1.2.0-c2rs.1`; check with
  `../wibo/build/release/wibo --version`.

Building
========

- Clone the repository (this fork or upstream — same build steps):

  ```sh
  git clone https://github.com/freeqaz/dc3-decomp.git
  ```

- Copy the xex to `orig/373307D9`.

- Configure:

  ```sh
  python configure.py
  ```

- Build:

  ```sh
  ninja
  ```

Diffing
=======

Once the initial build succeeds, an `objdiff.json` exists in the project root.

Download the latest release from [encounter/objdiff](https://github.com/encounter/objdiff). Under project settings, set `Project directory`. The configuration should be loaded automatically.

Select an object from the left sidebar to begin diffing. Changes to source files, headers, `configure.py`, `splits.txt`, or `symbols.txt` trigger automatic rebuilds.

![](assets/objdiff.png)

Native Port
===========

A native x86_64 Linux port lives in `native/`. It renders the game via WebGPU (Dawn) instead of Xbox 360 DX9, with real audio playback, DTA script execution, and the full UI flow. An Emscripten/WASM build is also wired up (`scripts/web/build.sh`).

Building
--------

```sh
cmake -S native -B native/build -G Ninja
cmake --build native/build --target dc3-native -- -j$(nproc)
```

Requires extracted game assets in `orig-assets/` (see [native port status](docs/plans/dc3-native/STATUS.md)).

Web build:

```sh
scripts/web/build.sh                       # dual release/debug deploy
python3 native/web/server.py --port 8420   # / = release, /?debug=true = debug
```

Debug Overlay
-------------

Press **`~`** (tilde) any time to toggle the ImGui debug overlay. Works during gameplay without pausing; provides real-time sliders for:

- **Camera Blend** — enable/disable smooth transitions, adjust blend frame counts
- **FOV Scale** — scale all camera FOV values (0.5x to 2.0x)
- **Clip Planes** — override near/far plane distances
- **Aspect Ratio** — force a specific aspect ratio
- **Camera Offset** — shift the camera forward/back, up/down, left/right in view space

Also accessible from **Settings > Gameplay Settings > Debug Overlay** in-game.

Questions?
==========

See [the FAQ](docs/FAQ.md). For anything fork-specific (AI tooling, native port, RB3 sync), open an issue against this repo rather than upstream.

Contributing
============

This fork is a personal sandbox and accepts PRs at the maintainer's discretion. For the canonical project — which is where you almost certainly want to contribute — see [the upstream CONTRIBUTING guide](https://github.com/rjkiv/dc3-decomp/blob/main/docs/CONTRIBUTING.md). The local [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) walkthrough is preserved for reference.
