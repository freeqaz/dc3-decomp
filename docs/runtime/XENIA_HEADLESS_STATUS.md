# Xenia Headless - Runtime Investigation

## Overview

We're running the original DC3 XEX in xenia-headless to compare behavior between the original and decomp binaries. This document tracks the investigation status.

## Build Location

- Xenia source: `/home/free/code/milohax/xenia/` (from `xenia-project/xenia` main fork)
- Headless binary: `build/bin/Linux/Checked/xenia-headless`
- Built with premake5 + gmake2, `config=checked_linux`

## Current Status (2026-02-20)

### Multi-Frame Capture: WORKING

Multi-frame capture is fully operational. 14+ captures per run confirmed with deferred draws, 15-35ms readback latency per frame.

### Rendering Quality: PARTIALLY FIXED

Cache invalidation fix applied — deferred draws now produce real rendered content (151K bright pixels, R=255 warm tones) instead of flat B=0x3F.

| Mode | Behavior | Render Quality |
|------|----------|----------------|
| **Inline draws** (Feb 19) | Deadlocks at frame 12 | Correct DC logo (orange/gold neon) |
| **Deferred draws** (Feb 20, pre-fix) | Runs indefinitely | Wrong colors (solid B=0x3F everywhere) |
| **Deferred draws** (Feb 20, post-fix) | Runs indefinitely, 16+ threads | **Real content: 525 unique colors, R=255 warm tones** |
| **Force_all_draws** (Feb 20) | Runs indefinitely, 16+ threads | Flat R=2,G=1,B=2 (different failure mode) |

**Root cause (B=0x3F)**: `FlushDeferredDraws()` raw memcpy bypassed `WriteRegister()` side effects, leaving constant buffer and texture binding caches stale. Fix: invalidate `current_constant_buffers_up_to_date_` and `texture_bindings_in_sync_` after register restore.

**Remaining**: Overall frame still dark despite real content — possible gamma ramp or readback format issue.

### Screenshots Captured (Feb 19, inline draws)

DC3 boot animation successfully rendered and captured via inline draws (single capture per run due to frame 12 deadlock).

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
| Multi-frame capture (every 100 frames) | 700+ | ~33 | Game survives, 15-35ms per capture |

### What Works

- XEX boots successfully in xenia-headless (both debug and retail)
- All 707 imports resolved (347 thunks + 360 variables)
- 36,000+ draw calls, 600+ swaps at ~30fps
- 16 game threads spawned and running
- Vulkan rendering at full game speed with async pipeline compilation
- **Multi-frame capture** — 7+ screenshots per run with game surviving
- GPU readback with pre-allocated resources (15-35ms per capture)
- Scripted controller input (`--scripted_input='5s:A,7s:START'`)
- Pipeline cache warms up in <10 seconds (only 2-3 initial stalls)

### What's Limited

- **Rendering partially dark** — deferred draws now produce real content (151K bright pixels, warm tones) but overall frame is dark. Possible gamma ramp or readback format issue.
- **force_all_draws flat color** — E10 mode produces uniform R=2,G=1,B=2 (different failure from deferred).
- **Inline draw deadlock** — still deadlocks at frame 12 (untested post-fix).
- **Boot animation only** — game needs scripted input to advance past the DC logo to menus.
- **PE Override blocked** — decomp linker produces different function addresses (+0x1600 .text shift + non-uniform per-function offsets).

## Root Cause Analysis: Why Draws Killed the Game (SOLVED)

### The Problem

Executing ANY Vulkan draw permanently killed the game thread ~2 frames later. The game thread stopped calling VdSwap and most threads parked at idle/wait state.

### Bisection Results

Systematic stage-by-stage bisection inside `IssueDraw()` identified the exact culprit:

| Draw Stage | Game Survives? | Conclusion |
|---|---|---|
| BeginSubmission + EndSubmission (no work) | YES (345+ frames) | Vulkan submission machinery is fine |
| + PrimitiveProcessor::Process | YES | Vertex/index processing safe |
| + Shader translation + samplers | YES | Shader compilation safe |
| + RenderTargetCache::Update | YES (527 frames) | EDRAM management safe |
| + ConfigurePipeline | YES (529 frames) | Pipeline creation safe |
| + **TextureCache::RequestTextures** | **NO (9 frames)** | **CULPRIT** |
| + SharedMemory::RequestRange | NO (9 frames) | Also triggers page watches |
| + Full draw (vkCmdDraw) | NO (9 frames) | Same, cascading effect |

### Root Cause: SharedMemory Page Watch Contention

`TextureCache::RequestTextures` → `LoadTextureData` → `shared_memory().RequestRange()` → `UploadRanges` → `MakeRangeValid` → `EnablePhysicalMemoryAccessCallbacks`

