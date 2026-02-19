# Vulkan Headless Rendering & Frame Capture

**Date:** 2026-02-18
**Outcome:** Frame capture pipeline working - GPU readback produces PPM files from Vulkan rendering
**Status:** RESOLVED — warmup frame capture produces full rendered screenshots of DC3 boot animation
**See also:** [2026-02-18-xenia-screenshot-breakthrough.md](2026-02-18-xenia-screenshot-breakthrough.md)

## Goal

Get xenia-headless to produce actual rendered frame captures from DC3 using the Vulkan GPU backend, replacing the null GPU backend that produces no visual output.

## Background

Previous sessions established:
- Null GPU headless mode: boots DC3, runs 600+ VdSwap calls in 20s (no rendering)
- Vulkan GPU headless mode: boots DC3 but deadlocks after ~12 swaps when draws are enabled
- The deadlock occurs because shader compilation (10-100ms per new shader) blocks the CP thread from processing synchronization packets

## Architecture

### The CP Thread Deadlock Problem

The Xbox 360 Command Processor (CP) thread processes PM4 packets sequentially:
1. Game submits draw commands via ring buffer
2. CP processes draws (which now require Vulkan shader compilation + pipeline creation)
3. CP processes EVENT_WRITE_SHD (writes sync counter to memory)
4. Game thread polls sync counter, unblocks, submits next frame

When shader compilation takes 10-100ms per draw, the CP can't process sync packets fast enough. The game thread waits forever for sync values that never appear.

### Solution: Async Pipeline Compilation

Instead of blocking on pipeline creation, compile pipelines on background threads and skip draws until their pipeline is ready:

```
CP Thread (fast path):
├─ DRAW → check if pipeline ready
│   ├─ YES → execute draw normally
│   └─ NO  → skip draw, queue pipeline compilation on background thread
├─ EVENT_WRITE_SHD → process immediately (write sync value)
├─ PM4_INTERRUPT → process immediately (fire callback)
└─ XE_SWAP → EndSubmission(true) to flush pending Vulkan work
```

This allows the CP to keep up with sync packets. Early frames may have missing geometry (pipelines not yet compiled), but the game doesn't deadlock.

### Frame Capture: GPU Readback

The base class `CommandProcessor::ExecutePacketType3_XE_SWAP` has a raw framebuffer dump that reads from host memory. This doesn't work for Vulkan because:
- Vulkan resolve writes to `shared_memory.buffer()` which uses **device-local** GPU memory
- The host CPU reads guest physical memory via `xe::Memory`, which is a separate memory space
- Result: raw dumps are always all-zero (black frames)

Solution: Override `HandlesFrameDump()` and implement proper Vulkan readback:
1. Call `RequestSwapTexture()` to get the swap VkImage
2. Create a host-visible staging buffer
3. Use one-shot command buffer to copy VkImage → staging buffer
4. Map staging buffer to CPU, write PPM file

## Implementation

### Files Modified

| File | Changes |
|------|---------|
| `vulkan_pipeline_cache.h` | +26 lines: `SetHeadlessMode()`, `PendingPipeline` struct, pending pipeline map |
| `vulkan_pipeline_cache.cc` | +115 lines: Async `ConfigurePipeline` path, background thread pool, `Shutdown` cleanup |
| `vulkan_command_processor.h` | +6 lines: `HandlesFrameDump()` override, `headless_frame_dump_` flag, frame counter |
| `vulkan_command_processor.cc` | +228 lines: `SetHeadlessMode(true)` in `SetupContext`, GPU readback in `IssueSwap` headless path |

### Key Code: Async Pipeline Compilation (vulkan_pipeline_cache.cc)

In `ConfigurePipeline()`, when headless mode is enabled:
```cpp
if (headless_mode_) {
  // Check if pipeline is already being compiled
  auto it = pending_pipelines_.find(pipeline_key);
  if (it != pending_pipelines_.end()) {
    if (it->second.ready.load()) {
      // Pipeline compiled, use it
      pipeline = it->second.pipeline;
    } else {
      // Still compiling, skip this draw
      return false;
    }
  } else {
    // Queue async compilation
    auto& pending = pending_pipelines_[pipeline_key];
    pending.ready.store(false);
    // Launch background thread...
    std::thread([this, pipeline_key, create_info]() {
      VkPipeline p;
      vkCreateGraphicsPipelines(..., &p);
      auto it = pending_pipelines_.find(pipeline_key);
      it->second.pipeline = p;
      it->second.ready.store(true);
    }).detach();
    return false;  // Skip this draw
  }
}
```

### Key Code: GPU Readback (vulkan_command_processor.cc)

