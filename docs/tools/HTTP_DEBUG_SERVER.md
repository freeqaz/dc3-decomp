# HTTP Debug Server

Embedded HTTP server for interacting with a running DC3 native engine instance. Query state, execute DTA commands, take screenshots, and tune settings — all via `curl` or any HTTP client.

Desktop-only (not available in Emscripten/web builds).

## Quick Start

```bash
# Enable the server (off by default)
DC3_HTTP=1 ./dc3-native

# Custom port (default 9090)
DC3_HTTP=1 DC3_HTTP_PORT=8080 ./dc3-native
```

## Endpoints

### Health

```bash
curl localhost:9090/api/health
# {"ok":true,"data":{"status":"ok","frame":1234,"uptime_s":5.2}}
```

Poll this to know when the engine is ready. Scripts can wait for a successful response before interacting.

### Telemetry

```bash
curl localhost:9090/api/telemetry
```

Returns the full `GameplayTelemetry::Snapshot` as JSON — everything that `DC3_TEL=1` dumps to stderr, but structured and on-demand. Fields include frame, state, beat, songAnimFrame, character clip layers, routine status, and more.

Requires `DC3_TEL=1` for the telemetry snapshot to update each sampling interval. Without it, the endpoint still works but reflects stale/initial state.

### Screenshot

```bash
curl -o frame.png localhost:9090/api/screenshot
```

Captures the current framebuffer as PNG. Requires headless rendering (the default when no window is present). The capture happens after `EndDrawing()` on the main thread, so the image reflects the fully rendered frame.

### Settings

```bash
# Read all settings
curl localhost:9090/api/settings

# Update one or more settings (query params)
curl -X PUT 'localhost:9090/api/settings?fovScale=1.2&cameraDebug=1'
```

Reads and modifies `NativeSettings` live. Available fields:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `cameraBlend` | bool | true | Enable camera blend transitions |
| `blendFramesSame` | float | 10.0 | Blend frames for same-category cuts |
| `blendFramesCross` | float | 15.0 | Blend frames for cross-category cuts |
| `fovScale` | float | 1.0 | FOV scale factor (1.0 = original) |
| `nearPlaneOverride` | float | -1.0 | Near plane override (-1 = per-camera) |
| `farPlaneOverride` | float | -1.0 | Far plane override (-1 = per-camera) |
| `aspectOverride` | float | -1.0 | Aspect ratio override (-1 = auto) |
| `camForwardOffset` | float | 0.0 | View-space forward offset |
| `camHeightOffset` | float | 0.0 | View-space height offset |
| `camLateralOffset` | float | 0.0 | View-space lateral offset |
| `cameraDebug` | bool | false | Camera debug overlay |

### DTA Eval

```bash
# Arithmetic
curl -X POST localhost:9090/api/dta/eval -d '{+ 1 2}'
# {"ok":true,"data":{"type":"int","value":3}}

# Set and use variables
curl -X POST localhost:9090/api/dta/eval -d '{set $x 10}{+ $x 5}'
# {"ok":true,"data":{"type":"int","value":15}}

# Query engine state
curl -X POST localhost:9090/api/dta/eval -d '{ui current_screen}'

# Call object methods
curl -X POST localhost:9090/api/dta/eval \
  -d '{$venue find "spotlight_01" get_showing}'

# Navigate UI
curl -X POST localhost:9090/api/dta/eval \
  -d '{ui goto_screen song_select_screen}'
```

Executes DTA expressions on the main thread and returns the result. Supports multi-command sequences (separated by `}{`). The body can be raw DTA text or JSON `{"expr": "..."}`.

Result types: `int`, `float`, `symbol`, `object` (returns object name). Functions that return temporary strings (like `sprint`) may produce garbled output — use `sprintf` with variables instead.

**Important**: Use `{ui goto_screen <name>}` for navigation, not `{flow_mgr goto_screen ...}` — `flow_mgr` is not a registered object in the native port. Jumping to screens that require prior setup (e.g. `loading_screen` without a song selected) will crash the engine — use the `MILO_INPUT_SCRIPT` system for safe menu navigation flows.

### DTA Functions List

```bash
curl localhost:9090/api/dta/funcs
# {"ok":true,"data":["option_bool","option_str",...]}
```

