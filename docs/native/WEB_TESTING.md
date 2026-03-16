# Web Port Testing Infrastructure

Automated testing for the DC3 web port using Playwright + Xvfb.

## Prerequisites

```bash
# System packages
sudo pacman -S xorg-server-xvfb chromium

# Node dependencies (from repo root)
npm install playwright
```

WebGPU requires a real GPU context — headless Chrome can't initialize WebGPU without X11/Vulkan. We use `xvfb-run` to provide a virtual X11 display that ANGLE/Vulkan can use.

## Quick Start

```bash
# Start the dev server in one terminal
python native/web/server.py --port 8420

# Run smoke test (auto xvfb, auto server detection)
native/web/tests/run-web-tests.sh

# Or manually with more control
xvfb-run -a --server-args="-screen 0 1920x1080x24" \
  node native/web/tests/web-smoke.js --no-server --timeout 45 --verbose
```

## Test Scripts

### `web-smoke.js` — General Smoke Test

Launches Chrome, navigates to the web port, captures all WASM console output, detects crashes and hangs.

```bash
# Basic: auto-start server, 60s timeout
node native/web/tests/web-smoke.js

# With existing server
node native/web/tests/web-smoke.js --no-server

# Wait for a specific log line (exit 0 on match, 2 if not seen)
node native/web/tests/web-smoke.js --no-server --wait-for "main_screen entered"

# Full verbose output with custom timeouts
node native/web/tests/web-smoke.js --no-server --timeout 120 --hang-timeout 15 --verbose

# Save full log capture to JSON
node native/web/tests/web-smoke.js --no-server --save-logs /tmp/smoke.json
```

**Flags:**
| Flag | Default | Description |
|------|---------|-------------|
| `--port N` | 8420 | Server port |
| `--timeout N` | 60 | Overall timeout (seconds) |
| `--hang-timeout N` | 10 | Console silence = hang (seconds) |
| `--wait-for "text"` | none | Success when this log line appears |
| `--no-server` | false | Don't auto-start server |
| `--verbose` | false | Print all console output |
| `--save-logs path` | none | Save full logs as JSON |

**Exit codes:** 0=success, 1=crash/WebGPU failure, 2=hang detected, 3=infra error

### `run-web-tests.sh` — Wrapper Script

Handles xvfb-run, asset directory detection, timestamped log files.

```bash
# Standard run
native/web/tests/run-web-tests.sh

# Verbose
native/web/tests/run-web-tests.sh --verbose

# Hang diagnosis mode (120s timeout, verbose, 15s hang threshold)
native/web/tests/run-web-tests.sh --diagnose-hang

# Skip xvfb (if you have a display)
native/web/tests/run-web-tests.sh --no-xvfb
```

Logs are saved to `native/web/build/test-results/smoke_YYYYMMDD_HHMMSS.json`.

### `diagnose-song-load.js` — Song Loading Diagnosis

Specialized test that navigates past the main menu to trigger song loading, tracking milestones through the loading pipeline.

```bash
xvfb-run -a --server-args="-screen 0 1920x1080x24" \
  node native/web/tests/diagnose-song-load.js --no-server --verbose
```

Tracks milestones: `main_screen entered` → `game_screen transition` → `PollForLoading entry` → `song data loading` → `song merger complete` → `IsLoaded state` → `DONE (state 4)` → `StartIntro`.

## How It Works

1. **Xvfb** provides a virtual X11 display (`:99` or auto-assigned)
2. **Chromium** launches with `--enable-unsafe-webgpu --use-angle=vulkan` for GPU access
3. **Playwright** controls the browser, captures `console.log` output
4. Emscripten routes all WASM `printf`/`TheDebug` output through JavaScript `console.log`
5. Tests poll for specific log patterns, detect hangs (no output for N seconds), and report results

## Architecture

```
xvfb-run
  └── node web-smoke.js
        └── Playwright → Chromium (WebGPU + ANGLE/Vulkan)
              └── localhost:8420
                    ├── index.html (canvas + loader)
                    ├── dc3-web.js (Emscripten glue)
                    ├── dc3-web.wasm (game binary)
                    └── server.py (Python, serves assets + /api/health)
```

## Console Output Pipeline

The WASM binary uses `printf` and `TheDebug` for logging. Emscripten captures these and routes them to the browser console:

- `printf` → Emscripten `Module.print` → `console.log`
- `TheDebug << "..."` → Emscripten `Module.printErr` → `console.warn`

Playwright captures all console messages via `page.on('console')`, giving us the full engine log stream programmatically.

## Troubleshooting

**WebGPU init fails:**
- Ensure Vulkan drivers are installed (`vulkaninfo` should work)
- Check that xvfb-run is providing a display: `xvfb-run -a echo $DISPLAY`
- Try `--use-angle=swiftshader` as fallback (slower, software rendering)

**Server won't start:**
- Check port isn't in use: `lsof -i :8420`
- Verify `python native/web/server.py` works standalone

**No console output captured:**
- The WASM must be built with `printf` support (not optimized out)
- Check `native/web/build/dc3-web.js` exists (run `native/web/build.sh` first)

**Hang detected but game is actually running:**
- Increase `--hang-timeout` (game may have long loading periods with no log output)
- Add more `printf` instrumentation to loading code paths
