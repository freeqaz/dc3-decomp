# Synchronous Load Hang: Process Never Exits

**Date**: 2026-03-20
**Status**: Diagnosed, not yet fixed
**Affects**: Desktop native (headless and windowed), web (contributes to stack overflow)

## Symptom

`MILO_MAX_FRAMES=2500` never triggers. The process runs indefinitely and must be killed externally (`timeout`, Ctrl-C). In 15 seconds of wall time, only ~800 frames advance. The process stops producing frame-related log output after entering `song_select_screen`.

## Root Cause

The native main loop in `App::RunWithoutDebugging` (App.cpp:1017) has `frameCount++` at line 1160 — after `TheRnd.BeginDrawing()`. Several code paths inside the loop body call `FileMerger::StartLoadInternal(false, ...)`, which spins in a **nested `LoadMgr::Poll()` loop** that never returns to the main loop:

```cpp
// FileMerger.cpp:502-506
if (async) {
    TheFileMergerOrganizer->AddFileMerger(this);
} else {
    LaunchNextLoader();
    while (!mFilesPending.empty()) {
        TheLoadMgr.Poll();       // ← nested poll loop, never increments frameCount
    }
}
```

This inner loop:
- Does NOT increment `frameCount`
- Does NOT check `MILO_MAX_FRAMES`
- Does NOT process scripted input (`MILO_INPUT_SCRIPT`)
- Does NOT call `TheUI->Poll()`, `TheTaskMgr.Poll()`, or `TheFlowMgr->Poll()`

If loaded files trigger further synchronous loads (e.g., `FinishLoading` → `PostMerge` → `StartLoad(false)`), the nested loop chains indefinitely.

## Primary Blocking Path

```
App::RunWithoutDebugging (main loop, frameCount=N)
  └─ TheRnd.BeginDrawing()
       └─ NativeVenueInit()                           [Rnd_Wgpu.cpp:934/958]
            └─ HamCharacter::StartLoad(false)          [Rnd_Wgpu.cpp:881]
                 └─ FileMerger::StartLoadInternal(false, false)
                      └─ while (!mFilesPending.empty())  ← BLOCKS HERE
                           TheLoadMgr.Poll()
                           // Poll → PollFrontLoader → DirLoader::PollLoading
                           //   → FinishLoading → PostMerge → may queue more loads
```

`NativeVenueInit()` (Rnd_Wgpu.cpp:819) runs inside `BeginDrawing()` on every frame until the venue is initialized. It synchronously loads character outfits:

```cpp
// Rnd_Wgpu.cpp:881
it->StartLoad(false);  // synchronous — blocks until all mergers finish
```

This runs in BOTH the no-GPU path (line 934) and the GPU path (line 958), so `MILO_NORENDER` does not prevent the hang.

## Additional Blocking Sites

`NativeVenueInit` is the known blocker, but any code path in the main loop body that calls `StartLoad(false)` or `LoadMgr::PollUntilLoaded()` will block similarly. The `song_select_screen` DTA enter handler likely triggers song data loads that also block — the process reaches `song_select_screen` but never advances past it.

## What Xbox Does Differently

On Xbox, character loading is async. The DTA wardrobe flow calls `StartLoad(true)`, which queues the merger via `TheFileMergerOrganizer->AddFileMerger(this)`. The organizer processes mergers incrementally through the normal `LoadMgr::Poll()` in the main loop. Characters appear gradually as meshes stream in.

The synchronous path (`async=false`) is only used during initial `.milo` loading (`FileMerger::PreLoad` at line 216), where blocking is expected and the frame loop hasn't started yet.

`NativeVenueInit` forces `async=false` because it needs character meshes immediately for rendering. This is a native-port-specific pattern that doesn't exist on Xbox — the Xbox venue enter flow is entirely DTA-driven and async.

## Relationship to Web Stack Overflow

The web build overrides `async=false` to `true` for Emscripten (FileMerger.cpp:471):

```cpp
#ifdef __EMSCRIPTEN__
    async = true; // Browser event loop can't block in Poll() loop
#endif
```

But the `NativeVenueInit` call happens inside `BeginDrawing()`, which runs in the `emscripten_set_main_loop` callback. If the venue loading triggers deep recursive destruction (cascade ObjectDir teardown), the fixed 4MB WASM stack overflows. The stack overflow error the user sees:

```
Aborted(stack overflow (Attempt to set SP to 0xffffffc0, with stack limits [0x00000000 - 0x00400000]))
```

...is likely from the cascade destruction path during `NativeVenueInit` → character loading → `PostMerge` → old character deletion → recursive `~ObjectDir`.

## Test Evidence

```bash
# Only ~800 frames in 15 seconds, then silent:
MILO_HEADLESS=1 MILO_NORENDER=1 MILO_MAX_FRAMES=2500 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  MILO_FATAL_FAILS=0 DC3_DATA=orig-assets \
  timeout 15 native/build/dc3-native 2>&1 | grep 'Frame [0-9]' | tail -3

# Output:
# DC3 Input: Frame 500 — scripted buttons 0x40
# DC3 Render: Frame 600 — 0 mesh draw calls
# DC3 Input: Frame 800 — scripted buttons 0x40
```

Last screen transition before silence: `song_select_screen Enter (from 'choose_mode_screen')`.

## Key Files

| File | Relevance |
|------|-----------|
| `src/App.cpp:1017-1297` | Main loop, `frameCount`, `MILO_MAX_FRAMES` check |
| `native/src/platform/Rnd_Wgpu.cpp:819-884` | `NativeVenueInit`, synchronous `StartLoad(false)` |
| `src/system/char/FileMerger.cpp:467-510` | `StartLoadInternal`, the blocking `while` loop |
| `src/system/char/FileMerger.cpp:219-300` | `FinishLoading`/`PostMerge`, can trigger cascading loads |
| `src/system/utl/Loader.cpp` | `LoadMgr::Poll`, `PollUntilLoaded` |
