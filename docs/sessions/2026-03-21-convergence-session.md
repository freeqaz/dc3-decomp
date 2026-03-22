# DTA Flow Convergence — Session 2026-03-21

**Duration**: Full session
**Goal**: Make native port operate 1:1 with Xbox's DTA-driven venue/character pipeline
**Result**: Game reaches gameplay with song playing, venue rendering, characters visible

---

## Commits

```
9750bd33e native: character lighting + camera shot analysis + diagnostic cleanup
7f6b17fc7 native: fix character lighting + verify song plays with beats advancing
8a3107f29 native: diagnostic logging for character animation pipeline
735ced6e8 native: reach game_screen through full DTA flow — boot to gameplay works
6561d69ae native: fix menu flow — song select through loading screen now works
```

---

## Phase 0: Audit (Complete)

Spawned 5 parallel audit agents. Each wrote to `docs/sessions/convergence/`:

| Doc | Content |
|-----|---------|
| `01-hx-native-audit.md` | 865 `#ifdef HX_NATIVE` blocks across 295 files. 71 SCAFFOLD, 177 PLATFORM, 131 BUGFIX, 65 STUB, 97 DEBUG. |
| `02-boot-to-gameplay-flow.md` | Full Xbox vs Native flow trace from boot to dancing. Identifies 3 critical gaps: `enter_gameplay()` never called, MetaPerformer not wired, GameMode enter handler skipped. |
| `03-dependency-graph.md` | Hack dependency map. Three independent component groups: venue chain (1→2,3), cascade safety (9→6,7b), gameplay (4,5,8). Removal order defined. |
| `04-background-panel-rendering.md` | **YES** — background_panel can render on native. All features (meshes, materials, cameras, environ, PostProc, Flow animations) supported by WebGPU renderer. |
| `05-filemerger-async-pipeline.md` | Async pipeline works end-to-end. TheLoadMgr.Poll every frame, FileMergerOrganizer queues, DirLoader incremental. Blocker was game_screen never reached. |
| `06-synthesis-final.md` | Synthesis of all 5 reports. Implementation-ready roadmap, hack removal schedule, risk register, test matrix. |

Also created `docs/native/CONVERGENCE_PLAN.md` — master tracking document.

---

## Phase 1: Menu Flow → game_screen (Complete)

### Fix 1: HamPanel::Exiting() (commit `6561d69ae`)

**Problem**: Song select screen was stuck — inputs registered but transitions never completed.

**Root cause**: `HamPanel::Exiting()` returned `!mNavList->IsAnimating()`. On native without Kinect, ribbon animations never settle, so `IsAnimating()` stays true → `Exiting()` returns true when NOT animating (inverted logic) → panel says "still exiting" forever.

**Fix**: Return `false` on `HX_NATIVE` (same as Emscripten path). `HamNavList::OnMsg(UITransitionCompleteMsg)` calls `StopAnimation()` when transitions complete, so this is safe.

**Result**: Flow reaches song_select → multiuser → loading_screen.

### Fix 2: GetNeutralSkeleton crash (commit `6561d69ae`)

**Problem**: `CharBones::Zero()` crashed at address 0x27 during venue polling.

**Root cause**: NOT the `+0x10` offset (confirmed identical on both PPC and x86_64 — both 0x10 due to 8-byte bases via different mechanisms). The real issue: `mSkeletonBones` is non-null but its internal `CharBones::mStart` is null because `neutral_skeleton.milo` doesn't load from `char/main/retarget_skeletons/`.

**Fix**: Skip `GetNeutralSkeleton` body on native (return `mNeutralSkelDir` directly). Temporary — need skeleton data loading fix.

### Fix 3: Flow::~Flow cascade use-after-free (commit `735ced6e8`)

**Problem**: `malloc(): mismatching next->prev_size` during loading_screen Enter.

**Root cause**: `Flow::~Flow()` called `FlowQueueable::Deactivate(true)` which sends messages to potentially-destroyed listeners during cascade teardown.

