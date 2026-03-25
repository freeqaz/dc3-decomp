# Web Port Testing Infrastructure

Automated testing for the DC3 web port using Playwright + system Chromium.

## Prerequisites

```bash
# System packages
sudo pacman -S chromium

# Node dependencies (from repo root)
npm install playwright
```

WebGPU requires a real GPU context. The test scripts launch system Chromium in **headed** mode against the existing display.

## Quick Start

```bash
# Start the dev server in one terminal
python3 native/web/server.py --port 8420

# Take a screenshot (default agent command)
node scripts/web/screenshot.mjs --verbose

# Scroll through song list with per-scroll screenshots
node scripts/web/scroll.mjs --scrolls 5 --verbose

# Full gameplay diagnosis
node scripts/web/gameplay.mjs --verbose
```

## Test Commands

All commands live in `scripts/web/` and share a core module (`scripts/web/lib/core.mjs`). Server must be running on the target port (default 8420).

### `screenshot.mjs` — Quick Screenshot

Navigate to song_select, save one PNG. The default command for agents.

```bash
node scripts/web/screenshot.mjs                        # defaults
node scripts/web/screenshot.mjs --out /tmp/shots --verbose
```

### `scroll.mjs` — Song List Scroll Test

Navigate to song_select, take initial + per-scroll screenshots.

```bash
node scripts/web/scroll.mjs --scrolls 10 --verbose
node scripts/web/scroll.mjs --scrolls 3 --out /tmp/scroll-shots
```

### `gameplay.mjs` — Song Loading Diagnosis

Navigate to gameplay, track loading milestones, detect hangs.

```bash
node scripts/web/gameplay.mjs --verbose --timeout 60
node scripts/web/gameplay.mjs --song-index 5 --hang-timeout 20
```

**Extra flags:**
| Flag | Default | Description |
|------|---------|-------------|
| `--song-index N` | 3 | Scrolls down N times before selecting (skips tier header) |
| `--timeout N` | 90 | Overall timeout (seconds) |
| `--hang-timeout N` | 15 | Console silence = hang (seconds) |

**Exit codes:** 0=success, 2=hang detected

### `cdp-break.mjs` — CDP Debugger Break

Pause at hang point via Chrome DevTools Protocol, dump WASM call stack.

```bash
node scripts/web/cdp-break.mjs --verbose
node scripts/web/cdp-break.mjs --song-index 3 --silence 10
```

**Extra flags:**
| Flag | Default | Description |
|------|---------|-------------|
| `--song-index N` | 3 | Scrolls before selecting |
| `--silence N` | 5 | Seconds of silence before triggering break |

### Common Flags (all commands)

| Flag | Default | Description |
|------|---------|-------------|
| `--port N` | 8420 | Server port |
| `--out DIR` | `/tmp/dc3-web/<cmd>-<timestamp>/` | Output directory |
| `--verbose` | off | Print all console output (errors always print) |

### npm Shortcuts

```bash
npm run web:screenshot
npm run web:scroll -- --scrolls 5
npm run web:gameplay -- --verbose
npm run web:cdp-break -- --silence 10
```

## How It Works

1. **Chromium** launches in headed mode with `--enable-unsafe-webgpu --use-angle=vulkan` for GPU access
2. **Playwright** controls the browser, captures `console.log` output
3. Emscripten routes all WASM `printf`/`TheDebug` output through JavaScript `console.log`
4. Tests poll for specific log patterns, detect hangs (no output for N seconds), and report results

The scripts check `process.env.DISPLAY` — if a display exists (desktop), Chrome launches headed.

## Architecture

```
scripts/web/
  lib/core.mjs         ← shared module (browser, capture, navigation, I/O)
  screenshot.mjs        ← quick screenshot (default for agents)
  scroll.mjs            ← song list scroll + per-scroll screenshots
  gameplay.mjs          ← full song loading diagnosis
  cdp-break.mjs         ← CDP debugger pause + WASM call stack

node scripts/web/screenshot.mjs
  └── core.mjs → Playwright → Chromium (headed, WebGPU + ANGLE/Vulkan)
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

## Keyboard Input Mapping (Browser)

| Key | Gamepad | Use |
|-----|---------|-----|
| `Space` | Start (kPad_Start) | Dismiss screens, skip intro |
| `Enter` | A/Confirm (kPad_X) | Select menu items |
| `ArrowDown` | D-pad Down | Navigate lists |
| `ArrowUp` | D-pad Up | Navigate lists |
| `Escape` | B/Back (kPad_Circle) | Go back |

Navigation sequence: `attract → [Space] → title → [Space] → main → [Enter] → choose_mode → [Enter] → song_select → [Down] → scroll`

## Sandbox (Claude Code agents)

GPU access requires `dangerouslyDisableSandbox: true` for bash commands. Chromium + Vulkan needs unrestricted filesystem and device access.

## Troubleshooting

**WebGPU init fails:**
- Ensure Vulkan drivers are installed (`vulkaninfo` should work)
- Verify `DISPLAY` is set
- Try `--use-angle=swiftshader` as fallback (slower, software rendering)

**Server won't start:**
- Check port isn't in use: `lsof -i :8420`
- Verify `python native/web/server.py` works standalone

**No console output captured:**
- The WASM must be built with `printf` support (not optimized out)
- Check `native/web/build/dc3-web.js` exists (run `scripts/build/web.sh` first)

**Hang detected but game is actually running:**
- Increase `--hang-timeout` (game may have long loading periods with no log output)
- Add more `printf` instrumentation to loading code paths