In `IssueSwap()` headless path:
```cpp
if (!presenter) {
  // Flush pending GPU work
  if (submission_open_) EndSubmission(true);

  // Frame capture
  if (headless_frame_dump_) {
    AwaitAllQueueOperationsCompletion();

    // Load swap texture
    BeginSubmission(true);
    VkImageView swap_view = texture_cache_->RequestSwapTexture(...);
    EndSubmission(true);
    AwaitAllQueueOperationsCompletion();

    VkImage swap_image = texture_cache_->GetLastSwapImage();
    // Create staging buffer, copy image, map, write PPM...
  }
  return;
}
```

### Vulkan API Patterns (Xenia-specific)

These patterns differ from standard Vulkan and caused multiple build failures:

| Operation | Standard Vulkan | Xenia Pattern |
|-----------|----------------|---------------|
| Physical device memory props | `vkGetPhysicalDeviceMemoryProperties(physDevice, ...)` | `vulkan_device->vulkan_instance()->functions().vkGetPhysicalDeviceMemoryProperties(vulkan_device->physical_device(), ...)` |
| Queue submission | `vkQueueSubmit(queue, ...)` | `auto q = vulkan_device->AcquireQueue(family_idx, 0); dfn.vkQueueSubmit(q.queue(), ...)` (RAII lock) |
| Device functions | `vkCreateBuffer(device, ...)` | `dfn.vkCreateBuffer(device, ...)` where `dfn = vulkan_device->functions()` |
| Command buffer | `DeferredCommandBuffer` | Has `CmdVkCopyBuffer` and `CmdVkCopyBufferToImage` but NOT `CmdVkCopyImageToBuffer` |

## Test Results

### Build
```bash
cd ~/code/milohax/xenia/build
make xenia-headless config=checked_linux -j$(nproc)
# Success
```

### Run (20 second timeout)
```bash
./xenia-headless --gpu=vulkan --dump_frames_path=/tmp/frames/ \
  --headless_timeout_ms=20000 --target=.../default.xex
```

| Frame | Resolution | Non-zero pixels | Content |
|-------|-----------|----------------|---------|
| 1-4 | 1280x720 | 0% | Black (pre-render boot) |
| 5 | 1280x720 | 100% | Solid R=76 G=0 B=0 (dark red/blue) |
| 6 | 1280x720 | 100% | Solid R=2 G=1 B=2 (near-black) |

