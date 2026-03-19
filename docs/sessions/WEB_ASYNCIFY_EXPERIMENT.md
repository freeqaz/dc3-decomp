# Web Build: Non-Blocking Loading (JSPI)

**Date**: 2026-03-19
**Status**: Working via JSPI. Blocked by pre-existing crowd anim crash (unrelated).

## JSPI Migration (from ASYNCIFY)

Switched from `-sASYNCIFY` (50% code size overhead) to `-sJSPI` (zero overhead).
JSPI uses the browser's native `WebAssembly.promising` stack switching — no WASM instrumentation.
Same `emscripten_sleep()` API, same behavior, same code. Requires Chrome 137+ / Firefox 139+.

**Key gotcha**: `emscripten_set_main_loop` calls the callback via the WASM function table (indirect call),
which bypasses JSPI's `promising` wrapper. Fix: export the tick function as a WASM export and drive it
from JS via `requestAnimationFrame` with `await`. The JSPI_EXPORTS regex must include the name WITHOUT
the leading underscore (e.g. `dc3MainLoopTick`, not just `_dc3MainLoopTick`) to match the actual WASM
export name.

**WASM size**: 23MB with JSPI = 23MB without = zero overhead. ASYNCIFY was ~35MB.

## Problem

The web build loads `.milo_xbox` assets on-demand via synchronous XHR (`WebAssetsFetchSync`). This blocks the main thread — the loading screen freezes, the browser shows "not responding", and game triggers can fire before dependent data is ready.

The original Xbox engine used truly async I/O with a frame-by-frame polling loop (`LoadMgr::Poll` -> `DirLoader::PollLoading` -> `AsyncFile::_ReadDone`). The native port short-circuits this by completing all I/O synchronously in `AsyncFile_Native.cpp`.

## Solution: ASYNCIFY + Async Fetch Yield

Added Emscripten's `-sASYNCIFY` to the web build. `WebAssetsFetchSync()` now starts a non-blocking `emscripten_fetch()` and yields via `emscripten_sleep(16)` (~60fps) until the fetch callback writes the file to MEMFS. The browser event loop runs during each yield, allowing the loading screen to render and fetch callbacks to fire.

### Why ASYNCIFY (not restructuring)

`AsyncFile::Init()` (shared engine code, line 282) has `while (!_OpenDone()) ;` — a spin loop that can't yield without ASYNCIFY. Restructuring this would require changes throughout the Loader/DirLoader/UIPanel pipeline. ASYNCIFY solves it at the lowest level with zero engine changes.

### Why not other approaches

- **Pre-fetching all assets at boot**: Wasteful (hundreds of MB), and we don't know which files the engine will request until it parses .milo headers.
- **JSPI (`-sASYNCIFY=2`)**: Zero code-size overhead but requires Chrome 123+ with flags. Not ready for general use.
- **Web Workers / pthreads**: Requires `-pthread` and `SharedArrayBuffer`. More complexity than needed.

## Files Changed

### Core experiment (reversible via `cmake -DDC3_WEB_ASYNCIFY=OFF`)

| File | Change |
|------|--------|
| `native/CMakeLists.txt` | `option(DC3_WEB_ASYNCIFY)`, conditional `-sASYNCIFY=1 -sASYNCIFY_STACK_SIZE=131072` |
| `native/src/platform/WebAssets.h` | Added `WebAssetsFetchByPath()`, `WebAssetsFetchSucceeded()` |
| `native/src/platform/WebAssets.cpp` | `#ifdef DC3_WEB_ASYNCIFY`: async fetch + `emscripten_sleep(16)` loop. `#else`: original blocking XHR. Shared `normalizeMemfsPath()` helper. |

### Abort guard fixes (keep regardless)

Changed `#ifdef HX_NATIVE` to `#if defined(HX_NATIVE) && !defined(HX_WEB)` on 5 `abort()` calls that fire during stream deserialization. The web build defines both `HX_NATIVE=1` and `HX_WEB=1`, so these were live on web but should be non-fatal (matching `Debug::Fail`'s existing web guard).

| File | Line | Trigger |
|------|------|---------|
| `src/system/utl/BinStream.cpp` | 132 | PopRev: empty revision stack |
| `src/system/utl/BinStream.cpp` | 219 | String size > 10000 or < 0 |
| `src/system/obj/Object.h` | 1396 | ObjVector length > 0x10000 |
| `src/system/obj/DataNode.cpp` | 782 | Unrecognized node type |
| `src/system/flow/FlowNode.cpp` | 130 | numEntries < 0 or > 256 |

## Test Results

### ASYNCIFY ON (async-yield fetch)
- Bundle loads: 492 DTA/DTB files in 0.6s
- On-demand `.milo_xbox` files: fetched non-blockingly, "async-yield fetch" log messages confirm yields
- Loading screen: **renders correctly** (screenshot shows loading bar + venue background)
- Crash at 23.1s: `Aborted(native code called abort())` after loading `male_medium.milo_xbox`

### ASYNCIFY OFF (blocking sync XHR)
- Same crash at 21.2s after `male_medium.milo_xbox`
- Confirms crash is **pre-existing**, not caused by ASYNCIFY

### Crash analysis
The crash happens during crowd animation deserialization, right after `male_medium.milo_xbox` (3.1MB) is written to MEMFS. `PollForLoading` is stuck at state 1 with `IsWorldLoaded=false`. No diagnostic message before the abort — likely hitting an unguarded assertion in a template instantiation or inline function not caught by the grep for `abort()`.

## HX_WEB / __EMSCRIPTEN__ Audit

All 18 guards in `src/` (shared engine) and 30+ in `native/src/` were reviewed. All are still necessary:

- `Debug::Fail` web return (non-fatal fails)
- `BinStream::WaitUntilReady` deadlock prevention
- `Loader::PollUntilLoaded` iteration limit (10000)
- `DirLoader::OpenFile` MEMFS path forcing
- `FileMerger` async mode forcing
- `UIPanel/UIScreen/HamPanel::Exiting` always-false (no exit animations)
- `UIScreen::Enter` sync Poll skip
- `HamDirector::IsWorldLoaded` venue recovery hack
- `HamNavList::DrawShowing` re-fill workaround
- Various movie/audio platform dispatching

## How to Disable

```bash
# Build without ASYNCIFY (falls back to blocking sync XHR)
emcmake cmake -S native -B native/build-web -DDC3_WEB_ASYNCIFY=OFF
cmake --build native/build-web

# Or just flip the option in CMakeLists.txt:
# option(DC3_WEB_ASYNCIFY "..." OFF)
```

## Next Steps

1. **Fix the crowd anim crash** — find the unguarded abort() or assertion in the `male_medium.milo_xbox` deserialization path. Try building with `-fsanitize=address` or add fprintf before all remaining potential crash points.
2. **Fix `IsWorldLoaded` stuck at false** — the `mMoveMerger` is null (`moveMerger=0`), preventing loading from advancing past state 1. The `movePending=-1` in logs is a hardcoded diagnostic sentinel, not a real value.
3. **Consider pre-fetching** — once loading works, add speculative pre-fetches for known asset paths (venue dirs, crowd anims) during menu navigation to reduce loading times.
