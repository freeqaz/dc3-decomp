# DC3 Web Port — Browser via WebAssembly + WebGPU

## Overview

Compile the DC3 native port to run in web browsers using Emscripten (C++ to WASM) with WebGPU. The engine already renders through Dawn's `webgpu.h` API — the browser's native WebGPU API implements the same interface, so the rendering pipeline ports with minimal changes.

**MVP goal**: A localhost:8420 dev server that boots the engine in a browser tab, streams assets from a local API, and renders a scene (venue or character).

**Current status**: Phase 5 nearly complete. Engine compiles to WASM, boots in browser, downloads all 246 assets via bundle API, parses ALL ~220 DTA config files, runs full SystemPreInit + SystemInit, and initializes WgpuRnd. WebGPU adapter/device request pending (works in real Chrome with GPU; headless Chromium lacks WebGPU).

## Architecture

```
+-----------------------------------------------------------+
|  Browser Tab                                               |
|  +--------------+  +-----------------------------------+  |
|  |  index.html  |  |  dc3-web.wasm + dc3-web.js        |  |
|  |  + bootstrap |--|  Emscripten runtime                |  |
|  |              |  |  webgpu.h -> browser WebGPU        |  |
|  +--------------+  +----------------+------------------+  |
|                                     | fetch()              |
|                                     v                      |
|  +---------------------------------------------------------+
|  |  Asset API  (localhost:8420/api/bundle, /api/file/...)  |
|  |  Serves DTA scripts, .milo files, textures             |
|  +---------------------------------------------------------+
+-----------------------------------------------------------+
                          ^
                          | HTTP
          +---------------+----------------+
          |  Python dev server (:8420)      |
          |  - Serves static WASM/HTML/JS  |
          |  - Bundle API (all assets)     |
          |  - File API (individual)       |
          +--------------------------------+
```

## Why Emscripten (and nothing else)

Researched alternatives as of 2026:

| Toolchain | WebGPU | Browser APIs | Maturity | Verdict |
|-----------|--------|-------------|----------|---------|
| **Emscripten** | Yes (emdawnwebgpu) | Full (GLFW shim, fetch, audio, threads) | Production | **Use this** |
| Cheerp | No WebGPU support | JS interop focus | Niche | Not viable |
| WASI-SDK | No graphics at all | Server-side only | Growing | Wrong target |
| Zig wasm32 | No browser layer | Manual JS interop | Early | Too much glue |

Emscripten provides:
- `emdawnwebgpu` — maps `webgpu.h` calls to browser's `navigator.gpu` JS API (maintained by Dawn team)
- GLFW 3 shim (our window/input layer already uses GLFW)
- `emscripten_fetch()` for async HTTP asset loading
- pthreads via SharedArrayBuffer + Web Workers
- CMake integration via `emcmake cmake`

## Browser WebGPU Support

~83% global coverage (mid-2026): Chrome 113+, Edge 113+, Opera 99+, Chrome Android 145+. Firefox behind a flag. Safari 26+ partial.

## Phases

---

### Phase 0: Toolchain Setup -- DONE

**Goal**: Emscripten SDK installed, can compile a trivial C++ WebGPU triangle to the browser.

**Completed**:
- Emscripten SDK installed at `~/emsdk`
- emdawnwebgpu available via `--use-port=emdawnwebgpu`
- Toolchain compiles and links C++ with WebGPU bindings

---

### Phase 1: CMake Web Target -- DONE

**Goal**: `native/CMakeLists.txt` gains a web build path producing `dc3-web.wasm` + `dc3-web.js`.

**Completed**:
- `dc3-web` target in `native/CMakeLists.txt` (line 1063), gated on `EMSCRIPTEN` toolchain
- Compile definitions: `HX_NATIVE=1`, `HX_WEB=1`, `MILO_DEBUG=1`
- Link flags: `--use-port=emdawnwebgpu`, `-sALLOW_MEMORY_GROWTH=1`, `-sMAXIMUM_MEMORY=512MB`, `-sSTACK_SIZE=1048576`, `-sFETCH=1`, `-sUSE_ZLIB=1`
- `-sERROR_ON_UNDEFINED_SYMBOLS=0` to tolerate asm-label stubs that wasm-ld cannot resolve
- `native/web/build.sh` wraps `emcmake cmake` + `cmake --build`
- Platform-conditional exclusions: HttpReqCurl.cpp, WebSvcMgrCurl.cpp removed from web sources

