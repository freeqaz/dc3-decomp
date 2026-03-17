# Web Port — Unify with App.cpp

## Problem

`main_web.cpp` duplicates the engine init sequence from `App::App()` with subtle divergences that cause bugs:

| Issue | Root Cause | Impact |
|-------|-----------|--------|
| `NgEnviron` not registered → vtable crash | Web calls `TheRnd.PreInit()` AFTER `SystemInit`/`MagnuInit` (native calls it BEFORE) | `function signature mismatch` WASM trap on character draw |
| Init order differs (SynthInit, Movie::Init, MagnuInit relative to Rnd) | Copy-paste drift | Unknown latent bugs |
| Missing `MidiParser::Init()` | Web never added it | MidiParser objects can't deserialize from .milo |
| Missing `ContextCheckerInit()` | Web never added it | DTA `random_context` calls fail silently |
| Missing `DirLoader::SetPathEvalCallback` | Web never added it | Loads unnecessary assets, wasting memory/bandwidth |
| Missing sound bank load | Web never added it | No common SFX (Faders, FxSend objects) |
| Missing player provider wiring | Web never added it | Player side/presence may be wrong |
| Simpler stub managers | Web has fewer handler cases | DTA handlers fail silently on unhandled messages |
| Missing `ContentMgr::RefreshSynchronously()` | Web never added it | Song list may be incomplete |

Every native-side fix must be manually ported to `main_web.cpp`, and every divergence is a potential crash. The web build is no longer a POC — it's a real build target that should share the same init code.

## Goal

Web build uses `App` for engine init and main loop, with platform differences handled by `#ifdef __EMSCRIPTEN__` (or `HX_WEB`) inside App, not by a separate entry point.

## Architecture After Unification

```
main_native.cpp          main_web.cpp
  |                        |
  App app(argc, argv)      BOOT_INIT: WebAssetsInit + bundle fetch
  app.Run()                BOOT_FETCHING: poll until done
                           BOOT_ENGINE_INIT: App app(0, nullptr)
                           BOOT_GPU_WAIT: poll WebGPU async init
                           BOOT_GPU_READY: gWgpuRnd->InitGpuResources()
                           BOOT_RUNNING: app.RunOneFrame()
```

