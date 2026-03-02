# Surfaced Work from base_size=0 False COMPLETE Reset

Date: 2026-03-02

## Context

747 functions were falsely marked COMPLETE during initial DB population (Jan 2026)
when objdiff reported 100% match for functions where the original binary had no
extracted code to compare against (base_size=0). The `reset_false_complete.py` script
cleared these verdicts, revealing real missing implementations.

This document lists the **694 real functions** (excluding template instantiations,
std:: boilerplate, vcall thunks, BinStream operators, and other noise).

Spot-checked samples confirmed these are genuine work:
- `CharClipDriver::Evaluate` -- 75.4% match, fixable
- `CharClipDriver::AlignToBeat` -- 99.4% match, nearly done
- `CharBonesSamples::EvaluateChannel` -- 0% match, needs full implementation
- `MakeRotMatrixX` -- stub, no decomp code exists

## Functions by Unit (152 files, 694 functions)

### `src/lazer/meta_ham/SongSortMgr.cpp` (3)

| Function | Symbol |
|----------|--------|
| `public: __cdecl SongSortBySong::SongSortBySong(void)` | `??0SongSortBySong@@QAA@XZ` |
| `public: void __cdecl SongSortMgr::SetSetlistMode(bool)` | `?SetSetlistMode@SongSortMgr@@QAAX_N@Z` |
| `public: void __cdecl SongSortMgr::SetupQuasiRandomSongs(void)` | `?SetupQuasiRandomSongs@SongSortMgr@@QAAXXZ` |

### `src/system/char/Char.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: __cdecl MsgSinks::Sink::~Sink(void)` | `??1Sink@MsgSinks@@QAA@XZ` |

### `src/system/char/CharBoneDir.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual class stlpmtx_std::vector<struct TrackChannels, class stlpmtx_std::StlNodeAlloc<struct TrackChannels> > const & __cdecl SongInfoCopy::GetTracks(void) const` | `?GetTracks@SongInfoCopy@@UBAABV?$vector@UTrackChannels@@V?$StlNodeAlloc@UTrackChannels@@@stlpmtx_std@@@stlpmtx_std@@XZ` |

### `src/system/char/CharBoneOffset.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual class DataNode __cdecl PhotoSpotlightPositioner::Handle(class DataArray *, bool)` | `?Handle@PhotoSpotlightPositioner@@UAA?AVDataNode@@PAVDataArray@@_N@Z` |

### `src/system/char/CharBonesMeshes.cpp` (4)

| Function | Symbol |
|----------|--------|
| `protected: virtual bool __cdecl CharBonesMeshes::Replace(class ObjRef *, class Hmx::Object *)` | `?Replace@CharBonesMeshes@@MAA_NPAVObjRef@@PAVObject@Hmx@@@Z` |
| `void __cdecl MakeRotMatrixX(float, class Hmx::Matrix3&)` | `?MakeRotMatrixX@@YAXMAAVMatrix3@Hmx@@@Z` |
| `void __cdecl MakeRotMatrixY(float, class Hmx::Matrix3&)` | `?MakeRotMatrixY@@YAXMAAVMatrix3@Hmx@@@Z` |
| `void __cdecl MakeRotMatrixZ(float, class Hmx::Matrix3&)` | `?MakeRotMatrixZ@@YAXMAAVMatrix3@Hmx@@@Z` |

### `src/system/char/CharBonesSamples.cpp` (4)

| Function | Symbol |
|----------|--------|
| `public: virtual bool __cdecl CharBonesSamples::SyncProperty(class DataNode &, class DataArray *, int, enum PropOp)` | `?SyncProperty@CharBonesSamples@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z` |
| `public: void __cdecl CharBonesSamples::EvaluateChannel(void *, int, int, float)` | `?EvaluateChannel@CharBonesSamples@@QAAXPAXHHM@Z` |
| `public: void __cdecl CharBonesSamples::Save(class BinStream &)` | `?Save@CharBonesSamples@@QAAXAAVBinStream@@@Z` |
| `void __cdecl NormalizeTo(class Hmx::Quat const &, class Hmx::Quat &)` | `?NormalizeTo@@YAXABVQuat@Hmx@@AAV12@@Z` |

### `src/system/char/CharClip.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl CharClip::Transitions::AddNode(class CharClip *, struct CharGraphNode const &)` | `?AddNode@Transitions@CharClip@@QAAXPAV2@ABUCharGraphNode@@@Z` |

### `src/system/char/CharClipDisplay.cpp` (5)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl CharClipDisplay::DrawBeatString(float, class Hmx::Color const &)` | `?DrawBeatString@CharClipDisplay@@QAAXMABVColor@Hmx@@@Z` |
| `public: void __cdecl CharClipDisplay::DrawBlend(float, float)` | `?DrawBlend@CharClipDisplay@@QAAXMM@Z` |
| `public: void __cdecl CharClipDisplay::DrawCursor(void)` | `?DrawCursor@CharClipDisplay@@QAAXXZ` |
| `public: void __cdecl CharClipDisplay::DrawTrack(void)` | `?DrawTrack@CharClipDisplay@@QAAXXZ` |
| `public: void __cdecl CharClipDisplay::SetStartEnd(float, float, bool)` | `?SetStartEnd@CharClipDisplay@@QAAXMM_N@Z` |

### `src/system/char/CharClipDriver.cpp` (6)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl CharClipDriver::ExecuteEvent(class Symbol)` | `?ExecuteEvent@CharClipDriver@@IAAXVSymbol@@@Z` |
| `protected: void __cdecl CharClipDriver::PlayEvents(float)` | `?PlayEvents@CharClipDriver@@IAAXM@Z` |
| `public: class CharClipDriver * __cdecl CharClipDriver::PreEvaluate(float, float, float)` | `?PreEvaluate@CharClipDriver@@QAAPAV1@MMM@Z` |
| `public: float __cdecl CharClipDriver::AlignToBeat(float)` | `?AlignToBeat@CharClipDriver@@QAAMM@Z` |
| `public: float __cdecl CharClipDriver::Evaluate(float, float, float)` | `?Evaluate@CharClipDriver@@QAAMMMM@Z` |
| `public: void __cdecl CharClipDriver::SetBeatOffset(float, enum TaskUnits, class Symbol)` | `?SetBeatOffset@CharClipDriver@@QAAXMW4TaskUnits@@VSymbol@@@Z` |

### `src/system/char/CharClipGroup.cpp` (5)

| Function | Symbol |
|----------|--------|
| `public: class CharClip * __cdecl CharClipGroup::FindClip(char const *) const` | `?FindClip@CharClipGroup@@QBAPAVCharClip@@PBD@Z` |
| `public: int __cdecl Rand::FastInt(int, int)` | `?FastInt@Rand@@QAAHHH@Z` |
| `public: int __cdecl Rand::Int(void)` | `?Int@Rand@@QAAHXZ` |
| `public: void __cdecl CharClipGroup::DeleteRemaining(int)` | `?DeleteRemaining@CharClipGroup@@QAAXH@Z` |
| `public: void __cdecl CharClipGroup::SetClipFlags(int)` | `?SetClipFlags@CharClipGroup@@QAAXH@Z` |

### `src/system/char/CharClipSet.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharClipSet::Draw(void)` | `?Draw@CharClipSet@@UAAXXZ` |

### `src/system/char/CharCollide.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharCollide::Highlight(void)` | `?Highlight@CharCollide@@UAAXXZ` |

### `src/system/char/CharDriver.cpp` (12)

| Function | Symbol |
|----------|--------|
| `class CharClip * __cdecl MyFindClip(class DataNode const &, class ObjectDir *)` | `?MyFindClip@@YAPAVCharClip@@ABVDataNode@@PAVObjectDir@@@Z` |
| `protected: class DataNode __cdecl CharDriver::OnGetFirstFlags(class DataArray const *)` | `?OnGetFirstFlags@CharDriver@@IAA?AVDataNode@@PBVDataArray@@@Z` |
| `protected: float __cdecl CharDriver::Display(float)` | `?Display@CharDriver@@IAAMM@Z` |
| `protected: struct stlpmtx_std::_Rb_tree_node_base * __cdecl stlpmtx_std::_Rb_tree<class CharClip *, struct stlpmtx_std::less<class CharClip *>, struct stlpmtx_std::pair<class CharClip *const, float>, struct stlpmtx_std::_Select1st<struct stlpmtx_std::pair<class CharClip *const, float> >, struct stlpmtx_std::priv::_MapTraitsT<struct stlpmtx_std::pair<class CharClip *const, float> >, class stlpmtx_std::StlNodeAlloc<struct stlpmtx_std::pair<class CharClip *const, float> > >::_M_create_node(struct stlpmtx_std::pair<class CharClip *const, float> const &)` | `?_M_create_node@?$_Rb_tree@PAVCharClip@@U?$less@PAVCharClip@@@stlpmtx_std@@U?$pair@QAVCharClip@@M@3@U?$_Select1st@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@@stlpmtx_std@@IAAPAU_Rb_tree_node_base@2@ABU?$pair@QAVCharClip@@M@2@@Z` |
| `public: class CharClip * __cdecl CharDriver::FindClip(class DataNode const &, bool)` | `?FindClip@CharDriver@@QAAPAVCharClip@@ABVDataNode@@_N@Z` |
| `public: struct stlpmtx_std::pair<struct stlpmtx_std::_Rb_tree_iterator<struct stlpmtx_std::pair<class CharClip *const, float>, struct stlpmtx_std::priv::_MapTraitsT<struct stlpmtx_std::pair<class CharClip *const, float> > >, bool> __cdecl stlpmtx_std::_Rb_tree<class CharClip *, struct stlpmtx_std::less<class CharClip *>, struct stlpmtx_std::pair<class CharClip *const, float>, struct stlpmtx_std::_Select1st<struct stlpmtx_std::pair<class CharClip *const, float> >, struct stlpmtx_std::priv::_MapTraitsT<struct stlpmtx_std::pair<class CharClip *const, float> >, class stlpmtx_std::StlNodeAlloc<struct stlpmtx_std::pair<class CharClip *const, float> > >::insert_unique(struct stlpmtx_std::pair<class CharClip *const, float> const &)` | `?insert_unique@?$_Rb_tree@PAVCharClip@@U?$less@PAVCharClip@@@stlpmtx_std@@U?$pair@QAVCharClip@@M@3@U?$_Select1st@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@@stlpmtx_std@@QAA?AU?$pair@U?$_Rb_tree_iterator@U?$pair@QAVCharClip@@M@stlpmtx_std@@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@2@@stlpmtx_std@@_N@2@ABU?$pair@QAVCharClip@@M@2@@Z` |
| `public: virtual bool __cdecl CharDriver::SyncProperty(class DataNode &, class DataArray *, int, enum PropOp)` | `?SyncProperty@CharDriver@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z` |
| `public: virtual class DataNode __cdecl CharDriver::Handle(class DataArray *, bool)` | `?Handle@CharDriver@@UAA?AVDataNode@@PAVDataArray@@_N@Z` |
| `public: virtual void __cdecl CharDriver::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` | `?Copy@CharDriver@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z` |
| `public: virtual void __cdecl CharDriver::Poll(void)` | `?Poll@CharDriver@@UAAXXZ` |
| `public: void __cdecl CharDriver::SetBeatScale(float, bool)` | `?SetBeatScale@CharDriver@@QAAXM_N@Z` |
| `public: void __cdecl CharDriver::SetClipWeightMap(void)` | `?SetClipWeightMap@CharDriver@@QAAXXZ` |

### `src/system/char/CharEyes.cpp` (11)

| Function | Symbol |
|----------|--------|
| `float __cdecl pow(float, float)` | `?pow@@YAMMM@Z` |
| `protected: class DataNode __cdecl CharEyes::OnAddInterest(class DataArray *)` | `?OnAddInterest@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `protected: class DataNode __cdecl CharEyes::OnToggleForceFocus(class DataArray *)` | `?OnToggleForceFocus@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `protected: class DataNode __cdecl CharEyes::OnToggleInterestOverlay(class DataArray *)` | `?OnToggleInterestOverlay@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `protected: class Vector3 __cdecl CharEyes::GenerateDartOffset(void)` | `?GenerateDartOffset@CharEyes@@IAA?AVVector3@@XZ` |
| `protected: void __cdecl CharEyes::UpdateOverlay(void)` | `?UpdateOverlay@CharEyes@@IAAXXZ` |
| `public: bool __cdecl CharEyes::CharInterestState::IsInRefractoryPeriod(void)` | `?IsInRefractoryPeriod@CharInterestState@CharEyes@@QAA_NXZ` |
| `public: virtual void __cdecl CharEyes::Exit(void)` | `?Exit@CharEyes@@UAAXXZ` |
| `public: virtual void __cdecl CharEyes::ListPollChildren(class stlpmtx_std::list<class RndPollable *, class stlpmtx_std::StlNodeAlloc<class RndPollable *> > &) const` | `?ListPollChildren@CharEyes@@UBAXAAV?$list@PAVRndPollable@@V?$StlNodeAlloc@PAVRndPollable@@@stlpmtx_std@@@stlpmtx_std@@@Z` |
| `public: virtual void __cdecl CharEyes::PollDeps(class stlpmtx_std::list<class Hmx::Object *, class stlpmtx_std::StlNodeAlloc<class Hmx::Object *> > &, class stlpmtx_std::list<class Hmx::Object *, class stlpmtx_std::StlNodeAlloc<class Hmx::Object *> > &)` | `?PollDeps@CharEyes@@UAAXAAV?$list@PAVObject@Hmx@@V?$StlNodeAlloc@PAVObject@Hmx@@@stlpmtx_std@@@stlpmtx_std@@0@Z` |
| `public: void __cdecl CharEyes::AddInterestObject(class CharInterest *)` | `?AddInterestObject@CharEyes@@QAAXPAVCharInterest@@@Z` |

### `src/system/char/CharForeTwist.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharForeTwist::Poll(void)` | `?Poll@CharForeTwist@@UAAXXZ` |

### `src/system/char/CharHair.cpp` (7)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl CharHair::DoReset(int)` | `?DoReset@CharHair@@IAAXH@Z` |
| `protected: void __cdecl CharHair::SimulateInternal(float)` | `?SimulateInternal@CharHair@@IAAXM@Z` |
| `public: __cdecl CharHair::Point::Point(class Hmx::Object *)` | `??0Point@CharHair@@QAA@PAVObject@Hmx@@@Z` |
| `public: float __cdecl CharCollide::GetRadius(class Vector3const &, class Vector3&) const` | `?GetRadius@CharCollide@@QBAMABVVector3@@AAV2@@Z` |
| `public: void __cdecl CharCollide::SyncWorldState(void)` | `?SyncWorldState@CharCollide@@QAAXXZ` |
| `public: void __cdecl CharHair::FreezePoseRaw(void)` | `?FreezePoseRaw@CharHair@@QAAXXZ` |
| `public: void __cdecl CharHair::Hookup(class ObjPtrList<class CharCollide, class ObjectDir> &)` | `?Hookup@CharHair@@QAAXAAV?$ObjPtrList@VCharCollide@@VObjectDir@@@@@Z` |

### `src/system/char/CharIKFingers.cpp` (6)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl CharIKFingers::CalculateFingerDest(enum CharIKFingers::FingerNum)` | `?CalculateFingerDest@CharIKFingers@@IAAXW4FingerNum@1@@Z` |
| `protected: void __cdecl CharIKFingers::CalculateHandDest(int, int)` | `?CalculateHandDest@CharIKFingers@@IAAXHH@Z` |
| `protected: void __cdecl CharIKFingers::FixSingleFinger(class RndTransformable *, class RndTransformable *, class RndTransformable *)` | `?FixSingleFinger@CharIKFingers@@IAAXPAVRndTransformable@@00@Z` |
| `protected: void __cdecl CharIKFingers::MoveFinger(enum CharIKFingers::FingerNum)` | `?MoveFinger@CharIKFingers@@IAAXW4FingerNum@1@@Z` |
| `public: void __cdecl CharIKFingers::MeasureLengths(void)` | `?MeasureLengths@CharIKFingers@@QAAXXZ` |
| `void __cdecl Invert(class Transform const &, class Transform &)` | `?Invert@@YAXABVTransform@@AAV1@@Z` |

### `src/system/char/CharIKHand.cpp` (4)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl CharIKHand::IKElbow(class RndTransformable *, class RndTransformable *)` | `?IKElbow@CharIKHand@@IAAXPAVRndTransformable@@0@Z` |
| `public: virtual void __cdecl CharIKHand::Highlight(void)` | `?Highlight@CharIKHand@@UAAXXZ` |
| `public: virtual void __cdecl CharIKHand::Poll(void)` | `?Poll@CharIKHand@@UAAXXZ` |
| `void __cdecl ScaleAddEq(class Hmx::Quat &, class Hmx::Quat const &, float)` | `?ScaleAddEq@@YAXAAVQuat@Hmx@@ABV12@M@Z` |

