# Platform Hacks Analysis

**Last Updated**: 2026-03-16
**Purpose**: Comprehensive audit of all `#ifdef HX_NATIVE` guards, categorized by root cause with actionable upstream fix recommendations for future agents.

## Summary

298 files contain `#ifdef HX_NATIVE` guards. Most are legitimate platform stubs (Xbox intrinsics, XInput, Bink SDK). **8 gameplay-altering hacks** cover upstream issues that could potentially be fixed.

---

## Category 1: Gameplay Hacks (Upstream Fixable)

### 1.1 DTA Animation Completion (CRITICAL)

**Files**: `src/system/rndobj/Anim.cpp:426-434`, `src/system/hamobj/HamNavList.cpp:505-509,1522-1526`

**Problem**: When AnimTask finishes playing, it sends `on_anim_event("ended")` to its listener. The listener's `Handle()` dispatches to DTA typedef handlers via `HANDLE_ARRAY(mTypeDef)`, which should call `StopAnimation()`. On native, these DTA handlers never execute, so `IsAnimating()` stays true forever.

**Current Hack**:
- `Anim.cpp`: Auto-null `mAnimTarget` when non-looping animation exceeds `mFrameSpan`
- `HamNavList.cpp`: Skip `IsAnimating()` check in kRibbonSelect completion

**Root Cause Investigation** (see [DTA_HANDLER_ANALYSIS.md](DTA_HANDLER_ANALYSIS.md)):
- The execution path: `AnimTask::Poll()` → `mListener->Handle(msg)` → `HANDLE_ARRAY(mTypeDef)` → `ExecuteScript()`
- Likely break point: `mTypeDef` is null (type definitions not loaded from DTA), OR script functions referenced in handlers are not registered
- **Missing Init calls** on native (see Section 2.3) mean DataRegisterFunc calls from `ContextCheckerInit()` and others never run, so DTA scripts referencing those functions fail silently

**Upstream Fix Path**:
1. Ensure `mTypeDef` is populated for AnimTask listeners during .milo object loading
2. Register all DTA script functions that handlers reference (see missing Init calls below)
3. Verify `ExecuteScript()` doesn't silently fail when encountering unregistered functions

**User Impact**: Menu selections hang in kRibbonSelect animation forever without the hack.

---

### 1.2 Audio Initialization Timeout (HIGH)

**File**: `src/lazer/game/Game.cpp:805-825`

**Problem**: Xbox's synchronous DTA script `load_new_song` fires before `PollForLoading()` reaches the audio readiness check. On native, async file I/O means the DTA script may not execute in time.

**Current Hack**: Force-initiate audio loading with `LoadNewSongAudio(song)`, poll for 120 frames, then bypass the wait.

**Root Cause**: Architectural difference — Xbox has synchronous DTA script execution during load, native has async file I/O.

**Upstream Fix Path**:
1. Make `Game::PollForLoading()` explicitly call `LoadNewSongAudio()` if audio isn't initiated yet (remove dependency on DTA script timing)
2. This is actually the more robust pattern — don't rely on script execution ordering for critical load sequencing

**User Impact**: Without hack, game hangs at loading screen forever. With hack, gameplay proceeds; audio continues loading in background.

---

### 1.3 Audio Thread Safety (SHOULD UPSTREAM)

**File**: `src/lazer/game/Game.cpp:297-310`

**Problem**: `Game::Restart()` calls `StopAllSounds()` which destroys `MoggClip` objects and frees ring buffers. On Xbox, audio callback runs on main thread (or exclusive lock). On native/web, audio callback runs on a **separate thread** (miniaudio/AudioWorklet) and may read freed memory.

**Current Hack**: `AudioDevice::Suspend()` before destroy, `Resume()` after.

**This is a real bug**, not a platform difference. Any threaded audio platform needs this.

**Upstream Fix**: Add `#ifdef HX_THREADED_AUDIO` (or runtime check) and always suspend before audio object destruction. This should be the default for any non-Xbox platform.

