# Logic Bug Roadmap - Functions with Real Behavioral Divergences

**Date**: 2026-03-26 (updated)
**Purpose**: Catalog functions with confirmed logic bugs (unicorn DIVERGENT) that impact game/engine behavior. These were previously AT_LIMIT but got demoted because unicorn behavioral testing found fixable differences.

## Summary Statistics

| Category | Count | Description |
|----------|-------|-------------|
| **P0 - Confirmed Divergent, native-port relevant** | 95 | Functions with unicorn-confirmed behavioral bugs in native-port subsystems |
| **P1 - Confirmed Divergent, Xbox/platform-specific** | 31 | Functions with unicorn-confirmed behavioral bugs in Xbox/platform/store subsystems |
| **P2 - Confirmed Divergent, Bink/OggVorbis/JPEG** | 11 | Functions in media codec subsystems (less likely to affect native port) |
| **Completed this session** | 13 | Functions removed from roadmap (fixed or no longer divergent) |
| **Total active** | 137 | |

### Session Changes
- **13 functions removed**: Fixed to 100% COMPLETE or no longer show divergent behavior
- **90 functions added**: New DIVERGENT entries from fresh unicorn scan not in original roadmap
- `compute_z_mzt` regressed from 72.1% to 49.1% (needs investigation)
- `yy_create_buffer` improved from 76.6% to 97.2% (COMPLETE)

### Divergence Types
- `call_count` - Wrong number of function calls (missing/extra calls) — 114 functions
- `call_arg` - Wrong argument passed to a function call — 9 functions
- `error` - Function produces errors/asserts differently — 6 functions
- `return_value` - Function returns wrong value — 5 functions
- `object_memory` - Object memory layout differs — 3 functions

---

## P0: Confirmed DIVERGENT Functions — Native-Port Relevant

These are in subsystems actively used by the native port (rndobj, char, obj, ui, world, synth, game, hamobj, gesture, math, flow, utl). Each has a real logic bug confirmed by unicorn behavioral testing.

### system/rndobj (Rendering)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `RndMatAnim::TexPtr::TexPtr(RndTex*)` | MatAnim | 93.5% | object_memory | Material animation texture pointers |
| `Rnd::DrawTimers(float)` | Rnd | 90.9% | call_count | Debug timer drawing |
| `RndBitmap::NearestColor(uchar,uchar,uchar,uchar)` | Bitmap | 94.3% | call_count | Palette color lookup |
| `RndAmbientOcclusion::BuildTrees(Quality)` | AmbientOcclusion | 99.9% | call_count | AO tree building |
| `RndFontBase::Load(BinStream&)` | FontBase | 99.8% | call_count | Font loading |
| `RndTexRenderer::SyncProperty(...)` | TexRenderer | 99.0% | call_count | Texture renderer properties |
| `Watcher::Update()` | Watcher | 98.9% | call_count | Debug watcher |
| `FloatKeys::FloatAt(float,float&)` | PropKeys | 97.2% | call_count | Animation key interpolation |
| `Key<Weight> >> operator` | Morph | 94.7% | call_arg | Morph weight deserialization |
| `ResetNormals(RndMesh*)` | Utl | 67.6% | call_count | Mesh normal recalculation |
| `BuildFromBSP(RndMesh*)` | Utl | 70.0% | call_count | BSP mesh building |

### system/char (Character System)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `CharEyes::NextLook()` | CharEyes | 89.3% | call_count | Eye gaze behavior |
| `CharForeTwist::Poll()` | CharForeTwist | 89.0% | call_count | Forearm twist animation |
| `CharHair::Strand resize` | CharHair | 56.2% | call_count | Hair strand allocation |

### system/obj (Object System)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `ObjDirPtr<ObjectDir>::LoadInlinedFile(...)` | Dir | 92.1% | call_count | Asset loading — foundational |
| `ThreadTask::Replace(ObjRef*,Object*)` | Task | 84.7% | error | Task object replacement |
| `TaskMgr::OnTimeTilNext(DataArray*)` | Task | 99.9% | call_count | Task timing |
| `ReadEmbeddedFile(char*,bool)` | DataFile | 86.9% | call_count | Embedded file reading |

