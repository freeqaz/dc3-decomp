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

### debug.xex: BOOTS SUCCESSFULLY (2026-02-20)

The DC3 debug build now boots past all SDK initialization barriers with `--stub_nui_functions=true`:

- **60 guest memory patches** applied (57 NUI + 3 XBC/SmartGlass)
- Shows DC3 loading animation (magenta sine wave), transitions to main game loop
- 26 threads spawned, 10K+ VdSwap cycles, 100+ frame captures in 90 seconds
- Same deferred rendering quality issues as retail XEX (dark red/black color corruption)
- No MILO_ASSERT failures, no crashes during sustained 90-second runs

**Previously**: Halted on `NuiInitialize` failure (error `0x8301000b`), then on "Failed to initialize Xbox SmartGlass library."

### default.xex (retail)

- Works correctly. DC logo boot animation renders with deferred draw cache fix.
- Build info from debug screen: Build 120916, Plat: xbox, SystemConfig: config/ham_preinit_keep.dta

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

### Screenshots Captured

#### Retail XEX — Inline Draws (Feb 19)

DC3 boot animation successfully rendered (single capture per run due to frame 12 deadlock).

| Frame | Content |
|-------|---------|
| 50 | Neon lines extending — early boot animation |
| 100 | "DC" logo forming with neon line extensions |
| 150 | DC card logo centered with reflection |
| 200 | DC card logo framed, fully formed with reflection |
| 250 | DC card logo (same, slight glow variation) |
| 275 | DC card tilting/shrinking — animation ending |
| 300+ | Screen transition (half black / half dark red) |

#### Debug XEX — Deferred Draws with NUI Stubs (Feb 20)

100+ captures across 5200 swaps (90 seconds). Game boots through loading and enters main loop.

| Frame | Content |
|-------|---------|
| 50-100 | Black (startup) |
| 150-200 | Golden vertical line (UI transition effect) |
| 250-350 | DC3 logo/icon appearing in golden frame |
| 450-550 | **DC3 loading animation** (magenta sine wave — unmistakable) |
| 650-750 | Loading animation fading, red/black diagonal transition |
| 800-950 | Dark red fullscreen, scanline patterns |
| 1000+ | Alternating dark red / black / split screens with geometric patterns |
| 2500-5000 | Continues cycling (game is alive, rendering corrupted by deferred draw issue) |

### Performance Summary

| Configuration | Swaps | Duration | FPS | Notes |
|---|---|---|---|---|
| Retail: Null GPU (no draws) | 600+ | 20s | ~30 | Baseline — game logic only |
| Retail: Vulkan all draws, no readback | 611 | 20s | ~30.5 | Full speed, async pipelines |
| Retail: Vulkan 120s sustained run | 3,931 | 120s | ~33 | Locked at game's internal timestep |
| Retail: Multi-frame capture (every 100) | 700+ | 20s | ~33 | Game survives, 15-35ms per capture |
| **Debug: Vulkan + NUI stubs** (every 50) | **5,200+** | **90s** | **~58** | **26 threads, 100+ captures, no crash** |
| Debug: Null GPU + NUI stubs | 884K+ draws | 60s | N/A | Game logic running, no rendering |

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

### Current Blockers for Full Gameplay

These are the remaining barriers between "game boots" and "game is playable":

#### 1. Deferred Draw Rendering Quality (affects both retail and debug XEX)

The primary blocker. Deferred draws produce dark red/black output instead of correct colors. The game IS running (frames change, geometric patterns animate, 26 threads active) but the rendered output is too corrupted to see menus or UI.

**Symptoms**: Captures show solid dark red, black, red/black split screens, and occasionally scanline/geometric patterns. Early boot frames (loading animation) render recognizably (magenta sine wave, DC3 logo icon), but main game rendering is broken.

**Root cause**: Partially fixed (B=0x3F cache invalidation), but remaining issues include:
- Gamma ramp not applied in readback path
- Possible format conversion issue in staging buffer copy
- EDRAM resolve may not be producing correct output for all render target configs
- `force_all_draws` mode produces uniform R=2,G=1,B=2 (different failure)

