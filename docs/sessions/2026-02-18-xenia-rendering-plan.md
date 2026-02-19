# Xenia Headless Rendering — Research & Plan

**Date:** 2026-02-18
**Status:** Research complete, plan ready for review

## Research Summary

### The Fundamental Problem

DC3 runs at ~30fps in xenia-headless with draws skipped. The game progresses normally because EVENT_WRITE_SHD sync values are written regardless of whether draws execute. But we need actual rendered frames.

The catch-22: enabling draws blocks the CP thread (shader compilation + pipeline creation), which prevents processing sync packets, causing the game to deadlock after ~13 frames.

### Key Discovery: GPU Trace System

Xenia has a built-in GPU trace and replay system:

- **`--trace_gpu_stream`** — Captures ALL PM4 packets + guest memory snapshots + EDRAM to `.xtr` files
- **`xenia-gpu-vulkan-trace-dump`** — Replays traces with full Vulkan rendering and outputs PNG screenshots
- **Trace replay has NO timing constraints** — no game thread, no sync polling, no deadlock risk
- **Critical insight**: The trace writer captures PM4 packets at the `CommandProcessor` level, BEFORE `IssueDraw()` is called. This means draw PM4 packets are captured in the trace **even when draws are skipped** during the original run.

### Key Discovery: Pipeline Cache

- Vulkan backend has **zero** persistent caching — no `VkPipelineCache`, no disk serialization
- Every run compiles all shaders and pipelines from scratch
- DC3 uses exactly 9 unique pipeline states during early boot (5 unique VS, 5 unique PS)
- Once cached, pipeline lookup is ~0.1ms (vs 10-100ms for first compilation)

### Key Discovery: Draw Path Cost Breakdown

| Operation | First-Time Cost | Cached Cost |
|-----------|-----------------|-------------|
| Shader Analysis | 0.5ms | 0.001ms |
| Shader Translation (SPIRV) | 5-50ms | 0.1ms |
| Pipeline Creation | **10-100ms** | 0.1ms |
| BeginSubmission (headless) | ~0ms | ~0ms |
| Render Target Update | 1-5ms | 0.1ms |
| Texture Binding | 0.1ms | 0.001ms |
| Dynamic State | 0.01ms | 0ms |
| System Constants | 0.5ms | 0.001ms |
| **Total per draw** | **~50-200ms** | **~0.5ms** |

**Implication:** With ~2000 draws/frame:
- First frame (all cold): 2000 × 50ms = too slow (deadlock)
- Warm frame (all cached): 2000 × 0.5ms = ~1 second (slow but no deadlock)

## Proposed Approaches (Ranked by Likelihood of Success)

### Approach A: Trace Capture + Offline Replay (Most Promising)

**Concept:** Run the game with draws SKIPPED (fast, 30fps). The trace writer captures all PM4 packets including draw commands. Replay the trace offline with full Vulkan rendering — no timing constraints.

**Why this works:**
1. Game runs at 30fps with draws skipped (proven to work)
2. Trace captures the full PM4 command stream (draws, copies, syncs, memory)
3. Trace replay processes packets through CommandProcessor without a game thread
4. No sync polling = no deadlock = unlimited time for shader/pipeline compilation
5. Screenshot output already implemented in `xenia-gpu-vulkan-trace-dump`

**Steps:**
1. Run xenia-headless with `--trace_gpu_stream --gpu=vulkan` and draws skipped
2. Let game progress to interesting state (title screen, menus) — ~20-60 seconds
3. Kill process (trace file written incrementally)
4. Replay trace: `xenia-gpu-vulkan-trace-dump <trace.xtr> <output_dir>`
5. Get PNG screenshots at each swap point

**Risks:**
- **Trace file size**: At 30fps with memory snapshots, could be GB+ for 60s run. May need to limit capture duration.
- **Memory consistency**: Trace includes memory snapshots, but if draws were skipped, GPU-side state (EDRAM, render targets) may be stale. The replay should rebuild this state from the PM4 stream.
- **Trace replay tool may not work**: Haven't tested it. May need to build it first (`xenia-gpu-vulkan-trace-dump`). May have bugs.
- **Draws in trace may reference stale memory**: If the game writes to vertex/texture memory based on CPU logic (which worked normally since draws were skipped), the data should be valid. But if the game expected GPU feedback (resolve results in EDRAM), those would be empty.

