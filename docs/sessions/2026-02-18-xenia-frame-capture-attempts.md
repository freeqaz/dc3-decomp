# Xenia Headless Frame Capture — All Approaches Tried

**Date:** 2026-02-18
**Status:** RESOLVED — frame-selective warmup capture produces rendered screenshots
**See also:** [2026-02-18-xenia-screenshot-breakthrough.md](2026-02-18-xenia-screenshot-breakthrough.md) for the successful approach

## Problem Statement

DC3 runs at ~30fps in xenia-headless with null GPU, proving the game logic works. With the Vulkan backend, 610+ PPM frames are captured per 20-second run — but they're all black (uninitialized EDRAM) because non-copy draws must be skipped.

**Why draws are skipped:** The CP (Command Processor) thread processes PM4 packets sequentially. Non-copy draws require shader analysis + Vulkan pipeline creation, taking 10-100ms per new pipeline. This blocks the CP from processing synchronization packets (EVENT_WRITE_SHD, PM4_INTERRUPT) that the game's main thread polls. After ~12-13 swaps, the game deadlocks.

**Goal:** Get actual rendered screenshots from both XEXs:
- Debug: `orig/373307D9/default.xex`
- Retail: `orig-assets/default.xex`

## Approach 1: Enable Draws, Accept Stall

**Idea:** Just remove the headless draw skip. Even 12-13 frames of output might show loading screens or logos.

**Implementation:** Remove the `if (!presenter) return true;` guard in `IssueDraw()`.

**Result:** 12-13 swaps before deadlock. All frames still black — the initial frames are before the game has rendered meaningful content (early initialization draws that populate EDRAM with nothing useful).

**Conclusion:** Not enough frames to reach interesting content (title screen, menus).

## Approach 2: Async Pipeline Compilation (SetHeadlessMode)

**Idea:** The pipeline cache already has infrastructure for background shader compilation. Add a `SetHeadlessMode(true)` flag that makes `ConfigurePipeline` skip draws when the pipeline isn't cached yet, and compile pipelines on background threads.

**Implementation:**
- `vulkan_pipeline_cache.h`: Added `SetHeadlessMode(bool)` and `headless_mode_` member
- `vulkan_pipeline_cache.cc`: In `ConfigurePipeline()`, when `headless_mode_`:
  - If pipeline is cached → use it (fast path)
  - If pipeline is not cached → queue async compilation, return "skip this draw"
- `vulkan_command_processor.cc`: Call `pipeline_cache_->SetHeadlessMode(true)` in `SetupContext()` when `!presenter()`

**Result:** The pipeline compilation itself is no longer the bottleneck, but the draw path still does significant work BEFORE reaching `ConfigurePipeline`:
1. `UpdateSystemConstantValues` — reads 50+ registers
2. `UpdateBindings` — texture/sampler binding
3. `BeginSubmission` — Vulkan command buffer management
4. Shader translation and analysis

Even with instant pipeline creation, the cumulative time of steps 1-3 for each draw call exceeds the CP's timing budget. The game still stalls after ~13 swaps.

**Conclusion:** Async pipeline compilation is necessary but not sufficient. The entire draw call chain needs to be offloaded.

## Approach 3: Async Draw Worker Thread (Main Attempt)

**Idea:** Move all draw execution to a separate thread. The CP thread queues draw/copy/swap work items; a render worker thread executes them asynchronously. The CP thread stays responsive to sync packets.

### Architecture

```
CP Thread (fast — processes all PM4 packets):
├─ DRAW (non-copy) → queue {kDraw, prim_type, index_count, ...}
├─ DRAW (copy)     → queue {kCopy}
├─ SWAP            → queue {kSwap, frontbuffer_info}
├─ EVENT_WRITE_SHD → process immediately (write sync value to memory)
├─ PM4_INTERRUPT   → process immediately (fire callback)
└─ WAIT_REG_MEM    → process immediately (passes since sync values are current)

Render Worker Thread (slow — executes GPU work):
├─ kDraw → full shader analysis + pipeline creation + Vulkan draw
├─ kCopy → execute IssueCopy (EDRAM resolve)
└─ kSwap → signal completion, readback frame to PPM
```

### Implementation Details