This chain sets `mprotect(PROT_READ)` on guest physical memory pages containing texture data. When the game thread subsequently writes to any of these watched pages:

1. **SIGSEGV fires** (permission violation — game thread wrote to read-only page)
2. **Signal handler** acquires `global_critical_region_` mutex (recursive mutex shared between CP and game threads)
3. **Handler unprotects page** and marks it dirty
4. **Mutex contention**: If the CP thread holds `global_critical_region_` during draw execution, the game thread's signal handler blocks until the CP releases it

This created a timing interference pattern:
- 1,106,634 SIGSEGVs observed via strace across 3 unique guest addresses on 3 different threads
- All were `SEGV_ACCERR` (permission violations from the mprotect page watches)
- The signal handler locking disrupted VdSwap timing, causing the game's sync mechanism to time out

### The Fix: `suppress_memory_watches_`

Added a `suppress_memory_watches_` flag to `SharedMemory` that prevents `EnablePhysicalMemoryAccessCallbacks` from being called during headless deferred draw execution:

**Files modified:**
- `src/xenia/gpu/shared_memory.h` — Added `set_suppress_memory_watches(bool)` setter and `suppress_memory_watches_` member
- `src/xenia/gpu/shared_memory.cc` — `MakeRangeValid` checks `!suppress_memory_watches_` before calling `EnablePhysicalMemoryAccessCallbacks`
- `src/xenia/gpu/vulkan/vulkan_command_processor.cc` — `FlushDeferredDraws()` sets flag before executing deferred draws, clears after

This is safe for headless mode because:
- Deferred draws are one-shot (render a single frame for capture)
- No persistent page tracking is needed since we don't re-render the same frame
- Texture data is freshly uploaded during the deferred draw, so watches for future invalidation are unnecessary

### Deferred Draw Architecture

```
Frame N-1 (RENDER+DEFER):
├─ IssueSwap: headless_render_frame_ = true, deferred_draws_enabled_ = true
└─ Returns — PM4 processing continues

PM4 stream for frame N (between swap N-1 and swap N):
├─ Register writes → processed normally
├─ Draws → headless_render_frame_ allows, deferred_draws_enabled_ saves state
│  └─ 128 draws + 6 copies saved to deferred_draws_ vector
├─ EVENT_WRITE_SHD → processed normally (writes counter to guest memory)
├─ WAIT_REG_MEM → processed normally (checks guest memory)
└─ (sync events process IMMEDIATELY, not blocked by draws)

Frame N (CAPTURE):
├─ IssueSwap: FlushDeferredDraws() ← executes 134 ops (draws + copies)
│  ├─ suppress_memory_watches_ = true (prevent page watch contention)
│  ├─ Each draw: restore saved register state → IssueDraw → Vulkan work
│  ├─ Each copy: restore vertex data → IssueCopy → EDRAM resolve
│  ├─ suppress_memory_watches_ = false (restore normal behavior)
│  └─ Restore original register state
├─ EndSubmission(true)
├─ Readback: AwaitAll → RequestSwapTexture → image copy → PPM write
├─ Schedule next RENDER+DEFER for capture_interval frames ahead
└─ Game thread: continues running normally (700+ frames and counting)
```

## Changes Made

### SharedMemory Page Watch Suppression (completed) — ROOT CAUSE FIX

**Root cause of draw-kills-game:** `MakeRangeValid()` sets mprotect page watches during texture/vertex uploads. Signal handler mutex contention between CP thread and game thread killed VdSwap timing.

**Files modified:**
- `src/xenia/gpu/shared_memory.h` — `set_suppress_memory_watches()`, `suppress_memory_watches_` member
- `src/xenia/gpu/shared_memory.cc` — Guard `EnablePhysicalMemoryAccessCallbacks` with `!suppress_memory_watches_`
- `src/xenia/gpu/vulkan/vulkan_command_processor.cc` — Set flag in `FlushDeferredDraws()`

### XAudio2 Render Driver Fix (completed) — CRITICAL FIX

**Root cause of stuck main thread:** The nop audio backend's `CreateDriver()` returned `X_STATUS_NOT_IMPLEMENTED`, causing `XAudioRegisterRenderDriverClient` to fail. The XAudio2 static library code (`CX2SourceVoice::Initialize`) then spun forever waiting for the render driver tic counter to advance.

**Files modified:**
- `src/xenia/kernel/xboxkrnl/xboxkrnl_audio.cc`:
  1. `XAudioRegisterRenderDriverClient` — Returns dummy handle when CreateDriver fails
  2. `XAudioGetRenderDriverTic` — Returns monotonically increasing tic counter (~200Hz)
  3. `XAudioSubmitRenderDriverFrame` / `XAudioUnregisterRenderDriverClient` — Skip for dummy handles

