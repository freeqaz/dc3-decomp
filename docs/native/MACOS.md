# macOS (Apple Silicon)

**State (2026-11-13): prepared from Linux, not yet built on a Mac.** The last
build on Mac hardware was March 2026. Everything below was fixed or checked
from Linux, with what that could and could not prove marked **MEASURED** /
**INFERRED**. Work items: [PLAN.md](PLAN.md) track M and P-8.

## The one command

From a `dc3-decomp` checkout on the Mac, next to a `milo-native-engine`
checkout (same parent directory):

```sh
scripts/mac/bootstrap.sh --fix
```

It checks every prerequisite, prints the command that fixes each missing one,
and with `--fix` runs the fixes it can (no sudo): `brew install`, the Dawn
download, the pose venv and model, unpacking an assets tarball. Then it
configures `native/build`, builds, and opens the game in a window. Without
`--fix` it only checks. Other flags: `--no-launch`, `--smoke` (the headless
YMCA flow the Linux gate runs, before the window), `--assets <dir | orig-assets.tar.xz>`,
`--jobs N`. Later launches: `scripts/start-mac.sh`.

## Prerequisites (what the script checks)

| Item | Check | Fix it prints / runs |
|---|---|---|
| Xcode command-line tools | `xcode-select -p`, `clang++` | `xcode-select --install` |
| Homebrew | `brew` on PATH (or `/opt/homebrew/bin/brew`) | the brew.sh installer line |
| Formulae | `cmake ninja pkgconf glfw jpeg-turbo libvorbis libogg ffmpeg coreutils` | `brew install …` |
| googletest | optional | present: `BUILD_TESTS=ON`, `milo-tests` is built |
| `../milo-native-engine` | contains engine `4e738fe` (libc++ shim, Apple flag exemptions, weak fallbacks) | `git clone` URL derived from dc3's `origin`, or `MILO_ENGINE_PATH` |
| Dawn | `../dawn-prebuilt/lib/cmake/Dawn/DawnConfig.cmake`, or `DAWN_PREFIX` | downloads Google's `v20260423.175430` `macos-latest-Release` tarball (9 MB, sha256-pinned; Intel: `macos-15-intel`) |
| Game data | `orig-assets/gen/main_xbox.hdr` or `DC3_DATA` | `--assets <dir>` links it; `--assets <orig-assets.tar.xz>` unpacks it (~25 GB); otherwise copy from the Linux box (`dc3-decomp-deps/orig-assets.tar.xz` is local-only there, not in git) |
| Pose (optional) | venv `~/.local/share/dc3/pose-venv` imports `mediapipe cv2 numpy`; `native/models/pose_landmarker_full.task` | venv + `pip install mediapipe==1.0.0 opencv-python numpy`; model from Google's storage, sha256-checked. The game runs without pose |

Notes:

- Dawn: the macOS tarball's `webgpu.h`/`webgpu_cpp.h` are byte-identical to
  the Linux build's (**MEASURED**). Its CMake package is a static
  `libwebgpu_dawn.a` (arm64 Mach-O) whose `dawn::webgpu_dawn` target already
  links Metal, QuartzCore, IOSurface, IOKit, Cocoa and Foundation.
- `native_configure.sh` no longer probes `dc3-decomp-deps/dawn` on a Mac. That
  is the **Linux** Dawn. It used to be found first, and a Mac that had run
  `setup-deps.sh` would have failed at link.
- `mediapipe` 1.0.0's macOS wheel is `py3-none-macosx_11_0_arm64`, so any
  Homebrew python3 works. A venv is needed because Homebrew's python refuses
  `pip install` into itself (PEP 668). The game runs the pose server as
  `python3` from PATH, so the launch puts the venv first.

## First run: what macOS will show

- **Camera permission, for your terminal app.** The webcam is opened by the
  Python pose server, a child of `dc3-native`, which is a child of your shell.
  macOS asks on behalf of the *responsible* app, so the dialog reads
  "Terminal" (or "iTerm", "Visual Studio Code", …) "would like to access the
  camera". OpenCV fails that first open while the dialog is up. The game then
  shows its "no camera" banner (pose server exit 3, PLAN P-2). Click Allow,
  quit the game (Esc or close the window), and run `scripts/start-mac.sh`
  again. **INFERRED** from how TCC and OpenCV's AVFoundation backend behave;
  not seen on this project's Mac.
  - Clicked Don't Allow: System Settings › Privacy & Security › Camera, enable
    the terminal app, then quit and reopen the terminal app.
  - Over SSH no dialog can appear and the camera is denied. Run it from a
    terminal on the Mac itself.
  - An iPhone near the Mac can show up as camera 0 (Continuity Camera). Then
    use `DC3_POSE_CAMERA=1 scripts/start-mac.sh`.
