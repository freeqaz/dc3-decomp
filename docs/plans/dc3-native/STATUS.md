# DC3 Native Port — Status

**Last updated**: 2026-03-02

## Current State

The DC3 native port (x86_64 Linux) boots through ALL engine subsystems, navigates UI screens automatically via DTA scripts, and runs 5000+ frames stably with clean exit.

### Boot Flow (verified)

```
Archive loading → Config/DTA parsing → SystemInit → All subsystem inits →
UIManager::Init → GotoFirstScreen('attract_screen') → Main loop (Poll + Draw)
```

### Screen Navigation (automatic, no input required)

```
attract_screen
  → autosave_warning_screen
  → title_screen
  → wait_main_after_saveload_screen
  → title_screen_to_voice_control_tutorial_screen
  → tutorial_party_mode_screen_0
  → tutorial_party_mode_screen_1  ← stuck here (needs Kinect gesture to advance)
```

With scripted input (Start/Confirm/Down buttons), the same flow occurs. The tutorial screens auto-advance via DTA timers without user input.

### Rendering

- WebGPU (Dawn) backend, headless or windowed
- 3D mesh geometry renders (verified via headless screenshots)
- UI text/panel overlays not yet visible in screenshots
- Tested with `MILO_RENDER=1 MILO_HEADLESS=1`

### Stability

- 5000+ frames without crash (default fatal mode)
- Clean exit via `MILO_MAX_FRAMES` env var
- No memory corruption detected (ASan clean with known suppressions)

## Architecture

### Error Handling Strategy

| Macro | Xbox 360 Behavior | Native Behavior | Use Case |
|-------|-------------------|-----------------|----------|
| `MILO_ASSERT(cond, line)` | `Debug::Fail` (modal dialog + Continue) | `Debug::Fail` (fatal by default) | Code invariant violations |
| `MILO_FAIL(...)` | `Debug::Fail` (modal dialog + Continue) | `Debug::Fail` (fatal by default) | Unexpected runtime errors |
| `MILO_FAIL_DTA(...)` | `Debug::Fail` (same as MILO_FAIL) | `MILO_WARN` (non-fatal) | DTA runtime errors (property not found, type mismatches) |
| `MILO_WARN(...)` | `Debug::Warn` (log only) | `Debug::Warn` (log only) | Non-fatal warnings |

**`MILO_FATAL_FAILS` env var**: Controls `Debug::Fail` behavior.
- `1` (default): Fatal — abort on MILO_ASSERT and MILO_FAIL. Catches real bugs early.
- `0`: Non-fatal — print + continue (Xbox 360 "Continue" dialog behavior). Use for exploring past crashes.

**`MILO_FAIL_DTA`**: Defined in `Debug.h`. On Xbox, expands identically to `MILO_FAIL` (no decomp impact). On native, expands to `MILO_WARN`. Used for errors that fire on Xbox debug builds but aren't code bugs (e.g., DTA script accessing a property that doesn't exist on a particular object type).

### DataNode Safe Fallback Returns

When `MILO_FAIL_DTA` returns (non-fatal mode), DataNode accessor methods need safe return values to prevent SIGSEGV from dereferencing garbage union members. Under `#ifdef HX_NATIVE`, each accessor returns a safe default after the error:

- `Sym()` / `LiteralSym()` / `ForceSym()` → `Symbol("")`
- `Str()` / `LiteralStr()` → `""`
- `Array()` / `LiteralArray()` / `Command()` → `nullptr`
- `Var()` / `Func()` → `nullptr`

### NewObject Vtable Guard