### Async I/O Fix (completed)

- `src/xenia/kernel/xboxkrnl/xboxkrnl_io.cc` — Removed `STATUS_PENDING` return for completed reads

### XAM Function Implementations (completed)

- `GetLocalTime`, `GetSystemTime`, `GetTickCount` — Time functions
- `OutputDebugStringA/W` — Debug output

### Vulkan GPU Backend (completed)

**Files modified:**
- `src/xenia/app/xenia_headless_main.cc` — `--gpu` flag, `--scripted_input`, `--force_all_draws`
- `src/xenia/gpu/vulkan/vulkan_pipeline_cache.h/cc` — Async pipeline compilation, warmup wait mode
- `src/xenia/gpu/vulkan/vulkan_command_processor.h/cc`:
  - Non-copy draw skip in headless mode (with warmup frame exception)
  - Deferred draw system: save register state at draw time, replay at swap time
  - Copy deferral with vertex data save/restore (24 bytes per copy)
  - GPU readback with pre-allocated staging buffer + persistent mapping
  - Frame-selective rendering with configurable capture interval
  - Multi-frame capture with automatic RENDER+DEFER scheduling
- `src/xenia/gpu/command_processor.cc` — Post-capture WAIT_REG_MEM detection
- `src/xenia/app/emulator_headless.cc` — Thread status reporting, PPC stack walking
- `src/xenia/hid/nop/nop_input_driver.cc` — Scripted input parsing and playback

### Kernel Shim Deadlock Fix (completed)

- `src/xenia/kernel/util/shim_utils.h` — `AppendParam` for `lpdword_t`, `lpqword_t`, `lpfloat_t`, `lpdouble_t` no longer dereferences the guest pointer. The old code triggered SIGSEGV on mprotect-watched pages, deadlocking in the MMIO handler.

### XAM UI / Sign-in / Device Selection Stubs (completed)

- `src/xenia/kernel/xam/xam_ui.cc`:
  - `XamShowMessageBoxUI` — logs title/text/button details, auto-picks active button
  - `XamShowNuiSigninUI` — broadcasts `XN_SYS_SIGNINCHANGED` + `XN_SYS_UI` notifications so PlatformMgr detects sign-in
  - `XamShowNuiDeviceSelectorUI` — returns valid device ID (0x00020000 = HDD) via overlapped callback
- `src/xenia/kernel/xam/xam_content.cc` / `xam_content_device.cc` — content enumeration stubs
- `src/xenia/kernel/xam/xam_notify.cc` — notification dequeue logging

### XBDM Stubs (completed)

