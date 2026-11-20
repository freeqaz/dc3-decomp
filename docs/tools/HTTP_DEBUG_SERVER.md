# HTTP Debug Server

Embedded HTTP server for interacting with a running DC3 native engine instance. Query state, execute DTA commands, take screenshots, and tune settings — all via `curl` or any HTTP client.

Desktop-only (not available in Emscripten/web builds).

**Loopback only:** the server binds `127.0.0.1` (it listened on `0.0.0.0`
until 2026-11-14). It evaluates arbitrary DTA, so it never answers another
machine; drive an engine on another box through a tunnel,
`ssh -L 9090:127.0.0.1:9090 <box>`.

**On by default** under the dev profile (`DC3_PROFILE`, the default; see
[debugging/native.md "Profiles"](../debugging/native.md#profiles-dc3_profile)).
`DC3_HTTP=0` or `DC3_PROFILE=release` turns it off.

## Quick Start

```bash
# Dev profile: the server is already on. With DC3_HTTP_PORT unset a window
# takes 9090 if it is free, else a kernel-assigned port (it says which);
# a headless run always takes a kernel-assigned port. Either way the port is
# printed on stdout as DC3_HTTP_PORT=<n>.
./dc3-native

# Under release, or to be explicit
DC3_HTTP=1 ./dc3-native

# A fixed port (strict: taken = exit 98)
DC3_HTTP=1 DC3_HTTP_PORT=8080 ./dc3-native

# Kernel-assigned free port; the chosen one is printed on stdout as
# DC3_HTTP_PORT=<n> (the race-free choice for scripts and parallel tests)
DC3_HTTP=1 DC3_HTTP_PORT=0 ./dc3-native | grep -m1 '^DC3_HTTP_PORT='

# ...and also written to a file once bound (temp file + rename, so a poller
# never reads half of it); what the test harnesses use
DC3_HTTP=1 DC3_HTTP_PORT=0 DC3_HTTP_PORT_FILE=/tmp/run/http_port ./dc3-native
```

An explicit port that cannot be bound is a named exit, **rc 98**:
`DC3 Native: FATAL - HTTP debug server: port N already in use (DC3_HTTP_PORT=N)`
and `DC3_EXIT: how=exit_runtime detail=http_port_in_use code=98`
(`http_port_bind_failed` for any other bind error). Before 2026-11-13 it was an
`abort()` (rc 134), and a port held by **another dc3-native** did not fail at
all: cpp-httplib binds with `SO_REUSEPORT` by default, so both engines listened
and the kernel split the requests between them (measured: `/api/health` on 9290
answered alternately by two engines). The server now sets `SO_REUSEADDR` only.

A harness must not pick a "free" port itself and pass it in: any process can
take it before dc3-native binds, seconds later. Use `DC3_HTTP_PORT=0` with
`DC3_HTTP_PORT_FILE` (helpers: `scripts/engine_http_port.py`,
`native/tests/engine_http_port.h`; the synthetic sensor's
`--perform-port-file`). See [native/TESTING.md](../native/TESTING.md#running-alongside-other-gates).

## Endpoints

### Health

```bash
curl localhost:9090/api/health
# {"ok":true,"data":{"status":"ok","frame":1234,"uptime_s":5.2,"pid":4321,"output_w":1920,"output_h":1080,"thermal_level":-1,"thermal_row_cap":0}}
```

Poll this to know when the engine is ready. `uptime_s` is wall-clock seconds since the server started (`steady_clock`; it used to read `TaskMgr kRealTime`, which the game sets to song time). `pid` is the engine's process id,
so a driver can check that the engine answering is the one it launched. Scripts can wait for a successful response before interacting.
`output_w`/`output_h` are the size the frame leaves the renderer at (a screenshot's size); `thermal_level` (-1 unknown, 0 nominal .. 3 critical) and `thermal_row_cap` (0 = none) are the thermal governor's, see [Thermal governor](#thermal-governor).

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

Captures the current framebuffer as PNG. The capture happens after `EndDrawing()` on the main thread, so the image reflects the fully rendered frame — and the request blocks until one is presented, which makes it usable as an "advance one frame" primitive.

**Windowed too** (PLAN H-13): the frame is read from the swapchain between the submit and the present, through the engine's `WgpuRnd::SetFrameReadyCallback` / `GpuDevice::ReadbackFrame` (BGRA swizzled to RGBA); before that a windowed engine answered 500. With no GPU device the request is answered after `EndDrawing` as before.

**Works headless with no display and no GPU.** Verified live under `MILO_HEADLESS=1` on a box with no framebuffer: 1280x720 RGBA PNG, ~750 KB per request. No window, X display or GPU device is required, so sandboxed agents can verify rendering changes visually (see `docs/tools/STATE_DIFF.md`, sweep mode).

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
# {"ok":true,"data":{"type":"int","typeId":0,"value":3}}

# Set and use variables
curl -X POST localhost:9090/api/dta/eval -d '{set $x 10}{+ $x 5}'
# {"ok":true,"data":{"type":"int","typeId":0,"value":15}}

# Strings come back verbatim (no {symbol ...} workaround needed)
curl -X POST localhost:9090/api/dta/eval -d '"hello world"'
# {"ok":true,"data":{"type":"string","typeId":18,"encoding":"utf8",
#                   "value":"hello world","bytes":11}}

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

**Important**: Use `{ui goto_screen <name>}` for navigation, not `{flow_mgr goto_screen ...}` — `flow_mgr` is not a registered object in the native port. Jumping to screens that require prior setup (e.g. `loading_screen` without a song selected) will crash the engine — use the `MILO_INPUT_SCRIPT` system for safe menu navigation flows.

#### Result types

Every `DataType` serializes to an object carrying a short `type` name *and* the
numeric `typeId`, so a client can switch on either. Nothing falls through to a
bare numeric type any more.

| `type` | `typeId` | `value` |
|--------|----------|---------|
| `int` | 0 | JSON number |
| `float` | 1 | JSON number; non-finite floats give `null` plus `"special":"nan"\|"inf"\|"-inf"` (JSON has no NaN literal) |
| `symbol` | 5 | string |
| `string` | 18 | string or base64 — see escaping below; also `encoding` and `bytes` |
| `object` | 4 | object name, plus `class`; `null` for a null object reference |
| `array` / `command` / `property` | 16 / 17 / 19 | JSON array of nested result objects, plus `size`; capped at 8 levels deep and 256 elements per level (`"truncated":true` when clipped) |
| `glob` | 20 | always base64, plus `bytes` |
| `var` | 2 | the dereferenced value as a nested result object, plus `name` |
| `func` | 3 | `null` (a raw function pointer is meaningless to a client) |
| `unhandled` | 6 | `null` |
| `ifdef` / `else` / `endif` / `define` / `include` / `merge` / `ifndef` / `autorun` / `undef` | 7,8,9,32,33,34,35,36,37 | `null` — parse directives, never real results |

**String/glob escaping.** Results are consumed by Python clients, so the body is
always valid UTF-8 and always `json.loads()`-able:

* valid UTF-8 payloads are sent as text with `"encoding":"utf8"` — quotes,
  backslashes, newlines, tabs and every other control byte (including NUL, as
  `\u0000`) are escaped, so they round-trip exactly;
* anything that is not valid UTF-8 (Latin-1 text, binary junk) is sent as
  `"encoding":"base64"` — `base64.b64decode(value)` gives the exact bytes back.
  Raw invalid bytes are never emitted.

`bytes` is the exact payload length in bytes (the trailing NUL of a DTA string
is not counted). Other endpoints' strings (object names, class names, …) use the
same escaper, but a non-UTF-8 name is escaped lossily as Latin-1 rather than
base64.

Functions that return temporary strings (like `sprint`) may still return a stale
engine buffer — that is a DTA-level quirk, not a serialization one; use
`sprintf` with variables instead.

#### Size limits

| Limit | Value | Behaviour when exceeded |
|-------|-------|-------------------------|
| Request body (`DtaEval::kMaxScriptBytes`) | **16384** | `413` + `{"ok":false,"error":"Request body too large (N bytes, max 16384 — RB3E_DTA_SCRIPT_MAX)"}` |
| Result payload (`DtaEval::kMaxResultBytes`) | **32768** | `413` + `{"ok":false,"error":"DTA result too large (…max 32768 — RB3E_DTA_OUTPUT_MAX)…"}` |
| Nesting depth (`DtaEval::kMaxNestingDepth`) | **256** | `400` + explicit error |
| Transport ceiling (`kMaxHttpBodyBytes`, HttpServer.cpp) | 1 MiB | bare `413` from cpp-httplib before the body is buffered |

The two DTA caps deliberately equal the RB3Enhanced console channel's
`RB3E_DTA_SCRIPT_MAX` / `RB3E_DTA_OUTPUT_MAX` (see `tools/console/dc3_eval.py`),
so a script that fits over the console also fits over localhost. Neither cap
truncates silently — you always get an explicit error naming the limit. (The
console rejects a body of *exactly* 16384; the HTTP endpoint accepts it and
rejects 16385.)

Before this was fixed the endpoint inherited cpp-httplib's
`CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH` default of 8192 — `curl -d`
sends `application/x-www-form-urlencoded`, so any body over 8 KiB was rejected
with a bodyless 413, *tighter* than the console channel.

#### Crash recovery

Arbitrary script can segfault the engine, so eval runs under a
`sigsetjmp`/`siglongjmp` net for SIGSEGV/SIGBUS/SIGFPE/SIGABRT and returns
`{"ok":false,"error":"DTA eval crashed: …"}` instead of taking the process down.

`siglongjmp` does **not** run C++ destructors, so every scope guard between the
crash site and the recovery point is skipped — including `DataCallStackFrame`,
which is what pops `gCallStackPtr`. Each crashed eval therefore used to burn one
or more of the 100 `HANDLE_STACK_SIZE` slots permanently, and after enough
failures the engine died on the **main thread** inside unrelated script with a
misleading `MILO_ASSERT(gCallStackPtr - gCallStack < HANDLE_STACK_SIZE)`.

`DtaEval::ScriptStateGuard` (native/src/platform/DtaEvalSupport.h) snapshots the
interpreter globals — `gCallStackPtr`, `gPreExecuteFunc`/`gPreExecuteLevel`,
`gDataThis`, `gDataDir`, the `$`-var stack pointer, `DataArray::gFile`, and the
per-thread heap/temp-allocation stack — and restores them from its destructor,
which runs on every path out of the handler including the post-`longjmp` return.
When it repairs something the response says so and a line is logged:

```
[HttpServer] DTA eval recovered from SIGABRT (abort) for expression: {if 1 {fail "boom"}}
[HttpServer] repaired leaked DTA state: call stack depth 2 -> 0
```

Two pieces of state still cannot be repaired because they are private to
`src/` (the PPC decomp, which this native-only code must not touch):
`Debug::mTry` (a crash inside a `MILO_TRY` leaves the try-depth incremented) and
`gDataArrayConditional` (`#ifdef` nesting state, file-static in `DataArray.cpp`).
Both are harmless in practice; fixing them needs an `HX_NATIVE`-gated accessor in
`src/` with PPC codegen verified unmoved by `run_objdiff` (equal-instruction counts
before and after, not a rendered percentage).

> **Do not read "verified byte-identical" into that** — this line used to say it, and
> as an instruction it was unrunnable. A bare `sha256sum` of a rebuilt `.obj` is this
> project's canonical test-that-cannot-fail: pre-`ee8902a22` every recompile changed
> every object's bytes, and a per-target `ninja <one>.obj` still does today, because
> the normalizing pass runs on the `post-compile` edge. If you genuinely need an
> object-byte neutrality proof, compare after a full `ninja`, or zero both clock
> fields with `scripts/obj_build_metadata_patcher.py`'s `plan()`/`normalize()`, and
> keep a sabotage control that is supposed to move the hash. See `CLAUDE.md`.

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

Presses are pad-0 `ButtonDownMsg`s (the matching `ButtonUpMsg` follows one frame
later), broadcast by `HttpServer::DispatchInjectedButtons` from
`ProcessCommands`. They do not enter `JoypadData::mButtons`, so code that samples
held buttons instead of listening for messages does not see them. Until
2026-09-30 (`native-partyplay`) both endpoints answered `{"ok":true}` and did
nothing: the only drain was the engine's `JoypadPoll`, which
`libmilo-engine.a` compiles without `DC3_HTTP_SERVER`. Guarded by
`HttpInputTest.PressReachesTheUIAsAPad0Button` (`native/tests/test_native_partyplay.cpp`).

```bash
# Press a button this frame
curl -X POST localhost:9090/api/input/press -d '{"button":"confirm"}'

# The same, standing for a REAL pad's button. Without "device":"pad" an
# injected press is nobody's device, and the song browser's hint bar counts it
# with the keyboard (?device=pad works too)
curl -X POST localhost:9090/api/input/press -d '{"button":"down","device":"pad"}'

# Queue a button sequence (delays are cumulative frames between events)
curl -X POST localhost:9090/api/input/sequence -d '[
  {"button":"dpad_down","delay":15},
  {"button":"dpad_down","delay":15},
  {"button":"confirm","delay":30}
]'

# Hold each press down for 2 frames instead of 1 (the playtest kit's driver
# does, so no press is a one-frame blip)
curl -X POST localhost:9090/api/input/sequence -d '[{"button":"confirm","delay":3,"hold":2}]'
```

`hold` (frames, default 1) is how long a sequence press stays down: its
`ButtonUpMsg` comes `hold` frames after its `ButtonDownMsg`; 1 is the next
frame, the behaviour before `hold` existed. A button pressed again while
still held is released and pressed again on that frame. The per-frame rule is
`InjectedPadHold` (`HttpServer.h`), tested by `HttpInjectedHoldTest`.

Button names: `confirm`/`a`, `cancel`/`b`, `start`, `select`/`back`/`option`, `up`/`dpad_up`, `down`/`dpad_down`, `left`/`dpad_left`, `right`/`dpad_right`, `l1`/`lb`, `r1`/`rb`, `l2`/`lt`, `r2`/`rt`, `x`, `y`, `l3`/`ls`, `r3`/`rs`.

Works in both windowed and headless modes. HTTP-injected buttons are OR'd with keyboard/gamepad/script input.

### Text input and the song browser

```bash
# Type text: every character is queued on the engine's text queue
# (platform/TextInput_Native.h), as if typed. Queued whether or not anything
# is capturing the keyboard; never presses a pad button.
curl -X POST localhost:9090/api/input/text -d '{"text":"crystal"}'
# {"ok":true,"data":{"typed":7,"pending":7}}

# An edit key: backspace, delete, left, right, home, end
curl -X POST localhost:9090/api/input/text -d '{"key":"backspace"}'
# Both in one request (text first), or as query parameters:
curl -X POST 'localhost:9090/api/input/text?text=ab&key=home'
# An unknown key name is refused: HTTP 400

# The natively drawn song browser on song select (DC3_SONG_BROWSER)
curl localhost:9090/api/songbrowser/state
# {"ok":true,"data":{"available":true,"mode":"on","on_song_select":true,
#  "eligible":true,"shown":true,"why":"DC3_SONG_BROWSER=1",
#  "hands_steering":false,"hand_in_pose":false,"tracked_bodies":0,"switches":1,
#  "controller_mode":true,"text_capture":true,"leaving":false,
#  "engine_text_capture":true,"hints":"keyboard","hint_accept":"ENTER",
#  "pad_presses":0,"screen_builds":1,"preview_song":"aint2proud2beg",
#  "preview_changes":2,"preview_starts":2,"preview_hold":false,
#  "commands":["random_song","playlists"],
#  "command_rows":["random_song","playlists"],
#  "songs":45,"classic_song":"aint2proud2beg","focus":"list",
#  "selected_song":"aint2proud2beg",
#  "selected_cover_path":"songs/aint2proud2beg/aint2proud2beg_keep.png",
#  "selected_cover":"ready","list_song":"aint2proud2beg",
#  "list_command":"","selected_command":"",
#  "recent_song":"boomboompow","query":"","sort":"title","rows":53,"list_index":2,
#  "recents_shown":false,"highlight_rect":[56.0,187.0,804.0,51.0],"frames":642,
#  "drew":true,"glyphs":436,"placeholder_glyphs":0,"quads":516,"draw_calls":52,
#  "images_missing":0,"images_ready":9,"images_failed":0,"build_us":36,
#  "replay_us":38}}
# Before song select has been entered in this run:
# {"ok":true,"data":{"available":true,"on_song_select":false,"eligible":false,
#  "shown":false,"why":"not on song select"}}

# The song browser's two commands: Random Song and Playlists. Runs what the
# screen's own command rows run (songbrowser_ui::Delegate::onCommand): the
# classic list's row of that name is selected, so the result is the classic
# row's. ?name= works too
curl -X POST localhost:9090/api/songbrowser/command -d '{"name":"random_song"}'
curl -X POST localhost:9090/api/songbrowser/command -d '{"name":"playlists"}'
# {"ok":true,"data":{"command":"playlists"}}
# HTTP 409 with the reason when it cannot run: the browser is not shown, a
# pick or a Back is in flight, or this visit's classic list has no such row
# (Rehearse has neither, Dance Battle only random_song):
# {"ok":false,"error":"the classic list has no playlists row in this mode"}
```

`typed` is the number of characters queued by this request and `pending` the
queue's length afterwards; the text reaches a screen on its next frame.

`/api/songbrowser/state` always answers. What its fields mean:

| Field | |
|---|---|
| `available` | `false` in a binary without the browser (`milo-viewer`); then only `shown` and `why` follow |
| `mode` | `auto`, `on` or `off`: what `DC3_SONG_BROWSER`, `DC3_CLASSIC_SONG_SELECT` and the defaults resolved to |
| `on_song_select` | `song_select_panel` is entered |
| `eligible` | this visit can show the browser (the mode is not `off`, it is a pick-one-song visit, the catalog and the font are there) |
| `shown` | the browser is what is drawn and what takes the pad right now |
| `why` | the reason for `shown`, in words: `the pad or the keyboard has the menu (no hand is steering)`, `DC3_SONG_BROWSER=1`, `DC3_SONG_BROWSER=0`, `DC3_CLASSIC_SONG_SELECT=1`, `MILO_INPUT_SCRIPT is set ...`, `a hand is steering (held where the list takes it; controller mode is off)`, `Start handed the menu to the hands`, `song select was reached by hand ...`, `an overlay or a dialog is up`, `not on song select` |
| `hands_steering` | the `auto` rule's latch: the classic list is up for somebody's hands |
| `hand_in_pose`, `tracked_bodies` | right now: a tracked skeleton's hand is where the song list or the help bar's list takes it (the image's `IsValidSwipePosition`); how many skeletons are tracked |
| `switches` | times the browser was shown or hidden in this run (a flapping rule shows here) |
| `controller_mode` | the game's controller mode; the browser keeps it on while it is shown |
| `text_capture`, `engine_text_capture` | the browser holds the engine's text capture; the engine's capture is on at all (it stays on under the classic list in `auto`, so a typed character can bring the browser back) |
| `leaving` | a pick, a Back or a command is in flight; input is dropped until the screen changes |
| `hints`, `hint_accept` | `pad` or `keyboard`: the device last used; the name the hint bar shows for Accept (`A` or `ENTER`), read back from the screen (`HintGlyphsShown()`) |
| `pad_presses` | presses counted as a real pad's in this run |
| `screen_builds` | times the screen was built in this run (the hint names change without one) |
| `preview_song`, `preview_changes` | the song the game's preview was last asked for; how many times that changed while the browser was shown |
| `preview_starts`, `preview_hold` | preview streams prepared in this run (a start, as opposed to a request); the preview is waiting for the selection to rest |
| `commands` | the commands this visit's classic list has a row for: `random_song`, `playlists` |
| `command_rows` | the command rows the screen was built with; the same list as `commands` except for the one frame in which the classic list changed |
| `songs` | songs in the browser's catalog |
| `classic_song` | the song the classic list's highlight was last put on |
| `focus` | `search`, `tabs`, `recents` or `list` |
| `selected_song` | the song the detail pane shows: what Accept would pick. Empty while the pane shows a command or the search help |
| `selected_cover_path`, `selected_cover` | that song's cover: the path it is read from (a downloaded song's is on its DLC root's drive, `dlcroot0:/<package>/songs/...`), and how far its load has come: `not asked for`, `queued`, `loading`, `ready`, `failed` (the logo is drawn in its place) or `evicted`. Absent while the pane shows no song |
| `list_song`, `recent_song` | the list's selected song and the shelf's selected card |
| `list_command`, `selected_command` | the command row the list stands on, and the command the detail pane shows (`random_song`, `playlists`, or empty): what Accept would run |
| `query`, `sort`, `rows`, `list_index`, `recents_shown` | the search text; the arrangement (`title`, `artist`, `difficulty`, `source`, `decade`); rows in the list, headings included; the selected row's index among them; whether the shelf is unfolded |
| `highlight_rect` | the list's selection bar, `x,y,width,height` in the UI's 1280x720 logical units (multiply by output rows / 720 for screenshot pixels) |
| `frames`, `drew`, `glyphs`, `placeholder_glyphs`, `quads`, `draw_calls`, `images_missing` | frames drawn so far and the last frame's numbers; `placeholder_glyphs` counts characters drawn as the missing-glyph box. `images_missing` counts pictures drawn with no texture at all; a cover whose load failed is drawn as the logo and is **not** counted here |
| `images_ready`, `images_failed` | covers held as textures now, and cover loads that have failed in this run (each of those draws the logo). 0 failed is the reading that says every cover asked for was found; these two count in a run that does not render too |
| `build_us`, `replay_us` | the last frame's cost in microseconds: building the display list, and replaying it into the canvas. Both 0 in a run that does not render |

The fields from `focus` to `highlight_rect` are present only while a visit has
a screen. In a run that does not render (`MILO_NORENDER=1`) `drew` is false
and `highlight_rect` and the frame's numbers are 0: nothing is laid out, but
focus, the selected song, the query and the sort are all live. Tested by `SongBrowserInSongSelectTest`, `SongBrowserSwitchFlowTest`,
`SongBrowserHandsTest` and `NativeUiHostGpuTest.SongBrowserRunsInTheEngine`.

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

### Frame pacing

A headless engine runs as fast as it can (1500–3700 frames/s without a GPU)
while advancing UI time a fixed 1/30 s per frame, so a client that must
answer every frame (the synthetic performer following `/api/frame/wait`)
falls behind. `DC3_HTTP_PACE_FPS=<n>` caps the frame rate (the sleep runs
right after each frame is announced, giving the client that window); unset is
unpaced.

```bash
curl localhost:9090/api/pace                    # {"ok":true,"data":{"fps":120.000}}
curl -X PUT 'localhost:9090/api/pace?fps=0'     # unpaced (e.g. for a song nobody gestures in)
```

### Thermal governor

The engine lowers the output's row cap in steps (1080, 900, 720 rows) when the
machine reports thermal pressure and the game is under its frame rate, and
raises it again once it has cooled (PLAN H-128; the rule and its constants are
in the engine's `src/gfx/ThermalGovernor.h`, the knobs in
`docs/debugging/native.md`). Only macOS reports a level, so anywhere else a
test injects one:

```bash
curl localhost:9090/api/thermal
# {"ok":true,"data":{"enabled":true,"level":2,"level_name":"serious","injected":true,
#   "target_fps":60.00,"window_fps":20.00,"late_windows":0,"row_cap":900,"base_rows":1080,
#   "output_w":1600,"output_h":900,"steps_down":1,"steps_up":0,"cool_s":0.0}}
curl -X PUT 'localhost:9090/api/thermal?level=2'   # inject: 0 nominal, 1 fair, 2 serious, 3 critical
curl -X PUT 'localhost:9090/api/thermal?level=os'  # withdraw it: the OS's own level again
```

`enabled` is `MILO_THERMAL_GOVERNOR`; `target_fps` the rate the game is meant
to hold (its frame cap, else 60, never above the display's); `window_fps` the
last judged one-second window and `late_windows` how many in a row were late;
`row_cap` the governor's cap (0 = none) over an output of `base_rows` without
it; `cool_s` how long the level has been nominal. Until a level has been seen
the governor has not run, and `base_rows`/`window_fps` read 0. A step down
needs late frames as well as a level: hold the frame rate under the target
with `PUT /api/pace?fps=20` (a headless run is otherwise far above 60). That
is what `NativeExitTest.ThermalPressureWithLateFramesLowersTheOutputAndCoolingRestoresIt`
does.

### GPU time per render pass

With `MILO_GPU_TIMERS=1` the engine times every render pass with WebGPU
timestamp queries (`docs/debugging/native.md`, PLAN H-96); `/api/gputime`
returns the latest ~1 s window. Without the env var, or on an adapter or build
without `timestamp-query`, it answers `"active":false` and `unavailable` says
why.

```bash
curl localhost:9090/api/gputime
# {"ok":true,"data":{"requested":true,"active":true,"quantizationOff":1,"window":42,
#   "windowSeconds":1.004,"frames":137,"skipped":0,"gpuSumMs":0.5780,"gpuSumMaxMs":0.5940,
#   "gpuSpanMs":0.6970,"gpuSpanMaxMs":0.7210,"unavailable":"",
#   "passes":[{"name":"MainPassResume","ms":0.3492,"maxMs":0.3626,"n":5.80}, ...]}}
```

`ms` is the mean per timed frame (passes of one name in a frame add up), `n`
the passes of that name per frame; `gpuSumMs` sums every pass, `gpuSpanMs` is
first begin to last end (it includes idle gaps between submits).
`scripts/playtest/gpu_pass_ab.py` drives a song with this endpoint and
`/api/dta/eval` to A/B one switch.

### Songs

The song browser's data
([SONG_BROWSER_DATA.md](../native/reference/SONG_BROWSER_DATA.md)): the songs
song select lists, a search over them, or one arrangement. Built from the game
on each request; read-only. Answers `503` with `"ok":false` while the game is
still starting (no unlock table yet) and for an unknown `sort`.

```bash
# The catalog: every song, the play history, the recents, the section names
curl -s localhost:9090/api/songs
# {"ok":true,"data":{"count":45,"songs":[{"id":7035,"shortName":"12step",
#   "title":"1, 2 Step","artist":"Ciara ft. Missy Elliott","album":"Goodies",
#   "year":2004,"bpm":113,"lengthMs":142309,"rank":187,"difficultyTier":1,
#   "origin":"ham3","source":"ham3","onDisc":true,"isCover":false,
#   "albumArtPath":"songs/12step/12step_keep.png","bestStars":0,"bestScore":0,
#   "played":false,"locked":false,"recentRank":-1}, ...],
#   "history":[],"recents":[],
#   "labels":{"ham3":"Dance Central 3","song_tier_0":"Warmup", ...}}}

# A search (any ?q= at all, an empty one included)
curl -s 'localhost:9090/api/songs?q=will+i+am'
# {"ok":true,"data":{"searchMicros":24,"query":"will i am","searching":true,
#   "words":["will","i","am"],"count":1,"rows":[{"shortName":"omg","title":"OMG",
#   "artist":"Usher ft. will.i.am","album":"Raymond V. Raymond",
#   "match":"artist_word","field":"artist",
#   "ranges":[{"field":"artist","begin":10,"length":9,"text":"will.i.am"}]}]}}

# An arrangement: sections and the rows in order
curl -s 'localhost:9090/api/songs?sort=artist&min=4'
# {"ok":true,"data":{"arrangement":"artist","count":45,
#   "sections":[{"token":"123 - A","title":"123 - A","firstRow":0,"count":5,
#   "recents":false}, ...],"rows":["iamthebest","indaclub", ...]}}
```

| Parameter | Meaning |
|-----------|---------|
| `q=TEXT` | Search. `searching` is false when the text has nothing to search for. `ranges` are bytes of the UTF-8 string; `text` is that slice. `searchMicros` is the search alone |
| `limit=N` | At most N search rows |
| `sort=NAME` | `title`, `artist`, `difficulty`, `source`, `decade`, `bpm` |
| `min=N` | With `sort=title` or `artist`: join letters until a section has N songs (4 is today's artist sort) |
| `recents=N` | Rows of Recently Played: the catalog's `recents`, and the first section of an arrangement. Default 6; 0 for none |
| `locked=1` | Also list songs that are only locked, with `"locked":true` |

`q` wins over `sort`. The examples are a disc-only run (`DC3_DLC=0`, 45 songs).

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
| `native/src/songbrowser/SongCatalogSource_Native.cpp` | `GET /api/songs`: the catalog from the game, and the endpoint's body |
| `native/src/platform/SongBrowserPanel_Native.cpp` | `GET /api/songbrowser/state`: the body (`StateJson`); `POST /api/songbrowser/command`: `RunCommandNamed` |
| `native/include/httplib.h` | Vendored cpp-httplib (MIT, header-only) |

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `DC3_HTTP` | `0` | Set to `1` to enable the server |
| `DC3_HTTP_PORT` | `9090` | Port to listen on. `0` or `auto` = kernel-assigned, reported on stdout as `DC3_HTTP_PORT=<n>` |
| `DC3_FAST_BOOT` | `0` | Set to `1` to skip boot screens in 10 frames instead of ~360 |