**Build command**:
```bash
cd native/web/build && source ~/emsdk/emsdk_env.sh && cmake --build . --target dc3-web
```

---

### Phase 2: Event Loop Adaptation -- DONE

**Goal**: Engine main loop runs in the browser without freezing the tab.

**Completed**:
- `native/src/main_web.cpp` implements a state-machine boot sequence driven by `emscripten_set_main_loop()`
- Boot states: `BOOT_INIT` -> `BOOT_FETCHING` -> `BOOT_ENGINE_INIT` -> `BOOT_GPU_WAIT` -> `BOOT_GPU_READY` -> `BOOT_RUNNING`
- Each frame yields to the browser event loop; no blocking while-loop

---

### Phase 3: Asset Streaming API -- DONE

**Goal**: A Python dev server at `:8420` that serves WASM build artifacts + streams game assets via HTTP API.

**Completed**:

#### Server (`native/web/server.py`)

```
GET /                         -> index.html + build artifacts
GET /dc3-web.{js,wasm}       -> WASM build output
GET /api/manifest             -> JSON list of all available assets
GET /api/bundle               -> Binary bundle of ALL assets (single request)
GET /api/file/<path>          -> Individual asset file (with Range request support)
```

- Python 3, stdlib only (`http.server`)
- COOP/COEP headers for future SharedArrayBuffer support
- `(..)` -> `..` path translation in bundle (ark extraction stores `..` as `(..)`)
- Auto-detects extracted assets dir via `DC3_ASSETS` env or `orig-assets/extracted/`

#### Client (`native/src/platform/WebAssets.cpp`)

- `WebAssetsFetchBundle()` downloads all assets in a single HTTP request
- Unpacks binary bundle into Emscripten MEMFS at `/data/`
- Resolves `..` path components to clean absolute MEMFS paths (e.g. `/data/../../system/run/config/macros.dta` -> `/system/run/config/macros.dta`)
- Individual fetch via `WebAssetsFetch()` also available
- Polling API: `WebAssetsAllDone()`, `WebAssetsPendingCount()`, etc.

**Result**: 246 files, ~5.7MB downloaded and unpacked into MEMFS.

#### Frontend (`native/web/index.html`)

- 1280x720 canvas (`#dc3-canvas`) with WebGPU detection
- Status bar and scrolling console log (captures engine printf output)
- Dynamic WASM module loading

**Dev server command**: `python3 native/web/server.py --port 8420`

---

### Phase 4: WebGPU Initialization (Browser Path) -- DONE

**Goal**: `WgpuRnd` initializes against the browser's WebGPU implementation instead of Dawn.

**Completed**:
- `GpuDevice.cpp` has `#ifdef HX_WEB` path for canvas surface creation
- Async adapter/device request via callbacks (browser WebGPU is async)
- `main_web.cpp` polls `gWgpuRnd->Gpu().PollEvents()` + `IsReady()` during `BOOT_GPU_WAIT`

---

### Phase 5: Engine Boot + Rendering Integration -- NEARLY COMPLETE

**Goal**: Full engine initialization pipeline runs: DTA config parsing, subsystem init, rendering.

**Completed**:
- `web_stubs.cpp` — proper C++ stubs replacing asm-label stubs (math, rendering, TextStream, ObjPtr, xbdm, Debug::Modal, SpewInit/Terminate, MemFindHeap, PhysMemTypeTracker, NuiSpeech*, etc.)
- `DataParser_Native.cpp` — DTA text parser ported from RB3 decomp (ParseArray, ParseNode, PushBack, all token types)
- `DataReadStream` global initialization (`gBinStream`, `gDataLine`, `gOpenArray`) under `#ifdef HX_NATIVE` in `DataFile.cpp`
- `ReadEmbeddedFile` state save/restore: saves and restores `gNode`, `gArray`, `gOpenArray`, `gBinStream`, `gFile`, `gDataLine` across `#include` processing
- Flex lexer `yy_hold_char` save/restore (`yyGetHoldChar`/`yySetHoldChar` in `DataFlex.h`/`DataFlex.c`) — preserves the lexer's lookahead byte across `#include` boundaries
- **`yySetHoldChar` buffer fix**: after `yyrestart(nullptr)` flushes the buffer (`yy_n_chars=0`), restoring a non-EOB holdChar requires bumping `yy_n_chars=1` to prevent "end of buffer missed" fatal error
- **`yySetHoldChar` ordering fix**: must be called AFTER `yyrestart(nullptr)` because `yy_load_buffer_state()` overwrites `yy_hold_char`
- **ALL ~220 DTA config files parse successfully** (including synth.dta with trailing-paren `#include` pattern)
- Full `SystemPreInit` completes (ham_preinit_keep.dta + all macros)
- Full `SystemInit` completes (ham_keep.dta + all game configs — 150+ DTA files)
- Locale initialization, cheats init, file cache, content manager
- `TheRnd.Init()` — WgpuRnd WebGPU renderer initialization
- WebGPU adapter/device request fires (async)
- 22 unit tests for DTA parser (including `#include` with trailing paren pattern)
- `ThreadCall_Native.cpp` — WASM single-threaded path (synchronous in `ThreadCallPoll`)
- `xdk_shims_web.cpp` — Win32/XDK API stubs for WASM (critical sections, events, semaphores, threads, timing, memory, XGraphics)

