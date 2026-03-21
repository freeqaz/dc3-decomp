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

**Root cause**: `Flow::~Flow()` cleared `mRunningNodes` (ObjPtrList) during cascade teardown. The freed `ObjPtrList::Node` objects were ObjRefs still linked in other objects' rings. When parent `DeleteObjects` ran `NullifyAllRefs`, it accessed freed Node memory.

**Fix**: Skip `mRunningNodes.clear()` during `InDeleteObjects()` cascade. Let parent cascade handle cleanup.

### Fix 4: UIScreen::UnloadPanels cascade crash (commit `735ced6e8`)

**Problem**: Panel unloading during screen transitions triggered cascade destruction with cross-referenced objects (RndTransformable refs to freed RndMesh, AppLabel dtor accessing freed objects).

**Root cause**: ASan confirmed `heap-use-after-free` in `RndTransformable::~RndTransformable()` → `ObjOwnerPtr::operator=` accessing freed `RndMesh`. The three-phase delete doesn't fully handle all cross-reference patterns during nested `DeleteObjects` cascades.

**Fix**: Skip `UnloadPanels` on native (temporary — memory leak). Panels persist across screen transitions.

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
Fix PropKeys retargeting so camera shots cycle during gameplay. Two approaches:
- **Quick**: Direct `ObjOwnerPtr` assignment on each PropKeys' mTarget after Copy
- **Proper**: Fix DirLoader path sharing for `director.milo`

### Priority 2: Skeleton Blending
Load `neutral_skeleton.milo` and `skeleton_clips.milo` from `char/main/retarget_skeletons/`. Required for proper skeleton blending in `GetNeutralSkeleton()`. Without it, characters use basic bone transforms from HamDriver but lack neutral-pose blending.

### Priority 3: Panel Unload Memory Leak
`UIScreen::UnloadPanels()` is skipped on native. Panels persist across transitions, leaking memory. Fix needs cascade destruction ordering for cross-referenced objects in PanelDir → ObjectDir → DeleteObjects.

### Priority 4: Hack Removal
Once the above are fixed, systematically remove scaffolding:
1. `gNativeVenueDir` + `NativeVenueInit()` (replaced by HamDirector::Enter → VenueEnter)
2. App.cpp venue poll/draw blocks (replaced by world_panel panel hierarchy)
3. HamDirector crew/outfit fallback (replaced by DTA player selection)
4. HamDirector move remixer init (keep until DTA modular.fm reset handler verified)

### Priority 5: UI Parity
- Verify `background_panel` turbo_shell renders on menu screens
- PostProc flush timing (FlushPostProcessingForOverlay for UI overlay separation)
- Score/stars/health HUD elements

---

## Key Technical Insights

### The +0x10 Offset Is Correct
`CharServoBone` inheritance: `RndHighlightable(8) + CharPollable(8) = 0x10` to CharBones base. Identical on both PPC (vtable+vbptr=4+4=8 per base) and x86_64 (vptr=8 per base). Confirmed with compiled test.

### main.drv "no clips" Is Normal
During gameplay, `CharDriver` (idle) is disabled (`SetWeight(0.0f)`). `HamDriver` (song.hdrv) drives character animation via clip layers. The "no clips" message is about the idle driver, not the song animation system.

### GetKeys Matches By Property Name, Not Target Pointer
`RndPropAnim::GetKeys(obj, prop)` finds keys matching the property name. When target mismatches, it still returns the keys (observable in SHOT DIAG output where `GetKeys(this, "shot")` returned keys with `target != this`). This is why the routine builder's keys work for clearing clip/move/practice but the shot keyframe callbacks fire on the wrong object.

### DirLoader Sharing Is Critical
The Milo engine relies on `DirLoader::Find` to share subdirectory loads. When path resolution differs between the world's load and the song's relative reference (`../../world/shared/director.milo`), duplicates are created. This breaks PropKeys targeting, and potentially other cross-dir references.

---

## Test Commands

```bash
# Full YMCA flow — should reach game_screen, 8000 frames stable
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

---

## Files Modified This Session

| File | Change |
|------|--------|
| `src/lazer/meta_ham/HamPanel.cpp` | `Exiting()` returns false on HX_NATIVE |
| `src/system/hamobj/HamCharacter.cpp` | `GetNeutralSkeleton()` skip + static_cast fixes |
| `src/system/hamobj/HamIKSkeleton.cpp` | Null guards for uninitialized skeleton |
| `src/system/char/CharBones.cpp` | `Zero()` null guard for mStart |
| `src/system/char/Character.cpp` | `RndEnvironTracker` for opaque draw |
| `src/system/flow/Flow.cpp` | Skip `mRunningNodes.clear()` during cascade |
| `src/system/ui/UIScreen.cpp` | Skip `UnloadPanels` on native (temp) |
| `src/system/hamobj/HamDirector.cpp` | Diagnostic logging + camera analysis |
| `src/lazer/game/Game.cpp` | Diagnostic logging for HandleWait/Poll |
| `src/system/rndobj/PropAnim.h` | `friend class HamDirector` for PropKeys access |
| `docs/native/CONVERGENCE_PLAN.md` | Master tracking document |
| `docs/sessions/convergence/01-06` | Audit + synthesis documents |
