# Native Hack Dependency Graph

Date: 2026-03-21

## Hack Inventory

### Hack 1: `gNativeVenueDir` Global

**Files**: `src/system/world/Dir.cpp:32`, `src/system/obj/Dir.cpp:684-693`, `src/system/world/Dir.h:149`

**Writer** (1 location):
- `ObjectDir::AddedSubDir()` — when a subdir named `"chars_base"` is added, stores `this` (the parent ObjectDir, i.e. the venue) into `gNativeVenueDir`.

**Readers** (3 locations):
- `WgpuRnd::NativeVenueInit()` — uses it to find and Enter() the venue WorldDir.
- `App.cpp:1042-1043` — fallback venue discovery when `TheHamDirector->GetVenueWorld()` returns null.
- `App.cpp:1109,1145-1146` — menu venue detection and pre-game venue drawing.

**Inputs**: ObjectDir subdirectory graph (chars_base name match).
**Outputs**: `gNativeVenueDir` global pointer.
**Xbox replacement**: `HamDirector::mVenue` is set by `OnLoadSong()` receiving the venue ObjectDir from DTA scripts. `TheHamDirector->GetVenueWorld()` returns `mVenue`. The venue is entered by `HamDirector::Enter()` calling `VenueEnter(mVenue)`.

---

### Hack 2: `NativeVenueInit()` (Rnd_Wgpu.cpp:819-866)

**Files**: `native/src/platform/Rnd_Wgpu.cpp:819-866`, `native/src/platform/Rnd_Wgpu.h:352-355`

**Members**: `mVenueInited`, `mLastVenueDir`, `mLastVenueHashSize`

**Inputs**: `gNativeVenueDir` (Hack 1), `TheHamDirector`.
**Outputs**: Calls `venue->Enter()` or `TheHamDirector->VenueEnter(venue)`, sets `mVenueInited`.
**Called from**: `BeginDrawing()` (line 884), every frame.

**Purpose**: On Xbox, `HamDirector::Enter()` is triggered by the meta_game panel DTA flow, which calls `VenueEnter()`. On native, the DTA panel transitions may not fully execute, so this detects venue availability and forces the Enter cascade.

**Dependencies**: Requires Hack 1 (`gNativeVenueDir`) to discover the venue.
**Xbox replacement**: Panel DTA flow: `meta_game.dta` enter handler -> `HamDirector::Enter()` -> `VenueEnter(mVenue)`.

---

### Hack 3: App.cpp Venue Poll/Setup Block (lines 1033-1158)

**Files**: `src/App.cpp:1033-1158`

**Sub-blocks**:

**3a. Venue discovery** (1037-1044): Gets `venueWorld` from `TheHamDirector->GetVenueWorld()`, falls back to `gNativeVenueDir`.

**3b. One-shot venue setup** (1047-1104): Hides Kinect-dependent meshes (TVScreen, projection, Reflect, refract, render targets, TexRenderers). Runs once per new venue via `sLastPresetVenue` static.

**3c. Menu venue poll** (1107-1113): Calls `venueWorld->Poll()` only for menu venues (when no HamDirector gameplay venue exists).

**3d. Character root reset** (1117-1123): Zeroes character Transform positions to prevent root motion drift on menu venues.

**3e. Pre-game venue draw** (1142-1149): Draws venue directly via `gNativeVenueDir` when `!TheHamDirector` (menu/attract screen, before gameplay starts).

**Inputs**: `gNativeVenueDir` (Hack 1), `TheHamDirector`, `TheUI`.
**Outputs**: Visual rendering, mesh visibility, character positioning.
**Dependencies**: Requires Hack 1 for venue discovery fallback. Does NOT require NativeVenueInit (Hack 2) to have run — Enter() is separate from Draw().
**Xbox replacement**:
- 3b: Kinect meshes render properly with Kinect hardware. No hiding needed.
- 3c: WorldDir is polled via `HamDirector::ListPollChildren()` during gameplay, and panel DTA flows handle menu venue polling.
- 3d: Root motion is handled by the gesture/skeleton pipeline with Kinect input.
- 3e: Venue draws through `world_panel` panel which `TheUI->Draw()` renders as part of the screen hierarchy.

---

### Hack 4: HamDirector Crew/Outfit Fallback (lines 1008-1045)

**Files**: `src/system/hamobj/HamDirector.cpp:1008-1045`

