# Web Build Debugging Guide

Techniques for debugging the DC3 WASM/Emscripten web port running in Chromium.

## Prerequisites

```bash
sudo pacman -S chromium
npm install playwright
```

WebGPU requires a real GPU context. The test scripts launch Chromium in headed mode against the existing display (`$DISPLAY`).

## Quick Reference

```bash
# Start dev server
python native/web/server.py --port 8420

# Smoke test (basic pass/fail)
native/web/tests/run-web-tests.sh

# Song loading diagnosis (navigates through full menu flow)
node native/web/tests/diagnose-song-load.js --no-server --verbose

# CDP debugger break (pauses at hang point, dumps call stack)
node native/web/tests/cdp-debugger-break.js --no-server

# Rebuild WASM
native/web/build.sh
```

## Debugging Techniques

### 1. CDP Debugger Break (best for hangs/infinite loops)

When the WASM hangs (no console output, page unresponsive), use Chrome DevTools Protocol to pause execution and dump the exact call stack.

**How it works**: Playwright gives CDP access via `page.context().newCDPSession(page)`. Enable `Debugger.enable`, wait for silence, then `Debugger.pause()`. The `Debugger.paused` event returns the full call stack with demangled C++ function names.

**Script**: `native/web/tests/cdp-debugger-break.js`

**Key flags**:
- `--silence N` — seconds of console silence before triggering break (default: 5)
- `--no-server` — use an existing server
- `--verbose` — print all console output

**Example output**:
```
=== CALL STACK ===
  #0: $MessageTimer::AddTime(Hmx::Object*, Symbol, float)
  #1: $OriginalChoreoRemixer::Handle(DataArray*, bool)
  ...
  #8: $DataWhile(DataArray*)           <-- infinite loop here
  ...
  #48: $GamePanel::StartIntro()
  #49: $GamePanel::Poll()
  #53: $mainLoop()
```

This immediately told us the hang was a DTA `while` loop in `perform.dta` called from `GamePanel::StartIntro()`, not an audio issue as log analysis had suggested.

**When to use**: Any time the game freezes and you don't know where. Works even when the WASM is blocking the main thread (Playwright keyboard/mouse APIs will hang, but CDP `Debugger.pause` still works).

**Implementation pattern** for new CDP debug scripts:
```javascript
const cdp = await page.context().newCDPSession(page);
await cdp.send('Debugger.enable');

cdp.on('Debugger.paused', (params) => {
    const { callFrames } = params;
    for (const frame of callFrames) {
        console.log(`${frame.functionName} at ${frame.url}:${frame.location.lineNumber}`);
    }
});

// ... wait for hang condition ...
await cdp.send('Debugger.pause');
```

### 2. Console Log Monitoring (best for crashes/progression)

The WASM binary routes all `printf`/`TheDebug` output through JavaScript `console.log`. Playwright captures this via `page.on('console')`.

**Script**: `native/web/tests/diagnose-song-load.js`

**What it tracks**:
- Screen transitions (`will enter 'screen_name'`)
- Loading milestones (PollForLoading, IsLoaded, DONE state 4, StartIntro)
- FAIL assertions
- Hang detection (configurable silence threshold)

**Key pattern**: The WASM runs on the main thread. If it enters an infinite loop, `page.keyboard.down()` and `page.screenshot()` will never resolve. Use `Promise.race` with timeouts:
```javascript
await Promise.race([
    page.screenshot({ path: 'screenshot.png' }),
    new Promise(r => setTimeout(r, 3000)),  // timeout fallback
]);
```

Run confirmation key presses and monitoring concurrently — don't await keys sequentially before starting the monitor, or you'll never detect the hang.

### 3. Automated Menu Navigation

The web port uses keyboard events mapped to gamepad buttons:
- `Space` → Start (kPad_Start, bit 11)
- `Enter` → A/Confirm (kPad_X, bit 6)
- `ArrowDown` → D-pad Down (kPad_DDown, bit 14)
- `Escape` → B/Back (kPad_Circle, bit 5)

Navigation sequence to reach gameplay:
```
attract → [Space] → title → [Space] → main → [Enter] → choose_mode
→ [Enter] → song_select → [Down x3] → [Enter] → multiuser → loading
→ preloading → real_loading → game_screen → StartIntro
```

Use `waitForScreen()` pattern — poll `allLogs` for `will enter 'screen_name'` before pressing the next key. Fixed timeouts between key presses are unreliable.

