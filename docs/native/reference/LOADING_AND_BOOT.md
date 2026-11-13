# Loading and Boot

How DC3 boots, how screens advance to gameplay, and how a song's world, venue and HUD are loaded through FileMergers. This covers both the image and the native port.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

DTA paths below are relative to `orig-assets/extracted/`. Source paths are relative to the repo root.

## 1. Boot sequence

`App::App` (`src/App.cpp`) runs `SystemPreInit(argc, argv, "config/ham_preinit_keep.dta")` and then `SystemInit("config/ham_keep.dta")` on both platforms. The two platforms then take different branches:

- **Native:** the `#ifdef HX_NATIVE` branch of the constructor, which starts at `src/App.cpp:240`.
- **Xbox:** the `#else` branch at `src/App.cpp:581`. Its work never happens on native:
  - Splash screens (`ui/splash/*/esrb_keep.milo`, `ui/splash/harmonix_keep.milo`).
  - `LiveCameraInput::PreInit()` / `Init()`.
  - The Kinect guide thread.
- **Web extra:** before `SystemInit`, the web build calls `DirLoader::SetCacheMode(true)`. Without it, `metamaterials.milo` would not resolve to its `gen/*.milo_xbox` path.

`UIManager::Init` broadcasts the `init` message (`src/system/ui/UI.cpp`, `static Message init_msg("init")`), and the DTA handlers from the system config respond to it. From that point on, screens are UIScreen objects whose behaviour comes from DTA: their `enter`/`exit` handlers, NAV_SELECT handling, and calls to `{ui goto_screen ...}`.

## 2. The per-frame loop

On native, the image's `App::RunWithoutDebugging` loop body is reproduced call-for-call by `Dc3NativeFramePoll()` in `src/App.cpp`. Two loops share it:

- the desktop/headless loop (`App::Run()` → `RunWithoutDebugging()`);
- the web loop, `App::RunOneFrame()` (`src/App.cpp:898`). It is driven from the `BOOT_RUNNING` state in `native/src/main_web.cpp`.

The comment block above `Dc3NativeFramePoll` lists the image's poll order and gives the target-listing evidence for each call.

The web build adds a boot state machine in `native/src/main_web.cpp` in front of the shared loop:

| State | What it does |
|---|---|
| `BOOT_INIT` | Create the MEMFS directories and start the bundle download. |
| `BOOT_FETCHING` | Poll until the download is complete. |
| `BOOT_ENGINE_INIT` | Run the `App` constructor (the same one desktop uses). |
| `BOOT_GPU_WAIT` | Wait for the async WebGPU adapter and device. |
| `BOOT_GPU_READY` | Create GPU resources. |
| `BOOT_RUNNING` | Call `App::RunOneFrame()` once per frame. |

## 3. Screen chain: boot to gameplay

Each screen named here is defined in the extracted DTA; the transitions are abbreviated.

```
attract_screen                    ui/main/main.dta
  -> autosave_warning_screen      ui/title/autosave_warning.dta
    -> title_screen               ui/title/title.dta
      -> wait_main_after_saveload_screen   (title NAV_SELECT sets $post_load_dest_screen)
        -> $post_load_dest_screen         (saveload_complete: {ui goto_screen $post_load_dest_screen})
           normally main_screen   ui/main/main.dta; first run may route through tutorial screens
          -> choose_mode_screen   ui/choose_mode/choose_mode.dta
            -> song_select_screen ui/song_select/song_select.dta
              -> {gamemode get ready_screen}, e.g. multiuser_screen (ui/multiuser/multiuser.dta)
                -> loading_screen ({enter_gameplay}, defined in ui/global.dta)
                  -> preloading_screen -> real_loading_screen (on_preload_ok)   ui/loading/loading.dta
                    -> game_screen                                              ui/game.dta
```

For the same chain annotated with each DTA handler, see [convergence/02 §8](../../sessions/convergence/02-boot-to-gameplay-flow.md). That is a 2026-03 session document: its "Native" column describes `DC3_SCREEN` auto-navigation, which no longer exists.

### How native differs today

- **Boot auto-advance only happens with `DC3_FAST_BOOT=1`** (the web equivalent is the `?fast_boot=1` URL parameter).
  - The `HX_NATIVE` block in `UIManager::Poll` (`src/system/ui/UI.cpp`, the `sBoot[]` table) advances these screens, and it runs only when fast boot is on:
    - `attract_screen` → `autosave_warning_screen` → `title_screen` → `wait_main_after_saveload_screen` → `main_screen`;
    - the Kinect tutorial screens → `main_screen`.
  - Each step waits at most 10 frames. `title_screen` keeps its full delay so a scripted confirm can still land.
  - When it skips `title_screen`, it sets `$post_load_dest_screen` to `main_screen`, standing in for the title screen's own NAV_SELECT.
  - Without fast boot the advance loop does not run, and the screens advance through their own DTA.
  - From `main_screen` on, the flow is driven by DTA and input on both platforms.
- **The legacy native panel-flow activation is off by default.**
  - `PanelDir::Enter` (`src/system/ui/PanelDir.cpp`) keeps a native-only synthetic flow activation, but the default filter mode is `none`, so `ShouldActivateNativeFlow` rejects everything.
  - `MILO_NATIVE_FLOW_FILTER=curated|all|menu_only` is kept only as an escape hatch.
  - Flows with `startMode > 0` start through `Flow::Enter()`; flows with `startMode == 0` are fired by DTA enter scripts, which run on native.
- **`MILO_DEBUG_UI_FLOW`** prints the boot-advance and transition probes (`DebugUIFlow()` in `UI.cpp`).

## 4. World, song, venue and HUD loading (FileMerger)