### `src/system/char/CharIKHead.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharIKHead::Poll(void)` | `?Poll@CharIKHead@@UAAXXZ` |

### `src/system/char/CharIKScale.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharIKScale::Poll(void)` | `?Poll@CharIKScale@@UAAXXZ` |

### `src/system/char/CharInterest.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: class CharEyeDartRuleset const * __cdecl CharInterest::GetDartRulesetOverride(void) const` | `?GetDartRulesetOverride@CharInterest@@QBAPBVCharEyeDartRuleset@@XZ` |

### `src/system/char/CharLipSync.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual __cdecl AnimPtr::~AnimPtr(void)` | `??1AnimPtr@@UAA@XZ` |

### `src/system/char/CharLipSyncDriver.cpp` (2)

| Function | Symbol |
|----------|--------|
| `float __cdecl Mod(float, float)` | `?Mod@@YAMMM@Z` |
| `protected: void __cdecl CharLipSyncDriver::UpdatePlayback(class CharLipSync::PlayBack *, float, float)` | `?UpdatePlayback@CharLipSyncDriver@@IAAXPAVPlayBack@CharLipSync@@MM@Z` |

### `src/system/char/CharLookAt.cpp` (4)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharLookAt::Highlight(void)` | `?Highlight@CharLookAt@@UAAXXZ` |
| `public: virtual void __cdecl CharLookAt::Poll(void)` | `?Poll@CharLookAt@@UAAXXZ` |
| `void __cdecl DrawBounds(class Vector3, class Hmx::Matrix3const &, class Vector3const &, class RndGraph *)` | `?DrawBounds@@YAXVVector3@@ABVMatrix3@Hmx@@ABV1@PAVRndGraph@@@Z` |
| `void __cdecl Multiply(class Hmx::Matrix3const &, class Hmx::Matrix3const &, class Hmx::Matrix3&)` | `?Multiply@@YAXABVMatrix3@Hmx@@0AAV12@@Z` |

### `src/system/char/CharMeshHide.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: static void * __cdecl CharMeshHide::operator new(unsigned int)` | `??2CharMeshHide@@SAPAXI@Z` |
| `public: static void __cdecl CharMeshHide::operator delete(void *)` | `??3CharMeshHide@@SAXPAX@Z` |

### `src/system/char/CharNeckTwist.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharNeckTwist::Poll(void)` | `?Poll@CharNeckTwist@@UAAXXZ` |

### `src/system/char/CharSignalApplier.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual class DataNode __cdecl CharBoneTwist::Handle(class DataArray *, bool)` | `?Handle@CharBoneTwist@@UAA?AVDataNode@@PAVDataArray@@_N@Z` |

### `src/system/char/CharWeightable.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl TrueColor::ExposureRecipe::SetGlobalGain(float)` | `?SetGlobalGain@ExposureRecipe@TrueColor@@QAAXM@Z` |

### `src/system/char/Character.cpp` (16)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl ObjDirItr<class CharInterest>::Advance(void)` | `?Advance@?$ObjDirItr@VCharInterest@@@@AAAXXZ` |
| `private: void __cdecl ObjDirItr<class Character>::Advance(void)` | `?Advance@?$ObjDirItr@VCharacter@@@@AAAXXZ` |
| `protected: bool __cdecl CharPollableSorter::ChangedByRecurse(struct CharPollableSorter::Dep *)` | `?ChangedByRecurse@CharPollableSorter@@IAA_NPAUDep@1@@Z` |
| `protected: void __cdecl Character::SyncShadow(void)` | `?SyncShadow@Character@@IAAXXZ` |
| `protected: void __cdecl Character::UnhookShadow(void)` | `?UnhookShadow@Character@@IAAXXZ` |
| `public: __cdecl ObjDirItr<class CharInterest>::ObjDirItr<class CharInterest>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VCharInterest@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: __cdecl ObjDirItr<class Character>::ObjDirItr<class Character>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VCharacter@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: class ObjDirItr<class CharInterest> & __cdecl ObjDirItr<class CharInterest>::operator++(void)` | `??E?$ObjDirItr@VCharInterest@@@@QAAAAV0@XZ` |
| `public: class ObjDirItr<class Character> & __cdecl ObjDirItr<class Character>::operator++(void)` | `??E?$ObjDirItr@VCharacter@@@@QAAAAV0@XZ` |
| `public: class RndDrawable * __cdecl DrawPtrVec::CollideShowing(class Segment const &, float &, class Plane &) const` | `?CollideShowing@DrawPtrVec@@QBAPAVRndDrawable@@ABVSegment@@AAMAAVPlane@@@Z` |
| `public: class Symbol __cdecl Hmx::Object::Type(void) const` | `?Type@Object@Hmx@@QBA?AVSymbol@@XZ` |
| `public: virtual void __cdecl Character::DrawShowing(void)` | `?DrawShowing@Character@@UAAXXZ` |
| `public: void __cdecl CharPollableSorter::Sort(class stlpmtx_std::vector<class RndPollable *, class stlpmtx_std::StlNodeAlloc<class RndPollable *> > &)` | `?Sort@CharPollableSorter@@QAAXAAV?$vector@PAVRndPollable@@V?$StlNodeAlloc@PAVRndPollable@@@stlpmtx_std@@@stlpmtx_std@@@Z` |
| `public: void __cdecl Character::FindInterestObjects(class ObjectDir *)` | `?FindInterestObjects@Character@@QAAXPAVObjectDir@@@Z` |
| `public: void __cdecl DrawPtrVec::Draw(void) const` | `?Draw@DrawPtrVec@@QBAXXZ` |
| `public: void __cdecl ObjPtrVec<class RndDrawable, class ObjectDir>::merge(class ObjPtrVec<class RndDrawable, class ObjectDir> const &)` | `?merge@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAAXABV1@@Z` |

### `src/system/char/CharacterTest.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: __cdecl ClipDistMap::Array2d::~Array2d(void)` | `??1Array2d@ClipDistMap@@QAA@XZ` |
| `public: __cdecl ClipDistMap::~ClipDistMap(void)` | `??1ClipDistMap@@QAA@XZ` |

### `src/system/char/ClipCollide.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl Transform::LookAt(class Vector3const &, class Vector3const &)` | `?LookAt@Transform@@QAAXABVVector3@@0@Z` |

### `src/system/char/ClipDistMap.cpp` (6)

| Function | Symbol |
|----------|--------|
| `OnlyReturns` | `OnlyReturns` |
| `protected: bool __cdecl ClipDistMap::LocalMin(int, int)` | `?LocalMin@ClipDistMap@@IAA_NHH@Z` |
| `protected: void __cdecl ClipDistMap::FindBestNodeRecurse(float, float, float, float, float)` | `?FindBestNodeRecurse@ClipDistMap@@IAAXMMMMM@Z` |
| `public: __cdecl DistEntry::~DistEntry(void)` | `??1DistEntry@@QAA@XZ` |
| `public: void __cdecl ClipDistMap::Draw(float, float, class CharDriver *)` | `?Draw@ClipDistMap@@QAAXMMPAVCharDriver@@@Z` |
| `public: void __cdecl ClipDistMap::FindDists(float, class DataArray *)` | `?FindDists@ClipDistMap@@QAAXMPAVDataArray@@@Z` |

### `src/system/char/FileMerger.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: __cdecl ObjDirPtr<class ObjectDir>::ObjDirPtr<class ObjectDir>(class ObjectDir *)` | `??0?$ObjDirPtr@VObjectDir@@@@QAA@PAVObjectDir@@@Z` |

### `src/system/char/FileMergerOrganizer.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual bool __cdecl SongCollision::SyncProperty(class DataNode &, class DataArray *, int, enum PropOp)` | `?SyncProperty@SongCollision@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z` |

### `src/system/char/Waypoint.cpp` (5)

| Function | Symbol |
|----------|--------|
| `private: static class DataNode __cdecl Waypoint::OnWaypointLast(class DataArray *)` | `?OnWaypointLast@Waypoint@@CA?AVDataNode@@PAVDataArray@@@Z` |
| `public: int __cdecl Rand::Int(int, int)` | `?Int@Rand@@QAAHHH@Z` |
| `public: static class Waypoint * __cdecl Waypoint::FindNearest(class Vector3const &, int)` | `?FindNearest@Waypoint@@SAPAV1@ABVVector3@@H@Z` |
| `public: virtual class DataNode __cdecl CharInterest::Handle(class DataArray *, bool)` | `?Handle@CharInterest@@UAA?AVDataNode@@PAVDataArray@@_N@Z` |
| `public: void __cdecl Waypoint::Constrain(class Transform &)` | `?Constrain@Waypoint@@QAAXAAVTransform@@@Z` |

### `src/system/flow/DrivenPropertyMathOps.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: float __cdecl FlowMathOp::Apply(float)` | `?Apply@FlowMathOp@@QAAMM@Z` |

### `src/system/flow/Flow.cpp` (7)

| Function | Symbol |
|----------|--------|
| `public: __cdecl FlowPtr<class Hmx::Object>::FlowPtr<class Hmx::Object>(class FlowPtr<class Hmx::Object> const &)` | `??0?$FlowPtr@VObject@Hmx@@@@QAA@ABV0@@Z` |
| `public: __cdecl FlowPtr<class Hmx::Object>::FlowPtr<class Hmx::Object>(class Hmx::Object *, class Hmx::Object *)` | `??0?$FlowPtr@VObject@Hmx@@@@QAA@PAVObject@Hmx@@0@Z` |
| `public: __cdecl FlowTrigger::PropTriggerDefn::PropTriggerDefn(class Hmx::Object *)` | `??0PropTriggerDefn@FlowTrigger@@QAA@PAVObject@Hmx@@@Z` |
| `public: virtual void __cdecl Flow::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` | `?Copy@Flow@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z` |
| `public: virtual void __cdecl Flow::Enter(void)` | `?Enter@Flow@@UAAXXZ` |
| `public: virtual void __cdecl Flow::Exit(void)` | `?Exit@Flow@@UAAXXZ` |
| `void __cdecl ScanForOutPorts(class ObjPtrVec<class FlowOutPort, class ObjectDir> &, class FlowNode *, class Flow *)` | `?ScanForOutPorts@@YAXAAV?$ObjPtrVec@VFlowOutPort@@VObjectDir@@@@PAVFlowNode@@PAVFlow@@@Z` |

### `src/system/flow/FlowAnimate.cpp` (4)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl FlowAnimate::OnAnimEvent(class Symbol)` | `?OnAnimEvent@FlowAnimate@@IAAXVSymbol@@@Z` |
| `public: virtual bool __cdecl FlowAnimate::Activate(void)` | `?Activate@FlowAnimate@@UAA_NXZ` |
| `public: virtual void __cdecl FlowAnimate::ChildFinished(class FlowNode *)` | `?ChildFinished@FlowAnimate@@UAAXPAVFlowNode@@@Z` |
| `public: virtual void __cdecl FlowAnimate::Execute(enum FlowNode::QueueState)` | `?Execute@FlowAnimate@@UAAXW4QueueState@FlowNode@@@Z` |

### `src/system/flow/FlowDistance.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl FlowDistance::Execute(enum FlowNode::QueueState)` | `?Execute@FlowDistance@@UAAXW4QueueState@FlowNode@@@Z` |

### `src/system/flow/FlowEventListener.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl FlowEventListener::Load(class BinStream &)` | `?Load@FlowEventListener@@UAAXAAVBinStream@@@Z` |

### `src/system/flow/FlowManager.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: __cdecl DataArrayPtr::DataArrayPtr(class DataNode const &, class DataNode const &, class DataNode const &, class DataNode const &, class DataNode const &)` | `??0DataArrayPtr@@QAA@ABVDataNode@@0000@Z` |
| `public: bool __cdecl ObjPtrVec<class FlowNode, class ObjectDir>::remove(class FlowNode *)` | `?remove@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA_NPAVFlowNode@@@Z` |

### `src/system/flow/FlowMultiSetProperty.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl ObjPtrVec<class Hmx::Object, class ObjectDir>::unique(void)` | `?unique@?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@QAAXXZ` |

### `src/system/flow/FlowNode.cpp` (5)

| Function | Symbol |
|----------|--------|
| `private: class Hmx::Object * __cdecl FlowPtr<class Hmx::Object>::Get(void)` | `?Get@?$FlowPtr@VObject@Hmx@@@@AAAPAVObject@Hmx@@XZ` |
| `protected: void __cdecl FlowNode::PushDrivenProperties(void)` | `?PushDrivenProperties@FlowNode@@IAAXXZ` |
| `public: static class FlowNode * __cdecl FlowNode::DuplicateChild(class FlowNode *)` | `?DuplicateChild@FlowNode@@SAPAV1@PAV1@@Z` |
| `public: static class Hmx::Object * __cdecl FlowNode::LoadObjectFromMainOrDir(class BinStream &, class ObjectDir *)` | `?LoadObjectFromMainOrDir@FlowNode@@SAPAVObject@Hmx@@AAVBinStream@@PAVObjectDir@@@Z` |
| `public: virtual void __cdecl FlowNode::MoveIntoDir(class ObjectDir *, class ObjectDir *)` | `?MoveIntoDir@FlowNode@@UAAXPAVObjectDir@@0@Z` |

### `src/system/flow/FlowPickOne.cpp` (2)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl FlowPickOne::OnChoiceTypeChanged(void)` | `?OnChoiceTypeChanged@FlowPickOne@@IAAXXZ` |
| `public: virtual bool __cdecl FlowPickOne::Activate(void)` | `?Activate@FlowPickOne@@UAA_NXZ` |

### `src/system/flow/FlowQueueable.cpp` (3)

| Function | Symbol |
|----------|--------|
| `public: virtual bool __cdecl FlowQueueable::Activate(class Hmx::Object *)` | `?Activate@FlowQueueable@@UAA_NPAVObject@Hmx@@@Z` |
| `public: virtual void __cdecl FlowQueueable::ChildFinished(class FlowNode *)` | `?ChildFinished@FlowQueueable@@UAAXPAVFlowNode@@@Z` |
| `public: virtual void __cdecl FlowQueueable::Deactivate(bool)` | `?Deactivate@FlowQueueable@@UAAX_N@Z` |

### `src/system/flow/FlowRun.cpp` (3)

| Function | Symbol |
|----------|--------|
| `private: class ObjectDir * __cdecl FlowPtr<class ObjectDir>::Get(void)` | `?Get@?$FlowPtr@VObjectDir@@@@AAAPAVObjectDir@@XZ` |
| `public: virtual bool __cdecl FlowRun::Activate(void)` | `?Activate@FlowRun@@UAA_NXZ` |
| `public: void __cdecl FlowRun::ResolveTarget(void)` | `?ResolveTarget@FlowRun@@QAAXXZ` |

### `src/system/flow/FlowSequence.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual bool __cdecl FlowSequence::Activate(void)` | `?Activate@FlowSequence@@UAA_NXZ` |

### `src/system/flow/FlowSetProperty.cpp` (38)

