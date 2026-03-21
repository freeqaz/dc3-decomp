# 05 - FileMerger Async Loading Pipeline

**Date**: 2026-03-21
**Status**: Analysis complete

## Architecture Overview

### FileMerger

**Files**: `src/system/char/FileMerger.h`, `src/system/char/FileMerger.cpp`

FileMerger is an Hmx::Object that loads `.milo` files and merges their contents
into an existing ObjectDir. It inherits from `Loader::Callback` (receives load
completion notifications), `MergeFilter` (controls merge behavior per object),
and `OriginalPathable` (tracks source file paths).

Each FileMerger contains an `ObjVector<Merger>` (named slots like "song",
"venue", "viz", "outfit"). Each Merger has:
- `mSelected` (FilePath) -- the file the game wants loaded
- `mLoaded` (FilePath) -- the file currently loaded
- `loading` (FilePath) -- the file being loaded right now
- `mDir` (ObjPtr<ObjectDir>) -- target dir for merge (or owner's Dir if null)
- `mLoadedObjects` / `mLoadedSubdirs` -- tracks what was merged in (for teardown)
- `mProxy` -- if true, merges as a proxy subdir instead of flattening objects
- `mPreClear` -- if true, clears old objects at StartLoad time (before load completes)

### Select() and StartLoad()

```
FileMerger::Select(name, filePath, forceReload)
  -> FindMerger(name) -> merger.SetSelected(filePath, forceReload)
```

Select only records the desired file. No loading happens until StartLoad() fires.

```
FileMerger::StartLoad(async)
  -> StartLoadInternal(async, loading=false)
```

### StartLoadInternal() -- The Core

```cpp
bool StartLoadInternal(bool async, bool loading) {
    mAsyncLoad = async;
    mLoadingLoad = loading;

    // 1. Fire "change_files" message -- DTA type handlers translate
    //    high-level selections (outfit symbol) into concrete file paths
    //    Example: HamCharacter::OnConfigureFileMerger()
    Message("change_files", async, loading).HandleType();

    // 2. Iterate mergers, queue any that need loading
    for each merger:
        if (NeedsLoading(merger)) AppendLoader(merger);

    // 3. Sort pending list by FileMergerOrganizer priority
    mFilesPending.sort(FileMergerSort());

    // 4. If nothing pending, or already loading, or organizer owns us: return
    if (mFilesPending.empty() || mCurLoader || mOrganizer != this) return false;

    // 5. BRANCH: async vs sync
    if (async) {
        TheFileMergerOrganizer->AddFileMerger(this);  // async path
    } else {
        LaunchNextLoader();                            // sync path
        while (!mFilesPending.empty()) {
            TheLoadMgr.Poll();    // <-- BLOCKS until all mergers done
        }
    }
}
```

The sync path (`async=false`) spins in a `TheLoadMgr.Poll()` loop until all
pending files are loaded. This is intentional for initial `.milo` loading
(`PreLoad` at line 216) where the frame loop hasn't started, but is a problem
when called from inside the main loop.

### LaunchNextLoader()

Creates either a `NullLoader` (for empty paths or blocked subdirs) or a
`DirLoader` for actual `.milo` files. On native, passes `Dir()` as parent
directory to DirLoader so ObjPtr fallback resolution works during
deserialization (Xbox flattens objects into the same scope; native keeps
subdirs separate).

### FinishLoading() -- Post-Load Merge

When DirLoader finishes, it calls `FileMerger::FinishLoading()` (via the
`Loader::Callback` interface). This:

1. Calls `NotifyFileLoaded()` -- clears old merger content, fires `on_pre_merge`
2. Merges loaded dir into target using `MergeDirs()` or proxy attachment
3. On native, walks nested objects and re-registers into parent hash table
   (fixes `Find<T>` for objects in kMergeReplace'd subdirs)
4. Calls `PostMerge()` -- fires `on_post_merge`, `on_post_delete`, launches
   next pending loader if any

### change_files Message

The `change_files` message is the critical DTA hook. When FileMerger fires
`change_files`, the owning object's type handler responds:

- **world.fm**: DTA handler wires `{$hamdirector set merger $this}` and calls
  `load_game_song` to trigger the entire song/venue/character loading chain
- **HamCharacter**: `OnConfigureFileMerger()` translates outfit symbol to
  file paths (outfit .milo, VO bank, viseme)
- **modular.fm**: Resets choreography system

Without `change_files`, FileMerger doesn't know what files to load. This is
why `PreLoad` calls `StartLoadInternal(true, true)` -- the `loading=true`
flag tells type handlers that this is initial load (wiring properties).

### sDisableAll

`static bool sDisableAll` -- defined only under `#ifdef HX_NATIVE`
(FileMerger.cpp:22). Zero-initialized to `false`. When true, `FinishLoading()`
skips the `MergeDirs` call (objects are loaded but not merged). Exposed via
`SYNC_PROP(disable_all, sDisableAll)` so DTA config could set it, but in
practice it stays `false`.

Used in two places:
- `FinishLoading()` line 222: skip merge + proxy attachment
- `NotifyFileLoaded()` line 520: skip `on_pre_merge` message

## FileMergerOrganizer

**Files**: `src/system/char/FileMergerOrganizer.h`, `src/system/char/FileMergerOrganizer.cpp`

Singleton (`TheFileMergerOrganizer`). Created during `Init()`. Coordinates
async loading across multiple FileMerger instances.

### AddFileMerger()

```cpp
void AddFileMerger(FileMerger *fm) {
    if (!gOrganizing)
        fm->LaunchNextLoader();  // fallback: just start immediately
    else {
        fm->mOrganizer = this;   // take ownership of callbacks
        mOrganizedFileMergers.push_back({fm, state=0});
        if (!mActiveOrg && !mStartOrg)
            mStartOrg = new FileMergerOrganizerLoader();  // schedule start
    }
}
```

When `gOrganizing` is true (set during `Init()` after reading
`file_merger_organizer.category_order` from system config), the organizer:

1. Creates a `FileMergerOrganizerLoader` that sits in the LoadMgr queue
2. When polled, `FileMergerOrganizerLoader::PollLoading()` calls
   `TheFileMergerOrganizer->StartLoad()`
3. `StartLoad()` calls `CheckDone()` which picks the highest-priority pending
   merger and calls `Dispatch()` to launch its loader
4. Only one merger loads at a time (`mActiveOrg`)

### Callback Redirect

When a FileMerger's `mOrganizer` is set to the organizer (not `this`), the
DirLoader's completion callback goes to `FileMergerOrganizer::FinishLoading()`
instead of `FileMerger::FinishLoading()`. The organizer records the state
(`kFinishLoad`), then dispatches back to `FileMerger::FinishLoading()` when
ready.

### Priority Sorting

`FileMergerSort` uses `gCatPriority` (populated from
`file_merger_organizer.category_order` DTA config). Categories with lower
priority numbers load first. Empty paths get negative priority (load last).
Gender chirality randomizes male/female order per session.

## LoadMgr (TheLoadMgr)

**Files**: `src/system/utl/Loader.h`, `src/system/utl/Loader.cpp`

Global `TheLoadMgr`. Maintains two lists:
- `mLoaders` -- all active loaders (for lifetime tracking)
- `mLoading` -- loaders in priority order (front = next to poll)

### Poll()

```cpp
void LoadMgr::Poll() {
    if (mPeriod > 0) {
        mTimer.Restart();
        mCurrentPeriod = mPeriod;
        while (!mLoading.empty()) {
            PollFrontLoader();                      // advance front loader one step
            if (front->IsLoaded()) mLoading.pop_front();  // remove if done
            if (CheckSplit()) return;                // time budget exceeded
        }
    }
}
```

`mPeriod` defaults to 10ms. Each `Poll()` call advances loaders until the
time budget is exhausted. This is the incremental async loading mechanism.

### Where Poll() Is Called

`TheLoadMgr.Poll()` is called from `SystemPoll()` (System.cpp:225), which is
called from the main loop:
- **Desktop native**: `App::RunWithoutDebugging()` line 1018 calls `SystemPoll(false)`
- **Web**: `App::RunOneFrame()` line 633 calls `SystemPoll(false)`
- **Xbox (PPC)**: `App::RunWithoutDebugging()` line 1296 calls `SystemPoll(false)`

So yes, **TheLoadMgr.Poll() runs every frame on native**, providing the
incremental async loading tick.

### PollUntilLoaded()

Synchronous blocking variant. Spins calling `PollFrontLoader()` with infinite
time budget until the target loader is done. On web, has a 10,000-iteration
safety valve to prevent blocking the browser event loop.

### PollFrontLoader() (native vs PPC)

On native (`#ifdef HX_NATIVE`), simplified to just `mLoading.front()->PollLoading()`.
On PPC, includes `AutoGlitchReport` timing, archive debug logging, and heap tracking.

## DirLoader

**Files**: `src/system/obj/DirLoader.h`, `src/system/obj/DirLoader.cpp`

State machine that loads `.milo` files in stages:
```
OpenFile -> LoadHeader -> CreateObjects -> LoadResources -> LoadDir -> LoadObjs -> DoneLoading
```

Each state does incremental work per `PollLoading()` call. Key behaviors:

- **OpenFile**: Opens a `ChunkStream` for the `.milo` file. On web, always uses
  cached paths (`gen/foo.milo_xbox`). On desktop native, uses direct paths.
- **CreateObjects**: Reads class names from the header, calls `NewObject()` to
  instantiate each. On native, skips stub vtable objects (STUB check at line 876).
- **LoadObjs**: Calls `PreLoad`/`PostLoad` on each object. Checks `TheLoadMgr.CheckSplit()`
  to yield back to the main loop (incremental loading). Also yields if another
  loader jumped to front position.
- **Cleanup**: Calls `SyncObjects()`, fires `FinishLoading` callback, optionally
  self-deletes.

### HX_NATIVE Differences

1. **Parent dir**: Constructor accepts a 7th parameter (`ObjectDir *dir2`) stored
   as `mParentDir`. FileMerger passes `Dir()` so nested objects can resolve
   ObjPtr references against the parent scope.
2. **Stub vtable detection**: `CreateObjects()` and `LoadObjs()` check vtable
   pointers -- if null/stub, skip the object instead of crashing. This handles
   unimplemented decomp classes.
3. **WaitUntilReady**: Used instead of `while(Eof() == TempEof)` busy-wait.
   On native, all I/O is synchronous (files read fully into memory), so this
   returns immediately.

## The Complete Xbox Async Loading Chain

```
Game::LoadSong() → HamDirector::OnLoadSong()
  → mMerger->Select("song", songPath)
  → mMerger->StartLoad(async=true)
    → StartLoadInternal(async=true)
      → change_files message (DTA wires merger props)
      → AppendLoader for dirty mergers
      → TheFileMergerOrganizer->AddFileMerger(this)
        → Creates FileMergerOrganizerLoader (sits in LoadMgr queue)

Every frame: SystemPoll() → TheLoadMgr.Poll()
  → PollFrontLoader() → FileMergerOrganizerLoader::PollLoading()
    → TheFileMergerOrganizer->StartLoad() → CheckDone() → Dispatch()
      → FileMerger::LaunchNextLoader() → creates DirLoader
  → PollFrontLoader() → DirLoader::PollLoading()
    → OpenFile → LoadHeader → CreateObjects → LoadDir → LoadObjs → DoneLoading
  → DirLoader::Cleanup() → callback: FileMergerOrganizer::FinishLoading()
    → FileMergerOrganizer::Dispatch() → FileMerger::FinishLoading()
      → MergeDirs() — objects merged into target ObjectDir
      → PostMerge() → fires on_post_merge, on_post_delete
      → LaunchNextLoader() (if more pending)

on_post_delete("song") fires back to HamDirector:
  → HamDirector::OnFileLoaded("song")
    → TheHamWardrobe->LoadCharacters(async=true)
    → mMerger->Select("venue", venueDir), Select("viz", visualizer)
    → mMerger->StartLoad(async) → async chain continues
```

## The world.milo / world.fm Chain

### How world_panel loads world.milo

`world_panel` is defined in DTA config (game.dta):
```
world_panel:
  (file "../world/world.milo")
  (unload_async TRUE)
```

When `game_screen` enters, `world_panel` enters, and UIPanel::Enter() triggers
loading of `world.milo`. This is a ~3.3KB skeleton ObjectDir containing:
- `world.fm` (a FileMerger with merger slots: "song", "venue", "viz")
- Basic scaffolding objects

### How world.fm wires to HamDirector

When `world.fm` is deserialized (via DirLoader), its `PreLoad` calls
`StartLoadInternal(true, true)`. This fires the `change_files` message.
The DTA type handler for `world.fm` responds:
```
{$hamdirector set merger $this}   -- wires HamDirector.mMerger = world.fm
```

This is how `HamDirector.mMerger` gets set. The `SYNC_PROP(merger, mMerger)`
in HamDirector.cpp:217 is the property that the DTA `set` command writes to.

After `change_files`, if the DTA handler also called `load_game_song` (which
calls `Select("song", ...)` + `StartLoad(true)`), the song milo starts
loading asynchronously.

### HamDirector.mMerger vs world.fm

`HamDirector.mMerger` is an `ObjPtr<FileMerger>` that points to `world.fm`.
`HamDirector::GetWorld()` returns `mMerger->Dir()` -- the ObjectDir that
world.fm lives in (the world ObjectDir loaded from world.milo). This is the
directory where venue, song, viz content gets merged into.

`mMerger->Select("venue", path)` tells world.fm to load a venue `.milo`
(e.g., `world/glitterati/glitterati.milo`) and merge it into the world dir.

## Async Loading on Native

### Is TheLoadMgr.Poll() called? YES

`SystemPoll(false)` calls `TheLoadMgr.Poll()` every frame. This is called
from:
- `App::RunWithoutDebugging()` (desktop, line 1018)
- `App::RunOneFrame()` (web, line 633)

The async loading tick runs every frame on native.

### Does the organizer work? YES (conditionally)

`TheFileMergerOrganizer` is created during `FileMergerOrganizer::Init()`,
which is called during system initialization. The `gOrganizing` flag is set
to `true` if `file_merger_organizer.category_order` exists in system config.
When `gOrganizing` is true, `AddFileMerger` creates a
`FileMergerOrganizerLoader` that gets processed through the normal LoadMgr
polling.

### Does DirLoader work on native? YES

DirLoader uses `ChunkStream` for file I/O. On native, all file I/O is
synchronous (files are fully read into memory). `ChunkStream::Eof()` returns
`NotEof` immediately (no async I/O to wait for). The DirLoader state machine
advances through all stages in a single or small number of `PollLoading()` calls.

The `CheckSplit()` mechanism still yields periodically (10ms budget), so even
on native, large `.milo` files don't block the main loop for more than 10ms
per frame.

### Emscripten-Specific Handling

The sync-load-hang doc (2026-03-20) mentions an Emscripten override:
```cpp
#ifdef __EMSCRIPTEN__
    async = true; // Browser event loop can't block in Poll() loop
#endif
```

**This block does NOT currently exist in FileMerger.cpp.** It was either
proposed but not implemented, or removed. The current code has no
`__EMSCRIPTEN__` or `HX_WEB` blocks in FileMerger.cpp at all.

On web, the safety is in `LoadMgr::PollUntilLoaded()` which has a
`HX_WEB` max-iteration guard (10,000 iterations). Also,
`BinStream::WaitUntilReady()` on Emscripten bails immediately since MEMFS
data is always ready.

## The Sync Load Hang

**Document**: `docs/sessions/2026-03-20-sync-load-hang.md`
**Status**: Diagnosed, not yet fixed

### Root Cause

`NativeVenueInit()` (Rnd_Wgpu.cpp:819) runs inside `BeginDrawing()` every
frame. It calls `venue->Enter()` or `TheHamDirector->VenueEnter(venue)`.
Previously, it also called `HamCharacter::StartLoad(false)` (synchronous),
which triggered `FileMerger::StartLoadInternal(false, false)` with the
blocking `while (!mFilesPending.empty()) { TheLoadMgr.Poll(); }` loop.

This inner loop:
- Does NOT increment `frameCount`
- Does NOT check `MILO_MAX_FRAMES`
- Does NOT process scripted input
- Does NOT call `TheUI->Poll()`, `TheTaskMgr.Poll()`, or `TheFlowMgr->Poll()`

If loaded files trigger further synchronous loads, the loop chains indefinitely.

### Current State (2026-03-21)

The current `NativeVenueInit()` code (Rnd_Wgpu.cpp:819-866) no longer does
synchronous character loading. The comment at line 863-865 says:
```
// Character outfits and animations are handled by the DTA wardrobe flow
// (HamWardrobe::LoadCharacters → StartLoad(true) → async FileMerger).
// No manual outfit/clip setup needed here.
```

The sync load hang was addressed by removing the synchronous
`StartLoad(false)` calls from `NativeVenueInit()` and relying on the DTA
wardrobe flow (async). However, the session doc's status says "Diagnosed, not
yet fixed" -- the broader issue (any code path calling `StartLoad(false)` or
`PollUntilLoaded` from inside the main loop blocks) is not fully resolved.

### Remaining Sync Load Sites in Main Loop Context

Code paths that call sync loading which could block if triggered from inside
the main loop:

1. **`ObjectDir::SetProxyFile()`** (Dir.cpp:402) -- `PollUntilLoaded`
2. **`ObjectDir::PostLoad()`** (Dir.cpp:421) -- `PollUntilLoaded` for subdir loads
3. **`UIPanel::CheckIsLoaded()`** (UIPanel.cpp:175) -- `PollUntilLoaded`
4. **`UIPanel::FinishLoad()`** (UIPanel.cpp:356) -- `PollUntilLoaded`
5. **`RndTex::SetBitmap()`** (Tex.cpp:414) -- `PollUntilLoaded`
6. **`LightHue::Sync()`** (LightHue.cpp:103) -- `PollUntilLoaded`
7. **`UI::IncFrame()`** (UI.cpp:959) -- `PollUntilLoaded`
8. **`DirLoader::LoadObjects()`** (DirLoader.cpp:1029) -- `PollUntilLoaded`

These are all Xbox patterns where the platform supports true async I/O (disc
reads return `TempEof` until data arrives). On native, file I/O is synchronous,
so `PollUntilLoaded` returns quickly -- BUT if the load triggers `FinishLoading`
callbacks that queue more sync loads, it can cascade.

## Is the Async FileMerger Pipeline Functional on Native?

**Yes, the core async pipeline works end-to-end on native.** The components are:

1. `TheLoadMgr.Poll()` runs every frame via `SystemPoll()` -- WORKING
2. `FileMergerOrganizer` queues and prioritizes async loads -- WORKING
3. `DirLoader` loads `.milo` files incrementally -- WORKING (native I/O is sync
   but DirLoader yields via `CheckSplit()`)
4. `FileMerger::FinishLoading()` merges objects into target dir -- WORKING
   (with native-specific hash table fixup for nested objects)
5. `change_files` message fires type handlers for property wiring -- WORKING

### Blockers for Full Gameplay Flow

The async pipeline itself is functional, but the **DTA-driven venue/character
loading chain** has gaps:

1. **No `game_screen` navigation**: The native port doesn't navigate to
   `game_screen` during normal flow. Without `game_screen`, `world_panel`
   never loads, `HamDirector.mMerger` never gets wired, and the Xbox loading
   chain never fires. (See Gap 1 in `docs/sessions/2026-03-20-dta-venue-flow-convergence.md`)

2. **`NativeVenueInit()` bypass**: The native port uses a manual `venue->Enter()`
   call from `BeginDrawing()` instead of the DTA-driven
   `HamDirector::Enter()` -> `VenueEnter()`. This works for rendering a static
   venue but skips choreography, camera, and post-processing setup.

3. **Missing DTA player selection flow**: On Xbox, outfit/crew data is
   populated through DTA screen transitions before `OnLoadSong()`. The native
   port's `DC3_SCREEN=game_screen` auto-nav may not set all required
   `HamPlayerData` fields. (See Gap 2 in convergence doc)

4. **Sync `PreLoad` is unavoidable**: `FileMerger::PreLoad()` (line 216)
   calls `StartLoadInternal(true, true)` -- the `loading=true` variant. This
   fires during `.milo` deserialization and is inherently synchronous (the
   loader cannot return partial results during deserialization). This is fine
   because `PreLoad` runs inside the DirLoader state machine which already
   blocks the loading pipeline. It's not a hang risk.

5. **Web stack overflow risk**: Deep cascade destruction during `PostMerge` ->
   old character deletion -> recursive `~ObjectDir` can overflow the 4MB WASM
   stack. The Emscripten async override (mentioned in the sync-load-hang doc)
   was proposed but is not in the current source.

### Summary

| Component | Status | Notes |
|-----------|--------|-------|
| `TheLoadMgr.Poll()` per frame | Working | Called from `SystemPoll()` |
| `FileMergerOrganizer` async queue | Working | Priority sorting, one-at-a-time dispatch |
| `DirLoader` incremental load | Working | Sync I/O on native but yields via `CheckSplit()` |
| `FileMerger::FinishLoading()` merge | Working | Native hash-table fixup for nested objects |
| `change_files` DTA wiring | Working | Type handlers fire correctly |
| `sDisableAll` | Non-issue | Always `false` |
| `game_screen` DTA flow | **NOT WORKING** | Native never navigates to gameplay |
| `HamDirector.mMerger` wiring | **NOT WORKING** | Requires `world_panel` → `world.milo` → `change_files` |
| Character async loading | **WORKING** (when triggered) | `HamWardrobe::LoadCharacters(async=true)` → `FileMergerOrganizer` |
| Sync load hang | **MITIGATED** | `NativeVenueInit` no longer does sync loads; broader issue remains |
| Web Emscripten safety | **PARTIAL** | `PollUntilLoaded` has iteration limit; no `FileMerger` async override |