**What it does**: In `OnLoadSong()`, before wardrobe loading:
1. Checks `player_present` property on each player's provider.
2. If player not present: clears crew/outfit to prevent stale data.
3. If crew is null but character is set: reconstructs crew via `GetCrewForCharacter()`.
4. If outfit is null but character is set: reconstructs outfit via `GetCharacterOutfit()`.

**Inputs**: `TheGameData->Player(i)`, `HamPlayerData::Crew()`, `HamPlayerData::Outfit()`, `HamPlayerData::Char()`, `HamPlayerData::Provider()`.
**Outputs**: `mCrews[i]`, `mCharacterOutfits[i]`.
**Dependencies**: NONE of the other hacks. This is self-contained within the OnLoadSong handler.
**Xbox replacement**: The multiuser DTA flow (`multiuser_screen.dta`) populates crew/outfit/character fully before `OnLoadSong` fires. The `player_present` check isn't needed because the flow never leaves stale data.

---

### Hack 5: HamDirector Move Remixer Init (lines 546-562)

**Files**: `src/system/hamobj/HamDirector.cpp:546-562`

**What it does**: After the song merger completes (in `OnFileLoaded`), explicitly calls:
1. `TheMoveMgr->mSuperEasyRemixer->Init()` — populates per-difficulty move parent arrays.
2. `TheMoveMgr->ResetRemixer()` — calls `SelectMove()` for each player x measure, writing clip/move keyvals into routine builder anims.

