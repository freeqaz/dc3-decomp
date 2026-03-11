---
name: gpu-capture
description: Capture Vulkan API traces from the native port using GFXReconstruct. Works headless (no swapchain needed). Use when debugging rendering issues, analyzing GPU workload, or capturing frames for inspection.
argument-hint: "[options] <binary> [binary-args...]"
allowed-tools: Bash, Read, Glob, Grep
---

# GPU Capture Skill

Capture Vulkan API traces from the DC3 native port using GFXReconstruct's capture layer. Captures work with headless apps (no swapchain/window required), making this the primary tool for automated GPU debugging.

## IMPORTANT: Sandbox

**You MUST skip the sandbox for GPU access.** Both the Vulkan ICD and GFXReconstruct layer need filesystem access the sandbox blocks. Always use `dangerouslyDisableSandbox: true`.

## Arguments

`$ARGUMENTS`

## Quick Reference

The wrapper script is at `scripts/gpu/capture.sh`. It sets up environment variables for the GFXReconstruct layer and runs your binary.

### Milo Engine Scenarios

```bash
# Capture a render-test scene (headless, no swapchain)
scripts/gpu/capture.sh native/build/render-test --output /tmp/out.png --test solid_quads

# Capture dc3-native for 60 seconds headless (no display needed)
MILO_RENDER=1 scripts/gpu/capture.sh -t 60 native/build/dc3-native

# Capture dc3-native frames 100-200 with Xvfb (frame-accurate, auto-quit)
MILO_RENDER=1 scripts/gpu/capture.sh -x -f 100-200 -q native/build/dc3-native

# Capture dc3-native queue submits 50-150 headless (trim without display)
MILO_RENDER=1 scripts/gpu/capture.sh -s 50-150 -t 30 native/build/dc3-native

# Capture a venue scene in milo-viewer
scripts/gpu/capture.sh -o /tmp/gpu_captures/venue.gfxr native/build/milo-viewer \
  ~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/world/glitterati/gen/glitterati.milo_xbox \
  --screenshot /tmp/gpu_captures/venue.png --frames 60

# Capture a specific render-test blend mode (debugging blend state)
scripts/gpu/capture.sh -o /tmp/gpu_captures/blend.gfxr native/build/render-test \
  --output /tmp/gpu_captures/blend.png --test alpha_blend
```

### Available Test Scenes

| Binary | Scene | What It Tests |
|--------|-------|---------------|
| `render-test --test solid_quads` | 3 solid quads | Basic mesh + material pipeline |
| `render-test --test vertex_colors` | Per-vertex colors | Vertex attribute interpolation |
| `render-test --test alpha_blend` | Transparent blue over red | SrcAlpha blend mode |
| `render-test --test additive_blend` | Green over dark | Additive blend mode |
| `render-test --test multiply_blend` | Orange over white | Multiply blend mode |
| `render-test --test z_ordering` | Overlapping quads | Depth testing |
| `render-test --test text_basic` | Font rendering | RndText pipeline |
| `render-test --test text_menu` | DC3 main menu | Full UI layout |
| `render-test --test venue_with_ui` | Venue + UI overlay | Full scene composite |
| `milo-viewer <scene.milo_xbox>` | Any .milo scene | Full Milo engine rendering |
| `dc3-native` | Full game | Complete boot flow |

### Script Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o <path>` | Output .gfxr file path | `/tmp/gpu_captures/capture.gfxr` |
| `-f <frames>` | Frame range (e.g. `100-200`). Needs Xvfb or display | all |
| `-s <submits>` | Queue submit range (e.g. `50-150`). Works headless | all |
| `-q` | Quit after captured frames (requires `-f`) | off |
| `-t <seconds>` | Kill app after N seconds (for long-running apps) | none |
| `-x` | Use Xvfb virtual display (enables frame counting) | off |
| `-c <type>` | Compression: LZ4, ZSTD, ZLIB, NONE | ZSTD |
| `-l <level>` | Log level: debug, info, warning, error | warning |

### Capture Strategies for dc3-native

dc3-native runs indefinitely — you need a strategy to limit capture size (~8 MB/s with ZSTD):

| Strategy | Command | Use When |
|----------|---------|----------|
| **Timeout** | `-t 60` | Quick headless capture, ~500MB/min |
| **Frame range** | `-x -f 100-200 -q` | Frame-accurate, needs Xvfb |
| **Submit trim** | `-s 50-150 -t 30` | Headless trimming by queue submit |
| **Full boot** | `-x -t 30 -q` | Windowed mode, ~250MB |

### Manual Capture (without script)

If the script isn't suitable, set these env vars directly:

```bash
GPU_DIR=$(realpath ../gpu)

VK_LAYER_PATH="$GPU_DIR/gfxreconstruct/build/layer" \
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct \
GFXRECON_CAPTURE_FILE="/tmp/capture.gfxr" \
GFXRECON_LOG_LEVEL=warning \
  native/build/render-test --output /tmp/out.png --test solid_quads
```

### Environment Variables

Full list in [USAGE_desktop_Vulkan.md](../../../gpu/gfxreconstruct/USAGE_desktop_Vulkan.md).

Key variables:
- `GFXRECON_CAPTURE_FILE` — output path (supports `${AppName}` pattern)
- `GFXRECON_CAPTURE_FRAMES` — frame range (e.g. `1`, `100-200`)
- `GFXRECON_QUIT_AFTER_CAPTURE_FRAMES` — exit after capture (bool)
- `GFXRECON_CAPTURE_COMPRESSION_TYPE` — LZ4/ZSTD/ZLIB/NONE
- `GFXRECON_LOG_LEVEL` — debug/info/warning/error/fatal
- `GFXRECON_CAPTURE_TRIGGER` — hotkey to start/stop (F1-F12/TAB/CTRL)

