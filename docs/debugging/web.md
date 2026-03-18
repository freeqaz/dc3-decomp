# Web Build Debugging

Techniques for debugging the DC3 WASM/Emscripten web port — both interactively in-browser and headlessly via CLI agents.

## Prerequisites

```bash
npm install playwright
```

WebGPU requires a **secure context** (HTTPS or localhost). All tests must run against the dev server on `localhost`, not `data:` or `file:` URLs. No xvfb needed — `--headless=new` provides its own compositor.

## Architecture Overview

The web port compiles the same codebase as the desktop native port, with platform split at:

| Layer | Desktop | Web (Emscripten) |
|-------|---------|------------------|
| Window / input | GLFW (keyboard, gamepad) | Canvas keyboard events (`native/src/platform/Joypad_Web.cpp`) |
| GPU device | Dawn native (`native/src/gfx/GpuDevice.cpp`) | emdawnwebgpu (`native/src/platform/GpuDevice_Web.cpp`) |
| Rendering | Same `WgpuRnd` pipeline (`native/src/platform/Rnd_Wgpu.cpp`) | Same |
| Audio | miniaudio | Emscripten audio worklet |
| Video | FFmpeg | Stubs (no playback) |
| Skeleton | Unix socket to pose_server.py, falls back to dummy | Dummy skeleton only |
| File I/O | Disk reads | MEMFS (assets bundled at build) |
| Main loop | `while (!shouldClose)` in `App::Run()` | `emscripten_set_main_loop(mainLoop, 0, true)` (rAF-synced) |
| Threading | `std::thread` (reader thread for skeleton) | Single-threaded (stubs) |
| Build | `cmake --build native/build` | `scripts/build/web.sh` (emcmake) |

Both desktop and web define `HX_NATIVE`. Platform-specific code uses `#ifdef __EMSCRIPTEN__`.

## Quick Reference

```bash
# Build web port
scripts/build/web.sh

# Start dev server (must be running for all browser-based tools)
python3 native/web/server.py --port 8420

# Screenshot validation (quick sanity check — no xvfb needed)
node native/web/tests/test-screenshot.js --no-server --verbose

# Smoke test (auto-server, hang detection, WebGPU init check)
node native/web/tests/web-smoke.js --verbose

# Song scroll test (captures screenshots per scroll)
node native/web/tests/test-song-scroll.js --no-server --verbose

# CDP debugger break (pauses at hang point, dumps call stack)
node native/web/tests/cdp-debugger-break.js --no-server --verbose

# Rebuild WASM
scripts/build/web.sh
```

## Headless Desktop Testing (Preferred for Agents)

The desktop native build is the fastest way to test game logic without a browser. Most bugs that affect web also affect desktop headless, and the feedback loop is instant.

### Environment Variables

| Variable | Description |
|----------|-------------|
| `MILO_HEADLESS=1` | No GLFW window, GPU renders to offscreen target |
| `MILO_RENDER=1` | Force GPU init (headless still renders, just no window) |
| `MILO_NORENDER` | Skip GPU entirely (logic-only, fastest) |
| `MILO_INPUT_SCRIPT=path` | Scripted button presses (see below) |
| `MILO_MAX_FRAMES=N` | Exit after N frames |
| `MILO_SCREENSHOT_DIR=dir` | Auto-capture PNGs |
| `MILO_SCREENSHOT_FRAMES=f1,f2,...` | Which frames to capture (default: 100,600,900,1500) |
| `MILO_CLEAR_COLOR=R,G,B,A` | Override clear color (debugging) |
| `MILO_SIMPLE_RENDER` | Simplified material rendering |
| `MILO_DEBUG_PIPELINES` | Log pipeline creation |
| `MILO_NO_TRANSPARENT_DEFER` | Disable transparent draw queue |
| `MILO_PERF` | Enable frame timing |
| `MILO_VIDEO=path.mp4` | Record frames to video |
| `MILO_CAPTURE_FRAME=N` | GFXReconstruct capture at frame N |
| `MILO_FATAL_FAILS=0` | Don't abort on non-critical failures |