| Function | Symbol |
|----------|--------|
| `float __cdecl EaseBackIn(float, float, float)` | `?EaseBackIn@@YAMMMM@Z` |
| `float __cdecl EaseBackInOut(float, float, float)` | `?EaseBackInOut@@YAMMMM@Z` |
| `float __cdecl EaseBackOut(float, float, float)` | `?EaseBackOut@@YAMMMM@Z` |
| `float __cdecl EaseBackOutIn(float, float, float)` | `?EaseBackOutIn@@YAMMMM@Z` |
| `float __cdecl EaseBounceIn(float, float, float)` | `?EaseBounceIn@@YAMMMM@Z` |
| `float __cdecl EaseBounceInOut(float, float, float)` | `?EaseBounceInOut@@YAMMMM@Z` |
| `float __cdecl EaseBounceOutIn(float, float, float)` | `?EaseBounceOutIn@@YAMMMM@Z` |
| `float __cdecl EaseCircIn(float, float, float)` | `?EaseCircIn@@YAMMMM@Z` |
| `float __cdecl EaseCircInOut(float, float, float)` | `?EaseCircInOut@@YAMMMM@Z` |
| `float __cdecl EaseCircOut(float, float, float)` | `?EaseCircOut@@YAMMMM@Z` |
| `float __cdecl EaseCircOutIn(float, float, float)` | `?EaseCircOutIn@@YAMMMM@Z` |
| `float __cdecl EaseElasticIn(float, float, float)` | `?EaseElasticIn@@YAMMMM@Z` |
| `float __cdecl EaseElasticInOut(float, float, float)` | `?EaseElasticInOut@@YAMMMM@Z` |
| `float __cdecl EaseElasticOut(float, float, float)` | `?EaseElasticOut@@YAMMMM@Z` |
| `float __cdecl EaseElasticOutIn(float, float, float)` | `?EaseElasticOutIn@@YAMMMM@Z` |
| `float __cdecl EaseExpoIn(float, float, float)` | `?EaseExpoIn@@YAMMMM@Z` |
| `float __cdecl EaseExpoInOut(float, float, float)` | `?EaseExpoInOut@@YAMMMM@Z` |
| `float __cdecl EaseExpoOut(float, float, float)` | `?EaseExpoOut@@YAMMMM@Z` |
| `float __cdecl EaseExpoOutIn(float, float, float)` | `?EaseExpoOutIn@@YAMMMM@Z` |
| `float __cdecl EaseHalfQuarterStairstep(float, float, float)` | `?EaseHalfQuarterStairstep@@YAMMMM@Z` |
| `float __cdecl EasePolyIn(float, float, float)` | `?EasePolyIn@@YAMMMM@Z` |
| `float __cdecl EasePolyInOut(float, float, float)` | `?EasePolyInOut@@YAMMMM@Z` |
| `float __cdecl EasePolyOut(float, float, float)` | `?EasePolyOut@@YAMMMM@Z` |
| `float __cdecl EasePolyOutIn(float, float, float)` | `?EasePolyOutIn@@YAMMMM@Z` |
| `float __cdecl EaseQuarterHalfStairstep(float, float, float)` | `?EaseQuarterHalfStairstep@@YAMMMM@Z` |
| `float __cdecl EaseQuarterStairstep(float, float, float)` | `?EaseQuarterStairstep@@YAMMMM@Z` |
| `float __cdecl EaseSineIn(float, float, float)` | `?EaseSineIn@@YAMMMM@Z` |
| `float __cdecl EaseSineInOut(float, float, float)` | `?EaseSineInOut@@YAMMMM@Z` |
| `float __cdecl EaseSineOut(float, float, float)` | `?EaseSineOut@@YAMMMM@Z` |
| `float __cdecl EaseSineOutIn(float, float, float)` | `?EaseSineOutIn@@YAMMMM@Z` |
| `float __cdecl EaseStairstep(float, float, float)` | `?EaseStairstep@@YAMMMM@Z` |
| `float __cdecl EaseThirdStairstep(float, float, float)` | `?EaseThirdStairstep@@YAMMMM@Z` |
| `protected: void __cdecl PropertyTask::SetProperty(class DataNode &)` | `?SetProperty@PropertyTask@@IAAXAAVDataNode@@@Z` |
| `public: __cdecl StackString<32>::StackString<32>(void)` | `??0?$StackString@$0CA@@@QAA@XZ` |
| `public: virtual __cdecl PropertyTask::~PropertyTask(void)` | `??1PropertyTask@@UAA@XZ` |
| `public: virtual bool __cdecl PropertyTask::Replace(class ObjRef *, class Hmx::Object *)` | `?Replace@PropertyTask@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z` |
| `public: virtual void __cdecl ObjOwnerPtr<class RndParticleSysAnim>::Replace(class Hmx::Object *)` | `?Replace@?$ObjOwnerPtr@VRndParticleSysAnim@@@@UAAXPAVObject@Hmx@@@Z` |
| `public: virtual void __cdecl PropertyTask::Poll(float)` | `?Poll@PropertyTask@@UAAXM@Z` |

### `src/system/flow/FlowSlider.cpp` (4)

| Function | Symbol |
|----------|--------|
| `bool __cdecl SliderChildSort(class FlowNode *, class FlowNode *)` | `?SliderChildSort@@YA_NPAVFlowNode@@0@Z` |
| `protected: void __cdecl FlowSlider::UpdateActivations(void)` | `?UpdateActivations@FlowSlider@@IAAXXZ` |
| `public: virtual void __cdecl FlowDistance::RequestStop(void)` | `?RequestStop@FlowDistance@@UAAXXZ` |
| `public: virtual void __cdecl FlowDistance::RequestStopCancel(void)` | `?RequestStopCancel@FlowDistance@@UAAXXZ` |

### `src/system/flow/FlowSound.cpp` (1)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl FlowSound::OnMarkerEvent(class Symbol)` | `?OnMarkerEvent@FlowSound@@IAAXVSymbol@@@Z` |

### `src/system/flow/FlowSwitch.cpp` (2)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl FlowSwitch::ActivateValueCases(class DataNode &, class DataNode &)` | `?ActivateValueCases@FlowSwitch@@IAAXAAVDataNode@@0@Z` |
| `protected: void __cdecl FlowSwitch::VerifyTypes(void)` | `?VerifyTypes@FlowSwitch@@IAAXXZ` |

### `src/system/flow/FlowSwitchCase.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: bool __cdecl FlowSwitchCase::IsValidCase(class FlowNode *, class DataNode *, class DataNode const *, bool)` | `?IsValidCase@FlowSwitchCase@@QAA_NPAVFlowNode@@PAVDataNode@@PBV3@_N@Z` |

### `src/system/flow/FlowTimer.cpp` (4)

| Function | Symbol |
|----------|--------|
| `public: static class Symbol __cdecl EventTask::StaticClassName(void)` | `?StaticClassName@EventTask@@SA?AVSymbol@@XZ` |
| `public: static void __cdecl Task::operator delete(void *)` | `??3Task@@SAXPAX@Z` |
| `public: virtual class Symbol __cdecl EventTask::ClassName(void) const` | `?ClassName@EventTask@@UBA?AVSymbol@@XZ` |
| `public: virtual void __cdecl EventTask::Poll(float)` | `?Poll@EventTask@@UAAXM@Z` |

### `src/system/flow/FlowWhile.cpp` (1)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl FlowWhile::ReActivate(void)` | `?ReActivate@FlowWhile@@IAAXXZ` |

### `src/system/gesture/HandRaisedGestureFilter.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl HandRaisedGestureFilter::Update(class Skeleton const &, int)` | `?Update@HandRaisedGestureFilter@@QAAXABVSkeleton@@H@Z` |

### `src/system/gesture/StandingStillGestureFilter.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl StandingStillGestureFilter::Update(class Skeleton const &, int)` | `?Update@StandingStillGestureFilter@@QAAXABVSkeleton@@H@Z` |

### `src/system/gesture/StubCameraInput.cpp` (1)

| Function | Symbol |
|----------|--------|
| `protected: virtual struct SkeletonFrame const * __cdecl StubCameraInput::PollNewFrame(void)` | `?PollNewFrame@StubCameraInput@@MAAPBUSkeletonFrame@@XZ` |

### `src/system/hamobj/CharCameraInput.cpp` (2)

| Function | Symbol |
|----------|--------|
| `protected: virtual struct SkeletonFrame const * __cdecl CharCameraInput::PollNewFrame(void)` | `?PollNewFrame@CharCameraInput@@MAAPBUSkeletonFrame@@XZ` |
| `public: void __cdecl CharCameraInput::ResetSkeletonCharOrigin(void)` | `?ResetSkeletonCharOrigin@CharCameraInput@@QAAXXZ` |

### `src/system/hamobj/CharFeedback.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl CharFeedback::DrawShowing(void)` | `?DrawShowing@CharFeedback@@UAAXXZ` |
| `public: virtual void __cdecl CharFeedback::Poll(void)` | `?Poll@CharFeedback@@UAAXXZ` |

### `src/system/hamobj/ClipPlayer.cpp` (7)

| Function | Symbol |
|----------|--------|
| `float __cdecl `anonymous namespace'::ClipStart(class CharClip *, float, float &, float &)` | `?ClipStart@?A0x8e038fa7@@YAMPAVCharClip@@MAAM1@Z` |
| `protected: bool __cdecl ClipPlayer::GetClipRange(char const *, char const *, float, float &, float &, float &)` | `?GetClipRange@ClipPlayer@@IAA_NPBD0MAAM11@Z` |
| `protected: bool __cdecl ClipPlayer::PushRoutineBuilderClip(int, struct HamDriver::LayerArray *)` | `?PushRoutineBuilderClip@ClipPlayer@@IAA_NHPAULayerArray@HamDriver@@@Z` |
| `protected: class CharClip * __cdecl ClipPlayer::GetPrevRoutineTransition(int)` | `?GetPrevRoutineTransition@ClipPlayer@@IAAPAVCharClip@@H@Z` |
| `protected: float __cdecl ClipPlayer::ClipLength(class CharClip *)` | `?ClipLength@ClipPlayer@@IAAMPAVCharClip@@@Z` |
| `protected: void __cdecl ClipPlayer::PushClip(int, struct HamDriver::LayerArray *)` | `?PushClip@ClipPlayer@@IAAXHPAULayerArray@HamDriver@@@Z` |
| `public: class DataNode __cdecl ClipPlayer::AnnotateClip(float)` | `?AnnotateClip@ClipPlayer@@QAA?AVDataNode@@M@Z` |

### `src/system/hamobj/DanceRemixer.cpp` (8)

| Function | Symbol |
|----------|--------|
| `public: __cdecl MetagameStats::FavoriteStat::FavoriteStat(void)` | `??0FavoriteStat@MetagameStats@@QAA@XZ` |
| `public: class MoveVariant const * __cdecl DanceRemixer::MoveVariantFromHamMove(class HamMove const *) const` | `?MoveVariantFromHamMove@DanceRemixer@@QBAPBVMoveVariant@@PBVHamMove@@@Z` |
| `public: float __cdecl DanceRemixer::JumpedBeat(float) const` | `?JumpedBeat@DanceRemixer@@QBAMM@Z` |
| `public: int __cdecl DanceRemixer::JumpedMeasureAdd(int, int) const` | `?JumpedMeasureAdd@DanceRemixer@@QBAHHH@Z` |
| `public: int __cdecl DanceRemixer::JumpedMeasureStepsBetween(int, int, int) const` | `?JumpedMeasureStepsBetween@DanceRemixer@@QBAHHHH@Z` |
| `public: int __cdecl DanceRemixer::JumpedMoveIdxAdd(int, int) const` | `?JumpedMoveIdxAdd@DanceRemixer@@QBAHHH@Z` |
| `public: void __cdecl DanceRemixer::ClearJump(void)` | `?ClearJump@DanceRemixer@@QAAXXZ` |
| `public: void __cdecl DanceRemixer::SetJump(int, int)` | `?SetJump@DanceRemixer@@QAAXHH@Z` |

### `src/system/hamobj/ErrorNode.cpp` (5)

| Function | Symbol |
|----------|--------|
| `float __cdecl ScaleDistToError(struct ScaleOp const &, float)` | `?ScaleDistToError@@YAMABUScaleOp@@M@Z` |
| `float __cdecl ScaleFullErrorDist(struct ScaleOp const &)` | `?ScaleFullErrorDist@@YAMABUScaleOp@@@Z` |
| `private: void __cdecl Ham1DisplacementNode::Errors(struct ErrorFrameInput const &, struct ErrorNodeInput const &, struct Ham1DisplacementNode::ErrorData &, struct BaseDisplacementNode::DisplacementData &, struct BaseDisplacementNode::Ham1DisplacementData &) const` | `?Errors@Ham1DisplacementNode@@ABAXABUErrorFrameInput@@ABUErrorNodeInput@@AAUErrorData@1@AAUDisplacementData@BaseDisplacementNode@@AAUHam1DisplacementData@6@@Z` |
| `protected: bool __cdecl BaseDisplacementNode::Displacements(struct ErrorFrameInput const &, struct BaseDisplacementNode::DisplacementData &, struct BaseDisplacementNode::Ham1DisplacementData &) const` | `?Displacements@BaseDisplacementNode@@IBA_NABUErrorFrameInput@@AAUDisplacementData@1@AAUHam1DisplacementData@1@@Z` |
| `void __cdecl XZErrorWeight(class Vector3const &, float &, float &)` | `?XZErrorWeight@@YAXABVVector3@@AAM1@Z` |

### `src/system/hamobj/FilterQueue.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: class RndText::Line * __cdecl stlpmtx_std::vector<class RndText::Line, class stlpmtx_std::StlNodeAlloc<class RndText::Line> >::erase(class RndText::Line *, class RndText::Line *)` | `?erase@?$vector@VLine@RndText@@V?$StlNodeAlloc@VLine@RndText@@@stlpmtx_std@@@stlpmtx_std@@QAAPAVLine@RndText@@PAV34@0@Z` |
| `public: void __cdecl FilterQueue::Poll(struct SkeletonUpdateData const &)` | `?Poll@FilterQueue@@QAAXABUSkeletonUpdateData@@@Z` |

### `src/system/hamobj/FreestyleMove.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl FreestyleMove::CalcCentering(int)` | `?CalcCentering@FreestyleMove@@QAAXH@Z` |

### `src/system/hamobj/FreestyleMoveRecorder.cpp` (10)

| Function | Symbol |
|----------|--------|
| `private: float __cdecl FreestyleMoveRecorder::CompareSkeletonJointDisplacement(struct FreestyleMoveFrame const *, int, class BaseSkeleton const *, float &) const` | `?CompareSkeletonJointDisplacement@FreestyleMoveRecorder@@ABAMPBUFreestyleMoveFrame@@HPBVBaseSkeleton@@AAM@Z` |
| `private: void __cdecl FreestyleMoveRecorder::CompareDisplacementVectors(class Vector3const &, int, class Vector3const &, int, float &, float &) const` | `?CompareDisplacementVectors@FreestyleMoveRecorder@@ABAXABVVector3@@H0HAAM1@Z` |
| `private: void __cdecl FreestyleMoveRecorder::UpdateFakeSkeleton(void)` | `?UpdateFakeSkeleton@FreestyleMoveRecorder@@AAAXXZ` |
| `public: class BaseSkeleton * __cdecl FreestyleMoveRecorder::GetLiveSkeleton(void)` | `?GetLiveSkeleton@FreestyleMoveRecorder@@QAAPAVBaseSkeleton@@XZ` |
| `public: float __cdecl FreestyleMoveRecorder::CompareSkeletonPositions(class BaseSkeleton const *, class BaseSkeleton const *, float) const` | `?CompareSkeletonPositions@FreestyleMoveRecorder@@QBAMPBVBaseSkeleton@@0M@Z` |
| `public: float __cdecl FreestyleMoveRecorder::GetScore(class BaseSkeleton const *, int, float, bool)` | `?GetScore@FreestyleMoveRecorder@@QAAMPBVBaseSkeleton@@HM_N@Z` |
| `public: void __cdecl FreestyleMoveRecorder::CalcFrameScore(struct FreestyleFrameScores &, struct FreestyleMoveFrame const *, int, class BaseSkeleton const *, float) const` | `?CalcFrameScore@FreestyleMoveRecorder@@QBAXAAUFreestyleFrameScores@@PBUFreestyleMoveFrame@@HPBVBaseSkeleton@@M@Z` |
| `public: void __cdecl FreestyleMoveRecorder::DrawDebug(void)` | `?DrawDebug@FreestyleMoveRecorder@@QAAXXZ` |
| `public: void __cdecl FreestyleMoveRecorder::Poll(void)` | `?Poll@FreestyleMoveRecorder@@QAAXXZ` |
| `public: void __cdecl FreestyleMoveRecorder::StopRecording(void)` | `?StopRecording@FreestyleMoveRecorder@@QAAXXZ` |

### `src/system/hamobj/HamAudio.cpp` (2)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl HamAudio::PollCrossfade(void)` | `?PollCrossfade@HamAudio@@AAAXXZ` |
| `public: void __cdecl HamAudio::FinishLoad(void)` | `?FinishLoad@HamAudio@@QAAXXZ` |

### `src/system/hamobj/HamCamShot.cpp` (10)

| Function | Symbol |
|----------|--------|
| `private: class DataNode __cdecl HamCamShot::OnAllowableNextShots(class DataArray const *)` | `?OnAllowableNextShots@HamCamShot@@AAA?AVDataNode@@PBVDataArray@@@Z` |
| `private: void __cdecl ObjDirItr<class HamCamShot>::Advance(void)` | `?Advance@?$ObjDirItr@VHamCamShot@@@@AAAXXZ` |
| `protected: void __cdecl HamCamShot::CreateFlippedShowHideList(void)` | `?CreateFlippedShowHideList@HamCamShot@@IAAXXZ` |
| `protected: void __cdecl HamCamShot::UpdateTargetsFlipped(void)` | `?UpdateTargetsFlipped@HamCamShot@@IAAXXZ` |
| `public: __cdecl ObjDirItr<class HamCamShot>::ObjDirItr<class HamCamShot>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VHamCamShot@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: class ObjDirItr<class HamCamShot> & __cdecl ObjDirItr<class HamCamShot>::operator++(void)` | `??E?$ObjDirItr@VHamCamShot@@@@QAAAAV0@XZ` |
| `public: virtual void __cdecl HamCamShot::EndAnim(void)` | `?EndAnim@HamCamShot@@UAAXXZ` |
| `public: virtual void __cdecl HamCamShot::Load(class BinStream &)` | `?Load@HamCamShot@@UAAXAAVBinStream@@@Z` |
| `public: virtual void __cdecl HamCamShot::SetPreFrame(float, float)` | `?SetPreFrame@HamCamShot@@UAAXMM@Z` |
| `public: void __cdecl HamCamShot::Reteleport(class Vector3const &, bool, class Symbol)` | `?Reteleport@HamCamShot@@QAAXABVVector3@@_NVSymbol@@@Z` |