**User Impact**: Without hack, random crashes/audio corruption during song restart.

---

### 1.4 Load State Reset After Sync Destroy (SHOULD UPSTREAM)

**File**: `src/lazer/game/Game.cpp:315-321`

**Problem**: `StopAllSounds()` synchronously destroys MOGG streams on native (Xbox uses deferred cleanup). After restart, `mLoadState=3` (loaded) but stream objects are gone. `IsLoaded()` doesn't re-poll `HamAudio::IsReady()`.

**Current Hack**: Reset `mLoadState = 0` to force the load state machine to re-run.

**Root Cause**: Stream lifecycle isn't abstracted — `KillStream()` behaves differently on native vs Xbox.

**Upstream Fix**: `MoggClip::KillStream()` should reset the owning load state, or the load state machine should detect destroyed streams.

**User Impact**: Without hack, game restart hangs or crashes when accessing dead stream objects.

---

### 1.5 MoveDir Null Safety (ACCEPTABLE — 6 instances)

**Files**: `src/lazer/game/Game.cpp:218,266,274,284,503,856`

**Problem**: Xbox loads venue/moves.milo synchronously before gameplay. Native/web may not have MoveDir assets.

**Current Hack**: Null-check `mMoveDir` before every use.

**This is acceptable** — DC3 native doesn't support gesture detection anyway. MoveDir is optional for spectator/demo mode.

**Upstream Fix (optional)**: Make MoveDir explicitly optional in the Game state machine — replace asserts with early returns.

---

### 1.6 MoveGraph Loading Skip (ACCEPTABLE)

**Files**: `src/lazer/game/Game.cpp:578-584,706-708,722-731,763-768,787-791`

**Problem**: `use_movegraph` property triggers MoveGraph asset loading, which requires files that may not exist on native.

**Current Hack**: Never set `mUseMoveGraph = true` on native.

**This is acceptable** — MoveGraph is Kinect-specific. No gesture detection = no move graph needed.

---

### 1.7 MoveDir Async Loading (ACCEPTABLE)

**Files**: `src/lazer/game/Game.cpp:950-963`

**Problem**: `HandleWait()` state 5 waits for MoveDir to appear in the venue world. On native, async loading means it may not exist yet.

**Current Hack**: Use non-creating `Find("moves", false)` and skip if null.

**This is acceptable** — covered by MoveDir null safety (1.5).

---

## Category 2: Missing System Initialization

### 2.1 Init Calls: Native vs PPC Comparison

The native `App.cpp` HX_NATIVE block (lines 233-346) calls a subset of the PPC init functions (lines 402-540). **Missing on native**:

| Missing Init Call | What It Does | Impact |
|---|---|---|
| `MidiParser::Init()` | `REGISTER_OBJ_FACTORY(MidiParser)` | MidiParser objects fail to deserialize |
| `SaveLoadManager::Init()` | Registers save/load factories | Save/load system non-functional |
| `ContextCheckerInit()` | `DataRegisterFunc()` for 5+ DTA functions | DTA scripts calling these functions fail |
| `MoveMgr::Init(0)` | Registers MoveMgr factories | Move system non-functional |
| `AccomplishmentManager::Init()` | Registers accomplishment factories | Achievement system non-functional |
| `MetagameRank::Init()` | Registers metagame factories | XP/level system non-functional |
| `MiniGameMgr::Init()` | Registers minigame factories | Minigame system non-functional |
| `DirLoader::SetPathEvalCallback()` | Path resolution for content loading | Content paths may not resolve |
| `TheServer.Init()` / `TheRockCentral.Init()` | Network/leaderboard | Not needed for offline play |
| `HamUserMgrInit(false)` | User/profile management | Profile system non-functional |
| `FixedSizeSaveable::Init()` | Save data format registration | Save system non-functional |

### 2.2 Impact Assessment

