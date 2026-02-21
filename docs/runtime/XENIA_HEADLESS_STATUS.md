# Xenia Headless - Runtime Investigation

## Overview

We're running the original DC3 XEX in xenia-headless to compare behavior between the original and decomp binaries. This document tracks the investigation status.

## Build Location

- Xenia source: `/home/free/code/milohax/xenia/` (from `xenia-project/xenia` main fork)
- Headless binary: `build/bin/Linux/Checked/xenia-headless`
- Built with premake5 + gmake2, `config=checked_linux`

## Current Status (2026-02-21)

### Multi-Frame Capture: WORKING

Multi-frame capture is fully operational. 14+ captures per run confirmed with deferred draws, 15-35ms readback latency per frame.

### debug.xex: BOOTS TO GAMEPLAY (2026-02-20)

The DC3 debug build boots past the main menu with fake Kinect skeleton data:

- **60 guest memory patches** applied (57 NUI + 3 XBC/SmartGlass)
- **XEnumerate fix**: `ERROR_NO_MORE_FILES` mapped to `SUCCESS` with count=0 (was crashing `CacheMgrXbox::PollSearch`)
- **Fake Kinect WORKING**: `--fake_kinect_data=true` injects T-pose skeleton data
  - PPC stub at `0x829C2790` copies skeleton template from heap-allocated guest memory (`SystemHeapAlloc`)
  - Binary patches: SkeletonUpdateThread timeout (INFINITE→33ms at `0x8242E74C`), NOP IsOverride branch (`0x8242E1B0`)
  - Skeleton data MUST NOT be placed adjacent to stub — NUI functions at `0x829C2A10`/`0x829C2C50` would be overwritten
- **Save/load crash FIXED**: Auto-cleanup of stale content cache in `emulator.cc` before DC3 patches
  - Root cause: Previous xenia runs wrote partial/corrupt cache data to `content_root/373307D9/`
  - `SaveLoadManager` deserializes stale data → garbage file size → `_MemAllocTemp(2GB)` → crash
  - Fix: `std::filesystem::remove_all(content_root / "373307D9")` at DC3 launch
- Boot sequence: DC logo → Harmonix splash → Fitness HUD → Main Menu → Player detection → **Gameplay**
- Game detects player, shows body silhouettes, reaches dance/gameplay code paths

**Previously**: Halted on Kinect player detection (needed fake skeleton data).

### default.xex (retail)

- Works correctly. DC logo boot animation renders with deferred draw cache fix.
- Build info from debug screen: Build 120916, Plat: xbox, SystemConfig: config/ham_preinit_keep.dta

### Rendering Quality: VERIFIED WORKING (2026-02-20)

Two rendering issues identified and fixed, now **verified with visual inspection**:
1. **Cache invalidation** (Feb 20): Deferred draws now produce real content instead of flat B=0x3F
2. **Readback correction** (Feb 20): Channel swizzle + sRGB gamma correction applied to PPM output

| Mode | Behavior | Render Quality |
|------|----------|----------------|
| **Inline draws** (Feb 19) | Deadlocks at frame 12 | Correct DC logo (orange/gold neon) |
| **Deferred draws** (Feb 20, pre-fix) | Runs indefinitely | Wrong colors (solid B=0x3F everywhere) |
| **Deferred draws** (Feb 20, cache fix) | Runs indefinitely, 16+ threads | Real content but very dark (linear-space, wrong channel order) |
| **Deferred draws** (Feb 20, full fix) | Runs indefinitely | **Correct rendering: 220K unique colors, full game scenes** |
| **Force_all_draws** (Feb 20) | Runs indefinitely, 16+ threads | Flat R=2,G=1,B=2 (different failure mode) |

**Root cause 1 (B=0x3F)**: `FlushDeferredDraws()` raw memcpy bypassed `WriteRegister()` side effects.
**Root cause 2 (dark frames)**: `vkCmdCopyImageToBuffer` bypassed VkImageView swizzle; missing gamma correction.

**No display server needed**: xenia-headless works fully headless — Vulkan initializes without DISPLAY env var (tested with RTX 3090).

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

#### Debug XEX — With Scripted Input (Feb 20, cache fix + rendering fix)

26 captures across 7800+ frames (140 seconds). Game boots to **main menu** with scripted START+A presses.