### system/hamobj (Dance/Gameplay Objects)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `HamNavList::DetermineHighlightedItem()` | HamNavList | 94.7% | call_count | Menu highlight logic |
| `HamCharacter::SetCampaignVo(char*)` | HamCharacter | 94.4% | call_count | Voice-over loading |
| `HamIKEffector::ComputeElbowPullAndQuat(...)` | HamIKEffector | 94.3% | call_count | IK elbow computation |
| `HamCamShot::UpdateTargetsFlipped()` | HamCamShot | 94.2% | call_count | Camera shot flipping |
| `HamCamShot::StartAnim()` | HamCamShot | 95.6% | call_count | Camera shot animation start |
| `HamCamShot::FlipTargetAnimGroups()` | HamCamShot | 91.5% | call_count | Camera target anim flipping |
| `Ham1EuclideanNode::CalcError(...)` | ErrorNode | 94.0% | call_count | Move error scoring |
| `Ham1DisplacementNode::Errors(...)` | ErrorNode | 98.5% | call_count | Displacement error calculation |
| `HamMove::PSNRToDetectFrac(float)` | HamMove | 96.9% | call_count | Move detection fraction |
| `MoveDir::SetCurrentMove(int,HamMove*)` | MoveDir | 96.8% | call_count | Current move assignment |
| `MoveAsyncDetector::EnqueueDetectFrames(...)` | MoveAsyncDetector | 90.1% | call_count | Async move detection |
| `PoseFatalities::InFatality(int)` | PoseFatalities | 89.4% | call_count | Fatality detection |
| `HamNavList::IsElementBig(int)` | HamNavList | 89.4% | call_count | Nav list element sizing |
| `HamNavList::DrawDebug()` | HamNavList | 88.2% | call_count | Debug drawing |
| `HamSkeletonConverter::SetLeg(...)` | HamSkeletonConverter | 85.5% | call_count | Skeleton leg mapping |
| `DanceRemixer::MoveVariantFromHamMove(HamMove*)` | DanceRemixer | 89.6% | call_arg | Move variant lookup (WRONG ARG!) |

### system/synth (Audio)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `op14(DataArray*)` (ByteGrinder) | ByteGrinder | 99.3% | call_count | Audio byte grinding |
| `op15(DataArray*)` (ByteGrinder) | ByteGrinder | 99.3% | call_count | Audio byte grinding |
| `FlangerEffect::SetParameters(Params&)` | FlangerEffect | 97.8% | object_memory | Flanger effect parameters |
| `op8(DataArray*)` (ByteGrinder) | ByteGrinder | 93.1% | call_count | Audio byte grinding |
| `compute_z_mzt()` | filterdesign | 49.1% | call_count | Filter coefficient computation (REGRESSED) |

### system/gesture (Kinect/Gesture)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `OnSetFakeSkeletonSidesSwapped(DataArray*)` | SkeletonUpdate | 99.0% | call_count | Skeleton side swapping |
| `LiveCameraInput::GetStreamTex(BufferType)` | LiveCameraInput | 97.9% | call_count | Camera stream texture |
| `SpeechMgr::PrintSemanticTree(...)` | SpeechMgr | 96.2% | error | Speech semantic parsing |
| `StubCameraInput::StubSkeletonData(...)` | StubCameraInput | 94.7% | call_count | Stub skeleton data |
| `DepthBuffer3D::AddAttachment(...)` | DepthBuffer3D | 77.9% | call_count | Depth buffer attachments |

### system/ui (UI System)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `UIList::CalcBoundingBox(Box&)` | UIList | 99.9% | call_count | List bounding box calculation |
| `UIList::GetDistanceToPlane(Plane&,Vector3&)` | UIList | 99.9% | call_count | List plane distance |
| `UIList::CollideShowing(Segment&,float&,Plane&)` | UIList | 99.7% | call_count | List collision detection |

### system/world (World)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `Spotlight::SyncProperty(...)` | Spotlight | 99.8% | call_count | Spotlight property sync |
| `GetLightPosition(Spotlight*,Vector3&)` | SpotlightDrawer_NG | 99.7% | call_count | Light position calculation |
| `LightPreset::GetKey(float,int&,int&,float&)` | LightPreset | 98.1% | call_count | Light preset key lookup |