- `src/xenia/kernel/xbdm/xbdm_misc.cc` — `DmGetSystemInfo` returns zeroed struct (game just checks it doesn't fail)

### Thread Diagnostics (completed, cleaned up)

- `src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc` — Main-thread wait/delay tracing, gated behind `--headless_thread_diagnostics` cvar
- `src/xenia/base/threading.h` / `threading_posix.cc` — `SuspendThread`/`ResumeThread` support via `pthread_kill` + signal handler for RIP capture
- Removed: `MonitorMainThreadPC()` dead code (90 lines, never called)
- Removed: unsafe `backtrace()` from signal handler (not async-signal-safe)

### x64 JIT Calling Convention Fix (completed)

- Fixed `EmitHostToGuestThunk()`, `EmitGuestToHostThunk()`, `EmitResolveFunctionThunk()` for Linux System V ABI

### Memory Aliasing Fix (completed)

- `MapFileView()`: `MAP_PRIVATE | MAP_ANONYMOUS` → `MAP_SHARED | MAP_FIXED`
- `AllocFixed()`: `mmap(MAP_PRIVATE | MAP_ANONYMOUS)` → `mprotect()`

## Pre-allocated Readback Resources

Created in SetupContext, destroyed in ShutdownContext. Eliminates per-frame allocation:

- `readback_staging_buffer_` / `readback_staging_memory_` — 1920x1080x4 host-visible buffer, persistently mapped
- `readback_command_pool_` / `readback_command_buffer_` — resettable command pool + one persistent CB
- `readback_fence_` — reusable fence for copy submission

## Thread Architecture

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
    --target=~/code/milohax/dc3-decomp/orig-assets/default.xex \
    --headless_timeout_ms=20000

# Run with Vulkan + multi-frame capture (every 100 frames)
# --force_all_draws is required for captures to contain rendered content
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig-assets/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=100 \
    --headless_timeout_ms=30000 --force_all_draws=true

# Run with scripted input
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig-assets/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=200 \
    --scripted_input='5s:A,7s:START,10s:A' \
    --headless_timeout_ms=25000 --force_all_draws=true

# Note: game data (gen/main_xbox.hdr, .ark files) must be accessible
# orig/373307D9/gen is symlinked to orig-assets/gen
```

## PE Override Feature (implemented, blocked)

A `--pe_override` flag loads the original XEX then replaces PE sections with a decomp binary. Re-patches all 347 import thunks and 360 variable imports.

**Status: BLOCKED** — decomp linker produces functions at different addresses. Need matching linker layout.

## Rendering Investigation (2026-02-20)

### B=0x3F Root Cause: FOUND AND FIXED

`FlushDeferredDraws()` restored register state via raw `memcpy`, bypassing `VulkanCommandProcessor::WriteRegister()`. WriteRegister has side effects:
1. Marks `current_constant_buffers_up_to_date_` dirty for shader constants
2. Calls `TextureFetchConstantWritten()` to mark `texture_bindings_in_sync_` dirty

Without these notifications, deferred draws rendered with stale constant data and textures — producing the B=0x3F default EDRAM content.

**Fix**: After memcpy in FlushDeferredDraws, invalidate both caches:
```cpp
current_constant_buffers_up_to_date_ = 0;
texture_cache_->ResetTextureBindingsInSync();
```

### Post-Fix Results (E20: deferred draws, 60s)

- 3 captures at frames 100/200/300
- frame_0100.ppm: 283K non-background pixels, 525 unique RGB triples
- Brightest pixels: R=255 G=232 B=127 (warm gold/yellow — matches DC logo palette)
- Content concentrated at x=[500-720], y=[260-490]
- Row variation peaks at 500+ unique colors per row in the active area

### Remaining Issues

1. **Overall frame darkness**: While content is real, most of the frame is very dark (max brightness in top colors is R=17, G=3, B=7 outside the hot spot). Possible causes:
   - Gamma ramp not applied in readback path
   - Format conversion issue in staging buffer copy
   - Readback endianness partially wrong

2. **E10 (force_all_draws) flat color**: Produces uniform R=2,G=1,B=2. Different failure from E20 — may be capture timing or different draw scheduling path.

3. **Inline draw deadlock**: Still present at frame 12 (not tested post-fix).

### Previously Tested Hypotheses

| Hypothesis | Result |
|-----------|--------|
| BeginSubmission blocking causes deadlock | NO — hangs with both blocking and non-blocking |
| VBlank missing in headless | NO — VSync worker fires every 16ms |
| Format/endian mismatch in capture pipeline | NO — raw bytes confirm correct R8G8B8A8 interpretation |
| Missing barriers between draws and resolve | NO — added EndSubmission + AwaitAll |
| **Stale constant/texture cache in deferred replay** | **YES — ROOT CAUSE. Fixed with cache invalidation.** |

### Next Debugging Steps

1. **Gamma ramp investigation** — check if gamma ramp tables are applied during readback, compare raw EDRAM values vs post-gamma
2. **Vulkan validation** (`--vulkan_validation`) — verify no API errors in deferred replay path
3. **Inline draw comparison** — if deadlock can be worked around, compare inline vs deferred output quality
4. **Dense capture** — capture every 10 frames to see rendering progression through boot animation

## Available Xenia Debug Flags

Useful flags for debugging the rendering pipeline:

| Flag | Description |
|------|-------------|
| `--vulkan_validation` | Enable VK_LAYER_KHRONOS_validation |
| `--trace_gpu_stream` | Record all GPU PM4 packets to trace file |
| `--trace_gpu_prefix=PATH` | Prefix for GPU trace output (default: `scratch/gpu/`) |
| `--dump_shaders=PATH` | Dump compiled GPU shaders |
| `--texture_dump` | Dump textures to DDS format |
| `--break_on_instruction=ADDR` | Break at guest PPC address |
| `--break_condition_gpr=N` | Conditional breakpoint on GPR value |
| `--log_high_frequency_kernel_calls` | Verbose kernel call logging |
| `--gpu_allow_invalid_fetch_constants` | Allow malformed fetch constants |

## Next Steps

### Priority 1: Fix Remaining Rendering Issues

Deferred draw cache invalidation fix produces real content but frames are dark. Next:
1. **Gamma ramp investigation** — verify gamma correction is applied in readback path
2. **Vulkan validation** — run with `--vulkan_validation` to catch any remaining API errors
3. **Dense capture test** — capture every 10 frames to see full boot animation progression
4. **E10 flat color diagnosis** — investigate why force_all_draws produces uniform color

### Priority 2: Boot Screen Advancement

With partially-correct rendering, iterate on scripted input sequences to navigate past the DC logo. The boot animation ends around frame 275. Need to capture frames 400-800 with input to see menu screens.

### Priority 3: PE Override

Generate COMDAT order file from original map to enable matching linker layout for PE override.