**Impact**: Cannot see game menus, cannot determine what screen the game is on, cannot plan scripted input sequences.

#### 2. No Controller Input in Headless Mode

Headless mode uses nop HID (no input devices). The game likely reaches a "Press Start" or title screen and waits. Scripted input (`--scripted_input='5s:A,7s:START'`) exists but requires knowing what screen the game is on — blocked by rendering quality.

**Workaround path**: Fix rendering → identify screens → craft input sequence → automate boot to gameplay.

#### 3. Missing devkit: Device (debug.xex only)

The debug build tries to load files from the `devkit:` path, which doesn't have a corresponding device mapped in xenia:
- `devkit:\locale\eng\locale_keep.dta` — locale overrides
- `devkit:\dancers.dta` — dancer data overrides

These are debug-mode overlay paths for dev kit workflows. The game handles the file-not-found gracefully (falls back to disc content), so this is **not a boot blocker** but may cause missing debug features.

#### 4. Missing Patch File

`d:\gen\patch_xbox.hdr` — title update / DLC patch file. Not available. The game handles this gracefully.

### Other Limitations

- **PE Override blocked** — decomp linker produces different function addresses (+0x1600 .text shift + non-uniform per-function offsets).
- **Inline draw deadlock** — still deadlocks at frame 12 (untested post-fix).
- **XAudio2 stubbed** — audio CreateDriver fails, returns dummy handle. Audio doesn't play but game doesn't crash.

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

### NUI + SmartGlass Guest Memory Stubbing (completed) — DEBUG XEX BOOT FIX

**Root cause of debug.xex crash:** The DC3 debug build statically links the Xbox 360 Kinect SDK (NUI) and SmartGlass SDK (XBC). These are PPC code embedded in the XEX, NOT kernel imports. `NuiInitialize` and `XbcInitialize` (CXbcImpl::Initialize) fail because no hardware exists in xenia. The debug build's `MILO_ASSERT_FMT` halts on NUI failures, and SmartGlass prints "Failed to initialize Xbox SmartGlass library" and triggers program end.