### system/flow (Flow System)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `FlowDistance::Execute(QueueState)` | FlowDistance | 99.7% | call_count | Flow distance execution |
| `Flow::SyncObjects()` | Flow | 92.4% | error | Flow object synchronization |
| `FlowRun::ResolveTarget()` | FlowRun | 92.6% | call_count | Flow target resolution |
| `FlowPickOne::Activate()` | FlowPickOne | 70.9% | call_count | Flow pick-one activation |
| `FlowSlider::UpdateActivations()` | FlowSlider | 69.9% | call_count | Flow slider activations |
| `FlowSwitch::VerifyTypes()` | FlowSwitch | 57.1% | call_count | Flow switch type verification |

### system/math (Math)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `Intersect(Transform&,Plane&,Ray&)` | Geo | 90.3% | call_count | Ray-plane intersection |
| `operator>(Sphere&,Frustum&)` | Geo | 87.9% | return_value | Sphere-frustum test (WRONG RESULT) |
| `Intersect(Vector3&,Vector3&,Triangle&,float&)` | Geo | 77.9% | call_count | Ray-triangle intersection |
| `Intersect(Segment&,Sphere&)` | Geo | 76.8% | return_value | Segment-sphere test (WRONG RESULT) |
| `Intersect(Segment&,Triangle&,bool,float&)` | Geo | 71.9% | call_count | Segment-triangle intersection |

### system/os (Operating System)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `PlatformMgr::SetDiskError(DiskError)` | PlatformMgr | 99.0% | call_count | Disk error handling |
| `FileInit` | File | 96.1% | call_count | File system initialization |
| `Debug::SetModalCallback(...)` | Debug | 93.6% | call_count | Debug modal callback |
| `FileRelativePathBuf` | File | 92.9% | call_count | File path resolution |
| `IsUselessLoad(char*)` | App | 92.0% | call_count | Load filtering |

### system/utl (Utilities)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `FrontLoaderGlitchCB(float,void*)` | Loader | 98.6% | call_count | Loader glitch callback |
| `Trie::get_free_node()` | trie | 98.1% | call_arg | Trie node allocation (WRONG ARG) |
| `GetPool()` | AllocInfo | 95.4% | call_arg | Pool allocation (WRONG ARG) |
| `AddHeap(...)` | MemMgr | 95.2% | call_arg | Heap addition (WRONG ARG) |
| `CacheMgrXbox::PollSearch()` | CacheMgr_Xbox | 94.9% | call_count | Cache search polling |
| `CharToWideChar(char*)` | UTF8 | 94.1% | call_count | UTF8 string conversion |
| `CheatsInit()` | Cheats | 94.0% | call_count | Cheat system init |
| `MemOrPoolAllocSTL(...)` | MemMgr | 94.0% | call_count | STL memory allocation |
| `Trie::store(char*)` | trie | 93.4% | call_count | Trie storage |
| `HxGuid::Generate()` | HxGuid | 89.8% | call_count | GUID generation |
| `MultiTempoTempoMap::~MultiTempoTempoMap()` | MultiTempoTempoMap | 88.5% | object_memory | Tempo map destructor |
| `MultiTempoTempoMap::TickToTime(float)` | MultiTempoTempoMap | 87.6% | call_count | Tick-to-time conversion |
| `NetCacheMgr::SetState(NetCacheMgrState)` | NetCacheMgr | 88.4% | call_count | Cache state management |
| `Trie::inc_count(uint)` | trie | 87.2% | call_count | Trie count increment |
| `Trie::dec_count(uint)` | trie | 87.2% | call_count | Trie count decrement |
| `MemInit()` | MemMgr | 86.8% | call_count | Memory system init |
| `MemPushHeap(int)` | MemMgr | 86.7% | call_count | Heap stack push |
| `ChunkStream::DecompressChunkAsync()` | ChunkStream | 83.5% | call_count | Chunk decompression |

### system/midi (MIDI)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `DefaultMidiLess(Midi&,Midi&)` | MidiReader | 76.3% | return_value | MIDI sorting comparator (WRONG RESULT) |

### lazer/game (Game)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `Game::PauseForSkeletonLoss()` | Game | 97.0% | call_count | Skeleton loss pause |

