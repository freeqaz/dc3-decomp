# macOS ARM (Apple Silicon) Native Build

Status: **Implemented** (untested on real hardware — needs macOS validation)

## Overview

The native port supports x86_64 Linux and now has macOS ARM64 (Apple Silicon) support in CMakeLists.txt and platform code.

## What Changed

### Files Modified
1. **`native/CMakeLists.txt`** — Platform-conditional linker flags, libs, ObjC++ support, GCC-compat flag guards
2. **`native/src/gfx/GpuDevice.cpp`** — Added `#elif defined(__APPLE__)` surface creation via `SurfaceSourceMetalLayer`

### Files Added
3. **`native/src/gfx/MetalSurface.mm`** — ObjC++ helper: creates CAMetalLayer from NSWindow (required because CAMetalLayer is an ObjC class)

## What Already Works (No Changes Needed)

| Component | Why |
|---|---|
| **WebGPU rendering** (`*_Wgpu.cpp`) | Dawn supports Metal on macOS natively |
| **GLFW windowing** | GLFW supports macOS + Cocoa natively |
| **miniaudio** | Supports CoreAudio on macOS natively |
| **Engine code** (`src/`) | Platform-independent C++ |
| **Clang compiler** | macOS uses Apple Clang natively |
| **Signal handlers** (`execinfo.h`) | `backtrace()` exists on macOS (libSystem) |
| **ALSA stderr suppression** | POSIX `dup2()` is harmless no-op on macOS |
| **libjpeg, zlib, ogg/vorbis** | Available via Homebrew |

## Platform Differences

### Linker Flags
| Linux (GNU ld) | macOS (Apple ld) |
|---|---|
| `-rdynamic` | not needed (default) |
| `-Wl,--unresolved-symbols=ignore-all` | `-Wl,-undefined,dynamic_lookup` |
| `-Wl,--warn-unresolved-symbols` | n/a |
| `-Wl,--allow-multiple-definition` | not needed |

### Libraries
| Linux | macOS |
|---|---|
| `dl` (libdl) | Not needed (built into libSystem) |
| — | `-framework Cocoa` (GLFW) |
| — | `-framework IOKit` (GLFW) |
| — | `-framework Metal` (Dawn) |
| — | `-framework QuartzCore` (CAMetalLayer) |

### Compiler Flags
- `__GNUC_STDC_INLINE__` and `__GCC_ATOMIC_*` defines: Linux-only (GCC 15 libstdc++ compat), skipped on macOS (uses libc++)

## Building Dawn for macOS ARM64

Dawn must be built from source. One-time setup (~15 minutes):

```bash
# Prerequisites
brew install cmake ninja python3
pip3 install gclient  # or use depot_tools

# Clone and sync
git clone https://dawn.googlesource.com/dawn
cd dawn
cp scripts/standalone.gclient .gclient
gclient sync

# Build (Metal backend only)
cmake -B build-arm64 -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DDAWN_ENABLE_METAL=ON \
    -DDAWN_ENABLE_VULKAN=OFF \
    -DDAWN_ENABLE_DESKTOP_GL=OFF \
    -DDAWN_ENABLE_OPENGLES=OFF \
    -DDAWN_BUILD_SAMPLES=OFF \
    -DTINT_BUILD_TESTS=OFF \
    -DCMAKE_INSTALL_PREFIX=$HOME/dc3-deps/dawn

cmake --build build-arm64 -j$(sysctl -n hw.ncpu)
cmake --install build-arm64
```

## Building the Native Port

```bash
# Install dependencies
brew install cmake ninja glfw libjpeg-turbo libvorbis libogg curl pkg-config

# Configure (from repo root)
cd native
cmake -B build-macos -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DDawn_DIR=$HOME/dc3-deps/dawn/lib/cmake/Dawn \
    -DENABLE_FFMPEG=OFF \
    -DBUILD_TESTS=OFF

# Build
cmake --build build-macos

# Run milo-viewer
./build-macos/milo-viewer path/to/file.milo_xbox
```

## Risk Assessment

| Risk | Level | Notes |
|---|---|---|
| Dawn Metal backend | Low | Google's primary macOS path, well-tested |
| GLFW on macOS | Low | Mature, well-tested |
| miniaudio CoreAudio | Low | Primary macOS backend |
| `-fms-compatibility` on Apple Clang | Medium | May differ from upstream Clang in edge cases |
| `-undefined dynamic_lookup` | Medium | Behaves differently from GNU ld's unresolved symbol handling |
| BC texture compression | Low | Apple Silicon supports BC (all M-series GPUs) |

## Known Issues / TODO

- **Untested**: No macOS hardware available for validation yet
- **FFmpeg**: Disabled by default on macOS. Could enable with `brew install ffmpeg` + `-DENABLE_FFMPEG=ON`
- **GTest**: Disabled by default. Could enable with `brew install googletest` + `-DBUILD_TESTS=ON`
- **Apple Clang vs upstream Clang**: Apple Clang may have differences in `-fms-compatibility` behavior that cause compile errors in the decomp engine headers