- 6 VdSwap calls in 20 seconds (~0.3 fps)
- **No deadlock** - game ran full 20 second timeout
- Pipeline creation messages appear between frames 4-5
- Solid-color frames suggest early boot clear screens (game hasn't loaded assets yet)
- Performance limited by synchronous shader compilation on first encounter

### Extended Test (120 second timeout, no readback)

```
11 XE_SWAP (boot, no draws)
9x "Creating graphics pipeline state" (shader compilation burst)
1 XE_SWAP (first frame with actual Vulkan draws)
... game essentially frozen for remaining ~100 seconds
TIMEOUT: 120000ms reached
```

Only 12 swaps in 120 seconds. After pipelines are compiled, draw execution itself becomes the bottleneck: each Vulkan draw call takes ~1ms, and with 2000+ draws per frame, a single frame takes seconds.

### Comparison: All Configurations

| Configuration | Swaps/20s | Swaps/120s | Frame Content | Deadlock? |
|---|---|---|---|---|
| Null GPU (skip all) | 600+ | ~3600 | N/A | No |
| Draw skip + copy only | 611 | ~3600 | Uninitialized EDRAM | No |
| All draws + async pipelines | ~9 | 12 | Solid colors (early boot) | No |
| All draws + async + readback | ~5 | N/A | Solid colors (early boot) | No |
| All draws, no async | 12-13 | N/A | N/A | **Yes** |

### Root Cause: Draw Execution Time

The fundamental bottleneck is not shader compilation (one-time cost) but draw execution itself:
- **Null GPU**: 0ms per draw (skip everything)
- **Vulkan draw**: ~1ms per draw (pipeline bind + vertex setup + vkCmdDraw)
- **DC3 typical frame**: ~2000 draw calls
- **Time per Vulkan frame**: ~2000ms (2 seconds!)

This means Vulkan rendering at native draw counts produces ~0.5 fps. To get useful frame captures, we need either:
1. Very long runtimes (10+ minutes to reach game content)
2. Selective draw execution (skip most draws, execute critical ones)
3. Frame-selective rendering (skip draws for N frames, render frame N+1)

## Approaches Tried and Abandoned

### 1. Async Draw Worker Thread (Abandoned)
**Idea:** Queue all non-copy draws to a background thread, let CP thread process sync packets immediately.
**Problem:** Complex thread safety issues - the render worker needs its own shadow register file, and register writes must be queued. Guest memory races (game might modify vertex/texture data before render worker reads it). Abandoned in favor of simpler async pipeline compilation.

### 2. VMA (Vulkan Memory Allocator) for staging buffer (Failed)
**Idea:** Use VMA `vmaCreateBuffer()` for the staging buffer allocation.
**Problem:** VMA allocator is private to `VulkanTextureCache`, not accessible from `VulkanCommandProcessor`. No global `allocator()` method on `VulkanProvider`.

### 3. DeferredCommandBuffer for image copy (Failed)
**Idea:** Use `deferred_command_buffer_.CmdVkCopyImageToBuffer()` for the readback.
**Problem:** `DeferredCommandBuffer` has `CmdVkCopyBuffer` and `CmdVkCopyBufferToImage` but NOT `CmdVkCopyImageToBuffer`. Would require adding a new command type to the deferred buffer system.

### 4. Raw framebuffer dump from guest memory (Failed)
**Idea:** Use base class `CommandProcessor` raw dump that reads `memory_->TranslatePhysical<uint8_t*>(frontbuffer_ptr)`.
**Problem:** Vulkan resolve writes to `shared_memory.buffer()` which uses `memory_types().device_local` (GPU-only memory). Host CPU reads a different memory space that contains all zeros.

## Known Issues

1. **Solid-color frames**: First 6 frames are boot clear screens. Game doesn't progress past early boot within practical timeframes.
2. **BGRA vs RGBA**: Vulkan textures are likely VK_FORMAT_B8G8R8A8_UNORM. The PPM write assumes RGBA byte order. Channel swap may be needed.
3. **Slow performance**: 0.3 fps due to synchronous shader compilation. After shader cache warms up, should improve significantly.
4. **File corruption issue**: `git checkout HEAD --` doesn't reliably restore files when background processes modify them. Workaround: `git show HEAD:path > /tmp/file && cp /tmp/file path`.

## Next Steps

### Priority 1: Frame-Selective Rendering
Run most frames with draw-skip (null-like speed, ~30fps) to advance the game quickly. Only enable Vulkan draws for specific frames that will be captured. This gives the speed of null GPU with occasional rendered snapshots.

Implementation sketch:
```cpp
// In IssueDraw:
if (!presenter && !capture_next_frame_) {
  return true;  // Skip draws (fast)
}
// Otherwise execute normally (slow but produces rendered content)
```

### Priority 2: Fix BGRA Channel Order
Vulkan textures are likely VK_FORMAT_B8G8R8A8_UNORM. Current PPM writer outputs bytes as-is (BGRA → PPM R=B, G=G, B=R). Need to swap R and B channels.

### Priority 3: Pipeline Caching
Vulkan driver should cache compiled pipelines (`VK_PIPELINE_CACHE_CREATE_INFO`). Second run should be significantly faster if pipeline cache persists.

### Priority 4: Test Retail XEX
Compare debug vs retail rendering to validate decomp correctness.

### Priority 5: PE Override with Matching Linker Layout
Generate COMDAT order file from original map so decomp PE functions are at identical addresses.

## Build & Run

```bash
# Build
cd ~/code/milohax/xenia/build
make xenia-headless config=checked_linux -j$(nproc)

# Run with frame capture
mkdir -p /tmp/frames
./build/bin/Linux/Checked/xenia-headless \
  --gpu=vulkan \
  --dump_frames_path=/tmp/frames/ \
  --headless_timeout_ms=60000 \
  --target=/path/to/default.xex

# Check frame content
python3 -c "
with open('/tmp/frames/frame_0001.ppm', 'rb') as f:
    f.readline(); f.readline(); f.readline()
    data = f.read()
pixels = {}
for i in range(0, len(data), 3):
    rgb = (data[i], data[i+1], data[i+2])
    pixels[rgb] = pixels.get(rgb, 0) + 1
for (r,g,b), c in sorted(pixels.items(), key=lambda x: -x[1])[:10]:
    print(f'R={r} G={g} B={b} count={c}')
"
```

## Related

- [2026-02-17-xenia-headless-research.md](2026-02-17-xenia-headless-research.md) - Initial null GPU headless setup
- [2026-02-17-extended-runtime-test.md](2026-02-17-extended-runtime-test.md) - Null GPU 115s runtime test
- [2026-02-17-xex-import-resolution.md](2026-02-17-xex-import-resolution.md) - XEX import debugging
- [../runtime/XENIA_HEADLESS_STATUS.md](../runtime/XENIA_HEADLESS_STATUS.md) - Current status tracking doc
