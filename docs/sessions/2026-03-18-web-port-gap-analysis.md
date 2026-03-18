# Web Port Gap Analysis (2026-03-18)

Full audit of divergences between the desktop (Linux) and web (Emscripten/WASM) builds of the DC3 native port. Goal: identify what's actually blocking the core game from working end-to-end on web.

## Fixes Applied This Session

### 1. `Surface::Present()` — NOT THE ISSUE (REVERTED)

**File**: `native/src/platform/GpuDevice_Web.cpp`

Initial hypothesis: desktop calls `mSurface.Present()` but web doesn't, causing frozen canvas. **Wrong.** emdawnwebgpu does not support explicit `wgpuSurfacePresent` — the browser auto-presents the surface texture at the end of the `requestAnimationFrame` callback. Adding `Present()` causes a fatal abort: `wgpuSurfacePresent is unsupported`.

The original `ProcessEvents()`-only approach is correct. The "UI doesn't update" issue on web has a different root cause — still under investigation.

### 2. `FillDummySkeleton` was a no-op on web (FIXED)

**File**: `native/src/platform/Skeleton_Native.cpp`

`FillDummySkeleton()` was inside the `#else` (non-Emscripten) block. On web, the stub did nothing, so the skeleton had zero-confidence joints, the quality filter set `mValid = false`, and all skeleton-gated systems (HamNavList scroll, gesture filters) disengaged.

**Fix**: Moved `FillDummySkeleton()` to a shared section after `#endif`. It uses only `Skeleton` struct members — no platform APIs. Now the dummy skeleton passes the quality filter on all platforms, and the `#ifdef HX_NATIVE` safety net hack in `HamNavList::Poll()` was removed.

### 3. `HamNavList` scroll safety net removed (FIXED)

**File**: `src/system/hamobj/HamNavList.cpp` (lines 415-423)

With the dummy skeleton properly wired, the `#ifdef HX_NATIVE` block that manually drove `mScrollBehavior.Update()` when no skeleton was available is no longer needed. Scroll now flows through the normal `UpdateGestures()` path on all platforms.

## Gaps Investigated

### A. UIPanel/UIScreen Exit Animations — NOT A BUG

**Status**: Intentional workaround, no fix needed.

Both `UIPanel::Exiting()` and `UIScreen::Exiting()` return `false` unconditionally on `__EMSCRIPTEN__`:

```cpp
// UIPanel.cpp:264
bool UIPanel::Exiting() const {
#ifdef __EMSCRIPTEN__
    return false;
#else
    // ... checks mDir->Exiting(), DTA handlers, UITrigger blocking
#endif
}
```

**Why it exists**: Exit animations are driven by DTA script tasks, UITrigger timers, and PropAnim systems. These subsystems aren't fully functional on web (DTA flow doesn't execute, timers may not advance). Without the guard, transitions would hang forever waiting for animations that never complete.

**How desktop handles it**: The native desktop port uses the real `Exiting()` logic but adds a 90-frame (~3 second) timeout in `UI.cpp:665-679`:

```cpp
if (++sExitWaitFrames > 90) {
    printf("DC3 UI WARNING: Exit animation timeout for '%s' — force-completing\n", ...);
    screenExited = true;
}
```

**Why we don't need to change this**: The web guard makes transitions instant (no animation wait). This is correct behavior — the animations wouldn't play anyway. The visual cost is that exit animations are skipped (instant screen switches), which is acceptable for now.

**Future improvement**: If/when DTA flow and UITrigger timers work on web, replace `return false` with the desktop's timeout-based approach.

### B. Video Playback — COSMETIC ONLY

**Status**: Not blocking core gameplay. All video usage is cosmetic.

The web build has `WebMovieImpl` (browser `<video>` element + canvas readback) but requires pre-transcoded `.webm` files. When videos are missing:

| Context | What happens | Blocking? |
|---------|-------------|-----------|
| Attract screen intro | Auto-skipped after 1 frame (hardcoded in `UI.cpp` screen flow) | No |
| Campaign story FMVs | Silently skipped, panel completes | No |
| Song preview clips | Falls back to silence | No |
| `movie_overlay_panel` | Renders transparent black (alpha=0), doesn't block UI | No |
| Credits roll | Skipped | No |

Key protections:
- `TexMovie.cpp` uses transparent clear color (`alpha=0`) on web so missing videos don't block UI
- `BinkMovieImpl` stubs return `false` from `BeginFromFile()` — engine knows playback failed
- Exit animation timeout (90 frames) force-completes any stuck movie panel

The 30fps hardcode in `WebMovieImpl` is cosmetic — it only affects frame number calculation for seeking, not actual playback rate (browser controls that).

**To actually get videos working**: Run `scripts/web/transcode_bik.sh` to convert `.bik` files to `.webm`, then serve them via the dev server.

### C. `BinStream::WaitUntilReady()` — LATENT RISK

**Status**: Currently works by accident. Fix recommended for robustness.