### lazer/meta_ham (Metagame)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `UIEventMgr::TriggerEvent(Symbol,DataArray*)` | UIEventMgr | 98.7% | call_count | Event triggering |
| `ProfileMgr::HasActiveProfileWithInvalidSaveData()` | ProfileMgr | 98.7% | call_count | Profile validation |
| `HamStoreProvider::RefreshFilteredCartOffers()` | HamStoreProvider | 95.4% | call_count | Store cart filtering |
| `SongStatusMgr::GetBestStars(int,bool&,Difficulty)` | SongStatusMgr | 93.9% | call_count | Song star ratings |
| `ChallengeResultPanel::Text(...)` | ChallengeResultPanel | 92.8% | error | Challenge results text |
| `MetaPerformer::CalcCharacters(...)` | MetaPerformer | 92.5% | call_count | Character selection |
| `MetaPerformer::SaveAndUploadScores(...)` | MetaPerformer | 88.8% | call_count | Score saving/upload |
| `OptionsPanel::OnMsg(RCJobCompleteMsg&)` | OptionsPanel | 86.1% | error | Options panel job handling |
| `RockCentral::OnMsg(ServerStatusChangedMsg&)` | RockCentral | 98.9% | call_arg | Server status handling (WRONG ARG) |

---

## P1: Confirmed DIVERGENT Functions — Xbox/Platform-Specific

These are in Xbox-specific or platform subsystems not directly used by the native port, but still contain real logic bugs.

### system/rnddx9 (DirectX 9 Rendering — Xbox)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `DxMesh::DxMesh()` | Mesh | 94.2% | call_arg | DX mesh constructor (WRONG ARG) |
| `CreateBackBuffers(...)` | Rnd_Xbox | 91.1% | call_count | Back buffer creation |
| `RndRenderState::Init()` | RenderState | 84.2% | call_arg | Render state init (WRONG ARG) |
| `DxMultiMesh::UpdateGeometryBuffers()` | MultiMesh | 78.6% | call_count | Geometry buffer update |
| `DxRnd::DxRnd()` | Rnd_Xbox | 69.7% | call_count | DX renderer constructor |
| `DxRnd::SavePreBuffer()` | Rnd_Xbox | 66.5% | call_count | Pre-buffer saving |

### system/os (Xbox/Platform OS)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `GetSystemLanguage(Symbol)` | System_Xbox | 97.1% | call_count | System language detection |
| `MCContainerXbox::Mount(CreateType)` | Memcard_Xbox | 87.9% | call_count | Xbox memcard mount |
| `HolmesClientClose(File*,int)` | HolmesClient | 87.9% | call_count | Debug client close |
| `HolmesClientRead(int,int,int,void*,File*)` | HolmesClient | 84.0% | call_count | Debug client read |
| `UsbMidiKeyboard::Poll()` | UsbMidiKeyboard | 83.9% | call_count | USB MIDI polling |
| `GetXinputSinceLastFrame(...)` | Joypad_Xbox | 78.8% | call_count | XInput state reading |
| `AsyncFileWin::_OpenAsync()` | AsyncFile_Win | 78.9% | call_count | Async file opening |
| `MemcardXbox::FindValidUnit(ContainerId*)` | Memcard_Xbox | 81.1% | call_count | Memcard unit search |

### system/utl (Xbox-specific utilities)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `CacheXbox::ThreadWrite()` | Cache_Xbox | 89.5% | call_count | Cache write thread |
| `CacheXbox::ThreadGetFileSize()` | Cache_Xbox | 84.2% | call_count | Cache file size query |

### system/synth_xbox (Xbox Audio)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `DSP::SynapseAPO::SynapseAPO()` | SynapseAPO | 96.0% | call_arg | Synapse APO constructor (WRONG ARG) |
| `MicXbox::SetFxSend(FxSend*)` | Mic | 92.0% | call_count | Xbox mic FX send |
| `MicManagerXbox::RequirePushToTalk(bool,int)` | Mic | 84.5% | call_count | Xbox mic push-to-talk |
| `fft_matrix_forward_columnwise(...)` | FFT | 54.8% | call_count | FFT matrix forward |
| `SampleInst360::SampleInst360(...)` | SampleInst360 | 19.7% | call_count | Xbox sample instance |

### system/net (Networking)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `Curl_resolv_unlock` | hostip | 99.9% | call_count | Curl resolve unlock |
| `WebSvcMgr::ResolveHostname(...)` | WebSvcMgr | 94.5% | call_count | Hostname resolution |
| `DingoJob::SendCallback(bool,bool)` | DingoJob | 82.6% | call_count | Xbox Live job callbacks |
| `WebSvcMgrCurl::FindAndFinish(...)` | WebSvcMgrCurl | 74.1% | call_count | Curl request completion |
| `curl_global_init` | curl/easy | 72.0% | call_count | Curl initialization |