### `src/system/hamobj/HamCamTransform.cpp` (5)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl HamCamTransform::Setup(bool)` | `?Setup@HamCamTransform@@IAAX_N@Z` |
| `public: __cdecl CamShotCrowd::~CamShotCrowd(void)` | `??1CamShotCrowd@@QAA@XZ` |
| `public: virtual void __cdecl HamCamTransform::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` | `?Copy@HamCamTransform@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z` |
| `public: virtual void __cdecl HamCamTransform::Load(class BinStream &)` | `?Load@HamCamTransform@@UAAXAAVBinStream@@@Z` |
| `public: void __cdecl HamCamTransform::ClearOldCrowds(void)` | `?ClearOldCrowds@HamCamTransform@@QAAXXZ` |

### `src/system/hamobj/HamCharacter.cpp` (8)

| Function | Symbol |
|----------|--------|
| `protected: class DataNode __cdecl HamCharacter::OnSoundPlay(class DataArray const *)` | `?OnSoundPlay@HamCharacter@@IAA?AVDataNode@@PBVDataArray@@@Z` |
| `protected: void __cdecl HamCharacter::ApplyBlendedSkeletons(class HamDriver *, class CharClip *, float)` | `?ApplyBlendedSkeletons@HamCharacter@@IAAXPAVHamDriver@@PAVCharClip@@M@Z` |
| `public: __cdecl QuatXfm::QuatXfm(class Transform const &)` | `??0QuatXfm@@QAA@ABVTransform@@@Z` |
| `public: __cdecl StackString<128>::StackString<128>(char const *)` | `??0?$StackString@$0IA@@@QAA@PBD@Z` |
| `public: class ObjectDir * __cdecl HamCharacter::GetNeutralSkeleton(void)` | `?GetNeutralSkeleton@HamCharacter@@QAAPAVObjectDir@@XZ` |
| `public: virtual void __cdecl HamCharacter::Poll(void)` | `?Poll@HamCharacter@@UAAXXZ` |
| `public: void __cdecl HamCharacter::BlendInFaceOverrideClip(class Symbol, float, float)` | `?BlendInFaceOverrideClip@HamCharacter@@QAAXVSymbol@@MM@Z` |
| `public: void __cdecl HamCharacter::SetFaceOverrideClip(class Symbol, bool)` | `?SetFaceOverrideClip@HamCharacter@@QAAXVSymbol@@_N@Z` |

### `src/system/hamobj/HamDirector.cpp` (14)

| Function | Symbol |
|----------|--------|
| `protected: bool __cdecl HamDirector::AreCharactersColliding(void)` | `?AreCharactersColliding@HamDirector@@IAA_NXZ` |
| `protected: class CharClip * __cdecl HamDirector::GetClipStartAndEndBeats(class Symbol, float &, float &, struct stlpmtx_std::pair<float, float> *)` | `?GetClipStartAndEndBeats@HamDirector@@IAAPAVCharClip@@VSymbol@@AAM1PAU?$pair@MM@stlpmtx_std@@@Z` |
| `protected: class DataNode __cdecl HamDirector::OnSelectCamera(class DataArray *)` | `?OnSelectCamera@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `protected: void __cdecl HamDirector::ChangeNextShotIfCharacterCollisionLikely(void)` | `?ChangeNextShotIfCharacterCollisionLikely@HamDirector@@IAAXXZ` |
| `protected: void __cdecl HamDirector::OnPopulateFromMoveMgr(void)` | `?OnPopulateFromMoveMgr@HamDirector@@IAAXXZ` |
| `protected: void __cdecl HamDirector::OnPopulateMoves(void)` | `?OnPopulateMoves@HamDirector@@IAAXXZ` |
| `protected: void __cdecl HamDirector::PlayNextShot(void)` | `?PlayNextShot@HamDirector@@IAAXXZ` |
| `public: __cdecl DancerFrame::~DancerFrame(void)` | `??1DancerFrame@@QAA@XZ` |
| `public: __cdecl DancerSkeleton::DancerSkeleton(class DancerSkeleton const &)` | `??0DancerSkeleton@@QAA@ABV0@@Z` |
| `public: int __cdecl Keys<class Vector2, class Vector2>::KeyLessEq(float) const` | `?KeyLessEq@?$Keys@VVector2@@V1@@@QBAHM@Z` |
| `public: virtual __cdecl ArchiveSkeleton::~ArchiveSkeleton(void)` | `??1ArchiveSkeleton@@UAA@XZ` |
| `public: void __cdecl HamDirector::DrawIconMan(class Symbol, class Symbol, class Symbol, float, float, class RndTex *)` | `?DrawIconMan@HamDirector@@QAAXVSymbol@@00MMPAVRndTex@@@Z` |
| `public: void __cdecl HamDirector::DrawIconMan(enum Difficulty, float, float, float, float, class RndTex *)` | `?DrawIconMan@HamDirector@@QAAXW4Difficulty@@MMMMPAVRndTex@@@Z` |
| `public: void __cdecl HamDirector::RemapSongAnimToTempoMap(class TempoMap *)` | `?RemapSongAnimToTempoMap@HamDirector@@QAAXPAVTempoMap@@@Z` |

### `src/system/hamobj/HamDriver.cpp` (5)

| Function | Symbol |
|----------|--------|
| `protected: float __cdecl HamDriver::DisplayRecurse(struct HamDriver::Layer *, int, float)` | `?DisplayRecurse@HamDriver@@IAAMPAULayer@1@HM@Z` |
| `protected: void __cdecl HamDriver::SetClipMapRecurse(struct HamDriver::Layer *)` | `?SetClipMapRecurse@HamDriver@@IAAXPAULayer@1@@Z` |
| `public: virtual void __cdecl HamDriver::LayerArray::Eval(float)` | `?Eval@LayerArray@HamDriver@@UAAXM@Z` |
| `public: virtual void __cdecl HamDriver::Poll(void)` | `?Poll@HamDriver@@UAAXXZ` |
| `public: void __cdecl HamDriver::SetClipWeightMap(void)` | `?SetClipWeightMap@HamDriver@@QAAXXZ` |

### `src/system/hamobj/HamIKEffector.cpp` (3)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl HamIKEffector::ComputeHandPullAndQuat(class QuatXfm &, class Transform &, class Transform const &, class Vector3const &)` | `?ComputeHandPullAndQuat@HamIKEffector@@IAAXAAVQuatXfm@@AAVTransform@@ABV3@ABVVector3@@@Z` |
| `protected: void __cdecl HamIKEffector::DoFancyElbow(class QuatXfm &, float)` | `?DoFancyElbow@HamIKEffector@@IAAXAAVQuatXfm@@M@Z` |
| `public: virtual void __cdecl HamIKEffector::Poll(void)` | `?Poll@HamIKEffector@@UAAXXZ` |

### `src/system/hamobj/HamListRibbon.cpp` (3)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl HamListRibbon::DrawRibbon(int, class Transform const &, class Transform const &, struct HamListRibbonDrawState const &, int, int, int, bool)` | `?DrawRibbon@HamListRibbon@@AAAXHABVTransform@@0ABUHamListRibbonDrawState@@HHH_N@Z` |
| `public: virtual float __cdecl HamListRibbon::EndFrame(void)` | `?EndFrame@HamListRibbon@@UAAMXZ` |
| `public: void __cdecl HamListRibbon::Draw(class Transform const &, class stlpmtx_std::vector<struct HamListRibbonDrawState, class stlpmtx_std::StlNodeAlloc<struct HamListRibbonDrawState> > const &, bool, bool)` | `?Draw@HamListRibbon@@QAAXABVTransform@@ABV?$vector@UHamListRibbonDrawState@@V?$StlNodeAlloc@UHamListRibbonDrawState@@@stlpmtx_std@@@stlpmtx_std@@_N2@Z` |

### `src/system/hamobj/HamNavList.cpp` (7)

| Function | Symbol |
|----------|--------|
| `private: class DataNode __cdecl HamNavList::OnMsg(class ButtonDownMsg const &)` | `?OnMsg@HamNavList@@AAA?AVDataNode@@ABVButtonDownMsg@@@Z` |
| `public: char const * __cdecl ResourceDirPtr<class UILabelDir>::GetName(void) const` | `?GetName@?$ResourceDirPtr@VUILabelDir@@@@QBAPBDXZ` |
| `public: static class Symbol __cdecl LeftHandListEngagementMsg::Type(void)` | `?Type@LeftHandListEngagementMsg@@SA?AVSymbol@@XZ` |
| `public: virtual void __cdecl HamNavList::Clear(void)` | `?Clear@HamNavList@@UAAXXZ` |
| `public: virtual void __cdecl WorldInstance::Load(class BinStream &)` | `?Load@WorldInstance@@UAAXAAVBinStream@@@Z` |
| `public: void __cdecl HamNavList::ClearBigElements(void)` | `?ClearBigElements@HamNavList@@QAAXXZ` |
| `public: void __cdecl HamNavList::UpdateGestures(class Skeleton const *)` | `?UpdateGestures@HamNavList@@QAAXPBVSkeleton@@@Z` |

### `src/system/hamobj/HamNavProvider.cpp` (1)

| Function | Symbol |
|----------|--------|
| `protected: class DataNode __cdecl HamNavProvider::OnSetFormatArgs(class DataArray const *)` | `?OnSetFormatArgs@HamNavProvider@@IAA?AVDataNode@@PBVDataArray@@@Z` |

### `src/system/hamobj/HamRegulate.cpp` (2)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl HamRegulate::Regulate(class Vector3&, float &)` | `?Regulate@HamRegulate@@IAAXAAVVector3@@AAM@Z` |
| `public: virtual void __cdecl HamRegulate::Poll(void)` | `?Poll@HamRegulate@@UAAXXZ` |

### `src/system/hamobj/HamRibbon.cpp` (5)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl HamRibbon::UpdateChase(void)` | `?UpdateChase@HamRibbon@@QAAXXZ` |
| `public: void __cdecl HamRibbon::UpdateMesh(void)` | `?UpdateMesh@HamRibbon@@QAAXXZ` |
| `public: void __cdecl stlpmtx_std::vector<class Key<class Transform>, class stlpmtx_std::StlNodeAlloc<class Key<class Transform> > >::push_back(class Key<class Transform> const &)` | `?push_back@?$vector@V?$Key@VTransform@@@@V?$StlNodeAlloc@V?$Key@VTransform@@@@@stlpmtx_std@@@stlpmtx_std@@QAAXABV?$Key@VTransform@@@@@Z` |
| `public: void __cdecl stlpmtx_std::vector<class Key<class Transform>, class stlpmtx_std::StlNodeAlloc<class Key<class Transform> > >::resize(unsigned int, class Key<class Transform> const &)` | `?resize@?$vector@V?$Key@VTransform@@@@V?$StlNodeAlloc@V?$Key@VTransform@@@@@stlpmtx_std@@@stlpmtx_std@@QAAXIABV?$Key@VTransform@@@@@Z` |
| `void __cdecl Multiply(class Transform const &, class Hmx::Matrix3const &, class Transform &)` | `?Multiply@@YAXABVTransform@@ABVMatrix3@Hmx@@AAV1@@Z` |

### `src/system/hamobj/HamScrollSpeedIndicator.cpp` (3)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl HamListRibbon::SetFrame(float, float)` | `?SetFrame@HamListRibbon@@UAAXMM@Z` |
| `public: virtual void __cdecl HamListRibbon::SyncObjects(void)` | `?SyncObjects@HamListRibbon@@UAAXXZ` |
| `public: void __cdecl HamScrollSpeedIndicator::Update(float, float, float)` | `?Update@HamScrollSpeedIndicator@@QAAXMMM@Z` |

### `src/system/hamobj/HamSkeletonConverter.cpp` (6)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl HamSkeletonConverter::CalcQuatBone(enum SkeletonJoint, enum SkeletonJoint, enum SkeletonJoint)` | `?CalcQuatBone@HamSkeletonConverter@@IAAXW4SkeletonJoint@@00@Z` |
| `protected: void __cdecl HamSkeletonConverter::CalcRotzBone(enum SkeletonJoint, enum SkeletonJoint, enum SkeletonJoint)` | `?CalcRotzBone@HamSkeletonConverter@@IAAXW4SkeletonJoint@@00@Z` |
| `protected: void __cdecl HamSkeletonConverter::ScaleBone(enum SkeletonJoint, enum SkeletonJoint, enum SkeletonCoordSys, class Vector3const &, class Vector3const &, class Vector3const &, class Vector3&)` | `?ScaleBone@HamSkeletonConverter@@IAAXW4SkeletonJoint@@0W4SkeletonCoordSys@@ABVVector3@@22AAV4@@Z` |
| `protected: void __cdecl HamSkeletonConverter::SetArm(enum SkeletonJoint, enum SkeletonJoint, enum SkeletonJoint, enum SkeletonJoint)` | `?SetArm@HamSkeletonConverter@@IAAXW4SkeletonJoint@@000@Z` |
| `protected: void __cdecl HamSkeletonConverter::SetLeg(enum SkeletonJoint, enum SkeletonJoint, enum SkeletonJoint, enum SkeletonJoint, enum SkeletonJoint, class BaseSkeleton const *, int)` | `?SetLeg@HamSkeletonConverter@@IAAXW4SkeletonJoint@@0000PBVBaseSkeleton@@H@Z` |
| `public: void __cdecl HamSkeletonConverter::Set(class BaseSkeleton const *)` | `?Set@HamSkeletonConverter@@QAAXPBVBaseSkeleton@@@Z` |

### `src/system/hamobj/HamVisDir.cpp` (1)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl HamVisDir::UpdateGestureFilter(class Skeleton const &, int)` | `?UpdateGestureFilter@HamVisDir@@IAAXABVSkeleton@@H@Z` |

### `src/system/hamobj/HamWardrobe.cpp` (5)

| Function | Symbol |
|----------|--------|
| `protected: class DataNode __cdecl HamWardrobe::OnAddCrowd(class DataArray *)` | `?OnAddCrowd@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `protected: class DataNode __cdecl HamWardrobe::OnLoadCharacters(class DataArray *)` | `?OnLoadCharacters@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `protected: class DataNode __cdecl HamWardrobe::OnSetVenue(class DataArray *)` | `?OnSetVenue@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `public: void __cdecl HamWardrobe::LoadCharacters(class Symbol, class Symbol, class Symbol, class Symbol, enum HamBackupDancers, class Symbol, class Symbol, bool)` | `?LoadCharacters@HamWardrobe@@QAAXVSymbol@@000W4HamBackupDancers@@00_N@Z` |
| `public: void __cdecl HamWardrobe::PlayCrowdAnimation(class Symbol, int, bool)` | `?PlayCrowdAnimation@HamWardrobe@@QAAXVSymbol@@H_N@Z` |

### `src/system/hamobj/HollaBackMinigame.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl HollaBackMinigame::OnBeat(void)` | `?OnBeat@HollaBackMinigame@@QAAXXZ` |

### `src/system/hamobj/MeterDisplay.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl MeterDisplay::DrawShowing(void)` | `?DrawShowing@MeterDisplay@@UAAXXZ` |

### `src/system/hamobj/MiniLeaderboardDisplay.cpp` (1)

| Function | Symbol |
|----------|--------|
| `protected: virtual void __cdecl LabelShrinkWrapper::OldResourcePreload(class BinStream &)` | `?OldResourcePreload@LabelShrinkWrapper@@MAAXAAVBinStream@@@Z` |

### `src/system/hamobj/MoveAsyncDetector.cpp` (4)

| Function | Symbol |
|----------|--------|
| `public: float __cdecl MoveAsyncDetector::MoveRatingFrac(int, enum MoveAsyncDetector::RatingBar, class HamMove const *)` | `?MoveRatingFrac@MoveAsyncDetector@@QAAMHW4RatingBar@1@PBVHamMove@@@Z` |
| `public: float __cdecl MoveDetector::Last4BeatsDetectFrac(int) const` | `?Last4BeatsDetectFrac@MoveDetector@@QBAMH@Z` |
| `public: void __cdecl MoveAsyncDetector::ClearLoopedRatingFrac(class HamMove const *)` | `?ClearLoopedRatingFrac@MoveAsyncDetector@@QAAXPBVHamMove@@@Z` |
| `public: void __cdecl MoveDetector::Poll(int, int, class MoveDir *)` | `?Poll@MoveDetector@@QAAXHHPAVMoveDir@@@Z` |

### `src/system/hamobj/MoveDir.cpp` (13)

| Function | Symbol |
|----------|--------|
| `float __cdecl `anonymous namespace'::DrawDetectedBar(float, char const *, float, float, float, bool, bool)` | `?DrawDetectedBar@?A0xe50ea9df@@YAMMPBDMMM_N1@Z` |
| `float __cdecl `anonymous namespace'::DrawPlayClip(float, class SkeletonClip *, int)` | `?DrawPlayClip@?A0xe50ea9df@@YAMMPAVSkeletonClip@@H@Z` |
| `private: void __cdecl MoveDir::PostUpdateFilters(void)` | `?PostUpdateFilters@MoveDir@@AAAXXZ` |
| `private: void __cdecl ObjDirItr<class SkeletonViz>::Advance(void)` | `?Advance@?$ObjDirItr@VSkeletonViz@@@@AAAXXZ` |
| `public: __cdecl ObjDirItr<class SkeletonViz>::ObjDirItr<class SkeletonViz>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VSkeletonViz@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: float __cdecl MoveDir::DetectFrac(int, int)` | `?DetectFrac@MoveDir@@QAAMHH@Z` |
| `public: virtual float __cdecl MoveDir::UpdateOverlay(class RndOverlay *, float)` | `?UpdateOverlay@MoveDir@@UAAMPAVRndOverlay@@M@Z` |
| `public: virtual void __cdecl MoveDir::DrawShowing(void)` | `?DrawShowing@MoveDir@@UAAXXZ` |
| `public: void __cdecl MoveDir::EnqueueDetectFrames(float, int, class stlpmtx_std::vector<class DetectFrame, class stlpmtx_std::StlNodeAlloc<class DetectFrame> > &, class FilterVersion const *)` | `?EnqueueDetectFrames@MoveDir@@QAAXMHAAV?$vector@VDetectFrame@@V?$StlNodeAlloc@VDetectFrame@@@stlpmtx_std@@@stlpmtx_std@@PBVFilterVersion@@@Z` |
| `public: void __cdecl MoveDir::FinalPoseStateMachine(void)` | `?FinalPoseStateMachine@MoveDir@@QAAXXZ` |
| `public: void __cdecl MoveDir::ResetDetectFrames(int, enum Difficulty)` | `?ResetDetectFrames@MoveDir@@QAAXHW4Difficulty@@@Z` |
| `public: void __cdecl stlpmtx_std::StlNodeAlloc<struct SongCollisionOutput>::deallocate(struct SongCollisionOutput *, unsigned int) const` | `?deallocate@?$StlNodeAlloc@USongCollisionOutput@@@stlpmtx_std@@QBAXPAUSongCollisionOutput@@I@Z` |
| `void __cdecl `anonymous namespace'::DrawBeatLine(float, float, float, class Hmx::Color const &)` | `?DrawBeatLine@?A0xe50ea9df@@YAXMMMABVColor@Hmx@@@Z` |

