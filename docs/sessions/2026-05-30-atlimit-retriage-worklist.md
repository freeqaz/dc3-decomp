# AT_LIMIT Re-triage Worklist (evidence-based, objdiff --analyze --verdict)

Re-triaged 1619 functions labeled AT_LIMIT (current 50-99.99%).
objdiff verdict: 939 LIKELY_FIXABLE, 389 MAYBE_FIXABLE, 209 NEEDS_INVESTIGATION, 77 AT_LIMIT.
Classifier tiers: A hand-fixable=911, B permuter=391, C investigate=294, E hard=11, D confirmed-at-limit=12.

## Tier A — HAND-FIXABLE, top 60 by recoverable bytes

| size | fuzzy | fixable-share | patterns | unit : function |
|---:|---:|---:|---|---|
| 5856 | 55.0% | 1.19 | COMMUT,OFFSET | system/math/SHA1: private: void __cdecl CSHA1::Transform(unsigne |
| 5072 | 60.4% | 0.40 | COMMUT,CTRLFLOW,MAKESTR,OFFSET | system/hamobj/MoveDir: public: virtual float __cdecl MoveDir::UpdateO |
| 4492 | 64.4% | 0.38 | COMMUT,CTRLFLOW,MAKESTR,OFFSET | system/rndobj/AmbientOcclusion: public: void __cdecl RndAmbientOcclusion::Tess |
| 2732 | 64.9% | 0.61 | COMMUT,CTRLFLOW,OFFSET | system/rndobj/Part: protected: void __cdecl RndParticleSys::InitPa |
| 7324 | 79.6% | 0.37 | CTRLFLOW,FSEL_TERNARY | system/rndobj/Part: public: virtual bool __cdecl RndParticleSys::S |
| 2800 | 66.8% | 0.51 | COMMUT,OFFSET | system/math/mtx: void __cdecl Invert(class Hmx::Matrix4const &, |
| 1628 | 66.5% | 0.87 | CTRLFLOW,OFFSET | system/world/Spotlight: protected: void __cdecl Spotlight::BuildBeam(s |
| 2340 | 79.9% | 0.99 | COMMUT,CTRLFLOW,OFFSET | system/rndobj/Part: protected: void __cdecl RndParticleSys::MovePa |
| 1236 | 50.0% | 0.68 | CTRLFLOW,OFFSET | system/rndobj/Lit_NG: protected: bool __cdecl NgLight::SphereConeTes |
| 1708 | 67.1% | 0.71 | COMMUT,CTRLFLOW,OFFSET | system/world/Spotlight: protected: void __cdecl Spotlight::BuildNGCone |
| 1036 | 58.4% | 0.92 | COMMUT,OFFSET | system/rndobj/Lit_NG: class Hmx::Matrix4 __cdecl Hmx::operator*(clas |
| 1672 | 63.0% | 0.63 | CTRLFLOW,OFFSET | system/rndobj/AmbientOcclusion: protected: void __cdecl RndAmbientOcclusion::S |
| 1144 | 65.5% | 0.96 | COMMUT,OFFSET | system/rndobj/Line: protected: void __cdecl RndLine::UpdateLinePai |
| 2684 | 85.8% | 0.96 | OFFSET | system/oggvorbis/mapping0:  |
| 1352 | 72.9% | 0.94 | COMMUT,CTRLFLOW,OFFSET | system/rndobj/Line: protected: void __cdecl RndLine::UpdateLine(cl |
| 1564 | 78.1% | 0.94 | COMMUT,OFFSET | system/synth/EQEffect: public: void __cdecl EQEffect::Process(float * |
| 2700 | 81.7% | 0.64 | CTRLFLOW,OFFSET | system/rndobj/Text: public: void __cdecl RndText::WrapText(unsigne |
| 1344 | 65.1% | 0.61 | COMMUT,CTRLFLOW,OFFSET | system/rndobj/Utl: void __cdecl BuildVisit(class BSPNode *) |
| 1648 | 72.5% | 0.62 | COMPARISON_STYLE,CTRLFLOW,FSEL_TERNARY,OFFSET | system/rndobj/Ribbon: public: void __cdecl RndRibbon::UpdateChase(vo |
| 1648 | 74.4% | 0.64 | COMMUT,OFFSET | system/hamobj/HamRibbon: public: void __cdecl HamRibbon::UpdateChase(vo |
| 2160 | 62.5% | 0.32 | CTRLFLOW,DEAD_STORE_ELIMINATION,MAKESTR,OFFSET | system/gesture/ArcDetector: public: float __cdecl ArcDetector::UpdateOverl |
| 1644 | 76.4% | 0.66 | CTRLFLOW,OFFSET | system/math/Geo: bool __cdecl MakeBSPTree(class BSPNode *&, cla |
| 2572 | 88.3% | 0.80 | COMMUT,CTRLFLOW,FSEL_TERNARY,OFFSET | system/char/CharHair: protected: void __cdecl CharHair::SimulateInte |
| 2184 | 66.4% | 0.31 | CTRLFLOW,MAKESTR,OFFSET | system/char/CharEyes: public: virtual void __cdecl CharEyes::Highlig |
| 3100 | 78.0% | 0.33 | FSEL_TERNARY,OFFSET | system/rndobj/Part: public: virtual void __cdecl RndParticleSys::L |
| 1620 | 70.5% | 0.43 | CTRLFLOW,OFFSET | system/synth_xbox/Synapse_dsp: public: __cdecl DSP::Synapse::Synapse::Synapse |
| 3400 | 85.4% | 0.41 | CTRLFLOW,OFFSET | system/gesture/StreamRenderer: protected: void __cdecl StreamRenderer::DrawTo |
| 3024 | 92.9% | 0.93 | COMMUT,MAKESTR,OFFSET | system/char/CharIKHand: protected: void __cdecl CharIKHand::IKElbow(cl |
| 1724 | 76.5% | 0.45 | CTRLFLOW,OFFSET | system/hamobj/HamMove: public: void __cdecl MoveFrame::Load(class Bin |
| 1092 | 70.3% | 0.55 | CTRLFLOW,OFFSET | system/utl/DebugGraph: public: void __cdecl DebugGraph::Draw(void) |
| 2164 | 84.0% | 0.50 | CTRLFLOW,MAKESTR,OFFSET | system/synth/EQEffect: public: void __cdecl EQEffect::SetParameter(in |
| 1484 | 85.1% | 0.77 | COMMUT,CTRLFLOW,OFFSET | system/rndobj/Trans: private: void __cdecl RndTransformable::ApplyD |
| 1296 | 53.2% | 0.28 | CTRLFLOW,OFFSET | system/hamobj/HamRibbon: public: void __cdecl HamRibbon::ConstructMesh( |
| 2412 | 88.7% | 0.62 | MAKESTR,OFFSET | system/obj/Dir: public: virtual void __cdecl ObjectDir::Save(c |
| 1944 | 77.3% | 0.38 | OFFSET | system/rndobj/PostProc_NG: protected: void __cdecl NgPostProc::DoBloom(vo |
| 1756 | 82.4% | 0.53 | COMMUT,CTRLFLOW,OFFSET | system/char/CharDriver: protected: float __cdecl CharDriver::Display(f |
| 1324 | 66.3% | 0.36 | OFFSET | system/world/SpotlightDrawer_NG: protected: void __cdecl NgSpotlightDrawer::Ren |
| 1164 | 60.2% | 0.33 | OFFSET | system/rndobj/Spline: private: void __cdecl RndSpline::SyncDeformedC |
| 1040 | 68.1% | 0.45 | CTRLFLOW,MAKESTR,OFFSET | system/rndobj/Utl: void __cdecl MakeTangentsLate(class RndMesh *) |
| 2796 | 86.2% | 0.39 | COMMUT,COMPARISON_STYLE,CTRLFLOW,MAKESTR,OFFSET | system/hamobj/HollaBackMinigame: public: void __cdecl HollaBackMinigame::OnBeat |
| 820 | 73.7% | 0.69 | COMMUT | system/rndobj/Bitmap: private: int __cdecl RndBitmap::PixelOffset(in |
| 1468 | 68.5% | 0.32 | COMMUT,CTRLFLOW | system/gesture/SkeletonViz: private: void __cdecl SkeletonViz::DrawJoints( |
| 1976 | 89.0% | 0.67 | COMMUT,OFFSET | system/char/CharEyes: protected: void __cdecl CharEyes::NextLook(voi |
| 2032 | 81.4% | 0.38 | MAKESTR,OFFSET | system/rnddx9/Rnd_Xbox: private: void __cdecl DxRnd::InitBuffers(void) |
| 1432 | 78.1% | 0.46 | OFFSET | system/rnddx9/Rnd_Xbox: private: void __cdecl DxRnd::DoPointTests(void |
| 1164 | 75.4% | 0.50 | CTRLFLOW,MAKESTR | system/os/HDCache: private: void __cdecl HDCache::OpenFiles(int) |
| 848 | 78.2% | 0.76 | COMMUT,CTRLFLOW | system/math/Geo: bool __cdecl Intersect(class Triangle const &, |
| 788 | 61.9% | 0.46 | CTRLFLOW,OFFSET | system/world/Crowd: public: void __cdecl WorldCrowd::Apply3DCharXf |
| 1748 | 84.3% | 0.50 | OFFSET | system/hamobj/PoseFatalities: public: void __cdecl PoseFatalities::DrawDebug |
| 1732 | 83.6% | 0.48 | COMMUT,OFFSET | system/char/ClipDistMap: public: void __cdecl ClipDistMap::Draw(float,  |
| 1376 | 87.8% | 0.81 | CTRLFLOW,OFFSET | system/world/Spotlight: protected: void __cdecl Spotlight::BuildNGShee |
| 544 | 55.9% | 0.55 | OFFSET | system/rndobj/AmbientOcclusion: public: static void __cdecl RndAmbientOcclusio |
| 1332 | 79.1% | 0.47 | COMMUT,CTRLFLOW,OFFSET | system/rndobj/Utl: void __cdecl MakeNormals(class RndMesh *) |
| 1168 | 70.4% | 0.38 | CTRLFLOW | system/synth_xbox/Voice: unsigned long __cdecl StartVoiceThreadEntry(vo |
| 848 | 54.3% | 0.32 | CTRLFLOW,MAKESTR,OFFSET | system/world/SpotlightDrawer: public: static void __cdecl SpotlightDrawer::D |
| 4132 | 95.0% | 0.61 | CTRLFLOW,MAKESTR,OFFSET | system/rndobj/Mesh: public: virtual void __cdecl RndMesh::Load(cla |
| 1092 | 82.8% | 0.64 | CTRLFLOW,OFFSET | system/hamobj/HamAudio: private: void __cdecl HamAudio::PollCrossfade( |
| 1456 | 82.1% | 0.46 | CTRLFLOW,DEAD_STORE_ELIMINATION,OFFSET | system/hamobj/RhythmBattlePlayer: private: void __cdecl RhythmBattlePlayer::Anim |
| 1304 | 80.3% | 0.47 | CTRLFLOW,OFFSET | system/gesture/HandInvokeGestureFilter: private: bool __cdecl HandInvokeGestureFilter: |
| 4872 | 93.1% | 0.35 | COMMUT,CTRLFLOW,OFFSET | system/net/json-c/json_tokener:  |

## Tier A — clustered by UNIT (shared root cause; fix one struct/decl, fix many)

| unit | fns | miss bytes | dominant patterns |
|---|---:|---:|---|
| system/rndobj/Part | 5 | 3,660 | CONTROL_FLOW:4,OFFSET_SWAP:3,FSEL_TERNARY:2 |
| system/rndobj/AmbientOcclusion | 8 | 2,997 | CONTROL_FLOW:5,OFFSET_SWAP:5,COMMUTATIVE_OP_ORDER:3 |
| system/hamobj/MoveDir | 11 | 2,661 | MAKESTR:9,CONTROL_FLOW:7,OFFSET_SWAP:7 |
| system/math/SHA1 | 1 | 2,637 | COMMUTATIVE_OP_ORDER:1,OFFSET_SWAP:1 |
| system/rndobj/Utl | 15 | 2,208 | OFFSET_SWAP:10,COMMUTATIVE_OP_ORDER:5,CONTROL_FLOW:5 |
| system/gesture/ArcDetector | 9 | 1,831 | OFFSET_SWAP:6,CONTROL_FLOW:5,MAKESTR:2 |
| system/rndobj/Text | 18 | 1,750 | CONTROL_FLOW:10,OFFSET_SWAP:10,COMMUTATIVE_OP_ORDER:7 |
| system/world/Spotlight | 6 | 1,415 | CONTROL_FLOW:4,OFFSET_SWAP:4,COMMUTATIVE_OP_ORDER:3 |
| system/char/CharEyes | 9 | 1,252 | CONTROL_FLOW:5,OFFSET_SWAP:5,COMMUTATIVE_OP_ORDER:3 |
| system/math/Geo | 18 | 1,160 | OFFSET_SWAP:12,COMMUTATIVE_OP_ORDER:8,CONTROL_FLOW:7 |
| lazer/game/BustAMovePanel | 8 | 1,135 | MAKESTR:7,CONTROL_FLOW:6,OFFSET_SWAP:2 |
| system/rndobj/Lit_NG | 2 | 1,049 | OFFSET_SWAP:2,CONTROL_FLOW:1,COMMUTATIVE_OP_ORDER:1 |
| system/hamobj/HamRibbon | 4 | 1,046 | OFFSET_SWAP:2,CONTROL_FLOW:2,COMMUTATIVE_OP_ORDER:1 |
| system/meta/StorePanel | 12 | 1,031 | CONTROL_FLOW:7,OFFSET_SWAP:5,MAKESTR:2 |
| system/math/mtx | 3 | 1,026 | COMMUTATIVE_OP_ORDER:2,OFFSET_SWAP:2 |
| system/rnddx9/Rnd_Xbox | 8 | 930 | OFFSET_SWAP:6,MAKESTR:3,COMMUTATIVE_OP_ORDER:2 |
| system/rndobj/Line | 3 | 834 | COMMUTATIVE_OP_ORDER:3,OFFSET_SWAP:2,CONTROL_FLOW:1 |
| system/utl/MemTracker | 10 | 798 | CONTROL_FLOW:9,MAKESTR:2,OFFSET_SWAP:1 |
| system/hamobj/HamDirector | 16 | 788 | CONTROL_FLOW:9,OFFSET_SWAP:9,MAKESTR:2 |
| system/rndobj/Spline | 3 | 757 | OFFSET_SWAP:3,CONTROL_FLOW:1,COMMUTATIVE_OP_ORDER:1 |
| system/synth/EQEffect | 3 | 755 | CONTROL_FLOW:2,OFFSET_SWAP:2,MAKESTR:1 |
| system/hamobj/RhythmBattle | 2 | 713 | MAKESTR:2,CONTROL_FLOW:1,OFFSET_SWAP:1 |
| system/rndobj/Mesh | 11 | 704 | OFFSET_SWAP:8,CONTROL_FLOW:4,MAKESTR:3 |
| system/gesture/LiveCameraInput | 11 | 699 | OFFSET_SWAP:7,CONTROL_FLOW:6,MAKESTR:6 |
| system/utl/Cheats | 1 | 673 | CONTROL_FLOW:1,STATIC_GUARD_COUNTER:1 |
| system/world/Crowd | 7 | 668 | CONTROL_FLOW:5,OFFSET_SWAP:2,COMMUTATIVE_OP_ORDER:2 |
| system/gesture/StreamRenderer | 2 | 604 | CONTROL_FLOW:2,OFFSET_SWAP:1,MAKESTR:1 |
| system/world/SpotlightDrawer_NG | 6 | 596 | CONTROL_FLOW:4,OFFSET_SWAP:2,COMMUTATIVE_OP_ORDER:1 |
| system/rnddx9/ShaderMgr | 2 | 577 | CONTROL_FLOW:2,OFFSET_SWAP:2,DEAD_STORE_ELIMINATION:1 |
| system/os/PlatformMgr_Xbox | 6 | 570 | CONTROL_FLOW:4,OFFSET_SWAP:1,MAKESTR:1 |

## Tier B — PERMUTER candidates, top 30 by size

| size | fuzzy | top patterns | unit : function |
|---:|---:|---|---|
| 4764 | 98.9% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/world/Spotlight: public: virtual bool __cdecl Spotlight::SyncPr |
| 2652 | 95.3% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/hamobj/CamShotCatVO: void __cdecl CamShotVOData(class Symbol, class |
| 2280 | 98.7% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/char/CharClip: public: virtual void __cdecl CharClip::Load(cl |
| 2232 | 99.0% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/world/LightPreset: public: virtual void __cdecl LightPreset::Load |
| 1868 | 99.6% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP | system/rndobj/EventTrigger: public: virtual void __cdecl EventTrigger::Loa |
| 1660 | 91.7% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/char/CharClipDisplay: public: void __cdecl CharClipDisplay::DrawTrac |
| 1628 | 96.2% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,LINKER_MERGED | system/oggvorbis/sharedbook:  |
| 1508 | 99.7% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/world/Crowd: public: virtual void __cdecl WorldCrowd::Load( |
| 1452 | 98.2% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/hamobj/HamAudio: public: void __cdecl HamAudio::FinishLoad(void |
| 1428 | 92.4% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,LINKER_MERGED | system/hamobj/HamDirector: public: virtual void __cdecl HamDirector::Poll |
| 1380 | 99.7% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE | system/ui/UIFontImporter: public: virtual void __cdecl UIFontImporter::L |
| 1360 | 99.4% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP | lazer/meta_ham/SkeletonChooser: private: void __cdecl SkeletonChooser::SetPlay |
| 1348 | 80.9% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,LINKER_MERGED | system/synth/StandardStream: public: void __cdecl StandardStream::InitInfo( |
| 1336 | 98.7% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,LINKER_MERGED | system/math/Geo: bool __cdecl CheckBSPTree(class BSPNode const  |
| 1264 | 95.6% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP | lazer/meta_ham/SongSort: public: virtual void __cdecl SongSort::BuildIt |
| 1248 | 98.1% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP | system/obj/DirLoader: public: static void __cdecl DirLoader::SaveObj |
| 1216 | 95.3% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/os/System_Xbox: class Symbol __cdecl GetSystemLanguage(class S |
| 1200 | 90.2% | REGISTER_SWAP,LINKER_MERGED,ADDRESS_RELOCATION_NOISE | system/oggvorbis/psy:  |
| 1200 | 54.7% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,LINKER_MERGED | system/synth_xbox/FFT: int __cdecl fft_matrix_forward_columnwise(floa |
| 1184 | 93.6% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE | system/flow/FlowNode: public: static class FlowNode * __cdecl FlowNo |
| 1180 | 99.5% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP | system/os/File:  |
| 1144 | 98.4% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE | system/hamobj/HamDirector: public: void __cdecl HamDirector::DrawIconMan( |
| 1144 | 88.5% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,BOOL_MASK | system/rndobj/Flare: public: virtual void __cdecl RndFlare::DrawSho |
| 1116 | 97.4% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,LINKER_MERGED | system/os/File:  |
| 1104 | 85.6% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE | system/gesture/StandingStillGestureFilter: public: void __cdecl StandingStillGestureFilte |
| 1028 | 94.2% | REGISTER_SWAP,LINKER_MERGED,ADDRESS_RELOCATION_NOISE | system/hamobj/HamNavList: public: virtual void __cdecl HamNavList::DrawS |
| 1024 | 98.5% | ADDRESS_RELOCATION_NOISE,REGISTER_SWAP,LINKER_MERGED | system/flow/FlowTrigger: public: virtual void __cdecl FlowTrigger::Load |
| 1016 | 92.3% | REGISTER_SWAP,LINKER_MERGED,ADDRESS_RELOCATION_NOISE | system/oggvorbis/mapping0:  |
| 1016 | 95.7% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE,LINKER_MERGED | system/rndobj/PropKeys: public: virtual void __cdecl SymbolKeys::SetFr |
| 1008 | 96.5% | REGISTER_SWAP,ADDRESS_RELOCATION_NOISE | system/gesture/SkeletonViz: private: void __cdecl SkeletonViz::SetCamera(s |

## Tier C — INVESTIGATE (real logic diffs), top 20 by size

| size | fuzzy | unattributed | unit : function |
|---:|---:|---:|---|
| 3024 | 98.9% | 3 | lazer/meta_ham/VoiceControlPanel: public: class DataNode __cdecl VoiceControlPan |
| 2520 | 97.6% | 18 | system/char/CharDriver: public: virtual class DataNode __cdecl CharDri |
| 2116 | 98.5% | 14 | system/flow/FlowAnimate: protected: void __cdecl FlowAnimate::OnAnimEve |
| 1964 | 98.9% | 22 | system/meta/StorePanel: public: virtual class DataNode __cdecl StorePa |
| 1764 | 99.4% | 105 | system/midi/MidiReader: private: void __cdecl MidiReader::ReadMetaEven |
| 1760 | 99.0% | 61 | lazer/meta_ham/HamSongMgr: public: void __cdecl HamSongMgr::InitializePla |
| 1740 | 98.4% | 12 | system/rndobj/Console: private: bool __cdecl RndConsole::OnMsg(class  |
| 1584 | 99.4% | 2 | system/rndobj/TransAnim: public: virtual void __cdecl RndTransAnim::Loa |
| 1516 | 96.6% | 13 | system/rndobj/Part: public: virtual void __cdecl RndParticleSys::C |
| 1428 | 99.0% | 2 | system/hamobj/RhythmBattle: public: virtual class DataNode __cdecl RhythmB |
| 1388 | 99.4% | 3 | system/char/CharIKHand: public: virtual void __cdecl CharIKHand::Load( |
| 1336 | 88.7% | 66 | system/obj/DataFlex:  |
| 1184 | 97.2% | 8 | system/world/PhysicsVolume: public: virtual bool __cdecl PhysicsVolume::Sy |
| 1152 | 96.4% | 14 | system/hamobj/CharFeedback: public: virtual void __cdecl CharFeedback::Loa |
| 1144 | 99.6% | 40 | system/rndobj/PostProc: public: virtual void __cdecl RndPostProc::Save |
| 1052 | 94.0% | 21 | system/flow/FlowSetProperty: public: virtual void __cdecl FlowSetProperty:: |
| 1036 | 99.7% | 15 | system/char/ClipCollide: protected: void __cdecl ClipCollide::Collide(v |
| 884 | 98.9% | 2 | lazer/meta_ham/SkeletonChooser: private: int __cdecl SkeletonChooser::RoundRob |
| 884 | 96.9% | 13 | system/obj/DirLoader: private: bool __cdecl DirLoader::SetupDir(clas |
| 884 | 99.2% | 2 | system/char/CharServoBone: public: virtual void __cdecl CharServoBone::Po |