**Mitigation for risks:**
- Test with short traces first (5-10 seconds)
- Check trace file size growth rate
- Build and test the trace dump tool before committing to this approach
- If memory consistency is an issue, try enabling copy draws (EDRAM resolves) during trace capture

### Approach B: Long Run with All Inline Draws + Async Pipelines

**Concept:** Run approach 4 (all draws inline, async pipeline compilation) for a very long time (10-20 minutes). Game runs at ~0.5fps but doesn't deadlock. Eventually reaches interesting content.

**Why this might work:**
- Approach 4 was proven to not deadlock (14 swaps/30s)
- After pipeline cache warms up, draw cost drops to ~0.5ms each
- At ~2s/frame, game reaches frame 600 in ~20 minutes
- Frame 600 is roughly equivalent to what null GPU reaches in 20 seconds

**Steps:**
1. Use existing approach 4 code (non-blocking BeginSubmission + async pipelines + all draws)
2. Set `--headless_timeout_ms=1200000` (20 minutes)
3. Set `--headless_capture_interval=100` (capture every 100 frames)
4. Wait for game to reach interesting content

**Risks:**
- 20 minutes is very long — may hit other issues (memory pressure, submission accumulation)
- Game content at frame 600 with 2s/frame timing may differ from game content at frame 600 with 33ms/frame timing (timing-dependent game logic)
- Each frame still takes ~1-2 seconds even with cached pipelines

### Approach C: Shader Pre-Translation During Skip Mode

**Concept:** During the draw-skip phase, still run shader analysis and SPIRV translation (but skip actual Vulkan submission). This warms the shader cache. Then enable draws — pipelines compile fast because shaders are already translated.

**Why this might work:**
- The draw path analysis shows `EnsureShadersTranslated` (5-50ms) runs on the CP thread
- Shader translation is cached per (shader, modification) pair
- If we pre-translate during skip mode, warmup frames only need pipeline creation (~10ms per new pipeline)
- With 9 pipelines × 10ms = ~90ms total — might fit within the game's sync timeout

**Steps:**
1. Modify draw skip path to call shader analysis + translation but skip BeginSubmission onwards
2. This doesn't need Vulkan submission, so no CP blocking
3. After N frames of pre-translation, enable draws with sync pipeline creation
4. First rendered frame creates 9 pipelines (~90ms) but shaders are already cached
5. Subsequent frames fully cached (~0.5ms/draw)

**Risks:**
- Need to modify `IssueDraw` to split the path
- 90ms for pipeline creation might still cause sync timeout
- Shader modifications depend on register state which changes per frame

### Approach D: VkPipelineCache Persistence

**Concept:** Add `VkPipelineCache` to xenia's Vulkan backend. First run compiles pipelines and saves cache to disk. Second run loads cached pipelines (near-instant).

**Why this might work:**
- Vulkan driver caches compiled pipeline binaries
- `vkGetPipelineCacheData` / `vkCreatePipelineCache` with initial data
- Second run: all 9 pipelines load from disk in <1ms each
- DC3 always uses the same 9 pipelines → cache is permanent

**Steps:**
1. Add `VkPipelineCache` creation in `VulkanPipelineCache::Initialize()`
2. Load cache from `cache_root/pipelines/{TITLE_ID}.vkpc` if exists
3. Pass `VkPipelineCache` to `vkCreateGraphicsPipelines()`
4. Save cache to disk on shutdown via `vkGetPipelineCacheData()`
5. First run: slow (same as now), but cache saved
6. Second run: all pipelines load from cache → fast

**Risks:**
- VkPipelineCache is driver-specific (not portable between GPU vendors/driver versions)
- Still slow on first run
- Requires code changes to pipeline creation path

## Recommended Approach Order