### system/meta (Store/Meta — Xbox)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `StorePanel::Exit()` | StorePanel | 84.9% | call_count | Store UI exit |
| `StorePanel::PopulateOffers(DataArray*,bool)` | StorePanel | 82.1% | call_count | Store offers population |
| `XboxMultipleItemsPurchaser::OnMsg(UIChangedMsg&)` | StorePurchaser | 71.8% | call_count | Xbox store purchase |

### keygen_xbox (Key Generation — Xbox)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `KeyChain::getKey(uint,uchar*,uchar*)` | keygen_xbox | 98.8% | return_value | Key generation (WRONG RETURN) |
| `KeyChain::getMasher(uchar*)` | keygen_xbox | 81.5% | return_value | Masher generation (WRONG RETURN) |

---

## P2: Confirmed DIVERGENT Functions — Media Codecs

These are in Bink, OggVorbis, or JPEG subsystems. Less likely to directly affect native port behavior but still contain real bugs.

### system/oggvorbis (Vorbis Audio Codec)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `mapping0_inverse` | mapping0 | 92.4% | call_count | Vorbis inverse mapping |
| `_vp_noisemask` | psy | 91.1% | call_count | Vorbis noise masking |
| `_vp_quantize_couple_sort` | psy | 90.5% | call_count | Vorbis quantize sorting |
| `bark_noise_hybridmp` | psy | 90.3% | call_count | Vorbis bark noise |
| `mdct_forward` | mdct | 90.2% | call_count | Vorbis MDCT forward |
| `mapping0_forward` | mapping0 | 85.9% | call_count | Vorbis forward mapping |
| `_vp_noise_normalize_sort` | psy | 84.4% | call_count | Vorbis noise normalize |

### system/moviebink (Bink Video)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `MovieInternalBuffers::~MovieInternalBuffers()` | BinkMovieImpl | 91.4% | call_count | Bink buffer destructor |
| `BinkMovieSys::Init()` | BinkMovieSys | 90.1% | error | Bink system init |
| `BinkMovieImpl::BinkMovieImpl()` | BinkMovieImpl | 59.3% | call_count | Bink impl constructor |

### system/jpeg (JPEG)

| Function | Unit | Match% | Divergence | Notes |
|----------|------|--------|------------|-------|
| `LoadBitmapIntoJpeg(...)` | Jpeg | 57.7% | call_count | JPEG bitmap loading |

---

## Recommended Fix Order

### Critical — Return-value and call-arg bugs (wrong results)

These functions produce **wrong outputs**, not just different call patterns. They are the highest-priority fixes.

1. **`operator>(Sphere&,Frustum&)`** (87.9%, Geo) — `return_value` divergence. Frustum culling returns wrong result, causing objects to incorrectly appear/disappear.
2. **`Intersect(Segment&,Sphere&)`** (76.8%, Geo) — `return_value` divergence. Collision detection returns wrong boolean.
3. **`DefaultMidiLess(Midi&,Midi&)`** (76.3%, MidiReader) — `return_value` divergence. MIDI sort comparator returns wrong order, corrupting MIDI event sequencing.
4. **`RndMatAnim::TexPtr::TexPtr(RndTex*)`** (93.5%, MatAnim) — `object_memory` divergence. Material animation texture pointer constructed with wrong memory.
5. **`Trie::get_free_node()`** (98.1%, trie) — `call_arg` divergence. Wrong argument to node allocation.
6. **`GetPool()`** (95.4%, AllocInfo) — `call_arg` divergence. Wrong argument to pool allocator.
7. **`AddHeap(...)`** (95.2%, MemMgr) — `call_arg` divergence. Wrong argument to heap setup.
8. **`Key<Weight> >> operator`** (94.7%, Morph) — `call_arg` divergence. Wrong data deserialized for morph weights.
9. **`RockCentral::OnMsg(ServerStatusChangedMsg&)`** (98.9%) — `call_arg` divergence. Wrong argument to server status handler.
10. **`DanceRemixer::MoveVariantFromHamMove`** (89.6%) — `call_arg` divergence. Wrong argument to move variant lookup, directly affects gameplay move selection.

### High Impact — Native-port gameplay and rendering