### `src/system/hamobj/MoveGraph.cpp` (1)

| Function | Symbol |
|----------|--------|
| `private: class MoveParent * __cdecl MoveGraph::GetNonConstMoveParent(class Symbol) const` | `?GetNonConstMoveParent@MoveGraph@@ABAPAVMoveParent@@VSymbol@@@Z` |

### `src/system/hamobj/MoveMgr.cpp` (5)

| Function | Symbol |
|----------|--------|
| `public: int __cdecl MoveMgr::ComputeRandomChoiceSet(int)` | `?ComputeRandomChoiceSet@MoveMgr@@QAAHH@Z` |
| `public: void __cdecl MoveMgr::ComputeLoadedMoveSet(void)` | `?ComputeLoadedMoveSet@MoveMgr@@QAAXXZ` |
| `public: void __cdecl MoveMgr::FillInRoutineAt(int, int)` | `?FillInRoutineAt@MoveMgr@@QAAXHH@Z` |
| `public: void __cdecl MoveMgr::FillRoutineFromReplacer(int)` | `?FillRoutineFromReplacer@MoveMgr@@QAAXH@Z` |
| `public: void __cdecl MoveMgr::FillRoutineFromVerses(int)` | `?FillRoutineFromVerses@MoveMgr@@QAAXH@Z` |

### `src/system/hamobj/PhotoSpotlightPositioner.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl PhotoSpotlightPositioner::Poll(void)` | `?Poll@PhotoSpotlightPositioner@@UAAXXZ` |

### `src/system/hamobj/Pose.cpp` (4)

| Function | Symbol |
|----------|--------|
| `public: float __cdecl Pose::CurrentScore(void) const` | `?CurrentScore@Pose@@QBAMXZ` |
| `public: virtual float __cdecl BoneAngleRangePoseElement::Score(class Skeleton const &) const` | `?Score@BoneAngleRangePoseElement@@UBAMABVSkeleton@@@Z` |
| `public: virtual float __cdecl CamDistancePoseElement::Score(class Skeleton const &) const` | `?Score@CamDistancePoseElement@@UBAMABVSkeleton@@@Z` |
| `public: void __cdecl Pose::Update(class Skeleton const &)` | `?Update@Pose@@QAAXABVSkeleton@@@Z` |

### `src/system/hamobj/PoseFatalities.cpp` (3)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl PoseFatalities::UpdateClipDriver(int)` | `?UpdateClipDriver@PoseFatalities@@AAAXH@Z` |
| `private: void __cdecl PoseFatalities::UpdateMatchingPose(int)` | `?UpdateMatchingPose@PoseFatalities@@AAAXH@Z` |
| `public: void __cdecl PoseFatalities::DrawDebug(void)` | `?DrawDebug@PoseFatalities@@QAAXXZ` |

### `src/system/hamobj/RhythmBattlePlayer.cpp` (2)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl RhythmBattlePlayer::AnimateBoxyState(int, bool, bool)` | `?AnimateBoxyState@RhythmBattlePlayer@@AAAXH_N0@Z` |
| `public: void __cdecl RhythmBattlePlayer::UpdateComboProgress(void)` | `?UpdateComboProgress@RhythmBattlePlayer@@QAAXXZ` |

### `src/system/hamobj/RhythmDetector.cpp` (5)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl RhythmDetector::AddFrame(class BaseSkeleton const &)` | `?AddFrame@RhythmDetector@@AAAXABVBaseSkeleton@@@Z` |
| `private: void __cdecl RhythmDetector::ProcessFrames(void)` | `?ProcessFrames@RhythmDetector@@AAAXXZ` |
| `public: struct RhythmDetector::RecordData const & __cdecl RhythmDetector::GetRecord(float, float, bool, class Symbol, class TextStream *)` | `?GetRecord@RhythmDetector@@QAAABURecordData@1@MM_NVSymbol@@PAVTextStream@@@Z` |
| `struct RhythmDetector::Frame __cdecl BlendFrameDataToBeat(struct RhythmDetector::Frame const &, struct RhythmDetector::Frame const &, float)` | `?BlendFrameDataToBeat@@YA?AUFrame@RhythmDetector@@ABU12@0M@Z` |
| `void __cdecl SetupFrame(struct RhythmDetector::Frame &, float, float, class Vector3const *, class Vector3const *, float)` | `?SetupFrame@@YAXAAUFrame@RhythmDetector@@MMPBVVector3@@1M@Z` |

### `src/system/hamobj/RhythmDetectorGroup.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl HollaBackMinigame::Enter(void)` | `?Enter@HollaBackMinigame@@UAAXXZ` |

### `src/system/hamobj/ScoreUtl.cpp` (2)

| Function | Symbol |
|----------|--------|
| `float __cdecl RatingToRatingFrac(class Symbol)` | `?RatingToRatingFrac@@YAMVSymbol@@@Z` |
| `void __cdecl ScoreUtlInit(class DataArray const *)` | `?ScoreUtlInit@@YAXPBVDataArray@@@Z` |

### `src/system/hamobj/SongCollision.cpp` (2)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl SongCollision::CheckCollision(int, enum Difficulty const *const, class Transform const *const, struct SongCollisionOutput &) const` | `?CheckCollision@SongCollision@@ABAXHQBW4Difficulty@@QBVTransform@@AAUSongCollisionOutput@@@Z` |
| `public: bool __cdecl SongCollision::Equals(class SongCollision *)` | `?Equals@SongCollision@@QAA_NPAV1@@Z` |

### `src/system/hamobj/SongLayout.cpp` (3)

| Function | Symbol |
|----------|--------|
| `public: __cdecl MoveReplacer::MoveReplacer(struct MoveReplacer const &)` | `??0MoveReplacer@@QAA@ABU0@@Z` |
| `public: void __cdecl SongLayout::SetDefaultPattern(int)` | `?SetDefaultPattern@SongLayout@@QAAXH@Z` |
| `public: void __cdecl SongLayout::SetDefaultReplacer(void)` | `?SetDefaultReplacer@SongLayout@@QAAXXZ` |

### `src/system/hamobj/TransConstraint.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl TransConstraint::Highlight(void)` | `?Highlight@TransConstraint@@UAAXXZ` |
| `public: virtual void __cdecl TransConstraint::Poll(void)` | `?Poll@TransConstraint@@UAAXXZ` |

### `src/system/os/Debug.cpp` (8)

| Function | Symbol |
|----------|--------|
| `char const * __cdecl DevHostname(class Symbol)` | `?DevHostname@@YAPBDVSymbol@@@Z` |
| `long __cdecl HmxGlobalHandler(struct _EXCEPTION_POINTERS *)` | `?HmxGlobalHandler@@YAJPAU_EXCEPTION_POINTERS@@@Z` |
| `private: void __cdecl Debug::Modal(enum Debug::ModalType &, char const *, void *)` | `?Modal@Debug@@AAAXAAW4ModalType@1@PBDPAX@Z` |
| `public: __cdecl ScopedState<bool, 1, 0>::~ScopedState<bool, 1, 0>(void)` | `??1?$ScopedState@_N$00$0A@@@QAA@XZ` |
| `public: __cdecl StackString<3096>::StackString<3096>(void)` | `??0?$StackString@$0MBI@@@QAA@XZ` |
| `public: __cdecl StackString<4096>::StackString<4096>(char const *)` | `??0?$StackString@$0BAAA@@@QAA@PBD@Z` |
| `public: __cdecl StackString<512>::StackString<512>(void)` | `??0?$StackString@$0CAA@@@QAA@XZ` |
| `public: void __cdecl Debug::DoCrucible(enum Debug::ModalType, char const *, void *)` | `?DoCrucible@Debug@@QAAXW4ModalType@1@PBDPAX@Z` |

### `src/system/rndobj/AmbientOcclusion.cpp` (11)

| Function | Symbol |
|----------|--------|
| `protected: bool __cdecl RndAmbientOcclusion::IsValid_Tessellate(class RndMesh const *, class ObjectDir const *) const` | `?IsValid_Tessellate@RndAmbientOcclusion@@IBA_NPBVRndMesh@@PBVObjectDir@@@Z` |
| `protected: float __cdecl RndAmbientOcclusion::DistanceSH(class Vector4const &, class Vector3const &, class Vector4const &, class Vector3const &) const` | `?DistanceSH@RndAmbientOcclusion@@IBAMABVVector4@@ABVVector3@@01@Z` |
| `protected: void __cdecl RndAmbientOcclusion::BurnTransform(class RndMesh *, class stlpmtx_std::list<class RndMesh *, class stlpmtx_std::StlNodeAlloc<class RndMesh *> > &) const` | `?BurnTransform@RndAmbientOcclusion@@IBAXPAVRndMesh@@AAV?$list@PAVRndMesh@@V?$StlNodeAlloc@PAVRndMesh@@@stlpmtx_std@@@stlpmtx_std@@@Z` |
| `protected: void __cdecl RndAmbientOcclusion::CalculateAOAtPoint(class Vector3const &, class Vector3const &, float *) const` | `?CalculateAOAtPoint@RndAmbientOcclusion@@IBAXABVVector3@@0PAM@Z` |
| `protected: void __cdecl RndAmbientOcclusion::SmoothResults(class RndMesh *) const` | `?SmoothResults@RndAmbientOcclusion@@IBAXPAVRndMesh@@@Z` |
| `public: bool __cdecl VectorSort<class RndMesh *>::operator()(class RndMesh *, class RndMesh *)` | `??R?$VectorSort@PAVRndMesh@@@@QAA_NPAVRndMesh@@0@Z` |
| `public: bool __cdecl kdTree<class Triangle>::Intersect(class Vector3const &, class Vector3const &, float, float &) const` | `?Intersect@?$kdTree@VTriangle@@@@QBA_NABVVector3@@0MAAM@Z` |
| `public: bool __cdecl kdTree<class Triangle>::kdTreeNode::FindSplit_SAH(class Box const &, class stlpmtx_std::list<class Triangle *, class stlpmtx_std::StlNodeAlloc<class Triangle *> > const &)` | `?FindSplit_SAH@kdTreeNode@?$kdTree@VTriangle@@@@QAA_NABVBox@@ABV?$list@PAVTriangle@@V?$StlNodeAlloc@PAVTriangle@@@stlpmtx_std@@@stlpmtx_std@@@Z` |
| `public: static void __cdecl RndAmbientOcclusion::BlendVert(class RndMesh::Vert const &, class RndMesh::Vert const &, class RndMesh::Vert &)` | `?BlendVert@RndAmbientOcclusion@@SAXABVVert@RndMesh@@0AAV23@@Z` |
| `public: void __cdecl RndAmbientOcclusion::CalculateAO(float *)` | `?CalculateAO@RndAmbientOcclusion@@QAAXPAM@Z` |
| `public: void __cdecl RndAmbientOcclusion::Tessellate(float *, float *)` | `?Tessellate@RndAmbientOcclusion@@QAAXPAM0@Z` |

### `src/system/rndobj/AnimFilter.cpp` (2)

| Function | Symbol |
|----------|--------|
| `protected: class DataNode __cdecl RndAnimFilter::OnSafeAnims(class DataArray *)` | `?OnSafeAnims@RndAnimFilter@@IAA?AVDataNode@@PAVDataArray@@@Z` |
| `public: virtual void __cdecl RndAnimFilter::SetFrame(float, float)` | `?SetFrame@RndAnimFilter@@UAAXMM@Z` |

### `src/system/rndobj/Bitmap.cpp` (5)

| Function | Symbol |
|----------|--------|
| `private: bool __cdecl RndBitmap::SamePaletteColors(class RndBitmap const &) const` | `?SamePaletteColors@RndBitmap@@ABA_NABV1@@Z` |
| `private: bool __cdecl RndBitmap::SamePixelFormat(class RndBitmap const &) const` | `?SamePixelFormat@RndBitmap@@ABA_NABV1@@Z` |
| `public: bool __cdecl RndBitmap::LoadDIB(class BinStream *, unsigned int)` | `?LoadDIB@RndBitmap@@QAA_NPAVBinStream@@I@Z` |
| `public: void __cdecl RndBitmap::Load(class BinStream &)` | `?Load@RndBitmap@@QAAXAAVBinStream@@@Z` |
| `public: void __cdecl RndBitmap::SetPreMultipliedAlpha(void)` | `?SetPreMultipliedAlpha@RndBitmap@@QAAXXZ` |

### `src/system/rndobj/BoxMap.cpp` (3)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl BoxMapLighting::ApplyLight(class BoxLightArray<struct BoxMapLighting::LightParams_Directional, 50> const &) const` | `?ApplyLight@BoxMapLighting@@ABAXABV?$BoxLightArray@ULightParams_Directional@BoxMapLighting@@$0DC@@@@Z` |
| `private: void __cdecl BoxMapLighting::ApplyLight(class BoxLightArray<struct BoxMapLighting::LightParams_Point, 50> const &, class Vector3const &) const` | `?ApplyLight@BoxMapLighting@@ABAXABV?$BoxLightArray@ULightParams_Point@BoxMapLighting@@$0DC@@@ABVVector3@@@Z` |
| `private: void __cdecl BoxMapLighting::ApplyLight(class BoxLightArray<struct BoxMapLighting::LightParams_Spot, 50> const &, class Vector3const &) const` | `?ApplyLight@BoxMapLighting@@ABAXABV?$BoxLightArray@ULightParams_Spot@BoxMapLighting@@$0DC@@@ABVVector3@@@Z` |

### `src/system/rndobj/Cam.cpp` (4)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl RndCam::UpdateLocal(void)` | `?UpdateLocal@RndCam@@IAAXXZ` |
| `public: void __cdecl RndCam::GetCamFrustum(class Vector3&, class Vector3(&)[4])` | `?GetCamFrustum@RndCam@@QAAXAAVVector3@@AAY03V2@@Z` |
| `public: void __cdecl RndCam::GetViewProjectXfms(class Transform &, class Hmx::Matrix4&) const` | `?GetViewProjectXfms@RndCam@@QBAXAAVTransform@@AAVMatrix4@Hmx@@@Z` |
| `void __cdecl Transpose(class Hmx::Matrix4const &, class Hmx::Matrix4&)` | `?Transpose@@YAXABVMatrix4@Hmx@@AAV12@@Z` |