**High impact** (likely causes DTA handler failures):
- `ContextCheckerInit()` — DTA scripts reference `random_context` etc. Missing = silent failure
- `MidiParser::Init()` — MidiParser objects in .milo files can't be created

**Medium impact** (gameplay features don't work):
- `SaveLoadManager::Init()`, `HamUserMgrInit()` — save/load non-functional
- `AccomplishmentManager::Init()`, `MetagameRank::Init()` — progression non-functional

**Low impact** (not needed for demo):
- `TheServer.Init()`, `TheRockCentral.Init()` — online features
- `MiniGameMgr::Init()` — minigames not targeted

### 2.3 Recommendation

For future agents: **Incrementally add Init calls to the native path**. Start with `ContextCheckerInit()` (DTA function registration) and `MidiParser::Init()` (object factory). These may unblock DTA handler execution, which would eliminate the need for hack 1.1.

Each Init call may pull in new dependencies that don't compile on native. Use weak stubs for unimplemented functions rather than skipping entire Init calls.

---

## Category 3: LP64 Pointer Size Fixes (Required)

These are **correct and necessary** for the 64-bit native port.

### 3.1 HamListRibbonDrawState (HamListRibbon.h:20-24)

```cpp
#ifdef HX_NATIVE
    UIListElementDrawState *mElemDrawState; // LP64: pointer is 8 bytes
#else
    unsigned int mElemDrawState;             // ILP32: int == pointer (4 bytes)
#endif
```

On Xbox 360 (ILP32), `sizeof(int) == sizeof(void*) == 4`. On x86_64 (LP64), pointers are 8 bytes. Without this fix: pointer truncation → SIGSEGV.

### 3.2 Other LP64 Fixes

26+ LP64 fixes documented in `docs/native/NATIVE_PORT_STATUS.md`. All are correct and required.

---

## Category 4: STL Container Differences (Required)

Xbox uses STLport. Native uses libstdc++/libc++. Iterator internals differ.

### 4.1 Vector Data Access (Ribbon.cpp, CharEyes.cpp)

```cpp
#ifdef HX_NATIVE
    RndMesh::Face *facePtr = faces.data();    // libstdc++: .data() returns T*
#else
    RndMesh::Face *facePtr = faces.begin();   // STLport: begin() returns T*
#endif
```

STLport's `vector::begin()` returns a raw pointer. libstdc++ returns a `__normal_iterator` wrapper. These guards are **correct and necessary**.

### 4.2 Algorithm Compatibility (msvc_compat.h)

`std::random_shuffle` and `std::mem_fun` removed in C++17. Compat shims provided, guarded for libstdc++ >= 15 and libc++.

---

## Category 5: Rendering Platform Differences (Expected)

### 5.1 Crowd Direct Rendering (Crowd.cpp:666-681)

Xbox: Render character → impostor texture → billboard quad. Native: Draw 3D characters directly.

**Note**: DC3 doesn't use WorldCrowd (see PHASE_C_WORLDCROWD.md). This code path is never hit.

### 5.2 PowerPC Intrinsics (Crowd.cpp:27-31, various)

`__fsel()` (FPU select) replaced with ternary. Expected and correct.

---

## Category 6: Debug/Diagnostic Output (Expected)

Various `fprintf(stderr, ...)` under `HX_NATIVE` for debugging. No gameplay impact. These are fine.

---

## Priority Action Items for Future Agents

1. **Add `ContextCheckerInit()` to native init path** — may fix DTA handler execution (unlocks hack 1.1 removal)
2. **Add `MidiParser::Init()` to native init path** — enables MidiParser object deserialization
3. **Upstream audio thread safety** (hack 1.3) — real bug, not platform difference
4. **Upstream load state reset** (hack 1.4) — decomp gap in stream lifecycle
5. **Test DTA handler execution** after adding Init calls — if `mTypeDef` populates correctly and handlers fire, remove hacks 1.1 and 1.2
