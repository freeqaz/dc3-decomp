# Xenia Headless: From Black Frames to Rendered Screenshots

**Date:** 2026-02-18
**Outcome:** First-ever rendered screenshots of DC3 running in xenia-headless
**Screenshots:** `archive/screenshots/debug/` and `archive/screenshots/retail/`

## The Goal

Capture actual rendered screenshots from Dance Central 3 running in xenia-headless with the Vulkan GPU backend. Both the debug XEX (`orig/373307D9/default.xex`) and retail XEX (`orig-assets/default.xex`).

## Where We Started

DC3 was already booting and running at ~30fps in xenia-headless with the null GPU backend (no rendering). With the Vulkan backend, a frame capture pipeline existed that produced PPM files — but they were all black. Non-copy draws had to be skipped because they deadlocked the game after ~12 frames.

**The deadlock:** The Xbox 360 Command Processor (CP) processes PM4 packets sequentially. Each new shader/pipeline takes 10-100ms to compile. During compilation, the CP can't process sync packets (EVENT_WRITE_SHD). The game's main thread polls for these sync values and deadlocks when they never appear.

## The Journey

### Attempt 1: Force All Draws (Failed)

Just remove the draw skip guard. Accept the 12-frame stall and see if any frames have content.

**Result:** 12-13 frames captured, all black. The initial frames are GPU initialization (48 single-point draws) — no meaningful content renders before the deadlock.

### Attempt 2: GPU Trace Capture + Replay (Failed)

Use xenia's `--trace_gpu_stream` to capture PM4 commands to a `.xtr` file, then replay offline with `xenia-gpu-vulkan-trace-dump`.

**Result:** The trace captured 18MB of PM4 packets, but `InitializeTrace()` only saves registers and gamma ramp — NOT physical memory (vertex buffers, textures). Trace replay would lack all the data needed to actually render. Dead end.

### Attempt 3: PM4 Packet Analysis (Diagnostic)

Added logging to understand the PM4 packet sequence around draws:

```
DRAW_INDX_2 (0x36) × N    ← non-copy draws
WAIT_REG_MEM (0x3C) × 2   ← sync checks
EVENT_WRITE_EXT (0x64)     ← sync event
DRAW_INDX (copy)           ← EDRAM resolve
XE_SWAP                    ← frame boundary
```

**Key finding:** Sync events come AFTER draws in the PM4 stream. When draws are slow, the CP never reaches the sync events. This confirmed the deadlock mechanism and pointed toward the solution: process sync events immediately, defer the actual draw work.

### Attempt 4: Deferred Draw System (Partially Worked)

Defer non-copy draws by saving register state snapshots (~80KB each). Execute them all at once when a copy draw or XE_SWAP is reached (after sync events have been processed).

**Implementation:**
- Save full register file (20,483 registers × 4 bytes) per deferred draw
- Save vertex/pixel shader pointers and draw parameters
- At copy/swap time, restore each snapshot and call IssueDraw

**Result:** The deferred system worked mechanically — 48 draws deferred and flushed in 0ms (trivial draws), 1 real draw flushed in 11ms. But the game stalled permanently after the flush. Even 11ms of draw execution during the flush blocked the CP long enough for the game thread to lose sync.

**Validation:** Made FlushDeferredDraws a no-op (defer but never execute) — game ran at full 433 VdSwaps/8s. Confirmed the stall was caused by actual draw execution, not the deferral mechanism.

### Attempt 5: Warmup Frame Capture (Success!)

Instead of executing draws every frame, use the existing warmup system:
1. Run in normal draw-skip mode (fast, ~30fps) for N frames
2. On frame N-1, enable `headless_render_frame_` — draws execute for one frame only
3. On frame N, capture via GPU readback (staging buffer → PPM)
4. The game stalls after capture, but we already have the screenshot

**Key insight:** By frame 50-100, the game has loaded all shaders and textures for the boot animation. The warmup frame compiles all needed pipelines (128 draws in ~48ms) and renders to EDRAM. The copy draw resolves EDRAM to the shared memory buffer. The capture reads the swap texture from the GPU via a staging buffer.

**Result:** frame_0100.ppm — **921,600/921,600 non-zero pixels (100%)** — the DC3 neon logo, fully rendered.

## The Solution Architecture

