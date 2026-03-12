# UI Animation System — Native Port Status

## Current State

**Unwind plan complete.** All removable hacks removed. Most remaining items are native infrastructure. The old native panel-unload shortcuts have now been removed from `UIScreen`, and the default boot path now reaches `main_screen` cleanly on native. With the `GameMode::SetMode()` native short-circuit removed, `Perform` now routes through `choose_mode_screen` into `song_select_screen`; the current blocker has moved to native manager/provider bring-up inside song select.

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
| `Mesh_Wgpu.cpp` | SrcAlpha zero-alpha floor (0.20) | Keep — still load-bearing (S54) |
| `MeshFilter.cpp` | Skip Kinect/speech/tutorial meshes | Keep — still load-bearing (S54) |
| ~~`MeshFilter.cpp`~~ | ~~Skip tiny white srcAlpha overlays~~ | **Removed (S54)** — flows now animate overlays |
| `MaterialSetup.cpp` | Auto-prelit, specular clamp, emissive guard, etc. | Keep (renderer heuristics) |
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

### E. Screen/panel lifecycle

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
- **Alpha floor refinement**: 29 meshes hit the zero-alpha floor (0.20) across 5 dirs. All are DTA-script-driven — no Flow objects target their material alpha. The `background` and `main_ribbon` dirs have NO associated flows at all; `letterbox` flows are filtered; `game_mode_icon` flows don't target alpha. Current floor is a reasonable permanent workaround. Could be refined with per-dir alpha values or by implementing a DTA property-set subset on native.
- **List widget draw traversal**: `list_choose_mode.milo` loads correctly (see `docs/sessions/NEXT_DTA_LIST_RENDERING.md`), but the populated list may not reach the renderer. Investigate whether `right_hand.hnl` is in the active drawable list and whether `HamNavList::DrawShowing()` is reached. Separate from the animation unwind — revisit after Step 4.
- **Choose-mode selection path**: this blocker is now resolved. `choose_mode_panel` DTA handles `NAV_SELECT_MSG` by doing `{gamemode set_mode <mode>}` followed by `{ui goto_screen {gamemode get newsong_screen}}`. Native used to return early from [`GameMode::SetMode()`](/home/free/code/milohax/dc3-decomp/src/lazer/game/GameMode.cpp) before applying the merged mode TypeDef, which left properties like `newsong_screen` unset and caused `Perform` to fall back to `main_screen`. After restoring the shared `SetMode()` path and fixing `Hmx::Object::Property()` so TypeDef-backed properties resolve even when `mTypeProps` is null, `Perform` now correctly routes to `song_select_screen`.
- **Song-select provider gap**: this was the next blocker after `GameMode::SetMode()` was fixed. A scripted run (`archive/screenshots/2026-03-12-session-gamemode-setmode-fix/run_step2.log`) showed:
  - `main_screen` stays active until user confirm, which appears closer to Xbox than the old native auto-hop to `choose_mode_screen`
  - `confirm` on `main_screen` enters `choose_mode_screen`
  - `confirm` on `Perform` now resolves `newsong_screen` correctly and transitions into `song_select_screen`
  - native then crashes in `UIListState::SetProvider()` because `right_hand.hnl set_provider song_offer_provider` receives a null `UIListProvider`
  Root cause: native was still skipping sort/provider manager init in `MetaPanel` (`SongSortMgr`, `ChallengeSortMgr`, `PlaylistSortMgr`, `MQSongSortMgr`, `FitnessCalorieSortMgr`) and then masking the missing objects with generic DTA-name stubs in `App.cpp`. That produced a no-op `song_offer_provider` object that could accept generic DTA messages like `enter`, but failed the typed `UIListProvider` cast used by `HamNavList`. A native fix is now in progress: restore those provider-manager inits and stop stubbing `song_offer_provider` / `challenge_provider`.
- **Song-select bring-up after provider fix**: after restoring those provider-manager inits and removing the `song_offer_provider` / `challenge_provider` stub fallback, the same scripted path changed behavior again (`archive/screenshots/2026-03-12-session-gamemode-setmode-fix-postproviders/run_step2.log`):
  - the `song_offer_provider` null-cast crash is gone
  - `Perform` still reaches `song_select_screen`
  - native now crashes later during `song_select_screen` enter, before the first song-select screenshot frame
  This is progress: the DTA-visible provider object is now real, and the remaining crash is deeper in song-select bring-up rather than a masked missing-manager stub. Keep the provider-manager fix and debug the next `song_select_screen` enter failure from there.
- **Native manager/stub inventory**: the remaining "missing manager" work is now concrete, not hypothetical. There are two classes:
  - restored now: `song_offer_provider`, `challenge_provider`
  - still native-skipped in `MetaPanel::Init()`: `SongStatusMgr::Init()`, `TheMemcardMgr.Init()`, `TheProfileMgr.Init()`, `Leaderboards::Init()`, `Challenges::Init()`, `FitnessGoalMgr::Init()`
  - still present in `App.cpp` fallback stub list: `platform_mgr`, `profile_mgr`, `content_mgr`, `challenges`, `saveload_mgr`, `speech_mgr`
  - already have real named implementations in-tree and should be audited as concrete bring-up targets: `content_mgr`, `profile_mgr`, `challenges`, `saveload_mgr`, `speech_mgr`
  Treat these as the next parity backlog after `song_select_screen` is stable. Each remaining stub should be removed only after its real native init path exists and survives a binary run.

## References

- **Xbox reference**: `archive/screenshots/references/dc3_main_menu.jpg`
- **Session 56 screenshots**: `archive/screenshots/session56/`
- **Forced-unload binary validation**: `archive/screenshots/2026-03-12-session072504/`
- **Default-unload binary validation**: `archive/screenshots/2026-03-12-session073910-default-unload/`
- **Choose-mode perform probe**: `archive/screenshots/2026-03-12-session074620-perform-song/`
- **GameMode/song-select provider probe**: `archive/screenshots/2026-03-12-session-gamemode-setmode-fix/`
- **Song-select post-provider probe**: `archive/screenshots/2026-03-12-session-gamemode-setmode-fix-postproviders/`
- **Session 55 screenshots**: `archive/screenshots/session55/`
- **Session 54 screenshots**: `archive/screenshots/session54/`
- **Session 53 screenshots**: `archive/screenshots/session53/`
- **Session 52 screenshots**: `archive/screenshots/session52/`
- **Session 49 screenshots**: `archive/screenshots/session49/`
