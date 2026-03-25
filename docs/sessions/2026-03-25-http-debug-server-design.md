# DC3 Native HTTP Debug Server — Design Report

**Date**: 2026-03-25

## Context

The DC3 native port already has significant debugging infrastructure: headless rendering, input scripting, telemetry logging, ImGui debug panels, and a DTA overlay system. But the workflow is still **batch-oriented** — you set env vars, run the binary, capture output, and inspect after-the-fact. There's no way to interact with a running engine instance.

An embedded HTTP server changes this to **interactive debugging**: query state, execute DTA commands, take screenshots, inject input — all while the game runs.

## Architecture

**Threading**: The server runs in a background thread (matching existing patterns in `Skeleton_Native.cpp` and `AudioDevice.cpp` which already use `std::thread` + `std::mutex`). Engine state is accessed via a command queue processed on the main thread during `App::Run()`'s frame loop.

**Library**: **cpp-httplib** (single header-only C++ library, ~6000 LOC, no dependencies, MIT license). No system packages to install — just drop the header. It handles threading internally (thread-per-connection). Supports WebSockets and SSE/streaming natively. Alternatives like libmicrohttpd work too but add an external dependency.

**Safety**: All mutating requests get queued to the main thread. Read-only telemetry uses double-buffered snapshots (same pattern as `Skeleton_Native.h`'s `mSwapMutex`). No direct engine access from the HTTP thread.

**Shutdown ordering**: The HTTP server thread must stop **before** engine teardown begins. In-flight requests could access freed memory otherwise. Follow the `Skeleton_Native` pattern: set `mRunning = false`, call `server.stop()`, join the thread. This should happen early in `App::~App()` or an explicit shutdown path, before any `ObjectDir` or subsystem destruction.

**Platform constraint**: The HTTP server is **desktop-only**. Emscripten runs single-threaded in the browser — no background threads, no socket server. The cmake guard (`OFF for Emscripten`) is a hard constraint, not a preference.

**CORS**: All responses include `Access-Control-Allow-Origin: *` to allow browser-based debug tools (React panels, web dashboards, etc.). One-liner middleware in cpp-httplib.

**Configuration**: Follows `NativeSettings.h` pattern — env var driven (using `DC3_` prefix, consistent with `DC3_TEL`):
- `DC3_HTTP=1` — enable server (off by default)
- `DC3_HTTP_PORT=9090` — port (default 9090)

**Error response format**: All endpoints return a consistent JSON envelope:
```json
{"ok": true, "data": {...}}
{"ok": false, "error": "description of what went wrong"}
```
Non-JSON responses (screenshots, hex dumps) use appropriate content types and return HTTP status codes directly.

---

## Phase 1 — Foundation + Telemetry (Highest ROI)

**Why first**: Eliminates the most painful workflow: parsing stderr logs for telemetry. Gives us a programmatic interface to a running engine for everything that follows.

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/health` | GET | `{"status":"ok","frame":12345,"uptime_s":42.0}` |
| `/api/telemetry` | GET | Full `GameplayTelemetry` data as JSON (everything currently dumped to stderr) |
| `/api/screenshot` | GET | Capture current frame as PNG, return as `image/png` |
| `/api/settings` | GET/PUT | Read/modify `NativeSettings` live (camera blend, FOV, offsets) |

### What this solves
- **No more grep-ing telemetry logs** — `curl localhost:9090/api/telemetry | jq` gives structured data
- **On-demand screenshots** — no more pre-specifying frame numbers; take one whenever you want
- **Live camera tuning** — adjust FOV, blend frames, offsets without restarting
- **Health monitoring** — scripts can poll `/health` to know when the engine is ready

### Implementation scope
- `native/src/platform/HttpServer.h/cpp` — ~400 LOC
- Telemetry snapshot struct (mirrors `GameplayTelemetry::Sample` but captures to a struct instead of fprintf)
- Main loop hook: queue processing + telemetry snapshot each frame
- CMake: add cpp-httplib header to `native/include/`

---

## Phase 2 — DTA Script Execution (Killer Feature)

**Why second**: The engine already has a full scripting language (DTA). Exposing it over HTTP turns the debug server into a **universal engine remote control** — anything the engine can do via DTA, you can do via HTTP.

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/dta/eval` | POST | Execute DTA expression, return result as JSON. Body: `{"expr": "{game_mode current_mode}"}` |
| `/api/dta/upload` | POST | Upload a .dta file to the overlay dir (shadows archive files without restart) |
| `/api/dta/funcs` | GET | List all registered `DataFunc` names |

### What this solves
- **Script the engine without recompiling** — post DTA to change game state, trigger transitions, modify objects
- **Hot-reload DTA** — upload modified .dta files to the overlay directory, then trigger reload
- **Query any engine state** — DTA can call any `Handle()` method on any object: `{$venue find "spotlight_01" get_showing}`
- **Automate navigation** — `{ui goto_screen main_screen}` via HTTP for screens that don't require prior state (use Phase 4 input injection for full menu flows)

### Examples
```bash
# Query current screen
curl -X POST localhost:9090/api/dta/eval -d '{"expr":"{ui current_screen}"}'

# Toggle an object's visibility
curl -X POST localhost:9090/api/dta/eval -d '{"expr":"{$venue find \"spotlight\" set_showing FALSE}"}'

# Upload modified venue DTA
curl -X POST localhost:9090/api/dta/upload -F "file=@my_venue.dta" -F "path=config/venue.dta"
```

### Implementation scope
- DTA text parsing — use `DataReadString(const char*)` (`DataFile.h`/`DataFile.cpp:469`) which wraps the text in a `BufStream` and calls `DataReadStream()` to parse. This handles the full DTA grammar including `#ifdef`, `{merge}`, macros. Thread-safe via `gDataReadCrit` critical section.
- Command queue with response futures (main thread executes, HTTP thread waits for result)
- JSON serialization for DataNode results — include type tag alongside value so callers can distinguish:
  ```json
  {"ok": true, "type": "symbol", "value": "main_screen"}
  {"ok": true, "type": "int", "value": 42}
  {"ok": true, "type": "array", "value": [...]}
  ```
- ~300 LOC

### Implementation status
- `/api/dta/eval` — **implemented and tested**. Supports raw text body and JSON `{"expr":"..."}` body. Multi-expression sequences work (`{set $x 10}{+ $x 5}` → 15). Returns typed JSON results.
- `/api/dta/funcs` — **implemented and tested**. Returns all 381 registered DataFunc names.
- `/api/dta/upload` — **not implemented**. Would require writing to the overlay directory + DTA cache invalidation. Lower priority since manual file edits + restart work for now.

---

## Testing Results (Phases 1 + 2)

Phases 1 and 2 are implemented and verified. Key findings from integration testing:

### What works well

- **All Phase 1 endpoints**: health, telemetry, screenshot, settings GET/PUT — all return correct JSON/PNG.
- **DTA eval**: Arithmetic (`{+ 1 2}` → 3), variables (`{set $x 10}{+ $x 5}` → 15), engine queries (`{ui current_screen}` → `"attract_screen"`), screen navigation (`{ui goto_screen main_screen}`) all work.
- **DTA funcs**: Returns all 381 registered DataFunc names.
- **Screenshot quality**: 1280x720 RGBA PNG, valid image with full scene rendering (characters, HUD, venue).
- **Live settings**: `fovScale` change applied mid-gameplay and visible in subsequent screenshots.
- **Combined workflow**: `MILO_INPUT_SCRIPT` + `DC3_HTTP=1` works — input script navigates menus, HTTP provides interactive access during gameplay. This is the recommended approach for reaching gameplay state.

### Gotchas discovered

1. **`flow_mgr` is null**: `{$this find flow_mgr}` returns null. Screen navigation uses `{ui goto_screen <name>}`, not `{flow_mgr goto_screen ...}`. Updated docs accordingly.
2. **Unsafe screen jumps crash**: `{ui goto_screen loading_screen}` without prior song selection causes SIGSEGV at address 0x10 (null deref). Screens that require setup state (loading, game) must be reached through proper menu flow.
3. **`MILO_MAX_FRAMES` default is 10000**: In headless mode, the engine exits after ~8.6 seconds. Easy to miss — the server appears healthy, then suddenly dies. Must set `MILO_MAX_FRAMES=500000` or higher for interactive sessions.
4. **`start_song` is not a DTA func**: `{start_song ymca}` silently returns 0. Song start requires the full UI flow (song select → multiuser → loading chain).
5. **`/api/dta/upload` not implemented**: Phase 2 design doc listed it but it was not built. DTA hot-reload would require overlay directory write + DataArray cache invalidation.

### Performance observations

- Server startup takes ~8 seconds (mostly ark loading + GPU init).
- Headless rendering runs at ~1150 FPS (10000 frames in 8.65 seconds).
- HTTP requests during gameplay have no measurable impact on frame rate.
- Telemetry snapshot during gameplay shows full pipeline active: `charClipLayers=2, player0=true, clipKeyCount=48, routineLoaded=1`.

---

## Phase 3 — Object Introspection

**Status**: **Implemented and compiles**. All endpoints below are live.

**Why third**: Once you can execute DTA, you want to browse the scene graph to know *what* to script. This phase makes the engine's object model queryable.

**Testing insight**: Phase 2 testing revealed that object discovery is a real pain point. `{$this find flow_mgr}` returned null — without `/api/objects` there's no way to know what objects *do* exist short of guessing names.

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/objects` | GET | List all objects in `ObjectDir::Main()` with types |
| `/api/objects/<path>` | GET | Get object details: type, properties, transform, showing |
| `/api/objects/<path>/props` | GET | List all gettable properties (via `Handle` + `PropSync`) |
| `/api/objects/<path>/children` | GET | List children (for Dir types) |
| `/api/scene/tree` | GET | Full scene graph as nested JSON |
| `/api/world` | GET | Current WorldDir state: camera, lighting, env |

### What this solves
- **Browse the scene graph** — understand what objects exist, their hierarchy, their state
- **Debug rendering** — which drawables are showing? What's their transform?
- **Find objects to script** — discover object names for DTA commands

### Implementation scope
- ObjectDir traversal with depth limiting
- RndTransformable -> JSON (position, rotation)
- RndDrawable -> JSON (showing, draw_order, sphere)
- ~500 LOC

---

## Phase 4 — Input Injection + Automation

**Status**: **Implemented and compiles**. All endpoints below are live. Input injection integrated into `JoypadPoll()`.

**Why fourth**: Builds on the DTA eval capability to provide clean input automation without file-based scripts.

**Testing insight**: This phase is more important than originally ranked. Testing revealed that DTA-based navigation (`{ui goto_screen ...}`) is **unsafe** for screens that require prior state — jumping to `loading_screen` without a selected song causes SIGSEGV. The `MILO_INPUT_SCRIPT` system works but is batch-only and file-based. HTTP input injection enables safe, interactive, screen-by-screen navigation with proper state transitions.

The `wait_screen` + frame-relative timing model from `MILO_INPUT_SCRIPT` (`Joypad_Native.cpp`) is battle-tested and should be the blueprint. Key design decisions:

- **Screen wait is essential**: The `wait_screen` directive in input scripts is what makes navigation reliable — it waits until the target screen is active before starting the frame counter for inputs. The `/api/screen/wait/<name>` endpoint must mirror this. Without it, inputs arrive at the wrong screen.
- **Input must go through JoypadPoll**: The existing `GetScriptedButtons()` path in `Joypad_Native.cpp` (line ~458) already injects synthetic button state safely. HTTP input should use the same mechanism — add HTTP-queued bits alongside script-queued bits.
- **Button names**: Use the same `ParseButtonName()` mapping (confirm→kPad_X, cancel→kPad_Circle, dpad_down→kPad_DDown, etc.) for consistency with input scripts.
- **`/api/screen` replaces repeated DTA eval**: Currently querying the screen requires `curl -X POST /api/dta/eval -d '{ui current_screen}'` — a dedicated `GET /api/screen` is cleaner and avoids the DTA parsing overhead.

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/input/press` | POST | Inject a button press this frame: `{"button":"confirm"}` |
| `/api/input/sequence` | POST | Queue a sequence: `[{"button":"dpad_down","delay":30},{"button":"confirm","delay":30}]` |
| `/api/screen` | GET | Current UI screen name (shortcut for DTA eval) |
| `/api/screen/wait/<name>` | GET | Long-poll: blocks until screen changes to `<name>` (with timeout) |
| `/api/frame` | GET | Current frame number |
| `/api/frame/wait/<n>` | GET | Long-poll: blocks until frame N reached |

### What this solves
- **Safe song start via HTTP** — currently requires `MILO_INPUT_SCRIPT` file; this enables the same flow interactively
- **Programmatic test scripts in any language** — Python, bash, whatever can do HTTP
- **Screen-aware navigation** — wait for screen transitions instead of guessing frame numbers
- **Replaces MILO_INPUT_SCRIPT** for interactive use — still useful for batch, but HTTP is better for iterative work

### Example workflow
```python
import requests
BASE = "http://localhost:9090/api"

# Wait for attract screen, skip it
requests.get(f"{BASE}/screen/wait/attract_screen", timeout=60)
requests.post(f"{BASE}/input/press", json={"button": "confirm"})

# Wait for title, skip it
requests.get(f"{BASE}/screen/wait/title_screen", timeout=30)
requests.post(f"{BASE}/input/press", json={"button": "confirm"})

# Wait for main menu, select Dance
requests.get(f"{BASE}/screen/wait/main_screen", timeout=30)
requests.post(f"{BASE}/input/press", json={"button": "confirm"})

# Wait for song select, scroll to a song and confirm
requests.get(f"{BASE}/screen/wait/song_select_screen", timeout=30)
requests.post(f"{BASE}/input/sequence", json=[
    {"button": "dpad_down", "delay": 15} for _ in range(20)
] + [{"button": "confirm", "delay": 30}])

# Wait for gameplay
requests.get(f"{BASE}/screen/wait/game_screen", timeout=60)
img = requests.get(f"{BASE}/screenshot")
open("/tmp/gameplay.png", "wb").write(img.content)
```

### Implementation scope
- Hook into `JoypadPoll()` to inject synthetic button state (alongside existing `GetScriptedButtons()`)
- Screen name query via `TheUI->CurrentScreen()` — already used by DTA eval path
- Long-poll with condition variable + timeout (max 60s ceiling to prevent thread leaks from forgotten clients)
- Button name parsing via existing `ParseButtonName()` in `Joypad_Native.cpp`
- ~400 LOC

---

## Phase 5 — Memory Inspection + Struct Debugging

**Why fifth**: More specialized — useful for decomp work specifically when debugging struct layout mismatches.

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/memory/object/<path>` | GET | Read raw bytes of a named object (by ObjectDir lookup). `?offset=0&size=256` for partial reads. |
| `/api/memory/object/<path>/field` | GET | Read a specific field: `?offset=0x118&type=float` -> interpreted value |
| `/api/vtable/<path>` | GET | Dump vtable pointer + first N entries for a live object |

**Removed**: `/api/memory/read` (arbitrary address) and `/api/memory/struct` (arbitrary address + type). Raw address access risks segfaults that crash the engine. The object-based endpoints cover 90%+ of the use case safely — objects are resolved via `ObjectDir`, guaranteeing valid addresses. If raw reads are needed later, validate against `/proc/self/maps` or known allocator ranges.

### What this solves
- **Live struct inspection** — verify field offsets against Ghidra without recompiling with debug prints
- **Compare live state to expected** — "is offset 0x118 really the value I think it is?"
- **Vtable verification** — confirm vtable layout matches decomp headers

### Implementation scope
- Object -> address resolution via ObjectDir (`ObjectDir::Main()->FindObject()`)
- Bounds-checked reads against object size (where known via RTTI or type registry)
- Hex dump formatting + typed field interpretation (int, float, symbol, pointer)
- ~300 LOC

---

## Phase 6 — Performance Profiling + Frame Analysis

**Why last**: Nice-to-have, not critical for current workflows.

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/perf/frame` | GET | Frame timing breakdown: poll time, draw time, GPU time |
| `/api/perf/history` | GET | Last N frames of timing data |
| `/api/perf/objects` | GET | Draw call count, mesh count, texture memory |
| `/api/events` | WebSocket | Real-time event stream (screen changes, asset loads, errors) |

---

## Implementation Plan

**Library**: `cpp-httplib` (header-only, MIT, no deps). Drop `httplib.h` into `native/include/`.

**New files**:
```
native/src/platform/HttpServer.h      # Server class, config, command queue
native/src/platform/HttpServer.cpp    # Implementation
native/include/httplib.h              # cpp-httplib (vendored header)
```

**Modified files**:
```
native/CMakeLists.txt                 # Add HttpServer.cpp to dc3-native sources
src/App.cpp                           # Process command queue in main loop
native/src/telemetry/GameplayTelemetry.h/cpp  # Struct-based capture (not just fprintf)
```

**Build flag**: `#ifdef DC3_HTTP_SERVER` guards, enabled via cmake option (default ON for desktop, OFF for Emscripten).

**Estimated sizes**: Phase 1: ~500 LOC. Phase 2: ~300 LOC. Phase 3: ~500 LOC. Phase 4: ~400 LOC. Total ~1700 LOC for phases 1-4.

---

## Library API Patterns (cpp-httplib)

Reference for implementation. Library source at `../dc3-decomp-deps/cpp-httplib/`.

### Handler Registration
```cpp
svr.Get("/api/health", [](const httplib::Request &req, httplib::Response &res) {
    res.set_content(R"({"ok":true,"data":{"status":"ok"}})", "application/json");
});
svr.Post("/api/dta/eval", [](const httplib::Request &req, httplib::Response &res) { ... });
svr.Put("/api/settings", [](const httplib::Request &req, httplib::Response &res) { ... });
```

### Path Parameters & Query Strings
```cpp
// Named params — /api/objects/:path
svr.Get("/api/objects/:path", [](const auto &req, auto &res) {
    auto path = req.path_params.at("path");
});
// Regex captures — /api/screen/wait/(.+)
svr.Get(R"(/api/screen/wait/(.+))", [](const auto &req, auto &res) {
    auto screen = req.matches[1].str();
});
// Query strings — /api/memory/object/foo?offset=0&size=256
auto offset = req.get_param_value("offset");
```

### Binary Responses (screenshots)
```cpp
// Return PNG bytes directly — no temp file needed
res.set_content(reinterpret_cast<const char*>(png_data.data()), png_data.size(), "image/png");
```

### Multipart Upload (DTA files)
```cpp
svr.Post("/api/dta/upload", [](const auto &req, auto &res) {
    if (req.form.has_file("file")) {
        const auto &file = req.form.get_file("file");
        auto path = req.form.get_field("path");
        // file.content has the bytes, file.filename has the name
    }
});
```

### CORS Middleware
```cpp
svr.set_post_routing_handler([](const auto &req, auto &res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
});
```

### Exception Safety
```cpp
svr.set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
    try { std::rethrow_exception(ep); }
    catch (std::exception &e) {
        res.set_content(R"({"ok":false,"error":")" + std::string(e.what()) + R"("})", "application/json");
    }
    res.status = 500;
});
```

### WebSocket (Phase 6 events)
```cpp
svr.WebSocket("/api/events", [&](const httplib::Request &req, httplib::ws::WebSocket &ws) {
    // Runs in dedicated thread per connection. Auto ping/pong (30s default).
    std::string msg;
    while (ws.read(msg)) {       // Blocks until message or close
        ws.send("echo: " + msg); // Text message
    }
    // ws.read() returns Fail(0) when connection closes
});
```

### SSE Push (alternative to WebSocket for Phase 6)
```cpp
svr.Get("/api/events", [&](const auto &req, auto &res) {
    res.set_chunked_content_provider("text/event-stream",
        [&](size_t offset, httplib::DataSink &sink) {
            // Block on condition variable, write event, return true for more
            Event event;
            if (!dispatcher.wait_event(&event, 5000)) return false;
            auto msg = "event: " + event.type + "\ndata: " + event.json + "\n\n";
            return sink.write(msg.data(), msg.size());
        });
});
```

### Long-Poll (Phase 4 screen/frame waits)
```cpp
svr.Get(R"(/api/screen/wait/(.+))", [&](const auto &req, auto &res) {
    auto target = req.matches[1].str();
    std::unique_lock<std::mutex> lk(mtx);
    bool ok = cv.wait_for(lk, std::chrono::seconds(60),
        [&] { return current_screen == target; });
    if (ok) res.set_content(R"({"ok":true})", "application/json");
    else { res.status = 408; res.set_content(R"({"ok":false,"error":"timeout"})", "application/json"); }
});
```

### Server Lifecycle
```cpp
// Start in background thread
std::thread http_thread([&svr] { svr.listen("0.0.0.0", 9090); });
// Stop from main thread (blocks until all handlers finish)
svr.stop();
http_thread.join();
```

---

## Integration Points

### Main Loop Hook (App.cpp)
Command queue processing goes in `RunWithoutDebugging()` after `GameplayTelemetry::Sample(frameCount)` (line ~1210), before `TheRnd.BeginDrawing()`. This runs every frame, after all game logic polls.

### DTA Evaluation Path
`DataReadString(text)` → `BufStream` → `DataReadStream()` → `DataArray*` → `Evaluate()` → `DataNode` result. Thread-safe via `gDataReadCrit`. Parse on main thread (command queue), not HTTP thread.

### Screenshot Capture
`WritePNG()` in `Screenshot.cpp` uses stb_image_write. Currently writes to file path. For HTTP: add `WritePNGToMemory()` variant using `stbi_write_png_to_func()` callback to append to a `std::vector<uint8_t>`. Queue capture request to main thread (needs framebuffer access after `EndDrawing()`).

### Input Injection
`JoypadPoll()` in `Joypad_Native.cpp` — after `GetScriptedButtons()` (line ~458), OR in HTTP-queued button bits: `newButtons |= httpQueuedButtons;`. Button names map via `ParseButtonName()` (confirm→kPad_X, cancel→kPad_Circle, etc.).

### CMake Guard
```cmake
$<$<NOT:$<BOOL:${EMSCRIPTEN}>>:src/platform/HttpServer.cpp>
```
Plus `target_compile_definitions(dc3-native PRIVATE $<$<NOT:$<BOOL:${EMSCRIPTEN}>>:DC3_HTTP_SERVER=1>)`.