1. **Try Approach A first** (trace capture + replay) — most likely to work, no timing issues
2. **If A fails**, try Approach C (shader pre-translation) — addresses root cause
3. **If C fails**, try Approach D (pipeline cache persistence) — guaranteed second-run success
4. **Approach B as last resort** (long run) — simplest, just takes 20 minutes

## Trace System Test Results

### Trace Capture: WORKS
- 10-second run with `--trace_gpu_stream` produced 18MB `.xtr` file
- ~1.8 MB/s trace growth rate — very manageable (60s = ~108MB)
- Trace captures PM4 packets + memory snapshots with Snappy compression
- Draws are skipped during capture (game runs at full speed)

### Trace Dump Tool: BUILT SUCCESSFULLY
- `xenia-gpu-vulkan-trace-dump` built after moving DEFINE flags to `gpu_flags.cc`
- Binary: 204MB (debug build, same libs as headless)

### Trace Replay: PARTIALLY WORKS — Two Issues Found

**Issue 1: Draw Skip Active During Replay**
The command processor's headless draw skip fires during trace replay because:
```cpp
if (!graphics_system_->presenter() && !headless_render_frame_) {
    return true;  // Skips all non-copy draws
}
```
In trace replay, `presenter()` is null (no window) and `headless_render_frame_` is false. So all draws are skipped, just like during capture. The trace data contains the PM4 packets for draws, but they get skipped during replay.

**Fix:** Add a `force_all_draws_` flag to VulkanCommandProcessor. Set it to true during trace replay. Check it in IssueDraw to bypass the headless skip.

**Issue 2: No Presenter for Screenshot Capture**
The trace dump tool captures screenshots via `presenter->CaptureGuestOutput()`, but there's no presenter in headless/trace mode.

**Fix:** Use our existing EDRAM readback code (staging buffer + one-shot command buffer) instead of the presenter-based capture. Or create a minimal headless VulkanPresenter without a window surface.

**Issue 3: Shutdown Assert (Non-Critical)**
`~XObject` assert `handles_.empty()` on cleanup. Doesn't affect trace replay or screenshot output.

### Trace Replay Log (Successful Parts)
```
Mapped 17997377b trace from 373307D9_stream.xtr
   Version: 1
    Commit: c7f61342d7061b8264e4b988b8d2e03351b6e088
  Title ID: 926091225
XE_SWAP                          ← Reached swap point (PM4 processing works)
~XObject assert failure          ← Cleanup issue
```

## Revised Plan

### Phase 1: Fix Trace Replay (2 changes needed)

1. **Bypass draw skip in trace mode**: Add `force_all_draws_` member to `VulkanCommandProcessor`, initialized false. Set true when trace replay is detected (e.g., via a `--force_all_draws` flag or automatic detection in `SetupContext`).

2. **Frame capture without presenter**: Either:
   - a. Reuse our existing EDRAM readback code (staging buffer approach already in IssueSwap)
   - b. Override `TraceDump::Run()` in `VulkanTraceDump` to use our readback code instead of `CaptureGuestOutput()`
   - c. Create a headless VulkanPresenter that can capture to buffer without a window

   Option (b) is simplest — we already have the `ReadbackAndDumpFrame` code.

### Phase 2: Capture Trace at Interesting Game State

1. Run xenia-headless with `--trace_gpu_stream` for 60 seconds (game reaches ~frame 1800)
2. This should be past early boot, into loading screens or title screen

### Phase 3: Replay and Screenshot

1. Run `xenia-gpu-vulkan-trace-dump` with `--force_all_draws` on the captured trace
2. No timing constraints — pipelines compile as slowly as needed
3. Get PNG screenshots at each frame boundary

### Alternative: Direct Long-Run Approach

If trace replay proves too complex, simply run approach 4 (all inline draws, async pipelines) for 20+ minutes:
- ~14 swaps/30s confirmed stable (no deadlock)
- 20 min = ~560 frames with full rendering
- Set `--headless_capture_interval=50` to capture every 50th frame
- Total runtime: ~20 minutes