**Approach:** Write PPC stub instructions (`li r3, 0; blr` = return S_OK) directly into guest memory at each function address before the JIT compiles them. This is the standard emulator approach for HLE of statically-linked SDK functions (like Dolphin's OS HLE patches).

**Key discovery:** XEX code pages are loaded as read-only. Writing stubs without first calling `heap->Protect(addr, 8, kMemoryProtectRead | kMemoryProtectWrite)` causes a SIGSEGV that deadlocks in xenia's signal handler (the handler tries to acquire a mutex, but the faulting thread may already hold it). Linux `mprotect` does not support `out_old_access` (asserts null on POSIX).

**Files modified:**
- `src/xenia/gpu/gpu_flags.cc/h` — `stub_nui_functions` cvar (bool, default false)
- `src/xenia/emulator.cc` — 60 guest memory patches in `CompleteLaunch()`:
  - 57 NUI functions: lifecycle, skeleton tracking, image streams, audio, camera properties, identity, fitness, wave gestures, head tracking, speech recognition
  - 3 XBC/SmartGlass functions: CXbcImpl::Initialize, DoWork, SendJSON
  - Functions returning S_OK: NuiInitialize, NuiSkeletonTrackingEnable, NuiImageStreamOpen, all camera/speech/identity stubs
  - Functions returning E_UNEXPECTED: NuiImageStreamGetNextFrame, NuiSkeletonGetNextFrame, NuiAudioCreate (game handles failure gracefully)
  - Extensive TODO comments for future proper NUI emulation

**Usage:** `--stub_nui_functions=true --target=/path/to/debug.xex`

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

### CLI Flag Notes

Previous attempts used wrong flag names. Correct flags:
- `--headless_timeout_ms=N` (NOT `--headless_timeout`)
- `--target=/path/to/xex` (NOT positional argument or `--capture_start_frame` which doesn't exist)
- `--headless_capture_interval=N` for multi-frame capture spacing
- `--dump_frames_path=/path/` for capture output directory

### SDL2-compat Issue (Arch Linux)

Arch replaced `sdl2` with `sdl2-compat` (SDL3 shim). When Xenia tries to show error dialogs, the call path goes through SDL2-compat -> SDL3 -> zenity, and crashes if zenity is not installed. Installing `zenity` is an optional fix to prevent crashes on dialog display. Not required for normal headless operation.

```bash
# Build
cd ~/code/milohax/xenia
cd build && make xenia-headless config=checked_linux -j$(nproc)

# Run retail XEX with null GPU (fast, no rendering)
./bin/Linux/Checked/xenia-headless --gpu=null \
    --target=~/code/milohax/dc3-decomp/orig-assets/default.xex \
    --headless_timeout_ms=20000

# Run retail XEX with Vulkan + multi-frame capture
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --headless_timeout_ms=90000 \
    --dump_frames_path=/tmp/frames/ \
    --headless_capture_interval=100 \
    --target=~/code/milohax/dc3-decomp/orig-assets/default.xex

# Run DEBUG XEX (requires NUI stubs)
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --stub_nui_functions=true \
    --dump_frames_path=/tmp/dc3-debug-frames/ \
    --headless_capture_interval=50 \
    --target=~/code/milohax/dc3-decomp/orig-assets/debug.xex

# Run with scripted input
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig-assets/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=200 \
    --scripted_input='5s:A,7s:START,10s:A' \
    --headless_timeout_ms=25000 --force_all_draws=true

# Note: game data (gen/main_xbox.hdr, .ark files) must be accessible
# orig/373307D9/gen is symlinked to orig-assets/gen
# IMPORTANT: Use absolute paths for --target, NOT symlinks (xenia path
# resolution strips the home directory from symlink targets)
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

### Priority 1: Fix Deferred Draw Rendering Quality

The primary blocker for both retail and debug XEX. Deferred draws produce dark red/black output instead of correct colors. The game IS running (26 threads, 10K+ swaps, changing frames) but rendered output is too corrupted to see UI/menus.

**Approach: Research first, then fix.** We partially fixed one symptom (B=0x3F stale cache) but don't fully understand xenia's deferred draw replay path, render target management, or EDRAM resolve pipeline. The next session should start with reading and understanding the relevant xenia code before attempting fixes.

Research phase:
1. **Read `FlushDeferredDraws()`** end-to-end — understand the full register save/restore, draw replay, and resolve flow
2. **Read `WriteRegister()` override** in VulkanCommandProcessor — identify ALL side effects that raw memcpy bypasses (not just constant buffers and texture bindings)
3. **Read the EDRAM resolve path** — understand how `IssueCopy()` converts EDRAM tile data to linear textures for presentation
4. **Read the readback/capture path** — understand how `RequestSwapTexture()` gets the final framebuffer for PPM output
5. **Compare with upstream xenia** — understand what our headless changes modify vs stock behavior

Investigation phase (after research):
1. **Vulkan validation** — run with `--vulkan_validation` to catch API errors in deferred replay
2. **Gamma ramp investigation** — verify gamma correction is applied in readback path
3. **Cache invalidation experiment** — try full cache clear before deferred draws
4. **E10 flat color diagnosis** — investigate why `force_all_draws` produces uniform R=2,G=1,B=2
5. **Inline vs deferred comparison** — if deadlock can be worked around, compare output quality

### Priority 2: Boot Screen Advancement

Once rendering is readable, use scripted input to navigate:
1. Past DC logo (boot animation ends ~frame 275)
2. Through "Press Start" / title screen
3. To main menu — this is where the game exercises most code paths

### Priority 3: PE Override

Generate COMDAT order file from original map to enable matching linker layout for PE override. This would allow running our decomp code against the original game's data.