### Where the mergers live

`game_screen` lists `world_panel` among its panels (`ui/game.dta`). `world_panel` is declared with `(file "../world/world.milo")`, which is a small ObjectDir shipped as `world/gen/world.milo_xbox`. It holds three FileMergers, each with a `type` that selects a `change_files` handler in `char/char_objects.dta`:

| Merger | type | Handler effect |
|---|---|---|
| `world.fm` | `world` | `{$hamdirector set merger $this}`, then `{$hamdirector load_game_song FALSE}` if `game_panel` exists |
| `modular.fm` | `modular` | `{$hamdirector set move_merger $this}` |
| `GameModeMerger.fm` | `game_mode` | `{$hamdirector set game_mode_merger $this}` |

A venue `.milo` carries its own crowd-animation merger, type `crowd_anim` in `char/char_objects.dta`.

### The cascade

```
world_panel loads world.milo
  FileMerger::PreLoad -> StartLoadInternal(...) fires "change_files"
    world type:     HamDirector::mMerger = world.fm, then load_game_song
    modular type:   HamDirector::mMoveMerger = modular.fm
    game_mode type: HamDirector::mGameModeMerger = GameModeMerger.fm
HamDirector::OnLoadSong        mMerger->Select("song", <song>, true)
HamDirector::OnFileLoaded("song")
    TheHamWardrobe->LoadCharacters(outfits, crews, backups, speed, venue, async)
    mMerger->Select("viz",   "ui/visualizer/visualizer.milo")
    mMerger->Select("venue", GetVenuePath(venue))
    mGameModeMerger: HandleType(load_game_hud), StartLoad(async)
    mMerger->StartLoad(async)
HamDirector::OnFileLoaded("venue")  mVenue      = dynamic_cast<WorldDir*>(dir)
HamDirector::OnFileLoaded("viz")    mVisualizer = dynamic_cast<HamVisDir*>(dir)
HamDirector::IsWorldLoaded()   mVenue && mMerger && mMoveMerger, neither merger has pending files
```

`HamDirector::GetWorld()` returns `dynamic_cast<WorldDir*>(mMerger->Dir())` and gives `nullptr` until `mMerger` is wired. All of these functions are in `src/system/hamobj/HamDirector.cpp`. `FileMerger::PreLoad` and `StartLoadInternal` are in `src/system/char/FileMerger.cpp`.

Native runs this cascade as the image does. Of the `HX_NATIVE` blocks still in `FileMerger.cpp`:

- One drains its loaded-object lists by popping the front node *before* deleting each object, so the loop cannot spin on a destructor that does not erase its own node.
- One adds `FileMerger::ForceReleaseOrganizer()`, which tests use to drain `TheLoadMgr` without a game loop.

### Object lookup during loading

`ObjectDir::FindObject` (`src/system/obj/Dir.cpp`) searches only this directory and its subdirectories, then `Dir()` and `sMainDir` when `parentDirs` is set, exactly as the image does.

A 2026-03 native fallback that resolved names through the loader's proxy or parent directory was **removed on 2026-09-30**; the comment at the end of `FindObject` explains why. Callers that need a loading proxy's directory now ask for it explicitly, for example FlowPtr via `FlowPtrGetLoadingDir`.

`DirLoader::ParentDir()` / `SetParentDir()` still exist in `src/system/obj/DirLoader.h`, but nothing outside that header calls them.

## 5. Loading lessons that still apply

- **An unregistered object factory desyncs the stream.**
  - When a class has no registered factory, its entry loads as NULL. `ReadDead` then skips by guesswork and every later read in that `.milo` is corrupt.
  - The symptom is far from the cause. See [PORTING.md](PORTING.md#missing-factory-registration) for the "String chars N > 512" tell.
- **Do not add members to `Hmx::Object` or other root classes for native-only state.** It shifts every multiple-inheritance this-adjustment and the PPC layout. Use external storage; `DirPtrRefCounts()` in `src/system/obj/Dir.h` is a static map for exactly this reason.
- **Virtual dispatch is unreliable during construction.** A base-class `AddRef`/`Release` cannot ask `IsDirPtr()`, because the derived vtable is not installed yet.
- **ObjRef ring walks are linked-list traversals.** Anything that walks the ring once per element is quadratic; cache the result or keep a count.

## Sources

- [archive: native/FILEMERGER_CONVERGENCE.md](../../archive/2026-11-13-native-doc-audit/native/FILEMERGER_CONVERGENCE.md), §Architecture
- [archive: native/DTA_LOADING_BLOCKER.md](../../archive/2026-11-13-native-doc-audit/native/DTA_LOADING_BLOCKER.md), §Architecture
- [archive: plans/dc3-native/PLATFORM_HACKS_ANALYSIS.md](../../archive/2026-11-13-native-doc-audit/plans/dc3-native/PLATFORM_HACKS_ANALYSIS.md), §Reference: Xbox DTA Screen Flow
- [archive: plans/web-port/UNIFY_WITH_APP.md](../../archive/2026-11-13-native-doc-audit/plans/web-port/UNIFY_WITH_APP.md)
- [sessions/convergence/05-filemerger-async-pipeline.md](../../sessions/convergence/05-filemerger-async-pipeline.md): a long walk-through of `FileMerger`, `FileMergerOrganizer`, `LoadMgr` and `DirLoader` (2026-03 background; its native-hack sections are history)
- [sessions/convergence/02-boot-to-gameplay-flow.md](../../sessions/convergence/02-boot-to-gameplay-flow.md)
- [sessions/NATIVE_SUBDIR_LOADING_FIX.md](../../sessions/NATIVE_SUBDIR_LOADING_FIX.md), §Key Lessons
