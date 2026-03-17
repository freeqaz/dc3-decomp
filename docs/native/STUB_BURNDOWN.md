# Stub Function Burndown

Consolidated report of stub functions across the decomp, categorized by type and grouped by source file. Generated 2026-03-12, updated 2026-03-17.

**2026-03-17 status**: All core engine subsystems (rndobj, char, synth, math, flow, world, ui, obj, os) have zero workable stubs remaining. Every function is either COMPLETE or AT_LIMIT. Remaining workable functions are Kinect/gameplay-specific (FreestyleMoveRecorder, MoveDetector) or template instantiations. Audio DSP effects, SampleInst, Sound, WavReader, Sequence are all implemented.

**2026-03-17 linker stub cleanup**: Removed 1,063 dead weak stubs from `engine_stubs_generated.cpp` (4192 → 1058 lines). These were auto-generated safety nets from early native port development that were silently overridden by strong symbols as decomp source files were added.

**2026-03-17 Phase 2 burndown**: Removed 141 more stubs (364 → 223):
- **34 dead inline/template stubs** (Phase 1): `UIPanel::SetPaused`, `UIList::NumData`, `ObjDirPtr::IsLoaded`, 13x `NavListItemSortCmp::Get*Cmp`, 8x `PropSync<ObjPtrVec<T>>`, 3x `PseudoRandomPicker`, `ScaleAddEq`, `CachedRead` (2), `GatherObjectsFromGroup`, `AutoGlitchReport::EndExternal` — all had real implementations from headers compiled in other TUs.
- **107 dead static variable stubs** (Phase 2): Added proper `static` member definitions to source .cpp files (guarded with `#ifdef HX_NATIVE`) and `native_link_glue.cpp`. Classes: HamScrollBehavior (15), RndShader (6), SpotlightDrawer (5), CharEyes (5), InlineHelp (6), MoveDir (4), MetaPanel (4), RndPostProc (3), RndDrawable (3), HamNavList (3), SkeletonUpdate (3), UIPanel (2), UILabel (2), and ~40 single-static classes.
- **24 dead non-virtual thunk stubs** (Phase 3): Removed PPC-offset thunks that don't match x86_64 class layout. The native Clang compiler auto-generates correct thunks in each class's .cpp.o. Kept 1 thunk (FitnessCalorieSortMgr::Handle, offset 8) that is actually referenced.