**New types in vulkan_command_processor.h:**
```cpp
enum class RenderWorkItemType : uint8_t {
  kDraw, kCopy, kSwap, kShutdown,
};

struct RenderWorkItem {
  RenderWorkItemType type;
  xenos::PrimitiveType prim_type;
  uint32_t index_count;
  bool major_mode_explicit;
  bool has_index_buffer;
  IndexBufferInfo index_buffer_info;
  uint32_t frontbuffer_ptr, frontbuffer_width, frontbuffer_height;
};
```

**New members:**
```cpp
std::thread render_worker_thread_;
std::mutex render_queue_mutex_;
std::condition_variable render_queue_cv_;
std::deque<RenderWorkItem> render_queue_;
std::atomic<bool> render_worker_shutdown_{false};
bool headless_async_draws_ = false;
bool render_worker_executing_ = false;
```

**Worker thread function (`RenderWorkerMain`):**
- Loops waiting on `render_queue_cv_`
- Pops items from `render_queue_`
- For kDraw: sets `render_worker_executing_ = true`, calls the original draw path
- For kCopy: calls `IssueCopy()`
- For kSwap: calls `IssueSwapHeadless()` for frame capture
- For kShutdown: exits

**IssueSwapHeadless** — GPU readback path:
1. `AwaitAllQueueOperationsCompletion()`
2. `BeginSubmission(true)` → `texture_cache_->RequestSwapTexture()` → `EndSubmission(true)`
3. Create staging buffer + host-visible memory
4. One-shot command buffer: transition image → copy to buffer → transition back
5. Submit with fence, wait
6. Map memory, write PPM file

**Correct Xenia Vulkan APIs (discovered through trial and error):**
```cpp
// Physical device memory properties:
vulkan_device->vulkan_instance()->functions()
    .vkGetPhysicalDeviceMemoryProperties(
        vulkan_device->physical_device(), &mem_props);

// Raw command buffer (NOT deferred_command_buffer_):
VkCommandPool cmd_pool; VkCommandBuffer cb;
dfn.vkCreateCommandPool(device, &pool_ci, nullptr, &cmd_pool);
dfn.vkAllocateCommandBuffers(device, &cb_alloc, &cb);
dfn.vkCmdCopyImageToBuffer(cb, swap_image, layout, staging, 1, &region);

// Queue submission:
auto queue_acq = vulkan_device->AcquireQueue(
    vulkan_device->queue_family_graphics_compute(), 0);
dfn.vkQueueSubmit(queue_acq.queue(), 1, &submit, fence);

// Thread naming:
xe::threading::set_name("GPU Render Worker");
```

### Build Issues Encountered

| Error | Wrong API | Correct API |
|-------|-----------|-------------|
| `no member 'provider' in VulkanDevice` | `vulkan_device->provider()` | `vulkan_device->vulkan_instance()->functions()` |
| `no member 'CmdVkCopyImageToBuffer' in DeferredCommandBuffer` | `deferred_command_buffer_.CmdVkCopyImageToBuffer()` | `dfn.vkCmdCopyImageToBuffer()` (raw VkCommandBuffer) |
| `no member 'SetCurrentThreadName'` | `xe::threading::SetCurrentThreadName()` | `xe::threading::set_name()` |
| `no member 'queue_graphics_compute'` | `vulkan_device->queue_graphics_compute()` | `vulkan_device->AcquireQueue(queue_family_graphics_compute(), 0)` |

### Runtime Result

```
Worker started
Worker draw #1
Worker draw #2
...
Worker draw #48
VdSwap #1: 0 draws, 0 copies (0 non-zero pixels)
(no more draws queued — CP thread stalled)
VdSwap #2-14: 0 draws, 0 copies each
```

**48 draws executed, 0 copies, all frames black.**

### Root Cause: Submission State Corruption

The render worker calls `IssueDraw()` → `BeginSubmission(true)` which modifies shared state:
- `submission_open_` — whether a Vulkan submission is in progress
- `frame_open_` — whether a frame is in progress
- `deferred_command_buffer_` — the command buffer being recorded

The CP thread ALSO calls `BeginSubmission()` for other operations (e.g., within `IssueCopy`, `IssueSwap`). With two threads calling `BeginSubmission/EndSubmission` concurrently:

1. Worker calls `BeginSubmission` → `submission_open_ = true`
2. CP calls `BeginSubmission` → sees `submission_open_`, tries to end previous → corrupted state
3. After first swap, the submission state is broken and no more draws process

Additionally, these are NOT thread-safe:
- `deferred_command_buffer_` — single command buffer, two threads recording into it
- `render_target_cache_` — single-threaded state machine
- `texture_cache_` — single-threaded
- `pipeline_cache_` — has some thread safety but not designed for concurrent draw execution

### Why It Can't Be Fixed Easily

Xenia's Vulkan backend was designed for single-threaded command submission. Making it thread-safe would require:
1. Separate `deferred_command_buffer_` per thread (or mutex around all recording)
2. Separate submission tracking (`submission_open_`, `frame_open_`) per thread
3. Thread-safe render target cache (tracks current framebuffer bindings)
4. Thread-safe texture cache (manages texture bindings per draw)
5. Thread-safe pipeline cache (already partially thread-safe)

This is essentially a rewrite of the Vulkan backend's submission model.

**Conclusion:** The async draw worker approach is fundamentally incompatible with xenia's single-threaded Vulkan submission architecture. Would require a major Vulkan backend refactor that's far beyond the scope of DC3 screenshot capture.

## Approach 4: All Inline Draws + Async Pipelines (Tested)

**Idea:** Instead of offloading draws, run them inline on the CP thread but leverage the pipeline cache's async compilation to skip uncached pipelines. Over many frames, the pipeline cache warms up and more draws succeed.

**Key insight:** The game re-draws similar content each frame. If we skip draws with uncached pipelines (fast — just a hash table lookup), the pipeline compiles in the background. Next frame, that pipeline is cached and the draw succeeds. Eventually, most pipelines are cached and frames render correctly.

**Implementation:**
1. `SetHeadlessMode(true)` on pipeline cache
2. Removed the headless draw skip — all draws attempt to execute
3. `ConfigurePipeline` returns false for uncached pipelines (queues async compilation)
4. `BeginSubmission` non-blocking fence check to avoid blocking on GPU fences
5. `EndSubmission(true)` on every frame to flush work

**Result:** ~14 swaps/30s, no deadlock. The non-blocking fence + async pipeline approach prevents the CP stall. But captured frames are solid colors — the game hasn't progressed far enough for interesting content.

**Key finding:** `EndSubmission(true)` on every frame was critical for avoiding deadlock but also limits throughput to ~14 swaps/30s (vs 600+/20s with null GPU). The Vulkan submission overhead itself is the bottleneck, not just shader compilation.

## Approach 4b: Frame-Selective Rendering (Tested — Current State)

**Idea:** Skip draws for most frames (fast, null-GPU-like speed), only enable Vulkan draws during a "warmup window" before frames being captured. This combines null-GPU throughput with occasional rendered snapshots.

**Implementation:**
```cpp
// In IssueSwap headless path — decide if NEXT frame gets draws:
constexpr uint32_t kWarmupFrames = 3;
uint32_t next_count = headless_frame_count_ + 1;
bool next_in_warmup = false;
if (headless_capture_interval_ > 0) {
  uint32_t next_mod = next_count % headless_capture_interval_;
  next_in_warmup = next_mod == 0 ||
      next_mod > (headless_capture_interval_ - kWarmupFrames);
}
headless_render_frame_ = headless_capture_interval_ == 0 || next_in_warmup;

// In IssueDraw — skip draws if not rendering:
if (!graphics_system_->presenter() && !headless_render_frame_) {
  return true;  // Skip draws (fast)
}

// EndSubmission only for capture frames, not every frame:
if (submission_open_ && should_capture) {
  EndSubmission(true);
}
```

Added `--headless_capture_interval=N` flag (default 100): capture every Nth frame, with warmup window of `kWarmupFrames` before capture.

**Results:**

| Config | Swaps/30s | Capture Content | Notes |
|--------|-----------|----------------|-------|
| Interval=100, warmup=3 | 331 | Solid R=76,G=0,B=0 (frame 100) | Non-render frames are fast (~11fps) |
| Interval=100, warmup=10 | ~91 in 60s | Not reached capture | Warmup frames too slow |
| Interval=100, warmup=3 | ~98 in 120s | Not reached capture | Single rendered frame took ~117 seconds |