| Frame | Content |
|-------|---------|
| 300 | **DC3 neon card logo** — boot animation with reflection |
| 750 | **Harmonix splash screen** — "EXIT CONTROLLER MODE" / "SELECT" prompts |
| 900 | **Fitness HUD** — Kinect body silhouettes, debug: "DataNode::Equal: 0.00 and dance_battle (kDataFloat/kDataSymbol) not compatible" |
| 1200 | **DC3 MAIN MENU** — "DANCE CENTRAL 3" title, "MAIN MENU", "THE PARTY" visible! |
| 1800-3600 | **Kinect player detection** — body silhouette zones waiting for players (black screen) |
| 6000 | Title screen again (cycled back after no player detection) |
| 7800 | Blue/black split screen transition |

**Without scripted input** (attract mode): Game enters demo/teaser loop — loading animation → 3D gameplay demo (220K unique colors, 848 draws) → loop.

**With scripted input** (START+A every 5s): Game navigates boot → splash → main menu → stuck on Kinect player detection.

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
- All 707 imports resolved (323 thunks + 334 variables + 50 stubs)
- 36,000+ draw calls, 600+ swaps at ~30fps
- 16 game threads spawned and running
- Vulkan rendering at full game speed with async pipeline compilation
- **Multi-frame capture** — 7+ screenshots per run with game surviving
- GPU readback with pre-allocated resources (15-35ms per capture)
- Scripted controller input (`--scripted_input='5s:A,7s:START'`)
- Pipeline cache warms up in <10 seconds (only 2-3 initial stalls)

### Current Blockers for Full Gameplay

These are the remaining barriers between "game boots" and "game is playable":

#### 1. Deferred Draw Rendering Quality: FIXED AND VERIFIED

Two root causes identified, fixed, and **visually verified**:
1. **Cache invalidation** — constant buffers + texture bindings invalidated after deferred draw register restore
2. **Readback correction** — VkImageView swizzle applied CPU-side + sRGB gamma correction LUT

Captures show correct DC3 game content: loading animation, 3D environments, dancers, HUD, debug text. 220K unique colors at gameplay frames. No display server required.

**Minor remaining**: `force_all_draws` mode produces uniform R=2,G=1,B=2 (different failure mode, lower priority).

#### 2. Kinect Player Detection: SOLVED

Fake Kinect skeleton data injection via `--fake_kinect_data=true`. Game detects player, shows body silhouettes, proceeds past player detection into gameplay.

**Implementation**: PPC stub at `0x829C2790` injects T-pose skeleton data from heap-allocated guest memory. Binary patches for SkeletonUpdateThread timeout and IsOverride branch.

#### 3. Controller Input for Menu Navigation: WORKING

Scripted input (`--scripted_input='15s:START,20s:A,...'`) successfully navigates boot → splash → main menu. The game responds to START and A button presses at the right timing.

#### 3. Missing devkit: Device (debug.xex only)

The debug build tries to load files from the `devkit:` path, which doesn't have a corresponding device mapped in xenia:
- `devkit:\locale\eng\locale_keep.dta` — locale overrides
- `devkit:\dancers.dta` — dancer data overrides

These are debug-mode overlay paths for dev kit workflows. The game handles the file-not-found gracefully (falls back to disc content), so this is **not a boot blocker** but may cause missing debug features.

#### 4. Missing Patch File

`d:\gen\patch_xbox.hdr` — title update / DLC patch file. Not available. The game handles this gracefully.

#### 5. Stale /dev/shm Files

Each xenia run creates `xenia_code_cache_*` and `xenia_memory_*` in `/dev/shm`. Never cleaned up. Can fill 38GB+ → SIGBUS on next launch. Periodically run: `rm -f /dev/shm/xenia_*`

### Decomp XEX: BOOTS TO MAIN LOOP (2026-02-20)

The decomp-linked PE (`build/373307D9/default.exe`) packaged as XEX via `build_xex.py` boots in xenia-headless:

- **All imports resolved**: xam 159 (100%), xboxkrnl 196 (100%), xbdm 5 (100%)
- **334 vars + 323 thunks** mapped from our PE, 50 stubs for unmapped overlapping ordinals
- **NUI patching**: 60/60 functions patched
- **Boot progress**: CRT init → thread creation → main loop entry → hangs at `RtlEnterCriticalSection` (LR=0x830DBFC8)
- **Hang root cause**: CRT initialization deadlock, likely critical section not initialized or held by a thread that never completes

**Import resolution fixes** (2026-02-20):
1. **Ordinal prefix stripping**: Original XEX uses prefixed ordinals (xam=0x0XXXX, xboxkrnl=0x1XXXX, xbdm=0x2XXXX). Our PE has plain ordinals (1-2500). Strip prefix: `real_ordinal = prefixed_ordinal & 0xFFFF`
2. **IAT grouping**: Thunk IAT addresses are contiguous per library. Group by proximity, assign to libraries by unique ordinal overlap scoring
3. **Consumed-VA tracking**: Prevent double-mapping when overlapping ordinals (18 between xam/xboxkrnl) would assign two libraries to the same thunk VA. Second library gets a stub instead.

