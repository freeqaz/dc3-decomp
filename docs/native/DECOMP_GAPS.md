# Native Port — Decomp Gaps & Missing Units

Inventory of decomp gaps affecting the native build. Prioritized by impact on rendering and UI.

**Last updated**: 2026-03-15

## Current State

- **Track A (Engine Boot → Gameplay)**: Boots through full menu flow to `game_screen` with 3D venue rendering via scripted button input (see "Scripted Input" section below). Current checkout reaches stable YMCA gameplay through frame 4500+. Roller rink venue with full geometry (dance floor, railing, arcade machines, neon signs), character model, HUD, and visualizer all render. 415 mesh draw calls on game_screen. The multiuser_screen DTA `enter` handler now drives the game start flow naturally — no auto-skip needed.
- **Current rendering (Session 61+)**: `game_screen` loads and renders the full venue via the FileMerger pipeline. Song → venue → viz → HUD all chain-load through `HamDirector::OnFileLoaded`. Venue is `rollerrink` (YMCA's assigned venue). Character model renders on the dance floor. 415 draw calls/frame, stable.
- **Session 63 follow-up**: `SkeletonViz` was no longer blocked by missing files but by two native-specific issues: `FileSystemRoot()` resolved to the wrong `system/run` tree on desktop native, and `SkeletonViz`'s `mResource` field was mis-typed as `ObjDirPtr<UILabelDir>` even though decomp shows `ObjDirPtr<ObjectDir>` and `ham/skeleton.milo` loads a `RndDir`. Fixing both restores `MoveDir::Enter()` and lets scripted gameplay remain stable through frame 4500.
- **Menu rendering (Sessions 47-58)**: choose_mode_screen shows menu items, game_mode_icon, ribbons, text. Flow->PropAnim animation pipeline verified working end-to-end.
- **Reference shots**: `archive/screenshots/references/` as live-game baseline.
- **Flow animation pipeline**: **Fully operational** (verified Session 58). Flow→FlowAnimate→AnimTask→PropAnim chain traced end-to-end. All UI animations use kTaskUISeconds timeline, advanced by UIManager::Poll() every frame. Enter animations fade in menu items correctly. `ShouldActivateNativeFlow()` filter handles startMode=0 flows; startMode>0 flows auto-start via Flow::Enter(). No rendering hacks needed for animation.
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
| `RndParticleSys` | `InitParticle(float,...)` | **64.6%** AT_LIMIT (28 reg swap pairs, condition inversions) | Particle initialization — native rendering works regardless |
| `RndRibbon` | `UpdateMesh()` | **61.4%** AT_LIMIT (reg swaps, float scheduling) | Ribbon tube mesh generation |
| `RndRibbon` | `UpdateChase()` | **54.7%** AT_LIMIT (reg swaps dominate) | Ribbon chase transform updates |
| `RndRibbon` | `ConstructMesh()` | **44.4%** AT_LIMIT (reg swaps, float scheduling) | Ribbon initial mesh construction |
| `NgSpotlightDrawer` | `RenderConeDefs()` | **66.3%** AT_LIMIT (Vector4 aggregate param passing) | Spotlight cone geometry |
| `BinkIntegration` | `BinkFileReadFrame()` | **84.7%** AT_LIMIT (base ptr caching, branchless conditional) | Bink video frame reading |
| `BinkIntegration` | `ReadFunc()` | **78.6%** AT_LIMIT (EndianSwap 64-bit scheduling) | Bink file I/O callback |
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

2. ~~**PropAnim not animating**~~ — **RESOLVED (Session 58)**. The Flow→PropAnim chain is fully operational. DC3 UI panels use Flow-driven PropAnims (not UITriggers/EventTriggers — zero exist in .milo). The `ShouldActivateNativeFlow()` filter activates startMode=0 flows on panel enter; FlowAnimate creates AnimTasks on the kTaskUISeconds timeline; UIManager::Poll() advances time; PropAnims animate material alpha/color. All alpha force hacks removed.

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
- `UnloadPanels()` — **Default path restored on native.** The stale "ObjRef crash" attribution was replaced by verified lower-level fixes (`FlowNode::~FlowNode()` tombstone teardown + `RndGroup::Replace()` owner-list cleanup) plus a native `ObjRefConcrete<FlowNode, ObjectDir>::CopyRef` link-glue export. After those fixes, the old native `UIScreen` unload shortcuts were removed and the default boot path still reaches `choose_mode_screen` and remains stable.
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

### Audio Pipeline (Session 64 — WORKING)

Shell music audio plays end-to-end through the full decode pipeline:

```
.mogg file → AES-128 CTR decrypt → OGG pages → vorbis decode → PCM int16
  → StandardStream::ConsumeData (float→int16) → StreamReceiverNative ring buffer
  → RenderAudio (miniaudio callback) → AudioDevice (ALSA/PulseAudio)
```

**Key components working:**
- **Mogg decryption**: Version 0xB uses hardcoded `gRB1Key`; version 0xC+ uses KeyChain-derived keys with ByteGrinder. Full AES-128 CTR via libtomcrypt.
- **VorbisReader**: Parses OGG headers, decodes via `vorbis_synthesis` (native delegates from `vorbis_synthesis_poll` stub).
- **StandardStream**: State machine (kInit→kBuffering→kReady→kPlaying→kFinished) with native synchronous pump loop in Play().
- **MetaMusic**: Start/Poll/Stop lifecycle working. Fader fade-in from silence. Loop support via `SetJump(kStreamEndMs, 0)`.
- **StreamReceiverNative**: 64KB ring buffer, int16→float conversion, volume/pan in RenderAudio callback.

**Remaining audio stubs (~8, updated 2026-03-15):**
- `SampleData` (4): Load, LoadWAV, SizeAs, SampleMarker::Load — **blocks SFX playback** (only real blocker)
- `Synth` (1): DrawMeter — debug audio meter (low priority)
- `Mic` (2+1): RingBuffer Write/Read + MicNull::GetContinuousBuf — not needed for native

**Previously listed as stubs, now DONE:**
- ~~`SampleInst::SynthPoll`~~ — **100% COMPLETE**
- ~~`Sound::SetPan`~~ — **100% COMPLETE**
- ~~`Sequence::ComputeNextTime`~~ — **97.3% AT_LIMIT**, ~~`PickNextIndex`~~ — **100% COMPLETE**
- ~~`WavReader::Poll`~~ — **94.8% AT_LIMIT**
- ~~`DelayEffect::Process`~~ — **99.4% AT_LIMIT**, ~~`SetParameter`~~ — **100% COMPLETE**
- ~~`FlangerEffect::Process`~~ — **78.8% AT_LIMIT**
- ~~`EQEffect::Reset/Process/SetParameter`~~ — **81.8%/78.2%/0.07% AT_LIMIT**
- ~~`complex::eval`~~ — no workable stubs remain in unit

**Song audio during gameplay**: Full v0xE mogg pipeline working (Session 73). Shell + song audio operational.

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

**Flow → PropAnim animation chain: VERIFIED WORKING (Session 58)**

The full DTA TypeDef → Flow → FlowAnimate → AnimTask → PropAnim pipeline has been traced end-to-end and confirmed operational. Menu enter animations (material alpha fade-in, transform transitions) run correctly without any rendering hacks.

**How it works on native:**
- `startMode > 0` flows auto-start via `Flow::Enter()` → `FlowQueueable::Execute(kQueue)` or `TheFlowMgr->QueueCommand()`
- `startMode == 0` flows activated by `ShouldActivateNativeFlow()` filter in `PanelDir::Enter()` (replaces DTA enter scripts)
- `FlowAnimate::Activate()` queues itself in FlowManager; `FlowAnimate::Execute(kQueue)` creates AnimTasks via `mAnim->Animate()`
- All UI animations use `k30_fps_ui` rate → `kTaskUISeconds` timeline, advanced by `UIManager::Poll()` every frame
- AnimTasks polled by `TaskMgr::Poll()` → `SetFrame()` on PropAnims → material alpha/color animates

**Visual confirmation**: Frame 200 (title screen) → Frame 300 (menu fading in) → Frame 400 (fully rendered main menu with DANCE/STORY/FITNESS/LIVE CHALLENGES/BUY MORE SONGS). Screenshots: `archive/screenshots/2026-03-12-ui-animations/`. Session doc: [Session 58](../sessions/2026-03-12-session58-ui-animation-verification.md).

**Remaining rendering hacks (non-animation):**
- **Zero-alpha floor** (`Mesh_Wgpu.cpp`): 29 background meshes have alpha=0 in .milo and are NOT targeted by any Flow/PropAnim — they rely on DTA property-set scripts. Floor provides reasonable default visibility.
- **Auto-prelit** (`MaterialSetup.cpp`): `#if 0`'d — UI renders correctly without it.
- **MeshFilter** (`MeshFilter.cpp`): Kinect/speech/tutorial mesh suppression — permanent (no Kinect on native).

**Completed steps:**
- ~~**Flow::Enter()**~~ — **Done**. 81.8% match.
- ~~**Locale data loading**~~ — **Done**. 2091 symbols loaded.
- ~~**Text positioning**~~ — **Verified working** (2026-03-06).
- ~~**DxCam::Select()**~~ — **Done** (81.3% AT_LIMIT).
- ~~**Camera animation on native**~~ — Superseded. CameraManager only runs in WorldDir::Poll, not PanelDir.
- ~~**Voice-tip overlay coverage**~~ — **Fixed** (2026-03-11).
- ~~**Flow→PropAnim animation chain**~~ — **Verified working** (2026-03-12, Session 58). Full pipeline traced and confirmed.
- ~~**Game screen venue rendering**~~ — **Working** (2026-03-12, Session 61). FileMerger pipeline fully operational. Song→venue→viz→HUD chain-load. Roller rink venue, 415 draw calls/frame, stable. See [Session 61](../sessions/2026-03-12-session61-merger-investigation.md).

### Track A — Current Focus: Gameplay Visual Quality + Audio

Now that game_screen renders the venue and shell music plays, focus shifts to visual quality and gameplay audio:
1. **Shell music** — **WORKING** (Session 64). Full mogg→vorbis→PCM→miniaudio pipeline.
2. **Song audio** — Uses same pipeline but untested with gameplay mogg files (version 0xC-0x10 vs shell's 0xB).
3. **Character materials** — Character model renders but needs proper material/texture application
4. **LightPreset activation** — Venue objects have correct serialized `Showing` state, but LightPresets that dynamically show/hide venue elements during gameplay need the song animation pipeline
5. **Song.anim camera shots** — Camera shot cycling during gameplay
6. **HUD textures** — Move card geometry renders as pink rectangles. Texture loading for gameplay HUD assets not connected.
7. **Game-time animation** — Song .milo contains beat-synced animations (kTaskSeconds pipeline). UI animations work (kTaskUISeconds), but gameplay animations untested.
8. **Post-processing** — Bloom, color correction, venue lighting effects stubbed.

### Remaining Stubs — All Resolved
4. ~~**SaveLoadManager::Poll()**~~ — **Done** (100% match)
5. ~~**MoveDir::DrawShowing()**~~ — **Done** (88.8% match)
6. ~~**BinkMovieImpl::Poll()**~~ — **Done** (95.6% match)
7. ~~**RandomIntervalGroupSeqInst::Poll()**~~ — **Done** (99.4% match)

### Session 59: New Implementations
| Function | File | Match | Notes |
|----------|------|-------|-------|
| `RndShadowMap::PrepShadow()` | ShadowMap.cpp | N/A (new) | Full shadow map setup — light frustum, depth pass, shadow map assignment |
| `RndFlare::CalcRect()` | Flare.cpp | N/A (new) | Screen-space flare rect with HiResScreen support, visible area clipping |
| `SpotlightDrawer::RemoveFromLists()` | SpotlightDrawer.cpp | N/A (new) | Removes spotlight from static/dynamic entry lists |
| `RndTexBlendController::GetBlendState()` | TexBlendController.cpp | N/A (new) | Camera-distance-based texture blend factor |

### Session 61: FileMerger Pipeline Fix

**Root cause**: `FileMerger::PreLoad` had `#ifndef HX_NATIVE` (commit 72538161a, 2026-03-02) suppressing `StartLoadInternal(true, true)`. This was added during early native port bring-up when the ark file system wasn't properly resolving Xbox asset paths. Once the ark system was working, the guard was no longer needed but was never removed.

**Impact**: `StartLoadInternal` does two critical things: (1) fires the `change_files` DTA message (which wires `HamDirector::mMerger` and calls `load_game_song FALSE` to select the song file), and (2) runs the loading loop that picks up selected files and starts async loading. Without step 2, the song was selected but never loaded, so the entire chain (song→venue→viz→HUD) never kicked off.

**Fix**: Removed the `#ifdef HX_NATIVE` guard — `StartLoadInternal(true, true)` now runs unconditionally. The ark system handles all file path resolution.

**Additional fixes this session**:
| Fix | File | Details |
|-----|------|---------|
| ChunkStream seek crash | SampleData.cpp | Read-and-discard instead of `bs.Seek()` under `#ifdef HX_NATIVE` |
| Undeclared `blendT` | CameraShot.cpp | Added `float blendT = f1;` in `CamShotFrame::Interp` |
| `next` variable scope | Geo.cpp | Moved declaration outside do-while loop |
| DataNode::GetObj graceful failure | DataNode.cpp | Log warning instead of crash for missing objects under `#ifdef HX_NATIVE` |

Session doc: [Session 61](../sessions/2026-03-12-session61-merger-investigation.md)

### Session 59: Safety Hacks (Technical Debt)
| Hack | File | Why | Proper Fix |
|------|------|-----|-----------|
| ObjRef ring validation + merge recovery | Object.cpp, FileMerger.cpp | Added while the ref-ring producer bug was unknown | **Root cause found in `ObjDirPtr(C*)`**: `ObjRefConcrete` already links the node in its base ctor, and the derived ctor's extra `dir->AddRef(this)` double-linked the same node and corrupted the ring. Remaining work is revalidating crowd/audio/song merges, then deleting the legacy guards. |
| SA_RESETHAND removal | main_native.cpp | Signal handler consumed after first recovery | Already fixed (permanent) |

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
| Audio backend (Bink, Synth internals) | ~150 | **Shell music WORKING** (Session 64). ~21 remaining stubs (SFX, DSP, Mic). See Tier 4 in STUB_BURNDOWN.md |
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

## Native Workarounds & Why DTA Callbacks Don't Fire

The native port has several `#ifdef HX_NATIVE` workarounds that replace behaviors normally driven by DTA scripts. This section explains WHY these are needed.

### Why DTA Callbacks Don't Fire on Native

DTA (Data Array) is Harmonix's scripting system. On Xbox, DTA scripts embedded in .milo files and loaded from the ark drive screen flow, animation lifecycle, content population, and UI initialization. **DTA IS mostly working on native** — TypeDefs load, handlers fire, commands execute. The failures are specific commands that break silently:

1. **Screen-level DTA handlers reference Xbox-only stubs.** Each screen's .milo embeds DTA TypeDef handlers (like `anim_done`, `saveload_complete`, `enter_controller_mode`). These handlers call commands on Xbox-only managers (`platform_mgr`, `profile_mgr`, `saveload_mgr`). Our smart stubs (Phase 1) handle common queries, but they **don't fire all events DTA expects** — e.g., `platform_mgr add_sink` accepts sinks but never dispatches `ui_changed` events. DTA conditionals downstream of failed commands silently skip their branches.

2. **DTA handler chains have cascading dependencies.** Example: `wait_main_after_saveload_screen` enter handler calls `{saveload_mgr activate}`, waits for `saveload_complete` message, then calls `{profile_mgr has_seen_tutorial}` and `{ui goto_screen}`. Before smart stubs, `is_idle` returned null so `saveload_complete` never fired and the entire handler chain was dead.

3. **Some DTA lifecycle management has no C++ equivalent.** On Xbox, when a screen's enter animation completes, a DTA `anim_done` handler calls `{$anim stop_animation}`. This is authored in the screen's .milo TypeDef, not in C++. On native, we start some animations from C++ directly (e.g., `HamNavList::PlayEnterAnim`), bypassing the DTA lifecycle entirely. No DTA handler exists to clean up these C++-originated animations.

4. **FlowAnimate-driven animations have a different cleanup path.** FlowAnimate actively manages its AnimTask through `Deactivate()` / `Execute()`. This works on native when the flow graph is correctly set up. But contradictory flow activations (see "blanket Flow start" issue above) and missing Xbox object references can break the cleanup chain.

### AnimTask Lifecycle (the "keepalive" mechanism)

`AnimTask::mAnimTarget` is a "keepalive" pointer. AnimTask::Poll only self-deletes when `mAnimTarget == NULL`:

```
AnimTask created → mAnimTarget = anim->AnimTarget() (non-null)
                 → plays animation frames each Poll()
                 → when mAnimTarget nulled externally:
                    → time > mFrameSpan? → send "ended" → self-delete
```

**On Xbox**, external systems null mAnimTarget:
- **FlowAnimate**: `Deactivate()` deletes the task directly, or `OnAnimEvent("ended")` nulls mAnimTarget
- **DTA handlers**: `{$anim stop_animation}` → `StopAnimation()` deletes all AnimTasks
- **Object destruction**: ObjPtr auto-nulls when target is destroyed

**On native**, for non-FlowAnimate animations (like HamNavList enter anims), nothing nulls mAnimTarget. The animation plays to its end frame but the AnimTask persists forever, making `IsAnimating()` return true indefinitely.

**Fix** (`#ifdef HX_NATIVE` in `AnimTask::Poll`): Auto-null `mAnimTarget` when a non-looping animation has played past its frame span (`time > mFrameSpan`). This replicates what the DTA cleanup chain would do.

### Active Workarounds (as of 2026-03-12)

| Workaround | File | Why Needed | Category |
|---|---|---|---|
| **AnimTask auto-null** | Anim.cpp | DTA `anim_done` handlers don't call `stop_animation` for C++-originated animations | Animation lifecycle |
| **Smart stubs** (3 classes) | App.cpp | `saveload_mgr`, `profile_mgr`, `platform_mgr` DTA commands need return values | DTA stub |
| **Screen auto-advance timer** | UI.cpp | Some screen transitions still depend on DTA handlers that fail silently | Screen flow |
| **Exit/enter animation timeouts** | UI.cpp | Safety net if animations don't complete (30/60 frame limits) | Animation lifecycle |
| **mSink = screen on transition** | UI.cpp | DTA `set_sink` handler never fires (screen-level DTA, not system config) | Button routing |
| **Controller mode force-on** | GestureMgr.cpp | DTA `enter_controller_mode` message depends on Kinect subsystem state | Input |
| **GameMode::SetMode skip** | GameMode.cpp | Property evaluation references uninitialized objects before DTA sets them | Game logic |
| **TheHamProvider property defaults** | Ham.cpp | 47+ call sites read properties with assert-on-missing before DTA initializes them | Initialization |
| **ShellInput Kinect guards** | ShellInput.cpp | mSkelIdentifier/mSkelExtTracker genuinely absent (no Kinect hardware) | Permanent |
| **SyncVoiceControl fallback** | ShellInput.cpp | No voice hardware — permanent platform difference | Permanent |
| **HamNavList IsAnimating() bypass** | HamNavList.cpp | DTA `transition_complete` handlers never fire → `IsAnimating()` stays true forever → all button input rejected | Input |

### Removed Workarounds

| Workaround | File | Removed | Replaced By |
|---|---|---|---|
| `wait_main_after_saveload_screen` timer entry | UI.cpp | Phase 1 | Smart stubs: `is_idle=1` lets DTA `saveload_complete` fire naturally |
| HamNavList timer-based `IsAnimating()` bypass | HamNavList.cpp | Phase 3 | AnimTask auto-null: real `Animate()` + `IsAnimating()` now works |
| HamNavList `mEnterAnimStartTime/Duration` members | HamNavList.h | Phase 3 | AnimTask auto-null |
| MultiUserGesturePanel auto-skip | MultiUserGesturePanel.cpp | Phase 4 | DTA `enter` handler on multiuser_screen drives game start naturally |

### Permanent Platform Differences (not workarounds)

These are correct adaptations for a non-Kinect platform:
- **ShellInput Kinect guards** — mSkelIdentifier/mSkelExtTracker are genuinely absent
- **SyncVoiceControl fallback** — no voice hardware
- **ExitControllerMode no-op** — no gesture input to re-enter from
- **8 stub managers** — Xbox Live, challenges, speech — not applicable on native

### Full plan: [DTA_FLOW_V2_PLAN.md](DTA_FLOW_V2_PLAN.md)

## Scripted Input (MILO_INPUT_SCRIPT)

The native port supports scripted button input for automated testing and agent workflows.

### Setup

Create a text file with one command per line: `frame_number button_name`

```
# comments start with #
400 confirm        # A button (kAction_Confirm)
600 confirm
800 down           # D-pad down (kAction_Down)
900 down
1000 confirm
```

Available buttons: `confirm` (A/X), `cancel` (B/Circle), `start`, `up`, `down`, `left`, `right`

### Running

```bash
MILO_HEADLESS=1 MILO_MAX_FRAMES=2000 MILO_INPUT_SCRIPT=/path/to/script.txt \
  timeout 120 native/build/dc3-native 2>&1 | grep -E "goto_screen|current="
```

Add `MILO_DEBUG_UI_FLOW=1` for verbose UI transition logging.

### Timing

- **Boot screens auto-advance** (attract → autosave → title → wait_main → main_screen) — takes ~300 frames
- **main_screen is ready at frame ~380** — first confirm should be at 400+
- **Space buttons 100+ frames apart** — tighter spacing can miss frames (engine frame counter may skip)
- **Each button is active for exactly 1 frame** — if the frame counter doesn't hit that exact number, the press is missed

### Working Menu Flow (as of 2026-03-12)

```
Frame 400: confirm → main_screen selects "gameplay" → choose_mode_screen
Frame 600: confirm → choose_mode selects "perform" → song_select_screen
Frame 800: down    → scroll song list
Frame 900: down    → scroll song list
Frame 1000: down   → scroll song list
Frame 1100: confirm → select song → multiuser_screen → loading → game_screen
```

**game_screen** is reached at ~frame 1500, stable through 2000+.

### Key Requirement: IsAnimating() Bypass

`HamNavList::OnMsg(ButtonDownMsg)` gates ALL input behind `!RndAnimatable::IsAnimating()`. On native, DTA `transition_complete` handlers that call `StopAnimation()` never fire, so `IsAnimating()` stays true forever. **The `#ifdef HX_NATIVE` bypass in `HamNavList.cpp` is required for any button input to work.**

Without it, every button press reaches the HamNavList but returns `kDataUnhandled` (the `anim=1` guard rejects it).

### Example: Full Headless Test

```bash
cat > /tmp/test_input.txt << 'EOF'
400 confirm
600 confirm
800 down
900 down
1000 down
1100 confirm
EOF

MILO_HEADLESS=1 MILO_MAX_FRAMES=2000 \
  MILO_INPUT_SCRIPT=/tmp/test_input.txt \
  timeout 120 native/build/dc3-native 2>&1 | \
  grep -E "goto_screen|GotoScreenImpl|current=" | head -20
```

Expected output includes transitions through main → choose_mode → song_select → multiuser → loading → game_screen.

## Key Architecture Notes

- **Weak stubs**: `native/src/engine_stubs_generated.cpp` uses `__attribute__((weak))` — any real implementation in a compiled .cpp automatically overrides the stub. **Critical**: If a class's "key function" (first non-inline virtual) is missing from its .cpp file, GCC can't emit the vtable and the weak zero-filled stub wins -> null vtable dispatch crash. Fixed for: MemStream, OvershellSlot, StreamReceiverFile, RandomIntervalGroupSeqInst.
- **ObjRef ring corruption**: Root cause fixed in `ObjDirPtr(C*)` (`Dir.h`) by removing an extra `AddRef` that double-linked the same ref node into an object's ring. Added a native regression test covering direct constructor behavior (`ObjectLifetimeTest.ObjDirPtrConstructorKeepsSingleWellFormedRefRingNode`). Two-pass deletion + ref ring snapshots + `gSuppressRefErase` remain as separate lifetime protections.
- **Stream deserialization**: Multiple guards against corrupt data suggest some Load() functions have bugs or version mismatches.
- **Vtable key function pattern**: If a class in `engine_stubs_generated.cpp` has zero-filled weak vtable, check if its first non-inline virtual is defined in its `.cpp` file. See `memory/MEMORY.md` for details.