**199 linker stubs remain** — see [Linker Stub Inventory](#linker-stub-inventory-engine_stubs_generatedcpp) below.

---

## Priority Tier List — Native Port Impact

Stubs ranked by impact on the native port roadmap (Session 59 → Milestone 4: Playable Dance Gameplay). Current state: game_screen renders 505 draw calls but scene is static, HUD shows pink rectangles, no character animation.

### TIER 1 — ~~Critical~~ COMPLETE (verified 2026-03-13)

All Tier 1 stubs have been implemented:
- **CameraShot.cpp**: `Shake`, `GetKey`, `SetPos`, `BuildTransform`, `Interp` — all implemented
- **HamDirector.cpp**: `RemapSongAnimToTempoMap` — implemented
- **HamCharacter.cpp**: `ApplyBlendedSkeletons` — implemented
- **FlowSequence.cpp**: `Activate` — implemented
- **FlowSwitchCase.cpp**: `IsValidCase` — implemented
- **FlowSound.cpp**: `OnMarkerEvent` — implemented
- **FlowTimer.cpp**: `EventTask` (ctor, dtor, ClassName, StaticClassName, Poll) — all implemented (ClassName/StaticClassName via OBJ_CLASSNAME macro)
- **Flow.cpp**: `ScanForOutPorts`, `Copy` — both implemented

### TIER 2 — ~~High~~ COMPLETE (verified 2026-03-13)

All Tier 2 functions are implemented (none were stubs). Match improvements applied this session:

| File | Function | Match% | Status |
|------|----------|--------|--------|
| **RhythmDetector.cpp** | `SetupFrame` | 86.8% | Implemented (was 0%) |
| **RhythmDetector.cpp** | `ProcessFrames` | 86.7% | Improved (was 75.8%) |
| **RhythmDetector.cpp** | `BlendFrameDataToBeat` | 67.0% | Improved (was 29.4%) |
| **RhythmDetector.cpp** | `GetRecord` | 100% | Already complete |
| **PoseFatalities.cpp** | `UpdateClipDriver` | 93.7% | Already implemented |
| **PoseFatalities.cpp** | `UpdateMatchingPose` | 85.8% | Improved (was 83.3%) |
| **PoseFatalities.cpp** | `DrawDebug` | 85.2% | Already implemented |
| **RhythmBattlePlayer.cpp** | `AnimateBoxyState` | 82.6% | Improved (was 50.4%) |
| **RhythmBattlePlayer.cpp** | `UpdateComboProgress` | 100% | Already complete |
| **GamePanel.cpp** | `UpdateNowBar` | 71.7% | Already implemented |
| **GamePanel.cpp** | `DeJitter` | 87.9% | Improved (was 85.9%) |
| **MoveMgr.cpp** | `FillRoutineFromVerses` | 100% | Improved to 100% (was 85.6%) |
| **MoveMgr.cpp** | `FillRoutineFromReplacer` | 100% | Already complete |
| **MoveMgr.cpp** | `FillInRoutineAt` | 89.6% | Improved (was 73.2%) |
| **MoveMgr.cpp** | `ComputeLoadedMoveSet` | 89.3% | Improved (was 69.9%) |
| **MoveMgr.cpp** | `ComputeRandomChoiceSet` | 78.9% | Improved (was 73.6%) |
| **SongLayout.cpp** | `SetDefaultReplacer` | 92.5% | Improved (was 75.0%) |
| **SongLayout.cpp** | `SetDefaultPattern` | 97.4% | Improved (was 93.0%) |
| **HamCamShot.cpp** | `CreateFlippedShowHideList` | 100% | Already complete |
| **HamIKEffector.cpp** | `DoFancyElbow` | 84.5% | Already implemented |
| **HollaBackMinigame.cpp** | `OnBeat` | — | Needs verification |
| **Game.cpp** | `OnDumpMoves` | 100% | Already complete |
| **Game.cpp** | `HandleWait` | 97.0% | Already implemented |
| **Game.cpp** | `OnCycleAutoplay` | 91.6% | Already implemented |
| **Game.cpp** | `OnCycleTestDancer` | 90.4% | Already implemented |

### TIER 3 — Medium: Venue Lighting & Rendering Quality (Phase 4.4 + Milestone 5)

These improve visual quality — venue lights, post-processing, line effects, spotlight volumes. The venue renders without them but looks flat/static. **Note**: SpotlightDrawer_NG and Shader stubs are Xbox D3D9/HLSL code — the native WebGPU renderer bypasses these entirely. They are decomp accuracy targets, not native blockers.

| File | Stubs | Status (updated 2026-03-15) | Impact |
|------|-------|--------|--------|
| **SpotlightDrawer.cpp** | 8 methods | All AT_LIMIT (implemented) | Xbox spotlight rendering — not needed for native WebGPU |
| **SpotlightDrawer_NG.cpp** | 15 methods | Most AT_LIMIT (implemented) | Xbox D3D9 spotlight — not needed for native |
| **Shader.cpp** | 22+ | Intentionally stubbed on native | Xbox HLSL — native uses WGSL shaders |
| **RndPostProc.cpp** | ~~`Interp`~~ | **99.0% AT_LIMIT** | Native has own post-proc pipeline |
| **RndLine.cpp** | `UpdateLine` (×2), `UpdateLinePair` (3) | **Being worked on** | Line rendering — native-relevant |
| **RndPropAnim.cpp** | ~~`ForeachKeyframe`~~ | **0.43% AT_LIMIT** (bool mask) | DTA script iteration |
| **Rnd.cpp** | `Modal` (1) | Low priority | Debug modal rendering |
| **ClipDistMap.cpp** | `Draw` (1) | Low priority | Debug visualization |
| **WorldDir.cpp** | `BitmapOverride::Sync` (1) | Low priority | Texture LOD overrides |

**Native-relevant remaining: RndLine (3 stubs). All spotlight/shader/postproc stubs are Xbox-only or AT_LIMIT.**

### TIER 4 — ~~Medium-Low~~ PARTIALLY COMPLETE: Audio Pipeline (Phase 6)

Shell music audio now plays end-to-end (Session 64). The mogg decryption → vorbis decode → PCM → miniaudio pipeline is working. Remaining stubs are for SFX, DSP effects, and song audio during gameplay.

**RESOLVED (Session 64):**
- **StandardStream.cpp**: `ConsumeData` — implemented (native `#ifdef HX_NATIVE` path), `setJumpSamplesFromMs` — implemented, `IsPastStreamJumpPointOfNoReturn` — implemented, `DoJump` — implemented
- **StreamReceiver.cpp**: `GetBytesPlayed` — implemented, `WriteData` — implemented, `Poll` — implemented (all in StreamReceiver_Native.cpp)
- **MetaMusic.cpp**: `Load` — implemented (shell music loads and plays)
- **VorbisReader.cpp**: Full decode pipeline working — `vorbis_synthesis_poll` stub replaced with real `vorbis_synthesis` delegation
- **Synth.cpp**: `InitSecurity` — fixed ByteGrinder init path for native
- **Keygen_Stub.cpp**: Full KeyChain implementation (getMasher, getKey, LCG random)
- **tomcrypt aes.c/ctr.c**: Real AES-128 CTR decryption (removed stubs from engine_stubs_generated.cpp)

**Bugs fixed (Session 64):**
1. LCG random overflow: `long` → `int` (32-bit Xbox PPC match)
2. InitSecurity early return: ByteGrinder::Init() must run on all platforms
3. setupCypher 64-bit pointer truncation: bypass DTA masterKey obfuscation on native
4. Missing Synth::Poll in native main loop
5. vorbis_synthesis_poll stub: was no-op, now delegates to real vorbis_synthesis
6. kStreamEndSamples signed comparison: -1 sentinel caused infinite Seek(0) loop
7. mPcmBuffers never resized after header parse
8. MetaMusic::Start() never called (Xbox triggers via DTA script, native needs explicit call)
9. PollStream state machine didn't handle kReady state

**REMAINING STUBS (updated 2026-03-15):**

| File | Stubs | Status | Impact |
|------|-------|--------|--------|
| **SampleData.cpp** | `Load`, `LoadWAV`, `SizeAs`, `SampleMarker::Load` (4) | **STUB** | Audio sample loading from .mogg/.wav files. Blocks SFX. |
| ~~**SampleInst.cpp**~~ | ~~`SynthPoll`~~ | **100% COMPLETE** | ~~Sample instance polling.~~ |
| ~~**Sound.cpp**~~ | ~~`SetPan`~~ | **100% COMPLETE** | ~~Stereo panning.~~ |
| ~~**Sequence.cpp**~~ | ~~`ComputeNextTime`, `PickNextIndex`~~ | **97.3% AT_LIMIT**, **100% COMPLETE** | ~~Random audio sequence scheduling.~~ |
| ~~**WavReader.cpp**~~ | ~~`Poll`~~ | **94.8% AT_LIMIT** | ~~WAV file reader polling.~~ |
| **Synth.cpp** | `DrawMeter` (1) | **STUB** | Audio level meter (debug only). |
| ~~**DelayEffect**~~ | ~~`Process`, `SetParameter`~~ | **99.4% AT_LIMIT**, **100% COMPLETE** | ~~DSP delay effect.~~ |
| ~~**FlangerEffect**~~ | ~~`Process`~~ | **78.8% AT_LIMIT** | ~~DSP flanger effect.~~ |
| ~~**EQEffect**~~ | ~~`Reset`, `Process`, `SetParameter`~~ | **81.8%**, **78.2%**, **0.07% AT_LIMIT** | ~~DSP EQ effect.~~ |
| **Mic.cpp** | `RingBuffer::Write`, `Read` (2) | **STUB** | Microphone input buffer. Not needed for native. |
| **MicNull.cpp** | `GetContinuousBuf` (1) | **STUB** | Null mic fallback. |
| **complex.cpp** | `eval` (1) | **STUB** | FFT/complex math for audio processing. |

**Remaining real stubs: ~8 (SampleData 4 + DrawMeter 1 + Mic 3). All DSP effects, SynthPoll, SetPan, WavReader::Poll, Sequence functions DONE.**
**Shell music + song audio WORKING (v0xE mogg pipeline). SFX blocked on SampleData::Load.**

### TIER 5 — Low: Meta/Online/Platform Features

Party mode, challenges, profiles, Xbox Live, save/load, memory management. These are secondary game modes or platform-specific features that aren't needed for core dance gameplay.

| Cluster | Files | Stubs | Notes |
|---------|-------|-------|-------|
| **Party/Social** | PartyModeMgr (11), ChallengeSortMgr (4), Challenges (1), AppMiniLeaderboardDisplay (2) | ~18 | Party mode, challenge leaderboards |
| **Profile/Save** | ProfileMgr (4), SaveLoadManager (3), MetagameRank (6), MetaPerformer (1) | ~14 | Player profiles, save data, ranking |
| **Kinect** | SkeletonChooser (6), SkeletonIdentifier (3) | ~9 | Kinect player selection (replaced by webcam ML) |
| **Xbox Live** | DingoSvr (7), XLSPConnection (2), DingoJob (1), WebSvcReq (1), HttpReqCurl (1) | ~12 | Online services (not applicable to native) |
| **Memory** | MemMgr (5), MemHeap (7), MemTrack (4), AllocInfo (2), Memory_Xbox (11) | ~29 | Xbox memory system (native uses libc malloc) |
| **OS/Platform** | DateTime (7), Debug (2), File (2), System (1), ContentMgr_Xbox (2), AsyncFile_Win (3), Keyboard_Xbox (1), MapFile_Xbox (1), HDCache (1), HolmesKeyboard (2) | ~22 | Platform utils, Xbox content, dev tools |
| **UI misc** | UIListDir (1), LocalePanel (1), LabelShrinkWrapper (1), InlineHelp (2), AppLabel (2) | ~7 | UI polish (label wrapping, inline help, locale) |
| **Obj/Utl** | Utl.cpp (5), Task.cpp (1), DataFile.cpp (4), Msg.cpp (1), Loader (1), Song (1), OSCMessenger (2), GlitchFinder (2), NetCacheMgr (4), Cheats (1) | ~22 | Engine utilities, DTA parser (already ported separately), debug |
| **Store** | StorePanel (1), StorePreviewMgr (3), StoreOffer (1) | ~5 | DLC store (not applicable) |
| **App** | App.cpp (1) | ~1 | Glitch reporting |
| **Char misc** | Character.cpp (1), CharClipGroup.cpp (1), CharIKHand.cpp (2), CharHair.cpp (1), Waypoint.cpp (1) | ~6 | Character subsystem edge cases |
| **Math** | Geo.cpp (8), mtx.cpp (2), DoubleExponentialSmoother (2) | ~12 | Geometry/matrix helpers (used in collision/physics) |

**Total: ~157 stubs. Unblocks: secondary modes, platform features, polish.**

### Loading Pipeline Analysis (Session 60 — Ghidra DB)

Searched 22,397 Ghidra decompilations for `mReady`/`mLoaded` references and loading state machines. **Key finding: all loading state setter functions are fully decomped at 100%.** The loading pipeline is NOT the blocker — the blockers are null-on-native subsystems and stubbed functions listed in the tiers above.

**Loading state machines (all COMPLETE in decomp):**
- `GamePanel::PollForLoading` — 5-state machine (GamePanel.cpp:908-954)
- `Game::IsLoaded` — 4-state machine (Game.cpp:712-775), state 1 needs TheMoveMgr, state 2 needs audio
- `MetaPanel::IsLoaded` — delegates to UIPanel + TheMetaMusic. Has game_screen shortcut (bypasses MetaMusic)
- `UIPanel::PollForLoading` — base panel loading (mLoaded field)

**What `mLoaded` touches (only 5 Ghidra decompilations):**
- `UIPanel::PollForLoading` — sets mLoaded when resources finish
- `MetaPanel` ctor — initializes mLoaded
- `StorePanel` ctor — initializes mLoaded
- `OptionsPanel::Poll` (×2) — checks mLoaded

**DTA config files loaded at boot** (from Ghidra string literal search):
- `ham_preinit_keep.dta`, `ham_keep.dta` — UI persistent objects
- `flow.dtb` — flow graph definitions
- `loading_screens.dtb`, `gameconfig_macros.dtb`, `system.dtb` — game config

**PartyModeMgr::Handle** has 20+ DTA handlers (add_player_to_team, finalize_team, clear_team, finalize_party, store_player_frame_pos, etc.) — all Tier 5 priority.

### Quick Reference: What to Work on When

| If you're working on... | Start with |
|---|---|
| **Getting the venue animated** | Tier 1: CameraShot, Flow*, HamDirector |
| **Character dancing** | Tier 1: HamCharacter, then Tier 2: MoveMgr |
| **Gameplay scoring working** | Tier 2: RhythmDetector, PoseFatalities, GamePanel |
| **Venue looking good** | Tier 3: SpotlightDrawer*, Shader, RndPostProc |
| **Adding sound** | Tier 4: SampleData, StandardStream, StreamReceiver |
| **Party/challenge modes** | Tier 5: PartyModeMgr, ChallengeSortMgr |
| **Web port progress** | Tier 1 Flow stubs + Tier 5 DataFile.cpp (already ported) |

---

## Summary

| Category | Count |
|---|---|
| METHOD (class methods needing .cpp implementation) | ~170 |
| FREE (free functions needing .cpp implementation) | ~65 |
| TEMPLATE (template instantiations, header-level) | ~55 |
| STDLIB/SDK (std::exception, NUISPEECH, XGRAPHICS) | ~6 |

**Note:** Query limit of 50 per pattern means additional stubs exist beyond what's captured, especially in hamobj (3099 workable), lazer (3838 workable), and rndobj (3323 workable) pools.

## Top 10 Heaviest Files

1. **Shader.cpp** — 22+ (CalcShaderOpts + Select for 12 shader subclasses, plus free functions)
2. **SpotlightDrawer_NG.cpp** — 15 methods (entire NG renderer)
3. **PartyModeMgr.cpp** — 11 methods
4. **Memory_Xbox.cpp** — 11 functions (XMem wrappers)
5. **SpotlightDrawer.cpp** — 8 methods
6. **MemHeap.cpp** — 7 methods (core allocator)
7. **DateTime.cpp** — 7 methods
8. **SkeletonChooser.cpp** — 6 methods
9. **MetagameRank.cpp** — 6 functions
10. **CameraShot.cpp** — 5 methods

---

## Non-Actionable Categories

### TEMPLATE — Header-level instantiations, not .cpp stubs

- PropSync variants: `PropSync<RndTransformable>`, `<RndDrawable>`, `<CharClip>`, `<Waypoint>`, `<Flow>`, `<RhythmDetector>`, `<Hmx::Object>` (ObjPtrVec), `<Hmx::Object>` (ObjOwnerPtr), `<MsgSinks::Sink>`, `<MsgSinks::EventSinkElem>`, `<MsgSinks::EventSink>`, PropSync(Matrix3), PropSync(Sphere), PropSync(Rect), PropSync(Box)
- **DONE** (2026-03-15): PropSync(MsgSinks), PropSync(EventSink), PropSync(EventSinkElem), PropSync(Sink) — all 7 functions at 100%
- ObjPtrList sorts: `ObjPtrList<CharBone>::sort<>`, `ObjPtrList<RndDrawable>::sort<>`
- ObjPtrList Link/stream: `ObjPtrList<Hmx::Object>::Link`, `ObjPtrList<RndAnimatable> operator<<`, `ObjPtrList<RndMat> operator<<`
- ObjPtrVec stream: `ObjPtrVec<RhythmDetector> operator<<`, `ObjPtrVec<RndMat> operator<<`
- Stream operators: `operator>><CharHair::Point>`, `operator>><CharHair::Strand>`, `operator>><TransformArea>`, `operator<<<RhythmDetector>`, `operator<<<RndMat>` (ObjPtrVec), `operator<<<RndMat>` (ObjPtrList), `operator<<<HamScrollSpeedIndicator>` (ObjDirPtr)
- FlowOutPort ObjPtrVec: `ObjPtrVec<FlowOutPort>::Node` (dtor, copy ctor, Replace, RefOwner), `ObjRefConcrete<FlowOutPort>`, `ObjPtrVec<FlowOutPort>` (insert, push_back, ReplaceNode)
- FlowTimer: `ObjPtr<FlowTimer>` dtor, `ObjRefConcrete<FlowTimer>` (SetObj, Replace), `ObjRefConcrete<CharWeightable>::CopyRef`
- Misc ObjPtrVec: `ObjPtrVec<FlowNode>::erase`, `ObjPtrVec<RndTransformable>::Node::RefOwner`, `ObjVector<Flow::DynamicPropertyEntry>::operator=`
- ObjDirItr: `ObjDirItr<UIList>` (ctor, Advance, operator++), `ObjDirItr<PanelDir>` (ctor, Advance, operator++)
- DataArray: `DataArray::Obj<DingoJob>`
- Sort: `InsertSort<StackData>`, `FastSort<3>` (LocaleChunkSort)
- PseudoRandomPicker: `PseudoRandomPicker<Symbol>::Randomize`, `::GetItem`, `PseudoRandomPicker<int>::GetItem`
- SendDataPoint: `SendDataPoint<Symbol,int,Symbol,int,Symbol,Symbol,Symbol,Symbol,Symbol,int>`, `SendDataPoint<Symbol,Symbol,Symbol,int>`
- Misc: `DrawAccessories<LensExtract>`, `StackString<4096>::StackString(const char*)`, `StackString<512>::StackString()`, `StackString<3096>::StackString()`, `ScopedState<bool,1,0>::~ScopedState`
- **DONE** (2026-03-15): `StackString<256>::operator=` — 100%

### STDLIB — Not decomp-actionable

- **DONE** (2026-03-15): `std::exception::_Copy_str`, `std::exception::operator=` — both 100%

### NUISPEECH / XGRAPHICS — Third-party SDK

- `NUISPEECH::CCfgEngineBase::GetClient`, `NUISPEECH::CUgtFilter::gathering`, `NUISPEECH::CGMClassifier::GetName`
- `XGRAPHICS::IRLoadConst::IsLoadConst`

---

## Implementable Stubs by Source File

### src/system/char/

#### Character.cpp
- `CharPollableSorter::ChangedByRecurse(CharPollableSorter::Dep*)` — protected method

#### CharClipGroup.cpp
- `Rand::Int()` — public (likely pulled in from header; may already exist in Rand.cpp)

#### CharIKHand.cpp
- ~~`void ScaleAddEq(Hmx::Quat&, const Hmx::Quat&, float)`~~ — **100% COMPLETE**
- `BinStream& operator<<(BinStream&, const CharBlendBone::ConstraintSystem&)` — FREE

#### CharHair.cpp
- ~~`void CharCollide::SyncWorldState()`~~ — **100% COMPLETE** (2026-03-15). Implemented from RB3 reference.

#### Waypoint.cpp
- `Rand::Int(int, int)` — public (likely pulled in from header)

#### ClipDistMap.cpp
- `void ClipDistMap::Draw(float, float, CharDriver*)` — public

#### CharWeightSetter.cpp
*(only template `operator<<<RndAnimatable>` — TEMPLATE)*

#### CharacterTest.cpp
*(only template `ObjPtrList<Hmx::Object>::Link` — TEMPLATE)*

---

### src/system/world/

#### Dir.cpp (WorldDir)
- `void WorldDir::BitmapOverride::Sync(bool)` — public

#### CameraShot.cpp
- `void CamShot::Shake(float, float, const Vector2&, Vector3&, Vector3&)` — protected
- `void CamShot::GetKey(float, CamShotFrame*&, CamShotFrame*&, float&)` — protected
- `void CamShotFrame::BuildTransform(RndCam*, Transform&, bool) const` — public
- `void CamShotFrame::Interp(const CamShotFrame&, float, float, RndCam*)` — public
- `bool CamShot::SetPos(CamShotFrame&, RndCam*)` — public

#### SpotlightDrawer_NG.cpp
- `void NgSpotlightDrawer::SetupFogDensityMap()` — protected
- `void NgSpotlightDrawer::RenderFogProxy()` — protected
- `void NgSpotlightDrawer::RenderSphere(Spotlight*)` — protected
- `void NgSpotlightDrawer::RenderSheet(Spotlight*)` — protected
- `bool NgSpotlightDrawer::CheckRTs(NgSpotlightDrawer::SpotlightResources*)` — static protected
- `void NgSpotlightDrawer::SetupXSection(Spotlight*, const Spotlight::BeamDef&)` — protected
- `void NgSpotlightDrawer::RenderConeDefs(Spotlight*, const Hmx::Color&)` — protected
- `void NgSpotlightDrawer::SetupFogDensityState()` — protected
- `void NgSpotlightDrawer::RenderCone(Spotlight*)` — protected
- `void NgSpotlightDrawer::RenderBeams(const Hmx::Matrix4&)` — protected
- `bool NgSpotlightDrawer::CheckCam()` — protected
- `void NgSpotlightDrawer::BlurRT(float, float)` — protected
- `void NgSpotlightDrawer::BlurRT()` — protected
- `void NgSpotlightDrawer::SetupForPostProcess()` — protected
- `void NgSpotlightDrawer::RenderScene()` — public

#### SpotlightDrawer.cpp
- `void SpotlightDrawer::DeSelect()` — public
- `void SpotlightDrawer::ApplyLightingApprox(BoxMapLighting&, float) const` — public
- `void SpotlightDrawer::DrawShadow()` — protected virtual
- `void SpotlightDrawer::UpdateBoxMap()` — public
- `void SpotDrawParams::Load(BinStreamRev&)` — public
- `void SpotlightDrawer::Load(BinStream&)` — public virtual
- `void SpotlightDrawer::DrawWorld()` — protected virtual
- `void SpotlightDrawer::ClearLights()` — public

---

### src/system/rndobj/

#### Rnd.cpp
- `void Rnd::Modal(Debug::ModalType&, FixedString&, bool)` — public

#### PostProc.cpp
- `void RndPostProc::Interp(const RndPostProc*, const RndPostProc*, float)` — public

#### Line.cpp
- `void RndLine::UpdateLine(RndLine::Point*, RndLine::Point*)` — protected
- `void RndLine::UpdateLinePair(RndLine::Point*, RndLine::Point*)` — protected
- `void RndLine::UpdateLine(const Transform&, float)` — protected

#### PropAnim.cpp
- `DataNode RndPropAnim::ForeachKeyframe(const DataArray*)` — public

#### Shader.cpp
- `void RndSpline::PrepareShader() const` — public (NOTE: may belong in Spline.cpp)
- `void CheckDistortionOpts(RndMat*, ShaderOptions&)` — FREE
- `void CheckDistortion(RndMat*)` — FREE
- `void SetColorWriteMask(const ShaderOptions&, RndMat*)` — FREE
- `RndShaderSimple::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderParticles::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderMultimesh::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderStandard::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderPostProc::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderDrawRect::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderUnwrapUV::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderVelocity::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderVelocityCamera::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderDepthVolume::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderFur::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `RndShaderSyncTrack::CalcShaderOpts(NgMat*, ShaderType, bool)` — protected virtual
- `Vector4 Hmx::Matrix4::Col4(int) const` — public (may belong in mtx.cpp)
- `Hmx::Matrix4 Hmx::operator*(const Hmx::Matrix4&, const Hmx::Matrix4&)` — FREE (may belong in mtx.cpp)
- `void CheckShadow()` — FREE
- `void CheckExtrude()` — FREE
- `RndShaderParticles::Select(RndMat*, ShaderType, bool)` — protected virtual
- `RndShaderMultimesh::Select(RndMat*, ShaderType, bool)` — protected virtual
- `RndShaderStandard::Select(RndMat*, ShaderType, bool)` — protected virtual

---

### src/system/ui/

#### UIListDir.cpp
- `(anon)::WidgetDrawSort::operator()(const UIListWidget*, const UIListWidget*) const` — anonymous namespace functor

#### LocalePanel.cpp
- `void LocalePanel::AddDirEntries(ObjectDir*, const char*)` — private

#### LabelShrinkWrapper.cpp
- `void LabelShrinkWrapper::UpdateAndDrawWrapper()` — protected

#### InlineHelp.cpp
- `BinStream& operator>>(BinStreamRev&, InlineHelp::ActionElement&)` — FREE
- `void InlineHelp::ClearActionToken(JoypadAction)` — public

---

### src/system/obj/

#### Utl.cpp
- `void FileCallbackFullPath(const char*, const char*)` — FREE
- `void FileCallback(const char*, const char*)` — FREE
- `DataNode MakeFileList(const char*, bool, bool(*)(char*))` — FREE
- `DataNode MakeFileListFullPath(const char*)` — FREE
- `void CopyTypeProperties(Hmx::Object*, Hmx::Object*)` — FREE

#### Task.cpp
- `void ScriptTask::UpdateVarsObjects(DataArray*)` — protected

#### DataFile.cpp
- `void PushBack(const DataNode&)` — FREE
- `DataInput` — FREE (C-linkage)
- `bool ParseNode()` — FREE
- `DataArray* ParseArray()` — FREE

#### Msg.cpp
- `void MsgSinks::RemovePropertySink(Hmx::Object*, DataArray*)` — public

---

### src/system/os/

#### System.cpp
- `void NormalizeSystemArgs()` — FREE

#### Debug.cpp
- `void Debug::DoCrucible(Debug::ModalType, const char*, void*)` — public
- `void Debug::Modal(Debug::ModalType&, const char*, void*)` — private

#### File.cpp
- `void RecursePatternInternal(const char*, void(*)(const char*, const char*), bool, bool)` — FREE
- `FileRecursePattern` — FREE (C-linkage)

#### DateTime.cpp
- `int DateTime::DayOfWeek() const` — public
- `int DateTime::ToDayNumber()` — public
- `void DateTime::FromDayNumber(int)` — public
- `unsigned int DateTime::ToSeconds()` — public
- `void DateTime::FromUtcToLocal()` — public
- `int DateTimeCmp(const DateTime&, const DateTime&)` — FREE
- `void DateTime::ParseDate(const char*)` — public

#### ContentMgr_Xbox.cpp
- `bool XboxContentMgr::MountContent(Symbol)` — public virtual
- `void XboxContentMgr::PollRefresh()` — public virtual

#### Keyboard_Xbox.cpp
- `int (anon)::TranslateVK(unsigned short, bool)` — anonymous namespace FREE

#### MapFile_Xbox.cpp
- `bool XboxMapFile::ParseStack(const char*, StackData*, int, FixedString&)` — static public

#### HolmesKeyboard.cpp
- `HolmesInput::HolmesInput(CWnd*)` — public ctor
- `unsigned int HolmesInput::SendJoypadMessages()` — public

#### HDCache.cpp
- `bool HDCache::WriteAsync(int, int, const void*)` — public

#### AsyncFile_Win.cpp
- `void AsyncFileWin::_WriteAsync(const void*, int)` — protected virtual
- `void AsyncFileWin::_ReadAsync(void*, int)` — protected virtual
- `bool AsyncFileWin::_ReadDone()` — protected virtual

---

### src/system/meta/

#### MemcardMgr_Xbox.cpp
- `MCResult MemcardMgr::ThreadCall_SaveGame()` — private

#### StorePanel.cpp
- `void StoreEnumJob::OnCompletion(Hmx::Object*)` — public virtual

#### StorePreviewMgr.cpp
- `Symbol PreviewDownloadCompleteMsg::Type()` — static public
- `PreviewDownloadCompleteMsg::PreviewDownloadCompleteMsg(bool, bool)` — public ctor
- `void StorePreviewMgr::Poll()` — public

#### StoreOffer.cpp
- `const TrueColor::FaceDetectionData& TrueColor::FaceDetector::GetDetectedRect() const` — public (third-party class, may be SDK)

---

### src/system/flow/

#### Flow.cpp
- `void ScanForOutPorts(ObjPtrVec<FlowOutPort>&, FlowNode*, Flow*)` — FREE
- `void Flow::Copy(const Hmx::Object*, Hmx::Object::CopyType)` — public virtual

#### FlowSequence.cpp
- `bool FlowSequence::Activate()` — public virtual

#### FlowSwitchCase.cpp
- `bool FlowSwitchCase::IsValidCase(FlowNode*, DataNode*, const DataNode*, bool)` — public

#### FlowSound.cpp
- `void FlowSound::OnMarkerEvent(Symbol)` — protected

#### FlowTimer.cpp
- `EventTask::~EventTask()` — public virtual dtor
- `Symbol EventTask::StaticClassName()` — static public
- `Symbol EventTask::ClassName() const` — public virtual
- `EventTask::EventTask(FlowTimer*, ObjPtrVec<FlowNode>*, TaskUnits, float)` — public ctor
- `void EventTask::Poll(float)` — public virtual

---

### src/system/hamobj/

#### MoveMgr.cpp
- `int MoveMgr::ComputeRandomChoiceSet(int)` — public
- `void MoveMgr::ComputeLoadedMoveSet()` — public
- `void MoveMgr::FillInRoutineAt(int, int)` — public
- `void MoveMgr::FillRoutineFromVerses(int)` — public
- `void MoveMgr::FillRoutineFromReplacer(int)` — public

#### HamDirector.cpp
- `void HamDirector::RemapSongAnimToTempoMap(TempoMap*)` — public

#### HamCharacter.cpp
- `void HamCharacter::ApplyBlendedSkeletons(HamDriver*, CharClip*, float)` — protected

#### PoseFatalities.cpp
- `void PoseFatalities::UpdateClipDriver(int)` — private
- `void PoseFatalities::DrawDebug()` — public
- `void PoseFatalities::UpdateMatchingPose(int)` — private

#### SongLayout.cpp
- `void SongLayout::SetDefaultReplacer()` — public
- `void SongLayout::SetDefaultPattern(int)` — public

#### HamCamShot.cpp
- `void HamCamShot::CreateFlippedShowHideList()` — protected

#### HamIKEffector.cpp
- `void HamIKEffector::DoFancyElbow(QuatXfm&, float)` — protected

#### RhythmDetector.cpp
- `void SetupFrame(RhythmDetector::Frame&, float, float, const Vector3*, const Vector3*, float)` — FREE
- `RhythmDetector::Frame BlendFrameDataToBeat(const RhythmDetector::Frame&, const RhythmDetector::Frame&, float)` — FREE
- `const RhythmDetector::RecordData& RhythmDetector::GetRecord(float, float, bool, Symbol, TextStream*)` — public
- `void RhythmDetector::ProcessFrames()` — private

#### RhythmBattlePlayer.cpp
- `void RhythmBattlePlayer::AnimateBoxyState(int, bool, bool)` — private
- `void RhythmBattlePlayer::UpdateComboProgress()` — public

#### HollaBackMinigame.cpp
- `void HollaBackMinigame::OnBeat()` — public

#### MoveDir.cpp
- `float (anon)::DrawDetectedBar(float, const char*, float, float, float, bool, bool)` — anon ns FREE
- `void (anon)::DrawBeatLine(float, float, float, const Hmx::Color&)` — anon ns FREE
- `float (anon)::DrawPlayClip(float, SkeletonClip*, int)` — anon ns FREE
- `float MoveDir::DetectFrac(int, int)` — public
- `float MoveDir::UpdateOverlay(RndOverlay*, float)` — public virtual
- `void MoveDir::PostUpdateFilters()` — private

---

### src/system/synth/

#### Synth.cpp
- `void Synth::DrawMeter(float&, float, float, const char*)` — public

#### Sound.cpp
- `void Sound::SetPan(float, Hmx::Object*)` — public

#### Sequence.cpp
- `void RandomIntervalGroupSeqInst::ComputeNextTime(int)` — protected
- `void RandomGroupSeq::PickNextIndex()` — public

#### Utl.cpp
- `const char* CacheWav(const char*, CacheResourceResult&)` — FREE

#### Mic.cpp
- `int RingBuffer::Write(void*, int)` — public
- `int RingBuffer::Read(void*, int)` — public

#### MicNull.cpp
- `short* MicNull::GetContinuousBuf(int&)` — public virtual

#### SampleData.cpp
- `void SampleMarker::Load(BinStream&)` — public
- `int SampleData::SizeAs(SampleData::Format) const` — public
- `void SampleData::LoadWAV(BinStream&, const FilePath&, bool)` — public
- `void SampleData::Load(BinStream&, const FilePath&)` — public

#### StandardStream.cpp
- `int StandardStream::ConsumeData(void**, int, int)` — public
- `void StandardStream::setJumpSamplesFromMs(float, float)` — private
- `bool StandardStream::IsPastStreamJumpPointOfNoReturn()` — public
- `void StandardStream::DoJump()` — private

#### SampleInst.cpp
- `void SampleInst::SynthPoll()` — public virtual

#### StreamReceiver.cpp
- `unsigned __int64 StreamReceiver::GetBytesPlayed()` — public
- `void StreamReceiver::WriteData(const void*, int)` — public
- `void StreamReceiver::Poll()` — public virtual

#### MetaMusic.cpp
- `void MetaMusic::Load(float, bool, bool)` — public

#### WavReader.cpp
- `void WavReader::Poll(float)` — public virtual

#### DelayEffect.cpp
- `void DelayEffect::Process(float*, int, int)` — public
- `void DelayEffect::SetParameter(int, float)` — public

#### FlangerEffect.cpp
- `void FlangerEffect::Process(float*, int, int)` — public

#### EQEffect.cpp
- `void EQEffect::Reset()` — public
- `void EQEffect::Process(float*, int, int)` — public
- `void EQEffect::SetParameter(int, float)` — public

#### complex.cpp
- `complex eval(complex* const, int, complex)` — FREE

#### ctr.cpp
- `int ctr_encrypt_fast(const unsigned char*, unsigned char*, unsigned long, Symmetric_CTR*)` — FREE

---

### src/system/math/

#### Geo.cpp
- `void BSPFace::OnSide(const Plane&, bool&, bool&)` — public
- `bool Intersect(const Vector3&, const Vector3&, const Box&, float&, float&)` — FREE
- `bool Intersect(const Plane&, const Box&)` — FREE
- `bool Intersect(const Triangle&, const Box&)` — FREE
- `void BSPFace::Update()` — public
- `void BSPFace::Set(const Vector3&, const Vector3&, const Vector3&)` — public
- `void Clip(const Hmx::Polygon&, const Hmx::Ray&, Hmx::Polygon&)` — FREE
- `bool Intersect(const Transform&, const Hmx::Polygon&, const BSPNode*)` — FREE

#### mtx.cpp
- `float Det(const Hmx::Matrix4&)` — FREE
- `void Invert(const Hmx::Matrix4&, Hmx::Matrix4&)` — FREE

#### DoubleExponentialSmoother.cpp
- `void Vector2DESmoother::Smooth(Vector2, float, bool)` — public
- `void Vector3DESmoother::Smooth(Vector3, float, bool)` — public

---

### src/system/net/

#### DingoSvr.cpp
- `DataNode DingoServer::OnMsg(const SigninChangedMsg&)` — protected
- `DataNode DingoServer::OnMsg(const ConnectionStatusChangedMsg&)` — protected
- `bool DingoServer::InitAndAddJob(DingoJob*, bool, bool)` — protected
- `bool DingoServer::SendAuthenticateMsg(const char*, DataPoint&, Hmx::Object*)` — private
- `void DingoServer::AddDelayedCalls()` — public
- `bool DingoServer::Authenticate(int, const char*)` — protected
- `DataNode DingoServer::OnMsg(const DingoJobCompleteMsg&)` — protected

#### XLSPConnection.cpp
- `void XLSPConnection::SetState(XLSPConnection::State)` — private
- `void XLSPConnection::Poll()` — public

#### DingoJob.cpp
- `OnlineID::OnlineID(const OnlineID&)` — public copy ctor

#### HttpReqCurl.cpp
- `unsigned int (anon)::WriteMemoryCallback(void*, unsigned int, unsigned int, void*)` — anon ns FREE

#### WebSvcReq.cpp
- `RecurseInfo::RecurseInfo(const RecurseInfo&)` — public copy ctor

---

### src/system/utl/

#### MemMgr.cpp
- `MemHeapStack& ThreadMemStack(bool)` — FREE
- `int GetCurrentHeapNum()` — FREE
- `void MemDelta(const char*, int)` — FREE
- `int MemFindHeap(const char*)` — FREE
- `void MemPrintOverview(int, char* const)` — FREE

#### Loader.cpp
- `void LoadMgr::PollFrontLoader()` — private

#### MemTrack.cpp
- `void BeginMemTrackObjectName(const char*)` — FREE
- `void EndMemTrackObjectName()` — FREE
- `void BeginMemTrackFileName(const char*)` — FREE
- `void EndMemTrackFileName()` — FREE

#### Cheats.cpp
- `int CheatsManager::OnMsg(const ButtonDownMsg&)` — private

#### Song.cpp
- ~~`void Song::SyncState()` — public~~ **REMOVED** (2026-03-17: unguarded in source, sync-wait loop guarded for native)

#### OSCMessenger.cpp
- `int OSCMessenger::MakeOSCAddress(String, char*)` — private
- `void OSCMessenger::SendOSCFloat(String, float)` — public

#### NetCacheMgr.cpp
- `NetLoaderRef& NetLoaderRef::operator=(const NetLoaderRef&)` — public
- `bool NetLoaderRef::IsLoadedOrFailed()` — public
- `void NetCacheMgr::PollLoaders()` — protected
- `NetCacheMgr::AddLoaderRef(const char*, NetCacheMgr::RefType, NetLoaderPos)` — protected

#### GlitchFinder.cpp
- `void GlitchFinder::CheckDump()` — public
- `DataNode GlitchFindScriptImpl(DataArray*, int)` — FREE

#### MemHeap.cpp
- `void MemHeap::LRUFit(int, int, MemHeap::FreeBlockInfo&)` — public
- `int MemHeap::GetAlignWords(int)` — static public
- `int* MemHeap::TryAlloc(int, int, int&)` — public
- `bool FreeBlock::AttemptMerge(FreeBlock*, int)` — public
- `int* MemHeap::Alloc(int, int, int&)` — public
- `int* MemHeap::Truncate(int*, int, int&)` — public
- `int MemHeap::Free(int*)` — public

#### AllocInfo.cpp
- `void AllocInfo::PrintForReport(TextStream&) const` — public
- `void AllocInfo::PrintForReport(_iobuf*) const` — public

---

### src/lazer/game/

#### Game.cpp
- `DataNode OnCycleAutoplay(DataArray*)` — FREE
- `DataNode OnCycleTestDancer(DataArray*)` — FREE
- `bool Game::HandleWait()` — private (may already be implemented)
- `DataNode OnDumpMoves(DataArray*)` — FREE

#### PartyModeMgr.cpp
- `int PartyModeMgr::GetCrewColor(int, int)` — public
- `void PartyModeMgr::ReadPartySongQueue()` — private
- `void PartyModeMgr::SetSongsFromPlaylist()` — private
- `void PartyModeMgr::PruneHistory()` — private
- `void PartyModeMgr::FinalizeTeam(int)` — public
- `void PartyModeMgr::ResetSongs()` — private
- `DataNode PartyModeMgr::OnSetSongAndDefaults(DataArray*)` — private
- `void PartyModeMgr::UpdateScores()` — private
- `void PartyModeMgr::ToggleIncludedModeOn(Symbol, bool)` — public
- `void PartyModeMgr::ResetModes(bool)` — private
- `void PartyModeMgr::FinalizeParty()` — public

#### GamePanel.cpp
- `void GamePanel::UpdateNowBar()` — private
- `float GamePanel::DeJitter(float)` — public

---

### src/lazer/meta_ham/

#### SaveLoadManager.cpp
- `DataNode SaveLoadManager::OnMsg(const MCResultMsg&)` — protected
- `DataNode SaveLoadManager::OnMsg(const SigninChangedMsg&)` — protected
- `void SaveLoadManager::HandleEventResponse(HamProfile*, int)` — public

#### Challenges.cpp
- `bool Challenges::HasNewChallenges()` — public

#### MetagameRank.cpp
- `int MetagameRank::SaveSize(int)` — static public
- `int MetagameRank::GetRankInTier() const` — public
- `int MetagameRank::GetTier() const` — public
- `void MetagameRank::AwardForRankUp(int)` — private
- `int MetagameRank::ComputeRankNumber(bool)` — private
- `bool compare_deferred_points(DeferredPoints, DeferredPoints)` — FREE

#### MetaPerformer.cpp
- `bool MetaPerformer::CheckRecommendedPracticeMove(String, int) const` — protected

#### ProfileMgr.cpp
- `float ProfileMgr::GetPadExtraLag(int, LagContext) const` — public
- `Symbol ProfileMgr::GetAlternateOutfit(Symbol)` — public
- `void ProfileMgr::LoadGlobalOptions(FixedSizeSaveableStream&)` — public
- `DataNode ProfileMgr::OnMsg(const SigninChangedMsg&)` — private

#### SkeletonIdentifier.cpp
- `void SkeletonIdentifier::DrawDebug()` — public
- `DataNode SkeletonIdentifier::OnMsg(const SigninChangedMsg&)` — private
- `DataNode SkeletonIdentifier::OnMsg(const SkeletonIdentifiedMsg&)` — private

#### SkeletonChooser.cpp
- `int SkeletonChooser::RoundRobinForHandRaised(int)` — private
- `int SkeletonChooser::RoundRobinForStandingStill(int)` — private
- `void SkeletonChooser::DrawDebug()` — public
- `void SkeletonChooser::SetPlayerSkeletonNavData(int, int)` — private
- `void SkeletonChooser::ChoosePlayerSides()` — private
- `void SkeletonChooser::CheckToSwitchActivePlayer()` — private

#### ChallengeSortMgr.cpp
- `const char* ChallengeSortMgr::GetBestChallengeScoreGamertag(int)` — public
- `int ChallengeSortMgr::GetChallengerXp(int)` — public
- `const char* ChallengeSortMgr::GetChallengerGamertag(int)` — public
- `void ChallengeSortMgr::OnEnter()` — public virtual

#### AppMiniLeaderboardDisplay.cpp
- `void AppMiniLeaderboardDisplay::Text(int, int, UIListLabel*, UILabel*) const` — public virtual
- `void AppMiniLeaderboardDisplay::UpdateSelfInRows()` — private

#### AppLabel.cpp
- `void AppLabel::SetStoreFilterName(const HamStoreFilter*)` — public
- `void AppLabel::SetTimeElapsedSince(unsigned int)` — private

---

### src/ (root-level)

#### App.cpp
- `void AutoGlitchReport::EndExternal(float, float, const char*, void(*)(float, void*), void*)` — static public

#### Memory_Xbox.cpp
- `int ForceLinkXMemFuncs()` — FREE
- `const char* (anon)::AllocType(unsigned long)` — anon ns FREE
- `XMemFree` — FREE (C-linkage)
- `XMemSize` — FREE (C-linkage)
- `PhysMemTypeTracker::~PhysMemTypeTracker()` — public dtor
- `int (anon)::AllocAlign(unsigned long)` — anon ns FREE
- `void (anon)::MemAllocFailed(unsigned long, bool)` — anon ns FREE
- `XMemAlloc` — FREE (C-linkage)
- `void* PhysicalAllocTracked(unsigned long, unsigned long, const char*, int, const char*)` — FREE
- `void* PhysicalAlloc(int)` — FREE
- `PhysMemTypeTracker::PhysMemTypeTracker(Symbol)` — public ctor

---

## Linker Stub Inventory (`engine_stubs_generated.cpp`)

**199 weak linker stubs** remain after Phase 3 burndown (223 → 199). These are `__attribute__((weak))` symbols that provide zero-return fallbacks for functions not yet compiled into the native binary. When the real implementation is added to the build, the strong symbol automatically overrides the weak stub.

### Priority A — Blocks Native Gameplay Features (~10 stubs)

Functions that affect runtime behavior on native. Most Phase 1 inline/template stubs were removed. Remaining:

| Category | Count | Examples | Impact |
|----------|-------|---------|--------|
| **Game logic** | 3 | `UILabel::Terminate`, `UILabel::LabelStyle::~LabelStyle`, `FlowPtr<Hmx::Object>::FlowPtr` | UI/flow correctness |
| **Sorting/nav** | 3 | `NavListSort::ChangeHighlightHeader`, `NavListHeaderNode::SelectChildren`, `PlaylistSortByType::NewHeaderNode` | Song list sorting |
| **Spotlight** | 1 | `Spotlight::RemoveFromLists` | Light cleanup |
| **Waypoint** | 1 | `Waypoint::Highlight` | Debug viz |
| **FitnessCalorieSortMgr** | 1 | `Handle` + thunk | Sort handler |
| ~~**Removed**~~ | ~~-27~~ | ~~`UIPanel::SetPaused`, `UIList::NumData`, `ObjDirPtr::IsLoaded` (2), `NavListItemSortCmp::Get*Cmp` (13), `PropSync<ObjPtrVec>` (8), `PseudoRandomPicker` (3)~~ | Dead — real impls from headers/templates |

### Priority B — Would Unblock Secondary Modes (28 stubs)

Party mode, challenges, scoring, achievements — not needed for core perform-mode gameplay.

| Category | Count | Examples |
|----------|-------|---------|
| **MultiUserGesturePanel** | 5 | `UpdateCharPic`, `UpdateVenueMesh`, `GetVoiceCommandOutfitTag`, `UpdateProviderPlayerIndices`, `HasNavList` |
| **PartyModeMgr** | 1 | `DetermineSubModePlayers` |
| **AccomplishmentProgress** | 3 | `GetNumCompleted`, `GetTotalSongsPlayed`, `GetTotalCampaignSongsPlayed` |
| **Achievements** | 3 | `PlatformInit`, `GetAchievementData`, `SubmitAchievementsFunc` |
| **GameEndedDataPointJob** | 1 | `CompileMoveRatings` |
| **HamStorePanel** | 1 | `GetOfferIDsToEnumerate` |
| **SingleUserCrewSelectPanel** | 1 | `UpdateCrewMesh` |
| **Sort managers** | 4 | `MQSongSort::BuildTree`, `ChallengeSort::BuildTree`, `SongSortMgr::SetupQuasiRandomSongs`, `LocationCmp::LocationCmp` |
| **MoveDir** | 1 | `EnqueueDetectFrames` |
| **Game misc** | 8 | `BaseSkeleton::LimbNormPos/MakeCameraToPlayerXfm`, `CamTexClip::StoreTextureClip`, `altCfg`, `PseudoRandomPicker` (3), `HongKongExceptionMet` |

### Priority C — Xbox Platform / Never Needed on Native (~195 stubs)

These are Xbox 360 hardware APIs, Kinect, D3D9, Bink video, Xbox Live networking, and debug profiling. They will never have native implementations — the native port uses different subsystems (WebGPU, FFmpeg, miniaudio, etc.).

| Category | Count | Notes |
|----------|-------|-------|
| **Static member variables** | 2 | `StandardStream::kStreamEndMs`, `CharSignalApplier` VTT — only 2 remain after Phase 2 burndown |
| **Vtable/typeinfo** | 3 | Compiler-generated vtables/typeinfo for CharSignalApplier, DxTex |
| **Xbox/Kinect** | 42 | LiveCameraInput (10), ArcDetector (6), DirectionGestureFilter (4), SetupHX* (4), VoiceInputPanel (3), VirtualKeyboard (3), etc. |
| **Non-virtual thunks** | 1 | Compiler-generated MI thunk (FitnessCalorieSortMgr::Handle only — rest auto-generated by Clang) |
| **Bink video** | 25 | BinkMovieImpl (14 methods), Bink C API (7), BinkMovieSys |
| **D3D9 rendering** | 26 | RndRenderState (15 methods), DxTex, DxMesh, DepthBuffer3D, StreamRenderer, NgDOFProc, DrawBufferMat |
| **Xbox OS** | 22 | CloseHandle, WaitForSingleObject, WSACreateEvent, AsyncFileWin, CacheMgrXbox, HolmesClient, MemcardXbox |
| **Xbox networking** | 13 | NetworkSocket, NetLoaderXbox, NetCacheMgrXbox, WebSvcMgr, DingoJob, XNetDns |
| **Debug/profiling** | 13 | MemTracker (3), AllocInfo, AutoSlowFrame, PhysMemTypeTracker, SpewInit/Terminate, DiffTblReport |
| **Audio (Xbox)** | 7 | StandardStream::GetJumpBackTotalTime, DspAllocate, CompressThread, CacheWav, RadAlloc |
| **JPEG** | 6 | jpeg_CreateCompress, jpeg_set_defaults, jpeg_start_compress, jpeg_finish_compress, jpeg_write_scanlines, jpeg_std_error |

### Global Variable Stubs (21)

Separate from function stubs — these provide zero-initialized storage for `The*` global pointers and other statics that haven't been properly initialized on native yet.

```
TheMaster, TheMC, TheRenderState, TheServer, TheSkeletonIdentifier, TheSkeletonViz,
TheSongSortMgr, TheMQSongSortMgr, TheChallengeSortMgr, TheFitnessGoalMgr,
TheHAQMgr, TheLeaderboards, TheDebugNotifyOncePrinter,
MemHeapStack::sDefaultHeap, gCharHighlightY, gMemStackLock,
lbl_82F14008, lbl_830A4104, lbl_8316EB70, lbl_8316EBA8, lbl_83172BB0
```

### How to Check for New Dead Stubs

After adding new source files to the native build, run:
```bash
# Find stubs that now have strong symbol overrides
nm native/build/dc3-native | grep ' T ' | awk '{print $3}' | sort > /tmp/strong.txt
grep 'ASM_SYM' native/src/engine_stubs_generated.cpp | sed 's/.*ASM_SYM("//;s/").*//' | sort > /tmp/stubs.txt
comm -12 /tmp/strong.txt /tmp/stubs.txt  # These are dead and can be removed
```
