# DC3 Web Port — Browser via WebAssembly + WebGPU

## Overview

Compile the DC3 native port to run in web browsers using Emscripten (C++ → WASM) with WebGPU. The engine already renders through Dawn's `webgpu.h` API — the browser's native WebGPU API implements the same interface, so the rendering pipeline ports with minimal changes.

**MVP goal**: A localhost:8420 dev server that boots the engine in a browser tab, streams assets from a local API, and renders a scene (venue or character).

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Browser Tab                                            │
│  ┌───────────────┐  ┌────────────────────────────────┐  │
│  │  index.html   │  │  dc3.wasm + dc3.js (glue)     │  │
│  │  + bootstrap  │──│  Emscripten runtime            │  │
│  │               │  │  webgpu.h → browser WebGPU     │  │
│  └───────────────┘  └──────────┬─────────────────────┘  │
│                                │ fetch()                 │
│                                ▼                         │
│  ┌─────────────────────────────────────────────────────┐ │
│  │  Asset API  (localhost:8420/api/ark/...)            │ │
│  │  Serves .milo files, DTA scripts, textures         │ │
│  └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
                          ▲
                          │ HTTP
          ┌───────────────┴────────────────┐
          │  Python dev server (:8420)      │
          │  - Serves static WASM/HTML/JS  │
          │  - Asset streaming API          │
          │  - .ark extraction on-the-fly   │
          └────────────────────────────────┘
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

### Phase 0: Toolchain Setup

**Goal**: Emscripten SDK installed, can compile a trivial C++ WebGPU triangle to the browser.

**Tasks**:
1. Install Emscripten SDK (`emsdk install latest && emsdk activate latest`)
2. Install emdawnwebgpu package for WebGPU bindings
3. Compile the existing `wgpu-window-test` target (minimal triangle) to WASM
4. Serve with `python -m http.server 8420` and verify it renders in Chrome
5. Document the toolchain in `native/web/README.md`

**Key `#ifdef`s**:
```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif
```

**Validation**: Triangle renders in Chrome tab at localhost:8420.

---

### Phase 1: CMake Web Target

**Goal**: `native/CMakeLists.txt` gains a web build path producing `dc3-web.wasm` + `dc3-web.js`.

**Tasks**:
1. Add `HX_WEB` compile definition (gated on `EMSCRIPTEN` toolchain detection)
2. Create `native/web/CMakeLists.txt` or extend root CMakeLists with web conditionals
3. Platform-conditional exclusions:
   - **Remove**: GLFW window creation (canvas replaces it), FFmpeg (no native libs in WASM), miniaudio (use Web Audio API later)
   - **Stub**: Threading (single-threaded MVP), audio playback
4. Link against emdawnwebgpu instead of Dawn
5. Emscripten link flags:
   ```cmake
   target_link_options(dc3-web PRIVATE
     -sUSE_WEBGPU=1
     -sUSE_GLFW=3           # Emscripten's GLFW shim → canvas
     -sALLOW_MEMORY_GROWTH=1
     -sMAXIMUM_MEMORY=512MB
     -sSTACK_SIZE=1048576   # 1MB stack
     -sEXPORTED_FUNCTIONS=['_main']
     -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']
     -sASYNCIFY              # Needed for async fetch in sync C++ code
     --preload-file assets@/ # Small bootstrap assets only
   )
   ```
6. Build script: `native/web/build.sh` wrapping `emcmake cmake .. && cmake --build .`

**Validation**: `dc3-web.wasm` compiles (link errors OK at this stage).

---

### Phase 2: Event Loop Adaptation

**Goal**: Engine main loop runs in the browser without freezing the tab.

**Problem**: Browser requires yielding to the event loop every frame. Native `while(running) { poll(); draw(); }` blocks forever.

**Solution**:
```cpp
// In App::Run() or equivalent
#ifdef HX_WEB
void WebFrameCallback(void* arg) {
    App* app = static_cast<App*>(arg);
    app->RunOneFrame();
}

void App::Run() {
    emscripten_set_main_loop_arg(WebFrameCallback, this, 0, true);
}
#else
void App::Run() {
    while (!mQuit) RunOneFrame();
}
#endif
```

**Tasks**:
1. Refactor `App::RunWithoutDebugging()` to extract a single-frame `RunOneFrame()` method
2. Wire `emscripten_set_main_loop_arg()` under `HX_WEB`
3. Handle `emscripten_request_animation_frame()` for vsync
4. Canvas sizing: read from HTML element, pass to engine init

**Validation**: Engine boots, clears screen to teal, runs frame loop without freezing.

---

### Phase 3: Asset Streaming API

**Goal**: A Python dev server at `:8420` that serves WASM build artifacts + streams game assets via HTTP API.

#### Server Side (Python)

**File**: `native/web/server.py`

```
GET /                         → index.html
GET /dc3-web.{js,wasm}       → build artifacts
GET /api/ark/<path>           → raw bytes from .ark archive
GET /api/milo/<path>          → extracted .milo_xbox file
GET /api/dta/<path>           → DTA script files
GET /api/manifest             → JSON list of available assets
```

**Implementation**:
- Python 3, no dependencies beyond stdlib (`http.server` + custom handler)
- `.ark` reading: reuse existing `scripts/` Python tooling or call the engine's archive reader
- Range request support (`Accept-Ranges: bytes`) for partial loading
- CORS headers for fetch() from WASM
- **COOP/COEP headers** (required if we later enable threads):
  ```
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp
  ```

#### Client Side (C++ in WASM)