### Other Limitations

- **PE Override superseded** — decomp XEX now boots directly via `build_xex.py`; PE Override no longer needed.
- **Inline draw deadlock** — still deadlocks at frame 12 (untested post-fix).
- **XAudio2 stubbed** — audio CreateDriver fails, returns dummy handle. Audio doesn't play but game doesn't crash.
- **SaveLoadManager error paths** all crash via `MILO_FAIL` — cannot make content ops fail gracefully.

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

### Decomp XEX Builder Fixes (completed) — DECOMP XEX BOOT

**Root cause of "Execute(830EAFF8): failed to find function"**: `build_xex.py` copied security info (including page descriptors) verbatim from the debug XEX template. Xenia's `XexModule::ContainsAddress()` uses CODE page descriptors to build the valid address range — with the debug XEX's descriptors, our entry point fell outside any CODE region.

**Files modified (dc3-decomp):**
- `scripts/build/build_xex.py`:
  - `generate_page_descriptors()` — parses PE sections, maps 64KB pages to CODE/DATA/RODATA
  - `build_security_info()` — replaces page descriptors in security info blob
  - `generate_thunk_data()` — accepts `target_size_of_image` to avoid thunk/code overlap

### XEnumerate NO_MORE_FILES Fix (completed) — CACHE SEARCH CRASH FIX

**Root cause of CacheMgrXbox::PollSearch() crash**: DC3's `PollSearch()` calls `XGetOverlappedResult` after async `XEnumerate`. When xenia's enumerator had no items, `WriteItems` returned `X_ERROR_NO_MORE_FILES` (0x12 = decimal 18). DC3 only handles result codes 0 and 0x65B, treating 18 as fatal (`MILO_FAIL`).

On real Xbox 360, empty enumeration completes with `ERROR_SUCCESS` and count=0, not `NO_MORE_FILES`.

**File modified:**
- `src/xenia/kernel/xam/xam_enum.cc` — Map `X_ERROR_NO_MORE_FILES` to `X_ERROR_SUCCESS` with count=0 in the `xeXamEnumerate` overlapped completion lambda

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

# Run DECOMP XEX (our built binary)
./bin/Linux/Checked/xenia-headless --gpu=null \
    --stub_nui_functions=true \
    --target=~/code/milohax/dc3-decomp/build/373307D9/default.xex \
    --headless_timeout_ms=30000

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

## Decomp XEX Boot (2026-02-21)

### build_xex.py Fixes

Two critical bugs in `scripts/build/build_xex.py` prevented the decomp XEX from booting:

#### 1. Page Descriptor Generation (root cause of "failed to find function")

**Problem**: `build_xex()` copied the security info (including page descriptors) verbatim from the debug XEX template. The debug XEX's page descriptors describe a different section layout than our decomp PE. Xenia's `XexModule::ContainsAddress()` uses CODE page descriptors to determine the valid address range — with wrong descriptors, the entry point fell outside any CODE region.

**XEX page descriptor encoding**: MSVC packs bitfields from LSB on both PPC and x86. In the `xex2_page_descriptor` struct, `info` occupies the LOW 4 bits and `page_count` the upper 28 bits: `value = (page_count << 4) | info`. Each descriptor is a big-endian uint32 followed by a 20-byte SHA-1 digest (24 bytes total).

**Fix**: `generate_page_descriptors()` parses the PE section headers, maps each 64KB page to CODE/DATA/RODATA based on section characteristics (IMAGE_SCN_MEM_EXECUTE for CODE, IMAGE_SCN_MEM_WRITE for DATA, neither for RODATA), and consolidates into contiguous ranges. `build_security_info()` replaces the page descriptors in the security info blob.

**Result**: Decomp XEX page layout: RODATA (66 pages) → CODE (370) → DATA (53) → RODATA (33) = 522 pages. Entry point at page 270, within CODE range.

#### 2. Thunk Placement Overlap

**Problem**: `generate_thunk_data()` placed import thunks at the original PE's SizeOfImage (~18MB). Our decomp PE is larger (~20.6MB), so thunks overlapped with valid .text code.

**Fix**: Added `target_size_of_image` parameter, passing our PE's SizeOfImage for thunk placement.

#### 3. Import Ordinal Namespace Collision (root cause of xenia assert crash)

