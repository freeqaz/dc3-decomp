# UI Animation System — Native Port Status

## Current State

**Song loading and gameplay bring-up progressed again.** The full menu→song pipeline now runs stably: `main_screen → choose_mode(Perform) → song_select(YMCA) → multiuser(auto-skip) → loading → preloading → real_loading → game_screen`, and the latest binary run survives through frame 4800 with no crash.

The old blockers in this area are resolved:
- `Perform` routing now reaches `song_select_screen`
- `song_select_screen` is stable
- the old `crowd_clips.fm` / `CharClip` merge crash is gone
- the old native unload workaround is gone
- later runtime gaps in `RndTexBlendController::GetBlendState()` and native shader-manager work material setup are fixed enough for gameplay rendering to continue

**The current blocker is no longer "no character", "no venue", or "bad vertex weights".** Native now loads the main character path, keeps the Xbox compressed vertex blob intact, and unpacks valid skinned weights/indices on the GPU path. In the latest stable YMCA run, the venue, HUD, and crowd all render; `angel04` also renders with valid skin data, but the visible result is still wrong because the performer is overlapped/misplaced or otherwise posed incorrectly after the load step.

Fresh current-checkout revalidation confirms the old song-select crash is gone. The only subtlety is scripted input targeting: a bare `confirm` on the default `LEGIT` / `song_tier_4` header stays on `song_select_screen`, while moving the highlight onto a real song row like `YMCA` transitions into gameplay immediately.

Fresh mesh-loader validation on the current checkout:
- `RndMesh::LoadVertices()` on native now preserves Xbox compressed vertex blobs instead of eagerly decoding them into broken native `mVerts`
- `MeshVertexLoading.*` unit tests cover the native compressed-load path, compressed skinned decode, and rev `0x26` uncompressed weight/index decode
- current staged gameplay proof is `archive/screenshots/2026-03-12-session-mesh-compressed-fix-gameplay3/`
- that run reaches `game_screen`, stays stable through frame 4200, and logs `angel04.1.mesh` as `compressed=1`, `invalidRefs=0`, `maxBoneIndex=14`

| Step | Status | Session |
|------|--------|---------|
| 1. Restore animation lifetime | Done | [Session 52](../sessions/2026-03-12-session52-animation-lifetime.md) |
| 2. Stop synthetic panel entry | Done | [Session 53](../sessions/2026-03-12-session53-step2-paneldir.md) |
| 3. Remove visibility masks | Done (2/3 removed) | [Session 54](../sessions/2026-03-12-session54-step3-visibility.md) |
| 4. Revisit parity bridges | Done (1 removed) | [Session 55](../sessions/2026-03-12-session55-step4-parity-bridges.md) |

## Session Log

| Session | Date | Summary |
|---------|------|---------|
| 48 | 2026-03-11 | [Initial native UI hacks](../sessions/2026-03-11-session48-native-ui-hacks.md) — Flow activation, PropAnim forcing, label alpha force |
| 49 | 2026-03-11 | [Camera fixes](../sessions/2026-03-11-session49-camera-fixes.md) — Scene camera selection, XYZ uniform bug, entering=true removal |
| 51 | 2026-03-11 | [Validation](../sessions/2026-03-11-session51-validation.md) — Screenshot A/B sweeps, GPU captures, flow tracing |
| 52 | 2026-03-12 | [Animation lifetime fix](../sessions/2026-03-12-session52-animation-lifetime.md) — Timer-based enter anim, transition waits, alpha hack removal |
| 53 | 2026-03-12 | [Step 2: PanelDir refactor](../sessions/2026-03-12-session53-step2-paneldir.md) — PropAnim forcing removed, flow activation narrowed to startMode=0 |
| 54 | 2026-03-12 | [Step 3: Visibility masks](../sessions/2026-03-12-session54-step3-visibility.md) — Overlay filter + voicetip hide removed; alpha floor + Kinect filter kept |
| 55 | 2026-03-12 | [Step 4: Parity bridges](../sessions/2026-03-12-session55-step4-parity-bridges.md) — PanelDir dir-hide removed; remaining bridges classified as permanent |
| 56 | 2026-03-12 | [Alpha floor + panel unload investigation](../sessions/2026-03-12-session56-alpha-floor-investigation.md) — Traced 29 zero-alpha meshes; older unload hypothesis later refined into lower-level FlowNode + RndGroup teardown bugs |
| 57 | 2026-03-12 | Menu→song pipeline — Fixed 10+ crashes across SongPreview, HamGameData, MultiUserGesturePanel, SkeletonIdentifier, LoadingPanel, PreloadPanel, ProfileMgr, MoveMgr, GestureMgr, HamDirector. Game reaches `game_screen` with active gameplay HUD. |
| 58 | 2026-03-12 | [Animation pipeline verification](../sessions/2026-03-12-session58-ui-animation-verification.md) — Full end-to-end trace of Flow→FlowAnimate→AnimTask→PropAnim chain. All working. Diagnostic logging removed. |

