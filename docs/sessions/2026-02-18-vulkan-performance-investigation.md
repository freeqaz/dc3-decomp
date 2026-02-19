# Vulkan Headless Performance Investigation

**Date:** 2026-02-18
**Status:** RESOLVED — readback path was the sole bottleneck; warmup frame capture produces rendered screenshots
**See also:** [2026-02-18-xenia-screenshot-breakthrough.md](2026-02-18-xenia-screenshot-breakthrough.md) for the successful approach

## Problem Statement

DC3 runs at ~30fps (600+ swaps/20s) with xenia-headless null GPU backend. With the Vulkan backend and frame readback enabled, performance drops to ~5 swaps/20s (0.25 fps). Frame captures show only solid-color boot screens because the game never progresses past early initialization.

**Question:** What exactly is causing the slowdown, and can we fix it?

## ANSWER: The Draw Path is Fine. Readback is the Bottleneck.

| Configuration | Swaps/20s | FPS | Notes |
|---|---|---|---|
| Null GPU (no rendering) | 600+ | ~30 | Baseline |
| Vulkan all draws, NO readback | **611** | **~30.5** | **FULL GAME SPEED** |
| Vulkan all draws WITH readback | 5 | 0.25 | 122x slower |

**The Vulkan draw path with async pipeline compilation runs at full game speed (30.5 fps).** The entire bottleneck is in the GPU readback path — staging buffer allocation, image copy, fence wait, and PPM file write.

## Perf Profiling Results

### Test: 30 seconds, Vulkan all draws, no readback

```
perf record -g -- ./xenia-headless --gpu=vulkan \
    --headless_capture_interval=0 --headless_timeout_ms=30000 ...
```

**144,493 samples, 30 second run**

### Thread CPU Distribution

| Thread | CPU % (Vulkan) | CPU % (Null GPU) | Delta |
|--------|---------------|-----------------|-------|
| Main XThread (game) | 67.66% | 67.62% | +0.04% |
| xenia-headless (host main) | 19.03% | 18.04% | +0.99% |
| BinkAsy2 | 3.26% | 4.06% | -0.80% |
| **GPU Commands (CP)** | **2.78%** | **1.43%** | **+1.35%** |
| Other game threads | ~7.27% | ~8.85% | |

**Critical finding: The CPU distribution is nearly IDENTICAL between null GPU and Vulkan.** The CP thread uses only 2.78% of CPU time with Vulkan draws — barely more than the 1.43% with null GPU.

### Game Main Thread Hotspots (67.66% of total)

| % | Function | Source |
|---|----------|--------|
| 1.53% | `xe::Exception::fault_address()` | exception_handler.h:197 |
| 1.28% | `_Safe_iterator::base()` (MMIORange) | debug/safe_iterator.h:437 |
| 1.24% | `_Safe_iterator::operator*()` (MMIORange) | debug/safe_iterator.h:356 |
| 0.88% | `_Safe_iterator::_M_is_end()` (MMIORange) | debug/safe_iterator.h:526 |
| 0.70% | `xe::Exception::code()` | exception_handler.h:131 |
| 0.50%+ | `MMIOHandler::ExceptionCallback()` | mmio_handler.cc (multiple) |
| 3.85% | `pthread_mutex_lock` | (MMIO handler locking) |
| 0.87% | `pthread_mutex_unlock` | (MMIO handler locking) |

The game's main thread spends nearly ALL its CPU time in MMIO exception handling — processing page faults triggered by reads/writes to GPU-mapped memory regions. This is the same in both null GPU and Vulkan modes.

### Host Main Thread Hotspots (19.03%)

| % | Function |
|---|----------|
| ~16% | `disruptorplus::spin_wait::yield_processor()` |
| ~3% | `disruptorplus::spin_wait::spin_once()` |

The host main thread is spinning on a disruptor-plus lock-free queue — likely the logging subsystem.

### GPU Commands (CP) Thread Hotspots (2.78%)

| Function | Notes |
|----------|-------|
| `PageEntry::size()` | Memory page tracking |
| `PhysicalHeap::EnableAccessCallbacks` | Memory protection |
| `RegisterFile::GetRegisterInfo` | GPU register lookup |
| `RingBuffer::ReadAndSwap` | PM4 ring buffer reading |
| `CommandProcessor::WriteRegister` | Register writes |
| `byte_swap` | Endian conversion |

The CP thread is doing normal packet processing work. **No Vulkan draw functions appear in its hot path** — meaning the CP is spending almost no time in the draw path itself.

## Key Insight: The Bottleneck is GPU Fence Waiting

Since the CPU profiles are nearly identical, the 40x slowdown is NOT from CPU-side draw processing. The CP thread processes draws quickly (2.78% CPU), then blocks waiting for GPU completion.

