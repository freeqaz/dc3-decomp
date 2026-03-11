---
name: screenshot
description: Take screenshots of the native port engine or milo-viewer. Captures headless GPU-rendered PNG frames at specified frame numbers. Use when debugging UI layout, rendering, or verifying visual changes.
argument-hint: "[target] [frames] [output-dir]"
allowed-tools: Bash, Read, Glob
---

# Screenshot Skill

**All Bash calls in this skill MUST use `dangerouslyDisableSandbox: true`.** The script will detect blocked GPU access and tell you if you forgot.

## Usage

The `scripts/gpu/screenshot.sh` wrapper handles everything (env vars, headless mode, timeouts).

```bash
# dc3-native: default frames (10, 50, 100)
bash scripts/gpu/screenshot.sh native/build/dc3-native

# dc3-native: specific frames, custom output
bash scripts/gpu/screenshot.sh -f 100,500 -o /tmp/my_shots native/build/dc3-native

# milo-viewer: venue scene
bash scripts/gpu/screenshot.sh native/build/milo-viewer \
  ~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/world/glitterati/gen/glitterati.milo_xbox

# render-test: specific test scene
bash scripts/gpu/screenshot.sh native/build/render-test --test solid_quads
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o <dir>` | Output directory | `/tmp/dc3_screenshots` |
| `-f <frames>` | Comma-separated frame numbers | `10,50,100` |
| `-t <seconds>` | Timeout | `30` |
| `-w <WxH>` | Resolution | `1280x720` |

### dc3-native Frame Timing

| Frame | Screen |
|-------|--------|
| ~10 | Kinect loading (blue orb) |
| ~50 | attract_screen |
| ~100 | title_screen (DC3 logo) |
| ~250+ | choose_mode_screen (main menu) |

### Common Assets (milo-viewer)

```
~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/
  world/glitterati/gen/glitterati.milo_xbox   # venue
  world/dclive/gen/dclive.milo_xbox           # outdoor venue
  char/main/gen/main.milo_xbox                # character
```

### Draw Call Analysis

```bash
MILO_RENDER=1 MILO_HEADLESS=1 MILO_CAPTURE_FRAME=500 timeout 60 native/build/dc3-native 2>/tmp/frame_capture.txt
grep "DRAW\|SKIP" /tmp/frame_capture.txt
```

## Digging Deeper

If screenshots aren't working or you need to understand the rendering pipeline:

### How Screenshots Work

dc3-native screenshots use headless GPU rendering: draw to offscreen texture → GPU readback → PNG.

| Step | Code | What Happens |
|------|------|--------------|
| Env var parsing | `native/src/platform/Rnd_Wgpu.cpp:182-201` | Reads `MILO_SCREENSHOT_DIR`, parses frame CSV |
| Frame counter | `native/src/platform/Rnd_Wgpu.cpp:285` | `mFrameID++` in `BeginDrawing()` |
| Frame match check | `native/src/platform/Rnd_Wgpu.cpp:994-1033` | `MaybeCaptureFrame()` — compares mFrameID to target list |
| GPU readback | `native/src/gfx/GpuDevice.cpp:308-361` | `ReadbackHeadlessFrame()` — copies mHeadlessTex → staging buffer → CPU |
| PNG write | `native/src/gfx/Screenshot.cpp:10-31` | `stbi_write_png()` via `WritePNG()` |

### Why Xvfb Breaks Screenshots

`ReadbackHeadlessFrame()` reads from `mHeadlessTex` — an offscreen texture created only in headless mode (`GpuDevice.cpp:294-306`). With Xvfb, Dawn creates a swapchain instead, and `mHeadlessTex` is never allocated. The readback returns `false` silently.

### Key Data Structures

| Member | File | Purpose |
|--------|------|---------|
| `mScreenshotDir` | `native/src/platform/Rnd_Wgpu.h:302` | Output directory |
| `mCaptureFrames` | `native/src/platform/Rnd_Wgpu.h:303` | Frame number targets |
| `mCaptureIndex` | `native/src/platform/Rnd_Wgpu.h:304` | Current position in vector |
| `mHeadlessTex` | `native/src/gfx/GpuDevice.h` | Offscreen render target (headless only) |
| `mFrameID` | `native/src/platform/Rnd_Wgpu.h` | Global frame counter |

### Debug Labels

All WebGPU objects carry debug labels that propagate to Vulkan via `VK_EXT_debug_utils`:

| Label | Object | Code |
|-------|--------|------|
| `HeadlessTarget` | Offscreen texture | `GpuDevice.cpp` |
| `FrameEncoder` | Command encoder | `Rnd_Wgpu.cpp` |
| `MainPass` | Render pass | `Rnd_Wgpu.cpp` |
| `SceneUniforms` etc. | Uniform buffers | `Rnd_Wgpu.cpp` |
| `DefaultWhite` etc. | Default textures | `Rnd_Wgpu.cpp` |
| Mesh `Name()` | Vertex/index buffers | `Mesh_Wgpu.cpp` |
| `ShadowPass`, `ShadowDepth` | Shadow rendering | `ShadowPass.cpp` |
| `MainStatic`, `StandardShader` | Pipelines/shaders | `PipelineManager.cpp` |

Enabled by the `use_user_defined_labels_in_backend` Dawn toggle in `GpuDevice.cpp:135-141`.

### Script Source

- `scripts/gpu/screenshot.sh` — the wrapper script (auto-sets all env vars, detects GPU access)

### Render Pipeline Overview

```
WgpuRnd::BeginDrawing()       → mFrameID++, acquire headless texture
  ShadowPass::Render()        → shadow depth map
  WgpuRnd::DrawMeshImmediate()→ per-mesh draws (labeled vertex/index buffers)
WgpuRnd::EndDrawing()         → submit, MaybeCaptureFrame() → readback → PNG
```