### Scripted Input System

Input scripts drive the engine through menu screens. Two formats:

**Absolute frame** (simple, but fragile if load times change):
```
60 start          # press Start at frame 60
200 confirm       # press A at frame 200
```

**Wait-for-screen** (robust, adapts to variable load times):
```
wait_screen main_screen
+30 confirm                # 30 frames after main_screen appears
wait_screen choose_mode_screen
+30 confirm
wait_screen song_select_screen
+50 down                   # scroll through songs
+100 down
```

Button names: `confirm`/`a`, `cancel`/`b`, `start`, `up`, `down`, `left`, `right`, `x`, `y`, `lb`/`l1`, `rb`/`r1`, `lt`/`l2`, `rt`/`r2`, `ls`/`l3`, `rs`/`r3`, `option`/`back`/`select`.

### Example: Test Song Scrolling

```bash
MILO_HEADLESS=1 MILO_MAX_FRAMES=2000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/song-scroll-test.txt \
  native/build/dc3-native 2>&1 | grep -E 'scroll|nav_highlight|skeleton|frame'
```

Expected output:
```
Native: headless mode, using dummy skeleton (no pose server)
DC3 Input: wait_screen 'song_select_screen' satisfied at frame 538
DC3 Input: Frame 588 — scripted buttons 0x4000
right_hand.hnl ... unhandled msg: nav_highlight
right_hand.hnl ... unhandled msg: nav_highlight_settled
```

Each `nav_highlight` + `nav_highlight_settled` pair confirms the list scrolled. Different song preview loads (`.bik` errors with song names) confirm the selection changed.

### Example: Headless Screenshot Comparison

```bash
MILO_HEADLESS=1 MILO_RENDER=1 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/song-scroll-test.txt \
  MILO_SCREENSHOT_DIR=/tmp/scroll-shots \
  MILO_SCREENSHOT_FRAMES=550,600,650,700 \
  MILO_MAX_FRAMES=800 \
  native/build/dc3-native
```

### Available Input Flow Scripts

| Script | Route | Purpose |
|--------|-------|---------|
| `scripts/dc3-input-flows/song-scroll-test.txt` | boot → main → choose_mode → song_select → 8x down | Verify list scrolling |
| `scripts/dc3-input-flows/ymca.txt` | boot → gameplay | Full song load test |

## Web-Specific Testing (Browser via Playwright)

Use these when you need to test browser-specific behavior (WebGPU canvas rendering, keyboard input mapping, asset loading over HTTP).

### Headless WebGPU Screenshots

All test scripts use a shared launch config (`native/web/tests/launch-helpers.js`) that configures Chrome for headless WebGPU rendering. Key points:

- **No xvfb needed.** `--headless=new` provides a real compositor with GPU support.
- **Must use localhost.** WebGPU requires a secure context — `data:` and `file:` URLs won't have `navigator.gpu`.
- **Readiness signal.** The engine sets `window.__webgpuReady = true` after 3 rendered frames. Use `waitForWebGPUReady(page)` before taking screenshots. Use `screenshotReady(page, path)` to wait for compositor presentation.
- **Engine init is slow (~30-60s).** Asset download + synchronous DTA parsing take time. Set timeouts accordingly.

Chrome flags (handled by `launch-helpers.js`):
```
--headless=new --use-angle=vulkan
--enable-features=Vulkan,VulkanFromANGLE,DefaultANGLEVulkan
--disable-vulkan-surface --enable-unsafe-webgpu --ignore-gpu-blocklist
```

### Test Scripts