`Hmx::Object::NewObject()` wraps factory calls in `sigsetjmp/siglongjmp` to catch SIGSEGV from broken vtables (weak stub constructors that don't initialize the vtable). After construction, it calls `obj->ClassName()` to verify the vtable works. Broken types are blacklisted in `sBrokenClasses` and return nullptr on subsequent calls.

**Currently blacklisted**: `KinectSharePanel` (now has proper stub), `MeshAnim` (weak stub only).

## Key Fixes (Sessions 20-21)

### KinectSharePanel Vtable Crash

**Root cause**: `native/src/platform/KinectShare_Stub.cpp` was empty — no constructor, no vtable. The weak stub in `engine_stubs_generated.cpp` ran instead (`return 0`), leaving the vtable uninitialized. DTA scripts call `{new KinectSharePanel ...}` during UIManager::Init, and subsequent virtual calls (ClassName, SetName) crashed.

**Fix**: Wrote a proper stub in `KinectShare_Stub.cpp` with constructor, handler table, propsyncs, and Poll. All XDK-specific methods (OnUpload, OnPostLink, ConvertImages, etc.) are no-ops.

**Also**: Improved `NewObject` to verify vtable after construction by calling `ClassName()` under the sigsetjmp guard.

### UIListSlot::Fill Assert Ordering

**Root cause**: `#ifdef HX_NATIVE` bounds guard was placed AFTER `MILO_ASSERT`, so the assert fired fatally before the guard could return.

**Fix**: Moved the native guard before the assert.

### UIList mListDir Null Guards

**Root cause**: `HamList.lst` deserializes `mListDir` via `ObjPtr<UIListDir>`, but the referenced UIListDir subobject doesn't exist in the directory at deserialization time. ObjPtr resolves to null. Subsequent calls to `mListDir->CreateElements()`, `mListDir->PollWidgets()`, `mListDir->ListEntered()` crash.

**Fix**: Added `#ifdef HX_NATIVE if (!mListDir) return;` guards in `Update()`, `Poll()`, and `Enter()`.

### MILO_FAIL_DTA Macro

**Root cause**: DTA runtime errors like "property [outfit] not found" and "Data 0 is not Symbol" cascade — Property returns null → Evaluate returns sNullNode(0) → DTA calls Sym() on int 0 → type mismatch → MILO_FAIL → abort. These errors also fire on Xbox 360 debug builds (developer clicks Continue).

**Fix**: Created `MILO_FAIL_DTA` macro — `MILO_FAIL` on Xbox (identical codegen), `MILO_WARN` on native. Applied to:
- All DataNode type error messages ("Data %s is not TYPE")
- Object::Property/HandleProperty/PropertySize "property not found"
- DataArray::Execute "not function or object"

## Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `MILO_MAX_FRAMES` | 10000 | Headless frame limit |
| `MILO_RENDER` | 0 | Enable GPU rendering (1=on) |
| `MILO_HEADLESS` | 0 | Headless mode (no window, 1=on) |
| `MILO_SCREENSHOT_DIR` | (none) | Directory for auto-screenshots |
| `MILO_SCREENSHOT_FRAMES` | (none) | Comma-separated frame numbers |
| `MILO_INPUT_SCRIPT` | (none) | Path to scripted input file |
| `MILO_FATAL_FAILS` | 1 | Fatal Debug::Fail (0=continue past errors) |
| `MILO_FORCE_DRAW_PANEL` | (none) | Force-draw a specific panel (debug) |

## Test Commands

```bash
# Build
cd native/build && cmake --build . -j$(nproc)

# Quick smoke test (500 frames, no render)
MILO_MAX_FRAMES=500 ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 60 ./native/build/dc3-native

# Headless render with screenshots
MILO_RENDER=1 MILO_HEADLESS=1 MILO_MAX_FRAMES=500 \
  MILO_SCREENSHOT_DIR=/tmp/claude-1000/shots \
  MILO_SCREENSHOT_FRAMES=50,100,200,300,400 \
  ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 60 ./native/build/dc3-native

# With scripted input
cat > /tmp/claude-1000/input.txt << 'EOF'
300 start
600 confirm
900 down
1200 confirm
EOF
MILO_RENDER=1 MILO_HEADLESS=1 MILO_MAX_FRAMES=3000 \
  MILO_INPUT_SCRIPT=/tmp/claude-1000/input.txt \
  MILO_SCREENSHOT_DIR=/tmp/claude-1000/shots \
  MILO_SCREENSHOT_FRAMES=200,400,700,1000,1500,2500 \
  ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 120 ./native/build/dc3-native

# Non-fatal mode (explore past crashes)
MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=5000 \
  ASAN_OPTIONS="alloc_dealloc_mismatch=0:halt_on_error=0:detect_odr_violation=0" \
  timeout 180 ./native/build/dc3-native
```

## Files Modified (Sessions 20-21)

| File | Changes |
|------|---------|
| `src/system/os/Debug.h` | `MILO_FAIL_DTA` macro |
| `src/system/os/Debug.cpp` | `MILO_FATAL_FAILS` env var in `Debug::Fail` |
| `src/system/obj/Object.cpp` | NewObject vtable verification; MILO_FAIL_DTA for property not found |
| `src/system/obj/DataNode.cpp` | MILO_FAIL_DTA for type errors; `#ifdef HX_NATIVE` safe fallback returns |
| `src/system/obj/DataArray.cpp` | MILO_FAIL_DTA for "not function or object" |
| `src/system/obj/DataFunc.cpp` | `#ifdef HX_NATIVE` null guard in DataNew |
| `src/system/ui/UIList.cpp` | mListDir null guards in Update/Poll/Enter |
| `src/system/ui/UIListSlot.cpp` | Reorder native bounds guard before MILO_ASSERT |
| `src/system/char/Character.cpp` | MergeDraws null guard before assert |
| `native/src/platform/KinectShare_Stub.cpp` | Proper stub with constructor/handlers/propsyncs |
| `native/src/platform/Mesh_Wgpu.cpp` | Use getters for protected BaseMaterial members |

## Testing Infrastructure

### GTest Integration Tests (`native/tests/`)

| Test | What it verifies |
|------|-----------------|
| `HeadlessBootTest.BootAndRun100Frames` | Engine boots and survives 100 frames |
| `HeadlessBootTest.SurvivesMainLoop` | 2000 frames of main loop stability |
| `HeadlessBootTest.InputReplayStartButton` | Scripted input processed correctly |
| `HeadlessBootTest.LongRunStability` | 10000 frames (env-gated: `MILO_LONG_TEST=1`) |
| `Subsystems.RandomSeeded` | RNG produces varied output |
| `Subsystems.ThreadCallRoundTrip` | Async ThreadCall → callback pipeline |
| `Subsystems.TaskMgrPoll` | TaskMgr timing and poll |
| `Subsystems.LocaleInitialized` | Locale subsystem doesn't crash |
| `Subsystems.JoypadPoll` | Joypad polling doesn't crash |

Tests launch `dc3-native` as a subprocess with `MILO_FATAL_FAILS=0`. Crashes produce `CrashSummary()` with signal, assertion, last DirLoader load, and last DataNew.

### ASan Build

```bash
cmake .. -DENABLE_ASAN=ON && cmake --build .
```

Run with: `ASAN_OPTIONS="alloc_dealloc_mismatch=0:detect_odr_violation=0"`

## Key Fixes (Session 2026-03-02)

### Font Loading Bugs
- Fixed `RndFontBase::Load` / `UIFontImporter` loading path — fonts were failing to load due to missing native codepath
- Fixed `UILabel` drawing to go through `RndText::DrawShowing()` → `FontMapBase` → glyph meshes → `RndMesh::DrawShowing()`
- Still needs screenshot verification to confirm text is visible end-to-end

## Next Steps

1. **Get past tutorial screens** — Need to either stub Kinect gesture detection or add DTA override to skip tutorials
2. **Verify text renders** — Font loading bugs fixed (session 2026-03-02), needs screenshot confirmation
3. **MeshAnim stub** — Write proper stub (same pattern as KinectSharePanel) to avoid blacklisting
4. **Main menu navigation** — Once past tutorials, test list navigation with scripted input