### `src/system/rndobj/ColorXfm.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl RndColorXfm::AdjustHue(void)` | `?AdjustHue@RndColorXfm@@QAAXXZ` |
| `public: void __cdecl RndColorXfm::AdjustSaturation(void)` | `?AdjustSaturation@RndColorXfm@@QAAXXZ` |

### `src/system/rndobj/Console.cpp` (8)

| Function | Symbol |
|----------|--------|
| `private: bool __cdecl RndConsole::OnMsg(class KeyboardKeyMsg const &)` | `?OnMsg@RndConsole@@AAA_NABVKeyboardKeyMsg@@@Z` |
| `private: void __cdecl RndConsole::ExecuteLine(void)` | `?ExecuteLine@RndConsole@@AAAXXZ` |
| `private: void __cdecl RndConsole::InsertBreak(class DataArray *, int)` | `?InsertBreak@RndConsole@@AAAXPAVDataArray@@H@Z` |
| `public: void __cdecl RndConsole::Break(class DataArray *)` | `?Break@RndConsole@@QAAXPAVDataArray@@@Z` |
| `public: void __cdecl RndConsole::Clear(int)` | `?Clear@RndConsole@@QAAXH@Z` |
| `public: void __cdecl RndConsole::MoveLevel(int)` | `?MoveLevel@RndConsole@@QAAXH@Z` |
| `public: void __cdecl RndConsole::SetShowing(bool)` | `?SetShowing@RndConsole@@QAAX_N@Z` |
| `public: void __cdecl RndConsole::Step(int)` | `?Step@RndConsole@@QAAXH@Z` |

### `src/system/rndobj/CubeTex.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl RndTex::Load(class BinStream &)` | `?Load@RndTex@@UAAXAAVBinStream@@@Z` |

### `src/system/rndobj/DOFProc_NG.cpp` (4)

| Function | Symbol |
|----------|--------|
| `protected: virtual bool __cdecl NgDOFProc::Enabled(void) const` | `?Enabled@NgDOFProc@@MBA_NXZ` |
| `protected: virtual void __cdecl NgDOFProc::Set(class RndCam const *, float, float, float, float)` | `?Set@NgDOFProc@@MAAXPBVRndCam@@MMMM@Z` |
| `public: virtual void __cdecl NgDOFProc::DoPost(void)` | `?DoPost@NgDOFProc@@UAAXXZ` |
| `void __cdecl SetVHBlurWeights(bool, int, int)` | `?SetVHBlurWeights@@YAX_NHH@Z` |

### `src/system/rndobj/Env.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: virtual bool __cdecl RndEnviron::Replace(class ObjRef *, class Hmx::Object *)` | `?Replace@RndEnviron@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z` |
| `public: virtual void __cdecl RndEnviron::Draw(void)` | `?Draw@RndEnviron@@UAAXXZ` |

### `src/system/rndobj/Env_NG.cpp` (7)

| Function | Symbol |
|----------|--------|
| `bool __cdecl `anonymous namespace'::CheckPointLight(class NgLight &)` | `?CheckPointLight@?A0x8e417309@@YA_NAAVNgLight@@@Z` |
| `bool __cdecl `anonymous namespace'::CheckProjLight(class NgLight &)` | `?CheckProjLight@?A0x8e417309@@YA_NAAVNgLight@@@Z` |
| `bool __cdecl `anonymous namespace'::SetPointLightRegisters(int, class RndLight &, bool &)` | `?SetPointLightRegisters@?A0x8e417309@@YA_NHAAVRndLight@@AA_N@Z` |
| `bool __cdecl `anonymous namespace'::SetProjLightRegisters(int, int, class NgLight &)` | `?SetProjLightRegisters@?A0x8e417309@@YA_NHHAAVNgLight@@@Z` |
| `void __cdecl `anonymous namespace'::ClearLightRegisters(int)` | `?ClearLightRegisters@?A0x8e417309@@YAXH@Z` |
| `void __cdecl `anonymous namespace'::ClearLightTransforms(void)` | `?ClearLightTransforms@?A0x8e417309@@YAXXZ` |
| `void __cdecl `anonymous namespace'::ClearPointCubeTex(void)` | `?ClearPointCubeTex@?A0x8e417309@@YAXXZ` |

### `src/system/rndobj/EventTrigger.cpp` (5)

| Function | Symbol |
|----------|--------|
| `protected: static class DataNode __cdecl EventTrigger::Cleanup(class DataArray *)` | `?Cleanup@EventTrigger@@KA?AVDataNode@@PAVDataArray@@@Z` |
| `protected: void __cdecl EventTrigger::TriggerSelf(void)` | `?TriggerSelf@EventTrigger@@IAAXXZ` |
| `public: __cdecl CharMeshHide::Hide::~Hide(void)` | `??1Hide@CharMeshHide@@QAA@XZ` |
| `public: void __cdecl ObjList<struct EventTrigger::HideDelay>::push_back(struct EventTrigger::HideDelay const &)` | `?push_back@?$ObjList@UHideDelay@EventTrigger@@@@QAAXABUHideDelay@EventTrigger@@@Z` |
| `public: void __cdecl ObjList<struct EventTrigger::ProxyCall>::push_back(struct EventTrigger::ProxyCall const &)` | `?push_back@?$ObjList@UProxyCall@EventTrigger@@@@QAAXABUProxyCall@EventTrigger@@@Z` |

### `src/system/rndobj/Flare.cpp` (1)

| Function | Symbol |
|----------|--------|
| `protected: class Hmx::Rect & __cdecl RndFlare::CalcRect(class Vector2&, float &)` | `?CalcRect@RndFlare@@IAAAAVRect@Hmx@@AAVVector2@@AAM@Z` |

### `src/system/rndobj/Font.cpp` (6)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl RndFont::SetBitmapSize(class Vector2const &)` | `?SetBitmapSize@RndFont@@IAAXABVVector2@@@Z` |
| `protected: void __cdecl RndFont::SetCharInfo(struct RndFont::CharInfo *, class RndBitmap &, class Vector2const &, int)` | `?SetCharInfo@RndFont@@IAAXPAUCharInfo@1@AAVRndBitmap@@ABVVector2@@H@Z` |
| `protected: void __cdecl RndFont::UpdateChars(void)` | `?UpdateChars@RndFont@@IAAXXZ` |
| `public: bool __cdecl RndFont::CharWidthAdvanceCoords(unsigned short, float &, float &, class Vector2&, class Vector2&) const` | `?CharWidthAdvanceCoords@RndFont@@QBA_NGAAM0AAVVector2@@1@Z` |
| `public: char const * __cdecl HamSongMetadata::Album(void) const` | `?Album@HamSongMetadata@@QBAPBDXZ` |
| `public: void __cdecl RndFont::BleedTest(void)` | `?BleedTest@RndFont@@QAAXXZ` |

### `src/system/rndobj/Font3d.cpp` (8)

| Function | Symbol |
|----------|--------|
| `public: bool __cdecl RndFont3d::CharWidthAdvanceMesh(unsigned short, float &, float &, class RndMesh **) const` | `?CharWidthAdvanceMesh@RndFont3d@@QBA_NGAAM0PAPAVRndMesh@@@Z` |
| `public: class Vector3 __cdecl RndFont3d::CharOriginOffset(void) const` | `?CharOriginOffset@RndFont3d@@QBA?AVVector3@@XZ` |
| `public: struct RndFont3d::CharInfo * __cdecl RndFont3d::GetCharInfo(unsigned short) const` | `?GetCharInfo@RndFont3d@@QBAPAUCharInfo@1@G@Z` |
| `public: virtual bool __cdecl RndFont3d::SyncProperty(class DataNode &, class DataArray *, int, enum PropOp)` | `?SyncProperty@RndFont3d@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z` |
| `public: virtual float __cdecl RndFont3d::AspectRatio(void) const` | `?AspectRatio@RndFont3d@@UBAMXZ` |
| `public: virtual float __cdecl RndFont3d::Kerning(unsigned short, unsigned short) const` | `?Kerning@RndFont3d@@UBAMGG@Z` |
| `public: virtual int __cdecl UIListDir::NumData(void) const` | `?NumData@UIListDir@@UBAHXZ` |
| `public: virtual void __cdecl RndFont3d::Load(class BinStream &)` | `?Load@RndFont3d@@UAAXAAVBinStream@@@Z` |

### `src/system/rndobj/FontBase.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: virtual class DataNode __cdecl UIListWidget::Handle(class DataArray *, bool)` | `?Handle@UIListWidget@@UAA?AVDataNode@@PAVDataArray@@_N@Z` |
| `public: virtual void __cdecl RndFontBase::Load(class BinStream &)` | `?Load@RndFontBase@@UAAXAAVBinStream@@@Z` |

### `src/system/rndobj/Gen.cpp` (4)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl RndGenerator::DrawParticleSys(class Transform &, float)` | `?DrawParticleSys@RndGenerator@@IAAXAAVTransform@@M@Z` |
| `public: virtual void __cdecl RndGenerator::SetFrame(float, float)` | `?SetFrame@RndGenerator@@UAAXMM@Z` |
| `public: virtual void __cdecl RndParticleSys::UpdateSphere(void)` | `?UpdateSphere@RndParticleSys@@UAAXXZ` |
| `public: void __cdecl RndGenerator::Generate(float)` | `?Generate@RndGenerator@@QAAXM@Z` |

### `src/system/rndobj/Group.cpp` (2)

| Function | Symbol |
|----------|--------|
| `bool __cdecl SortInWorld(struct GroupDrawDist const &, struct GroupDrawDist const &)` | `?SortInWorld@@YA_NABUGroupDrawDist@@0@Z` |
| `public: int __cdecl RndGroup::MoveObject(class Hmx::Object *, int)` | `?MoveObject@RndGroup@@QAAHPAVObject@Hmx@@H@Z` |

### `src/system/rndobj/Line.cpp` (6)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl RndLine::MapVerts(int, class RndLine::VertsMap &)` | `?MapVerts@RndLine@@IAAXHAAVVertsMap@1@@Z` |
| `protected: void __cdecl RndLine::UpdateLine(class RndLine::Point *, class RndLine::Point *)` | `?UpdateLine@RndLine@@IAAXPAVPoint@1@0@Z` |
| `protected: void __cdecl RndLine::UpdateLine(class Transform const &, float)` | `?UpdateLine@RndLine@@IAAXABVTransform@@M@Z` |
| `protected: void __cdecl RndLine::UpdateLinePair(class RndLine::Point *, class RndLine::Point *)` | `?UpdateLinePair@RndLine@@IAAXPAVPoint@1@0@Z` |
| `public: virtual void __cdecl Spotlight::UpdateSphere(void)` | `?UpdateSphere@Spotlight@@UAAXXZ` |
| `public: void __cdecl RndLine::SetPointsColor(int, int, class Hmx::Color const &)` | `?SetPointsColor@RndLine@@QAAXHHABVColor@Hmx@@@Z` |

### `src/system/rndobj/Lit.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: class Transform __cdecl RndLight::Projection(void)` | `?Projection@RndLight@@QAA?AVTransform@@XZ` |
| `public: virtual void __cdecl RndLight::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` | `?Copy@RndLight@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z` |

### `src/system/rndobj/Lit_NG.cpp` (8)

| Function | Symbol |
|----------|--------|
| `class Hmx::Matrix4 __cdecl Hmx::operator*(class Transform const &, class Hmx::Matrix4const &)` | `??DHmx@@YA?AVMatrix4@0@ABVTransform@@ABV10@@Z` |
| `protected: bool __cdecl NgLight::SphereConeTest(class Vector3const &, float)` | `?SphereConeTest@NgLight@@IAA_NABVVector3@@M@Z` |
| `protected: class RndTex * __cdecl NgLight::CreateShadowTex(void)` | `?CreateShadowTex@NgLight@@IAAPAVRndTex@@XZ` |
| `protected: virtual void __cdecl NgLight::BlurShadowRT(void)` | `?BlurShadowRT@NgLight@@MAAXXZ` |
| `protected: virtual void __cdecl NgLight::RenderShadows(class stlpmtx_std::vector<class RndDrawable *, class stlpmtx_std::StlNodeAlloc<class RndDrawable *> > &)` | `?RenderShadows@NgLight@@MAAXAAV?$vector@PAVRndDrawable@@V?$StlNodeAlloc@PAVRndDrawable@@@stlpmtx_std@@@stlpmtx_std@@@Z` |
| `protected: virtual void __cdecl NgLight::SetAndClearShadowViewport(void)` | `?SetAndClearShadowViewport@NgLight@@MAAXXZ` |
| `protected: void __cdecl NgLight::SetShadowTransforms(void)` | `?SetShadowTransforms@NgLight@@IAAXXZ` |
| `public: void __cdecl NgLight::CheckShadowMap(void)` | `?CheckShadowMap@NgLight@@QAAXXZ` |

### `src/system/rndobj/MatAnim.cpp` (2)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl RndMatAnim::LoadStages(class BinStreamRev &)` | `?LoadStages@RndMatAnim@@IAAXAAVBinStreamRev@@@Z` |
| `public: virtual bool __cdecl RndMatAnim::Replace(class ObjRef *, class Hmx::Object *)` | `?Replace@RndMatAnim@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z` |

### `src/system/rndobj/Mat_NG.cpp` (3)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl NgMat::RefreshState(void)` | `?RefreshState@NgMat@@IAAXXZ` |
| `public: static class Hmx::Object * __cdecl NgMat::NewObject(void)` | `?NewObject@NgMat@@SAPAVObject@Hmx@@XZ` |
| `void __cdecl MakeTex3(class Transform const &, bool, class Hmx::Matrix4&)` | `?MakeTex3@@YAXABVTransform@@_NAAVMatrix4@Hmx@@@Z` |

### `src/system/rndobj/Mesh.cpp` (16)

| Function | Symbol |
|----------|--------|
| `class Vector3 __cdecl TransformNormal(class Vector3const &, class Hmx::Matrix3const &)` | `?TransformNormal@@YA?AVVector3@@ABV1@ABVMatrix3@Hmx@@@Z` |
| `protected: bool __cdecl RndMesh::PatchOkay(int, int)` | `?PatchOkay@RndMesh@@IAA_NHH@Z` |
| `protected: class DataNode __cdecl RndMesh::OnCompareEdgeVerts(class DataArray const *)` | `?OnCompareEdgeVerts@RndMesh@@IAA?AVDataNode@@PBVDataArray@@@Z` |
| `protected: int __cdecl PatchVerts::GreaterEq(int) const` | `?GreaterEq@PatchVerts@@IBAHH@Z` |
| `protected: virtual void __cdecl RndMesh::OnSync(int)` | `?OnSync@RndMesh@@MAAXH@Z` |
| `public: bool __cdecl PatchVerts::HasVert(int) const` | `?HasVert@PatchVerts@@QBA_NH@Z` |
| `public: class Vector3 __cdecl RndMesh::SkinVertex(class RndMesh::Vert const &, class Vector3*)` | `?SkinVertex@RndMesh@@QAA?AVVector3@@ABVVert@1@PAV2@@Z` |
| `public: int __cdecl AccomplishmentProgress::GetFlawlessMoveCount(void) const` | `?GetFlawlessMoveCount@AccomplishmentProgress@@QBAHXZ` |
| `public: virtual class RndDrawable * __cdecl RndMesh::CollideShowing(class Segment const &, float &, class Plane &)` | `?CollideShowing@RndMesh@@UAAPAVRndDrawable@@ABVSegment@@AAMAAVPlane@@@Z` |
| `public: void __cdecl PatchVerts::Add(int, class RndMesh::VertVector &, class Vector3&)` | `?Add@PatchVerts@@QAAXHAAVVertVector@RndMesh@@AAVVector3@@@Z` |
| `public: void __cdecl PatchVerts::Clear(void)` | `?Clear@PatchVerts@@QAAXXZ` |
| `public: void __cdecl RndMesh::DeleteBones(bool)` | `?DeleteBones@RndMesh@@QAAX_N@Z` |
| `public: void __cdecl RndMesh::InstanceGeomOwnerBones(void)` | `?InstanceGeomOwnerBones@RndMesh@@QAAXXZ` |
| `public: void __cdecl RndMesh::SetVolume(enum RndMesh::Volume)` | `?SetVolume@RndMesh@@QAAXW4Volume@1@@Z` |
| `public: void __cdecl Triangle::Set(class Vector3const &, class Vector3const &, class Vector3const &)` | `?Set@Triangle@@QAAXABVVector3@@00@Z` |
| `void __cdecl SaveCompressedVertex(struct CompressedVertex_Xbox const &, class BinStream &)` | `?SaveCompressedVertex@@YAXABUCompressedVertex_Xbox@@AAVBinStream@@@Z` |

### `src/system/rndobj/MeshAnim.cpp` (3)

