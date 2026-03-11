---
name: screenshot
description: Take screenshots of the native port engine or milo-viewer. Captures headless GPU-rendered PNG frames at specified frame numbers. Use when debugging UI layout, rendering, or verifying visual changes.
argument-hint: "[target] [frames] [output-dir]"
allowed-tools: Bash, Read, Glob
---

# Screenshot Skill

Capture PNG screenshots from the DC3 native port or milo-viewer using headless GPU rendering.

## IMPORTANT: Sandbox

**You MUST skip the sandbox for GPU access.** The GPU (Vulkan ICD / Dawn) requires filesystem and device access that the sandbox blocks. Always use `dangerouslyDisableSandbox: true` for any command that runs the renderer.

## dc3-native (full game boot)

```bash
cd native/build
cmake .. && make -j$(nproc) dc3-native

# Take screenshots at frames 100, 300, 500
MILO_RENDER=1 \
MILO_SCREENSHOT_DIR=../../archive/screenshots/sessionNN \
MILO_SCREENSHOT_FRAMES=100,300,500 \
./dc3-native
```

### Environment Variables

| Variable | Description | Default |
|---|---|---|
| `MILO_RENDER=1` | **Required.** Enable GPU rendering (without this, no draws happen) |  |
| `MILO_SCREENSHOT_DIR=<path>` | Directory to save `frame_NNNNN.png` files | (none, no capture) |
| `MILO_SCREENSHOT_FRAMES=<csv>` | Comma-separated frame numbers to capture | `100,600,900,1500` |
| `MILO_CAPTURE_FRAME=<N>` | Capture a single frame + dump full draw call log to stderr | (none) |
| `MILO_HEADLESS=1` | Force headless mode (no window, useful for CI) | auto-detected |
| `MILO_SIMPLE_RENDER=1` | Debug: all prelit, skip multiply, black clear | (off) |

### Typical Frame Timing (boot flow)

| Frame | Screen | Notes |
|---|---|---|
| ~50 | attract_screen | Movie panel (no video) |
| ~100 | title_screen | DC3 logo, "HOW TO NAVIGATE" text |
| ~200 | wait_main_after_saveload_screen | Transition |
| ~250+ | choose_mode_screen | Main menu (DANCE, STORY, etc.) |

The boot flow uses hardcoded delays (see `UI.cpp` sFlow[]).

### Draw Call Analysis

To get a full draw call dump for a specific frame:
```bash
MILO_RENDER=1 MILO_CAPTURE_FRAME=500 ./dc3-native 2>/tmp/frame_capture.txt
# Then search: grep "DRAW\|SKIP" /tmp/frame_capture.txt
```

## milo-viewer (single .milo file)

```bash
cd native/build

# Screenshot mode (headless, saves PNG)
./milo-viewer path/to/scene.milo_xbox --screenshot output.png --frames 60

# Video mode (multiple frames)
./milo-viewer path/to/scene.milo_xbox --video output/ --frames 120
```

### Common test assets
```
~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/
  world/glitterati/gen/glitterati.milo_xbox   # venue with meshes/lights
  world/dclive/gen/dclive.milo_xbox           # outdoor venue
  char/main/gen/main.milo_xbox                # main character
```

## render-test (unit test scenes)

```bash
cd native/build
./render-test --test text_menu --output text_menu.png --frames 60
./render-test --test venue_with_ui --output venue_ui.png --frames 120
```

## Gotchas

1. **Sandbox blocks GPU** - Always `dangerouslyDisableSandbox: true`
2. **GLFW fails headless** - Normal: "GpuDevice: failed to initialize GLFW" means it falls back to headless rendering correctly
3. **Camera/pose server warnings** - Normal: camera/OpenCV errors are expected (no Kinect)
4. **0 mesh draw calls** - If you see this, `MILO_RENDER=1` is not set
5. **Build directory** - Screenshots use paths relative to `native/build/`. Use `../../archive/...` for the repo root
6. **CMake reconfigure** - After modifying .cpp files outside `native/src/`, run `cmake ..` before `make`
7. **Concurrent agents** - Other agents may modify shared .cpp files. Check `git diff` after build failures