Lists all 381 registered `DataFunc` names. Useful for discovering what DTA commands are available.

### Objects (Phase 3)

```bash
# List all objects in the main directory
curl localhost:9090/api/objects
# [{"name":"ui","type":"UIManager"},{"name":"rnd","type":"WgpuRnd"}, ...]

# List all objects recursively (includes subdirs)
curl 'localhost:9090/api/objects?recurse=true'

# List objects in a specific directory
curl 'localhost:9090/api/objects?dir=world_panel'

# Get object details (type, position, showing, dir info)
curl localhost:9090/api/objects/spotlight_01
# {"name":"spotlight_01","type":"RndLight","dir":"world/glitterati",
#  "position":{"x":1.5,"y":2.0,"z":-3.0},"showing":true,"order":0.0}

# List children of a directory-type object
curl localhost:9090/api/objects/world_panel/children
# {"objects":[...],"subdirs":[{"name":"venue","type":"RndDir","objectCount":42}]}

# Scene tree (nested directory structure)
curl localhost:9090/api/scene/tree
curl 'localhost:9090/api/scene/tree?depth=5'
```

Object details include type-specific fields:
- **RndTransformable**: `position` (x/y/z), `transParent`
- **RndDrawable**: `showing`, `order`, `sphere` (if non-zero)
- **ObjectDir**: `isDir`, `objectCount`, `subDirCount`

### Input Injection (Phase 4)

```bash
# Press a button this frame
curl -X POST localhost:9090/api/input/press -d '{"button":"confirm"}'

# Queue a button sequence (delays are cumulative frames between events)
curl -X POST localhost:9090/api/input/sequence -d '[
  {"button":"dpad_down","delay":15},
  {"button":"dpad_down","delay":15},
  {"button":"confirm","delay":30}
]'
```

Button names: `confirm`/`a`, `cancel`/`b`, `start`, `select`/`back`/`option`, `up`/`dpad_up`, `down`/`dpad_down`, `left`/`dpad_left`, `right`/`dpad_right`, `l1`/`lb`, `r1`/`rb`, `l2`/`lt`, `r2`/`rt`, `x`, `y`, `l3`/`ls`, `r3`/`rs`.

Works in both windowed and headless modes. HTTP-injected buttons are OR'd with keyboard/gamepad/script input.

### Screen + Frame (Phase 4)

```bash
# Get current screen name
curl localhost:9090/api/screen
# {"ok":true,"data":{"screen":"attract_screen"}}

# Long-poll: block until a specific screen is active (max 60s timeout)
curl 'localhost:9090/api/screen/wait/main_screen'
curl 'localhost:9090/api/screen/wait/game_screen?timeout=30'

# Get current frame number
curl localhost:9090/api/frame
# {"ok":true,"data":{"frame":1234}}

# Long-poll: block until frame N is reached
curl localhost:9090/api/frame/wait/5000
```

Screen wait replaces the polling loop pattern — instead of repeatedly querying the screen, a single request blocks until the target screen is active.

## Example Workflows

### Wait for engine ready, then screenshot

```bash
#!/bin/bash
while ! curl -sf localhost:9090/api/health >/dev/null; do sleep 1; done
echo "Engine ready"
curl -o screenshot.png localhost:9090/api/screenshot
```

### Live camera tuning session

```bash
# Widen FOV
curl -X PUT 'localhost:9090/api/settings?fovScale=1.3'
curl -o wide.png localhost:9090/api/screenshot

# Reset
curl -X PUT 'localhost:9090/api/settings?fovScale=1.0'
```

### HTTP-only navigation (no input script needed)

Phase 4 screen waits + input injection can fully replace `MILO_INPUT_SCRIPT` for interactive sessions:

```bash
DC3_HTTP=1 MILO_MAX_FRAMES=500000 ./dc3-native &

# Wait for engine ready
while ! curl -sf localhost:9090/api/health >/dev/null; do sleep 1; done

# Navigate through menus using screen waits + button presses
curl -s 'localhost:9090/api/screen/wait/attract_screen'
curl -X POST localhost:9090/api/input/press -d '{"button":"confirm"}'

curl -s 'localhost:9090/api/screen/wait/title_screen'
curl -X POST localhost:9090/api/input/press -d '{"button":"confirm"}'

curl -s 'localhost:9090/api/screen/wait/main_screen'
curl -X POST localhost:9090/api/input/press -d '{"button":"confirm"}'

# Wait for gameplay
curl -s 'localhost:9090/api/screen/wait/game_screen?timeout=60'
curl -o gameplay.png localhost:9090/api/screenshot
```