11. **`ObjDirPtr::LoadInlinedFile`** (92.1%) — Asset loading is foundational; wrong call count means assets may load incorrectly
12. **`ResetNormals(RndMesh*)`** (67.6%) — Mesh normal bugs cause visible rendering artifacts
13. **`CharEyes::NextLook`** (89.3%) — Character eye behavior is very visible
14. **`CharForeTwist::Poll`** (89.0%) — Forearm twist visible in all character animation
15. **`CharHair::Strand resize`** (56.2%) — Hair strand allocation
16. **`Ham1EuclideanNode::CalcError`** (94.0%) — Directly affects move scoring
17. **`MoveAsyncDetector::EnqueueDetectFrames`** (90.1%) — Async move detection timing
18. **`PoseFatalities::InFatality`** (89.4%) — Fatality detection affects scoring
19. **`HamIKEffector::ComputeElbowPullAndQuat`** (94.3%) — IK affects character posing
20. **`FloatKeys::FloatAt(float,float&)`** (97.2%) — Animation key interpolation affects all animated properties
21. **`compute_z_mzt`** (49.1%) — Filter design affects audio processing (REGRESSED — was 72.1%)

### Medium Impact — Native-port infrastructure

22. **`MemOrPoolAllocSTL`** (94.0%) — Memory allocation correctness
23. **`CharToWideChar`** (94.1%) — Text encoding bugs
24. **`FileRelativePathBuf`** (92.9%) — File path resolution
25. **`MemInit()`** (86.8%) — Memory system initialization
26. **`MemPushHeap(int)`** (86.7%) — Heap management
27. **`ThreadTask::Replace`** (84.7%) — Task object replacement
28. **`ReadEmbeddedFile`** (86.9%) — Embedded file reading
29. **`Intersect(Transform&,Plane&,Ray&)`** (90.3%) — Math intersection
30. **`UIList::CalcBoundingBox`** (99.9%) — UI layout
31. **`SongStatusMgr::GetBestStars`** (93.9%) — Star rating calculation
32. **`Flow*` functions** (57-99%) — Multiple flow system functions with divergences
33. **`HamNavList::*`** (88-95%) — Multiple nav list functions with divergences

---

## Functions Removed This Session

These were in the original roadmap but are no longer divergent or have been fixed to 100%:

| Function | Old Match% | New Status | Reason |
|----------|-----------|------------|--------|
| `CharEyes::DartUpdate()` | 100.0% | COMPLETE | No longer divergent |
| `yy_create_buffer` | 76.6% | 97.2% COMPLETE | Fixed this session |
| `HamNavList::UpdateGestures(Skeleton*)` | 94.9% | COMPLETE | Fixed |
| `HamNavList::Poll()` | 93.5% | COMPLETE | Fixed |
| `HamStoreProvider::Refresh()` | 94.7% | 100% COMPLETE | Fixed |
| `MetaPerformer::SetupCharacters()` | 100.0% | 100% COMPLETE | Fixed |
| `MetaPerformer::SetDefaultSongCharacter(int)` | 100.0% | 100% COMPLETE | Fixed |
| `MetaPerformer::OnRecallMovePassed(int,HamMove*)` | 100.0% | 100% COMPLETE | Fixed |
| `HandleDeferredAward(DataArray*)` | 100.0% | 100% COMPLETE | Fixed |
| `DanceRemixer::AddRoutineMove(...)` | 100.0% | 100% COMPLETE (divergent) | Header/inline issue, non-actionable |
| `RndScaleObject(Object*,float,float)` | 94.0% | COMPLETE | Fixed |
| `BaseDisplacementNode::Displacements(...)` | 93.0% | Not divergent | Replaced by Ham1DisplacementNode::Errors |
| `HolmesClientEnumerate(...)` | — | 100% COMPLETE (stale) | Stale entry |

---

## Notes

- All P0 functions were identified by the unicorn behavioral testing framework comparing decompiled code execution against the original binary
- `call_count` divergences mean the function makes a different number of sub-calls, which typically indicates missing/extra branches, loop iteration differences, or early returns
- `call_arg` divergences are the most dangerous — wrong data is being passed to sub-functions
- `return_value` divergences are critical — the function returns an incorrect result to its caller
- `object_memory` divergences mean the object is constructed with different memory contents
- `error` divergences mean different error/assert paths are taken
- Functions showing 100% match but with unicorn divergence likely have bugs in inlined code from headers
- The `compute_z_mzt` regression from 72.1% to 49.1% needs investigation — this may indicate a bad change was introduced