| Function | Symbol |
|----------|--------|
| `public: virtual bool __cdecl RndMeshAnim::Replace(class ObjRef *, class Hmx::Object *)` | `?Replace@RndMeshAnim@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z` |
| `public: virtual void __cdecl RndMeshAnim::SetFrame(float, float)` | `?SetFrame@RndMeshAnim@@UAAXMM@Z` |
| `public: void __cdecl RndMeshAnim::ShrinkVerts(int)` | `?ShrinkVerts@RndMeshAnim@@QAAXH@Z` |

### `src/system/rndobj/MeshDeform.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: __cdecl CharIKHand::IKTarget::~IKTarget(void)` | `??1IKTarget@CharIKHand@@QAA@XZ` |
| `public: int __cdecl RndMeshDeform::VertArray::AppendWeights(int, int *const, float *const)` | `?AppendWeights@VertArray@RndMeshDeform@@QAAHHQAHQAM@Z` |

### `src/system/rndobj/MetaMaterial.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: bool __cdecl MetaMaterial::IsEquivalent(class MetaMaterial *)` | `?IsEquivalent@MetaMaterial@@QAA_NPAV1@@Z` |

### `src/system/rndobj/Morph.cpp` (3)

| Function | Symbol |
|----------|--------|
| `public: float __cdecl RndMorph::InterpWeight(class Keys<float, float> const &, float)` | `?InterpWeight@RndMorph@@QAAMABV?$Keys@MM@@M@Z` |
| `public: virtual float __cdecl RndMorph::EndFrame(void)` | `?EndFrame@RndMorph@@UAAMXZ` |
| `public: virtual void __cdecl RndMorph::SetFrame(float, float)` | `?SetFrame@RndMorph@@UAAXMM@Z` |

### `src/system/rndobj/Part.cpp` (5)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl RndParticleSys::InitParticle(float, class RndParticle *, class Transform const *, class PartOverride &)` | `?InitParticle@RndParticleSys@@IAAXMPAVRndParticle@@PBVTransform@@AAVPartOverride@@@Z` |
| `public: virtual bool __cdecl RndParticleSys::Replace(class ObjRef *, class Hmx::Object *)` | `?Replace@RndParticleSys@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z` |
| `public: virtual void __cdecl RndParticleSys::Load(class BinStream &)` | `?Load@RndParticleSys@@UAAXAAVBinStream@@@Z` |
| `public: virtual void __cdecl RndParticleSys::Mats(class stlpmtx_std::list<class RndMat *, class stlpmtx_std::StlNodeAlloc<class RndMat *> > &, bool)` | `?Mats@RndParticleSys@@UAAXAAV?$list@PAVRndMat@@V?$StlNodeAlloc@PAVRndMat@@@stlpmtx_std@@@stlpmtx_std@@_N@Z` |
| `public: virtual void __cdecl RndParticleSys::Poll(void)` | `?Poll@RndParticleSys@@UAAXXZ` |

### `src/system/rndobj/PostProc.cpp` (4)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl RndPostProc::UpdateColorModulation(void)` | `?UpdateColorModulation@RndPostProc@@IAAXXZ` |
| `public: float __cdecl TrueColor::ExposureRecipe::GetLux(void)` | `?GetLux@ExposureRecipe@TrueColor@@QAAMXZ` |
| `public: void __cdecl RndPostProc::Interp(class RndPostProc const *, class RndPostProc const *, float)` | `?Interp@RndPostProc@@QAAXPBV1@0M@Z` |
| `public: void __cdecl RndPostProc::LoadRev(class BinStreamRev &)` | `?LoadRev@RndPostProc@@QAAXAAVBinStreamRev@@@Z` |

### `src/system/rndobj/PostProc_NG.cpp` (21)

| Function | Symbol |
|----------|--------|
| `protected: static void __cdecl NgPostProc::ReleaseTex(void)` | `?ReleaseTex@NgPostProc@@KAXXZ` |
| `protected: virtual void __cdecl NgPostProc::OnSelect(void)` | `?OnSelect@NgPostProc@@MAAXXZ` |
| `protected: virtual void __cdecl NgPostProc::OnUnselect(void)` | `?OnUnselect@NgPostProc@@MAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckBlendPrevious(void)` | `?CheckBlendPrevious@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckChromaticAberration(void)` | `?CheckChromaticAberration@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckGradientMap(void)` | `?CheckGradientMap@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckHallOfTime(void)` | `?CheckHallOfTime@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckHueConverge(void)` | `?CheckHueConverge@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckMotionBlur(void)` | `?CheckMotionBlur@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckNoise(void)` | `?CheckNoise@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckPosterizeAndKaleidoscope(void)` | `?CheckPosterizeAndKaleidoscope@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckRefract(void)` | `?CheckRefract@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::CheckVignette(void)` | `?CheckVignette@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::DoBloom(void)` | `?DoBloom@NgPostProc@@IAAXXZ` |
| `protected: void __cdecl NgPostProc::ModulateColorXfm(void)` | `?ModulateColorXfm@NgPostProc@@IAAXXZ` |
| `public: static void __cdecl NgPostProc::Terminate(void)` | `?Terminate@NgPostProc@@SAXXZ` |
| `public: virtual void __cdecl NgPostProc::DoPost(void)` | `?DoPost@NgPostProc@@UAAXXZ` |
| `public: virtual void __cdecl NgPostProc::EndWorld(void)` | `?EndWorld@NgPostProc@@UAAXXZ` |
| `public: virtual void __cdecl NgPostProc::QueueMotionBlurObject(class RndDrawable *)` | `?QueueMotionBlurObject@NgPostProc@@UAAXPAVRndDrawable@@@Z` |
| `void __cdecl Bloom_Blur(class RndTex *, class RndTex *, enum BloomBlurStyle, enum BloomBlurDirection, unsigned int, float, float)` | `?Bloom_Blur@@YAXPAVRndTex@@0W4BloomBlurStyle@@W4BloomBlurDirection@@IMM@Z` |
| `void __cdecl Bloom_Downsample(enum ShaderType, class RndTex *, class RndTex *)` | `?Bloom_Downsample@@YAXW4ShaderType@@PAVRndTex@@1@Z` |

### `src/system/rndobj/PropAnim.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: class DataNode __cdecl RndPropAnim::ForeachKeyframe(class DataArray const *)` | `?ForeachKeyframe@RndPropAnim@@QAA?AVDataNode@@PBVDataArray@@@Z` |
| `public: int __cdecl Keys<class RndMatAnim::TexPtr, class RndTex *>::KeyGreaterEq(float) const` | `?KeyGreaterEq@?$Keys@VTexPtr@RndMatAnim@@PAVRndTex@@@@QBAHM@Z` |

### `src/system/rndobj/PropKeys.cpp` (2)

| Function | Symbol |
|----------|--------|
| `float __cdecl CalcSpline(float, float *const)` | `?CalcSpline@@YAMMQAM@Z` |
| `public: virtual int __cdecl QuatKeys::QuatAt(float, class Hmx::Quat &)` | `?QuatAt@QuatKeys@@UAAHMAAVQuat@Hmx@@@Z` |

### `src/system/rndobj/Ribbon.cpp` (3)

| Function | Symbol |
|----------|--------|
| `public: void __cdecl RndRibbon::ConstructMesh(void)` | `?ConstructMesh@RndRibbon@@QAAXXZ` |
| `public: void __cdecl RndRibbon::UpdateChase(void)` | `?UpdateChase@RndRibbon@@QAAXXZ` |
| `public: void __cdecl RndRibbon::UpdateMesh(void)` | `?UpdateMesh@RndRibbon@@QAAXXZ` |

### `src/system/rndobj/Rnd.cpp` (11)

| Function | Symbol |
|----------|--------|
| `protected: class DataNode __cdecl Rnd::OnToggleHeap(class DataArray const *)` | `?OnToggleHeap@Rnd@@IAA?AVDataNode@@PBVDataArray@@@Z` |
| `protected: class RndTex * __cdecl Rnd::CreateDefaultTexture(enum Rnd::DefaultTextureType)` | `?CreateDefaultTexture@Rnd@@IAAPAVRndTex@@W4DefaultTextureType@1@@Z` |
| `protected: float __cdecl Rnd::DrawTimers(float)` | `?DrawTimers@Rnd@@IAAMM@Z` |
| `protected: virtual void __cdecl Rnd::DrawPreClear(void)` | `?DrawPreClear@Rnd@@MAAXXZ` |
| `protected: void __cdecl Rnd::UpdateRate(void)` | `?UpdateRate@Rnd@@IAAXXZ` |
| `public: __cdecl Rnd::CompressTexDesc::~CompressTexDesc(void)` | `??1CompressTexDesc@Rnd@@QAA@XZ` |
| `public: virtual __cdecl ModalKeyListener::~ModalKeyListener(void)` | `??1ModalKeyListener@@UAA@XZ` |
| `public: void __cdecl Rnd::Modal(enum Debug::ModalType &, class FixedString &, bool)` | `?Modal@Rnd@@QAAXAAW4ModalType@Debug@@AAVFixedString@@_N@Z` |
| `public: void __cdecl Rnd::TestPoint(class Vector3const &, class RndFlare *)` | `?TestPoint@Rnd@@QAAXABVVector3@@PAVRndFlare@@@Z` |
| `unsigned long __cdecl CompressThread(void *)` | `?CompressThread@@YAKPAX@Z` |
| `void __cdecl WordWrap(char const *, int, char *, int)` | `?WordWrap@@YAXPBDHPADH@Z` |

### `src/system/rndobj/Rnd_NG.cpp` (1)

| Function | Symbol |
|----------|--------|
| `float __cdecl EstimateDraw(int)` | `?EstimateDraw@@YAMH@Z` |

### `src/system/rndobj/ScreenMask.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl RndScreenMask::DrawShowing(void)` | `?DrawShowing@RndScreenMask@@UAAXXZ` |
| `public: virtual void __cdecl RndScreenMask::Load(class BinStream &)` | `?Load@RndScreenMask@@UAAXAAVBinStream@@@Z` |

### `src/system/rndobj/Shader.cpp` (34)

| Function | Symbol |
|----------|--------|
| `class Hmx::Matrix4 __cdecl Hmx::operator*(class Hmx::Matrix4const &, class Hmx::Matrix4const &)` | `??DHmx@@YA?AVMatrix4@0@ABV10@0@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderDepthVolume::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderDepthVolume@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderDrawRect::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderDrawRect@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderFur::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderFur@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderMultimesh::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderMultimesh@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderParticles::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderParticles@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderPostProc::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderPostProc@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderSimple::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderSimple@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderStandard::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderStandard@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderSyncTrack::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderSyncTrack@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderUnwrapUV::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderUnwrapUV@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderVelocity::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderVelocity@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual unsigned __int64 __cdecl RndShaderVelocityCamera::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` | `?CalcShaderOpts@RndShaderVelocityCamera@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderDepthVolume::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderDepthVolume@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderDrawRect::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderDrawRect@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderFur::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderFur@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderMultimesh::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderMultimesh@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderParticles::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderParticles@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderPostProc::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderPostProc@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderStandard::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderStandard@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderSyncTrack::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderSyncTrack@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderUnwrapUV::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderUnwrapUV@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderVelocity::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderVelocity@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `protected: virtual void __cdecl RndShaderVelocityCamera::Select(class RndMat *, enum ShaderType, bool)` | `?Select@RndShaderVelocityCamera@@MAAXPAVRndMat@@W4ShaderType@@_N@Z` |
| `public: class Vector4 __cdecl Hmx::Matrix4::Col4(int) const` | `?Col4@Matrix4@Hmx@@QBA?AVVector4@@H@Z` |
| `public: virtual bool __cdecl RndShaderMultimesh::CheckError(enum RndShader::MatFlagErrorType)` | `?CheckError@RndShaderMultimesh@@UAA_NW4MatFlagErrorType@RndShader@@@Z` |
| `public: virtual bool __cdecl RndShaderParticles::CheckError(enum RndShader::MatFlagErrorType)` | `?CheckError@RndShaderParticles@@UAA_NW4MatFlagErrorType@RndShader@@@Z` |
| `public: virtual void __cdecl StackString<32>::Print(char const *)` | `?Print@?$StackString@$0CA@@@UAAXPBD@Z` |
| `public: void __cdecl RndSpline::PrepareShader(void) const` | `?PrepareShader@RndSpline@@QBAXXZ` |
| `void __cdecl CheckDistortion(class RndMat *)` | `?CheckDistortion@@YAXPAVRndMat@@@Z` |
| `void __cdecl CheckDistortionOpts(class RndMat *, struct ShaderOptions &)` | `?CheckDistortionOpts@@YAXPAVRndMat@@AAUShaderOptions@@@Z` |
| `void __cdecl CheckExtrude(void)` | `?CheckExtrude@@YAXXZ` |
| `void __cdecl CheckShadow(void)` | `?CheckShadow@@YAXXZ` |
| `void __cdecl SetColorWriteMask(struct ShaderOptions const &, class RndMat *)` | `?SetColorWriteMask@@YAXABUShaderOptions@@PAVRndMat@@@Z` |

### `src/system/rndobj/ShaderProgram.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: bool __cdecl RndShaderProgram::Cache(enum ShaderType, struct ShaderOptions const &, class RndShaderBuffer *, class RndShaderBuffer *)` | `?Cache@RndShaderProgram@@QAA_NW4ShaderType@@ABUShaderOptions@@PAVRndShaderBuffer@@2@Z` |

### `src/system/rndobj/ShadowMap.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: static bool __cdecl RndShadowMap::PrepShadow(class RndDrawable *, class RndEnviron *)` | `?PrepShadow@RndShadowMap@@SA_NPAVRndDrawable@@PAVRndEnviron@@@Z` |
| `public: static void __cdecl RndShadowMap::EndShadow(void)` | `?EndShadow@RndShadowMap@@SAXXZ` |

### `src/system/rndobj/SoftParticleBuffer.cpp` (3)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl RndSoftParticleBuffer::BlurSurface(void)` | `?BlurSurface@RndSoftParticleBuffer@@AAAXXZ` |
| `public: virtual void __cdecl RndSoftParticleBuffer::DoPost(void)` | `?DoPost@RndSoftParticleBuffer@@UAAXXZ` |
| `public: void __cdecl RndSoftParticleBuffer::Queue(class RndDrawable *, enum BaseMaterial::Blend)` | `?Queue@RndSoftParticleBuffer@@QAAXPAVRndDrawable@@W4Blend@BaseMaterial@@@Z` |

### `src/system/rndobj/Spline.cpp` (7)

| Function | Symbol |
|----------|--------|
| `private: void __cdecl RndSpline::SyncDeformedCtrlPoints(int, int) const` | `?SyncDeformedCtrlPoints@RndSpline@@ABAXHH@Z` |
| `private: void __cdecl RndSpline::SyncDeformedDummyCtrlPoints(int, int) const` | `?SyncDeformedDummyCtrlPoints@RndSpline@@ABAXHH@Z` |
| `private: void __cdecl RndSpline::SyncPristineCtrlPoints(void)` | `?SyncPristineCtrlPoints@RndSpline@@AAAXXZ` |
| `public: virtual void __cdecl RndSpline::Poll(void)` | `?Poll@RndSpline@@UAAXXZ` |
| `public: void __cdecl RndSpline::CtrlPoint::Interp(class RndSpline::CtrlPoint const &, class RndSpline::CtrlPoint const &, float)` | `?Interp@CtrlPoint@RndSpline@@QAAXABV12@0M@Z` |
| `public: void __cdecl RndSpline::SetEndCtrlPoint(int)` | `?SetEndCtrlPoint@RndSpline@@QAAXH@Z` |
| `public: void __cdecl RndSpline::SetStartCtrlPoint(int)` | `?SetStartCtrlPoint@RndSpline@@QAAXH@Z` |

### `src/system/rndobj/Tex.cpp` (7)

| Function | Symbol |
|----------|--------|
| `protected: class DataNode __cdecl RndTex::OnSetBitmap(class DataArray const *)` | `?OnSetBitmap@RndTex@@IAA?AVDataNode@@PBVDataArray@@@Z` |
| `public: bool __cdecl RndTex::PowerOf2(void)` | `?PowerOf2@RndTex@@QAA_NXZ` |
| `public: static char const * __cdecl RndTex::CheckSize(int, int, int, int, enum RndTex::Type, bool)` | `?CheckSize@RndTex@@SAPBDHHHHW4Type@1@_N@Z` |
| `public: virtual void __cdecl RndTex::PostLoad(class BinStream &)` | `?PostLoad@RndTex@@UAAXAAVBinStream@@@Z` |
| `public: virtual void __cdecl RndTex::PreLoad(class BinStream &)` | `?PreLoad@RndTex@@UAAXAAVBinStream@@@Z` |
| `public: void __cdecl RndTex::SetBitmap(class RndBitmap const &, char const *, bool, enum RndTex::Type)` | `?SetBitmap@RndTex@@QAAXABVRndBitmap@@PBD_NW4Type@1@@Z` |
| `public: void __cdecl RndTex::SetBitmap(int, int, int, enum RndTex::Type, bool, char const *)` | `?SetBitmap@RndTex@@QAAXHHHW4Type@1@_NPBD@Z` |