Web still needs the async boot state machine (can't block main thread for asset download or GPU init), but the engine init inside `BOOT_ENGINE_INIT` becomes a single `App` constructor call, and the per-frame work becomes a single `App::RunOneFrame()` call.

## Phases

### Phase 1: Extract `App::RunOneFrame()` from the main loop

Currently `App::RunWithoutDebugging()` has a `while(true)` loop (native, ~460 lines) or is never called (web). The native loop body contains several distinct concerns:

| Section | ~Lines | Description |
|---------|--------|-------------|
| Core polling | 15 | SystemPoll, UI, TaskMgr, FlowMgr, Synth |
| Venue setup | 210 | One-shot component .milo loading, crowd diagnostics, Kinect mesh hiding, face animation init, CharInterest creation |
| Drawing | 80 | BeginDrawing, venue 3D render (proxy vs non-proxy), environment/camera selection, UI Draw, EndDrawing |
| Auto-nav | 100 | DC3_SCREEN env var → screen chain navigation, game data setup |
| Frame mgmt | 40 | Frame counter, UI state dump, GLFW window close / headless frame limit |

Extract only the **core polling + drawing** into `RunOneFrame()`. Keep venue setup, auto-nav, and frame management in `RunWithoutDebugging`:

```cpp
void App::RunOneFrame() {
    // Core engine polling (shared between native/web)
    SystemPoll(false);
    if (TheUI) TheUI->Poll();
    TheTaskMgr.Poll();
    if (TheFlowMgr) TheFlowMgr->Poll();
    if (TheSynth) TheSynth->Poll();

#ifdef __EMSCRIPTEN__
    AudioDevice::GetInstance().PumpAudio();
#endif

    // Drawing
    TheRnd.BeginDrawing();
    if (TheUI) TheUI->Draw();
    TheRnd.EndDrawing();
}
```

- Native's `RunWithoutDebugging` calls `RunOneFrame()` inside its loop, with venue setup/auto-nav/frame mgmt around it
- Web's `BOOT_RUNNING` calls `app.RunOneFrame()` plus the EM_ASM frame counter
- Venue 3D rendering (proxy path, environment/camera selection, mesh iteration) stays in `RunWithoutDebugging` — it runs BEFORE `RunOneFrame()`'s draw call and is native-desktop-only
- Add `RunOneFrame()` declaration to App.h inside `#ifdef HX_NATIVE` (PPC build won't see it)

**Validation**: Native port still runs identically. Web build compiles and runs.

### Phase 2: Unify engine init — web uses `App::App()`

Remove the init sequence from `main_web.cpp` BOOT_ENGINE_INIT and replace with:

```cpp
case BOOT_ENGINE_INIT: {
    NativeSetDataDir("/data");
    static App *sApp = new App(0, nullptr);
    sBootState = BOOT_GPU_WAIT;
    break;
}
```

This requires making `App::App()` work under `__EMSCRIPTEN__`. Issues to address:

1. **`SystemPreInit` call path**: `App::App(0, nullptr)` calls `SystemPreInit(int argc, char **argv, const char *config)` (System_Native.cpp:124). With argc=0, the cmdLine loop is a no-op (empty string). Then it calls `InitMakeString()` and `NativeDetectDataDir()`. Both are fine — InitMakeString is idempotent, and NativeDetectDataDir won't find `gen/main_xbox.hdr` in MEMFS but also won't overwrite the `/data` dir already set. **Fix**: guard NativeDetectDataDir with `#ifndef __EMSCRIPTEN__` to suppress the spurious "WARNING - could not find game data" message.

2. **Init ordering: `SetFileChecksumData` vs `InitMakeString`**: Web currently calls `InitMakeString()` first, then `SetFileChecksumData()`. `App::App()` calls `SetFileChecksumData()` first (line 247), then `InitMakeString()` inside SystemPreInit. This matches native desktop and works (SetFileChecksumData doesn't use MakeString). No change needed.

3. **`DirLoader::SetCacheMode(true)`**: Web needs this because `UsingCD()==false` (MEMFS mode), so the automatic `ObjectDir::Init()` path (Dir.cpp:902-904) doesn't set it. After unification, web's explicit call at main_web.cpp:176 disappears. **Fix**: add to `App::App()`:
   ```cpp
   #ifdef __EMSCRIPTEN__
   DirLoader::SetCacheMode(true); // MEMFS uses pre-extracted gen/*_xbox paths
   #endif
   ```

4. **Sound bank load**: The native path loads it via `SystemConfig("sound", "banks", "common")`. Web will inherit this. **Prerequisite**: verify the bundle includes the common sound bank .milo file. There is NO on-demand fetch in MEMFS — files are either pre-downloaded in the bundle or they don't exist. If missing, `LoadFile` will fail.

5. **`ContentMgr::RefreshSynchronously()`**: On Xbox/native desktop, ContentMgr scans the archive to discover songs. On web with `UsingCD()==false` and no archive, behavior is unknown. **Must test before implementing**: call RefreshSynchronously on web, verify it doesn't hang or crash. If it no-ops (empty content list), web may need to skip it or populate content differently.

6. **Stub managers**: Already inside `#ifdef HX_NATIVE` which covers both web and native desktop. Web inherits the native stubs automatically. The native stubs use `FindObject` duplicate checking (App.cpp:386) while web stubs don't — native stubs are strictly better.

7. **GLFW header dependency**: App.cpp includes `<GLFW/glfw3.h>` inside `#ifdef HX_NATIVE`. Emscripten provides a GLFW shim so it compiles. `extern GLFWwindow *gNativeWindow` resolves to null via `-sERROR_ON_UNDEFINED_SYMBOLS=0`. This works but is fragile. **Fix**: guard GLFW-specific code with `#if defined(HX_NATIVE) && !defined(__EMSCRIPTEN__)`.

8. **Debug logging**: Web's BOOT_ENGINE_INIT has ~15 printf progress statements (e.g. "DC3 Web: TheRnd.PreInit()..."). After unification these disappear. Add equivalent logging to App::App() under `#ifdef __EMSCRIPTEN__` or drop them (the engine's own debug output covers most of it).

**Validation**: Web build produces no WASM traps, reaches BOOT_RUNNING, renders first frame. Playwright screenshot comparison with pre-unification baseline. CDP debugger test passes.

### Phase 3: Unify stub managers

> **Note**: This phase is independent of Phases 1-2 and lower risk. Consider doing it first as a standalone refactor to reduce the diff for later phases.

The web and native stub managers (`WebSaveLoadMgr` vs `NativeSaveLoadStub`, etc.) are duplicated with different coverage. Consolidate:

- Keep the native versions (they handle more message types — e.g. NativeProfileMgrStub handles 23 messages vs WebProfileMgr's 7)
- Delete the web-specific versions from `main_web.cpp`
- The `App::App()` constructor already registers them for both platforms (inside `#ifdef HX_NATIVE`, which covers web)
- Native stubs use `FindObject` duplicate checking before registration; web stubs don't. After unification, all stubs get the safer native registration path

### Phase 4: Unify main loop

After Phases 1-2, `PumpAudio` is already inside `RunOneFrame()` (under `#ifdef __EMSCRIPTEN__`). The web main loop reduces to:

```cpp
case BOOT_RUNNING: {
    sFrameCount++;
    sApp->RunOneFrame();

    // Web-specific: frame counter for Playwright
    EM_ASM({ window.dc3FrameCount = $0; }, sFrameCount);
    break;
}
```

The early-frame debug logging (`if (sFrameCount <= 3) printf(...)`) can be dropped or kept in main_web.cpp around the RunOneFrame call — it doesn't need to be inside App.

### Phase 5: Slim down `main_web.cpp`

After unification, `main_web.cpp` should contain ONLY:
- The `BootState` state machine (asset download → engine init → GPU wait → running)
- `WebAssetsInit()` / `WebAssetsFetchBundle()` calls
- GPU wait polling
- `emscripten_set_main_loop` setup
- A pointer to the `App` instance

Everything else lives in `App.cpp` behind `#ifdef` guards where needed. Target: `main_web.cpp` under 100 lines.

### Phase 6: Delete `web_stubs.cpp` cruft

With the real init path running, many web stubs become dead code (the real implementations are linked). Audit `web_stubs.cpp` and remove anything that's now provided by the unified init:
- `Waypoint::sWaypoints` — already removed (real `Waypoint.cpp` compiled)
- `TextStream::operator<<` — check if real impl linked
- Template instantiations — check if still needed

## What stays web-specific

These are genuinely platform-different and should remain `#ifdef __EMSCRIPTEN__`:

| Component | Where | Why |
|-----------|-------|-----|
| `WebAssets` fetch system | `main_web.cpp` | Browser uses MEMFS + XHR, native uses filesystem |
| `emscripten_set_main_loop` | `main_web.cpp` | Browser requires cooperative yielding |
| `AudioDevice::PumpAudio()` | `App::RunOneFrame()` (`#ifdef __EMSCRIPTEN__`) | Web uses AudioWorklet ring buffer push model |
| GPU wait state machine | `main_web.cpp` (BOOT_GPU_WAIT + BOOT_GPU_READY) | WebGPU adapter/device init is async in browser |
| `DirLoader::SetCacheMode(true)` | `App::App()` (`#ifdef __EMSCRIPTEN__`) | MEMFS uses pre-extracted gen/*_xbox paths; UsingCD()==false skips auto-set |
| `NativeSetDataDir("/data")` | `main_web.cpp` (before App constructor) | Web data lives in MEMFS /data, not filesystem |
| `missing_stubs.js` | linker pre-js | Catches remaining undefined symbols gracefully |
| `EM_ASM` frame counter | `main_web.cpp` | Playwright test harness integration |

## What should NOT diverge

| Component | Current state | Target |
|-----------|--------------|--------|
| Engine init sequence | Duplicated, diverged | Single `App::App()` |
| Subsystem init order | Different between web/native | Identical |
| Stub manager classes | Duplicated, web is simpler | Single set in App.cpp |
| Main loop body | Duplicated | Single `App::RunOneFrame()` |
| Factory registration (NgEnviron etc.) | Web called PreInit too late (after SystemInit) | Inherited from App (correct order) |

## Risk

- **Async init**: `App::App()` does synchronous file I/O. On web, files are in MEMFS (from bundle download in BOOT_FETCHING), so this is fine. But if future changes add I/O that hits the network, it would block the main thread. Mitigation: the bundle download happens BEFORE `App::App()`.
- **Missing stubs**: Unifying may expose new missing functions that web was previously avoiding by not calling certain init paths (e.g. sound bank loading pulls in Fader/FxSend deserialization). Fix: implement them as they surface in `web_stubs.cpp` (same approach as native).
- **PPC decomp**: Changes to `App.cpp` must not regress PPC match. All new code goes inside `#ifdef HX_NATIVE` blocks. The existing native init path (App.cpp:267-402) is already inside `#ifdef HX_NATIVE`, so additions are safe. `RunOneFrame()` declaration in App.h must also be guarded.
- **ContentMgr on web**: `RefreshSynchronously()` may behave differently without an archive. Must test before merging — if it hangs or crashes, web needs to skip it with `#ifndef __EMSCRIPTEN__`.
- **Bundle completeness**: Sound bank .milo and any other assets loaded during `App::App()` must be present in the web asset bundle. MEMFS has no on-demand fetch — missing files fail immediately. Audit the bundle manifest against the init path's `LoadFile` calls.
- **GLFW symbol resolution**: `extern GLFWwindow *gNativeWindow` inside `#ifdef HX_NATIVE` compiles on web (Emscripten GLFW shim) but resolves to null via `-sERROR_ON_UNDEFINED_SYMBOLS=0`. Works today but is fragile — should be guarded with `#if defined(HX_NATIVE) && !defined(__EMSCRIPTEN__)`.
- **Rollback**: Consider a compile-time `#define USE_UNIFIED_INIT 1` during migration so either init path can be selected if issues surface late.