| Script | Purpose | Key Flags |
|--------|---------|-----------|
| `test-screenshot.js` | Validate WebGPU screenshots capture real content | `--gpu-info`, `--frames N`, `--out dir` |
| `web-smoke.js` | Boot check, hang/crash detection | `--timeout`, `--hang-timeout`, `--wait-for`, `--save-logs` |
| `test-song-scroll.js` | Navigate to song_select, scroll, capture screenshots | `--scrolls N`, `--out dir` |
| `cdp-debugger-break.js` | Pause at hang point via CDP, dump WASM call stack | `--silence N` |
| `diagnose-song-load.js` | Full menu nav to gameplay, diagnose load issues | `--timeout`, `--hang-timeout` |
| `run-web-tests.sh` | Wrapper: server + smoke test | `--diagnose-hang` |

All scripts live in `native/web/tests/`. The dev server is at `native/web/server.py`.

### Running Tests

```bash
# Start server in background
python3 native/web/server.py --port 8420 &

# Run any test directly (no xvfb wrapper needed)
node native/web/tests/test-screenshot.js --no-server --verbose
node native/web/tests/test-song-scroll.js --no-server --verbose

# Or use the wrapper (auto-starts server)
native/web/tests/run-web-tests.sh --verbose
```

### Sandbox

GPU access requires `dangerouslyDisableSandbox: true` for bash commands. Chrome needs access to `/dev/dri/renderD*` for Vulkan.

### Keyboard Input Mapping (Browser)

| Key | Gamepad | Use |
|-----|---------|-----|
| `Space` | Start (kPad_Start) | Dismiss screens, skip intro |
| `Enter` | A/Confirm (kPad_X) | Select menu items |
| `ArrowDown` | D-pad Down | Navigate lists |
| `ArrowUp` | D-pad Up | Navigate lists |
| `Escape` | B/Back (kPad_Circle) | Go back |

Navigation sequence: `attract → [Space] → title → [Space] → main → [Enter] → choose_mode → [Enter] → song_select → [Down] → scroll`

## CDP Debugger Break

The most powerful tool for diagnosing WASM hangs. Uses Chrome DevTools Protocol to pause execution and dump the call stack.

### How It Works

Playwright launches Chrome, monitors console output. When output goes silent (configurable `--silence` threshold), it sends `Debugger.pause()` via CDP, capturing the exact call stack with demangled C++ function names.

### Reading the Output

**Successful pause (hang detected):**
```
=== CALL STACK ===
  #0: $ObjDirItr<RndLight>::Advance()
  #1: $WgpuRnd::WriteSceneUniforms()
  #2: $DrawMeshImmediate(RndMesh*)
  ...
  #20: $mainLoop()
```

**CDP can't pause (WASM trapped):**
```
WARNING: Debugger.pause did not trigger within 5s
```
WASM crashed, not hung. Look for `[PAGE_ERROR]` — shows error type (`function signature mismatch`, `memory access out of bounds`).

**Safety timeout (no hang):**
```
Safety timeout reached (90s)
=== CALL STACK ===
  #0: MainLoop_runner
```
Game is running normally.

## Skeleton Subsystem

Both desktop and web use a **dummy skeleton** when no pose server is available. `FillDummySkeleton()` fills 20 joints with a neutral standing pose (`kConfidenceTracked`), which passes the quality filter pipeline:

```
GestureMgr_NativePoll(mgr)
  ├─ FillDummySkeleton(slot 0)     ← works on ALL platforms (desktop + web)
  │   └─ 20 joints, kConfidenceTracked, mTracking=kSkeletonTracked, ID=1
  ├─ SetActiveSkeletonTrackingID(1)
  └─ PostUpdate() → SkeletonQualityFilter
       └─ 20/20 confident, not sitting, not sideways → mValid = true
```

This means `GetSkeletonByTrackingID(1)` returns a valid skeleton and game code (HamNavList, gesture filters, etc.) flows through the normal path without `#ifdef HX_NATIVE` workarounds.