### `src/system/rndobj/TexBlendController.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: enum RndTexBlendController::BlendState __cdecl RndTexBlendController::GetBlendState(float &, float) const` | `?GetBlendState@RndTexBlendController@@QBA?AW4BlendState@1@AAMM@Z` |

### `src/system/rndobj/TexBlender.cpp` (1)

| Function | Symbol |
|----------|--------|
| `public: virtual void __cdecl RndTexBlender::DrawShowing(void)` | `?DrawShowing@RndTexBlender@@UAAXXZ` |

### `src/system/rndobj/TexProc.cpp` (2)

| Function | Symbol |
|----------|--------|
| `protected: void __cdecl TexProc::DrawToTexture(void)` | `?DrawToTexture@TexProc@@IAAXXZ` |
| `public: virtual void __cdecl TexProc::Poll(void)` | `?Poll@TexProc@@UAAXXZ` |

### `src/system/rndobj/TexRenderer.cpp` (1)

| Function | Symbol |
|----------|--------|
| `float __cdecl ComputeAngle(class Vector3const &, class Vector3const &, class Vector3const &)` | `?ComputeAngle@@YAMABVVector3@@00@Z` |

### `src/system/rndobj/Text.cpp` (25)

| Function | Symbol |
|----------|--------|
| `bool __cdecl CalcScreenHeight(float, class RndMesh *, float &)` | `?CalcScreenHeight@@YA_NMPAVRndMesh@@AAM@Z` |
| `float __cdecl SegmentLength(int, int, float const *, unsigned short const *, float)` | `?SegmentLength@@YAMHHPBMPBGM@Z` |
| `protected: int __cdecl RndText::OnComputeCharWidths(unsigned short const *, float *, bool)` | `?OnComputeCharWidths@RndText@@IAAHPBGPAM_N@Z` |
| `protected: static class RndText::FontMapBase * __cdecl RndText::AcquireFontMap(class RndFontBase *)` | `?AcquireFontMap@RndText@@KAPAVFontMapBase@1@PAVRndFontBase@@@Z` |
| `protected: static void __cdecl RndText::DrawMesh(class RndMesh *, float, int)` | `?DrawMesh@RndText@@KAXPAVRndMesh@@MH@Z` |
| `protected: unsigned short const * __cdecl RndText::ParseMarkup(unsigned short const *, class RndText::StyleState &, unsigned short &)` | `?ParseMarkup@RndText@@IAAPBGPBGAAVStyleState@1@AAG@Z` |
| `protected: void __cdecl RndText::FitTextEllipsis(void)` | `?FitTextEllipsis@RndText@@IAAXXZ` |
| `protected: void __cdecl RndText::FitTextJust(void)` | `?FitTextJust@RndText@@IAAXXZ` |
| `protected: void __cdecl RndText::FitTextScroll(void)` | `?FitTextScroll@RndText@@IAAXXZ` |
| `protected: void __cdecl RndText::SizeCheck(void)` | `?SizeCheck@RndText@@IAAXXZ` |
| `protected: void __cdecl RndText::UpdateScrollOffsets(void)` | `?UpdateScrollOffsets@RndText@@IAAXXZ` |
| `public: __cdecl RndText::StyleState::StyleState(class RndText *, float)` | `??0StyleState@RndText@@QAA@PAV1@M@Z` |
| `public: float __cdecl RndText::ComputeCharWidthsForText(class String)` | `?ComputeCharWidthsForText@RndText@@QAAMVString@@@Z` |
| `public: int __cdecl Playlist::GetNumSongs(void) const` | `?GetNumSongs@Playlist@@QBAHXZ` |
| `public: static void __cdecl RndText::DrawBlacklight(void)` | `?DrawBlacklight@RndText@@SAXXZ` |
| `public: virtual float __cdecl RndText::GetDistanceToPlane(class Plane const &, class Vector3&)` | `?GetDistanceToPlane@RndText@@UAAMABVPlane@@AAVVector3@@@Z` |
| `public: virtual void __cdecl RndText::FontMap3d::AllocateMeshes(class RndText *, int)` | `?AllocateMeshes@FontMap3d@RndText@@UAAXPAV2@H@Z` |
| `public: virtual void __cdecl RndText::FontMap3d::CleanupSyncMeshes(void)` | `?CleanupSyncMeshes@FontMap3d@RndText@@UAAXXZ` |
| `public: virtual void __cdecl RndText::FontMap3d::IncrementDisplayableChars(unsigned short)` | `?IncrementDisplayableChars@FontMap3d@RndText@@UAAXG@Z` |
| `public: virtual void __cdecl RndText::FontMap3d::SetupCharacter(unsigned short, float &, float, class RndText::StyleState const &, unsigned short, float, enum RndText::FitType, float)` | `?SetupCharacter@FontMap3d@RndText@@UAAXGAAMMABVStyleState@2@GMW4FitType@2@M@Z` |
| `public: virtual void __cdecl RndText::FontMap::SetupCharacter(unsigned short, float &, float, class RndText::StyleState const &, unsigned short, float, enum RndText::FitType, float)` | `?SetupCharacter@FontMap@RndText@@UAAXGAAMMABVStyleState@2@GMW4FitType@2@M@Z` |
| `public: void __cdecl RndText::GetWidthHeightBox(class Box &) const` | `?GetWidthHeightBox@RndText@@QBAXAAVBox@@@Z` |
| `public: void __cdecl RndText::ReFitTextScroll(class String)` | `?ReFitTextScroll@RndText@@QAAXVString@@@Z` |
| `public: void __cdecl RndText::UpdateText(void)` | `?UpdateText@RndText@@QAAXXZ` |
| `void __cdecl ResetFontMapPageMeshFaces(class RndMesh *, int)` | `?ResetFontMapPageMeshFaces@@YAXPAVRndMesh@@H@Z` |

### `src/system/rndobj/Trans.cpp` (3)

| Function | Symbol |
|----------|--------|
| `bool __cdecl `anonymous namespace'::HorizontalCmp(class RndTransformable const *, class RndTransformable const *)` | `?HorizontalCmp@?A0xbfd9cae5@@YA_NPBVRndTransformable@@0@Z` |
| `bool __cdecl `anonymous namespace'::VerticalCmp(class RndTransformable const *, class RndTransformable const *)` | `?VerticalCmp@?A0xbfd9cae5@@YA_NPBVRndTransformable@@0@Z` |
| `private: void __cdecl RndTransformable::ApplyDynamicConstraint(void)` | `?ApplyDynamicConstraint@RndTransformable@@AAAXXZ` |

### `src/system/rndobj/Utl.cpp` (44)

| Function | Symbol |
|----------|--------|
| `char const * __cdecl CacheResource(char const *, class Hmx::Object const *)` | `?CacheResource@@YAPBDPBDPBVObject@Hmx@@@Z` |
| `class DataNode __cdecl GetNormalMapTextures(class ObjectDir *)` | `?GetNormalMapTextures@@YA?AVDataNode@@PAVObjectDir@@@Z` |
| `class DataNode __cdecl GetRenderTextures(class ObjectDir *)` | `?GetRenderTextures@@YA?AVDataNode@@PAVObjectDir@@@Z` |
| `class DataNode __cdecl GetRenderTexturesNoZ(class ObjectDir *)` | `?GetRenderTexturesNoZ@@YA?AVDataNode@@PAVObjectDir@@@Z` |
| `class DataNode __cdecl GetTexturesOfType(class ObjectDir *, enum RndTex::Type)` | `?GetTexturesOfType@@YA?AVDataNode@@PAVObjectDir@@W4Type@RndTex@@@Z` |
| `class DataNode __cdecl OnTestDrawGroups(class DataArray *)` | `?OnTestDrawGroups@@YA?AVDataNode@@PAVDataArray@@@Z` |
| `private: void __cdecl ObjDirItr<class RndDrawable>::Advance(void)` | `?Advance@?$ObjDirItr@VRndDrawable@@@@AAAXXZ` |
| `private: void __cdecl ObjDirItr<class RndMat>::Advance(void)` | `?Advance@?$ObjDirItr@VRndMat@@@@AAAXXZ` |
| `private: void __cdecl ObjDirItr<class RndMesh>::Advance(void)` | `?Advance@?$ObjDirItr@VRndMesh@@@@AAAXXZ` |
| `private: void __cdecl ObjDirItr<class RndTex>::Advance(void)` | `?Advance@?$ObjDirItr@VRndTex@@@@AAAXXZ` |
| `protected: class RndMesh::Face * __cdecl stlpmtx_std::vector<class RndMesh::Face, class stlpmtx_std::StlNodeAlloc<class RndMesh::Face> >::_M_erase(class RndMesh::Face *, struct stlpmtx_std::__false_type const &)` | `?_M_erase@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@IAAPAVFace@RndMesh@@PAV34@ABU__false_type@2@@Z` |
| `public: __cdecl ObjDirItr<class RndDrawable>::ObjDirItr<class RndDrawable>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VRndDrawable@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: __cdecl ObjDirItr<class RndMat>::ObjDirItr<class RndMat>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VRndMat@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: __cdecl ObjDirItr<class RndMesh>::ObjDirItr<class RndMesh>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VRndMesh@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: __cdecl ObjDirItr<class RndTex>::ObjDirItr<class RndTex>(class ObjectDir *, bool)` | `??0?$ObjDirItr@VRndTex@@@@QAA@PAVObjectDir@@_N@Z` |
| `public: class ObjDirItr<class RndDrawable> & __cdecl ObjDirItr<class RndDrawable>::operator++(void)` | `??E?$ObjDirItr@VRndDrawable@@@@QAAAAV0@XZ` |
| `public: class ObjDirItr<class RndMat> & __cdecl ObjDirItr<class RndMat>::operator++(void)` | `??E?$ObjDirItr@VRndMat@@@@QAAAAV0@XZ` |
| `public: class ObjDirItr<class RndMesh> & __cdecl ObjDirItr<class RndMesh>::operator++(void)` | `??E?$ObjDirItr@VRndMesh@@@@QAAAAV0@XZ` |
| `public: class ObjDirItr<class RndTex> & __cdecl ObjDirItr<class RndTex>::operator++(void)` | `??E?$ObjDirItr@VRndTex@@@@QAAAAV0@XZ` |
| `public: int __cdecl Keys<class Vector3, class Vector3>::AtFrame(float, class Key<class Vector3> const *&, class Key<class Vector3> const *&, float &) const` | `?AtFrame@?$Keys@VVector3@@V1@@@QBAHMAAPBV?$Key@VVector3@@@@0AAM@Z` |
| `public: virtual char const * __cdecl ResourceFileCacheHelper::CacheFile(char const *)` | `?CacheFile@ResourceFileCacheHelper@@UAAPBDPBD@Z` |
| `public: void __cdecl stlpmtx_std::vector<class RndMesh::Face, class stlpmtx_std::StlNodeAlloc<class RndMesh::Face> >::push_back(class RndMesh::Face const &)` | `?push_back@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@QAAXABVFace@RndMesh@@@Z` |
| `void __cdecl AttachMesh(class RndMesh *, class RndMesh *)` | `?AttachMesh@@YAXPAVRndMesh@@0@Z` |
| `void __cdecl BuildFromBSP(class RndMesh *)` | `?BuildFromBSP@@YAXPAVRndMesh@@@Z` |
| `void __cdecl BuildVisit(class BSPNode *)` | `?BuildVisit@@YAXPAVBSPNode@@@Z` |
| `void __cdecl BurnXfm(class RndMesh *, bool)` | `?BurnXfm@@YAXPAVRndMesh@@_N@Z` |
| `void __cdecl ComputeFaceTangentBasis(class RndMesh *, int, class Hmx::Matrix3&)` | `?ComputeFaceTangentBasis@@YAXPAVRndMesh@@HAAVMatrix3@Hmx@@@Z` |
| `void __cdecl ConvertBonesToTranses(class ObjectDir *, bool)` | `?ConvertBonesToTranses@@YAXPAVObjectDir@@_N@Z` |
| `void __cdecl DistributeXfms(class RndMultiMesh *, int, float)` | `?DistributeXfms@@YAXPAVRndMultiMesh@@HM@Z` |
| `void __cdecl FixVertOrder(class RndMesh const *, class RndMesh *)` | `?FixVertOrder@@YAXPBVRndMesh@@PAV1@@Z` |
| `void __cdecl MakeNormals(class RndMesh *)` | `?MakeNormals@@YAXPAVRndMesh@@@Z` |
| `void __cdecl MakeTangentsLate(class RndMesh *)` | `?MakeTangentsLate@@YAXPAVRndMesh@@@Z` |
| `void __cdecl MoveXfms(class RndMultiMesh *, class Vector3const &)` | `?MoveXfms@@YAXPAVRndMultiMesh@@ABVVector3@@@Z` |
| `void __cdecl RandomPointOnMesh(class RndMesh *, class Vector3&, class Vector3&)` | `?RandomPointOnMesh@@YAXPAVRndMesh@@AAVVector3@@1@Z` |
| `void __cdecl ResetNormals(class RndMesh *)` | `?ResetNormals@@YAXPAVRndMesh@@@Z` |
| `void __cdecl RndScaleObject(class Hmx::Object *, float, float)` | `?RndScaleObject@@YAXPAVObject@Hmx@@MM@Z` |
| `void __cdecl ScaleXfms(class RndMultiMesh *, class Vector3const &)` | `?ScaleXfms@@YAXPAVRndMultiMesh@@ABVVector3@@@Z` |
| `void __cdecl TessellateMesh(class RndMesh *)` | `?TessellateMesh@@YAXPAVRndMesh@@@Z` |
| `void __cdecl TestTexturePaths(class ObjectDir *)` | `?TestTexturePaths@@YAXPAVObjectDir@@@Z` |
| `void __cdecl TestTextureSize(class ObjectDir *, int, int, int, int, int)` | `?TestTextureSize@@YAXPAVObjectDir@@HHHHH@Z` |
| `void __cdecl UtilDrawCigar(class Transform const &, float const *const, float const *const, class Hmx::Color const &, int)` | `?UtilDrawCigar@@YAXABVTransform@@QBM1ABVColor@Hmx@@H@Z` |
| `void __cdecl UtilDrawCircle2D(class Vector2const &, float, class Hmx::Color const &, int)` | `?UtilDrawCircle2D@@YAXABVVector2@@MABVColor@Hmx@@H@Z` |
| `void __cdecl UtilDrawCylinder(class Transform const &, float, float, class Hmx::Color const &, int)` | `?UtilDrawCylinder@@YAXABVTransform@@MMABVColor@Hmx@@H@Z` |
| `void __cdecl UtilDrawPlane(class Plane const &, class Vector3const &, class Hmx::Color const &, int, float, bool)` | `?UtilDrawPlane@@YAXABVPlane@@ABVVector3@@ABVColor@Hmx@@HM_N@Z` |

### `src/system/rndobj/VelocityBuffer.cpp` (4)

| Function | Symbol |
|----------|--------|
| `private: bool __cdecl RndXfmCache::CacheXfms(unsigned int *, class RndMesh &volatile, unsigned int *, float &volatile, unsigned int, unsigned int &)` | `?CacheXfms@RndXfmCache@@AAA_NPIBVRndMesh@@PIBMIAAI@Z` |
| `private: bool __cdecl RndXfmCache::GetXfms(unsigned int *, class RndMesh &volatile, unsigned int, unsigned int, float const *&) const` | `?GetXfms@RndXfmCache@@ABA_NPIBVRndMesh@@IIAAPBM@Z` |
| `public: void __cdecl RndVelocityBuffer::CacheTransform(unsigned int *, class RndMesh &, unsigned int *, float &volatile, unsigned int)` | `?CacheTransform@RndVelocityBuffer@@QAAXPIAVRndMesh@@PIBMI@Z` |
| `public: void __cdecl RndVelocityBuffer::DrawMesh(class RndMesh *) const` | `?DrawMesh@RndVelocityBuffer@@QBAXPAVRndMesh@@@Z` |

### `src/system/rndobj/Wind.cpp` (2)

| Function | Symbol |
|----------|--------|
| `public: static class Hmx::Object * __cdecl RndWind::NewObject(void)` | `?NewObject@RndWind@@SAPAVObject@Hmx@@XZ` |
| `public: static void * __cdecl RndWind::operator new(unsigned int)` | `??2RndWind@@SAPAXI@Z` |

### `src/system/rndobj/wordwrap.cpp` (1)

| Function | Symbol |
|----------|--------|
| `bool __cdecl WordWrap_CanBreakLineAt(wchar_t const *, wchar_t const *)` | `?WordWrap_CanBreakLineAt@@YA_NPB_W0@Z` |
