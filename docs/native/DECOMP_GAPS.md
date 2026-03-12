# Native Port — Decomp Gaps & Missing Units

Inventory of decomp gaps affecting the native build. Prioritized by impact on rendering and UI.

**Last updated**: 2026-03-12

## Current State

- **Track A (Engine Boot)**: Boots to `choose_mode_screen`, ~169 draw calls/frame, 10000 frames stable. Menu text, icons, description panel, and ribbons all visible.
- **Current rendering (Session 48)**: choose_mode_screen shows menu items on right side with game_mode_icon description panel and icon image on the left. Key fix: `ObjDirItr::RecurseSubdirs()` only traverses formal `SubDirs()`, not nested RndDir objects in the hash table. `game_mode_icon` (42 objects, 6 PropAnims) was invisible to the PropAnim forcing code. Now fixed with separate nested RndDir iteration in `PanelDir::Enter()`.
- **Reference shots**: `archive/screenshots/references/` as live-game baseline. Native progress: `archive/screenshots/session47/` (text visible, centered), `archive/screenshots/session48/` (icon panel visible, improved layout).
- **Flow activation**: Flows activate via `ShouldActivateNativeFlow()` filter (blocks only `letterbox`). Enter PropAnims forced to end frame for positioning. Nested RndDir PropAnims (icon_enter.anim etc.) now also forced.
- **DTA/Flow/PropAnim chain**: `RndPollable::Enter()` → `HandleType("enter")` → DTA TypeDef script → `Flow::Activate()` → PropAnim → material alpha/color. DTA execution works. Zero UITrigger/EventTrigger objects exist (created dynamically, not in .milo).
- **Track B (Milo Viewer)**: Full rendering pipeline. 14/44 demo shots render (8 broken YAML paths).
- **Weak stubs**: `engine_stubs_generated.cpp` has ~2530 weak function stubs. Any real .cpp implementation automatically overrides them.

## Priority 1: Rendering & Draw Pipeline

These directly affect what's visible on screen.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `DxCam` | `Select()` | **81.3%** AT_LIMIT (prologue, ShaderMgr caching, SetViewProj forwarding) | Xbox D3D9 camera matrix setup. Fully implemented. |
| `DxCam` | `SetViewport()` | **94.0%** (mostly volatile FPR/regalloc) | Sets D3D9 viewport from RndCam frustum |
| `DxCam` | `ProjectZ(float)` | **88.8%** (offset/control flow cleanup) | Projects Z coordinate for depth sorting |
| `MeterDisplay` | `DrawShowing()` | **88.7%** (WorldInstance type, volatile FPR swaps) | Score/progress meters render |
| `Spotlight` | `DrawShowing()` | **96.8%** (stack frame, bne/beq polarity, store reorder) | Stage lighting |
| `SpotlightDrawer` | `DrawShowing()` | **100%** | Spotlight drawer selection |
| `RndTexBlender` | `DrawShowing()` | **88.6%** (regswaps, static guards) | Texture blend effects |
| `MoveDir` | `DrawShowing()` | **90.4%** (MI this-adjust, r27/r28 regswap) | Dance move overlay (debug) |
| `InlineHelp` | `DrawShowing()` | **96.2%** (r30/r31 regswap) | On-screen help text positioning |
| ~~`RndParticleSys`~~ | ~~`Poll()`~~ | **100%** | Particles now animate |
| `LabelShrinkWrapper` | `Poll()` | **Done** (pass-through to UIComponent::Poll) | Label auto-sizing |
| ~~`TexProc`~~ | ~~`DrawShowing()`, `Poll()`~~ | **Done** | Poll 100%, DrawToTexture 92.3% |

### Text Rendering

All RndText functions have native implementations in `src/system/rndobj/Text.cpp`. Stubs in `engine_stubs_generated.cpp` are dead (weak, overridden). Text rendering and font layout work correctly on native.