### Fence Wait Chain

```
IssueSwap (headless path)
├─ EndSubmission(true)          ← Submits Vulkan command buffer + fence
│   ├─ AcquireFenceAndSubmit    ← vkQueueSubmit with fence
│   └─ (returns, submission_open_ = false)
├─ [frame_open_ = false, frame_current_++]
│
[Next frame's draws:]
├─ BeginSubmission(true)
│   ├─ await_submission = closed_frame_submissions_[frame % kMaxFramesInFlight]
│   ├─ HEADLESS: CheckSubmissionCompletionAndDeviceLoss(0)  ← Non-blocking check!
│   └─ (proceeds even if previous frame's GPU work isn't done)
├─ IssueDraw × 2000             ← All draws recorded to deferred command buffer
│   ├─ BeginSubmission (already open, returns immediately)
│   ├─ Shader analysis, primitive processing, render target update
│   ├─ ConfigurePipeline (async: skip if uncached, sync: create pipeline)
│   ├─ Texture requests
│   └─ deferred_command_buffer_.CmdVkDraw(...)
└─ IssueSwap
    └─ EndSubmission(true)      ← Submits ANOTHER command buffer
        └─ vkQueueSubmit        ← GPU now has 2+ pending submissions
```

With `kMaxFramesInFlight = 3`, the game can queue up to 3 frames before `BeginSubmission` must wait for a previous fence. But the headless non-blocking check bypasses this — the CP just keeps submitting.

**So where is the actual wait?** With the non-blocking BeginSubmission, the CP should never block... unless something else forces a wait.

### Potential Wait Points

1. **`EndSubmission(true)` in IssueSwap** — submits but doesn't wait for fence
2. **`AwaitAllQueueOperationsCompletion()`** — only called in readback path (not when no dump_frames_path)
3. **Vulkan queue submission itself** — `vkQueueSubmit` may block if the GPU queue is full
4. **Memory coherency** — `shared_memory_->EndSubmission()` may trigger page protection changes that stall
5. **The game's sync mechanism** — `WAIT_REG_MEM` polling a GPU register that takes time to update

### Theory: Vulkan Queue Saturation

Without readback, the flow should be:
1. CP processes ~2000 draws into deferred command buffer
2. EndSubmission submits command buffer + fence (non-blocking)
3. CP starts next frame immediately

If `vkQueueSubmit` itself blocks when the GPU queue is saturated, that could explain the slowdown. The Nvidia driver may serialize submissions when the GPU is busy processing previous frames.

### Theory: Shared Memory Access Callbacks

The GPU Commands thread hotspots show significant time in:
- `PhysicalHeap::EnableAccessCallbacks` — memory page protection changes
- `PageEntry::size()` — page tracking

These are triggered by `shared_memory_->EndSubmission()` which may need to update page protections for the next frame. With Vulkan draws generating actual GPU memory accesses, there may be more pages to track.

### Theory: Frame Pacing from kMaxFramesInFlight

Even with non-blocking BeginSubmission, the CP still checks `closed_frame_submissions_[frame_current_ % kMaxFramesInFlight]`. With kMaxFramesInFlight=3, after queuing 3 frames the CP must wait. The non-blocking check just prevents hard-blocking — but if the fence isn't signaled (GPU still processing), subsequent `BeginSubmission(true)` calls return false or the frame tracking gets confused.

## Chrono Instrumentation Results

Added per-draw timing to IssueDraw and per-frame timing to IssueSwap. Running with `--headless_capture_interval=0 --dump_frames_path=...`:

| Frame | Total ms | Draws | Shader ms | Submit ms | Pipeline ms | RT ms | Texture ms | Other ms |
|-------|---------|-------|-----------|-----------|-------------|-------|------------|----------|
| 3 | 411 | 1 | 0 | 0 | 0 | 0 | 0 | 411 |
| 4 | 462 | 128 | 0 | 0 | 0 | 1 | 0 | 461 |

**The draw sub-components (shader, submit, pipeline, render_target, texture) total <2ms.** The remaining 410-460ms is "other" — time spent outside the instrumented draw path.

With interval=0 + dump_frames_path, "other" includes:
- `EndSubmission(true)` in IssueSwap (command buffer submission)
- `AwaitAllQueueOperationsCompletion()` (readback fence wait)
- Staging buffer allocation + vkCmdCopyImageToBuffer (readback)
- PPM file write (3.6MB per frame at 1280×720)

**The readback path dominates.** Frame 3 has only 1 draw but takes 411ms. Frame 4 has 128 draws and takes 462ms — only 50ms more. This confirms the readback/fence-wait is the primary cost, not draw processing.

