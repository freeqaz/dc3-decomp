# GPU Debugging Tools

Tools in `../gpu/` for headless debugging of the native port's Vulkan/WebGPU rendering.

## Skills & Scripts

| Skill | Script | Purpose |
|-------|--------|---------|
| `/screenshot` | `scripts/gpu/screenshot.sh` | Take PNG screenshots (handles headless mode automatically) |
| `/gpu-capture` | `scripts/gpu/capture.sh` | Capture Vulkan API traces (GFXReconstruct) |
| `/gpu-inspect` | `scripts/gpu/inspect.sh` | Analyze captures: metadata, JSON, shaders |
| `/gpu-debug` | `scripts/gpu/rdc_capture.sh` | RenderDoc frame debugging (windowed apps) |

## Tool Overview

| Tool | Purpose | Headless? | Best For |
|------|---------|-----------|----------|
| **GFXReconstruct** | Capture/replay/analyze Vulkan API calls | Yes | Scriptable capture, API call analysis, shader extraction, JSON export |
| **RenderDoc** | Interactive frame debugger | Partial | Visual inspection, pipeline state, pixel history, shader debugging |
| **rdc-cli** | CLI wrapper for RenderDoc Python API | Partial | AI-assisted frame debugging via Claude Code |

**Key distinction**: GFXReconstruct captures at the Vulkan layer level (works with any app including headless), while RenderDoc requires a swapchain present call (`vkQueuePresentKHR`) to trigger capture. Our `render-test` is fully headless, so **GFXReconstruct is the primary tool** for automated/CI capture workflows.

## Building from Source

### GFXReconstruct

```bash
cd ../gpu/gfxreconstruct
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DGFXRECON_ENABLE_OPENXR=OFF
cmake --build build -j$(nproc)
```

Binaries land in `build/tools/` and `build/layer/`:
- `build/tools/info/gfxrecon-info` — capture file metadata
- `build/tools/convert/gfxrecon-convert` — JSON Lines export
- `build/tools/extract/gfxrecon-extract` — SPIR-V shader extraction
- `build/tools/replay/gfxrecon-replay` — replay with screenshots/resource dump
- `build/layer/libVkLayer_gfxreconstruct.so` — Vulkan capture layer

### RenderDoc

```bash
cd ../gpu/renderdoc
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QRENDERDOC=OFF -Bbuild -H.
cmake --build build -j$(nproc)
```

Binaries:
- `build/bin/renderdoccmd` — CLI for capture/replay/convert
- `build/lib/librenderdoc.so` — capture layer library
- `build/lib/renderdoc.so` — Python module (must match system Python version)

Register the Vulkan layer (one-time):
```bash
../gpu/renderdoc/build/bin/renderdoccmd vulkanlayer --register --user
```

### rdc-cli

```bash
pip install rdc-cli
```

Requires `renderdoc.so` built against the same Python version. Set `RENDERDOC_PYTHON_PATH` to the directory containing the module.

## GFXReconstruct Workflows

### capture.sh Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o <path>` | Output .gfxr file | `/tmp/gpu_captures/capture.gfxr` |
| `-f <frames>` | Frame range (needs Xvfb or display) | all |
| `-s <submits>` | Queue submit range (works headless) | all |
| `-q` | Quit after captured frames (requires `-f`) | off |
| `-t <seconds>` | Kill app after N seconds | none |
| `-x` | Use Xvfb virtual display | off |
| `-c <type>` | Compression: LZ4, ZSTD, ZLIB, NONE | ZSTD |
| `-l <level>` | Log level: debug, info, warning, error | warning |

### 1. Capture Headless (render-test)

```bash
scripts/gpu/capture.sh native/build/render-test --output /tmp/out.png --test solid_quads
```

### 2. Capture dc3-native (runs indefinitely)

dc3-native runs forever — use `-t` (timeout) or `-f ... -q` (frame range + quit):

```bash
# 60 seconds headless (~500MB with ZSTD)
MILO_RENDER=1 scripts/gpu/capture.sh -t 60 native/build/dc3-native

# Frame-accurate with Xvfb virtual display (auto-quit after frame 200)
MILO_RENDER=1 scripts/gpu/capture.sh -x -f 100-200 -q native/build/dc3-native

# Queue submit trimming headless (no display needed)
MILO_RENDER=1 scripts/gpu/capture.sh -s 50-150 -t 30 native/build/dc3-native
```

### Xvfb Virtual Display

Use `-x` to create a virtual X11 display via `xvfb-run`. This gives the app a real window so Dawn creates a swapchain, enabling frame counting (`-f`) and auto-quit (`-q`). Auto-enabled when `-f` is used without `$DISPLAY`.

Requires `xorg-server-xvfb` (`pacman -S xorg-server-xvfb`).

### Capture Size Estimates

With ZSTD compression, dc3-native generates ~8 MB/s of capture data. The script warns when estimated size exceeds 1GB. Use `-f` (frame range), `-s` (submit range), or shorter `-t` values to limit size.

### 3. Manual Capture (without script)

```bash
GPU_DIR=$(realpath ../gpu)

VK_LAYER_PATH="$GPU_DIR/gfxreconstruct/build/layer" \
VK_INSTANCE_LAYERS=VK_LAYER_LUNARG_gfxreconstruct \
GFXRECON_CAPTURE_FILE="/tmp/capture.gfxr" \
GFXRECON_LOG_LEVEL=warning \
  native/build/render-test --output /tmp/out.png --test solid_quads
```

### 2. Inspect Capture Metadata

```bash
gfxrecon-info capture.gfxr
```

