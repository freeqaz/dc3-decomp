# Stub Function Burndown

Consolidated report of stub functions across the decomp, categorized by type and grouped by source file. Generated 2026-03-12, updated 2026-03-13 from source verification.

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

### TIER 2 — High: Enables Gameplay Scoring & HUD (Phase 4.1–4.3)

These stubs block the gameplay loop: detecting moves, scoring, updating HUD, managing song routines.

| File | Stubs | Why Important |
|------|-------|--------------|
| **RhythmDetector.cpp** | `ProcessFrames`, `GetRecord`, `SetupFrame`, `BlendFrameDataToBeat` (4) | Core move detection — processes skeleton frames against expected dance moves. |
| **PoseFatalities.cpp** | `UpdateClipDriver`, `UpdateMatchingPose`, `DrawDebug` (3) | Pose matching/scoring for dance moves. |
| **RhythmBattlePlayer.cpp** | `AnimateBoxyState`, `UpdateComboProgress` (2) | Battle mode scoring and combo display. |
| **GamePanel.cpp** | `UpdateNowBar`, `DeJitter` (2) | HUD timing bar (shows upcoming moves). DeJitter smooths Kinect/input jitter. |
| **MoveMgr.cpp** | `ComputeRandomChoiceSet`, `ComputeLoadedMoveSet`, `FillInRoutineAt`, `FillRoutineFromVerses`, `FillRoutineFromReplacer` (5) | Builds the move routine for a song — which dance moves appear at which beats. |
| **SongLayout.cpp** | `SetDefaultReplacer`, `SetDefaultPattern` (2) | Song structure configuration (patterns, replacer choreography). |
| **HamCamShot.cpp** | `CreateFlippedShowHideList` (1) | Camera direction — show/hide objects based on camera angle (player-facing vs away). |
| **HamIKEffector.cpp** | `DoFancyElbow` (1) | IK for character elbows. Improves dance pose quality. |
| **HollaBackMinigame.cpp** | `OnBeat` (1) | Minigame beat handler. |
| **Game.cpp** | `OnCycleAutoplay`, `OnCycleTestDancer`, `HandleWait`, `OnDumpMoves` (4) | Debug/test functions for gameplay iteration. |

**Total: ~25 stubs. Unblocks: move detection, scoring, song routine generation, gameplay HUD.**

### TIER 3 — Medium: Venue Lighting & Rendering Quality (Phase 4.4 + Milestone 5)

These improve visual quality — venue lights, post-processing, line effects, spotlight volumes. The venue renders without them but looks flat/static.

| File | Stubs | Impact |
|------|-------|--------|
| **SpotlightDrawer.cpp** | `Load`, `DrawWorld`, `DrawShadow`, `ClearLights`, `DeSelect`, `ApplyLightingApprox`, `UpdateBoxMap`, `SpotDrawParams::Load` (8) | Spotlight volumes, shadows, box map lighting. |
| **SpotlightDrawer_NG.cpp** | 15 methods (entire NG spotlight renderer) | Next-gen spotlight cones, beams, fog density, blur, scene rendering. |
| **Shader.cpp** | 22+ (`CalcShaderOpts` × 12 subclasses, `Select` × 3, free functions) | NG shader option calculation and selection. Partially works without these. |
| **RndPostProc.cpp** | `Interp` (1) | Post-processing interpolation between presets (bloom, color correction transitions). |
| **RndLine.cpp** | `UpdateLine` (×2), `UpdateLinePair` (3) | Line rendering for light beams, debug lines, ribbon trails. |
| **RndPropAnim.cpp** | `ForeachKeyframe` (1) | Script iteration over keyframes. Used by DTA for procedural animation. |
| **Rnd.cpp** | `Modal` (1) | Debug modal rendering (low priority). |
| **ClipDistMap.cpp** | `Draw` (1) | Character clip distance visualization (debug/tuning). |
| **WorldDir.cpp** | `BitmapOverride::Sync` (1) | Texture overrides per world (LOD, platform-specific). |

**Total: ~53 stubs. Unblocks: spotlight rendering, post-processing, line effects, NG shaders.**

### TIER 4 — Medium-Low: Audio Pipeline (Phase 6)

Audio stubs. The engine is silent; these enable sound effects and music playback.

| File | Stubs | Impact |
|------|-------|--------|
| **SampleData.cpp** | `Load`, `LoadWAV`, `SizeAs`, `SampleMarker::Load` (4) | Audio sample loading from .mogg/.wav files. |
| **StandardStream.cpp** | `ConsumeData`, `setJumpSamplesFromMs`, `IsPastStreamJumpPointOfNoReturn`, `DoJump` (4) | Streaming audio playback (song music). |
| **StreamReceiver.cpp** | `GetBytesPlayed`, `WriteData`, `Poll` (3) | Audio stream output buffer management. |
| **SampleInst.cpp** | `SynthPoll` (1) | Sample instance polling (SFX playback). |
| **Sound.cpp** | `SetPan` (1) | Stereo panning. |
| **Sequence.cpp** | `ComputeNextTime`, `PickNextIndex` (2) | Random audio sequence scheduling. |
| **MetaMusic.cpp** | `Load` (1) | Menu/meta music loading. |
| **WavReader.cpp** | `Poll` (1) | WAV file reader polling. |
| **Synth.cpp** | `DrawMeter` (1) | Audio level meter (debug). |
| **DelayEffect/Flanger/EQ** | `Process`, `SetParameter`, `Reset` (7) | DSP audio effects. |
| **Mic.cpp** | `RingBuffer::Write`, `Read` (2) | Microphone input buffer. |
| **MicNull.cpp** | `GetContinuousBuf` (1) | Null mic fallback. |
| **complex.cpp** | `eval` (1) | FFT/complex math for audio processing. |
| **ctr.cpp** | `ctr_encrypt_fast` (1) | Encryption (audio DRM?). |

**Total: ~30 stubs. Unblocks: song audio, SFX, music streaming.**

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

- PropSync variants: `PropSync<RndTransformable>`, `<RndDrawable>`, `<CharClip>`, `<Waypoint>`, `<Flow>`, `<RhythmDetector>`, `<Hmx::Object>` (ObjPtrVec), `<Hmx::Object>` (ObjOwnerPtr), `<MsgSinks::Sink>`, `<MsgSinks::EventSinkElem>`, `<MsgSinks::EventSink>`, PropSync(Matrix3), PropSync(Sphere), PropSync(Rect), PropSync(Box), PropSync(MsgSinks), PropSync(EventSink), PropSync(EventSinkElem), PropSync(Sink)
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
- Misc: `DrawAccessories<LensExtract>`, `StackString<256>::operator=`, `StackString<4096>::StackString(const char*)`, `StackString<512>::StackString()`, `StackString<3096>::StackString()`, `ScopedState<bool,1,0>::~ScopedState`

### STDLIB — Not decomp-actionable

- `std::exception::_Copy_str`, `std::exception::operator=`

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
- `void ScaleAddEq(Hmx::Quat&, const Hmx::Quat&, float)` — FREE
- `BinStream& operator<<(BinStream&, const CharBlendBone::ConstraintSystem&)` — FREE

#### CharHair.cpp
- `void CharCollide::SyncWorldState()` — public (CharCollide, instantiated in CharHair TU)

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
- `void Song::SyncState()` — public

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