Must click canvas first for keyboard focus: `await page.click('canvas')`.

### 4. Screenshot Capture

Screenshots work when the WASM isn't blocking the main thread. Useful for:
- Verifying correct screen transitions
- Checking if the venue/characters render
- Comparing before/after fixes

```javascript
await page.screenshot({ path: '/tmp/claude-1000/gameplay.png' });
```

If the WASM is hung, wrap in `Promise.race` with a timeout — a failed screenshot is diagnostic in itself (confirms the main thread is blocked).

### 5. Error Log Analysis

Extract and categorize errors from a full test run:
```bash
# Run with verbose output to file
node scripts/web/gameplay.mjs --verbose > /tmp/claude-1000/full-log.txt 2>&1

# Extract unique errors
grep 'error\]' full-log.txt | sed 's/^\s*\[[0-9.]*s error\] //' | \
  sed 's/^\[XDK\] //' | sort -u > errors-deduped.txt

# Count specific patterns
grep -c "couldn't find.*in MetaMaterials" full-log.txt
grep -c "FAILED.*skeleton" full-log.txt
```

**Common error categories**:
- `WebAssets: FAILED on-demand fetch` — asset path mapping issue
- `FAIL: File: ... Line: ... Error:` — C++ assertion failure
- `not function or object` — DTA script referencing missing stub
- `unhandled msg` — message handler not implemented
- `couldn't find ... in MetaMaterials` — metamaterial system not loaded

## Common Issues

### Asset Path Mapping (`system/run/` files)

Files under `system/run/` (skeleton, metamaterials, fonts, UI resources) have a special path flow:
- Ark extraction stores `..` as `(..)` in directory names
- On disk: `orig-assets/extracted/(..)/(..)/system/run/ham/gen/skeleton.milo_xbox`
- Engine path: `/system/run/ham/gen/skeleton.milo_xbox` or `/../system/run/...`
- Server URL: `/api/file/system/run/ham/gen/skeleton.milo_xbox`

The server maps `system/run/` requests to `(..)/(..)/system/run/` on disk. If new `system/` subdirectories fail to load, check `server.py:_serve_asset_file`.

### Hang vs Crash

- **Hang** (no output, process alive): Use CDP debugger break. Usually a DTA `while` loop or blocking sync wait.
- **Crash** (RuntimeError/abort in console): Check the error message. Usually a null pointer or uninitialized field.
- **Silent stall** (output stops but no crash): Could be either. CDP break will tell you. Check if the last few log lines suggest what system was active.

### Sandbox

GPU access requires `dangerouslyDisableSandbox: true` for bash commands. Chromium + Vulkan needs unrestricted filesystem and device access.

### Server Must Match Build

After rebuilding WASM (`native/web/build.sh`), restart the dev server to serve the new files. The server reads from `native/web/build/` which is where the build outputs go. If using the server's `--assets` flag, ensure it points to `orig-assets/extracted/`.

## Test Scripts Reference

| Script | Purpose | Key Flags |
|--------|---------|-----------|
| `web-smoke.js` | Basic smoke test, detect crashes | `--wait-for`, `--timeout`, `--hang-timeout` |
| `diagnose-song-load.js` | Full menu navigation + song loading | `--timeout`, `--hang-timeout`, `--verbose` |
| `cdp-debugger-break.js` | Pause at hang, dump call stack | `--silence`, `--verbose` |
| `run-web-tests.sh` | Wrapper with log capture | `--verbose`, `--diagnose-hang` |

## Case Study: The StartIntro Hang (2026-03-17)

**Symptom**: Game loaded YMCA, reached `StartIntro`, then went silent after "queue rekicking crowd".

**Log analysis** suggested audio (wrong): `HamAudio::FinishLoad - stream 1 not ready for resync` appeared during loading, and no audio playback confirmation followed StartIntro.

**CDP debugger break** showed the truth: a DTA `{while}` loop in `perform.dta:1507` calling `{[remixer] measures_total}` was looping infinitely because `DanceRemixer::mTotalMeasures` was uninitialized (garbage value). The `OriginalChoreoRemixer::Init()` had an early `#ifdef HX_NATIVE return;` that skipped `DanceRemixer::Init()`.

**Fix**: Initialize `mTotalMeasures = 0` before the early return, and let Init proceed as far as possible rather than bailing out entirely.

**Lesson**: Log-based speculation can mislead. CDP debugger break gives ground truth in seconds.
