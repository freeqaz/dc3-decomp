# Native Port Parity Audit — 2026-03-22

## Scope

Full audit of remaining hacks, stubs, and divergences between Xbox, Desktop Native, and Web builds. Goal: identify what needs to be wired up for Xbox parity and where Web deviates from Desktop Native.

## By the Numbers

- **~908 `HX_NATIVE` guards** total in `src/`
- **~638 legitimate** (LP64, endianness, Kinect, STL, threading)
- **~60 crash-masking**, **~80 stubbed subsystem**, **~130 workarounds**
- **~137 `__EMSCRIPTEN__`/`HX_WEB` guards** (Web-specific divergences)

---

## HIGH Priority Hacks Still Open

| Area | File(s) | Issue |
|------|---------|-------|
| **Boot screen auto-advance** | `UI.cpp:554-607` | Hardcoded screen-to-screen table with frame delays. Biggest navigation hack — bypasses real DTA screen flow |
| **mSink force-assignment** | `UI.cpp:668-673` | DTA `set_sink` never fires; native force-assigns `mSink = trans` to prevent input blackhole |
| **Audio load from IsLoaded()** | `Game.cpp:829-858` | Audio initiated from wrong place (polling loop), 120-frame timeout before proceeding without audio |
| **PropKey retargeting** | `HamDirector.cpp:833-840` | After `Copy`, PropKeys target wrong HamDirector — acknowledged TODO, not yet fixed |
| **Force characters visible** | `HamDirector.cpp:707-715` | DTA visibility handlers don't fire; characters forced visible manually |
| **Null-this UB guards** | `Object.cpp:449-480` | 4 methods guard `if (!this)` — DTA handlers call `AddSink` on null globals before init |
| **MILO_FAIL-to-WARN downgrades** | `Object.cpp:560-631` | 3 property lookup paths silently warn instead of fatal — masks missing object registrations |
| **Exit anim timeout** | `UI.cpp:641-655` | 90-frame force-complete for stuck exit animations |
| **Intro force-advance** | `GamePanel.cpp:407-415` | 30-frame timeout force-starts game past intro state |

---

## MEDIUM Priority — Stubbed Subsystems

| Global | Status | Impact |
|--------|--------|--------|
| `TheServer` | `void* = 0` in stubs | `TheServer.Init()` dispatches through null vtable — risky. Needs a stub `Server` object |
| `TheMetaMusic` | Initialized but 7 null guards in MetaPanel | Shell music works but MetaPanel methods have scattered guards |
| `TheSongSortMgr` | Null in stubs | Quickplay song sorting broken — may cause unsorted/blank lists |
| `TheCampaign` | Null | Campaign mode unreachable — acceptable for perform mode |
| `SampleData::Load` | 4 stubs | **SFX entirely broken** — gameplay hit/miss/success cues are silent |
| `RockCentral::ManageJob` | Crashes on `SendDropInDatapoint` | Needs null-safe ManageJob or proper stub initialization |

---

## Object System Workarounds (LP64 Cascade Safety)

These are **fundamental** and cannot easily be removed:

- **Three-phase `~ObjectDir` destruction** (`Dir.cpp:49-114`) — nullify, destroy, deferred-free
- **Three-phase `DeleteObjects`** (`Dir.cpp:728-815`) — same pattern
- **ReplaceRefs snapshot** (`Object.cpp:338-368`) — snapshots ring to vector before iterating
- **NullifyAllRefs** (`Object.cpp:371-397`) — safe ring walk with `kAliveSentinel`
- **HasDirPtrs counter** (`Dir.cpp:537-554`) — O(1) counter instead of ring walk
- **Deferred purge** (`Object.h/cpp`) — `SuppressEraseScope` RAII for ring mutation safety
- **UnloadPanels batch delete** (`UIScreen.cpp:149-165`) — clears tasks + defers frees

These exist because LP64 virtual inheritance changes object layout and ring pointers can be subobject offsets within malloc blocks. They are **correct and necessary**.

---

## Web vs Native Divergences

### Gaps (Web can't do what Desktop can)

| Gap | Impact | Fixable? |
|-----|--------|----------|
| No skeleton/pose tracking | Dancing gameplay non-functional | Hard — needs WebNN/TFLite.js |
| No analog gamepad | Keyboard-only, no axis values | Easy — wire `navigator.getGamepads()` |
| No canvas resize | Fixed 1280x720 | Easy — add `ResizeObserver` |
| No headless readback | `ReadbackHeadlessFrame()` returns false | Low priority — Playwright covers it |
| Video needs `.webm` transcoding | FMVs blank without offline transcode | By design |
| No network services | Leaderboards/DLC silently fail | By design |

### Web-only Workarounds