## Unwind Plan

### Step 1: Restore animation lifetime (DONE)

- HamNavList enter animation runs via timer instead of dummy AnimTask
- UIManager transition waits re-enabled with frame-based timeouts
- Text alpha force hacks removed (alpha now driven by PropAnim forcing)

### Step 2: Stop synthetic panel entry (DONE)

- PropAnim end-frame forcing removed entirely (was a no-op after Step 1's timer fix)
- Flow activation narrowed: `startMode>0` flows auto-start via `Flow::Enter()`; only `startMode==0` (game-code-triggered) flows still get blanket-activated
- Remaining flow activation is necessary — `startMode=0` flows drive visual state that DTA enter scripts would trigger on Xbox

### Step 3: Remove visibility masks (PARTIAL)

- Overlay filter (small white texture check) removed — flows now animate PropAnim overlays correctly
- HelpBarPanel voice-tip hiding removed — redundant with MeshFilter
- **Remaining (still load-bearing):**
  - Zero-alpha floor — background meshes stay invisible without it (DTA scripts needed)
  - Kinect mesh filter — white opaque overlays appear without it

### Step 4: Revisit broad parity bridges (DONE)

- PanelDir Kinect/tutorial dir-hide block removed — redundant with MeshFilter
- Most other bridges remain native infrastructure:
  - Boot flow auto-advance (DTA handlers don't work on native)
  - Controller mode forcing (no Kinect gesture input)
  - Tutorial suppression (gesture UI conflicts with controller mode)

### Panel Unload Revalidation (UPDATED)

- The old "immediate ObjRef SIGSEGV" claim is obsolete. The validated chain is now:
  - `ObjectLifetimeTest.DeleteAutosaveWarningRawDir` originally hung on `delete dir`
  - `ObjectLifetimeTest.DeleteAutosavingIconSubdirOnly` originally hung on the detached `autosaving_icon` subdir
- Root causes confirmed and fixed:
  - `FlowNode::~FlowNode()` spun on null `mChildNodes` tombstones after child delete
  - `RndGroup::Replace()` failed to remove deleted members from its owner-control `mObjects` list, later crashing `ObjPtrList::clear()` during `RndGroup` teardown
- First full-binary forced-unload retest exposed one additional issue, but it was **not** a lifetime bug:
  - `main_screen` enter hit a runtime undefined symbol for `ObjRefConcrete<FlowNode, ObjectDir>::CopyRef`
  - native needed an explicit `FlowNode` `ObjRefConcrete` instantiation in `native/src/native_link_glue.cpp`
- After that link-glue fix, the real app boot path now survives end-to-end with forced unload:
  - `attract_screen -> autosave_warning_screen -> title_screen -> wait_main_after_saveload_screen -> main_screen -> choose_mode_screen`
  - the default path now uses real `UnloadPanels()` again and remains stable through a 1000-frame soak on `choose_mode_screen`
  - screenshot evidence is in `archive/screenshots/2026-03-12-session072504/` and `archive/screenshots/2026-03-12-session073910-default-unload/`
- Current conclusion: the old unload blocker was **not** topological deletion or a generic ObjRef crash. It was a pair of concrete teardown producer bugs in flow/group containers, followed by one native-only missing template instantiation. With those fixed, the `UIScreen` unload workaround is no longer needed.

## Hack Inventory

### A. UI animation/state forcing

| File | Hack | Status |
|------|------|--------|
| `PanelDir.cpp` | Flow activation for `startMode=0` flows via `ShouldActivateNativeFlow()` | Narrowed (S53) — only game-code-triggered flows |
| ~~`PanelDir.cpp`~~ | ~~Force `"enter"` PropAnims to end frame (+ nested RndDirs)~~ | **Removed (S53)** |
| ~~`PanelDir.cpp`~~ | ~~Hide tutorial/gesture/silhouette subdirs on enter~~ | **Removed (S55)** — redundant with MeshFilter |
| ~~`HamListRibbon.cpp`~~ | ~~Label alpha force to 1.0~~ | **Removed (S52)** |
| ~~`HamNavList.cpp`~~ | ~~Native StopAnimation + frame reset~~ | **Replaced (S52)** |
| ~~`HamNavList.cpp`~~ | ~~Input bypass of IsAnimating check~~ | **Fixed (S52)** |
| `UIListMesh.cpp` | Temporarily force hidden template meshes visible | Keep |

### B. Renderer/material heuristics

| File | Hack | Status |
|------|------|--------|
| ~~`MaterialSetup.cpp`~~ | ~~Text-only alpha force~~ | **Removed (S52)** |
| `Mesh_Wgpu.cpp` | SrcAlpha zero-alpha floor (0.20) | `#if 0` — testing removal (S58) |
| `MeshFilter.cpp` | Skip Kinect/speech/tutorial meshes | Keep — still load-bearing (S54) |
| ~~`MeshFilter.cpp`~~ | ~~Skip tiny white srcAlpha overlays~~ | **Removed (S54)** — flows now animate overlays |
| `MaterialSetup.cpp` | Auto-prelit (`#if 0` S58), specular clamp, emissive guard, etc. | Auto-prelit disabled; other heuristics kept |
| `MeshGpuCache.cpp` | `FixZeroAlpha()` vertex alpha fix | Keep (compatibility) |

### C. Camera/composition

| File | Hack | Status |
|------|------|--------|
| `HamNavList.cpp` | Scene camera selection (`turbo_shell.cam`) | **Real fix** — keep |
| `Rnd_Wgpu.cpp` | XYZ camera change detection | **Real fix** — keep |
| `UI.cpp` | `MILO_UI_CAM_MODE` debug overrides | Debug-only |
| `UI.cpp` | Single-pass camera/environment selection | Architectural bridge |

### D. Controller/Kinect bypasses

| File | Hack | Status |
|------|------|--------|
| `GestureMgr_Native.cpp` | Force controller mode | Permanent — no Kinect |
| `HamScreen.cpp` | Force controller mode on first enter | Permanent — no Kinect |
| `ShellInput.cpp` | Skip Kinect init, never exit controller mode | Permanent — no Kinect |
| `CursorPanel.cpp` | Skip gesture cursor logic | Permanent — no Kinect |
| ~~`HelpBarPanel.cpp`~~ | ~~Hide voice-tip drawables~~ | **Removed (S54)** — redundant with MeshFilter |

### E. Song loading pipeline (S57)

| File | Hack | Status |
|------|------|--------|
| `SongPreview.cpp` | Early return when `!mInitted` | Permanent — no audio init |
| `HamGameData.cpp` | MILO_FAIL→MILO_WARN for outfit remap | Permanent — outfit DB subset |
| `HamGameData.cpp` | `IsSkeletonPresent()` always true | Permanent — no Kinect |
| `MultiUserGesturePanel.cpp` | Auto-skip to loading_screen in Poll | Permanent — Kinect skeleton chooser |
| `MultiUserGesturePanel.cpp` | Null guards for SkeletonChooser | Permanent — no Kinect |
| `LoadingPanel.cpp` | Skip PlayLoadingMusic + audio stream | Permanent — no MIDI/mogg |
| `PreloadPanel.cpp` | Skip content mount/cache, set success | Permanent — no ark song content |
| `ProfileMgr.cpp` | Lazy InitSliders in SliderIxToDb | Permanent — init order diff |
| `GestureMgr.cpp` | Safe defaults for LiveCameraInput handlers | Permanent — no Kinect camera |
| `HamDirector.cpp` | Null guard in FindNextDircut | Keep — no dircut data loaded |
| `Game.cpp` | Null guard for TheMoveMgr in LoadSong/Reset | Keep — MoveMgr not initialized |
| `Game.cpp` | Null guard for mMoveDir in Reset | Keep — no move assets |

### F. Screen/panel lifecycle (pre-S57)

| File | Hack | Status |
|------|------|--------|
| `UI.cpp` | Auto-advance stuck boot/tutorial screens | Permanent — DTA handlers fail |
| ~~`UI.cpp`~~ | ~~Skip transition exit/enter waits~~ | **Replaced (S52)** — timeouts |
| `UI.cpp` | Set `mSink = current screen` | Permanent — input routing |
| `UIPanel.cpp` | Synchronous panel loading | Permanent — LoadMgr queue issue |
| `UIPanel.cpp` | Force-finish panels without loader | Permanent — no DLC/save state |
| `UIPanel.cpp` | Block tutorial panel enter | Permanent — gesture UI conflicts |
| `UIScreen.cpp` | Skip null panels in SetTypeDef | Permanent — defensive |
| ~~`UIScreen.cpp`~~ | ~~Always load all panels~~ | **Removed** — default path uses normal panel reference/load logic again |
| ~~`UIScreen.cpp`~~ | ~~Hide previous screen instead of unload~~ | **Removed** — default path uses `UnloadPanels()` again |
| `UIScreen.cpp` | Skip tutorial panels on enter | Permanent — gesture UI conflicts |

## Follow-Up Items

- **Fresh binary revalidation on current checkout**: the current binary behavior is:
  - `archive/screenshots/2026-03-12-session-recheck-song-loading/` shows `song_select_screen` is now stable for 10000 frames. That run did not leave song select because the scripted `confirm` at frame 900 selected the `LEGIT` header (`song_tier_4`), not a playable song row.
  - `archive/screenshots/2026-03-12-session-recheck-song-loading-ymca/` shows the actual gameplay path still works. Scripted input `520 confirm`, `700 confirm`, `860 down`, `900 down`, `940 down`, `980 confirm` moves the highlight to `YMCA`, then transitions:
    - `song_select_screen -> multiuser_screen -> loading_screen -> preloading_screen -> real_loading_screen -> game_screen`
    - the older run remained in a black/pink-placeholder `game_screen`, but that is no longer the latest truth
  - representative screenshots:
    - `frame_00820.png` / `frame_00900.png`: expanded song list on `song_select_screen`
    - `frame_00980.png`: `YMCA` highlighted before confirm
    - `frame_01060.png`, `frame_01400.png`, `frame_02200.png`: stable `game_screen`
  - newer bring-up runs changed the next blockers again:
    - `archive/screenshots/2026-03-12-session-crowd-merge-trace-131011/` pinned the old crash to `MergeObject()` on colliding `CharClip` data inside `crowd_clips.fm`, specifically `crouching_bad_01` during `female_medium.milo -> female_base.milo`
    - native-only `CharClip::Transitions` fixes let that merge complete
    - the next two runtime gaps were then exposed and fixed:
      - missing `RndTexBlendController::GetBlendState()`
      - native `WgpuShaderMgr` / `WgpuRnd` not creating the shared `mWorkMat` that `RndTexBlender` needs
    - `archive/screenshots/2026-03-12-session-mainchar-onloadsong-fix10/` is the current truth: gameplay is stable through frame 4800, venue/crowd/HUD render, and the main character is present but corrupted
  Treat the old song-select crash, old crowd-clip merge crash, old `RndShadowMap::PrepShadow` runtime gap, and old `RndTexBlendController` / shader-manager crashes as resolved. The current blocker is later character-render correctness.
- **Panel unload teardown**: the older Session 56 note that blamed `FlowSwitchCase` and hash-table deletion order is superseded. The verified chain is now:
  - deleting a flow child leaves a null `ObjPtrVec` tombstone under suppressed ref erasure
  - `FlowNode::~FlowNode()` used to spin forever on that tombstone
  - once that was fixed, `RndGroup::Replace()` was shown to leave deleted members in its owner-control `ObjPtrList`
  - `RndGroup` teardown then crashed in native `ObjPtrList::Unlink()` / `clear()`
  - after both fixes, `ObjectLifetimeTest.DeleteAutosaveWarningRawDir` and `ObjectLifetimeTest.DeleteAutosavingIconSubdirOnly` both pass quickly
  - the first end-to-end binary retest then failed on a missing native `ObjRefConcrete<FlowNode, ObjectDir>::CopyRef` export, fixed by explicit instantiation in `native_link_glue.cpp`
  - after that, forced-unload validation reached `choose_mode_screen` and remained stable for 1000 frames
  - `UIScreen` now uses the normal unload/load path by default
  Treat this as a resolved producer-bug chain for the real boot path too. The next work is wider interactive validation, not keeping a screen-lifecycle workaround in place.
- **Alpha floor refinement**: 29 meshes hit the zero-alpha floor (0.20) across 5 dirs. All are DTA-script-driven — no Flow objects target their material alpha. The `background` and `main_ribbon` dirs have NO associated flows at all; `letterbox` flows are filtered; `game_mode_icon` flows don't target alpha. **Now `#if 0`'d (S58)** — menu renders correctly without it since Flow animations drive the material alphas for all visible UI elements. The 29 background meshes that relied on DTA scripts stay invisible, which is acceptable (they're background fill, not UI content).
- **List widget draw traversal**: `list_choose_mode.milo` loads correctly (see `docs/sessions/NEXT_DTA_LIST_RENDERING.md`), but the populated list may not reach the renderer. Investigate whether `right_hand.hnl` is in the active drawable list and whether `HamNavList::DrawShowing()` is reached. Separate from the animation unwind — revisit after Step 4.
- **Choose-mode selection path**: this blocker is now resolved. `choose_mode_panel` DTA handles `NAV_SELECT_MSG` by doing `{gamemode set_mode <mode>}` followed by `{ui goto_screen {gamemode get newsong_screen}}`. Native used to return early from [`GameMode::SetMode()`](/home/free/code/milohax/dc3-decomp/src/lazer/game/GameMode.cpp) before applying the merged mode TypeDef, which left properties like `newsong_screen` unset and caused `Perform` to fall back to `main_screen`. After restoring the shared `SetMode()` path and fixing `Hmx::Object::Property()` so TypeDef-backed properties resolve even when `mTypeProps` is null, `Perform` now correctly routes to `song_select_screen`.
- **Song-select provider gap**: this was the next blocker after `GameMode::SetMode()` was fixed. A scripted run (`archive/screenshots/2026-03-12-session-gamemode-setmode-fix/run_step2.log`) showed:
  - `main_screen` stays active until user confirm, which appears closer to Xbox than the old native auto-hop to `choose_mode_screen`
  - `confirm` on `main_screen` enters `choose_mode_screen`
  - `confirm` on `Perform` now resolves `newsong_screen` correctly and transitions into `song_select_screen`
  - native then crashes in `UIListState::SetProvider()` because `right_hand.hnl set_provider song_offer_provider` receives a null `UIListProvider`
  Root cause: native was still skipping sort/provider manager init in `MetaPanel` (`SongSortMgr`, `ChallengeSortMgr`, `PlaylistSortMgr`, `MQSongSortMgr`, `FitnessCalorieSortMgr`) and then masking the missing objects with generic DTA-name stubs in `App.cpp`. That produced a no-op `song_offer_provider` object that could accept generic DTA messages like `enter`, but failed the typed `UIListProvider` cast used by `HamNavList`. A native fix is now in progress: restore those provider-manager inits and stop stubbing `song_offer_provider` / `challenge_provider`.
- **Song-select bring-up after provider fix**: this bullet is now historical context only. After restoring those provider-manager inits and removing the `song_offer_provider` / `challenge_provider` stub fallback, follow-up fixes removed the later `song_select_screen` enter crashes as well. The fresh revalidation above is the current truth: song select is stable and a real song can be loaded end-to-end.
- **Native manager/stub inventory**: the remaining "missing manager" work is now concrete, not hypothetical. There are two classes:
  - restored now: `song_offer_provider`, `challenge_provider`
  - still native-skipped in `MetaPanel::Init()`: `SongStatusMgr::Init()`, `TheMemcardMgr.Init()`, `TheProfileMgr.Init()`, `Leaderboards::Init()`, `Challenges::Init()`, `FitnessGoalMgr::Init()`
  - still present in `App.cpp` fallback stub list: `platform_mgr`, `profile_mgr`, `content_mgr`, `challenges`, `saveload_mgr`, `speech_mgr`
  - already have real named implementations in-tree and should be audited as concrete bring-up targets: `content_mgr`, `profile_mgr`, `challenges`, `saveload_mgr`, `speech_mgr`
  Treat these as the next parity backlog now that `song_select_screen` and `game_screen` are stable. Each remaining stub should be removed only after its real native init path exists and survives a binary run.
- **Current song-loading cleanup backlog**:
  - gameplay now gets through the former `crowd_clips.fm` `CharClip` merge crash via native-only `CharClip::Transitions` fixes:
    - native transition nodes must be real `ObjOwnerPtr<CharClip>` objects before assignment in `Transitions::Load()`
    - native transition-node ownership must route `Replace()` through the `Transitions` container, not the outer `CharClip`
    - these are native-port fixes and are wrapped under `#ifdef HX_NATIVE`
  - the next hard blocker is no longer content merge or a runtime undefined symbol. It is character-render correctness after the real performer path activates:
    - native multiuser auto-skip was leaving `player_present = 0` for both slots, so `HamDirector::OnLoadSong()` treated both performers as absent and only venue/crowd rendered
    - native now explicitly marks player 0 present and clears player 1 during auto-skip, so `HamDirector::OnLoadSong()` loads `angel04` for the primary performer
    - representative evidence:
      - `archive/screenshots/2026-03-12-session-mainchar-onloadsong-fix10/frame_01400.png`
      - `archive/screenshots/2026-03-12-session-mainchar-onloadsong-fix10/frame_03000.png`
    - GPU trace evidence:
      - `archive/screenshots/2026-03-12-session-mainchar-onloadsong-fix10-gpu/ymca_20260312T142105.gfxr`
      - the converted trace contains debug names for `angel04.*.mesh`, `angel04_head.mesh`, `angel04_hair.mesh`, `lush01_bd03*.mesh`, `crowd_*`, and `newspaper*.mesh`
      - that means the remaining issue is not "character mesh never submitted". The main and backup character meshes are in the draw stream; the bug is in how those submitted character draws are posed, placed, or shaded on native
    - native mesh loading was previously one concrete bug here:
      - old native `RndMesh::LoadVertices()` eagerly decoded Xbox compressed verts into native `mVerts`
      - that path corrupted skinned weight/index data for `angel04.1.mesh` (`invalidRefs=1061`, all-zero weights on samples)
      - native now preserves the raw compressed blob and lets the renderer unpack it later
      - `MeshVertexLoading.NativeCompressedLoadPreservesRawBlob`
      - `MeshVertexLoading.CompressedSkinnedDecodePreservesBoneWeightsAndIndices`
      - `MeshVertexLoading.UncompressedVertRev26ReadsWeightsAndIndices`
      - current gameplay logs now show `angel04.1.mesh` as `compressed=1`, `invalidRefs=0`, with sane sample weights/indices
    - the visible corruption in the latest screenshots is therefore now consistent with one of:
      - bad bone transforms or animation pose application after mesh load
      - backup dancer placement/pose parity error causing `angel04` and `lush01_bd03` to overlap incorrectly
      - remaining material/tex-blender parity gap on the character path
      - duplicate character instance submission with distinct roots but overlapping world placement in the DCI venue
  - the loading/audio path remains intentionally short-circuited in `SongPreview.cpp`, `LoadingPanel.cpp`, and `PreloadPanel.cpp`
  - stale unconditional diagnostics are still compiled in (`MainMenuProvider.cpp`, `HamNavList.cpp`, `SongSortMgr.cpp`, `HamSongMgr.cpp`, `UI.cpp`, `App.cpp`) and should be removed or re-gated once bring-up work settles

## References

- **Xbox reference**: `archive/screenshots/references/dc3_main_menu.jpg`
- **Session 56 screenshots**: `archive/screenshots/session56/`
- **Forced-unload binary validation**: `archive/screenshots/2026-03-12-session072504/`
- **Default-unload binary validation**: `archive/screenshots/2026-03-12-session073910-default-unload/`
- **Choose-mode perform probe**: `archive/screenshots/2026-03-12-session074620-perform-song/`
- **GameMode/song-select provider probe**: `archive/screenshots/2026-03-12-session-gamemode-setmode-fix/`
- **Song-select post-provider probe**: `archive/screenshots/2026-03-12-session-gamemode-setmode-fix-postproviders/`
- **CharClip merge fix + shadow-path blocker**: `archive/screenshots/2026-03-12-session-charclip-ownerfix-retest2-132003/`
- **Current stable gameplay + corrupted main character**: `archive/screenshots/2026-03-12-session-mainchar-onloadsong-fix10/`
- **Current compressed-mesh fix revalidation**: `archive/screenshots/2026-03-12-session-mesh-compressed-fix-gameplay3/`
- **Current gameplay GPU capture**: `archive/screenshots/2026-03-12-session-mainchar-onloadsong-fix10-gpu/`
- **Session 57 game_screen screenshots + video**: `archive/screenshots/session57/` (`menu_to_gameplay_30fps.mp4`)
- **Session 55 screenshots**: `archive/screenshots/session55/`
- **Session 54 screenshots**: `archive/screenshots/session54/`
- **Session 53 screenshots**: `archive/screenshots/session53/`
- **Session 52 screenshots**: `archive/screenshots/session52/`
- **Session 49 screenshots**: `archive/screenshots/session49/`
- **Session 58 animation verification**: `archive/screenshots/2026-03-12-ui-animations/`