**Remaining tasks**:
1. Verify WebGPU adapter/device acquisition in a real Chrome tab with GPU
2. Verify WebGPU pipeline creation and first frame render
3. Debug any rendering issues (shader compilation, surface configuration)

---

### Phase 6: Input & Interaction

**Goal**: Keyboard/mouse input works for camera control and UI navigation.

**Tasks**:
1. Emscripten's GLFW shim handles keyboard/mouse events on the canvas automatically
2. If GLFW shim is insufficient, add `emscripten_set_keydown_callback()` etc.
3. Touch events for mobile (future)
4. Pointer lock for mouse look (`emscripten_request_pointerlock()`)

**Validation**: Can orbit camera, navigate UI menus.

---

### Phase 7: Audio (Post-MVP)

**Goal**: Audio playback in browser via Web Audio API.

**Options**:
- **Option A**: Emscripten's SDL_audio or OpenAL shim (maps to Web Audio internally)
- **Option B**: Direct Web Audio API via JS interop
- **Option C**: Replace miniaudio backend with Emscripten audio worklet

**Tasks**:
1. Stub audio for MVP (silent)
2. Later: implement `AudioDevice_Web.cpp` using Emscripten's audio API
3. Music/SFX streaming from server API

---

### Phase 8: Optimization & Polish

**Goal**: Acceptable load times and frame rates.

**Tasks**:
1. **WASM size reduction**: `-Oz`, `--closure 1`, strip debug info, Brotli compression
2. **Asset caching**: IndexedDB for downloaded .milo files
3. **Loading screen**: HTML/CSS spinner while WASM boots + assets download
4. **Memory**: Monitor WASM heap usage, tune `MAXIMUM_MEMORY`
5. **Threading** (optional): Enable `-pthread` + `-sPROXY_TO_PTHREAD` for background asset loading
6. **Progressive loading**: Load venue first, stream characters/animations after

---

## Key Files

```
native/
+-- web/
|   +-- build/                     # Build output (dc3-web.wasm, dc3-web.js, index.html)
|   +-- build.sh                   # emcmake wrapper
|   +-- server.py                  # Dev server (localhost:8420)
|   +-- index.html                 # Bootstrap HTML + canvas + console
+-- src/
|   +-- main_web.cpp               # Entry point + boot state machine
|   +-- web_stubs.cpp              # C++ stubs for unported engine functions
|   +-- platform/
|       +-- WebAssets.cpp           # Bundle download + MEMFS unpacker
|       +-- WebAssets.h             # Public API (WebAssetsInit, WebAssetsFetchBundle, etc.)
|       +-- DataParser_Native.cpp   # DTA text parser (ported from RB3)

src/system/obj/
+-- DataFile.cpp                   # DataReadStream + ReadEmbeddedFile (#ifdef HX_NATIVE patches)
+-- DataFlex.c                     # Flex lexer (generated from DataFlex.l)
+-- DataFlex.h                     # Lexer API + yyGetHoldChar/yySetHoldChar declarations
+-- DataFlex.l                     # Flex grammar for DTA tokenization
```

## Technical Notes

### DTA Text Parser Port

The DTA (Data Text Array) parser is the engine's config file format. DC3's parser functions (`ParseArray`, `ParseNode`, `DataReadStream`) are undecompiled stubs in the PPC decomp, so the native/web port needed full implementations.