```
Frames 0 to N-2:  (fast, ~30fps)
├─ Non-copy draws → SKIP (return true)
├─ Copy draws → IssueCopy (fast, no EDRAM data)
├─ Sync events → process immediately
└─ VdSwap → advance counter, no capture

Frame N-1:  (warmup, ~48ms)
├─ headless_render_frame_ = true
├─ Non-copy draws → EXECUTE (shader compile + pipeline + draw)
│   First draw: ~28ms (new pipeline)
│   Subsequent: <1ms each (cached)
├─ Copy draws → IssueCopy (EDRAM has real data now!)
└─ VdSwap → log timing

Frame N:  (capture)
├─ EndSubmission → flush Vulkan commands
├─ AwaitAllQueueOperationsCompletion → GPU finishes rendering
├─ RequestSwapTexture → texture_cache loads swap texture
├─ vkCmdCopyImageToBuffer → GPU→CPU staging copy
├─ vkWaitForFences → wait for copy
├─ Map + write PPM → actual pixels!
└─ Game stalls (CP blocked during readback, game thread times out)
```

## Why Previous Readback Attempts Failed

The frame capture code was working all along — the issue was WHEN it ran:

1. **Black frames (attempts 1-3):** Capture happened before any real draws executed. EDRAM was empty, swap texture was zero.
2. **Force_all_draws + deferred (attempt 4):** The real draw (prim=6, idx=4) executed and IssueCopy resolved EDRAM to the shared memory buffer — but the capture was timed BEFORE the draw (capture_interval=5, draw at frame 6).
3. **Warmup (attempt 5):** Capture is timed correctly — frame N-1 renders, frame N captures. The pipeline cache is warm from 100+ frames of draw-skip mode, so pipeline compilation is fast.

## Performance Data

### Pipeline Warmup (120-second run, no readback)

- 3,931 swaps at 33.0 fps average
- Only 3 stalls >50ms, all in first 0.5 seconds:
  - swap 1: 102ms (first pipeline)
  - swap 12: 100ms (second variant)
  - swap 13: 54ms (third variant)
- After 0.5 seconds: zero stalls, median frame time 29.9ms, p99 33.1ms
- Game is locked at its internal ~33.3fps timestep

### Warmup Frame Timing

```
Frame 99 timing: 48ms total, 128 draws
  First draw: 27.7ms (pipeline creation)
  Draw 2: 4.8ms (second pipeline variant)
  Draw 3: 1.5ms
  Draw 4: 10μs (cached pipeline)
  Draw 5+: 3-5μs each
```

### Capture Overhead

The capture itself is what kills performance — two full GPU drains plus staging buffer allocation and PPM write. This is why the game stalls after capture: the CP thread is blocked for ~100ms+ during readback, causing the game thread to timeout on sync.

## Screenshots Captured

All from the DC3 boot animation (the "DC" art deco card design with gold neon glow):

| File | Frame | Content |
|------|-------|---------|
| `debug/boot_frame_050.png` | 50 | Neon lines extending, early animation |
| `debug/boot_frame_100.png` | 100 | DC logo with neon lines fully extended |
| `debug/boot_frame_150.png` | 150 | DC card logo centered, reflection below |
| `debug/boot_frame_200.png` | 200 | DC card logo, slightly different phase |
| `debug/boot_frame_250.png` | 250 | DC card logo, subtle glow differences |
| `retail/boot_frame_100.png` | 100 | Same as debug — renders identically |

Frames 350+ show a half-black/half-red split — the double-buffered frontbuffer with the game transitioning between boot phases.

## What We Learned

1. **Async pipeline compilation is necessary but not sufficient.** Even with instant pipelines, the draw call chain (shader analysis, binding, submission) exceeds the CP's timing budget.

2. **Deferred draws work mechanically but cause timing issues.** Saving and restoring 80KB of register state per draw is feasible, but executing deferred draws blocks the CP long enough to break game synchronization.

3. **Frame-selective rendering is the practical solution.** Skip draws for 99% of frames (maintaining full game speed), execute draws for exactly one frame, capture, accept the stall.

4. **The Vulkan draw path is fast once warmed up.** After 0.5 seconds of pipeline compilation, draws execute at sub-microsecond cost. The 48ms warmup frame is dominated by the first 2-3 new pipelines.

5. **Guest memory vs GPU memory is a key distinction.** IssueCopy resolves EDRAM to the shared memory VkBuffer (GPU-local), not to guest physical memory (CPU). The capture must read from the Vulkan texture cache, not from guest memory at the frontbuffer address.

## Reproducing

```bash
# Capture debug XEX at frame 100
cd ~/code/milohax/xenia
./build/bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig/373307D9/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=100 \
    --headless_timeout_ms=15000

# Capture retail XEX
./build/bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig-assets/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=100 \
    --headless_timeout_ms=15000

# Convert PPM to PNG
python3 -c "from PIL import Image; Image.open('/tmp/frames/frame_0100.ppm').save('screenshot.png')"
```

## Next Steps

- **Multi-frame capture per run** — reduce readback overhead to avoid the one-frame stall
- **Later game states** — tune scripted input to advance past boot to loading screens and menus
- **Debug vs retail comparison** — capture corresponding frames to identify behavioral differences