- **No .app bundle, on purpose.** `dc3-native` is a plain executable in
  `native/build/`, which every test, script and `NativeTreeDir()` expects. On
  macOS the link embeds an `Info.plist` into it (`__TEXT,__info_plist`:
  bundle id `com.milohax.dc3-native`, `NSCameraUsageDescription`,
  `NSHighResolutionCapable`). That only matters if `dc3-native` itself ever
  becomes the responsible process; from a terminal it is the terminal's
  description that is shown. A `.app` would need an ad-hoc-signed bundle, whose
  camera grant macOS may drop after every rebuild (the identity is the code
  hash). It is not worth that for a developer build.
- **Gatekeeper: none expected.** Locally built binaries and files fetched with
  `curl` carry no quarantine flag. A Dawn tarball or a repo copy fetched
  through a browser or AirDrop does. If macOS then says a file "cannot be
  opened", run `xattr -dr com.apple.quarantine <dir>`.
- **Firewall "accept incoming network connections?"**: only when the HTTP
  debug server is on (`DC3_HTTP=1`, e.g. `scripts/dc3-agent-test.sh`). A plain
  launch does not start it.
- **Xcode license**: if `clang` says the license is not accepted, run
  `sudo xcodebuild -license accept`. Only the full Xcode app needs this; the
  command-line tools do not.

## Not verifiable from Linux (watch for these)

| Risk | Where | Why it is open |
|---|---|---|
| Apple clang + `-fms-compatibility` vs the macOS SDK headers | `native/CMakeLists.txt` `DC3_MSVC_COMPAT_FLAGS` (the `-include stdarg.h -D_VA_LIST_T` Darwin pair) | Built in March; the SDK and Xcode have moved since |
| Apple ld with no relaxing flags | `native/CMakeLists.txt` Apple branch | `-ld_classic`, `-multiply_defined,suppress` and `-undefined,dynamic_lookup` were removed. Linux proves 0 duplicate definitions (LinkHygiene) and 0 undefined symbols under libc++. If the Mac link lists undefined symbols, report them; `-DDC3_MAC_DYNAMIC_LOOKUP=ON` is the stopgap |
| Metal backend, `MetalSurface.mm`, Retina scale, live resize | engine `src/gfx/` | The March fixes (`contentsScale`, resize, blend state) predate the engine move |
| Audio output | engine `AudioDevice.cpp`, miniaudio CoreAudio | miniaudio loads CoreAudio at run time (no framework link); device rate and latency untested |
| Gamepad | GLFW / IOKit HID | Mapping and any Input Monitoring prompt untested |
| ThreadCall worker | engine `ThreadCall_Native.cpp` | macOS has no unnamed POSIX semaphores (`sem_init` returns ENOSYS), so the worker never started and every ThreadCall job would wait forever. **Fixed on engine branch `np-macprep2` (`6e946f0`), not landed**: a mutex + condvar semaphore. It runs on Linux in every boot (gate below) |
| Exit without glibc `on_exit` | `native/src/main_native.cpp`, `NativeQuit.h` | **MEASURED on Linux with the macOS path forced** (`-DDC3_EXIT_HOOK_PORTABLE=1`): DTA `{exit}` rc 0 and port-in-use rc 98, no crash; the old report-only hook in the same build: rc 139. Apple's `atexit` order is the same C rule, not checked on a Mac |
| Closing the window | GLFW Cocoa | `WindowLoss_X11.cpp` is X11-only; on macOS the close button is GLFW's should-close, the clean path the Linux `WM_DELETE_WINDOW` test covers |
| Test harness | `scripts/native_test.sh` on macOS bash 3.2 / BSD tools | `--no-assets` is what the macOS CI job runs; the tests' `timeout` falls back to `gtimeout` (coreutils) |

Not problems: case-insensitive APFS (no case collisions in either repo or the
assets), endianness, `char` signedness (signed on Apple arm64, as on x86-64),
x86 intrinsics (none in compiled code), `/proc` (gone: `ExecutablePath.h` uses
`_NSGetExecutablePath`, RSS uses `task_info`), `<malloc.h>`.

