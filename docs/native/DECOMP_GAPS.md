# Native Port — Decomp Gaps & Missing Units

Inventory of decomp gaps affecting the native build. Prioritized by impact on rendering and UI.

**Last updated**: 2026-03-10

## Current State

- **Track A (Engine Boot)**: Boots to `choose_mode_screen`, 51 draw calls/frame, 5000+ frames stable. `Flow::Enter()` and `Flow::Exit()` now implemented — screen navigation should work.
- **Current UI regression**: `choose_mode_screen` menu content invisible. Root cause confirmed: **camera positioning**. `turbo_shell.cam` sits at Z=-63, ribbons render at Z=285–517, frustum half-width ≈206 at that depth — ribbons are outside the view. Moving `[ui.cam]` to Z=370 produces 447 mesh draw calls (from 0), proving the rendering pipeline is correct. On Xbox, CamShots/PropAnims baked into .milo files reposition the camera; on native, the animation system isn't driving camera position.
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
| `RndText::OnComputeCharWidths(unsigned short const*, float*, bool)` | `?OnComputeCharWidths@RndText@@IAAHPBGPAM_N@Z` | 60.0% | Large function |
| `RndText::FitTextScroll()` | `?FitTextScroll@RndText@@IAAXXZ` | 12.1% | Low match |
| `RndText::UpdateScrollOffsets()` | `?UpdateScrollOffsets@RndText@@IAAXXZ` | 10.0% | Low match |
| `RndText::ParseMarkup(unsigned short const*, RndText::StyleState&, unsigned short&)` | `?ParseMarkup@RndText@@IAAPBGPBGAAVStyleState@1@AAG@Z` | 3.2% | |
| `RndText::SizeCheck()` | `?SizeCheck@RndText@@IAAXXZ` | 0.3% | Effectively unimplemented |

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

### Choose-Mode Follow-Up — 2026-03-10

**Current status**: camera state was one blocker, but not the only one. Debug and fresh frame-500 capture confirmed:
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

### Screen Transition Workarounds (src/system/ui/UIScreen.cpp)
- ~~`UnloadPanels()` — Skipped on native (ObjRef crash)~~ **FIXED**: Real crash was null `sHamMaster` in `MetaPanel::Load`, not ObjRef corruption. UnloadPanels fully re-enabled.
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
2. ~~**Flow::Enter()**~~ — **Done**. Implemented with `Flow::Exit()`. 81.8% / 99.1% match respectively.
3. ~~**Locale data loading**~~ — **Done**. 2091 symbols loaded from 2 files. Fix was proper `mInitialized` constructor init + LocalePanel vtable key function fix.
4. ~~**Text positioning**~~ — **Verified working** (2026-03-06). No alignment issues — sFlipYZ + ortho projection correct. `FitTextJust()` was missing (undefined symbol crash), now implemented.
5. ~~**DxCam::Select()**~~ — **Done** (81.3% AT_LIMIT). Fully implemented with boolean materialization, ScreenRect field-by-field copy, member_ref_bind. Remaining gaps are compiler-level (prologue, ShaderMgr caching, SetViewProj forwarding).
6. **Camera animation on native** — CamShots/PropAnims in .milo files should reposition camera for menu screens. Investigate why the Milo animation system isn't driving camera position on native. May need `RndDir::Enter()` to trigger animations, or `CameraManager` to play CamShots.
7. **Shell/main panel composition on choose_mode** — the list payload now draws under `[ui.cam]`, but `PanelDir 'main'` still selects `turbo_shell.cam` for the shared shell overlay. Trace which shell/title/overlay elements still depend on `turbo_shell.cam` and whether that camera should be animated, replaced, or split from the list pass.
8. **Re-test choose_mode_screen without `MILO_DEBUG_UI_CAM_HACK`** — now done for frame 500. Keep using the default `[ui.cam]` path as the baseline; only use the env hack for controlled experiments.

### Remaining Stubs — All Resolved
4. ~~**SaveLoadManager::Poll()**~~ — **Done** (100% match)
5. ~~**MoveDir::DrawShowing()**~~ — **Done** (88.8% match)
6. ~~**BinkMovieImpl::Poll()**~~ — **Done** (95.6% match)
7. ~~**RandomIntervalGroupSeqInst::Poll()**~~ — **Done** (99.4% match)

## Key Architecture Notes

- **Weak stubs**: `native/src/engine_stubs_generated.cpp` uses `__attribute__((weak))` — any real implementation in a compiled .cpp automatically overrides the stub. **Critical**: If a class's "key function" (first non-inline virtual) is missing from its .cpp file, GCC can't emit the vtable and the weak zero-filled stub wins -> null vtable dispatch crash. Fixed for: MemStream, OvershellSlot, StreamReceiverFile, RandomIntervalGroupSeqInst.
- **ObjRef ring corruption**: Largely resolved. Two-pass deletion + ref ring snapshots + gSuppressRefErase handle object lifecycle correctly.
- **Stream deserialization**: Multiple guards against corrupt data suggest some Load() functions have bugs or version mismatches.
- **Vtable key function pattern**: If a class in `engine_stubs_generated.cpp` has zero-filled weak vtable, check if its first non-inline virtual is defined in its `.cpp` file. See `memory/MEMORY.md` for details.