**Tasks**:
1. Create `native/src/platform/File_Web.cpp` implementing file I/O via `emscripten_fetch()`
2. Replace synchronous `fopen/fread` with async fetch + ASYNCIFY (Emscripten feature that suspends/resumes C++ across async JS calls)
3. `CDReader_Web.cpp` — fetches .ark data via `/api/ark/<file>?offset=N&size=M`
4. Cache fetched assets in Emscripten's IDBFS (IndexedDB) to avoid re-downloading

**Alternative (simpler MVP)**: Use Emscripten's `--preload-file` for a small set of bootstrap assets (DTA configs), and fetch .milo files on demand.

**Validation**: Engine loads a DTA config from the server API and parses it.

---

### Phase 4: WebGPU Initialization (Browser Path)

**Goal**: `WgpuRnd` initializes against the browser's WebGPU implementation instead of Dawn.

**Key differences from native Dawn**:

| Aspect | Native (Dawn) | Browser (emdawnwebgpu) |
|--------|---------------|----------------------|
| Instance creation | `wgpu::CreateInstance(descriptor)` | `wgpu::CreateInstance(nullptr)` — descriptor must be null |
| Adapter request | Synchronous in Dawn | Async callback (use ASYNCIFY) |
| Device request | Synchronous in Dawn | Async callback (use ASYNCIFY) |
| Surface | GLFW native window handle | HTML canvas element |
| Shader | WGSL string (same) | WGSL string (same) |

**Tasks**:
1. `GpuDevice.cpp` — add `#ifdef HX_WEB` path:
   ```cpp
   #ifdef HX_WEB
   // Get canvas surface
   wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc;
   canvasDesc.selector = "#dc3-canvas";
   wgpu::SurfaceDescriptor surfaceDesc;
   surfaceDesc.nextInChain = &canvasDesc;
   mSurface = mInstance.CreateSurface(&surfaceDesc);
   #else
   // GLFW native surface (existing code)
   #endif
   ```
2. Async adapter/device request with Emscripten callbacks or ASYNCIFY
3. Canvas resize handling via `emscripten_set_resize_callback()`
4. Remove GLFW dependency under `HX_WEB` (canvas IS the window)

**Validation**: WebGPU device created, clear color visible on canvas.

---

### Phase 5: Rendering in Browser

**Goal**: Full rendering pipeline works — meshes, materials, textures, shaders.

**What should Just Work (same `webgpu.h` API)**:
- WGSL shaders (browser natively interprets WGSL)
- Render pipeline creation
- Bind groups / uniform buffers
- Texture creation and upload
- Draw calls
- MSAA resolve

**What needs attention**:
1. **Texture formats**: Verify BGRA8Unorm is supported (it is on Chrome)
2. **Buffer alignment**: 256-byte uniform offset alignment (same on browser)
3. **Shader compilation**: Browser compiles WGSL at runtime (may need error handling for validation differences)
4. **Frame timing**: `requestAnimationFrame` cadence vs native vsync

**Tasks**:
1. Load `standard.wgsl` shader (embed as string or fetch from server)
2. Verify ring buffer allocation works with WASM memory
3. Test with a simple .milo scene (single mesh + material)
4. Debug any WebGPU validation errors in browser console

**Validation**: A .milo_xbox venue or character renders in the browser.

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

## File Layout

```
native/
├── web/
│   ├── CMakeLists.txt          # Web-specific build config (or integrated into parent)
│   ├── build.sh                # emcmake wrapper
│   ├── server.py               # Dev server (localhost:8420)
│   ├── index.html              # Bootstrap HTML + canvas
│   ├── style.css               # Fullscreen canvas styling
│   └── README.md               # Setup instructions
├── src/
│   ├── platform/
│   │   ├── File_Web.cpp        # Emscripten fetch-based file I/O
│   │   ├── CDReader_Web.cpp    # Asset streaming via HTTP API
│   │   ├── System_Web.cpp      # Web platform init
│   │   └── Audio_Web.cpp       # Web Audio API (post-MVP)
│   └── gfx/
│       └── GpuDevice.cpp       # +#ifdef HX_WEB canvas surface path
```

## `#ifdef HX_WEB` Guards

| File | What changes |
|------|-------------|
| `App.cpp` | `emscripten_set_main_loop_arg()` instead of while loop |
| `GpuDevice.cpp` | Canvas surface instead of GLFW window |
| `File_Native.cpp` → `File_Web.cpp` | `emscripten_fetch()` instead of POSIX |
| `CDReader_Native.cpp` → `CDReader_Web.cpp` | HTTP range requests instead of local fread |
| `main_native.cpp` | Remove signal handlers, simplify to `main()` |
| `Rnd_Wgpu.cpp` | Async device init, canvas resize |
| Stubs | FFmpeg stubs, miniaudio stubs |

## Dependencies to Clone/Install

1. **Emscripten SDK**:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
   cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
   source ~/emsdk/emsdk_env.sh
   ```
2. **emdawnwebgpu**: Built into modern Emscripten — `-sUSE_WEBGPU=1` pulls it in automatically at link time. No separate install needed.

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|-----------|
| ASYNCIFY performance overhead | ~10-30% slower | Profile; consider `-sPROXY_TO_PTHREAD` instead |
| WASM memory limits | OOM on large scenes | `ALLOW_MEMORY_GROWTH`, stream assets |
| WebGPU validation differences | Shader/pipeline creation fails | Test early, browser DevTools has great WebGPU errors |
| .ark file size (multi-GB) | Slow asset loading | Stream on demand, cache in IndexedDB |
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