## If it fails: what to send back

Every bootstrap run writes `native/build/mac-bootstrap/<stamp>/`:
`system.txt` (macOS, Xcode, clang, ld, brew, cmake, both repo HEADs),
`configure.log`, `build.log`, `game.log`, and one log per `--fix` step. Send
that directory, plus:

- `native/build/CMakeFiles/CMakeConfigureLog.yaml` if configure failed;
- for a crash: the `DC3_EXIT:` line at the end of `game.log`, and the newest
  `~/Library/Logs/DiagnosticReports/dc3-native-*.ips`;
- a screenshot if it draws wrongly.

```sh
tar -czf ~/Desktop/dc3-mac-logs.tgz -C native/build mac-bootstrap CMakeCache.txt
```

## Web on a Mac

The web build needs WebGPU **and** JSPI (`WebAssembly.Suspending`). Use
**Chrome or Edge 137+**, which have both on macOS by default. Safari 26 has
WebGPU but no JSPI. The page now says so and does not load the engine; before,
it failed inside the WASM instantiate with an error that named neither feature.
Firefox is not supported. See [WEB.md](WEB.md).

## Checking from Linux

- `scripts/mac/libcxx_proxy_build.sh [--smoke]`: builds `dc3-native`,
  `milo-viewer` and `render-test` against LLVM libc++. It checks from ninja's
  deps log that every object opening `wrap_iter.h` got the generated shim's
  copy. `--smoke` runs the headless YMCA flow in `DtaFlowTest`'s
  environment (GPU on, empty private cache) and requires `state=playing` on
  `game_screen` plus a `frame_cap_reached code=0` exit. Measured 2026-11-13:
  passes (2749 playing frames, 38 s). Run it under the GPU lock on a shared
  box. The extra
  `-l:libstdc++.so.6` it links is Linux-only (the Linux Dawn is built against
  libstdc++).
- ctest `LinkHygiene.*`: relinks `dc3-native` without
  `--allow-multiple-definition`. 0 duplicates since engine `4e738fe`.
- `MILO_BOOTSTRAP_PLATFORM=Darwin MILO_BOOTSTRAP_DRY_RUN=1 scripts/mac/bootstrap.sh --fix`
  and `MILO_CONFIGURE_PLATFORM=Darwin MILO_CONFIGURE_DRY_RUN=1 scripts/native_configure.sh`
  run the scripts' logic with every macOS probe printed instead of run.
- CI: `.github/workflows/native-macos.yml` (`macos-14`, arm64). Same steps as
  the bootstrap minus assets: Google's Dawn, `BUILD_TESTS=ON`, `dc3-native`
  `milo-viewer` `milo-tests`, then `scripts/native_test.sh --no-assets`
  against the shared floor. Validated with actionlint. Not yet run (not
  pushed, and the engine repo must be reachable from Actions:
  `vars.MILO_ENGINE_REPO`).
- Not available: cross-compiling with `--target=arm64-apple-macos` needs an
  Xcode SDK. An aarch64-linux build is a poor stand-in (different `char`
  signedness and variadic ABI).

## History

- March 2026: four commits on `origin/feature/mac` / `origin/some-mac-changes`,
  cherry-picked as `0a8f5c73a`, `8971b728e`, `7bc413130`. They fixed a BloomPass
  dangling blend state that crashed on arm64, Retina `contentsScale` and live
  resize, Dawn rejecting an `Undefined` blend state, and added `DC3_AUDIO_GAIN`.
- 2026-05-26: `GpuDevice.cpp`, `MetalSurface.mm` and `AudioDevice.cpp` moved
  into the engine (`c15b23a`).
- 2026-11-13 `np-macprep`: the generated libc++ iterator shim (`0bed35b3e`),
  `CharEyes.cpp` `::pow` (`5502e9e56`), `malloc_size` (`1edf01e06`), 17
  duplicate definitions removed (`ecfd3e581`), the Darwin branch in
  `native_configure.sh` (`8db01cdd7`), `ExecutablePath.h` (`c84d7ddae`). Engine
  `4e738fe` landed the engine halves (M-1, M-2, M-4).
- 2026-11-13 `np-macprep2`: this page's bootstrap, CI job, proxy script and
  the fixes listed above; see PLAN track M for commits.
