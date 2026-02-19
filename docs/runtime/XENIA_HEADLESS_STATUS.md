# Xenia Headless - Runtime Investigation

## Overview

We're running the original DC3 XEX in xenia-headless to compare behavior between the original and decomp binaries. This document tracks the investigation status.

## Build Location

- Xenia source: `/home/free/code/milohax/xenia/` (from `xenia-project/xenia` main fork)
- Headless binary: `build/bin/Linux/Checked/xenia-headless`
- Built with premake5 + gmake2, `config=checked_linux`

## Current Status (2026-02-19)

### Screenshots Captured

DC3 boot animation successfully rendered and captured from both debug and retail XEXs. Multi-frame capture via sequential runs captures the full boot animation sequence.

| Frame | Content |
|-------|---------|
| 50 | Neon lines extending — early boot animation |
| 100 | "DC" logo forming with neon line extensions |
| 150 | DC card logo centered with reflection |
| 200 | DC card logo framed, fully formed with reflection |
| 250 | DC card logo (same, slight glow variation) |
| 275 | DC card tilting/shrinking — animation ending |
| 300+ | Screen transition (half black / half dark red) |

### Performance Summary

| Configuration | Swaps/20s | FPS | Notes |
|---|---|---|---|
| Null GPU (no draws) | 600+ | ~30 | Baseline — game logic only |
| Vulkan all draws, no readback | 611 | ~30.5 | Full speed, async pipelines |
| Vulkan 120s sustained run | 3,931/120s | ~33 | Locked at game's internal timestep |
| Warmup frame capture | 1 frame/run | — | Game dies after rendering a frame |

### What Works

- XEX boots successfully in xenia-headless (both debug and retail)
- All 707 imports resolved (347 thunks + 360 variables)
- 36,000+ draw calls, 600+ swaps at ~30fps
- 16 game threads spawned and running
- Vulkan rendering at full game speed with async pipeline compilation
- Frame capture via GPU readback with pre-allocated resources (3ms total stall)
- Multi-frame capture via sequential runs (one GPU-rendered frame per run)
- Scripted controller input (`--scripted_input='5s:A,7s:START'`)
- Pipeline cache warms up in <10 seconds (only 2-3 initial stalls)

### What's Limited

- **One screenshot per run** — **the rendering itself** (not the readback) kills the game. When `headless_render_frame_` is enabled, GPU draws execute on the CP thread, blocking PM4 packet processing. The game thread's sync mechanism detects the stall and stops issuing VdSwap calls. The GPU readback was optimized to 3ms with pre-allocated staging buffers, but the draw execution on the render frame takes much longer and is the actual bottleneck.
- **Boot animation only** — game needs scripted input to advance past the DC logo to menus. Current input timing doesn't get past the boot screen within the capture window.
- **PE Override blocked** — decomp linker produces different function addresses (+0x1600 .text shift + non-uniform per-function offsets). Can't swap in decomp code at runtime.

### Root Cause: Rendering Kills the Game

The CP (Command Processor) thread processes PM4 packets from the game's ring buffer. During normal headless operation, non-copy draws are skipped (instant return), keeping the CP responsive for sync events. When rendering is enabled for capture, draws execute fully (shader compile + pipeline creation + Vulkan draw). This blocks the CP long enough that:

1. Game thread writes PM4 sync events (EVENT_WRITE_SHD, WAIT_REG_MEM)
2. CP can't process them while executing draws
3. Game thread times out waiting for sync acknowledgment
4. Game stops issuing VdSwap calls → no more frames

Evidence:
- No rendering, no capture: 277 swaps/10s (healthy)
- Rendering enabled, capture disabled: 50 swaps then death
- force_all_draws (all frames rendered): 12 swaps then death
- GPU readback stall: only 3ms (not the bottleneck)

### Fix Approaches for Multi-Frame Capture (TODO)