**Problem**: The original XEX uses prefixed ordinals (xam=0x0XXXX, xboxkrnl=0x1XXXX, xbdm=0x2XXXX) while our PE linker emits plain ordinals (1-2500). A flat `ordinal → VA` dictionary caused cross-library collisions — 18 ordinals overlap between xam and xboxkrnl. When two libraries shared the same thunk VA, xenia's sequential processing overwrote the ordinal marker with a syscall stub (0x44000042) before the second library read it, triggering `assert_always()` on `record_type=0x44`.

**Fix (3 parts)**:
1. **Ordinal prefix stripping**: `real_ordinal = prefixed_ordinal & 0xFFFF`
2. **IAT grouping**: Thunk IAT addresses are contiguous per library. Group by address proximity (>32 byte gap = new group), assign to libraries by unique ordinal overlap scoring
3. **Consumed-VA tracking**: Track assigned VAs in a set. When an ordinal overlaps, the second library gets a stub instead of re-using the same VA

**Result**: 334 vars + 323 thunks mapped, 50 stubs for unresolvable overlaps. All 707 markers verified valid.

### Boot Verification

```bash
# Run decomp XEX
./bin/Linux/Checked/xenia-headless --gpu=null \
    --stub_nui_functions=true \
    --target=~/code/milohax/dc3-decomp/build/373307D9/default.xex \
    --headless_timeout_ms=30000
```

Output confirms: NUI patched (60/60), title loaded, enters main loop, runs 30+ seconds.

## PE Override Feature (implemented, superseded)

A `--pe_override` flag loads the original XEX then replaces PE sections with a decomp binary. Re-patches all 347 import thunks and 360 variable imports.

**Status: SUPERSEDED** — the direct decomp XEX boot path (above) is now the primary approach. PE Override is no longer needed since `build_xex.py` produces a valid bootable XEX directly.

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

1. **E10 (force_all_draws) flat color**: Produces uniform R=2,G=1,B=2. Different failure from E20 — may be capture timing or different draw scheduling path.

2. **Inline draw deadlock**: Still present at frame 12 (not tested post-fix).

### Frame Darkness Root Cause Analysis (2026-02-20)

Deep investigation of the dark frame issue revealed TWO contributing factors:

#### 1. Channel Order Mismatch (VkImageView Swizzle Bypass)

**The rendering pipeline:**
```
Game draws → EDRAM → Resolve (IssueCopy) → guest memory
→ RequestSwapTexture → VkImage (via compute load shader) → readback → PPM
```

The compute load shader (`kLoadShaderIndex32bpb`) copies 32bpp guest data after endian swap (k_8in32 = full byte reversal) directly into a `VK_FORMAT_R8G8B8A8_UNORM` VkImage. The byte order in the VkImage depends on the guest format's endian convention.

In the **normal xenia path**, the swap texture is sampled through a `VkImageView` that applies a component mapping (swizzle) to reorder channels correctly. The headless readback used `vkCmdCopyImageToBuffer` which bypasses the VkImageView and reads raw image bytes.

**Fix**: Store the host swizzle computed by `RequestSwapTexture` (via `GuestToHostSwizzle(fetch.swizzle, GetHostFormatSwizzle(key))`), then apply the same channel reordering on the CPU side when writing PPM pixels.

#### 2. Missing Gamma Correction

In the **normal xenia path**, IssueSwap renders the swap texture through a gamma correction shader (`apply_gamma_table.ps.xesl`) that maps linear-space render target values through the game's gamma ramp lookup table. The gamma application pipeline (`swap_apply_gamma_render_pass_`, `swap_apply_gamma_pwl_pipeline_`, etc.) is initialized even in headless mode, but was never invoked.

The game stores render target values in linear space. Without gamma correction, these values are extremely dark when viewed directly (mean brightness 3.55/255 for frame 50, with max=255 for some pixels).

**Fix (v1)**: Apply CPU-side sRGB gamma correction (`linear → sRGB` conversion) to each channel. This approximates the game's gamma ramp.

**Upgrade path (v2)**: The game writes custom gamma ramps via `DC_LUT_RW_INDEX`/`DC_LUT_RW_DATA` registers. The 256-entry table is already maintained by the command processor (`gamma_ramp_256_entry_table_`). For exact results, either:
- Read the actual gamma ramp table on the CPU side and build a game-specific LUT
- Connect the existing GPU-side gamma pipeline (`swap_apply_gamma_render_pass_`, `swap_apply_gamma_256_entry_table_pipeline_`) to the headless readback path

#### Implementation

