# Web Build Debugging

Techniques for debugging the DC3 WASM/Emscripten web port running in headless Chromium.

## Prerequisites

```bash
sudo pacman -S xorg-server-xvfb chromium
npm install playwright
```

WebGPU requires a real GPU context. Use `xvfb-run` to provide a virtual X11 display for headless execution.

## Quick Reference

```bash
# Start dev server (must be running for all tools below)
python3 native/web/server.py --port 8420

# Capture gameplay logs (90s default)
scripts/web/capture-logs.sh

# Capture with custom duration
scripts/web/capture-logs.sh --duration 120 --output /tmp/my-log.txt

# CDP debugger break (pauses at hang point, dumps call stack)
xvfb-run -a --server-args="-screen 0 1920x1080x24" \
  node native/web/tests/cdp-debugger-break.js --no-server --verbose

# Full menu navigation + song loading diagnosis
xvfb-run -a --server-args="-screen 0 1920x1080x24" \
  node native/web/tests/diagnose-song-load.js --no-server --verbose

# Rebuild WASM
scripts/build/web.sh
```

## CDP Debugger Break

The most powerful tool for diagnosing hangs and crashes. Uses Chrome DevTools Protocol to pause WASM execution and dump the call stack.

### How It Works

Playwright launches Chrome, navigates through menus to gameplay, then monitors console output. When output goes silent (configurable `--silence` threshold), it sends `Debugger.pause()` via CDP, capturing the exact call stack.

### Script

`native/web/tests/cdp-debugger-break.js`

### Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--silence N` | 5 | Seconds of console silence before triggering break |
| `--no-server` | off | Use an existing server (don't spawn one) |
| `--port N` | 8420 | Server port |
| `--verbose` | off | Print all console output |

### Usage

```bash
xvfb-run -a --server-args="-screen 0 1920x1080x24" \
  node native/web/tests/cdp-debugger-break.js --no-server --verbose --silence 8
```

### Reading the Output

**Successful pause (hang detected):**
```
=== CALL STACK ===
  #0: $ObjDirItr<RndLight>::Advance()
  #1: $WgpuRnd::WriteSceneUniforms()
  #2: $DrawMeshImmediate(RndMesh*)
  ...
  #20: $mainLoop()
```
This tells you exactly where the WASM is stuck. Function names are demangled C++.

**CDP can't pause (WASM trapped):**
```
WARNING: Debugger.pause did not trigger within 5s
```
This means the WASM crashed (not hung). Look for `[PAGE_ERROR]` in the output — it will show the error type (e.g., `function signature mismatch`, `memory access out of bounds`).

**Safety timeout (no hang):**
```
Safety timeout reached (90s)
=== CALL STACK ===
  #0: MainLoop_runner
```
Game is running normally. `MainLoop_runner` is the idle state between frames.

### Diagnosing Common Issues

#### `function signature mismatch`

A WASM `call_indirect` type check failed. This means a virtual function call went through a vtable entry with the wrong function type. Common causes:

1. **Missing decomp function** — A virtual method is declared but never implemented. Emscripten generates an abort stub with wrong WASM type. Fix: implement the function.
2. **Editor-mode code path** — `UsingCD()==false` activates diagnostic paths that call into unimplemented NG shader/material subsystems. Fix: guard with `#ifdef HX_NATIVE` to skip editor-only paths.
3. **Init ordering** — Factory registration happens after objects are loaded, so vtables point to base class instead of the registered subclass.

To identify which function: check the browser console's full error stack trace. It shows the C++ function names with demangled signatures.

#### `memory access out of bounds`

A WASM load/store hit invalid memory. Common causes:

1. **Null pointer dereference** — An object pointer is null but code doesn't check.
2. **PropAnim SetFrame OOB** — `RndPropAnim::SetFrame()` evaluates PropKeys targeting objects that don't exist on the native port. Use `AdvanceFrame()` instead to only update the internal frame counter.
3. **Stale object reference** — FileMerger cleared an object that's still referenced elsewhere.

#### Infinite loop (hang, CDP pauses successfully)

CDP gives the exact stack. Common patterns:

1. **`ObjDirItr::Advance()`** — Recursive dir iterator on corrupted hash table chain. Fix: avoid `ObjDirItr(dir, true)` in per-frame code.
2. **DTA `{while}` loop** — A script loop waiting for a condition that never becomes true on native. Fix: initialize the missing variable.
3. **Sync file load** — `FileMerger::StartLoadInternal` with `async=false` busy-loops `TheLoadMgr.Poll()`. Fix: force `async=true` on native.

## Log Capture

### Automated

```bash
scripts/web/capture-logs.sh --duration 120
```

Captures all console output, then prints a summary with error counts and timeline.

### Manual

```bash
xvfb-run -a --server-args="-screen 0 1920x1080x24" \
  node native/web/tests/cdp-debugger-break.js --no-server --verbose --silence 20 \
  > /tmp/my-log.txt 2>&1
```

### Analyzing Logs

```bash
# Timeline of game state transitions
grep -E "StartGame|game_stage|IsLoaded|MoveGraph|rekick|LoadCharacters|AllCharsLoaded" log.txt

# All file loads (character outfits, venues, songs)
grep -E "AsyncFile opening|fetched on-demand|FAILED" log.txt

# Errors
grep -E "PAGE_ERROR|function signature|memory access|stub" log.txt

# Animation state
grep "AnimDiag" log.txt

# Missing metamaterials (rendering issues)
grep "couldn't find.*mmat" log.txt | sort -u
```

## Keyboard Input Mapping

The web port maps keyboard events to gamepad buttons:

| Key | Gamepad | Use |
|-----|---------|-----|
| `Space` | Start (kPad_Start) | Dismiss screens, skip intro |
| `Enter` | A/Confirm (kPad_X) | Select menu items |
| `ArrowDown` | D-pad Down | Navigate lists |
| `ArrowUp` | D-pad Up | Navigate lists |
| `Escape` | B/Back (kPad_Circle) | Go back |

Navigation sequence to reach gameplay:
```
attract → [Space] → title → [Space] → main → [Enter] → choose_mode
→ [Enter] → song_select → [Down x3] → [Enter] → multiuser → loading
→ preloading → real_loading → game_screen → StartIntro
```

## Sandbox

GPU access requires `dangerouslyDisableSandbox: true` for bash commands. The xvfb-run + Chromium + Vulkan stack needs unrestricted filesystem and device access.

## Test Scripts

| Script | Purpose | Key Flags |
|--------|---------|-----------|
| `cdp-debugger-break.js` | Pause at hang, dump call stack | `--silence`, `--verbose` |
| `diagnose-song-load.js` | Full menu navigation + song loading | `--timeout`, `--verbose` |
| `capture-logs.sh` | Automated log capture + summary | `--duration`, `--output` |

## Case Studies

### ObjDirItr Infinite Loop (2026-03-17)

**Symptom**: Game hung after ~1500 frames during song gameplay.

**CDP stack**: `ObjDirItr<RndLight>::Advance()` → `WgpuRnd::WriteSceneUniforms()` — recursive dir iterator on venue WorldDir every frame.

**Fix**: Removed `ObjDirItr<RndLight>(venueDir, true)` scans from `WriteSceneUniforms()`. Environment light lists + fallback defaults are sufficient.

### SelectConfig Shader Crash (2026-03-17)

**Symptom**: `function signature mismatch` WASM trap during character drawing.

**Stack**: `RndShader::MatShaderFlagsOK` → `RndShader::SelectConfig` → `RndTexBlender::DrawShowing` → `Character::DrawShowing`.

**Root cause**: `UsingCD()==false` on web (set by `NativeArchiveInit` for MEMFS). This activated the editor-mode shader diagnostic path in `SelectConfig`, which calls virtual methods on NG shader objects not fully implemented for WASM.

**Fix**: Guard the `!UsingCD()` check in `SelectConfig` with `#ifdef HX_NATIVE` so native/web only enters diagnostics via `EditMode()` (always false at runtime), matching Xbox retail behavior.

### song.anim Not Advancing (2026-03-17)

**Symptom**: Dancers animate for one frame then freeze in T-pose.

**Diagnostic**: Added fprintf in `HamDirector::Poll()` showing `songAnim->GetFrame()` stuck at 0.0 across all frames.

**Root cause**: On Xbox, a DTA `{animate}` task advances `song.anim` each frame. On native/web, the DTA scripting flow that creates this `AnimTask` doesn't execute.

**Fix**: In `HamDirector::Poll()`, manually call `songAnim->AdvanceFrame(secs * 30.0f)` from real-time seconds. Uses `AdvanceFrame()` (not `SetFrame()`) to avoid triggering PropKeys evaluation that crashes on missing Xbox objects.
