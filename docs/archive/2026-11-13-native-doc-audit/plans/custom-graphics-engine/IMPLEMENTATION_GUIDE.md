# Implementation Guide: Native Port

Step-by-step guide to building the DC3 native port. The goal is to get
Dance Central 3 running on modern platforms — this guide covers the rendering
foundation.

## Current Status

**Phase 0 complete, Phase 1 Track A & B complete, Phase 2 nearly complete, Phase 3 complete, Phase 4 complete.**

The full engine boots on x86_64 Linux, loads game data from `.ark` archives,
initializes all subsystems, navigates UI screens automatically via DTA scripts,
and runs 5000+ frames stably. Rendering pipeline operational with full material
pipeline (Blinn-Phong specular, emissive, rim lighting, intensify, multi-light).
Audio decoding complete (FFmpeg for Bink, Vorbis for OGG/MOGG). Input working
via GLFW (gamepad + keyboard-as-joypad). GTest integration test suite verifies
headless boot stability.

### Post-Processing Pipeline (Phase 5)

- **Bloom**: Complete. 4-level mip chain with 9-tap Gaussian blur, threshold extract, upsample chain with additive blending, composited in final post-proc pass. Driven by `RndPostProc::mBloomIntensity/mBloomThreshold/mBloomColor`.
- **Depth of Field**: Complete (shader + pipeline). Resolves MSAA depth to R32Float, 8-tap Poisson disc gather blur with circle-of-confusion from `DOFProc` focal plane/blur depth params. Runs before bloom in the post-proc chain.
- **Shadow Mapping**: Scaffolding in place (depth texture, comparison sampler, light VP matrix computation, shadow WGSL shader). **Not yet wired up** — needs shadow depth pipeline creation, caster geometry drawing, scene bind group expansion, and shadow sampling in the standard fragment shader. See [SHADOW_MAPPING.md](SHADOW_MAPPING.md) for detailed plan.
- **Existing post-proc effects**: Color grading (contrast/brightness/saturation/levels), chromatic aberration/sharpen, posterization, vignette.

**Batch screenshot script**: `native/scripts/render_screenshots.sh`

**Next**: Wire up shadow mapping (shadow caster draw pass, bind group 0 expansion, PCF sampling in standard shader). See [SHADOW_MAPPING.md](SHADOW_MAPPING.md).

## Prerequisites

### Dawn Source

Dawn should be cloned at `../dawn` (sibling to `dc3-decomp`):

```
/home/free/code/milohax/
  ├── dc3-decomp/    # this project
  └── dawn/          # git@github.com:google/dawn.git
```

### System Dependencies (Linux)

For headless only: just a Vulkan-capable GPU + driver.

For windowed mode (future): X11 or Wayland dev libraries.

```bash
# Arch Linux
sudo pacman -S libxrandr libxinerama libxcursor mesa libx11

# Ubuntu/Debian
sudo apt install libxrandr-dev libxinerama-dev libxcursor-dev mesa-common-dev libx11-xcb-dev
```

## Step 1: Build Dawn

```bash
cd /home/free/code/milohax/dawn

# Configure (fetches dependencies automatically)
cmake -S . -B out/Release -GNinja \
  -DDAWN_FETCH_DEPENDENCIES=ON \
  -DDAWN_ENABLE_INSTALL=ON \
  -DCMAKE_BUILD_TYPE=Release

# Build (~10-20 minutes first time, 2393 targets)
cmake --build out/Release

# Install headers + libs for external consumption
cmake --install out/Release --prefix install/Release
```

This produces `dawn/install/Release/` with:
- `include/` — WebGPU headers (`webgpu_cpp.h`, etc.)
- `lib/libwebgpu_dawn.a` — Main static library
- `lib/cmake/Dawn/` — CMake package config for `find_package(Dawn)`

**Note**: Dawn's install only exports `dawn::webgpu_dawn`. The GLFW integration
(`dawn_glfw`) is NOT part of the install — for windowed mode, either create
surfaces manually or use system GLFW with `FetchContent`.

## Step 2: Build dc3-native

```bash
cd /home/free/code/milohax/dc3-decomp/native

# Point CMake at Dawn's install tree
export CMAKE_PREFIX_PATH=/home/free/code/milohax/dawn/install/Release

# Configure
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build
```

**Required**: `find_package(Threads)` must come before `find_package(Dawn)`
because Dawn's installed target depends on `Threads::Threads`.

## Step 3: Run

```bash
# Headless (current) — renders to file
./build/dc3-native output.ppm

# Convert to PNG (optional)
magick output.ppm output.png
```

## Project Structure