1. **Separate render thread** — Execute draws on a background thread instead of the CP thread. CP processes sync events while rendering happens in parallel. Requires careful synchronization for EDRAM state and submission ordering.
2. **Incremental rendering** — Time-budget draws (e.g., 2ms per frame). Execute only a few draws per frame, accumulating over multiple frames until the EDRAM has full content. Then capture.
3. **EDRAM snapshot** — Save EDRAM state after the one successful render frame. On subsequent runs, restore EDRAM state without re-rendering, then do the resolve + readback.
4. **Direct EDRAM readback** — Read EDRAM contents directly instead of going through RequestSwapTexture. May avoid the need to render at all if EDRAM retains content from copy operations.

## Changes Made

### XAudio2 Render Driver Fix (completed) — CRITICAL FIX

**Root cause of stuck main thread:** The nop audio backend's `CreateDriver()` returned `X_STATUS_NOT_IMPLEMENTED`, causing `XAudioRegisterRenderDriverClient` to fail. The XAudio2 static library code (`CX2SourceVoice::Initialize`) then spun forever waiting for the render driver tic counter to advance — but since no driver was registered, the tic never advanced.

**Files modified:**
- `src/xenia/kernel/xboxkrnl/xboxkrnl_audio.cc`:
  1. `XAudioRegisterRenderDriverClient` — Returns dummy handle (0x41550000) when `CreateDriver` fails
  2. `XAudioGetRenderDriverTic` — Returns monotonically increasing tic counter based on host uptime (~200Hz)
  3. `XAudioSubmitRenderDriverFrame` / `XAudioUnregisterRenderDriverClient` — Skip operations for dummy handles

### Async I/O Fix (completed)

**Root cause:** Xenia's `NtReadFile` completed reads synchronously but returned `STATUS_PENDING` for async file handles. `GetOverlappedResult` then busy-polled forever.

**Files modified:**
- `src/xenia/kernel/xboxkrnl/xboxkrnl_io.cc` — Removed `STATUS_PENDING` return for completed reads

### XAM Function Implementations (completed)

- `GetLocalTime`, `GetSystemTime`, `GetTickCount` — Time functions
- `OutputDebugStringA/W` — Debug output

### Vulkan GPU Backend (completed)

**Files modified:**
- `src/xenia/app/xenia_headless_main.cc` — `--gpu` flag, `--scripted_input`, `--force_all_draws`, `--headless_async_draws`
- `src/xenia/gpu/vulkan/vulkan_pipeline_cache.h/cc` — Async pipeline compilation, warmup wait mode
- `src/xenia/gpu/vulkan/vulkan_command_processor.h/cc`:
  - Non-copy draw skip in headless mode (with warmup frame exception)
  - Deferred draw system for force_all_draws mode
  - GPU readback via staging buffer for frame capture
  - BeginSubmission non-blocking fence check (prevents CP deadlock)
  - Frame-selective rendering with configurable capture interval
- `src/xenia/hid/nop/nop_input_driver.cc` — Scripted input parsing and playback

### x64 JIT Calling Convention Fix (completed)

- Fixed `EmitHostToGuestThunk()`, `EmitGuestToHostThunk()`, `EmitResolveFunctionThunk()` for Linux System V ABI

### Memory Aliasing Fix (completed)

- `MapFileView()`: `MAP_PRIVATE | MAP_ANONYMOUS` → `MAP_SHARED | MAP_FIXED`
- `AllocFixed()`: `mmap(MAP_PRIVATE | MAP_ANONYMOUS)` → `mprotect()`

## Architecture Notes

### CP Thread Deadlock Problem (Solved)

The Xbox 360 Command Processor processes PM4 packets sequentially. Non-copy draws require shader compilation + pipeline creation (10-100ms). This blocks sync packets (EVENT_WRITE_SHD, PM4_INTERRUPT) that the game thread polls, causing deadlock after ~12 frames.

**Solution:** Two-pronged approach:
1. **Async pipeline compilation** — compile shaders on background threads, skip draws until pipeline ready
2. **Frame-selective rendering** — only execute draws on the one frame before capture, skip all others

### Frame Capture Pipeline

