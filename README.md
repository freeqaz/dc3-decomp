Dance Central 3
[![Build Status]][actions] [![Code Progress]][progress] [![Discord Badge]][discord]
=============

[Build Status]: https://github.com/rjkiv/dc3-decomp/actions/workflows/build.yml/badge.svg
[actions]: https://github.com/rjkiv/dc3-decomp/actions/workflows/build.yml
[Code Progress]: https://decomp.dev/rjkiv/dc3-decomp.svg?mode=shield&measure=code&label=Code
[progress]: https://decomp.dev/rjkiv/dc3-decomp
[Discord Badge]: https://img.shields.io/discord/727908905392275526?color=%237289DA&logo=discord&logoColor=%23FFFFFF
[discord]: https://discord.gg/milohax

A decompilation of Dance Central 3 (build Sep 16 2012) for the Xbox 360.

This repository does **not** contain any game assets or assembly whatsoever. An existing copy of the game is required.

Sister project
==============

Personal-project counterpart to [RB3 decomp (Wii)](https://github.com/freeqaz/rb3) maintained by [@freeqaz](https://github.com/freeqaz) — same AI-assisted decomp methodology, shared Milo engine codebase. The decomp tooling stack (orchestrator MCP, Ghidra MCP, m2c integration, source permuter, slash commands, persistent agent memory) was developed here on the harder target — Xbox 360 MSVC PowerPC with no DWARF, ICF, and link-time pragmas — and ports across to RB3 (Wii Gekko/MWCC, full DWARF) with minor adaptation.

Cross-platform engine + game code (i.e. what actually runs in the WebGPU native port) is **~93% fuzzy-matched** here, with the Xbox 360 XDK excluded since it gets replaced in a native build. RB3 is at ~79% on the same basis. See [docs/SYNC_WITH_DC3.md in RB3](https://github.com/freeqaz/rb3/blob/master/docs/SYNC_WITH_DC3.md) for the cross-project sync plan.

Dependencies
============

Windows
--------

On Windows, it's **highly recommended** to use native tooling. WSL or msys2 are **not** required.  
When running under WSL, [objdiff](#diffing) is unable to get filesystem notifications for automatic rebuilds.

- Install [Python](https://www.python.org/downloads/) and add it to `%PATH%`.
  - Also available from the [Windows Store](https://apps.microsoft.com/store/detail/python-311/9NRWMJP3717K).
- Download [ninja](https://github.com/ninja-build/ninja/releases) and add it to `%PATH%`.
  - Quick install via pip: `pip install ninja`

macOS
------

- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages):

  ```sh
  brew install ninja
  ```

- Install [wine-crossover](https://github.com/Gcenx/homebrew-wine):

  ```sh
  brew install --cask --no-quarantine gcenx/wine/wine-crossover
  ```

After OS upgrades, if macOS complains about `Wine Crossover.app` being unverified, you can unquarantine it using:

```sh
sudo xattr -rd com.apple.quarantine '/Applications/Wine Crossover.app'
```

Linux
------

- Install [ninja](https://github.com/ninja-build/ninja/wiki/Pre-built-Ninja-packages).
- For non-`x86_64` platforms: Install wine from your package manager.
- For `x86_64`, [wibo](https://github.com/decompals/wibo), a minimal 32-bit Windows binary wrapper, will be automatically downloaded and used.

Building
========

- Clone the repository:

  ```sh
  git clone https://github.com/rjkiv/dc3-decomp.git
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

Once the initial build succeeds, an `objdiff.json` should exist in the project root.

Download the latest release from [encounter/objdiff](https://github.com/encounter/objdiff). Under project settings, set `Project directory`. The configuration should be loaded automatically.

Select an object from the left sidebar to begin diffing. Changes to the project will rebuild automatically: changes to source files, headers, `configure.py`, `splits.txt` or `symbols.txt`.

![](assets/objdiff.png)

Native Port
===========

A native x86_64 Linux port lives in `native/`. It renders the game using WebGPU (Dawn) instead of Xbox 360 DX9, with real audio playback, DTA script execution, and the full UI flow.

### Building

```sh
cmake -S native -B native/build -G Ninja
cmake --build native/build --target dc3-native -- -j$(nproc)
```

Requires extracted game assets in `orig-assets/` (see [native port status](docs/plans/dc3-native/STATUS.md)).

### Debug Overlay

Press **`~`** (tilde/backtick) at any time to toggle the ImGui debug overlay. This works during gameplay without pausing and provides real-time sliders for:

- **Camera Blend** -- enable/disable smooth camera transitions, adjust blend frame counts
- **FOV Scale** -- scale all camera field-of-view values (0.5x to 2.0x)
- **Clip Planes** -- override near/far plane distances
- **Aspect Ratio** -- force a specific aspect ratio
- **Camera Offset** -- shift the camera forward/back, up/down, left/right in view space

The debug overlay is also accessible from **Settings > Gameplay Settings > Debug Overlay** in the game menus.

### DTA Overlay System

Native-only features (like the Camera Blend toggle in Gameplay Settings) are injected via file overlays in `native/dta/`. These files shadow the `.ark` archive transparently -- no asset patching required. See [DTA Overlay docs](docs/native/dta/USAGE_GUIDE.md).

Questions?
==========
Please see [the FAQ](docs/FAQ.md).

Want to contribute?
===================
If you are interested in contributing, please see [the CONTRIBUTING walkthrough and guidelines](docs/CONTRIBUTING.md).