### Combined: Input script + HTTP server

The HTTP server also works alongside `MILO_INPUT_SCRIPT` — the script handles full menu navigation, while HTTP gives interactive control during gameplay.

```bash
DC3_HTTP=1 DC3_TEL=1 MILO_MAX_FRAMES=500000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  ./dc3-native &

# Wait for engine ready, then wait for gameplay screen
while ! curl -sf localhost:9090/api/health >/dev/null; do sleep 1; done
curl -s 'localhost:9090/api/screen/wait/game_screen?timeout=60'

# Now interact during gameplay
curl -s localhost:9090/api/telemetry | python3 -m json.tool
curl -o gameplay.png localhost:9090/api/screenshot
curl -X PUT 'localhost:9090/api/settings?fovScale=1.3'
```

**Note**: In headless mode, the engine exits after `MILO_MAX_FRAMES` (default 10000, ~8.6 seconds). Set a higher value for interactive sessions.

### Python automation

```python
import requests

BASE = "http://localhost:9090/api"

# Wait for ready
requests.get(f"{BASE}/health").raise_for_status()

# Navigate to main menu using screen waits
requests.get(f"{BASE}/screen/wait/attract_screen", timeout=60)
requests.post(f"{BASE}/input/press", json={"button": "confirm"})

requests.get(f"{BASE}/screen/wait/title_screen", timeout=30)
requests.post(f"{BASE}/input/press", json={"button": "confirm"})

requests.get(f"{BASE}/screen/wait/main_screen", timeout=30)

# Browse objects in the scene
objects = requests.get(f"{BASE}/objects?recurse=true").json()
for obj in objects["data"][:10]:
    print(f"  {obj['name']} ({obj['type']})")

# Get details on a specific object
detail = requests.get(f"{BASE}/objects/world_panel").json()
print(detail)

# Take screenshot
img = requests.get(f"{BASE}/screenshot")
open("frame.png", "wb").write(img.content)

# Read telemetry
tel = requests.get(f"{BASE}/telemetry").json()
print(f"Frame {tel['data']['frame']}, beat {tel['data']['beat']}")
```

## Architecture

- **Thread model**: cpp-httplib runs a background thread (thread-per-connection). All mutating operations (DTA eval, settings changes, screenshots, object queries) are queued to the main thread via a command queue with condition variable signaling. HTTP handler threads block until the main thread processes their command.
- **Main loop integration**: `ProcessCommands()` runs after `GameplayTelemetry::Sample()` (before drawing). `ProcessScreenshots()` runs after `EndDrawing()` (framebuffer is ready). `NotifyFrame()` updates screen/frame state for long-poll waits.
- **Input injection**: HTTP-queued button bits are consumed by `JoypadPoll()` via `ConsumeHttpButtons()`, OR'd with keyboard/gamepad/script input. Works in both windowed and headless modes.
- **Long-poll waits**: Screen and frame wait endpoints block on a condition variable that the main loop notifies each frame. Max timeout 60 seconds to prevent thread leaks.
- **CORS**: All responses include `Access-Control-Allow-Origin: *` for browser-based tools.
- **Shutdown**: Server stops before engine teardown. Long-poll threads are woken via `notify_all()` and exit cleanly.

## Files

| File | Description |
|------|-------------|
| `native/src/platform/HttpServer.h` | Server class, command queue, input/wait state |
| `native/src/platform/HttpServer.cpp` | All endpoint handlers (~650 LOC) |
| `native/include/httplib.h` | Vendored cpp-httplib (MIT, header-only) |

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `DC3_HTTP` | `0` | Set to `1` to enable the server |
| `DC3_HTTP_PORT` | `9090` | Port to listen on |
| `DC3_FAST_BOOT` | `0` | Set to `1` to skip boot screens in 10 frames instead of ~360 |