```
native/
├── CMakeLists.txt              # Build system — finds Dawn, builds dc3-native + milo-viewer
├── scripts/
│   └── render_screenshots.sh   # Batch screenshot renderer
├── shaders/
│   └── standard.wgsl           # WGSL shader (diffuse+ambient+fog+alphatest)
└── src/
    ├── main_native.cpp         # Engine entry point (full game boot)
    ├── viewer/
    │   └── milo_viewer.cpp     # Milo Viewer — loads .milo_xbox, orbit camera, screenshots
    ├── gfx/
    │   ├── GpuDevice.h/.cpp    # WebGPU device, GLFW window, surface, headless readback
    │   ├── TextureConvert.h/.cpp  # Xbox 360 texture pipeline (byte-swap, untile, DXT decompress)
    │   ├── VertexFormats.h/.cpp   # GPU vertex layouts, unpack from RndMesh + compressed verts
    │   ├── PipelineManager.h/.cpp # Bind group layouts, pipeline cache, shader cache
    │   └── standard_wgsl.inc   # Embedded WGSL shader source
    ├── platform/
    │   ├── Rnd_Wgpu.h/.cpp     # WgpuRnd renderer (scene uniforms, ring buffers, draw orchestration)
    │   ├── Mesh_Wgpu.cpp       # RndMesh::DrawShowing (GPU upload, material bind groups)
    │   ├── Tex_Wgpu.cpp        # RndTex::PresyncBitmap (GPU texture upload)
    │   ├── Cam_Native.cpp      # RndCam::UpdateLocal (viewProj computation)
    │   └── ...                 # Other platform stubs
    └── ...
```

### Build Targets

```bash
# Full engine binary
cmake --build build --target dc3-native

# Milo viewer (loads .milo_xbox files, renders with orbit camera)
cmake --build build --target milo-viewer

# Usage
./build/milo-viewer --help                                              # Show options
./build/milo-viewer path/to/file.milo_xbox                              # Windowed mode
./build/milo-viewer path/to/file.milo_xbox --screenshot output.ppm      # Headless screenshot
./build/milo-viewer path/to/file.milo_xbox --azimuth 30 --elevation 15  # Custom camera angle
```

## Architecture Notes

### Why not use Dawn's SampleUtils?

Dawn's `SampleUtils.h` depends on Chromium internals (`partition_alloc/raw_ptr.h`)
that aren't available outside the Chromium build. We use the public API directly:
- `dawn/webgpu_cpp.h` — C++ WebGPU wrapper
- `dawn/webgpu_cpp_print.h` — `operator<<` for WebGPU types

### Dawn API Gotchas

**Include path**: Installed headers are under `dawn/` prefix, not `webgpu/`.
Use `#include <webgpu/webgpu_cpp.h>` which maps to `dawn/webgpu_cpp.h`.

**Shader source**: Use `wgpu::ShaderSourceWGSL` (not `ShaderModuleWGSLDescriptor`).

**Callbacks**: Modern Dawn uses `wgpu::StringView` in callbacks, not `const char*`.
The lambda-only overload (no userdata) is cleanest:
```cpp
instance.WaitAny(
    instance.RequestAdapter(&opts, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter result, wgpu::StringView msg) {
            adapter = std::move(result);
        }),
    UINT64_MAX);
```

**X11 conflicts**: If using GLFW native headers, `#undef Success` after
`#include <GLFW/glfw3native.h>` — X11 defines `Success` as 0, which breaks
`wgpu::RequestAdapterStatus::Success`.

**Texture readback alignment**: Row pitch must be 256-byte aligned for
`CopyTextureToBuffer`. Use `(width * 4 + 255) & ~255u`.

### Rendering Flow (Headless)

1. Create `wgpu::Instance` with `TimedWaitAny` feature
2. Request `wgpu::Adapter` (synchronous via `WaitAny`)
3. Create `wgpu::Device` + `wgpu::Queue`
4. Create offscreen `wgpu::Texture` as render target
5. Create WGSL shader module + render pipeline
6. Render to texture via command encoder
7. `CopyTextureToBuffer` → map buffer → read pixels → save image

### Rendering Flow (Windowed, future)

Same as above, but:
4. Create GLFW window + `wgpu::Surface` (X11: `SurfaceSourceXlibWindow`)
5. Configure surface with preferred format from `surface.GetCapabilities()`
8. Main loop: get surface texture → render → `surface.Present()`

## Troubleshooting

### "VulkanHeaders version not compatible"

Warning during Dawn configure — usually harmless, Dawn bundles its own headers.

### "No adapter found"

Ensure you have a Vulkan-capable GPU and driver. Check with `vulkaninfo`.

### "Device lost: Device was destroyed"

Normal on exit — just cleanup messaging when the device goes out of scope.

## File Index

| File | Contents |
|------|----------|
| [PLAN.md](PLAN.md) | Master plan — phased roadmap to get the game running |
| [APPROACH_WEBGPU.md](APPROACH_WEBGPU.md) | Why Dawn, API mapping, shader strategy |
| [INITIAL_IDEAS.md](INITIAL_IDEAS.md) | Architecture analysis and brainstorming |
| **IMPLEMENTATION_GUIDE.md** | **This file** — build instructions and API notes |
