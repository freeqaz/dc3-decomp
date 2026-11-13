# macOS (Apple Silicon)

**State (2026-11-13): ran on a real Mac in March 2026; would not build today.**
Nothing Mac-specific has been verified on hardware since 2026-03-18. This page is
the list of what broke since and the order to fix it in. Work items are tracked
in [PLAN.md](PLAN.md) (track M).

Tags: **[L]** fixable and checkable from Linux · **[Mac]** needs a real Mac.

## History

- The Mac work is four commits (2026-03-15 .. 03-18) on `origin/feature/mac` and
  `origin/some-mac-changes`, cherry-picked to main as `0a8f5c73a`, `8971b728e`,
  `7bc413130`. They fix things you only find by running: a BloomPass dangling
  blend state that crashed on arm64, Retina `contentsScale` and live resize,
  Dawn rejecting an `Undefined` blend state, `DC3_AUDIO_GAIN`.
- `GpuDevice.cpp`, `MetalSurface.mm` and `AudioDevice.cpp` moved into
  `../milo-native-engine` on 2026-05-26 (`c15b23a`). The engine CMake adds
  `MetalSurface.mm` under `if(APPLE)`. The copies still under `native/src/gfx/`
  and `native/src/audio/` in this repo are dead — no building target links them.

## What breaks today

| # | Breakage | Where | Fix | |
|---|---|---|---|---|
| 1 | The libc++ iterator shim `include/__iterator/wrap_iter.h` is one libc++ version's header plus one added conversion operator. Against a different libc++ it fails nearly every TU (measured on Linux with libc++ 23: 867 of 874 files). | both repos | Generate the shim at configure time from the toolchain's own `wrap_iter.h` | [L] then [Mac] |
| 2 | The Apple exemption from `-fms-compatibility` / the PCH names the dead dc3 copies (`native/CMakeLists.txt`, `if(APPLE)` block near the end of the native targets). The live engine files now get flags that break on macOS SDK headers. | dc3 + engine CMake | Move the exemption into the engine CMake; delete the dead copies | [L] then [Mac] |
| 3 | `src/system/obj/Dir.cpp` and `native/tests/test_native_memory_lp64.cpp` use `<malloc.h>` / `malloc_usable_size`. | dc3 | `<malloc/malloc.h>` / `malloc_size` under `__APPLE__` | [L] |
| 4 | 22 strong duplicate definitions at link (Linux hides them with `--allow-multiple-definition`; Apple ld errors). The loser is always a stub or fallback: 13 in `engine_stubs_generated.cpp`, `RndTex::{Load,PreLoad,PostLoad}`, `RndMesh::OnSync`, `TheUI`, `StreamReceiver::sFactory`, 2 in `SongSortMgr_Native`, `gDrawJmpBuf{,Set}`. | dc3 + engine | Delete or weaken the losers; make "links without `--allow-multiple-definition`" a Linux gate | [L] |
| 5 | `scripts/native_configure.sh` has no macOS path: it never looks for a Mac Dawn and refuses `BUILD_TESTS=OFF`. | dc3 | Add a Darwin branch | [L] then [Mac] |
| 6 | `/proc/self/exe` is used to find `pose_server.py` (`native/src/platform/Skeleton_Native.cpp`) and by six tests; `main_native.cpp` reads `/proc/self/statm` (fails soft). | dc3 | `_NSGetExecutablePath` under `__APPLE__` | [L] |
| 7 | In-process camera capture (`native/src/pose/CameraCapture.cpp`) is V4L2-only. Only matters for the in-process ncnn pose path; the Python pose server uses its own capture. See [POSE_INPUT.md](POSE_INPUT.md). | dc3 | AVFoundation backend | [Mac] |

Not problems: case-insensitive APFS (no case collisions in either repo or in the
asset tree), endianness, x86 intrinsics (none in compiled code), BC textures
(optional with fallback).

## Linux-side proxies

These catch most of the above without a Mac. Use them before asking for Mac time.

- **libc++ build on Linux** (`-stdlib=libc++`, shim regenerated for the local
  libc++): compiles, links with zero undefined symbols, and runs a headless flow
  to `game_screen`. Covers item 1 and general libc++ compatibility.
- **Relink `dc3-native` without `--allow-multiple-definition`**: reproduces the
  duplicate-symbol set exactly. Covers item 4.
- Not available: cross-compiling with `--target=arm64-apple-macos` needs an Xcode
  SDK. An aarch64-linux build is a poor stand-in (different `char` signedness
  and variadic ABI).
- The web build covers libc++ compatibility for emsdk's libc++ only, and nothing
  about linking, LP64, Metal, CoreAudio or Apple clang.

## Bring-up on a Mac

1. Xcode command-line tools; Homebrew
   `cmake ninja pkg-config glfw jpeg-turbo libvorbis libogg ffmpeg googletest`.
2. Clone `dc3-decomp` and `milo-native-engine` side by side; fetch `orig-assets`.
3. Dawn: use Google's prebuilt `macos-latest-Release` tarball from release
   `v20260423.175430` — its `webgpu.h` / `webgpu_cpp.h` are byte-identical to the
   Dawn the Linux build uses. Building Dawn from source is not needed.
4. Configure with `scripts/native_configure.sh` once item 5 lands; until then,
   plain `cmake -S native -B native/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
   -DDawn_DIR=<prefix>/lib/cmake/Dawn -DENABLE_NCNN=OFF`.
5. Build `dc3-native`. Apple-clang `-fms-compatibility` surprises surface here.
6. Run windowed from the repo root (`scripts/start-mac.sh`): check Metal surface
   sizing on Retina, CoreAudio output, keyboard/gamepad input.
7. Webcam: `pip install mediapipe opencv-python`, place
   `native/models/pose_landmarker_full.task`, grant Camera permission to the
   terminal. See [POSE_INPUT.md](POSE_INPUT.md).

## Open questions for the owner

- Which Mac / macOS / Xcode, and was the March build Apple clang or Homebrew llvm?
- Is a Metal Dawn still installed at `~/dc3-deps/dawn`?
- Python pose server acceptable on Mac, or is in-process capture wanted?
- May `milo-native-engine` be made reachable from GitHub Actions for a macOS
  compile+link job? (There is no macOS CI in either repo.)