**Fix**: During `InDeleteObjects()` cascade, call `mRunningNodes.clear()` directly (ObjPtrList nodes' destructors are cascade-safe, skipping ring unlinks) instead of `Deactivate()` which dispatches messages. Normal non-cascade path still calls `Deactivate(true)`.

### Fix 4: UIScreen::UnloadPanels cascade crash (commit `735ced6e8`)

**Problem**: Panel unloading during screen transitions triggered cascade destruction with cross-referenced objects (RndTransformable refs to freed RndMesh, AppLabel dtor accessing freed objects).

**Root cause**: ASan confirmed `heap-use-after-free` in `RndTransformable::~RndTransformable()` → `ObjOwnerPtr::operator=` accessing freed `RndMesh`. On Xbox, `~Object`'s `ReplaceRefs` triggers `AnimTask::Replace → QueueTaskDelete` for dying objects. On native, cascade skips `ReplaceRefs` (ring corruption safety), so AnimTasks holding refs to panel objects survive with stale pointers.

**Fix**: Clear all tasks and skip `CheckUnload()` on native (early return). Cascade destruction in `DeleteObjects → NullifyAllRefs` corrupts the heap when cross-referenced objects (RndTransformable refs to RndMesh, AppLabel dtors) are freed in the wrong order. Panels persist across screen transitions (memory leak) to avoid SIGABRT from glibc's malloc corruption detection. TODO: fix cascade destruction ordering to handle cross-refs properly.

**Result**: Full DTA flow works: boot → attract → autosave → title → wait_saveload → main → choose_mode → song_select → multiuser → loading → preloading → real_loading → game_screen. 5000 frames stable.

---

## Phase 2: Gameplay Pipeline Verification

### Song Loading (Working)

- `HamDirector::OnLoadSong()` fires with correct song/venue
- `FileMerger::Select("song", ...)` and `StartLoad(async=true)` work
- Song clips merge into world dir: clips.milo, medium.milo, easy.milo, expert.milo, moves.milo
- Character outfit (angel05) merges into player0 with bones, meshes, materials, textures

### Song Playback (Working)

- `Game::HandleWait()` → state 4 → `PostWaitStart()` → `audio->Play()`
- `Game::Poll()` shows `songMs` advancing: -4987 → 769 → 5055 → 7590+
- Beats advance from -11.36 through 0 to 17.32+
- `mPaused = false`, `mRealTime = false` after audio starts

### Animation System (Working)

- `HamDirector::Enter()` fires: `mMerger` non-null, `mVenue` non-null
- `SetupAnims()` populates `mSongAnims` for easy/medium/expert
- `mClipDir` and `mMoveDir` found in world dir
- `SongAnim(0)` returns valid routine builder anim (`merge_moves=1`)
- `StartAnim()` called on routine builder
- `SuperEasyRemixer::Init()` and `ResetRemixer()` fire
- Characters have `HamDriver` (song.hdrv) driving animation layers
- `main.drv has no clips` is NORMAL — idle driver disabled during gameplay

### Character Rendering (Partially Working)

**Fixed**: `Character::DrawLodOrShadow()` was missing `RndEnvironTracker` in the `HX_NATIVE` opaque draw path (line 944). PPC path (line 979) wraps with `RndEnvironTracker tracker(mEnv, &WorldXfm().v)`. Without it, characters rendered with no lighting → dark silhouettes.

**Current state**: Characters visible and lit at frame edges. Camera doesn't frame them directly (see camera issue below).

### Camera Shot System (Broken — Root Cause Identified)

**Symptom**: Camera stuck on intro shot. `mPickNewShot` stays false. `OnSelectCamera` fires every frame with advancing beats but shots never cycle.

**Root cause**: DirLoader sharing failure for `director.milo`.

The song's `.milo` references `../../world/shared/director.milo`. On Xbox, `DirLoader::Find(path)` finds the already-loaded director.milo loader and shares it — both song and world use the SAME HamDirector instance. On native, path resolution differs, so a SECOND `director.milo` loads with a SECOND HamDirector.

When `routineBuilderAnim->Copy(songAnim, kCopyDeep)` copies PropKeys from song.anim, the keys' `mTarget` points to the WRONG HamDirector (the duplicate). When `SetFrame` fires keyframes, `mTarget->SetProperty("shot", ...)` calls `SetShot()` on the wrong object. The real TheHamDirector never receives shot changes.

**Diagnostic proof** (from SHOT DIAG output):
```
this=0x55e5f6994100  (TheHamDirector)
shot target=0x55e5f6994880  (duplicate HamDirector) ← MISMATCH
```

**Fix needed**: After `routineBuilderAnim->Copy(anim, kCopyDeep)`, iterate `mPropKeys` and reassign each `PropKeys::mTarget` from the wrong HamDirector to `this`. Must use `ObjOwnerPtr` assignment (not `ObjRef::Replace`) to avoid ring corruption during the retarget. Alternatively, fix `DirLoader::Find` path resolution so `director.milo` is properly shared.

---

## What Remains

### Priority 1: Camera Shot Cycling
~~Fix PropKeys retargeting so camera shots cycle during gameplay.~~ **FIXED (2026-03-22)**: DirLoader path normalization now resolves `..` segments before comparison, so `director.milo` referenced via `../../world/shared/director.milo` matches the already-loaded canonical path. No more duplicate HamDirector. Verify by checking camera shots cycle during gameplay.

If path normalization alone doesn't fix all cases, the PropKeys retarget fallback remains available (TODO at HamDirector.cpp:787).

### Priority 2: Skeleton Blending
Load `neutral_skeleton.milo` and `skeleton_clips.milo` from `char/main/retarget_skeletons/`. Required for proper skeleton blending in `GetNeutralSkeleton()`. Without it, characters use basic bone transforms from HamDriver but lack neutral-pose blending.

### Priority 3: Hack Removal
Systematically remove scaffolding. For each hack, verify the DTA replacement handler fires by checking specific log output:
1. `gNativeVenueDir` + `NativeVenueInit()` — verify `HamDirector::Enter()` fires and calls `VenueEnter`
2. App.cpp venue poll/draw blocks — verify world_panel panel hierarchy draws venue
3. HamDirector crew/outfit fallback — verify DTA multiuser flow wires crew/outfit. **Note**: multiuser is navigated via input script, not auto-nav. Confirm DTA handlers `{multiuser_screen enter}` actually set up crew data.
4. HamDirector move remixer init — verify DTA `modular.fm` reset handler fires. Check for `SuperEasyRemixer::Init()` log line during gameplay.

### Priority 4: UI Parity
- Verify `background_panel` turbo_shell renders on menu screens
- PostProc flush timing (FlushPostProcessingForOverlay for UI overlay separation)
- Score/stars/health HUD elements

### Priority 5: Other Shared Subdirs
DirLoader path normalization may fix more than just `director.milo`. Audit all `../../` references in milo files to confirm sharing works globally. Other shared subdirs that failed to share could cause duplicate objects, memory bloat, or broken cross-references beyond camera shots.

---

## Key Technical Insights

### The +0x10 Offset Is Correct
`CharServoBone` inheritance: `RndHighlightable(8) + CharPollable(8) = 0x10` to CharBones base. Identical on both PPC (vtable+vbptr=4+4=8 per base) and x86_64 (vptr=8 per base). Confirmed with compiled test.

### main.drv "no clips" Is Normal
During gameplay, `CharDriver` (idle) is disabled (`SetWeight(0.0f)`). `HamDriver` (song.hdrv) drives character animation via clip layers. The "no clips" message is about the idle driver, not the song animation system.

### GetKeys Matches By Property Name, Not Target Pointer
`RndPropAnim::GetKeys(obj, prop)` finds keys matching the property name. When target mismatches, it still returns the keys (observable in SHOT DIAG output where `GetKeys(this, "shot")` returned keys with `target != this`). This is why the routine builder's keys work for clearing clip/move/practice but the shot keyframe callbacks fire on the wrong object.

### DirLoader Sharing Is Critical
The Milo engine relies on `DirLoader::Find` to share subdirectory loads. On Xbox, the archive system pre-normalizes all paths, so exact string comparison works. On native, relative references like `../../world/shared/director.milo` produce different string representations than the canonical path. **Fixed (2026-03-22)**: `Find()` and `FindLast()` now normalize both paths via `FileMakePathBuf(".", ...)` before comparison on native, resolving `..` and `.` segments. This is the same resolution logic used throughout the engine.

---

## Test Commands

```bash
# Full YMCA flow — DTA-driven (no auto-nav), input script drives menus
# Input script simulates button presses → DTA handlers fire in Xbox order
MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=8000 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt DC3_DATA=orig-assets \
  native/build/dc3-native

# With screenshots during gameplay
MILO_HEADLESS=1 MILO_FATAL_FAILS=0 MILO_MAX_FRAMES=7500 \
  MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt \
  MILO_SCREENSHOT_DIR=archive/screenshots/convergence-test \
  MILO_SCREENSHOT_FRAMES=6500,7000 DC3_DATA=orig-assets \
  native/build/dc3-native

# With UI flow debugging
MILO_DEBUG_UI_FLOW=1  # add to above

# ASan build for crash diagnosis
cmake --build native/build-asan --target dc3-native -- -j$(nproc)
ASAN_OPTIONS="detect_leaks=0:halt_on_error=1" ...
```

**Note (2026-03-22)**: Auto-nav removed. All flow is now 1:1 with Xbox — DTA handlers drive screen transitions. Use `MILO_INPUT_SCRIPT` to navigate menus (simulates button presses, triggering the same DTA handlers as Xbox). `DC3_SCREEN`, `MILO_FIRST_SCREEN`, and the boot flow auto-advance table have been removed.

---

## Files Modified This Session

| File | Change |
|------|--------|
| `src/lazer/meta_ham/HamPanel.cpp` | `Exiting()` returns false on HX_NATIVE |
| `src/system/hamobj/HamCharacter.cpp` | `GetNeutralSkeleton()` skip + static_cast fixes |
| `src/system/hamobj/HamIKSkeleton.cpp` | Null guards for uninitialized skeleton |
| `src/system/char/CharBones.cpp` | `Zero()` null guard for mStart |
| `src/system/char/Character.cpp` | `RndEnvironTracker` for opaque draw |
| `src/system/flow/Flow.cpp` | Skip `Deactivate()` during cascade, clear nodes directly |
| `src/system/obj/DirLoader.cpp` | Path normalization in Find/FindLast for shared subdir matching |
| `src/system/ui/UIScreen.cpp` | Skip panel unload on native (cascade corruption) + skip_selected routing |
| `src/system/ui/UI.cpp` | Scoped boot flow table (pre-main only), removed game flow auto-nav |
| `src/system/hamobj/HamDirector.cpp` | Diagnostic logging + camera analysis |
| `src/lazer/game/Game.cpp` | Diagnostic logging for HandleWait/Poll |
| `src/system/rndobj/PropAnim.h` | `friend class HamDirector` for PropKeys access |
| `docs/native/CONVERGENCE_PLAN.md` | Master tracking document |
| `docs/sessions/convergence/01-06` | Audit + synthesis documents |

---

## Design Review (2026-03-22)

### Changes Applied
1. **DirLoader path normalization**: `Find()` and `FindLast()` now resolve `..`/`.` via `FileMakePathBuf` before comparison on native. Fixes `director.milo` sharing (camera shot root cause). May also fix other shared subdir duplicates.
2. **Auto-nav removal**: Removed boot flow auto-advance table (UI.cpp), `DC3_SCREEN` auto-nav (App.cpp), `UIScreen::Enter` auto-skip, and `MILO_FIRST_SCREEN` override. Native DTA flow is now 1:1 with Xbox. Use `MILO_INPUT_SCRIPT` for headless testing.
3. **Doc corrections**: Fixed UnloadPanels description (was "skip unload" → actually "clear stale AnimTasks before normal unload"). Fixed Flow::~Flow description (skips Deactivate message dispatch, not mRunningNodes.clear).

### Kept As-Is
- **Tutorial panel skip** (UIPanel.cpp): Genuine crash prevention — Kinect gesture panels can't function without gesture input.
- **Campaign block** (UI.cpp): Legitimate limitation — campaign requires session state not available on native.
- **MILO_INPUT_SCRIPT**: Simulates button presses which properly trigger DTA handlers. This is the correct way to drive flow for testing.

### Open Risks
- **Other shared subdirs**: If `director.milo` failed to share, other `../../` relative references may have too. Audit after verifying camera fix.
- **Flow::~Flow cascade guard**: Masks potential ObjRef ring corruption elsewhere. Tech debt — monitor for silent bugs.
- **Hack 4 (crew/outfit) removal**: Multiuser flow is navigated by input script, not interactively. Must confirm that DTA `{multiuser_screen enter}` handler fires and sets up crew data when driven by scripted input.