## Pipeline Warmup Analysis (120s Test)

Ran with `--headless_capture_interval=0` (no readback) for 120 seconds:

**Total: 3,931 swaps in 119.2s = 33.0 fps average**

| Time Window | Swaps | FPS |
|-------------|-------|-----|
| 0-1s | ~27 | 27 (initial pipeline stalls) |
| 1-10s | ~306 | 30.6 (minor warmup) |
| 10-120s | ~332/10s | 33.2 (locked) |

Only 3 pipeline stalls >50ms in the entire 120s run, all within the first 0.5 seconds. Pipeline cache fully warms in ~10 seconds. After that, performance is rock-solid at ~33 fps with 1.9ms stdev.

**Conclusion: Pipeline warmup is not an issue.** The game's internal fixed timestep (~33.3 fps) is the frame rate limiter, not GPU work.

## Readback Path Analysis

The current readback path does ALL of this PER FRAME:

```
1. AwaitAllQueueOperationsCompletion()    ← Wait for ALL pending GPU work
2. BeginSubmission(true)                  ← Open new submission
3. RequestSwapTexture(...)                ← Load swap texture into VkImage
4. EndSubmission(true)                    ← Submit + fence
5. AwaitAllQueueOperationsCompletion()    ← Wait for swap texture load
6. vkCreateBuffer (staging, 3.6MB)        ← Allocate staging buffer
7. vkAllocateMemory (host-visible)        ← Allocate GPU memory
8. vkBindBufferMemory                     ← Bind
9. vkCreateCommandPool                    ← Create command pool
10. vkAllocateCommandBuffers              ← Allocate command buffer
11. vkCmdPipelineBarrier (transition)     ← Image layout transition
12. vkCmdCopyImageToBuffer                ← Copy swap image to staging
13. vkCmdPipelineBarrier (transition back) ← Restore layout
14. vkQueueSubmit + fence                 ← Submit copy
15. vkWaitForFences(UINT64_MAX)           ← Wait for copy completion
16. vkMapMemory                           ← Map staging to CPU
17. fwrite (2.7MB PPM)                    ← Write file
18. vkUnmapMemory                         ← Unmap
19. vkDestroyBuffer, vkFreeMemory, etc.   ← Cleanup
```

**That's 19 Vulkan API calls + 2 GPU fence waits + 3.6MB memory allocation + 2.7MB file write PER FRAME.**

Key overhead sources:
- **Two `AwaitAllQueueOperationsCompletion()` calls** — each waits for ALL pending GPU work
- **Per-frame allocation**: staging buffer, memory, command pool, command buffer — all created and destroyed each frame
- **File I/O**: 2.7MB PPM write is synchronous, blocking the CP thread

## Open Questions (Answered)

1. ~~Without readback, what causes 2s per frame?~~ **ANSWERED: Nothing! Without readback, the game runs at 30.5 fps.**
2. ~~Is vkQueueSubmit blocking?~~ **ANSWERED: No, the non-blocking BeginSubmission + async pipelines work perfectly.**
3. ~~Does performance improve after pipeline warmup?~~ **ANSWERED: Yes, warmup takes only 10 seconds. After that, locked at 33 fps.**
4. ~~How many unique pipelines does DC3 use?~~ **ANSWERED: Only 2-3 pipeline stalls in first 0.5 seconds.**
5. ~~Would removing EndSubmission from non-capture IssueSwap help?~~ **ANSWERED: Not needed — the current path already runs at full speed without readback.**

## Architecture Summary

```
Headless IssueSwap flow (current):
1. EndSubmission(true)     ← submits command buffer with fence
2. Frame accounting (advance frame_current_)
3. Check should_capture
4. If capture: AwaitAll → readback → PPM write

Headless IssueDraw flow:
1. Check headless_render_frame_  ← if false, skip (return true)
2. Shader analysis              ← fast (< 1ms total per frame)
3. BeginSubmission(true)        ← non-blocking in headless mode
4. Primitive/render target/pipeline/texture setup  ← fast (< 2ms total)
5. Record draw to deferred command buffer  ← fast (just recording, not executing)

EndSubmission internals:
1. End render pass
2. Flush subsystem state (render_target, primitive, shared_memory, uniform_buffer)
3. Execute deferred command buffer into real VkCommandBuffer
4. vkQueueSubmit with fence (non-blocking submit, but driver may serialize)
5. Advance submission tracking

BeginSubmission (headless):
1. Check closed_frame_submissions_[frame % kMaxFramesInFlight]
2. NON-BLOCKING: CheckSubmissionCompletionAndDeviceLoss(0)
   ← Only checks fences that are already signaled (vkGetFenceStatus)
3. If device not lost: proceed
4. Open new submission (reset deferred command buffer, etc.)
```