### What Gets Captured

The .gfxr file contains every Vulkan API call the application makes:
- Instance/device creation, feature negotiation
- Memory allocations and buffer/image creation
- Pipeline creation (shader modules, render state)
- Command buffer recording (draws, dispatches, barriers)
- Resource updates (descriptor sets, buffer copies)

### Headless vs Windowed

| Mode | Trigger | Frame counting | Queue submit trim |
|------|---------|---------------|-------------------|
| **Headless** | No `$DISPLAY`, no `-x` | No (captures everything) | Yes (`-s`) |
| **Xvfb** | `-x` flag | Yes (`-f`, `-q` work) | Yes (`-s`) |
| **Native display** | `$DISPLAY` set | Yes (`-f`, `-q` work) | Yes (`-s`) |

`render-test` is always headless (no swapchain). `dc3-native` without `-x` or `$DISPLAY` falls back to headless.

## dc3-native Capture Tips

`dc3-native` is the full game binary and runs indefinitely. You **must** use `-t` (timeout) or `-f ... -q` (frame range + quit) to limit capture duration. `MILO_RENDER=1` is required for GPU rendering.

For exact UI debugging, pair the capture with the app's own screenshot path so you can compare "live native output" against the replayed Vulkan frame:

```bash
MILO_RENDER=1 \
MILO_SCREENSHOT_DIR=/tmp/dc3_frames \
MILO_SCREENSHOT_FRAMES=500 \
scripts/gpu/capture.sh -x -f 500-500 -q -t 90 \
  -o /tmp/dc3_frame500.gfxr native/build/dc3-native
```

This gives you:
- a trimmed `.gfxr` with the exact target frame
- a native app screenshot from the same frame for visual ground truth

### Xvfb Virtual Display

Without an X11 server, dc3-native runs headless (no swapchain). Use `-x` to start an Xvfb virtual display, which gives the app a real X11 window so Dawn creates a swapchain. This enables:
- Frame-based capture (`-f`) with accurate frame counting
- Auto-quit after capture (`-q`)
- Windowed-mode rendering (1280x720)

Xvfb is auto-enabled when `-f` is used without `$DISPLAY` set.

```bash
# Capture first 5 frames with Xvfb (windowed, auto-quit) — ~1.2MB
MILO_RENDER=1 scripts/gpu/capture.sh -x -f 1-5 -q native/build/dc3-native

# Capture 60 seconds headless (no display needed) — ~500MB
MILO_RENDER=1 scripts/gpu/capture.sh -t 60 native/build/dc3-native

# Capture queue submits 50-150 headless (trimmed, no display)
MILO_RENDER=1 scripts/gpu/capture.sh -s 50-150 -t 30 native/build/dc3-native

# Capture frames 100-200 with Xvfb (title screen → menu transition)
MILO_RENDER=1 scripts/gpu/capture.sh -x -f 100-200 -q native/build/dc3-native

# Simple render mode (prelit, no multiply, black clear)
MILO_RENDER=1 MILO_SIMPLE_RENDER=1 \
  scripts/gpu/capture.sh -x -t 30 native/build/dc3-native
```

### dc3-native Frame Timing (boot flow)

| Frame | Screen | Notes |
|-------|--------|-------|
| ~50 | attract_screen | Movie panel (no video) |
| ~100 | title_screen | DC3 logo, "HOW TO NAVIGATE" text |
| ~200 | wait_main_after_saveload | Transition |
| ~250+ | choose_mode_screen | Main menu (DANCE, STORY, etc.) |

## Known Gotchas

- A `.gfxr` file can still be valid even if `dc3-native` crashes during teardown under GFXReconstruct. We have seen post-capture Dawn/GFXReconstruct shutdown crashes after the target frame was already written. If the script prints a capture path, inspect the file before discarding the run.
- For `dc3-native`, `-x -f N-N -q` is the most reliable way to capture an exact boot/UI frame. Headless `-s` submit trimming is useful, but harder to align with named screens.
- Use the app screenshot as the visual source of truth. Replay screenshots are excellent for confirming draw-call presence and rough placement, but they can diverge from live native output in blend/composite fidelity.

## After Capture

Use the `/gpu-inspect` skill to analyze the capture:

```bash
# Show metadata
scripts/gpu/inspect.sh info /tmp/capture.gfxr

# Summarize API calls
scripts/gpu/inspect.sh summary /tmp/capture.gfxr

# Extract shaders
scripts/gpu/inspect.sh shaders /tmp/capture.gfxr
```

## Source Code

- **GFXReconstruct repo**: `../gpu/gfxreconstruct/` ([github.com/LunarG/gfxreconstruct](https://github.com/LunarG/gfxreconstruct))
- **Layer source**: `../gpu/gfxreconstruct/layer/`
- **Layer JSON**: `../gpu/gfxreconstruct/build/layer/VkLayer_gfxreconstruct.json`
- **Usage docs**: `../gpu/gfxreconstruct/USAGE_desktop_Vulkan.md`
- **Build docs**: `../gpu/gfxreconstruct/BUILD.md`

## Building (if not already built)

```bash
cd ../gpu/gfxreconstruct
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGFXRECON_ENABLE_OPENXR=OFF
cmake --build build -j$(nproc)
```