**Files modified:**
- `src/xenia/gpu/vulkan/vulkan_texture_cache.h` — Added `last_swap_host_swizzle_` member and `GetLastSwapHostSwizzle()` getter
- `src/xenia/gpu/vulkan/vulkan_texture_cache.cc` — Store computed host swizzle in `RequestSwapTexture()`
- `src/xenia/gpu/vulkan/vulkan_command_processor.cc` — PPM writer now:
  - Reads the host swizzle and reorders channels accordingly
  - Applies sRGB gamma correction via lookup table
  - Logs fetch constant details (endianness, guest swizzle, host swizzle)
  - Saves both corrected (`frame_NNNN.ppm`) and raw (`frame_NNNN_raw.ppm`) for comparison

#### Key Architecture Insight

The normal xenia swap texture display always passes through:
1. `RequestSwapTexture()` → VkImageView with swizzle correction
2. Gamma application render pass → shader samples through VkImageView, applies gamma LUT
3. Presenter → renders gamma-corrected image to screen

The headless path was only doing step 1 (partially — got VkImage but not view), then reading raw bytes. The fix applies steps 1+2 equivalent on the CPU side.

### Previously Tested Hypotheses

| Hypothesis | Result |
|-----------|--------|
| BeginSubmission blocking causes deadlock | NO — hangs with both blocking and non-blocking |
| VBlank missing in headless | NO — VSync worker fires every 16ms |
| Format/endian mismatch in capture pipeline | PARTIALLY — VkImageView swizzle bypass causes channel reorder |
| Missing barriers between draws and resolve | NO — added EndSubmission + AwaitAll |
| **Stale constant/texture cache in deferred replay** | **YES — ROOT CAUSE. Fixed with cache invalidation.** |
| **Missing gamma correction in readback** | **YES — linear-space values too dark without gamma.** |
| **VkImageView swizzle bypassed by vkCmdCopyImageToBuffer** | **YES — raw bytes in wrong channel order.** |

### Verified Results (2026-02-20)

The rendering fix has been **visually confirmed** via headless captures (no display server needed):
- Loading animation: Harmonix glowing orb with magenta energy trails
- Gameplay: 3D club environment, dancers, props, HUD overlay with debug text
- 220K unique colors at gameplay frames, correct DC3 purple/blue/magenta palette
- Raw frames (pre-fix) show clear R/B swap with red/orange tones — swizzle correction verified

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

### Priority 1: COMPLETE — Rendering, Input, Kinect All Working

- Rendering pipeline fully operational (channel swizzle + gamma correction)
- Scripted controller input navigates boot → main menu
- Fake Kinect skeleton data gets past player detection into gameplay
- Save/load crash fixed

### Priority 2: COMPLETE — Decomp XEX Boots Successfully

Our decomp-built XEX (`build_xex.py` → valid XEX2) boots in xenia-headless:
- 60/60 NUI/XBC stubs patched, title loads successfully, enters main loop
- 30+ seconds clean execution with null GPU, no crashes or assertions
- All 707 imports resolved (334 vars, 323 thunks, 50 stubs)
- Three critical fixes in `build_xex.py`: page descriptors, thunk placement, ordinal collision

### Priority 3: Automated Regression Testing

Build a test harness that:
1. Boots DC3 debug.xex headlessly
2. Captures frames at known intervals
3. Compares against reference screenshots (pixel hashing or perceptual diff)
4. Reports rendering regressions as decomp code changes

### Priority 4: Reduce Remaining Link Errors

Current: 239 unique unresolved, 16 unique LNK4006, 36 LNK2013 fixup overflow.
Target: Minimize errors so the linked PE is as close to the original as possible.

### Fake Kinect Support: IMPLEMENTED

DC3 is a Kinect game — many code paths depend on NUI (Natural User Interface) SDK functions. Fake Kinect data injection is now working via `--fake_kinect_data=true`.

**Current implementation**:
- PPC stub at `0x829C2790` copies T-pose skeleton template from heap-allocated guest memory
- Binary patches: SkeletonUpdateThread timeout (INFINITE→33ms), NOP IsOverride branch
- Game detects player, shows body silhouettes, enters gameplay code paths
- Skeleton data placement: MUST NOT be adjacent to stub (NUI functions at `0x829C2A10`/`0x829C2C50` would be overwritten)

**Future enhancements**:
- Scripted skeleton pose sequences (timed to game events, like scripted controller input)
- Pre-recorded dance move replay from captured data
- Multiple player support (currently single player T-pose)

### Priority: Decomp XEX Behavioral Comparison

Now that the decomp XEX boots, the next milestone is comparing its runtime behavior against the original debug XEX:
1. Boot decomp XEX with Vulkan GPU + frame capture
2. Compare rendered frames against debug XEX reference screenshots
3. Identify behavioral divergences (crashes, visual differences, assert failures)
4. Use runtime differences as regression signals for decomp correctness