## Next Steps

### Immediate Research (in progress)
- [ ] Run 120s test with all draws, no readback — does frame rate improve as pipelines warm up?
- [ ] Count unique pipeline creations during 30s run
- [ ] Instrument EndSubmission timing (vkQueueSubmit duration specifically)
- [ ] Test with EndSubmission removed from non-capture IssueSwap (just accumulate draws)

### The Plan: Optimize the Readback Path

Since the draw path runs at 30.5 fps, we only need to fix the readback. The game needs ~600+ swaps to reach interesting content (loading screens, menus). Strategy:

1. **Run at full speed for N frames (no readback)**
2. **Capture a single frame efficiently at frame N**
3. **Resume full speed**

#### Optimization A: Reuse Vulkan Resources (Medium Impact)
Pre-allocate the staging buffer, command pool, and command buffer ONCE in SetupContext. Reuse them for every readback instead of creating/destroying per frame.

**Expected savings:** ~50-100ms per readback (eliminates 8 Vulkan object creation/destruction calls).

#### Optimization B: Reduce Fence Waits (High Impact)
Current: 2 separate `AwaitAllQueueOperationsCompletion()` calls per readback.
Optimized: Combine the swap texture load and image copy into a single submission with a single fence wait.

**Expected savings:** ~200-400ms per readback (eliminates 1 full GPU round-trip).

#### Optimization C: Async File Write (Medium Impact)
Write the PPM file on a background thread instead of blocking the CP.

**Expected savings:** ~50ms per readback (2.7MB synchronous write eliminated from critical path).

#### Optimization D: Just Skip Readback Until Late Game
The simplest approach: use `--headless_capture_interval=N` with a very high N (e.g., 1000+). Let the game run at 30fps for 30+ seconds, then capture a frame. At 30fps, 1000 swaps = ~33 seconds, which should be well into loading/menus.

**Expected: One readback (even at 4 seconds) is fine if we only do it once.**

#### Optimization E: PNG Instead of PPM
PPM is 2.7MB uncompressed. PNG would be ~50-200KB. Use stb_image_write or libpng for 10-50x smaller files.

### Recommended Approach: D then A+B

1. **First: test with interval=1000 or higher** — see if a single late capture shows actual game content
2. **Then: pre-allocate resources (A) + combine submissions (B)** to bring readback from ~4s to <500ms
3. **Optional: async file write (C) + PNG output (E)** for further optimization

## Test: Late Frame Capture (interval=1000)

With `--headless_capture_interval=1000` and `--headless_timeout_ms=60000`:
- Game ran at full speed for 997 frames (~33 seconds)
- Warmup frames 998-999 (draws enabled)
- Frame 1000 captured

**Frame 1000 content:**
- 50% black (left half), 50% R=76,G=0,B=0 (right half)
- Vertically split: left=black, right=dark blue (BGRA: B=76)
- Still early boot: only 2 unique colors
- The split pattern is the double-buffered framebuffer (alternating ptrs 0x1E830000/0x1EBC8000)

**Conclusion:** At 1000 swaps (~33 seconds), the game is still in early initialization. DC3 likely needs 2000+ swaps (60+ seconds) to reach loading screens, and much longer for actual menus. The game may also require scripted input (button presses) to advance past title screens.

## Summary of Findings

1. **Draw path is NOT the bottleneck.** Vulkan all-draws mode runs at 30.5 fps, identical to null GPU.
2. **Pipeline warmup is trivial.** Only 2-3 stalls in first 0.5 seconds. Full speed by 10 seconds.
3. **Readback is the sole bottleneck.** Per-frame readback drops from 30fps to 0.25fps (122x slower).
4. **Game is still in early boot at 1000 swaps.** Need to run longer and/or provide scripted input.
5. **The readback path allocates/destroys Vulkan resources per frame and does 2 full GPU round-trips.**

## Related

- [2026-02-18-xenia-screenshot-breakthrough.md](2026-02-18-xenia-screenshot-breakthrough.md) — **Successful approach**: warmup frame capture produces rendered DC3 screenshots
- [2026-02-18-vulkan-headless-rendering.md](2026-02-18-vulkan-headless-rendering.md) — Session 1: Architecture, implementation, initial test results
- [2026-02-18-xenia-frame-capture-attempts.md](2026-02-18-xenia-frame-capture-attempts.md) — All approaches tried (enable draws, async worker, async pipelines, frame-selective)
- [../runtime/XENIA_HEADLESS_STATUS.md](../runtime/XENIA_HEADLESS_STATUS.md) — Current status tracking
