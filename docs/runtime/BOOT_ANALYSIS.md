# Dance Central 3 Boot Analysis

## Current Boot Status (2026-02-20)

**Status:** Game boots, renders, and runs at 30fps in Xenia headless with Vulkan GPU backend. Boot animation (DC logo) captured successfully. Currently blocked on rendering quality regression in deferred draw mode.

### Boot Progress

```
┌─────────────────────────────────────────────────────┐
│ Boot Progress: ~70-80%                              │
├─────────────────────────────────────────────────────┤
│ ✅ XEX loading                        (0-5%)       │
│ ✅ Import resolution (707 imports)    (5-10%)      │
│ ✅ Kernel initialization              (10-15%)     │
│ ✅ Thread creation (16 threads)       (15-20%)     │
│ ✅ Main loop entry                    (20-25%)     │
│ ✅ Rendering system init              (25-40%)     │
│ ✅ Asset loading (ARK archives)       (40-60%)     │
│ ✅ Boot animation renders             (60-70%)     │
│ ⏸️  Boot screen → menu transition     (70-80%)     │
│ ❓ Menu interaction                   (80-100%)    │
└─────────────────────────────────────────────────────┘
```

### What We Can See

| Subsystem | Status | Evidence |
|-----------|--------|----------|
| GPU Rendering | ✅ Working | 36K+ draw calls, boot animation captured as PPM |
| Audio | ✅ Stubbed | XAudio2 dummy driver, game continues without real audio |
| User Input | ✅ Scripted | `--scripted_input='5s:A,7s:START'` via NopInputDriver |
| Screen Output | ✅ Captured | Multi-frame PPM capture every N swaps |
| Frame Timing | ✅ 30fps | Game's internal timestep, 33ms per frame |
| File I/O | ✅ Working | ARK archives loaded, no I/O errors |

## Boot Sequence (Observed)

```
Timeline of Execution:
┌─────────────────────────────────────────────────┐
│ 1. XEX Load (0-100ms)                          │
│    - 293 memory pages mapped                    │
│    - CODE: 187 pages (~12MB)                   │
│    - RWDATA: 76 pages (~5MB)                   │
│    - RODATA: 30 pages (~2MB)                   │
├─────────────────────────────────────────────────┤
│ 2. Import Resolution (100-200ms)               │
│    - d3d9: 318 imports                         │
│    - xboxkrnl: 379 imports                     │
│    - xbdm: 10 imports                          │
│    - 347 thunks + 360 variables patched        │
├─────────────────────────────────────────────────┤
│ 3. Thread Creation (200-500ms)                 │
│    - GPU Commands + VSync threads              │
│    - 16 game threads (main, D3D workers,       │
│      loaders, audio, etc.)                     │
├─────────────────────────────────────────────────┤
│ 4. Rendering Pipeline Active (500ms+)          │
│    - 228 draws per frame (with resolve)        │
│    - Vulkan async pipeline compilation          │
│    - Pipeline warmup: 2-3 stalls, <10 seconds  │
├─────────────────────────────────────────────────┤
│ 5. Boot Animation (frames 1-300)               │
│    - DC logo neon formation animation          │
│    - Card logo with reflection                 │
│    - Animation transition at ~frame 275        │
├─────────────────────────────────────────────────┤
│ 6. Post-Boot (frame 300+)                      │
│    - Screen transitions (dark/red)             │
│    - Likely waiting for input to advance       │
│    - Game continues running (1700+ swaps)      │
└─────────────────────────────────────────────────┘
```

## Thread Architecture

| Thread | Handle | Role | Status |
|--------|--------|------|--------|
| GPU Commands | F8000004 | PM4 ring buffer processing | Active |
| GPU VSync | F8000008 | VSync interrupts (~60Hz) | Active |
| Main XThread | F8000028 | Game main thread | Running |
| Game Threads 7-18 | F8000088+ | D3D workers, loaders, audio | Running |

## How to Run

```bash
# Build xenia-headless
cd ~/code/milohax/xenia
cd build && make xenia-headless config=checked_linux -j$(nproc)

# Run with Vulkan + multi-frame capture
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig/373307D9/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=100 \
    --headless_timeout_ms=30000 --force_all_draws=true

# Run with scripted input
./bin/Linux/Checked/xenia-headless --gpu=vulkan \
    --target=~/code/milohax/dc3-decomp/orig/373307D9/default.xex \
    --dump_frames_path=/tmp/frames/ --headless_capture_interval=200 \
    --scripted_input='5s:A,7s:START,10s:A' \
    --headless_timeout_ms=25000 --force_all_draws=true

# Note: game data (gen/main_xbox.hdr, .ark files) must be accessible
# orig/373307D9/gen is symlinked to orig-assets/gen
```

## Key Differences: Original vs Decompiled XEX

Both binaries behave identically in headless mode:

| Behavior | Original XEX | Decompiled XEX |
|----------|--------------|----------------|
| Boot success | ✅ | ✅ |
| Threads started | 16 | 16 |
| Imports resolved | 707 | 707 |
| Draws per frame | 228 | 228 |
| FPS | ~30 | ~30 |
| Crashes | None | None |

The decompiled binary is structurally correct through the entire boot sequence.

## Current Blocker

See [XENIA_HEADLESS_STATUS.md](XENIA_HEADLESS_STATUS.md) for details on the rendering investigation. Deferred draws produce wrong EDRAM content (B=0x3F instead of correct colors). Inline draws produce correct output but deadlock at frame 12.