**Inputs**: `TheMoveMgr`, `TheMoveMgr->mSuperEasyRemixer`, `mPlayer1RoutineBuilderAnim`, `mPlayer2RoutineBuilderAnim`.
**Outputs**: Populates routine builder anims with choreography data.
**Dependencies**: Requires the song merger to have completed (it's in the post-merge handler). Does NOT depend on Hacks 1-4.
**Xbox replacement**: DTA reset handler calls `SetGameplayMode(perform, true)` which triggers the remixer init chain: `merge_moves=1` -> routine builder anim population via DTA script callbacks.

---

### Hack 6: FlowQueueable Listener Divergence

**Files**: `src/system/flow/FlowQueueable.h:33-37`, `src/system/flow/FlowQueueable.cpp` (multiple blocks)

**What it does**: Replaces `std::list<Hmx::Object*>` with `ObjPtrList<Hmx::Object>` on native.

**Why**: `std::list<Hmx::Object*>` stores raw pointers. When a listener object is destroyed during cascade teardown, the list retains a dangling pointer. `ObjPtrList` uses the engine's ObjRef ring system — when an object is destroyed, all ObjPtrList entries pointing to it are automatically nullified (via `kObjListNoNull`).

**Code differences**:
- Constructor: `mListeners(this)` initializer for ObjPtrList vs none for std::list.
- `Deactivate()`: Pop-before-release pattern (pop_back, then ReleaseListener) vs erase-after-release (ReleaseListener, then erase).
- `ChildFinished()`: Same pop-before-release vs erase-after-release.
- `Activate()`: `insert(begin(), listener)` vs `push_front(listener)` (ObjPtrList has no push_front).
- Iterator type: `auto` vs `std::list<Hmx::Object*>::iterator`.

**Inputs**: Flow system listener lifecycle.
**Outputs**: Safe listener management during cascade teardown.
**Dependencies**: Depends on the cascade protection infrastructure (Hack 9 — `InDeleteObjects()`), but is orthogonal to venue hacks 1-5.
**Classification**: **Platform adaptation**, not a hack. `std::list<raw ptr>` is fundamentally unsafe when objects can be destroyed by the ObjectDir cascade. This would be needed even with full DTA flow convergence.
**Xbox context**: On Xbox, the flow system and object lifecycle are managed by Kinect/game timing that prevents the specific cascade scenarios. The raw pointer list "works" because the destruction order is deterministic and controlled by the game's state machine.

---

### Hack 7: FileMerger Native Blocks

**Files**: `src/system/char/FileMerger.cpp` (5 `#ifdef HX_NATIVE` blocks)

**7a. `sDisableAll` static** (line 22): Declaration of static bool. Used by `FinishLoading()` (line 222) and `SYNC_PROP(disable_all, sDisableAll)` (line 145). Allows DTA to disable all mergers.

**7b. Cascade guards in `Merger::Clear()`** (lines 54-83, three blocks):
- Skip `on_pre_clear` message when `InDeleteObjects()` is true (message handler might access freed objects).
- Skip `delete front` loop for `mLoadedObjects` during cascade (objects are dir-owned, cleaned by DeleteObjects).
- Skip `RemoveSubDir` loop for `mLoadedSubdirs` during cascade (subdirs handled by DeleteSubDirs).

**7c. Post-merge subdir object registration** (lines 240-252): After MergeDirs, walks nested objects in subdirs and registers any missing ones into the merger dir's hash table via `SetName()`. Ensures `Find<T>(name, false)` works at runtime, matching Xbox's flat scope behavior.

**7d. DirLoader parent propagation** (lines 448-455): Passes `Dir()` as parent to DirLoader constructor so ObjPtr fallback can resolve objects in the parent ObjectDir during deserialization. On Xbox, MergeDirs flattens everything into one scope.

**Inputs**: `ObjectDir::InDeleteObjects()`, Dir hierarchy, MergeDirs scope model.
**Outputs**: Safe cascade teardown, correct object resolution.
**Dependencies**:
- 7b depends on `InDeleteObjects()` infrastructure (Hack 9).
- 7c and 7d are scope-resolution workarounds — they depend on the native port's different MergeDirs behavior.
**Classification**: 7b is a **necessary safety guard** (cascade protection). 7c and 7d are **scope-resolution adaptations** needed because native MergeDirs doesn't flatten as aggressively as Xbox. 7a is a convenience feature.
**Xbox replacement**: Xbox's MergeDirs flattens all objects into a single ObjectDir scope. No parent propagation or post-merge registration needed. Cascade teardown doesn't hit the same bugs because object lifecycle is more controlled.

---

### Hack 8: GamePanel Native Blocks

**Files**: `src/lazer/game/GamePanel.cpp` (5 blocks)

**8a. Intro force-advance** (lines 407-416): If `kGameInIntro` state persists for 30+ frames, force-call `StartGame()`. Works around audio/timing not advancing on native (no Kinect ready signal).

**8b. Frame time array bounds masking** (lines 548-552): `mFrameTimeSamples[mJitterSampleCount & 0x1F]` on native vs bare `mFrameTimeSamples[mJitterSampleCount]` on Xbox. Prevents out-of-bounds write if jitter counter wraps differently.

**8c. StartGame bypass** (lines 571-581): Unconditionally calls `mGame->Start()` instead of checking `mGame->HasIntro()`. On native, the intro sequence may not play (no Kinect/audio timing), so we skip the gate.

**8d. game_stage forcing** (lines 584-589): After StartGame, explicitly sets `game_stage` to `"playing"`. On Xbox, `SongSequence::Play` sets this, but on native the intro skip means it might not fire.

**8e. PollForLoading diagnostics** (lines 930-933, 956-959): Debug fprintf logging for load state tracking.

**Inputs**: `mState`, `mGame`, `TheHamProvider`, game timing.
**Outputs**: Game state transitions, `game_stage` property.
**Dependencies**: NONE of the venue hacks (1-3). Self-contained within gameplay startup flow.
**Xbox replacement**: Kinect ready signal + audio timing + SongSequence::Play handle intro->playing transition naturally.

---

### Hack 9: ObjectDir Cascade Protection Infrastructure

**Files**: `src/system/obj/Dir.cpp:49-92,714-777`, `src/system/obj/Dir.h:478-499`, `src/system/obj/Object.cpp:112`, `src/system/obj/ObjPtr_p.h:39,56`, `src/system/obj/Task.cpp:436,512`, `src/system/obj/Utl.cpp:426-430`

This is not one of the 8 listed hacks but is a **foundational infrastructure** that Hacks 6, 7b, and others depend on. It includes:
- `sDeleteObjectsDepth` counter (incremented in `~ObjectDir`, decremented after DeleteObjects).
- `InDeleteObjects()` static query.
- Three-phase DeleteObjects (nullify refs, destroy, defer-free).
- `sInMergeDirs` flag set during MergeDirs to prevent subdir copy cascade.
- Guards in `~ObjRefConcrete`, `~Object`, Task creation, FlowNode deletion.

**Classification**: **Necessary infrastructure**. Cannot be removed without replacing the entire native object lifecycle model. On Xbox, the single-threaded deterministic destruction order prevents the cascade bugs this infrastructure guards against.

---

## Dependency Diagram

```
                    +-----------------------+
                    | Hack 9: Cascade       |
                    | Protection Infra      |
                    | (InDeleteObjects,     |
                    | 3-phase delete,       |
                    | sInMergeDirs)         |
                    +-----------+-----------+
                                |
                    +-----------+-----------+
                    |                       |
          +---------v--------+    +---------v--------+
          | Hack 6:          |    | Hack 7b:         |
          | FlowQueueable    |    | FileMerger       |
          | ObjPtrList       |    | Cascade Guards   |
          +------------------+    +------------------+


    +-------------------+
    | Hack 1:           |
    | gNativeVenueDir   |
    | (Dir.cpp:684)     |
    +--------+----------+
             |
     +-------+--------+
     |                 |
+----v----+     +------v---------+
| Hack 2: |     | Hack 3:        |
| Venue   |     | App.cpp venue  |
| Init    |     | poll/setup/    |
| (Rnd)   |     | draw block     |
+---------+     +----------------+


+-------------------+     +-------------------+     +-------------------+
| Hack 4:           |     | Hack 5:           |     | Hack 8:           |
| HamDirector       |     | HamDirector       |     | GamePanel          |
| crew/outfit       |     | move remixer      |     | intro/timing       |
| fallback          |     | init              |     | workarounds        |
+-------------------+     +-------------------+     +-------------------+
    (independent)             (independent)             (independent)
```

**Key**: Arrows point FROM dependency TO dependent. Hacks 4, 5, 8 are fully independent — no arrows connect them to anything else. Hacks 6 and 7b both depend on Hack 9. Hacks 2 and 3 both depend on Hack 1.

---

## Recommended Removal Order

### Phase 0: Independent Hacks (can be removed in any order, no dependencies)

| Order | Hack | Condition for removal |
|-------|------|----------------------|
| 0a | **Hack 4** — crew/outfit fallback | DTA multiuser flow fully wires crew/outfit/character before OnLoadSong |
| 0b | **Hack 8e** — PollForLoading diagnostics | Pure debug logging, remove anytime |
| 0c | **Hack 5** — move remixer init | DTA reset handler fires SetGameplayMode(perform,true) correctly on native |

### Phase 1: Gameplay Flow Hacks (require DTA flow convergence)

| Order | Hack | Condition for removal |
|-------|------|----------------------|
| 1a | **Hack 8a** — intro force-advance | Audio/timing subsystem advances correctly (SongSequence timing works) |
| 1b | **Hack 8c** — StartGame bypass | `HasIntro()` returns correct value on native |
| 1c | **Hack 8d** — game_stage forcing | SongSequence::Play fires and sets game_stage on native |
| 1d | **Hack 8b** — frame time bounds | Verify mJitterSampleCount stays in [0,31]; if so, safe to remove mask |

### Phase 2: Venue Discovery Chain (requires panel DTA flow)

| Order | Hack | Condition for removal |
|-------|------|----------------------|
| 2a | **Hack 2** — NativeVenueInit | HamDirector::Enter() fires via meta_game panel DTA flow |
| 2b | **Hack 3** — App.cpp venue block | world_panel renders venue via TheUI->Draw(); no manual poll/draw needed |
| 2c | **Hack 1** — gNativeVenueDir | Remove LAST — only after all readers (Hacks 2, 3) are removed |

### Phase 3: Scope/Merge Adaptations (require MergeDirs convergence or are permanent)

| Order | Hack | Condition for removal |
|-------|------|----------------------|
| 3a | **Hack 7c** — post-merge registration | MergeDirs flattens objects into single scope (matching Xbox) |
| 3b | **Hack 7d** — DirLoader parent propagation | ObjPtr resolution works without parent chain (flat scope) |
| 3c | **Hack 7a** — sDisableAll | Low priority, small convenience feature |

### Phase 4: Safety Infrastructure (LAST or NEVER)

| Order | Hack | Assessment |
|-------|------|-----------|
| 4a | **Hack 7b** — FileMerger cascade guards | Remove only if cascade protection infra (Hack 9) is removed |
| 4b | **Hack 6** — FlowQueueable ObjPtrList | **Keep permanently** — raw pointer list is fundamentally unsafe |
| 4c | **Hack 9** — cascade protection infra | **Keep permanently** — without it, any ObjectDir teardown during gameplay can crash. Removing would require proving Xbox's destruction order is replicated exactly. |

---

## Risk Matrix

| Hack | Removal Risk | Worst Case | Blast Radius | Reversibility |
|------|-------------|------------|--------------|---------------|
| **Hack 1** (gNativeVenueDir) | **HIGH** | No venue renders on menu/attract screens; blank screen | Visual — menu/attract only | Easy — re-add global |
| **Hack 2** (NativeVenueInit) | **HIGH** | Venue never Enter'd; no lights, no animations, characters T-posed | Visual — all venues | Easy — re-add call |
| **Hack 3** (App.cpp block) | **MEDIUM** | Kinect meshes visible (white rects), venue not polled on menus, character drift | Visual — menus; gameplay unaffected if world_panel works | Easy — re-add block |
| **Hack 4** (crew/outfit) | **LOW** | Wrong character loaded for P2 slot, or stale outfit on single-player | Visual — character appearance only | Easy — re-add block |
| **Hack 5** (remixer init) | **MEDIUM** | No choreography in perform mode; characters stand still | Gameplay — dance moves don't play | Easy — re-add calls |
| **Hack 6** (FlowQueueable) | **CRITICAL** | Use-after-free crash during flow teardown | Crash — any screen transition | Moderate — need to rebuild ObjPtrList wiring |
| **Hack 7b** (cascade guards) | **CRITICAL** | Double-free or use-after-free during dir teardown | Crash — any venue/char unload | Moderate — need cascade infra |
| **Hack 7c** (post-merge reg) | **MEDIUM** | Find<T>() fails for merged objects; DTA scripts break | Functional — gameplay features | Easy — re-add loop |
| **Hack 7d** (parent propagation) | **MEDIUM** | ObjPtr resolution fails during load; null deref or wrong object | Crash or wrong behavior during load | Easy — re-add param |
| **Hack 8a** (intro force) | **LOW** | Game stuck at intro screen if audio doesn't advance | Hang — must restart | Easy — re-add timer |
| **Hack 8b** (bounds mask) | **LOW** | Array out-of-bounds write; memory corruption | Crash — rare, timing-dependent | Easy — re-add mask |
| **Hack 8c** (StartGame bypass) | **LOW** | Game never starts if HasIntro() returns false | Hang — stuck at intro | Easy — re-add bypass |
| **Hack 8d** (game_stage) | **LOW** | HUD/scoring doesn't activate; game plays but no feedback | Functional — HUD missing | Easy — re-add set |
| **Hack 8e** (diagnostics) | **NONE** | No impact — pure logging | None | N/A |
| **Hack 9** (cascade infra) | **CRITICAL** | Cascade crashes on any ObjectDir teardown | Crash — any dir unload | Hard — pervasive |

### Risk Categories

- **CRITICAL**: Removal causes crashes. Must keep or replace with equivalent safety.
- **HIGH**: Removal causes major visual/functional regression (blank screens, no venues).
- **MEDIUM**: Removal causes partial functional regression (missing features, wrong objects).
- **LOW**: Removal causes minor issues or hangs that are easily diagnosed.
- **NONE**: Safe to remove anytime.

---

## Hack Interaction Summary

### Connected Components

**Component A — Venue Discovery Chain**:
`Hack 1 -> Hack 2, Hack 3`

These three form a chain: gNativeVenueDir is the data source, NativeVenueInit and the App.cpp block are consumers. Hack 1 must be removed LAST.

**Component B — Cascade Safety**:
`Hack 9 -> Hack 6, Hack 7b`

These are safety infrastructure. Hack 9 is foundational. Hacks 6 and 7b are consumers. All should be kept permanently.

**Component C — Independent Gameplay Hacks**:
`Hack 4, Hack 5, Hack 8` (no connections to each other or to A/B)

Each can be removed independently as its corresponding Xbox mechanism is replicated on native.

### Cross-Component Dependencies: NONE

The three components are fully independent. Venue discovery (A) does not depend on cascade safety (B), and gameplay hacks (C) depend on neither. This means:
- Removing all of Component C requires only DTA flow convergence for gameplay screens.
- Removing Component A requires panel DTA flow convergence for venue lifecycle.
- Component B should likely never be removed.

---

## Summary

Of the 8+1 hacks analyzed:
- **3 are independently removable** (Hacks 4, 5, 8e) with minimal risk
- **4 require DTA flow convergence** (Hacks 2, 3, 8a-d) and should be removed in order
- **1 requires MergeDirs scope convergence** (Hack 7c, 7d)
- **3 should be kept permanently** (Hacks 6, 7b, 9) — they fix real bugs in the object lifecycle
- Hack 1 (gNativeVenueDir) is the linchpin of the venue chain — remove only after Hacks 2 and 3 are gone

Total estimated convergence effort: Removing the "easy" hacks (4, 5, 8e) is immediate. The venue chain (1-3) and gameplay hacks (8a-d) require DTA flow work. The safety infrastructure (6, 7b, 9) is permanent.