```
Normal headless operation (fast, ~30fps):
├─ Non-copy draws → skip (return true immediately)
├─ Copy draws → IssueCopy (EDRAM resolve)
├─ Sync events → process immediately
└─ XE_SWAP → IssueSwap → advance frame counter

Warmup frame (frame N-1 before capture):
├─ headless_render_frame_ = true
├─ Non-copy draws → execute fully (shader compile + pipeline + Vulkan draw)
├─ Copy draws → IssueCopy (EDRAM resolve to shared memory)
├─ *** CP thread blocked during draws — game thread sync times out ***
└─ XE_SWAP → frame timing logged

Capture frame (frame N):
├─ AwaitAllQueueOperationsCompletion → wait for GPU (pre-allocated fence)
├─ RequestSwapTexture → load swap texture from shared memory into VkImage
├─ vkCmdCopyImageToBuffer → copy to pre-allocated staging buffer
├─ vkWaitForFences → wait for copy (pre-allocated, ~0ms)
├─ Read from persistent mapping → write PPM file (3ms total)
└─ Game already dead from rendering stall at frame N-1
```

**Pre-allocated readback resources** (created in SetupContext, destroyed in ShutdownContext):
- `readback_staging_buffer_` / `readback_staging_memory_` — 1920x1080x4 host-visible buffer, persistently mapped
- `readback_command_pool_` / `readback_command_buffer_` — resettable command pool + one persistent CB
- `readback_fence_` — reusable fence for copy submission
- `readback_cpu_buffer_` — CPU-side pixel buffer for async file write

### Thread Architecture

| Thread | Handle | Role | Status |
|---|---|---|---|
| GPU Commands | F8000004 | Processes GPU ring buffer commands | Active |
| GPU VSync | F8000008 | Fires vsync interrupts | Active |
| Main XThread | F8000028 | Game's main thread | Running (main loop) |
| Game Threads 7-18 | F8000088+ | D3D workers, loaders, audio | Running |

## How to Reproduce

```bash
# Build
cd ~/code/milohax/xenia
cd build && make xenia-headless config=checked_linux -j$(nproc)

# Run with null GPU (fast, no rendering)
./bin/Linux/Checked/xenia-headless --gpu=null \
    --target=~/code/milohax/dc3-decomp/orig/373307D9/default.xex \
    --headless_timeout_ms=20000

# Run with Vulkan + capture at frame 100
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig/373307D9/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=100 \
    --headless_timeout_ms=15000

# Run with scripted input
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig/373307D9/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=200 \
    --scripted_input='5s:A,7s:START,10s:A' \
    --headless_timeout_ms=25000

# Note: game data (gen/main_xbox.hdr, .ark files) must be accessible
# orig/373307D9/gen is symlinked to orig-assets/gen
```

## PE Override Feature (implemented, blocked)

A `--pe_override` flag loads the original XEX then replaces PE sections with a decomp binary. Re-patches all 347 import thunks and 360 variable imports.

**Status: BLOCKED** — decomp linker produces functions at different addresses:
- Original `.text`: VA `0x330000` (base `0x82330000`)
- Decomp `.text`: VA `0x331600` (base `0x82331600`)
- Functions have non-uniform offsets within sections

**To fix:** Requires matching linker layout (COMDAT order file) or per-function patching.

## Next Steps

1. **Non-blocking rendering** — the #1 priority. Rendering on the CP thread kills the game because it blocks sync event processing. Approaches:
   - **Background render thread**: Execute deferred draws on a separate thread, signal completion via fence. CP thread continues processing PM4.
   - **Time-budgeted draws**: Execute only N draws per swap (2ms budget), accumulate EDRAM over multiple frames.
   - **EDRAM snapshot/restore**: Render once, save EDRAM state, restore on subsequent runs without re-rendering.
2. **Advance past boot** — tune scripted input timing to navigate through the DC logo to menus. Current frames show the boot animation ends around frame 275 (card tilts away), then fades to black/red at 300+. Need to capture frames 400-800 to see what screen appears next.
3. **Matching linker layout** — generate COMDAT order file from original map to enable PE override
4. **Compare debug vs retail rendering** — capture corresponding frames from both XEXs to identify differences