Shows: application info, Vulkan physical device, memory allocations, pipeline counts, GPU details.

### 3. Convert to JSON Lines

```bash
# Full JSON export
gfxrecon-convert --output capture.jsonl capture.gfxr

# Stream to stdout for piping
gfxrecon-convert --output stdout capture.gfxr | jq .

# Summarize API call frequency
gfxrecon-convert --output stdout capture.gfxr 2>/dev/null \
  | grep -oP '"name"\s*:\s*"vk\w+"' | sort | uniq -c | sort -rn
```

Each line is a JSON object with `index` and either `function` (Vulkan call with args/return) or `meta` (capture metadata). This is machine-parseable — ideal for scripting or feeding to Claude.

### 4. Extract SPIR-V Shaders

```bash
mkdir -p /tmp/shaders
gfxrecon-extract --dir /tmp/shaders capture.gfxr

# Disassemble with spirv-dis (from vulkan-tools)
spirv-dis /tmp/shaders/sh38
```

Shaders are named `sh<handle_id>` matching their `vkCreateShaderModule` handle IDs. Use `spirv-dis` to inspect.

### 5. Replay with Screenshots

```bash
gfxrecon-replay --screenshots 1-5 --screenshot-dir /tmp/frames --screenshot-format png capture.gfxr
```

### 6. Replay with Resource Dump

Dump render targets, buffers, and descriptor bindings at specific draw calls:

```bash
# Create a dump config JSON
cat > /tmp/dump_config.json << 'EOF'
{
  "draw_call_indices": [307],
  "dump_resources_before": false,
  "dump_depth": true,
  "dump_vertex_index_buffers": true
}
EOF

gfxrecon-replay --dump-resources /tmp/dump_config.json --dump-resources-dir /tmp/dump capture.gfxr
```

### 7. Replace Shaders for Testing

Extract shaders, modify, and replay with replacements:

```bash
gfxrecon-extract --dir /tmp/shaders capture.gfxr
# Edit /tmp/shaders/sh<id> with spirv-as or glslang
gfxrecon-replay --replace-shaders /tmp/shaders capture.gfxr
```

## RenderDoc Workflows

RenderDoc requires a swapchain. For non-headless apps (e.g., `milo-viewer` with a window), it's the best tool for interactive debugging.

### Capture with renderdoccmd

```bash
ENABLE_VULKAN_RENDERDOC_CAPTURE=1 \
  ../gpu/renderdoc/build/bin/renderdoccmd capture \
  --wait-for-exit -c /tmp/capture.rdc \
  native/build/milo-viewer /path/to/scene.milo_xbox
```

### rdc-cli Inspection (requires Python-matched renderdoc.so)

```bash
export RENDERDOC_PYTHON_PATH=../gpu/renderdoc/build/lib

rdc doctor                          # verify setup
rdc open captures/frame.rdc         # load capture
rdc info --json                     # capture metadata
rdc draws --limit 20                # list draw calls
rdc pipeline 42                     # pipeline state at event 42
rdc rt 42 -o rt.png                 # export render target
rdc shader 42 vs --source           # vertex shader source
rdc pixel 256 256 42                # pixel history
rdc close                           # release GPU
```

### MCP Server for Claude Code Integration

The `renderdoc-skill` repo includes an MCP server exposing rdc-cli as tools:

```bash
pip install -r ../gpu/renderdoc-skill/requirements-mcp.txt
claude mcp add rdc-tools -- python ../gpu/renderdoc-skill/mcp_server/server.py
```

This gives Claude Code 13 native tools (`rdc_session`, `rdc_draws`, `rdc_pipeline`, `rdc_export`, `rdc_pixel`, etc.) plus 6 debugging recipe prompts. See `../gpu/renderdoc-skill/README.md` for full details.

## Practical Debugging Scenarios

### "Why is this mesh invisible?"

1. Capture with GFXReconstruct
2. Convert to JSON, grep for `vkCmdDraw` calls — verify the draw happened
3. Check pipeline state: culling mode, depth test, blend state
4. Extract and inspect vertex/fragment shaders

### "Why are colors wrong?"

1. Capture, convert to JSON
2. Find the draw call writing to the framebuffer
3. Dump resources at that draw call — inspect render target images
4. Check descriptor bindings: are the right textures bound?
5. Replace the fragment shader to test fixes without recompiling

### "What Vulkan calls does a frame make?"

```bash
gfxrecon-convert --output stdout capture.gfxr 2>/dev/null \
  | grep -oP '"name"\s*:\s*"vk\w+"' | sort | uniq -c | sort -rn
```

### "Are my shaders correct?"

```bash
gfxrecon-extract --dir /tmp/shaders capture.gfxr
for f in /tmp/shaders/sh*; do
  echo "=== $(basename $f) ==="
  spirv-dis "$f" | head -5
done
```

## Tool Paths (after build)

```bash
# GFXReconstruct
export GFXR=../gpu/gfxreconstruct/build
alias gfxrecon-info="$GFXR/tools/info/gfxrecon-info"
alias gfxrecon-convert="$GFXR/tools/convert/gfxrecon-convert"
alias gfxrecon-extract="$GFXR/tools/extract/gfxrecon-extract"
alias gfxrecon-replay="$GFXR/tools/replay/gfxrecon-replay"

# RenderDoc
alias renderdoccmd=../gpu/renderdoc/build/bin/renderdoccmd

# Capture layer path (needed for GFXReconstruct capture)
export VK_LAYER_PATH="$(realpath ../gpu/gfxreconstruct/build/layer)"
```
