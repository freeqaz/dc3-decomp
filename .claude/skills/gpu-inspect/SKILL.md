---
name: gpu-inspect
description: Analyze GFXReconstruct Vulkan captures. Show metadata, API call summaries, JSON export, SPIR-V shader extraction and disassembly. Use after gpu-capture to diagnose rendering issues.
argument-hint: "<command> [options] <capture.gfxr>"
allowed-tools: Bash, Read, Glob, Grep
---

# GPU Inspect Skill

Analyze GFXReconstruct (.gfxr) capture files. Extract metadata, Vulkan API call traces, shaders, and resource data.

**All Bash calls in this skill MUST use `dangerouslyDisableSandbox: true`** (GFXReconstruct tools need filesystem access the sandbox blocks).

## Arguments

`$ARGUMENTS`

## Commands

The wrapper script is at `scripts/gpu/inspect.sh`.

### info — Capture Metadata

Shows GPU, Vulkan version, memory allocations, pipeline counts.

```bash
scripts/gpu/inspect.sh info /tmp/capture.gfxr
```

Example output:
```
Vulkan application info:
  Engine name:         Dawn
  Target API version:  1.1.0
Vulkan physical device info:
  Device name:         NVIDIA GeForce RTX 3090
Vulkan pipeline info:
  Total graphics pipelines:   3
```

### summary — API Call Frequency

Lists all Vulkan API calls sorted by count.

```bash
scripts/gpu/inspect.sh summary /tmp/capture.gfxr
```

Example output:
```
     53 vkCmdPipelineBarrier
     34 vkCmdCopyBuffer
     28 vkUpdateDescriptorSets
      6 vkCreateShaderModule
      3 vkCmdDrawIndexed
=== Total calls: 431 ===
```

### convert — JSON Lines Export

Convert the binary capture to machine-readable JSON Lines format.

```bash
# Stream to stdout
scripts/gpu/inspect.sh convert /tmp/capture.gfxr

# Save to file
scripts/gpu/inspect.sh convert /tmp/capture.gfxr -o /tmp/trace.jsonl

# Pipe through jq for pretty-printing
scripts/gpu/inspect.sh convert /tmp/capture.gfxr | jq .
```

Each line is a JSON object:
```json
{"index":307,"function":{"name":"vkCmdDraw","thread":2,"args":{"commandBuffer":80,...}}}
```

This is ideal for scripted analysis — grep for specific calls, extract arguments, count patterns. You can pipe to `jq`, `grep`, or Python scripts.

### extract — SPIR-V Shader Extraction

Extract raw SPIR-V shader binaries from the capture.

```bash
# Extract to auto-generated temp dir
scripts/gpu/inspect.sh extract /tmp/capture.gfxr

# Extract to specific directory
scripts/gpu/inspect.sh extract /tmp/capture.gfxr -d /tmp/my_shaders
```

Shaders are named `sh<handle_id>` corresponding to `vkCreateShaderModule` handles. Use `spirv-dis` (from `spirv-tools` / `vulkan-tools`) to disassemble.

### calls — Filtered API Calls

List Vulkan calls matching a pattern.

```bash
# Find all draw calls
scripts/gpu/inspect.sh calls /tmp/capture.gfxr vkCmdDraw

# Find pipeline-related calls
scripts/gpu/inspect.sh calls /tmp/capture.gfxr Pipeline

# Find all commands
scripts/gpu/inspect.sh calls /tmp/capture.gfxr vkCmd
```

### shaders — Extract + Disassemble

Extract all shaders and show SPIR-V disassembly (first 30 lines each).

```bash
scripts/gpu/inspect.sh shaders /tmp/capture.gfxr
```

Example output:
```
=== sh38 (1972 bytes) ===
; SPIR-V
; Version: 1.4
; Generator: Google Tint Compiler; 1
OpCapability Shader
OpEntryPoint Vertex %83 "dawn_entry_point" %1 %8 %13 %gl_Position
...
=== 6 shaders extracted to /tmp/gfxr_shaders_12345 ===
```

Requires `spirv-dis` (install: `pacman -S spirv-tools`).

## Advanced Usage: Direct Tool Access

For operations not covered by the wrapper script, use the GFXReconstruct tools directly:

```bash
GFXR=../gpu/gfxreconstruct/build/tools

# Replay with screenshots
$GFXR/replay/gfxrecon-replay --swapchain offscreen --screenshots 1-5 \
  --screenshot-dir /tmp/frames --screenshot-format png capture.gfxr

# Replay with resource dump at specific draw calls
cat > /tmp/dump.json << 'EOF'
{"draw_call_indices": [307], "dump_depth": true, "dump_vertex_index_buffers": true}
EOF
$GFXR/replay/gfxrecon-replay --dump-resources /tmp/dump.json \
  --dump-resources-dir /tmp/dump capture.gfxr

# Replace shaders and replay
$GFXR/extract/gfxrecon-extract --dir /tmp/shaders capture.gfxr
# ... edit /tmp/shaders/sh<id> ...
$GFXR/replay/gfxrecon-replay --replace-shaders /tmp/shaders capture.gfxr
```

### Replay Notes

- In headless or no-compositor environments, replay screenshots usually need `--swapchain offscreen`. Without it, `gfxrecon-replay` may fail with `--wsi auto attempted to pick a surface, but no compositor was available`.
- Treat replay screenshots as diagnostic, not authoritative. They are useful for confirming that draws exist and roughly where they land, but they can differ from the live app on blend/composite fidelity.
- For DC3 UI work, compare replay screenshots against the app's own `MILO_SCREENSHOT_*` output. If the replay shows the draws but the live app still looks wrong, that usually points to higher-level state issues such as conflicting Flows/PropAnims rather than missing GPU work.

## Milo Engine Debugging Workflows

### "dc3-native renders but meshes are missing"
1. Capture: `MILO_RENDER=1 scripts/gpu/capture.sh -o /tmp/dc3.gfxr native/build/dc3-native`
2. Check draws: `scripts/gpu/inspect.sh calls /tmp/dc3.gfxr vkCmdDraw` — are draw calls present?
3. Check pipelines: `scripts/gpu/inspect.sh calls /tmp/dc3.gfxr Pipeline` — how many created?
4. Full trace: `scripts/gpu/inspect.sh convert /tmp/dc3.gfxr | grep vkCmdDraw` — inspect draw args

### "Blend mode looks wrong (alpha/additive/multiply)"
1. Capture both modes side by side:
   ```bash
   scripts/gpu/capture.sh -o /tmp/alpha.gfxr native/build/render-test --output /tmp/a.png --test alpha_blend
   scripts/gpu/capture.sh -o /tmp/additive.gfxr native/build/render-test --output /tmp/b.png --test additive_blend
   ```
2. Compare pipeline creation: `scripts/gpu/inspect.sh convert /tmp/alpha.gfxr | grep CreateGraphicsPipeline`
3. Extract shaders to check blend constants: `scripts/gpu/inspect.sh shaders /tmp/alpha.gfxr`

### "Dawn/WebGPU shaders are wrong"
Dawn compiles WGSL -> SPIR-V. Extract and inspect:
1. `scripts/gpu/inspect.sh shaders capture.gfxr` — disassemble all SPIR-V
2. Look for `dawn_entry_point` entry points (Dawn's naming convention)
3. Check descriptor set layouts (Set 0 = per-frame UBO, Set 1 = per-material, etc.)
4. Compare vertex input attributes against Milo's mesh vertex format

### "What changed between two renders?"
```bash
scripts/gpu/capture.sh -o /tmp/before.gfxr native/build/render-test --output /tmp/a.png --test solid_quads
# ... make code change ...
scripts/gpu/capture.sh -o /tmp/after.gfxr native/build/render-test --output /tmp/b.png --test solid_quads
scripts/gpu/inspect.sh convert /tmp/before.gfxr -o /tmp/before.jsonl
scripts/gpu/inspect.sh convert /tmp/after.gfxr -o /tmp/after.jsonl
diff <(grep '"name"' /tmp/before.jsonl) <(grep '"name"' /tmp/after.jsonl)
```

### "What Vulkan resources does a scene allocate?"
```bash
scripts/gpu/inspect.sh convert capture.gfxr | grep -E 'CreateImage|CreateBuffer|AllocateMemory' | jq .
```

## Source Code

- **GFXReconstruct tools**: `../gpu/gfxreconstruct/tools/` ([github.com/LunarG/gfxreconstruct](https://github.com/LunarG/gfxreconstruct))
  - `tools/info/` — gfxrecon-info source
  - `tools/convert/` — gfxrecon-convert source (JSON Lines format: `tools/convert/README.md`)
  - `tools/extract/` — gfxrecon-extract source
  - `tools/replay/` — gfxrecon-replay source
- **Resource dump docs**: `../gpu/gfxreconstruct/vulkan_dump_resources.md`
- **Full usage guide**: `../gpu/gfxreconstruct/USAGE_desktop_Vulkan.md`