The pose server (`native/scripts/pose_server.py`) is desktop-only (uses Unix sockets + fork). On Emscripten, `Start()` returns false and the dummy skeleton is always used.

## Diagnosing Common Issues

### `function signature mismatch`

WASM `call_indirect` type check failed — a virtual function call hit a vtable entry with wrong type. Causes:

1. **Missing decomp function** — virtual method declared but never implemented. Fix: implement it.
2. **Editor-mode code path** — `UsingCD()==false` activates paths calling unimplemented NG shader subsystems. Fix: guard with `#ifdef HX_NATIVE`.
3. **Init ordering** — Factory registration after objects load → vtables point to base class.

### `memory access out of bounds`

WASM load/store hit invalid memory. Causes:

1. **Null pointer dereference** — Missing null check.
2. **PropAnim SetFrame OOB** — `SetFrame()` evaluates PropKeys targeting missing Xbox objects. Use `AdvanceFrame()` instead.
3. **Stale object reference** — FileMerger cleared an object still referenced.

### Infinite loop (CDP pauses successfully)

CDP gives the exact stack. Patterns:

1. **`ObjDirItr::Advance()`** — Recursive dir iterator on corrupted hash table. Fix: avoid `ObjDirItr(dir, true)` in per-frame code.
2. **DTA `{while}` loop** — Script waiting for condition that never becomes true on native.
3. **Sync file load** — `FileMerger::StartLoadInternal` with `async=false` busy-loops. Fix: force `async=true`.

### UI renders but doesn't update visually

Data changes (selection, scroll) work but the canvas shows stale content. emdawnwebgpu auto-presents at end of rAF, so explicit `Surface::Present()` is NOT the issue (and will abort if called). Check instead:

1. **Camera selection** — `HamNavList::DrawShowing()` selects the PanelDir's scene camera. If the camera isn't found, draws may go to an invisible target.
2. **Draw state not rebuilt** — `UIListDir::BuildDrawState()` reads from `UIListState`. If the state isn't updated before draw, visuals are stale.
3. **Command encoder not submitting** — Verify `EndDrawing()` reaches the `mGpu.Queue().Submit()` + `mGpu.PresentFrame()` path. If `mInPass` is false, the command buffer is never submitted.
4. **Swapchain texture stale** — `GetCurrentTexture()` may return the same texture if the previous frame wasn't consumed by the compositor. Check that each frame gets a fresh surface texture.

### Selection changes but list doesn't scroll

The underlying selection data updates but the scroll animation doesn't play. Check the skeleton pipeline — if `IsValid()` returns false, `HamNavList::Poll()` calls `Disengage()` instead of `UpdateGestures()`, which means `mScrollBehavior.Update()` never runs. Verify:

```bash
# Should see "using dummy skeleton" at startup
MILO_HEADLESS=1 native/build/dc3-native 2>&1 | head -5
```

If you see `"Native: headless mode, using dummy skeleton (no pose server)"` at boot, the skeleton is wired. If not, check `GestureMgr_Native.cpp` — `MILO_HEADLESS` env var triggers the dummy skeleton path.

## Log Analysis

### Filtering Patterns

```bash
# Screen transitions
grep -E "will enter|Exit|satisfied" log.txt

# Scroll events
grep -E "nav_highlight|StartScroll|CompleteScroll|DC3 SCROLL" log.txt

# File loads
grep -E "AsyncFile opening|fetched on-demand|FAILED" log.txt

# Errors
grep -E "PAGE_ERROR|function signature|memory access|stub|ASSERT" log.txt

# Rendering state
grep -E "BeginDrawing|frame acquisition|mesh draw calls" log.txt

# Skeleton / gesture
grep -E "skeleton|gesture|dummy|FillDummy|InControllerMode" log.txt
```

## Case Studies

### Surface::Present() — False Lead (2026-03-18)

**Symptom**: Web UI renders once but never updates. Pressing down changes song selection (confirmed by `nav_highlight` logs) but the canvas stays frozen.