On web, `WaitUntilReady()` returns `false` immediately to prevent deadlock (can't spin-wait on single-threaded browser event loop). **All 11 callers ignore the return value.**

```cpp
// Every single caller does this:
bs.WaitUntilReady();    // return value discarded
bs >> data;             // proceeds regardless
```

**Why it works today**: `WebAssetsFetchSync()` (called from `AsyncFile::_OpenAsync()`) completes the entire HTTP fetch synchronously before returning. By the time `WaitUntilReady()` runs, all data is already in MEMFS, so `Eof()` returns `NotEof` on the first check and the function returns `true`.

**When it would break**: If any file fetch is slow or incomplete (network timeout, large file, server error), `WaitUntilReady()` returns `false`, callers ignore it, and subsequent reads get garbage data. This causes silent corruption of:
- `.milo` directory structure (DirLoader)
- Mesh vertex data (every 512 verts)
- Bone animation samples (every 128 samples)
- MoveGraph parent chains

**Call sites** (11 total):
- `ChunkStream.cpp:113` — chunk boundary during loading
- `DirLoader.cpp:144,350` — class symbol + footer sentinel
- `Mesh.cpp:1721,1787` — vertex streaming (HX_NATIVE only)
- `CharBonesSamples.cpp:215` — bone sample streaming
- `MoveGraph.cpp:32,45` — move graph parent loading (100ms sleep)

**Recommended fix**: Ensure `WebAssetsFetchSync()` guarantees complete data before returning. Add MILO_WARN at call sites if return is false (defense in depth). Don't change the `return false` behavior — the deadlock prevention is correct.

### D. Web Headless Rendering — NON-ISSUE

**Status**: Not a gap. Working as designed.

The web build always renders to canvas (`desc.headless = false` hardcoded in `Rnd_Wgpu.cpp:268`). The headless stubs (`ReadbackHeadlessFrame() → false`, `AcquireHeadlessFrame() → AcquireNextFrame()`) are never called on web.

- Screenshots: Use Playwright `page.screenshot()` (captures canvas content)
- Video capture: Not needed on web
- Render-to-texture (`TexRenderer`): Independent of headless mode — uses `BeginTexturePass()` with separate render targets

No changes needed.

## What's Actually Blocking the Web Port

After this session's fixes, the remaining blockers for core gameplay on web are:

### Must-fix for playable web port

| Issue | Severity | Description | Fix approach |
|-------|----------|-------------|--------------|
| Dummy skeleton on web | **FIXED** | Skeleton invalid, scroll/gesture broken | Shared `FillDummySkeleton()` |
| Scroll safety net | **FIXED** | Removed unnecessary `#ifdef HX_NATIVE` hack | Skeleton fix makes it redundant |
| Web UI not updating visually | **OPEN** | Canvas renders but doesn't reflect data changes (scroll, selection) | Under investigation — NOT a `Present()` issue (browser auto-presents via rAF) |

### Should-fix for robustness

| Issue | Severity | Description | Fix approach |
|-------|----------|-------------|--------------|
| `WaitUntilReady` silent failure | Medium | 11 callers ignore return, risk of silent data corruption on slow network | Validate `WebAssetsFetchSync` guarantees completeness; add warnings at call sites |
| Exit animation skip | Low | Screen transitions are instant (no exit animations) | Port desktop's 90-frame timeout when DTA flow works |

### Not blocking (cosmetic / future)

| Issue | Priority | Description |
|-------|----------|-------------|
| Video playback | Low | Needs `.bik` → `.webm` transcoding pipeline |
| Background tab audio | Low | Audio may degrade when tab loses focus |
| Web headless screenshots | N/A | Use Playwright instead |

## Architecture Notes for Future Agents

### Platform guards

- `#ifdef HX_NATIVE` — defined for BOTH desktop and web native ports. Use for all non-Xbox code.
- `#ifdef __EMSCRIPTEN__` — web-only. Use for browser-specific APIs, stubs, single-threading workarounds.
- `#ifndef __EMSCRIPTEN__` — desktop-only. Use for Unix sockets, threads, GLFW, fork/exec.

### Testing strategy

1. **Fast iteration**: Desktop headless (`MILO_HEADLESS=1 MILO_INPUT_SCRIPT=... native/build/dc3-native`) — instant feedback, same game logic as web
2. **Visual verification**: Playwright screenshot comparison (`native/web/tests/test-song-scroll.js`) — confirms canvas rendering
3. **Hang diagnosis**: CDP debugger break (`native/web/tests/cdp-debugger-break.js`) — WASM call stack at hang point
4. **Full docs**: `docs/debugging/web.md` — comprehensive reference for all env vars, test scripts, and case studies

### Key files

| File | Purpose |
|------|---------|
| `native/src/platform/GpuDevice_Web.cpp` | WebGPU device init, surface, present |
| `native/src/platform/Skeleton_Native.cpp` | Skeleton tracking + dummy fallback |
| `native/src/platform/GestureMgr_Native.cpp` | Gesture/skeleton pipeline dispatch |
| `native/src/main_web.cpp` | Emscripten entry point, boot state machine |
| `native/src/platform/Rnd_Wgpu.cpp` | Shared rendering pipeline (desktop + web) |
| `src/system/ui/UI.cpp` | Screen transition system, auto-advance, exit timeout |
| `src/system/ui/UIPanel.cpp` | Panel lifecycle, exit animation check |
| `src/system/hamobj/HamNavList.cpp` | Song list scroll, skeleton-gated gestures |
