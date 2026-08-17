# Divergence Burndown

These are functions where unicorn behavioral testing detected actual behavioral differences between decomp and original code. These represent real bugs, not just register allocation or compiler artifact noise.

## Summary

| Class | Count | Avg Match% | Priority | Description |
|-------|-------|-----------|----------|-------------|
| return_value | 15 | 85.52% | P0 | Functions with wrong return values |
| call_arg | 17 | 81.77% | P0 | Functions with wrong function arguments |
| object_memory | 15 | 73.68% | P1 | Wrong memory access patterns or struct layout mismatches |
| fpr_precision | 2 | 87.03% | P1 | Float precision issues |
| stack_layout | 92 | 85.97% | P2 | Stack frame differences suggesting wrong locals or inlines |
| call_count_top50 | 50 | 69.07% | P3 | Different number of calls (top 50 by size; full list has 352) |

## P0: Return Value Mismatches (15 functions)

Wrong return values - definite logic bugs.

| Status | Match% | Size | Unit | Function |
|--------|--------|------|------|----------|
| [ ] | 3% | 224 | ui/UIList | protected: int __cdecl UIList::CollidePlane(class stlpmtx_std::vector<class Vect... |
| [ ] | 68% | 280 | default/keygen_xbox | void __cdecl KeyChain::getMasher(unsigned char *) |
| [ ] | 79% | 300 | movie/Splash | protected: bool __cdecl Splash::ShowNext(void) |
| [ ] | 80% | 84 | os/PlatformMgr_Xbox | public: bool __cdecl PlatformMgr::HasKinectSharePrvilege(void) const |
| [ ] | 90% | 200 | utl/NetCacheMgr | public: bool __cdecl NetLoaderRef::IsDownloading(void) |
| [ ] | 90% | 104 | hamobj/RhythmDetector | void __cdecl EraseNewerData(class stlpmtx_std::vector<struct RhythmDetector::Fra... |
| [ ] | 90% | 228 | synth_xbox/StreamReceiver | public: virtual void __cdecl StreamReceiver360::Poll(void) |
| [ ] | 92% | 224 | utl/NetCacheMgr | public: bool __cdecl NetLoaderRef::NeedsToDownload(void) |
| [ ] | 94% | 292 | hamobj/HamDirector | public: class Key<class Symbol> * __cdecl Keys<class Symbol, class Symbol>::KeyN... |
| [ ] | 96% | 904 | gesture/StreamRenderer | protected: __cdecl StreamRenderer::StreamRenderer(void) |
| [ ] | 97% | 16 | meta_ham/FitnessCalorieSortByCalorie | public: virtual int __cdecl FitnessCalorieSortCmp::Compare(class NavListItemSort... |
| [ ] | 98% | 32 | ui/Utl | int __cdecl PageDirection(enum JoypadAction) |
| [ ] | 98% | 100 | default/keygen_xbox | void __cdecl KeyChain::getKey(unsigned int, unsigned char *, unsigned char *) |
| [ ] | 99% | 104 | meta_ham/SongSortMgr | public: virtual int __cdecl SongSortMgr::GetListIndexFromHeaderIndex(int) |
| [ ] | 99% | 312 | obj/DataNode | public: static char const * __cdecl DataNode::DataTypeString(enum DataType) |

## P0: Call Argument Mismatches (17 functions)

Wrong function arguments passed - definite logic bugs.

| Status | Match% | Size | Unit | Function |
|--------|--------|------|------|----------|
| [ ] | 5% | 132 | synth_xbox/SynthSample | void * __cdecl SampleAlloc(int, char const *, int, char const *, int) |
| [ ] | 47% | 444 | synth/Sequence | public: __cdecl RandomGroupSeqInst::RandomGroupSeqInst(class RandomGroupSeq *) |
| [ ] | 78% | 120 | os/File | FileTerminate |
| [ ] | 79% | 236 | char/CharClip | public: void __cdecl CharClip::Transitions::RemoveNodes(class CharClip::NodeVect... |
| [ ] | 84% | 284 | rndobj/PostProc_NG | public: static void __cdecl NgPostProc::RebuildTex(void) |
| [ ] | 84% | 76 | rnddx9/RenderState | public: void __cdecl RndRenderState::Init(void) |
| [ ] | 85% | 120 | synth_xbox/SynapseAPO | public: __cdecl DSP::SynapseAPO::SynapseAPO(void) |
| [ ] | 85% | 184 | ui/UIListMesh | protected: virtual class UIListSlotElement * __cdecl UIListMesh::CreateElement(c... |
| [ ] | 86% | 328 | rndobj/PartLauncher | protected: __cdecl RndPartLauncher::RndPartLauncher(void) |
| [ ] | 89% | 76 | rndobj/Morph | class BinStreamRev & __cdecl operator>><struct Weight>(class BinStreamRev &, cla... |
| [ ] | 89% | 108 | utl/AllocInfo | class Pool & __cdecl GetPool(void) |
| [ ] | 92% | 676 | rnddx9/Mesh | protected: __cdecl DxMesh::DxMesh(void) |
| [ ] | 95% | 424 | meta_ham/ChallengeSortNode | public: virtual void __cdecl ChallengeSortNode::OnContentMounted(char const *, c... |
| [ ] | 95% | 264 | utl/MemMgr | void __cdecl AddHeap(int, int, char const *, bool, int, enum MemHeap::Strategy, ... |
| [ ] | 95% | 216 | SoundTouch/TDStretch | public: __cdecl soundtouch::TDStretch::TDStretch(void) |
| [ ] | 97% | 168 | utl/trie | public: unsigned int __cdecl Trie::get_free_node(void) |
| [ ] | 98% | 580 | net_ham/RockCentral | public: class DataNode __cdecl RockCentral::OnMsg(class ServerStatusChangedMsg c... |

## P1: Object Memory Mismatches (15 functions)

Wrong memory access patterns or struct layout mismatches - likely type confusion.

| Status | Match% | Size | Unit | Function |
|--------|--------|------|------|----------|
| [ ] | 16% | 176 | hamobj/Pose | public: __cdecl JointDistPoseElement::JointDistPoseElement(enum SkeletonJoint, e... |
| [ ] | 32% | 144 | os/DateTime | public: __cdecl DateTime::DateTime(unsigned int) |
| [ ] | 32% | 124 | moviebink/BinkMovieSys | public: __cdecl BinkMovieSys::BinkMovieSys(void) |
| [ ] | 70% | 80 | synth_xbox/FxSendSynapse | public: __cdecl DSP::SynapseAPOParams::SynapseAPOParams(void) |
| [ ] | 77% | 76 | hamobj/HamDriver | public: virtual __cdecl HamDriver::LayerClip::~LayerClip(void) |
| [ ] | 81% | 96 | hamobj/HamDriver | public: virtual void * __cdecl HamDriver::LayerClip::`scalar deleting destructor... |
| [ ] | 84% | 80 | math/Rand | public: void __cdecl Rand::Seed(int) |
| [ ] | 86% | 88 | meta_ham/ChallengeSortNode | public: virtual void * __cdecl ChallengeHeaderNode::`scalar deleting destructor'... |
| [ ] | 86% | 88 | meta_ham/FitnessCalorieSortNode | public: virtual void * __cdecl FitnessCalorieHeaderNode::`scalar deleting destru... |
| [ ] | 86% | 88 | meta_ham/MQSongSortNode | public: virtual void * __cdecl MQSongHeaderNode::`scalar deleting destructor'(un... |
| [ ] | 86% | 88 | meta_ham/PlaylistSortNode | public: virtual void * __cdecl PlaylistHeaderNode::`scalar deleting destructor'(... |
| [ ] | 86% | 132 | os/BlockMgr | public: __cdecl Block::Block(void) |
| [ ] | 88% | 108 | utl/MultiTempoTempoMap | public: virtual __cdecl MultiTempoTempoMap::~MultiTempoTempoMap(void) |
| [ ] | 90% | 108 | synth/FlangerEffect | public: void __cdecl FlangerEffect::SetParameters(struct FlangerEffect::Params c... |
| [ ] | 99% | 140 | char/CharLipSync | void __cdecl stlpmtx_std::fill<struct stlpmtx_std::_Bit_iter<struct stlpmtx_std:... |

## P1: Float Precision Mismatches (2 functions)

Float precision issues - likely type mismatch (float vs double).

| Status | Match% | Size | Unit | Function |
|--------|--------|------|------|----------|
| [ ] | 79% | 192 | math/Rot | void __cdecl Multiply(class Vector3const &, class Hmx::Quat const &, class Vecto... |
| [ ] | 94% | 208 | flow/FlowSetProperty | float __cdecl EaseStairstep(float, float, float) |

## P2: Stack Layout Mismatches (92 functions)

Stack frame differences suggesting wrong local variable count/types or inline expansion mismatches. Showing lowest-match functions only.

| Status | Match% | Size | Unit | Function |
|--------|--------|------|------|----------|
| [ ] | 29% | 1284 | gesture/DrawUtl | bool __cdecl UpdateBufferTex(class LiveCameraInput *, class RndTex *, enum LiveC... |
| [ ] | 46% | 148 | ui/UILabel | public: void __cdecl stlpmtx_std::vector<struct UILabel::LabelStyle, class stlpm... |
| [ ] | 54% | 560 | hamobj/HamWardrobe | class Symbol __cdecl GetDanceBattleBackupOutfit(class Symbol, class Symbol) |
| [ ] | 58% | 448 | synth_xbox/FftIpp | public: void __cdecl FftIpp::SetMode(int) |
| [ ] | 60% | 904 | meta_ham/HamStorePanel | public: __cdecl HamStorePanel::HamStorePanel(void) |
| [ ] | 66% | 372 | ui/UILabel | protected: class DataNode __cdecl UILabel::OnSetTokenFmt(class DataArray const *... |
| [ ] | 67% | 612 | hamobj/HamDirector | protected: class Symbol __cdecl HamDirector::ClosestMove(void) |
| [ ] | 67% | 588 | utl/MemTracker | public: void __cdecl MemTracker::Report(int, class TextStream &) |
| [ ] | 69% | 112 | rndobj/MatAnim | public: int __cdecl RndMatAnim::TexKeys::Add(class RndTex *, float, bool) |
| [ ] | 69% | 2340 | rndobj/Part | protected: void __cdecl RndParticleSys::MoveParticles(float, float) |

(and 82 more)

## P3: Call Count Mismatches (50 shown, full list has 352)

Different number of function calls - could be missing stubs, wrong asserts, inline expansion differences, or test-dependent code. Top 50 by function size.

| Status | Match% | Size | Unit | Function |
|--------|--------|------|------|----------|
| [ ] | 3% | 1412 | char/CharBonesSamples | public: void __cdecl CharBonesSamples::Relativize(class CharClip *) |
| [ ] | 14% | 1232 | synth/BinkReader | public: virtual void __cdecl BinkReader::Poll(float) |
| [ ] | 15% | 1076 | meta_ham/CursorPanel | public: virtual void __cdecl CursorPanel::Poll(void) |
| [ ] | 15% | 1200 | ui/UILabel | bool __cdecl PropSync(struct UILabel::LabelStyle &, class DataNode &, class Data... |
| [ ] | 15% | 1036 | char/ClipCollide | protected: void __cdecl ClipCollide::Collide(void) |
| [ ] | 18% | 852 | hamobj/MoveGraph | public: bool __cdecl MoveGraph::FindVariantPair(class MoveVariant const *&, clas... |
| [ ] | 20% | 1256 | rndobj/AmbientOcclusion | public: void __cdecl kdTree<class Triangle>::kdTreeNode::Pack(enum kdTree<class ... |
| [ ] | 25% | 1200 | synth_xbox/FFT | int __cdecl fft_matrix_forward_columnwise(float *, long, float *) |
| [ ] | 30% | 840 | synth_xbox/Synth | public: virtual void __cdecl Synth360::Init(void) |
| [ ] | 32% | 1416 | meta_ham/OptionsPanel | protected: class DataNode __cdecl OptionsPanel::OnMsg(class RCJobCompleteMsg con... |

(and 40 more)

## Methodology

### How to Work on These Functions

1. Run `/recon` on a function to understand its database status and behavioral verdict
2. Use `run_objdiff` to see specific instruction mismatches
3. Use `/ghidra-decompile` to see Ghidra's decompilation of the target function
4. Compare the target code with your implementation and identify the bug
5. Fix the source code and test with `run_objdiff` to verify

### False Positives

Functions at 100% match that show as DIVERGENT are false positives from unicorn limitations (e.g., timing-sensitive code, external state dependencies, or test harness differences). Focus effort on lower-match functions where real bugs are likely.

### Excluded Classes

These divergence classes are excluded as unfixable:
- `build_env` (623 functions) - Unfixable build/environment artifacts (__FILE__ paths, merged symbols from ICF)
- `regalloc` (18 functions) - Register allocation noise (reordering variable declarations sometimes helps, but often unfixable)
- `error` (224 functions) - Unicorn harness errors (crash detection, external state)

### Source

Generated from unicorn behavioral testing run on 2026-02-20:
```
python3 scripts/unicorn/batch_to_db.py --force
```

Results represent 1,779 divergent functions out of 25,682 total (6.9% divergence rate). The remaining 93.1% are behaviorally equivalent.