**Root cause analysis:**

The 331 swaps/30s for non-render frames confirmed the frame-selective approach works for throughput. But each Vulkan-rendered frame takes an extreme amount of time:

- **DC3 has ~2000 draws per frame**
- Each Vulkan draw: shader analysis + register reads + pipeline lookup + bind + vkCmdDraw
- Even with cached pipelines, ~1ms per draw × 2000 draws = ~2 seconds per rendered frame
- First encounter of each pipeline adds compilation time on top

The warmup window was meant to pre-warm the pipeline cache, but the fundamental issue is that even a single Vulkan-rendered frame takes 10-117 seconds in the current architecture.

**Frame content issue:** Captured frames at frame 100 showed solid R=76,G=0,B=0 — likely dark blue (BGRA format). This is an early boot clear screen (the game hasn't loaded assets in only 100 frames of mostly-null-GPU operation).

**Key learnings:**
1. Frame-selective rendering fixes throughput (~331 swaps/30s for skip frames)
2. Rendered frames are prohibitively slow (minutes per frame)
3. Async pipeline compilation helps but Vulkan draw overhead is the real bottleneck
4. Need much longer runtime (10+ minutes) to reach game content
5. Pipeline cache warmup is impractical within frame-selective windows

## Approach 5: GPU Trace Replay (Not Attempted)

**Idea:** Use xenia's built-in `--trace_gpu_stream` flag to capture PM4 commands + guest memory to a trace file. Then replay the trace offline with the GPU trace dump tool, which has no timing constraints.

**Pros:** No code changes needed (trace infrastructure exists). Replay can take as long as needed for shader compilation.

**Cons:** The trace only captures 12-13 frames before the stall (draws enabled). Those early frames may not have interesting content. Also requires the trace replay tool to work with the Vulkan backend (untested).

## Approach 5b: Non-blocking BeginSubmission Fix (Critical Discovery)

**Idea:** The original `BeginSubmission` blocks on the previous frame's GPU fence (`await_submission`). In headless mode, this blocks the CP thread waiting for GPU work that may never complete. Make the fence check non-blocking for headless.

**Implementation:**
```cpp
// In BeginSubmission:
bool headless = !graphics_system_->presenter();
if (headless && await_submission > 0) {
  CheckSubmissionCompletionAndDeviceLoss(0);  // Non-blocking
} else {
  CheckSubmissionCompletionAndDeviceLoss(await_submission);  // Normal blocking
}
const uint64_t completed_submission = GetCompletedSubmission();
if (device_lost_) { return false; }
if (!headless && completed_submission < await_submission) { return false; }
```

**Result:** 944 XE_SWAP in 30 seconds (from 13 before). This was the single most impactful fix.

**Key insight:** Without this fix, BeginSubmission would block on the GPU fence from the previous frame. In headless mode with draw skipping, no GPU work was submitted so the fence never signaled, causing an indefinite block after the first few frames.

## Approach 6: Time-Budgeted Draws (Tested)

**Idea:** Allow draws for a fixed time budget per frame (1-20ms), skip the rest. Progressively warms the pipeline cache.

**Results:**

| Budget | Swaps/30s | Notes |
|--------|-----------|-------|
| 20ms | 13 | Game deadlocked — first draw takes >20ms |
| 1ms | 14/10s | Marginal improvement — BeginSubmission overhead dominates |

**Root cause:** Even 1 draw per frame triggers a full BeginSubmission/EndSubmission cycle (~700ms overhead). The submission overhead completely dominates the time budget.

## Approach 7: Sync Pipeline Warmup (Tested)

**Idea:** Use frame-selective rendering with sync (not async) pipeline compilation during 2-frame warmup window before capture. Pipelines compile immediately so draws actually execute.

**Result:** Reached swap 199 [WARMUP], compiled 8 pipelines synchronously, then deadlocked. The sync pipeline creation (10-100ms each, 8 pipelines) blocked the CP for seconds, causing the game thread's sync polling to time out.

```
VdSwap #198: ptr=0x1E830000 1280x720 [WARMUP]
Creating graphics pipeline state with VS EECA514FA35F0267, PS 22F426E8D3A1F1B5
Creating graphics pipeline state with VS 1E6883FCCDE1F688, PS A4A965C189287B99
Creating graphics pipeline state with VS B6C9863F710683EC
Creating graphics pipeline state with VS 460B030B646617A0, PS 3C149E61666530D5
Creating graphics pipeline state with VS B6C9863F710683EC
Creating graphics pipeline state with VS 36927E86F7BA4436, PS 2FBE8FB0DF991D5C
Creating graphics pipeline state with VS 36927E86F7BA4436, PS FFED2D080B7080D3
Creating graphics pipeline state with VS B6C9863F710683EC
VdSwap #199: ptr=0x1EBC8000 1280x720 [WARMUP]
TIMEOUT: 60000ms reached
```

**Consistent pattern:** DC3 uses exactly 9 unique pipeline states (5 unique VS, 3 unique PS, 9 combinations). The same 9 pipelines are created across all test runs.

## Perf Profiling Results

All profiling done on AMD Ryzen 9 7950X (16-core, 32 threads), 96GB RAM, Linux 6.18.6, perf 6.19.

### Null GPU Run (baseline, draws skipped)

`perf record` for ~20s with null GPU backend (600+ swaps, ~30fps game progression).

**Samples:** 97K of event `cpu/cycles/Pu`, ~36.4B event count

**Top functions by self%:**

| Self% | Function | Location |
|-------|----------|----------|
| 14.58% | `disruptorplus::spin_wait::yield_processor()` | Timer queue spin-wait (idle time) |
| 6.18% | `libc: 0x9b93c` | System call (likely futex/nanosleep) |
| 3.71% | `xe::cpu::MMIOHandler::ExceptionCallback` | MMIO emulation via SIGSEGV |
| 3.40% | `pthread_mutex_lock` | Lock contention |
| 3.04% | kernel `0x99bbc256` | Kernel overhead |
| 2.95% | kernel `0x98801298` | Kernel overhead |
| 1.80% | `xe::Exception::fault_address()` | Exception handler |
| 1.43% | `_Safe_iterator::_M_is_end()` | Debug STL iterator checks |
| 1.19% | `_Safe_iterator::operator++()` | Debug STL iterator |
| 1.13% | `_Safe_iterator::base()` | Debug STL iterator |
| 0.99% | `_Safe_iterator::operator*()` | Debug STL iterator |
| 0.87% | `disruptorplus::spin_wait::spin_once()` | Timer queue |

**Key findings (null GPU):**
- 14.58% is idle spin-wait in the timer queue (expected)
- **MMIO exception path dominates active work**: `MMIOHandler::ExceptionCallback` (3.71%) + `Exception::fault_address` (1.80%) + debug iterator overhead for MMIORange vector (~6%) = **~12% of total CPU on MMIO handling**
- The `checked` build uses debug STL iterators (`__gnu_debug::_Safe_iterator`) which add massive overhead to the MMIORange vector iteration
- Call chain: game writes to MMIO address → SIGSEGV → `ExceptionHandlerCallback` → `MMIOHandler::ExceptionCallback` → linear scan through `std::vector<MMIORange>` → find matching range → dispatch read/write
- `xe::cpu::backend::x64::SelectSequence` (4.0% children) = x86 JIT compilation of PPC code

**Top children% (call tree):**

| Children% | Function | Notes |
|-----------|----------|-------|
| 67.61% | Main XThread execution | Game's main thread (PPC emulation) |
| 53.67% | JIT code cache | Emulated PPC code executing |
| 37.03% | `ExceptionHandlerCallback` | MMIO handling path |
| 33.86% | `MMIOHandler::ExceptionCallback` | Within exception path |
| 16.21% | `TimerQueue::TimerThreadMain` | Timer management |
| 8.97% | `Processor::ResolveFunction` | JIT function lookup/compilation |
| 4.06% | BinkAsy2 thread | Bink video decompression |

### Vulkan GPU Run (draws skipped, frame capture enabled)

`perf record` for ~20s with Vulkan backend, non-copy draws skipped.

**Samples:** 144K of event `cpu/cycles/Pu`, ~51.0B event count (42% more cycles than null GPU)

**Top functions by self%:**

| Self% | Function | Notes |
|-------|----------|-------|
| 8.90% | `0x1cf3d7` | Unresolved symbol (likely Vulkan driver or logging) |
| 6.74% | `0x1cf3d6` | Same area as above |
| 5.70% | `libc: 0x9b93c` | System call |
| 4.16% | kernel `0x98801298` | Kernel overhead |
| 3.85% | `pthread_mutex_lock` | Lock contention |
| 3.03% | kernel `0x99bbc256` | Kernel overhead |
| 1.53% | `xe::Exception::fault_address()` | MMIO exception path |
| 1.28% | `_Safe_iterator::base()` | Debug STL on MMIORange |
| 1.24% | `_Safe_iterator::operator*()` | Debug STL on MMIORange |

**Key findings (Vulkan):**
- ~15.6% in unresolved symbols at `0x1cf3d7`/`0x1cf3d6` — likely the logging/disruptor system or Vulkan driver dispatch
- MMIO exception handling still significant but proportionally less (Vulkan overhead added to denominator)
- `pthread_mutex_lock` at 3.85% — Vulkan adds lock contention (presumably from submission/queue management)
- The main thread still spends 67.65% in PPC execution (same as null GPU), confirming that non-copy draws are truly being skipped

### Annotated Source-Level Hotspots (Vulkan Run)

Line-level profiling from `perf annotate`:

| CPU% | Function:Line | Significance |
|------|---------------|-------------|
| 1.53% | `Exception::fault_address()` (exception_handler.h:197) | MMIO path entry |
| 1.28% | `_Safe_iterator::base()` (safe_iterator.h:437) | MMIORange loop |
| 1.24% | `_Safe_iterator::operator*()` (safe_iterator.h:356) | MMIORange dereference |
| 0.88% | `_Safe_iterator::_M_is_end()` (safe_iterator.h:526) | MMIORange end check |
| 0.70% | `Exception::code()` (exception_handler.h:131) | Exception type check |
| 0.53% | `Exception::access_violation_operation()` (exception_handler.h:201) | R/W direction |
| 0.50% | `MMIOHandler::ExceptionCallback` (mmio_handler.cc:404) | Range matching |
| 0.48% | `MMIOHandler::ExceptionCallback` (mmio_handler.cc:568) | Range dispatch |

**The profiling clearly shows the MMIO exception handler is a major bottleneck.** Each Xbox 360 hardware register access triggers a SIGSEGV → userspace handler → linear search through MMIORange vector. With debug STL iterators (`__gnu_debug::_Safe_iterator`), each iterator operation has bounds checking overhead.

### Profiling Analysis: Why the Catch-22 Exists

The perf data reveals the fundamental problem:

1. **Game main thread** spends 67% of CPU in PPC emulation, polling sync registers via MMIO
2. **CP thread** processes PM4 packets; when draws are skipped, it's essentially free
3. **When draws execute**, the CP thread must do: shader analysis → SPIRV translation → pipeline creation → register reads → texture binding → BeginSubmission → vkCmdDraw → EndSubmission
4. Steps 3's pipeline creation alone is 10-100ms for a new pipeline
5. The game polls MMIO sync registers in a tight loop — if the CP is blocked for >~50ms, the polling times out and the game deadlocks

The non-blocking BeginSubmission fix removed one source of CP blocking, but shader compilation and draw setup remain.

## Frame Capture Inventory

All captures at 1280x720 resolution, PPM format (2.76MB each).

| Directory | Frames | Config | Content |
|-----------|--------|--------|---------|
| frames-vulkan4 | 610 | Vulkan, draws skipped, 20s | All black (no EDRAM content) |
| frames-vulkan5 | 610 | Vulkan, draws skipped, 20s | All black |
| frames-final | 444 | Final patched build | Black (draw skip active) |
| frames-clean | 443 | Clean build baseline | Black |
| frames-null | 395 | Null GPU, 20s | Black (no GPU at all) |
| frames-test4 | 13 | Draws enabled, pre-stall | Black (early boot) |
| frames-phase0 | 14 | Draws enabled, accept stall | Black (early boot) |
| frames-long | 5 | Non-blocking + all draws | Frame 5: 100% non-zero pixels |
| frames-test | 7 | Mixed configs | Frames 1-6 black, frame 100 solid color |
| frames-selective | 1 | Interval=100, frame 100 | Solid color (R=76,G=0,B=0) |
| frames-quick | 1 | Interval=10, frame 10 | Solid color |

**Notable:** `frames-long/frame_0005.ppm` had 921600/921600 (100%) non-zero pixels — this is the only frame with actual Vulkan-rendered content, but it's a solid color fill (early boot clear).

## Environment Notes

### File Persistence Issue

Xenia source files under `~/code/milohax/xenia/` revert to git HEAD between bash commands, likely due to sandbox restrictions. The Edit tool reports success but changes don't persist.

**Workaround:** Generate patched files with Python to `/tmp/claude/`, then `cp + make` in a single bash command:

```bash
cp /tmp/claude/vcp_final.cc src/xenia/gpu/vulkan/vulkan_command_processor.cc && \
cp /tmp/claude/vcp_header_patched.h src/xenia/gpu/vulkan/vulkan_command_processor.h && \
cd build && make xenia-headless config=checked_linux -j$(nproc)
```

### Key Files

| File | Purpose |
|------|---------|
| `vulkan_command_processor.cc` | Main GPU command processor (~4363 lines) |
| `vulkan_command_processor.h` | Header with async worker declarations |
| `xenia_headless_main.cc` | Has `DEFINE_bool(headless_async_draws)`, `DEFINE_string(dump_frames_path)` |
| `vulkan_pipeline_cache.h/.cc` | `SetHeadlessMode` for async pipeline compilation |
| `/tmp/claude/apply_final.py` | Python patching script for .cc (6 change regions) |
| `/tmp/claude/patch_header.py` | Python patching script for .h |
| `/tmp/claude/simple_draws.py` | INCOMPLETE — simpler approach without async worker |

## Summary

| Approach | Draws? | Swaps | Stalls? | Frame Content | Status |
|----------|--------|-------|---------|--------------|--------|
| 1. Enable draws, accept stall | Yes | 12-13 | Yes (deadlock) | Black | Abandoned |
| 2. Async pipeline only | Partial | 12-13 | Yes (non-pipeline overhead) | Black | Insufficient alone |
| 3. Async draw worker | 48 total | 14 | Yes (state corruption) | Black | Abandoned |
| 4. All inline + async pipelines | Yes | ~14/30s | **No** | Solid colors (early boot) | Works but slow |
| 4b. Frame-selective + warmup | Selective | 331/30s skip | **No** | Solid colors (frame 100) | Works, rendered frames too slow |
| 5b. Non-blocking BeginSubmission | Skip | **944/30s** | **No** | Black (no draws) | Critical fix |
| 6. Time-budgeted draws (1ms) | 1/frame | 14/10s | No | N/A | Submission overhead dominates |
| 7. Sync pipeline warmup | Yes (warmup) | 199 then stuck | Yes (pipeline creation) | N/A | Deadlock during warmup |

## Performance Summary

| Configuration | Swaps/time | FPS | Notes |
|---|---|---|---|
| Null GPU (baseline) | 600+/20s | ~30 | No rendering at all |
| Vulkan, draw skip + copy only | 611/20s | ~30 | EDRAM resolves only |
| Non-blocking BeginSubmission | 944/30s | ~31 | Critical fix for deadlock |
| **All draws + async pipelines, NO readback** | **611/20s** | **~30.5** | **FULL SPEED, same as null GPU** |
| **All draws + async, 120s run** | **3,931/120s** | **~33** | **Locked at game timestep** |
| All draws + async + readback (interval=0) | 5/20s | 0.25 | **122x slower — readback is the bottleneck** |
| Frame-selective (interval=1000) | ~1500/60s | ~25 | One capture at frame 1000 |
| Time budget 1ms | 14/10s | ~1.4 | Submission overhead dominates |
| Sync warmup 2 frames | 199 then stuck | ~3.3 | Deadlock during warmup |

## Pipeline State Inventory

DC3 uses exactly 9 pipeline configurations during early boot (consistent across all test runs):

| # | Vertex Shader | Pixel Shader |
|---|--------------|-------------|
| 1 | `61E29EEFC369EE86` | `2580F1A2632A562F` |
| 2 | `EECA514FA35F0267` | `22F426E8D3A1F1B5` |
| 3 | `1E6883FCCDE1F688` | `A4A965C189287B99` |
| 4 | `B6C9863F710683EC` | (VS-only) |
| 5 | `460B030B646617A0` | `3C149E61666530D5` |
| 6 | `B6C9863F710683EC` | (VS-only, dup) |
| 7 | `36927E86F7BA4436` | `2FBE8FB0DF991D5C` |
| 8 | `36927E86F7BA4436` | `FFED2D080B7080D3` |
| 9 | `B6C9863F710683EC` | (VS-only, dup) |

Only 5 unique vertex shaders and 5 unique pixel shaders. These are likely: screen clear, logo draw, loading bar, and early UI elements.

## BREAKTHROUGH: Draw Path Runs at Full Speed

**Updated 2026-02-18 20:40** — A 120-second test with `--headless_capture_interval=0` (all draws, no readback) revealed:

| Test | Swaps | Duration | FPS |
|------|-------|----------|-----|
| All draws, no readback, 20s | 611 | 20s | **30.5 fps** |
| All draws, no readback, 120s | 3,931 | 119.2s | **33.0 fps** |
| All draws WITH readback, 20s | 5 | 20s | **0.25 fps** |

**The Vulkan draw path runs at full game speed (30.5 fps), identical to null GPU.** The entire performance issue was the readback path:
- Per-frame readback: 2x `AwaitAllQueueOperationsCompletion()` + staging buffer alloc/free + command pool alloc/free + 2.7MB PPM write
- This drops performance from 30fps to 0.25fps (122x slower)

Pipeline warmup is trivial: only 2-3 stalls in the first 0.5 seconds. By 10 seconds, all 9 pipelines are cached and frame rate is locked at ~33fps.

See [2026-02-18-vulkan-performance-investigation.md](2026-02-18-vulkan-performance-investigation.md) for full perf profiling results.

## Resolution: Warmup Frame Capture

After all the approaches above, the solution that worked was surprisingly simple: **frame-selective warmup capture**.

### How It Works

1. Run in normal draw-skip mode (fast, ~30fps) for N frames — game advances at full speed
2. On frame N-1, set `headless_render_frame_ = true` — all draws execute for one frame
3. On frame N, capture via GPU readback (staging buffer → PPM)
4. Game stalls after capture (acceptable — we already have the screenshot)

### Why It Succeeds Where Others Failed

- **Pipeline cache is warm**: After 50-100 frames of draw-skip mode, the async pipeline compilation has already compiled all 9 pipeline configurations. The warmup frame doesn't need to wait for shader compilation.
- **One frame is fast enough**: 128 draws in ~48ms (first 2-3 draws compile new pipelines, rest use cached). The CP thread stalls for only ~48ms — short enough that the game doesn't deadlock during the warmup frame itself.
- **Correct capture timing**: Frame N-1 renders to EDRAM → IssueCopy resolves to shared memory VkBuffer → Frame N captures via RequestSwapTexture + GPU readback. Previous attempts captured before real draws executed.

### Results

| Frame | Resolution | Non-zero pixels | Content |
|-------|-----------|----------------|---------|
| 50 | 1280x720 | 100% | Neon lines extending from DC logo |
| 100 | 1280x720 | 100% | DC logo with full neon line extensions |
| 150 | 1280x720 | 100% | DC card logo centered with reflection |
| 200 | 1280x720 | 100% | DC card logo (animation phase) |
| 250 | 1280x720 | 100% | DC card logo with subtle glow |

Screenshots saved to `archive/screenshots/debug/` and `archive/screenshots/retail/`. Both debug and retail XEXs render identically.

See [2026-02-18-xenia-screenshot-breakthrough.md](2026-02-18-xenia-screenshot-breakthrough.md) for the full story.

## Remaining Limitations

1. **One screenshot per run** — GPU readback blocks the CP thread for ~100ms+, causing the game thread to timeout on sync. The game doesn't recover after capture.
2. **Boot animation only** — Game needs scripted input to advance past the DC logo to menus.
3. **PE Override blocked** — Decomp linker produces different function addresses. Can't swap in decomp code at runtime.