| Workaround | Location | Issue |
|------------|----------|-------|
| `UIPanel::Exiting()` always false | `UIPanel.cpp:264` | Exit anims skipped entirely (instant transitions) |
| `UIScreen::Enter()` skips `Poll()` | `UIScreen.cpp:308` | Panels may not init before first Draw |
| `Debug::Fail()` never fatal | `Debug.cpp:177` | All failures silently continue |
| `PollUntilLoaded` 10K iteration cap | `Loader.cpp:418` | Prevents browser hang, but silent on failure |
| `WaitUntilReady()` returns false | `BinStream.cpp:250` | 11 callers silently ignore — latent data corruption risk |
| Verbose per-panel logging | `UIScreen.cpp:264-271` | Unconditional `fprintf` spam |
| `AppLabel` WASM vtable crash | `MainMenuProvider.cpp:48` | `call_indirect` type mismatch — OPEN |

### Legitimate Divergences (correct, keep)

- Audio: AudioWorklet+SAB vs miniaudio thread
- Video: `<video>` element vs FFmpeg
- GPU: async WebGPU init vs sync Dawn
- Files: MEMFS + HTTP fetch vs .ark archive
- Input: JS event listeners vs GLFW

---

## What's Working Well

- Full gameplay pipeline: boot, menu, song select, game_screen
- 6 venues tested, 350-518 draw calls/frame
- Real-time MOGG audio decoding + ring buffer playback
- Character dance animation (skinned mesh, 4-bone blending, 40-bone palettes)
- Camera cuts from song.anim PropKeys
- Post-processing (bloom, contrast, saturation, vignette, chromatic aberration)
- Hair/cloth physics (fixed in Session 74)
- Content system (62 songs from DTA)
- Render-to-texture pipeline
- FileMerger convergence (5 phases complete)

---

## App.cpp Boot Sequence Comparison

### Native Boot (280-429)

1. `TheRnd.PreInit()`
2. `DirLoader::SetCacheMode(true)` (Emscripten only)
3. `SystemInit("config/ham_keep.dta")`
4. `SynthInit()`, `Movie::Init()`, `TheRnd.Init()`
5. `MagnuInit()`, `FlowInit()`
6. Common sound bank load
7. `CharInit()`, `WorldInit()`, `HamInit()`
8. `MoveMgr::Init(0)`, `MiniGameMgr::Init()`
9. `TheHamSongMgr.Init()`, `MetaPanel::Init()`, `GameInit()`
10. `TheContentMgr.RefreshSynchronously()`
11. `TheUI = &TheHamUI; TheHamUI.Init()`
12. Register stubs (saveload_mgr, profile_mgr, platform_mgr, content_mgr, challenges, speech_mgr)
13. `TheUI->GotoFirstScreen()`

### Xbox Boot (430-638) — Additional Steps

- `SynthPreInit()` before splash
- Splash screen system (ESRB/Harmonix logos)
- `LiveCameraInput::PreInit/Init()` (Kinect)
- KinectGuideThread on CPU 1
- `TheServer.Init()`, `TheRockCentral.Init()`
- `FixedSizeSaveable::Init()`, `HamUserMgrInit()`
- `SaveLoadManager::Init()`
- `sfx/audio_mixer.milo` load
- `AccomplishmentManager::Init()`, `MetagameRank::Init()`
- `FileCache` persistent cache + `PollUntilLoaded`
- `GestureMgr::DebugInit()`, `ThePresenceMgr.Init()`
- `PartyModeMgr::Init()`
- `MemTrackEnable(true)`

### Globals Null on Native

| Global | Why | Risk |
|--------|-----|------|
| `TheServer` | `void* = 0` in stubs | High — null vtable dispatch |
| `TheSplasher` | No `Splash` object | None — guarded |
| `TheSaveLoadMgr` | Replaced by `NativeSaveLoadStub` | None |
| `TheAchievements` | Xbox-only poll | None |
| `TheAccomplishmentMgr` | Xbox-only init | None |
| `TheLeaderboards` | Xbox-only poll | None |
| `ThePresenceMgr` | Xbox-only init | None |
| `gPersistentCache` | Xbox-only FileCache | None |
| `PartyModeMgr` | Xbox-only init | None |
| `TheSkeletonIdentifier` | `void* = 0` in stubs | None — Kinect |
| `TheSkeletonViz` | `void* = 0` in stubs | None — debug |
| `TheMaster` | `void* = 0` in stubs | Medium — `AddSink` on null |
| `TheSongSortMgr` | `void* = 0` in stubs | Medium — sorting broken |
| `TheCampaign` | Not constructed | Low — campaign mode only |
| `TheFitnessGoalMgr` | `void* = 0` in stubs | None |
| `TheHAQMgr` | `void* = 0` in stubs | None |

---

## Recommended Next Steps

1. **Create a stub `Server` object** — prevent null vtable dispatch crashes
2. **Fix `SampleData::Load`** — unblock gameplay SFX (RB3 has reference impl)
3. **Wire DTA `set_sink`** — eliminate mSink force-assignment + boot auto-advance table
4. **Fix PropKey retargeting after Copy** — natural character visibility + camera cuts
5. **Web: wire `navigator.getGamepads()`** — easy win for web gamepad support
6. **Web: add canvas `ResizeObserver`** — easy win for responsive layout