**Initial hypothesis**: Desktop `PresentFrame()` calls `mSurface.Present()` but web doesn't. Added the call.

**Result**: `Aborted(wgpuSurfacePresent is unsupported (use requestAnimationFrame via html5.h instead))`. emdawnwebgpu does NOT support explicit present — the browser auto-presents the surface texture at the end of the rAF callback. The original code was correct.

**Lesson**: Don't assume desktop and web WebGPU APIs are identical. emdawnwebgpu wraps the browser's WebGPU, which has different lifecycle semantics than native Dawn. The "UI not updating" issue on web has a different root cause — still under investigation.

### ObjDirItr Infinite Loop (2026-03-17)

**Symptom**: Game hung after ~1500 frames during song gameplay.

**CDP stack**: `ObjDirItr<RndLight>::Advance()` → `WgpuRnd::WriteSceneUniforms()` — recursive dir iterator on venue WorldDir every frame.

**Fix**: Removed `ObjDirItr<RndLight>(venueDir, true)` scans from `WriteSceneUniforms()`. Environment light lists + fallback defaults are sufficient.

### SelectConfig Shader Crash (2026-03-17)

**Symptom**: `function signature mismatch` WASM trap during character drawing.

**Stack**: `RndShader::MatShaderFlagsOK` → `RndShader::SelectConfig` → `RndTexBlender::DrawShowing` → `Character::DrawShowing`.

**Root cause**: `UsingCD()==false` on web (MEMFS has no CD). This activated editor-mode shader diagnostics calling unimplemented NG shader vtable entries.

**Fix**: Guard the `!UsingCD()` check in `SelectConfig` with `#ifdef HX_NATIVE` to match Xbox retail behavior.

### song.anim Freeze (2026-03-17)

**Symptom**: Dancers animate for one frame then freeze in T-pose.

**Root cause**: On Xbox, a DTA `{animate}` task advances `song.anim` each frame. On native/web, the DTA scripting flow doesn't execute.

**Fix**: `HamDirector::Poll()` manually calls `songAnim->AdvanceFrame(secs * 30.0f)`. Uses `AdvanceFrame()` (not `SetFrame()`) to avoid PropKeys evaluation crashes.

### Dummy Skeleton Not Filling on Web (2026-03-18)

**Symptom**: `FillDummySkeleton()` was a no-op stub in the `#ifdef __EMSCRIPTEN__` block. Skeleton `IsValid()` returned false, so `HamNavList::Poll()` disengaged and `mScrollBehavior.Update()` never ran.

**Fix**: Moved `FillDummySkeleton()` out of the platform-specific blocks to a shared section. It uses only `Skeleton` struct members — no platform APIs.

## Web Build System

```bash
# Full build (configure + compile + deploy)
scripts/build/web.sh

# Manual steps
emcmake cmake -S native -B native/build-web
cmake --build native/build-web -- -j$(nproc)
cp native/build-web/dc3-web.{js,wasm} native/web/build/

# Dev server
python3 native/web/server.py --port 8420
# Opens at http://localhost:8420
```

Build output: `native/build-web/dc3-web.js` + `dc3-web.wasm`, deployed to `native/web/build/`.

## Known Divergences from Desktop

| Area | Desktop | Web | Impact |
|------|---------|-----|--------|
| Video playback | FFmpeg `.bik` decode | Stubs (no video) | No intro/preview videos |
| Pose server | Unix socket to Python process | Not available | Dummy skeleton only |
| Audio | miniaudio native backend | Emscripten audio worklet | May have latency |
| File I/O | Direct disk reads | MEMFS (pre-bundled) | No dynamic file loads |
| Threading | `std::thread` for skeleton reader | Single-threaded stubs | N/A with dummy skeleton |
| `UsingCD()` | Returns true | Returns false (MEMFS) | Must guard editor-mode paths |
