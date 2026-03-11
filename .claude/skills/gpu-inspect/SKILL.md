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

The wrapper script is at `scripts/gpu/inspect.sh`. The query engine is at `scripts/gpu/query_trace.py`.

### info — Capture Metadata

Shows GPU, Vulkan version, memory allocations, pipeline counts.

```bash
scripts/gpu/inspect.sh info /tmp/capture.gfxr
```

### summary — API Call Frequency

Lists all Vulkan API calls sorted by count.

```bash
scripts/gpu/inspect.sh summary /tmp/capture.gfxr
```

### convert — JSON Export

Convert the binary capture to JSON format (pretty-printed JSON array).

```bash
# Save to file (recommended for large captures)
scripts/gpu/inspect.sh convert /tmp/capture.gfxr -o /tmp/trace.jsonl
```

**Important**: gfxrecon-convert outputs a pretty-printed JSON array (NOT JSON Lines), so files can be very large (100MB+). Use `query`, `pipelines`, `draws`, or `labels` commands to efficiently query the converted output instead of grepping raw JSON.

### pipelines — Pipeline Creation Details

**Recommended for blend/depth/rasterization debugging.** Shows all `vkCreateGraphicsPipelines` calls with human-readable output: shader stages, vertex input, topology, rasterization, depth/stencil, color blend state, and dynamic state.

```bash
scripts/gpu/inspect.sh pipelines /tmp/capture.gfxr
```

Example output:
```
=== Pipeline [2010] handle=1313 ===
  Stages: VERTEX, FRAGMENT
  Shader modules: [1310, 1311]
  Vertex binding 0: stride=64
    location=0 offset=0 R32G32B32_SFLOAT
    location=1 offset=12 R32G32B32_SFLOAT
  Topology: TRIANGLE_LIST
  Raster: cull=0x00000002 front=COUNTER_CLOCKWISE
  Depth: test=False write=False op=ALWAYS stencil=False
  Blend: logicOp=False, 1 attachment(s)
    [0] BLEND ON: color=SRC_ALPHA ADD ONE_MINUS_SRC_ALPHA | alpha=SRC_ALPHA ADD ONE_MINUS_SRC_ALPHA | mask=0x0000000f
```

Accepts .gfxr files (auto-converts, caches the .jsonl next to it).

### draws — Draw Call Listing

Shows all draw calls with their bound pipeline handle and render target dimensions. Cross-reference pipeline handles with `pipelines` output to see blend/depth state.

```bash
scripts/gpu/inspect.sh draws /tmp/capture.gfxr
```

Example output:
```
[19749] vkCmdDrawIndexed: 54 indices, 1 instances, first=0, pipeline=2417 render=1280x720
[19896] vkCmdDrawIndexed: 2943 indices, 1 instances, first=0, pipeline=2490 render=1280x720
```

### labels — Debug Labels

Show `vkSetDebugUtilsObjectNameEXT` calls (Dawn debug labels). Filter by substring.

```bash
# All labels
scripts/gpu/inspect.sh labels /tmp/capture.gfxr

# Filter to text-related objects
scripts/gpu/inspect.sh labels /tmp/capture.gfxr -g "Text"
```

### query — General Purpose Query

The Swiss Army knife. Query any API call by name, index, text content, or extract specific fields.

```bash
# Find specific API calls
scripts/gpu/inspect.sh query /tmp/capture.gfxr --call CreateGraphicsPipelines

# Show blend state from pipeline creation
scripts/gpu/inspect.sh query /tmp/capture.gfxr --call CreateGraphicsPipelines --field pColorBlendState

# Find draw calls in index range
scripts/gpu/inspect.sh query /tmp/capture.gfxr --call vkCmdDrawIndexed --range 5000-6000

# Show specific entry by index
scripts/gpu/inspect.sh query /tmp/capture.gfxr --index 2007

# Search for text in entries
scripts/gpu/inspect.sh query /tmp/capture.gfxr --grep "SRC_ALPHA"

# Compact one-line-per-match output
scripts/gpu/inspect.sh query /tmp/capture.gfxr --call vkCmdDrawIndexed --compact --limit 20

# Raw JSON output for piping to jq
scripts/gpu/inspect.sh query /tmp/capture.gfxr --call vkCmdDrawIndexed --raw | jq .
```

Query options:
- `--call <name>` — Filter by API call name (substring match)
- `--index <N>` or `--index <N-M>` — Show specific entry/entries
- `--range <MIN-MAX>` — Filter by index range
- `--grep <text>` — Search raw JSON text
- `--field <path>` — Extract a specific field (recursive search)
- `--limit <N>` — Max results
- `--compact` — One line per match
- `--raw` — Raw JSON (pipe-friendly)
- `--summary` — Count API calls by name

### extract — SPIR-V Shader Extraction

```bash
scripts/gpu/inspect.sh extract /tmp/capture.gfxr -d /tmp/my_shaders
```

### calls — Filtered API Call Counts

```bash
scripts/gpu/inspect.sh calls /tmp/capture.gfxr vkCmdDraw
```

### shaders — Extract + Disassemble

```bash
scripts/gpu/inspect.sh shaders /tmp/capture.gfxr
```

Requires `spirv-dis` (install: `pacman -S spirv-tools`).

## Auto-Conversion

The `query`, `pipelines`, `draws`, and `labels` commands accept either:
- A `.jsonl`/`.json` file (already converted)
- A `.gfxr` file (auto-converts, caches the result as `.jsonl` next to the original)

Subsequent runs reuse the cached `.jsonl` unless the `.gfxr` is newer.

## Debugging Workflows

### "Blend mode looks wrong (alpha/additive/multiply)"
```bash
# 1. Capture
scripts/gpu/capture.sh -o /tmp/blend.gfxr native/build/dc3-native

# 2. Show all pipeline blend states
scripts/gpu/inspect.sh pipelines /tmp/blend.gfxr

# 3. Show which draws use which pipeline
scripts/gpu/inspect.sh draws /tmp/blend.gfxr
```

### "What draws happen in a specific range?"
```bash
scripts/gpu/inspect.sh query /tmp/trace.gfxr --range 5000-6000 --call vkCmd --compact
```

### "What Vulkan resources does a scene allocate?"
```bash
scripts/gpu/inspect.sh query /tmp/trace.gfxr --call Create --compact
```

### "Compare two captures"
```bash
scripts/gpu/inspect.sh pipelines /tmp/before.gfxr > /tmp/p1.txt
scripts/gpu/inspect.sh pipelines /tmp/after.gfxr > /tmp/p2.txt
diff /tmp/p1.txt /tmp/p2.txt
```

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

- In headless or no-compositor environments, replay screenshots usually need `--swapchain offscreen`.
- Treat replay screenshots as diagnostic, not authoritative.
- For DC3 UI work, compare replay screenshots against the app's own `MILO_SCREENSHOT_*` output.

## Source Code

- **Wrapper script**: `scripts/gpu/inspect.sh`
- **Query engine**: `scripts/gpu/query_trace.py`
- **GFXReconstruct tools**: `../gpu/gfxreconstruct/tools/` ([github.com/LunarG/gfxreconstruct](https://github.com/LunarG/gfxreconstruct))