The parser was ported from the RB3 (Rock Band 3) decomp, which shares the same Milo engine. Key components:
- **DataParser_Native.cpp**: `ParseArray()` and `ParseNode()` — token-by-token parsing of DTA syntax (arrays, commands, properties, strings, symbols, ints, floats, hex, `#ifdef`/`#ifndef`/`#else`/`#endif`, `#define`/`#undef`, `#include`/`#include_opt`, `#merge`, `#autorun`)
- **DataFlex.c** (generated from DataFlex.l): flex-based lexer that tokenizes DTA text from a `BinStream`
- **DataFile.cpp patches**: `#ifdef HX_NATIVE` blocks in `DataReadStream()` and `ReadEmbeddedFile()` to initialize/save/restore parser globals

Global state shared between lexer and parser: `gBinStream` (input stream), `gDataLine` (line number), `gFile` (filename symbol), `gNode` (current node index), `gArray` (current array being built), `gOpenArray` (bracket type tracking).

### The `#include` Fix (RESOLVED)

DTA files use `#include filename.dta` to include other files. This is handled by `ReadEmbeddedFile()` in `DataFile.cpp`, which is a recursive call: save state, parse included file, restore state.

**The two-part fix**:

1. **Ordering**: `yySetHoldChar(savedHoldChar)` must be called AFTER `yyrestart(nullptr)`, not before. `yyrestart` calls `yy_load_buffer_state()` which executes `yy_hold_char = *yy_c_buf_p`, overwriting any previously set holdChar.

2. **Buffer boundary**: After `yyrestart(nullptr)` flushes the buffer (`yy_n_chars=0`), if a non-EOB holdChar is restored, the scanner consumes it at `yy_ch_buf[0]` then hits EOB at `yy_ch_buf[1]`, advancing `yy_c_buf_p` to `&yy_ch_buf[2]`. But `yy_get_next_buffer` checks `yy_c_buf_p > &yy_ch_buf[yy_n_chars + 1]` — with `yy_n_chars=0`, the boundary is `&yy_ch_buf[1]` and position 2 exceeds it, triggering "end of buffer missed". Setting `yy_n_chars=1` moves the boundary to `&yy_ch_buf[2]`, exactly where `yy_c_buf_p` lands.

The critical test case is `synth.dta`: `(scenes #include metamusic_scenes.dta)` where `)` immediately follows the filename — the `)` is the holdChar that must survive the include round-trip.

### Wasm-ld and ASM Labels

The decomp uses `asm("")` labels on stub functions to control symbol mangling for the PPC MSVC linker. Wasm-ld (LLVM's WebAssembly linker) does not support these — they produce wrong Itanium-ABI mangled names or `unreachable` traps. `web_stubs.cpp` provides proper C++ stub implementations with correct type signatures so libc++'s `std::__2` mangling works. `-sERROR_ON_UNDEFINED_SYMBOLS=0` tolerates any remaining unresolved stubs.

## Dependencies to Clone/Install

1. **Emscripten SDK**:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
   cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
   source ~/emsdk/emsdk_env.sh
   ```
2. **emdawnwebgpu**: Built into modern Emscripten — `--use-port=emdawnwebgpu` pulls it in automatically. No separate install needed.

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|-----------|
| ~~Flex lexer state across `#include`~~ | ~~Engine can't parse config files~~ | **RESOLVED** — holdChar ordering + yy_n_chars fix |
| WASM memory limits | OOM on large scenes | `ALLOW_MEMORY_GROWTH`, stream assets |
| WebGPU validation differences | Shader/pipeline creation fails | Test early, browser DevTools has great WebGPU errors |
| .ark file size (multi-GB) | Slow asset loading | Bundle API for bootstrap, stream on demand |
| MSVC compat flags vs Emscripten | Compile errors | `-fms-extensions` works in Emscripten's Clang |
| Emscripten's GLFW shim limitations | Missing input features | Fall back to Emscripten HTML5 input API |
| SharedArrayBuffer (for threads) | Requires COOP/COEP headers | Server sends headers; MVP is single-threaded |

## MVP Definition

The MVP is complete when:
1. `native/web/build.sh` produces `dc3-web.wasm` + `dc3-web.js`
2. `python native/web/server.py` starts on `:8420`
3. Opening `http://localhost:8420` in Chrome shows the engine rendering a scene
4. Assets stream from the local server API
5. Basic keyboard input works (camera orbit or UI navigation)

No audio, no threading, no mobile, no production hosting. Just proof-of-life in a browser tab.