Remaining decomp gaps (assembly doesn't match Xbox binary) — file: `src/system/rndobj/Text.cpp`, unit: `default/system/rndobj/Text`:

| Function | Symbol | Match | Notes |
|---|---|---|---|
| `RndText::Load(BinStream&)` | `?Load@RndText@@UAAXAAVBinStream@@@Z` | 94.5% | Near-match |
| `RndText::FontMap::AllocateMeshes(RndText*, int)` | `?AllocateMeshes@FontMap@RndText@@UAAXPAV2@H@Z` | 88.1% | |
| `RndText::FitTextEllipsis()` | `?FitTextEllipsis@RndText@@IAAXXZ` | 87.3% | |
| `RndText::OnComputeCharWidths(unsigned short const*, float*, bool)` | `?OnComputeCharWidths@RndText@@IAAHPBGPAM_N@Z` | 94.8% | AT_LIMIT (regswap r16↔r17, offset swap, vbtable dispatch) |
| `RndText::FitTextScroll()` | `?FitTextScroll@RndText@@IAAXXZ` | 96.7% | AT_LIMIT (regswap) |
| `RndText::UpdateScrollOffsets()` | `?UpdateScrollOffsets@RndText@@IAAXXZ` | 97.8% | AT_LIMIT (regswap) |
| `RndText::ParseMarkup(unsigned short const*, RndText::StyleState&, unsigned short&)` | `?ParseMarkup@RndText@@IAAPBGPBGAAVStyleState@1@AAG@Z` | 97.2% | AT_LIMIT (regswap) |
| `RndText::SizeCheck()` | `?SizeCheck@RndText@@IAAXXZ` | 96.5% | AT_LIMIT (regswap, offset swap) |

These are decomp accuracy targets, not native port blockers. Native text rendering works correctly regardless.

- `FitTextJust()` — **Implemented** (binary search size fitting). Was missing definition causing `undefined symbol` crash on native.
- `AllocateMeshes()` — Clamps displayableChars instead of asserting (font data sometimes corrupt)
- Multiple `#ifdef HX_NATIVE` guards for font page validation and vertex allocation safety
- **Text positioning verified working** (2026-03-06): sFlipYZ, ortho projection, and coordinate pipeline all correct. Text renders at correct screen positions on `choose_mode_screen`.

### Small Regression Candidates — 2026-03-06 Baseline
- `BustAMovePanel::ShowMoveRating()` — 89.5% normalized, still likely fixable. Tested branch-local `DataNode`/typed-zero cleanup variants with no `objdiff` improvement; live mismatch is still branch cleanup/tail-merging around `moveFinishedMsg[1]`.
- `HamSongData::Load(const SongInfo *, bool, HamSongDataValidate)` — 100.0% normalized on current source.
- `RndMat::CreateMetaMaterial(bool)` — 100.0% normalized on current source. Keep shared source; do not reintroduce an `#ifdef HX_NATIVE` null-guard split here.

### Material/Shader Gaps
- `RndMat::Init()` — Skips MetaMaterial loading (`sMetaMaterials = nullptr`). MetaMaterials only control editor property permissions, not rendering. Blocked by MetaMaterial::Load() decomp gap.
- All `RndShader*::Select()` — Intentionally stubbed (24 methods). WebGPU renderer bypasses Xbox shader pipeline entirely.
- All `RndShader*::CalcShaderOpts()` — Return 0 (12 methods). Correct for native — these produce Xbox HLSL macro bitmasks.
- `PostProc_NG::DoVelocity()` — Motion blur stubbed (hardcoded ILP32 offsets)
- **Skinned mesh GPU shader**: Full bone-blending vertex shader (`vs_skinned`) exists in `native/src/gfx/standard_wgsl.inc` (542 lines). The standalone `native/shaders/standard.wgsl` has been synced to match.

## Priority 2: UI & Screen Flow

These affect menu navigation, list population, and screen transitions.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `OptionsPanel` | `OnMsg(RCJobComplete)` | **89.5%** (regswaps, addr reloc) | Token redemption / linking code |
| `OptionsPanel` | `Poll()` | **98.9%** | Options purchasing/update flow |
| ~~`ContentLoadingPanel`~~ | ~~`Poll()`~~ | **100%** (fixed: static const float for GPR caching) | Loading progress animation |
| `ContentLoadingPanel` | `ShowIfPossible()` | **100%** | Loading panel entry |
| `ProfileMgr` | `Poll()` | **99.6%** | Profile debug overlay |
| `SaveLoadManager` | `Poll()` | **100%** | Save/load state machine |
| ~~`Flow`~~ | ~~`Enter()`~~ | **Done** (81.8% match) | Flow graph entry — menus can now navigate |
| ~~`LoadingPanel`~~ | ~~`Poll()`~~ | **Done** | Real panel update logic lives in `ContentLoadingPanel.cpp` |

### UIList Poll Note
- `UIListWidget::Poll()`, `UIListSlotElement::Poll()`, and `UIListSubListElement::Poll()` are **not** real native-port gaps.
- They are trivial inline virtual defaults in source, and the symbol dump shows them folded into ICF-merged tiny functions.
- The real list polling path is already implemented and complete through `UIList::Poll()`, `UIListDir::PollWidgets()`, and `UIListSlot::Poll()`.

### Choose-Mode Follow-Up — 2026-03-11

**Session 43 discoveries**: Text/icons/ribbons ARE rendering correctly. Two key issues found:

1. **Voice-tip overlay coverage (MOSTLY FIXED)**: Kinect speech UI (grey_alpha.mesh, warning_*.mesh) drew AFTER text with full alpha, covering it. Fixed in the renderer for meshes; `HelpBarPanel::Draw()` also needed a recursive lookup fix (`Find(..., true)`) so nested `voice_tip.lbl` suppression actually reaches the live label object.

2. **PropAnim not animating (ROOT CAUSE FOUND)**: 84+ PropAnims exist across all panels but 0 UITriggers/EventTriggers exist to activate them. Investigation chain:
   - PropAnims loaded correctly from .milo (confirmed via ObjDirItr)
   - UITrigger factory registered correctly (REGISTER_OBJ_FACTORY in UIManager::Init)
   - No "Can't make" errors during loading — UITrigger class isn't in .milo binary data
   - **UITriggers are created dynamically by Flow/DTA scripts at runtime**, not stored in .milo
   - The Flow proxy system and DTA script execution path need investigation to determine why triggers aren't being created
   - AlphaForce hack (forcing alpha 0→1 on 198 SrcAlpha meshes) is a symptom, not the fix

3. **Multiply blend enabled**: Previously skipped, now allowed through. With dark background, multiply meshes produce near-zero (invisible) results — correct behavior. Debloom/overlay_colortexture already dark.

**Previous status**: camera state was one blocker, but not the only one. Debug and fresh frame-500 capture confirmed:
- `turbo_shell.cam`: worldPos=(-125.0, -663.5, -63.0) — loaded from .milo file
- Ribbons render at final Z=285.5–517.5 (after HamNavList WorldXfm applied)
- At FOV 34.5° and Y-distance 663.5, visible Z half-width ≈206, so ribbons at Z=348+ from camera are outside frustum
- **Proof**: Overriding `[ui.cam]` to Z=370 → 447 mesh draw calls (from 0). Rendering pipeline is correct.
- On Xbox, `DxCam::Select()` builds D3D view/projection matrices and CamShots/PropAnims in .milo files reposition the camera. Neither mechanism works on native yet.
- **Important follow-up**: after `HamNavList::DrawShowing()` was fixed to re-select `[ui.cam]` for list widgets, the unconditional `UI.cpp` debug override (`mCam->SetLocalPos(Vector3(0, -768, 370));`) became counterproductive. Runtime logs showed choose-mode labels/icons projecting to negative screen Y under that forced camera. The override is now opt-in via `MILO_DEBUG_UI_CAM_HACK=1` and should not be treated as the normal native path.
- Fresh frame-500 capture (`/tmp/dc3_postbuild_fix/run.log`) showed a second real source-level bug: `UIListMeshElement::Draw()` was trying to render list template meshes that were authored with `showing=false`. On native those died at the final `RndDrawable::Draw()` gate as `reason='not showing'`, even though the provider, transforms, mats, and screen positions were all correct.
- That bug is now fixed in shared source: native temporarily forces hidden `UIListMesh` template meshes visible for the duration of the slot draw, then restores their original hidden state. Regression coverage was added in `native/tests/test_rndcam_projection.cpp` (`UIListMeshDrawTemporarilyShowsHiddenTemplateMesh`).
- After that fix, frame-500 capture now emits real choose-mode icon draws (`icon_2p`, `icon_1p_plus`, `icon_1por2p`) under `[ui.cam]` instead of skipping them as hidden.
- What remains broken is the shared-panel composition path: `choose_mode_panel` still draws through `PanelDir 'main'` with `camOverride=turbo_shell.cam`, while the live list payload is reselected onto `[ui.cam]`. The remaining artifact is now a mixed-camera shell/layout problem, not a provider-population or basic list-widget problem.

**Draw chain status** (all functions in the UI draw path):
- `PanelDir::DrawShowing()` — **100%** COMPLETE
- `RndDir::DrawShowing()` — **100%** COMPLETE
- `UIListDir::DrawWidgets()` — **100%** COMPLETE
- `UIListDir::SetElementPos()` — **100%** COMPLETE
- `UIListSlot::Draw()` — **100%** COMPLETE
- `UIListWidget::CalcXfm()` — **100%** COMPLETE
- `RndCam::Select()` — **100%** COMPLETE
- `UIListDir::FillElements()` — **100%** COMPLETE
- `HamListRibbon::DrawShowing()` — **100%** COMPLETE
- `UIListWidget::DrawMesh()` — **93.0%** AT_LIMIT (behaviorally equivalent)
- `UIListMeshElement::Draw()` — behavioral native fix landed. Hidden template meshes now draw correctly through list slots; new regression test covers the temporary `showing=true` restore path.
- `HamListRibbon::Draw()` — **91.6%** AT_LIMIT. Fresh objdiff still points at control flow + regalloc cleanup, but the earlier `visibleCount=4→5` logic bug is fixed and producing correct ribbon spacing.
- `HamNavList::DrawShowing()` — **90.7%** AT_LIMIT. Camera re-select fixed. 11 register swap pairs (r28/r29).
- `HamListRibbon::DrawRibbon()` — **89.6%** AT_LIMIT. Fresh objdiff still shows mostly regswap/control-flow cleanup; no new clear source bug after the mesh-visibility fix.
- `UIListDir::BuildDrawState()` — **87.6%** AT_LIMIT. Zero-init fix landed. 130 diff_arg instructions.
- `RndCam::GetViewProjectXfms()` — **66.8%** normalized AT_LIMIT on current objdiff. Still needed for native `WorldToScreen()`, but the current projection tests pass, so it no longer looks like the main choose-mode blocker.
- `native/tests/test_rndcam_projection.cpp` now covers `GetViewProjectXfms()` / `WorldToScreen()` for identity perspective, translated camera, screen-subrect projection, orthographic projection, and frustum edges. All pass on native as of 2026-03-10, so any remaining issue here is likely specific-path behavior, not a basic projection math bug.
- `native/tests/test_rndcam_projection.cpp` now also covers the approximate choose-mode list coordinates captured from runtime. Those tests show the default `[ui.cam]` geometry keeps the list in-bounds, while the old forced `Z=370` debug camera pushes it off-screen vertically.
- `native/tests/test_rndcam_projection.cpp` now also covers the hidden-template `UIListMesh` case that was suppressing choose-mode icons on native.
- `RndCam::UpdateLocal()` — **99.9%** AT_LIMIT. 6 stfs offset mismatches.
- **New smoking gun from env-gated flow logging** (`MILO_DEBUG_PANEL_FLOWS=1`): blanket native Flow activation in `PanelDir::Enter()` is starting contradictory flows on the same panel. Examples:
  - `main_screen`: `select.flow`, `enter1.flow`, and `exit.flow` all activate together from `ui/main/main.milo`
  - `choose_mode_screen`: `show_game_mode_icon.flow` and `hide_game_mode_icon.flow` both activate from `ui/choose_mode/choose_mode.milo`
  - `helpbar`: `controller_mode.flow`, `exit_controller_mode.flow`, `exit_controller_mode_immediate.flow`, `start_wave_icon_display.flow`, and `end_wave_icon_display.flow` all activate together
  This is now the leading explanation for the current composition regression. The blanket Flow-start workaround is too aggressive and is no longer just a cosmetic aid.
- **What the new filter experiments proved**:
  - suppressing helpbar/letterbox auto-start fixes the giant overlay wash
  - it does **not** bring back the large readable choose-mode labels
  - therefore the remaining missing-text issue is likely in trigger-driven label/material state inside the `main` choose-mode path, not just in late fullscreen overlays
  - the newly logged `Flow::Activate()` failures in `ui/main/main.milo` are the best concrete lead for that missing state

### Screen Transition Workarounds (src/system/ui/UIScreen.cpp)
- `UnloadPanels()` — **Still skipped by default on native.** The old "ObjRef crash" attribution is stale: `MILO_NATIVE_UNLOAD_PANELS=1` shows early screen unloads can succeed, but `autosave_warning_panel` teardown later gets stuck in `UIPanel::Unload() -> RELEASE(mDir) -> ObjectDir::DeleteObjects()` for 40s+ during `autosave_warning_screen -> title_screen`. `MetaPanel::Load` null-guards fixed one old crash, but unload is not fully re-enabled.
- `SetTypeDef()` — Skips null panels

### Object System Workarounds (src/system/obj/)
- `Dir.cpp` — Two-pass object deletion to avoid dangling ObjRef walks
- `Dir.cpp` — `DeleteShared()` skipped (vtable corruption during cleanup)
- `Object.cpp` — `ReplaceRefs()` snapshots ref ring before processing
- `Object.cpp` — Multiple `if (!this)` guards for null object access
- `DataNode.cpp` — Returns safe defaults instead of crashing on bad data
- `DataFunc.cpp` — MILO_WARN instead of MILO_FAIL when objects not found
- `DirLoader.cpp` — Validates vtable after NewObject(), clears on EOF

## Priority 3: Game Logic & Animation

These affect gameplay but not the menu/rendering flow.

| Class | Method | Status | Impact |
|-------|--------|--------|--------|
| `Game` | `Poll()` | **98.0%** (FPR regswap f30/f31) | Core game loop |
| `HamCharacter` | `Poll()` | **94.4%** (volatile regswaps) | Character update |
| `RandomIntervalGroupSeqInst` | `Poll()` | **99.4%** (AT_LIMIT) | Audio sequencer interval group |
| `BinkMovieImpl` | `Poll()` | **95.6%** (offset swap, addr reloc) | Video playback |
| ~~`PhysicsManager`~~ | ~~`Poll()`~~ | **Done** | DefaultPhysicsManager::Poll() in DefaultPhysicsManager.cpp |
| ~~`GroupSeqInst`~~ | ~~`Poll()`~~ | **Done** | All subclasses implemented in Sequence.cpp |
| `StarsDisplay` | `Poll()` | **100%** | Star rating display |

### Animation/Character Workarounds
- `HamRibbon::UpdateChase()` — Stubbed (needs Interp<Transform>)
- `MoveDir::UpdateOverlay()` — Returns 0 (complex overlay rendering)
- `CharTransDraw::~CharTransDraw()` — Uses method call instead of pointer arithmetic

### Stream Deserialization Guards
- `DrivenPropertyEntry::Load()` — Caps numOps to [0, 256]
- `DrivenPropertyMathOps::Load()` — Rejects rev > 20
- `FlowNode::Load()` — Aborts if numEntries out of [0, 256]

## Priority 4: Platform/Network (Not Needed for Single-Player)

| Category | Stub Count | Notes |
|----------|-----------|-------|
| Xbox XDK APIs | ~25 | XInput, XNet, marketplace, etc. |
| Kinect/NUI APIs | ~47 | Replaced by native YOLO pose server |
| Bink Video | 6 | BinkOpen, BinkGoto, etc. |
| JSON Library | ~14 | Network serialization |
| Crypto (Rijndael) | 6 | Network encryption |
| Xbox Live | DingoServer, HttpReqCurl, WebSvcMgr, RockCentral | All Poll() stubbed |

## Recommended Next Steps

Sorted by impact x feasibility:

### Immediate (data fixes, no code risk)
1. ~~**Fix demo.yaml paths**~~ — **Done**. Removed undefined `dare_streets` scene, fixed duplicate `dare_front` key.

### Track A — Next Steps

**CRITICAL PATH: DTA TypeDef → Flow → PropAnim activation chain**

The biggest native rendering correctness gap: material alpha/color never animates because DTA-driven Flow activation doesn't fully work. Two hacks mask this:
- **AlphaForce**: the old broad `Mesh_Wgpu.cpp` alpha=1 fallback was too aggressive and has now been removed. Native still needs a narrow replacement for text meshes until Flow/PropAnim activation is reliable enough to drive font/material alpha correctly.
- **Auto-animate** (`PanelDir.cpp`): Starts ALL PropAnims simultaneously on Enter. Causes show/hide conflicts (fade-out PropAnims fight fade-in ones).
- **Blanket Flow activation** (`PanelDir.cpp`): Also too aggressive. Current runtime logging shows it starts mutually exclusive flows (`show_*` and `hide_*`, `controller_mode` and `exit_controller_mode`, `enter` and `exit`) on the same panel. Env-gated filtering now proves this is only part of the problem: once helpbar/letterbox flows are suppressed, the choose-mode plaque and icon art recover, but the large list/shell text still does not. The remaining gap is likely missing trigger-driven label/material setup within `main` / `choose_mode` itself.

**What works on native (confirmed session 43):**
- DTA TypeDef `enter` handlers fire correctly (`RndPollable::Enter()` → `HandleType("enter")`)
- DTA script execution pipeline functional (e.g., `ui_objects.dta` letterbox `enter` handler: `{hide_mic.flow activate}`)
- Flow proxy loading works (inline proxies loaded, objects created)
- PropAnims loaded from .milo with valid keyframe data (84+ across all panels)
- PropAnim::SetFrame() drives material properties correctly when called
- SyncObjects() populates mAnims vectors after loading

**What's missing — roadmap to remove hacks:**

1. **Flow activation on Enter** — Most panels' TypeDef `enter` handlers don't explicitly activate enter-transition Flows. The letterbox panel does (`{hide_mic.flow activate}`), but choose_mode only does `{$this update_postproc}`. The enter-transition animations may be triggered by:
   - Flow objects with `mStartMode > 0` (auto-start on Enter) — but proxy Flows have mStartMode=0 because PostLoad proxy path skips FlowQueueable::Load. Only `kInlineAlways` proxies get mStartMode=5.
   - EventTriggers wired to `ui_enter` events — but these need to be created first.
   - Other screen navigation events we haven't fully traced.

2. **UITrigger/EventTrigger creation** — Zero triggers exist at runtime. They're NOT in .milo binary — they're created dynamically. Possible sources:
   - Flow graph nodes that create triggers as part of their activation
   - DTA `{new UITrigger ...}` commands in untraced scripts
   - Panel-specific initialization paths not yet executed

3. **Selective PropAnim activation** — Replace blanket auto-animate with targeted activation of only enter-transition PropAnims. Requires knowing WHICH PropAnims to start (information normally provided by Flow/trigger wiring).

4. **Label/text state wiring in choose-mode** — the `menu_only` experiment shows the choose-mode plaque and card art can be made stable without helpbar/letterbox flow spam, but the big readable labels still do not appear. That suggests some label/material state is gated behind trigger-driven flows that still never start successfully, or behind `Flow::Activate()` calls that currently fail silently.
   - First concrete targets from runtime logging: `update_rank_number.flow`, `udpate_icon_state.flow`, and `update_tier.flow` in `ui/main/main.milo`
   - **2026-03-11 investigation**: All three flows DO activate correctly with children. But children are control-flow nodes (FlowSwitch, FlowCommand, FlowRun) not FlowAnimate — they depend on game state (rank, tier, icon state) to route to actual animations. `show_game_mode_icon.flow` runs `play_enter_anim.flow` whose FlowAnimate has `enable=0`. This is a game-state dependency, not a pipeline bug.

4. **Material default state** — Materials in .milo have alpha=0 as default. On Xbox, specific PropAnims animate alpha 0→1. Without selective activation, native still needs a small text-only alpha fallback. The remaining work is to remove even that by getting the real Flow→PropAnim chain working reliably.

**Hack removal dependency chain:**
```
Flow activation working → triggers created → specific PropAnims activated
  → material alpha animated correctly → AlphaForce removable
  → no show/hide conflicts → auto-animate removable
```

**Completed steps:**
- ~~**Flow::Enter()**~~ — **Done**. 81.8% match.
- ~~**Locale data loading**~~ — **Done**. 2091 symbols loaded.
- ~~**Text positioning**~~ — **Verified working** (2026-03-06).
- ~~**DxCam::Select()**~~ — **Done** (81.3% AT_LIMIT).
- ~~**Camera animation on native**~~ — Superseded. CameraManager only runs in WorldDir::Poll, not PanelDir. Camera positions come from .milo file data, not CameraManager animations.
- ~~**Voice-tip overlay coverage**~~ — **Fixed** (2026-03-11). Filtering grey_alpha.mesh and warning_*.mesh in DrawMeshImmediate.
- ~~**Re-test choose_mode_screen without camera hacks**~~ — Done. Default [ui.cam] is the baseline.

**Other rendering improvements:**
- **Shell/main panel composition** — list payload draws under `[ui.cam]`, shell overlay under `turbo_shell.cam`. Mixed-camera composition issue remains.
- **Clean up remaining debug logging** — DC3_PROPANIM, DC3_ENTER, DC3_TYPEDEF, DC3_TRANSITION removed (session 43). Check for any remaining DC3_SYNC or DC3_EVTTRIG diagnostics.

### Remaining Stubs — All Resolved
4. ~~**SaveLoadManager::Poll()**~~ — **Done** (100% match)
5. ~~**MoveDir::DrawShowing()**~~ — **Done** (88.8% match)
6. ~~**BinkMovieImpl::Poll()**~~ — **Done** (95.6% match)
7. ~~**RandomIntervalGroupSeqInst::Poll()**~~ — **Done** (99.4% match)

## Priority 5: Inlined Subdir Loading (Asset Loading Chain) — FIXED

**Last updated**: 2026-03-11

~~Files with inlined subdirs crash during loading with `FAIL: String chars N > 512`.~~ **FIXED** (2026-03-11).

**Root cause**: Missing `PanelDir` factory registration in test engine init. `REGISTER_OBJ_FACTORY(PanelDir)` lives inside `UIManager::Init()` (`UI.cpp:792`), but the test engine only called `FlowInit()`, `CharInit()`, `WorldInit()`, `HamInit()` — not the full UIManager init (too heavy for tests). Without PanelDir registered, `DirLoader::CreateObjects` produced NULL objects for `PanelDir 'hud'` in `director.milo_xbox`. The NULL object caused `ReadDead` to skip the wrong amount of data, desyncing the stream for subsequent objects — producing garbage revision fields and the `String chars N > 512` abort.

**Fix**: Added `REGISTER_OBJ_FACTORY(PanelDir)`, `REGISTER_OBJ_FACTORY(UIPanel)`, `REGISTER_OBJ_FACTORY(UIScreen)` to `native/tests/test_helpers.cpp`.

**Additional code changes** (from 6-step plan, improving PPC match%):
1. `ObjectDir::PreLoad`: Switched ~15 `bs >>` to `d >>` (BinStreamRev), added `ShouldBlockSubdirLoad` filtering, fixed pool name
2. `ObjectDir::PostLoad`: `gLoadingProxyFromDisk` → `TheLoadMgr.EditMode()`, inline condition → `ShouldSaveProxy(bs)`, removed `proxyPath` temp
3. `ObjectDir::PostLoadInlined`: `ClearAndShrink` → explicit destructor+reconstruct

**Regression tests**: `LoadWorldMasterFile` and `LoadDirectorSubdir` (converted from `EXPECT_DEATH` to normal load-and-verify).

```bash
cd native/build && ctest -R "LoadDirectorSubdir|LoadWorldMasterFile" --output-on-failure
```

### Match% Status (post-fix)

| Function | Symbol | Match | Notes |
|----------|--------|-------|-------|
| `ObjectDir::PreLoad` | `?PreLoad@ObjectDir@@UAAXAAVBinStream@@@Z` | **87.8%** (was 88.9%) | bs→d, ShouldBlockSubdirLoad, pool name — slight regression from regalloc shift |
| `ObjectDir::PostLoad` | `?PostLoad@ObjectDir@@UAAXAAVBinStream@@@Z` | **97.9%** (was 85.5%) | EditMode, ShouldSaveProxy |
| `ObjectDir::PostLoadInlined` | `?PostLoadInlined@ObjectDir@@IAA?AV?$ObjDirPtr@VObjectDir@@@@XZ` | **85.2%** (was 82.5%) | destructor+reconstruct |
| `DirLoader::LoadObjs` | `?LoadObjs@DirLoader@@IAAXXZ` | **95.3%** | No changes |
| `DirLoader::CreateObjects` | `?CreateObjects@DirLoader@@IAAXXZ` | **98.8%** | No changes |

### What Loads Successfully

All these assets load correctly, including previously-crashing inlined subdir files:
- **Inlined subdirs** (fixed): `director.milo_xbox` (RndDir w/ PanelDir 'hud', RndDir 'iconmandir'), `world/gen/world.milo_xbox` (WorldDir w/ deep subdir chain)
- 8 full venue worlds (2671-3525 objects each): glitterati, dclive, houseparty, rollerrink, bid, dci, throneroom, streetside
- Character files: main.milo_xbox (59 objects)
- Shared world subdirs: iconman, peak_spiral, phrase_meter, move_feedback, chars_base (262 objects)
- UI, SFX, Flow dirs (all archive-backed and standalone)

### Future Work

**If new `.milo_xbox` files crash on native** with "String chars N > 512" or stream desync symptoms, the likely cause is another missing `REGISTER_OBJ_FACTORY` in `test_helpers.cpp`. Diagnostic pattern:
1. Check for `MILO_NOTIFY` "unknown class" messages in test output
2. Find where the class is registered (usually in a subsystem `Init()` function)
3. Add `REGISTER_OBJ_FACTORY(ClassName)` to `test_helpers.cpp` `EnsureEngineInit()`

Currently registered for tests: `PanelDir`, `UIPanel`, `UIScreen`, `UIColor`, `UIGuide`, `UIPicture`, `UITrigger`, `UILabel`, `UIComponent`, `UIButton`, `UISlider`, `LabelNumberTicker`, `LabelShrinkWrapper`, `UILabelDir`, `InlineHelp`, `TexMovie`, `SynthSample`, `Sound`, `Sfx`, `SynthEmitter`, `Fader`, `MidiInstrument`, `MoggClip`, `MeterEffectMonitor`, `ADSR`, `ThreeDSound`, `AudioDuckerTrigger`, 12 `FxSend*` types, 6 `Sequence` types, UIList family (via `UIList::Init()`) (plus all classes from `FlowInit`, `CharInit`, `WorldInit`, `HamInit`).

**PreLoad match% regression** (88.9% → 87.8%): The `bs >> d >>` changes shifted register allocation to a new dominant r19↔r20 swap. Could potentially be recovered by reverting the `d >>` changes for reads that are functionally identical to `bs >>` (non-rev-dependent scalar types), but the native behavioral fix is more important than the PPC match% here.

### Bulk Load Test

A comprehensive test (`AssetLoadingTest.BulkLoadAllFiles`) walks the entire MILO_LIB directory and attempts to load every `.milo_xbox` file. 5,399 files across 6 categories (ui, world, char, sfx, flow, songs).

```bash
# Run all categories (slow — 5399 files)
cd native/build && ctest -R BulkLoad --output-on-failure

# Run a single category
MILO_BULK_CATEGORY=world ctest -R BulkLoad --output-on-failure
MILO_BULK_CATEGORY=ui ctest -R BulkLoad --output-on-failure

# Limit files per category (for quick smoke tests)
MILO_BULK_CATEGORY=char MILO_BULK_LIMIT=20 ctest -R BulkLoad --output-on-failure
```

Uses `MILO_FATAL_FAILS=0` so MILO_FAIL becomes non-fatal — one bad file doesn't abort the sweep.

## Reference Libraries (Cross-Validation)

Community milo/ark tools at `~/code/milohax/milo-engine-libs/harmonix-repos/`:

| Tool | Language | Milo Parse | ARK | Notes |
|------|----------|-----------|-----|-------|
| **MiloEditor** | C# .NET 8.0 | Full scene | Yes | Best reference for object types. `MiloLib/` core lib, `MiloUtil/` CLI. Validates rev (6,10,24,25,26,28,32), max symbol=512, max viewport=7, max subdirs=100 |
| **Mackiloha** | C# .NET 8.0 | Full scene | v2-10 | ARK extraction + repacking. `Ark2DirApp`, `Milo2DirApp` CLIs |
| **pikaxe** | Rust 2024 | Full scene | Yes | Successor to Mackiloha. `scene_tool milo2dir` validates structure. Handles 20+ object types |
| **PyMilo** | Python | Container | No | Quick container inspection. `MiloContainer.py` decompression |
| **LibForge** | C# .NET 4.7.1 | Full | Yes | RB4/RBVR focus. 010 Editor templates document formats |
| **Boomy** | C# + TypeScript | Bundled MiloLib | No | DC3-specific song editor |

**Key validation patterns from reference libs:**
- MiloEditor validates symbol length < 512, viewport count < 7, subdir count < 100
- Pikaxe uses `0xADDEADDE` end-marker scanning with magic-byte validation to avoid false positives in texture data
- Both auto-detect endianness: if revision > 50, assume little-endian (Forge engine)
- Class name fixups for older revisions (e.g., "RenderedTex" → "TexRenderer" for rev ≤ 24)

**Pre-extracted asset library**: `~/code/milohax/milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/` — 5,399 `.milo_xbox` files across songs (2,678), ui (1,153), sfx (1,091), char (324), world (141), flow (3).

## Priority 6: Loading Completeness & Runtime Performance

**Added**: 2026-03-11

### O(n²) HasDirPtrs Performance Fix

World files with 500+ objects (venue worlds have 2671–3525) caused the destructor chain to hang for 30+ minutes. Root cause: `ObjDirPtr::operator=(nullptr)` calls `HasDirPtrs()` which walked the entire ObjRef ring — O(n) per call, O(n²) total during `ObjectDir::~ObjectDir() → DeleteSubDirs()`.

**Fix**: Static `unordered_map<const void*, int>` counter tracks how many `ObjDirPtr`s reference each object. `HasDirPtrs()` checks the counter in O(1). All changes guarded by `#ifdef HX_NATIVE` — zero impact on PPC decomp match.

| Test | Before | After |
|------|--------|-------|
| `LoadWorldMasterFile` | 30+ min hang | **431ms** |
| `LoadFullVenueWorlds` (8 venues) | infinite hang | **11 seconds** |
| PPC `HasDirPtrs` match | 100% | **100%** (unchanged) |

**Why not add a member to `Hmx::Object`?** Shifts class layout by 8 bytes, breaking every non-virtual thunk offset in `engine_stubs_generated.cpp` (e.g., `_ZThn88_` → `_ZThn96_`). Use external storage for native-only data.

**Why not use `IsDirPtr()` in `Object::AddRef/Release`?** During `ObjRefConcrete` base class construction, vtable hasn't been updated to `ObjDirPtr` yet — `IsDirPtr()` returns false. Counter would never increment during construction but would decrement during destruction.

Files modified: `src/system/obj/Dir.h` (counter map + ObjDirPtr ctors/operator=), `src/system/obj/Dir.cpp` (O(1) HasDirPtrs on native).

Session doc: `docs/sessions/NATIVE_SUBDIR_LOADING_FIX.md`

### CharHair.cpp __frsqrte Fix

`__frsqrte` (PPC reciprocal square root estimate) is not available on x86. Added `#ifdef HX_NATIVE` guard using `1.0f / sqrtf(x)` as the native equivalent in `RecipSqrtAccurate()`.

File modified: `src/system/char/CharHair.cpp`

### Loading Completeness Analysis

The native port's object factory registrations were incomplete. `UIManager::Init()` registers ~25 UI types but is too heavy for tests (needs SystemConfig, Automator, cameras). The main `App.cpp` native path calls all subsystem inits including `UIManager::Init()`, so full runtime is fine — but the test engine (`test_helpers.cpp`) needed manual registrations.

**12 UI types added to test engine** (safe — Load functions implemented):
`UIColor`, `UIGuide`, `UIPicture`, `UITrigger`, `UILabel`, `UIComponent`, `UIButton`, `UISlider`, `LabelNumberTicker`, `LabelShrinkWrapper`, `UILabelDir` (plus `PanelDir`, `UIPanel`, `UIScreen` from earlier fix)

**Previously blocked categories — now fixed (2026-03-11):**

| Category | Types | Fix | Status |
|----------|-------|-----|--------|
| ~~TexMovie~~ | `TexMovie` | Fixed `TexMovie::Load` — wrong read order, missing movie data consumption | **Registered** |
| ~~Synth/Sound~~ | `SynthSample`, `Sound`, `Sfx`, `SynthEmitter`, `Fader`, `MidiInstrument`, `MoggClip`, `ADSR`, `ThreeDSound`, `AudioDuckerTrigger`, `MeterEffectMonitor`, 12 `FxSend*` types, 6 `Sequence` types | `SynthPreInit()` creates TheSynth singleton; `SampleData::Dealloc` null guard; `SynthSample::Init()` sets allocator | **Registered (28 types)** |
| ~~UIList family~~ | `UIList`, `UIListLabel`, `UIListMesh`, etc. | `UIList::Init()` already handled registration correctly — null dir assert was not triggered | **Registered** |
| ~~InlineHelp~~ | `InlineHelp` | Implemented `InlineHelp::PreLoad` body (mirrors Save order) | **Registered** |

### Stub Function Analysis

Of 1,117 unimplemented functions (weak stubs in `engine_stubs_generated.cpp`):

| Subsystem | Stub Count | Native Impact |
|-----------|-----------|---------------|
| Xbox platform (XDK, D3D9, Kinect, XNet) | ~400 | None — replaced by native implementations |
| Audio backend (Bink, Synth internals) | ~150 | Blocks audio playback only |
| Rendering (DxMesh, DxShader, DxCam internals) | ~120 | None — WebGPU renderer bypasses |
| Network/Online (RockCentral, DingoServer) | ~80 | None — single-player only |
| Editor/Debug tools | ~100 | None — runtime only |
| **Load/PreLoad/PostLoad** | **16** | **Blocks object type registration** |
| Game logic (HamCharacter, Crowd, MoveDir) | ~150 | Partial — some affect gameplay |
| Other (crypto, JSON, profile) | ~100 | Minimal |

**All 4 previously-blocked categories now fixed** (2026-03-11). Key fixes:
1. `InlineHelp::PreLoad` — implemented body mirroring Save order
2. `TexMovie::Load` — fixed read order (was reading `mIsLocalized` before `sRoot`), pass stream for movie data
3. Synth types — `SynthPreInit()` for TheSynth singleton, `SampleData::Dealloc` null-guard, `SynthSample::Init()` for allocator
4. UIList family — already worked via `UIList::Init()`, no fix needed

Bulk load: 200/200 UI files pass, 20/20 SFX files pass. All previously-segfaulting asset loading tests now pass.

## Key Architecture Notes

- **Weak stubs**: `native/src/engine_stubs_generated.cpp` uses `__attribute__((weak))` — any real implementation in a compiled .cpp automatically overrides the stub. **Critical**: If a class's "key function" (first non-inline virtual) is missing from its .cpp file, GCC can't emit the vtable and the weak zero-filled stub wins -> null vtable dispatch crash. Fixed for: MemStream, OvershellSlot, StreamReceiverFile, RandomIntervalGroupSeqInst.
- **ObjRef ring corruption**: Largely resolved. Two-pass deletion + ref ring snapshots + gSuppressRefErase handle object lifecycle correctly.
- **Stream deserialization**: Multiple guards against corrupt data suggest some Load() functions have bugs or version mismatches.
- **Vtable key function pattern**: If a class in `engine_stubs_generated.cpp` has zero-filled weak vtable, check if its first non-inline virtual is defined in its `.cpp` file. See `memory/MEMORY.md` for details.
