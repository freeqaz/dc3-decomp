# Unimplemented Link Glue Stubs

Functions that exist in the original binary but are not in decomp source.
When a Matching TU's decomp `.obj` replaces the split `.obj`, these symbols
are missing from the decomp `.obj`, causing LNK2001 errors resolved by
ALTERNATENAME stubs in `src/link_glue.cpp`.

**Goal: implement these functions in decomp source and remove the stubs.**

## Summary

| Category | Count | Action |
|---|---:|---|
| Game/engine (in DB) | 585 | Implement in decomp source |
| Game/engine (not in DB) | 188 | Investigate, then implement or keep stub |
| Template instantiations | 397 | Resolve via header includes |
| Dynamic initializers | 236 | Auto-resolve when parent compiles |
| Static locals | 4 | Auto-resolve when parent function compiles |
| Anonymous namespace | 7 | Auto-resolve when parent compiles |
| CRT/compiler | 2 | Keep stub (unfixable) |
| SDK/XDK | 24 | Keep stub (external library) |
| SDK/Audio | 43 | Keep stub (external library) |
| **Total** | **1486** | |

## How to Query

### Database (`decomp.db`)

Stubs are marked with `is_stub = 1` in the `functions` table:

```sql
-- All stubs (1,037 in DB)
SELECT * FROM functions WHERE is_stub = 1;

-- Stubs by unit (most stubs first)
SELECT unit, COUNT(*) as cnt FROM functions WHERE is_stub = 1
GROUP BY unit ORDER BY cnt DESC;

-- Stubs for a specific class
SELECT demangled, size, unit FROM functions
WHERE is_stub = 1 AND demangled LIKE '%ClassName%';
```

Note: Many stubs show `verdict = 'COMPLETE'` and `current_percent = 100.0`
because objdiff reports `base_size=0` (function absent from decomp .obj) as
100% match. This is misleading — these functions are NOT in the decomp source.

### Grep (`link_glue.cpp`)

449 stubs are NOT in the database (templates, dynamic inits, SDK, etc.).
For the full picture, grep the source:

```bash
# Count all stubs
grep -c 'ALTERNATENAME:.*=__link_glue_noop' src/link_glue.cpp

# Find stubs for a class
grep 'ALTERNATENAME:.*ClassName.*=__link_glue_noop' src/link_glue.cpp
```

### Workflow

After implementing a function:
1. Remove its ALTERNATENAME line from `link_glue.cpp`
2. Run `ninja link` to verify
3. Update `decomp.db`: `UPDATE functions SET is_stub = 0 WHERE symbol = '...'`

---

## Game/Engine Functions by Unit

**585 functions** across **148 units**.

### `system/rndobj/Utl` (28 functions)

- [ ] `char const * __cdecl CacheResource(char const *, class Hmx::Object const *)` (324B) — `?CacheResource@@YAPBDPBDPBVObject@Hmx@@@Z`
- [ ] `class DataNode __cdecl GetNormalMapTextures(class ObjectDir *)` (436B) — `?GetNormalMapTextures@@YA?AVDataNode@@PAVObjectDir@@@Z`
- [ ] `class DataNode __cdecl GetRenderTextures(class ObjectDir *)` (52B) — `?GetRenderTextures@@YA?AVDataNode@@PAVObjectDir@@@Z`
- [ ] `class DataNode __cdecl GetRenderTexturesNoZ(class ObjectDir *)` (52B) — `?GetRenderTexturesNoZ@@YA?AVDataNode@@PAVObjectDir@@@Z`
- [ ] `class DataNode __cdecl OnTestDrawGroups(class DataArray *)` (740B) — `?OnTestDrawGroups@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `void __cdecl AttachMesh(class RndMesh *, class RndMesh *)` (648B) — `?AttachMesh@@YAXPAVRndMesh@@0@Z`
- [ ] `void __cdecl BuildFromBSP(class RndMesh *)` (552B) — `?BuildFromBSP@@YAXPAVRndMesh@@@Z`
- [ ] `void __cdecl BurnXfm(class RndMesh *, bool)` (672B) — `?BurnXfm@@YAXPAVRndMesh@@_N@Z`
- [ ] `void __cdecl ConvertBonesToTranses(class ObjectDir *, bool)` (732B) — `?ConvertBonesToTranses@@YAXPAVObjectDir@@_N@Z`
- [ ] `void __cdecl DistributeXfms(class RndMultiMesh *, int, float)` (176B) — `?DistributeXfms@@YAXPAVRndMultiMesh@@HM@Z`
- [ ] `void __cdecl FixVertOrder(class RndMesh const *, class RndMesh *)` (536B) — `?FixVertOrder@@YAXPBVRndMesh@@PAV1@@Z`
- [ ] `void __cdecl MakeTangentsLate(class RndMesh *)` (1040B) — `?MakeTangentsLate@@YAXPAVRndMesh@@@Z`
- [ ] `void __cdecl MoveXfms(class RndMultiMesh *, class Vector3const &)` (80B) — `?MoveXfms@@YAXPAVRndMultiMesh@@ABVVector3@@@Z`
- [ ] `void __cdecl RandomXfms(class RndMultiMesh *)` (328B) — `?RandomXfms@@YAXPAVRndMultiMesh@@@Z`
- [ ] `void __cdecl ResetNormals(class RndMesh *)` (1980B) — `?ResetNormals@@YAXPAVRndMesh@@@Z`
- [ ] `void __cdecl RndScaleObject(class Hmx::Object *, float, float)` (2928B) — `?RndScaleObject@@YAXPAVObject@Hmx@@MM@Z`
- [ ] `void __cdecl ScaleXfms(class RndMultiMesh *, class Vector3const &)` (160B) — `?ScaleXfms@@YAXPAVRndMultiMesh@@ABVVector3@@@Z`
- [ ] `void __cdecl SetBloomBlurWeights(bool, float, float)` (248B) — `?SetBloomBlurWeights@@YAX_NMM@Z`
- [ ] `void __cdecl SetBloomBlurWeightsStreak(bool, float, float, float, int, float)` (712B) — `?SetBloomBlurWeightsStreak@@YAX_NMMMHM@Z`
- [ ] `void __cdecl TessellateMesh(class RndMesh *)` (1176B) — `?TessellateMesh@@YAXPAVRndMesh@@@Z`
- [ ] `void __cdecl TestMaterialTextures(class ObjectDir *)` (232B) — `?TestMaterialTextures@@YAXPAVObjectDir@@@Z`
- [ ] `void __cdecl TestTexturePaths(class ObjectDir *)` (672B) — `?TestTexturePaths@@YAXPAVObjectDir@@@Z`
- [ ] `void __cdecl TestTextureSize(class ObjectDir *, int, int, int, int, int)` (448B) — `?TestTextureSize@@YAXPAVObjectDir@@HHHHH@Z`
- [ ] `void __cdecl UtilDrawCigar(class Transform const &, float const *const, float const *const, class Hmx::Color const &, int)` (872B) — `?UtilDrawCigar@@YAXABVTransform@@QBM1ABVColor@Hmx@@H@Z`
- [ ] `void __cdecl UtilDrawCircle2D(class Vector2const &, float, class Hmx::Color const &, int)` (332B) — `?UtilDrawCircle2D@@YAXABVVector2@@MABVColor@Hmx@@H@Z`
- [ ] `void __cdecl UtilDrawCylinder(class Transform const &, float, float, class Hmx::Color const &, int)` (576B) — `?UtilDrawCylinder@@YAXABVTransform@@MMABVColor@Hmx@@H@Z`
- [ ] `void __cdecl UtilDrawPlane(class Plane const &, class Vector3const &, class Hmx::Color const &, int, float, bool)` (760B) — `?UtilDrawPlane@@YAXABVPlane@@ABVVector3@@ABVColor@Hmx@@HM_N@Z`
- [ ] `void __cdecl UtilDrawSphere(class Vector3const &, float, class Hmx::Color const &, class RndMat *)` (576B) — `?UtilDrawSphere@@YAXABVVector3@@MABVColor@Hmx@@PAVRndMat@@@Z`

### `system/os/PlatformMgr_Xbox` (25 functions)

- [ ] `private: class DataNode __cdecl PlatformMgr::OnSignInUsers(class DataArray const *)` (132B) — `?OnSignInUsers@PlatformMgr@@AAA?AVDataNode@@PBVDataArray@@@Z`
- [ ] `public: __cdecl SingleItemEnumJob::SingleItemEnumJob(class Hmx::Object *, int, unsigned __int64)` (88B) — `??0SingleItemEnumJob@@QAA@PAVObject@Hmx@@H_K@Z`
- [ ] `public: bool __cdecl PlatformMgr::GetServiceID(class String const &, unsigned int &)` (72B) — `?GetServiceID@PlatformMgr@@QAA_NABVString@@AAI@Z`
- [ ] `public: bool __cdecl PlatformMgr::HasOnlinePrivilege(int) const` (280B) — `?HasOnlinePrivilege@PlatformMgr@@QBA_NH@Z`
- [ ] `public: bool __cdecl PlatformMgr::IsInParty(void)` (136B) — `?IsInParty@PlatformMgr@@QAA_NXZ`
- [ ] `public: bool __cdecl PlatformMgr::IsInPartyWithOthers(void)` (72B) — `?IsInPartyWithOthers@PlatformMgr@@QAA_NXZ`
- [ ] `public: bool __cdecl PlatformMgr::PollXSocialCapabilities(void)` (256B) — `?PollXSocialCapabilities@PlatformMgr@@QAA_NXZ`
- [ ] `public: bool __cdecl PlatformMgr::QueryXSocialCapabilities(void)` (352B) — `?QueryXSocialCapabilities@PlatformMgr@@QAA_NXZ`
- [ ] `public: char const * __cdecl PlatformMgr::GetName(int) const` (192B) — `?GetName@PlatformMgr@@QBAPBDH@Z`
- [ ] `public: enum ShowGamercardResult __cdecl PlatformMgr::ShowGamercardForPadNum(int, class OnlineID const *)` (392B) — `?ShowGamercardForPadNum@PlatformMgr@@QAA?AW4ShowGamercardResult@@HPBVOnlineID@@@Z`
- [ ] `public: int __cdecl PlatformMgr::GetOwnerOfGuest(int)` (300B) — `?GetOwnerOfGuest@PlatformMgr@@QAAHH@Z`
- [ ] `public: virtual __cdecl PlatformMgr::~PlatformMgr(void)` (200B) — `??1PlatformMgr@@UAA@XZ`
- [ ] `public: virtual __cdecl SingleItemEnumJob::~SingleItemEnumJob(void)` (172B) — `??1SingleItemEnumJob@@UAA@XZ`
- [ ] `public: virtual bool __cdecl SingleItemEnumJob::IsFinished(void)` (72B) — `?IsFinished@SingleItemEnumJob@@UAA_NXZ`
- [ ] `public: virtual void __cdecl SingleItemEnumJob::Cancel(class Hmx::Object *)` (76B) — `?Cancel@SingleItemEnumJob@@UAAXPAVObject@Hmx@@@Z`
- [ ] `public: virtual void __cdecl SingleItemEnumJob::OnCompletion(class Hmx::Object *)` (264B) — `?OnCompletion@SingleItemEnumJob@@UAAXPAVObject@Hmx@@@Z`
- [ ] `public: virtual void __cdecl SingleItemEnumJob::Start(void)` (264B) — `?Start@SingleItemEnumJob@@UAAXXZ`
- [ ] `public: void __cdecl PlatformMgr::Init(void)` (352B) — `?Init@PlatformMgr@@QAAXXZ`
- [ ] `public: void __cdecl PlatformMgr::InviteParty(int)` (156B) — `?InviteParty@PlatformMgr@@QAAXH@Z`
- [ ] `public: void __cdecl PlatformMgr::Poll(void)` (4844B) — `?Poll@PlatformMgr@@QAAXXZ`
- [ ] `public: void __cdecl PlatformMgr::PreInit(void)` (4B) — `?PreInit@PlatformMgr@@QAAXXZ`
- [ ] `public: void __cdecl PlatformMgr::SetNotifyUILocation(enum NotifyLocation)` (100B) — `?SetNotifyUILocation@PlatformMgr@@QAAXW4NotifyLocation@@@Z`
- [ ] `public: void __cdecl PlatformMgr::SetPadProperty(int, int, unsigned short const *) const` (104B) — `?SetPadProperty@PlatformMgr@@QBAXHHPBG@Z`
- [ ] `public: void __cdecl PlatformMgr::ShowControllerRequiredUI(class Hmx::Object *)` (264B) — `?ShowControllerRequiredUI@PlatformMgr@@QAAXPAVObject@Hmx@@@Z`
- [ ] `public: void __cdecl PlatformMgr::SignInUsers(int, unsigned long)` (188B) — `?SignInUsers@PlatformMgr@@QAAXHK@Z`

### `system/rndobj/Shader` (24 functions)

- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderDepthVolume::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (128B) — `?CalcShaderOpts@RndShaderDepthVolume@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderDrawRect::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (220B) — `?CalcShaderOpts@RndShaderDrawRect@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderFur::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (1100B) — `?CalcShaderOpts@RndShaderFur@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderMultimesh::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (1668B) — `?CalcShaderOpts@RndShaderMultimesh@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderParticles::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (640B) — `?CalcShaderOpts@RndShaderParticles@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderPostProc::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (696B) — `?CalcShaderOpts@RndShaderPostProc@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderSimple::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (304B) — `?CalcShaderOpts@RndShaderSimple@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderStandard::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (2100B) — `?CalcShaderOpts@RndShaderStandard@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderSyncTrack::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (2012B) — `?CalcShaderOpts@RndShaderSyncTrack@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderUnwrapUV::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (60B) — `?CalcShaderOpts@RndShaderUnwrapUV@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderVelocity::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (56B) — `?CalcShaderOpts@RndShaderVelocity@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual unsigned __int64 __cdecl RndShaderVelocityCamera::CalcShaderOpts(class NgMat *, enum ShaderType, bool)` (32B) — `?CalcShaderOpts@RndShaderVelocityCamera@@MAA_KPAVNgMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderDepthVolume::Select(class RndMat *, enum ShaderType, bool)` (464B) — `?Select@RndShaderDepthVolume@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderDrawRect::Select(class RndMat *, enum ShaderType, bool)` (352B) — `?Select@RndShaderDrawRect@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderFur::Select(class RndMat *, enum ShaderType, bool)` (248B) — `?Select@RndShaderFur@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderMultimesh::Select(class RndMat *, enum ShaderType, bool)` (248B) — `?Select@RndShaderMultimesh@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderParticles::Select(class RndMat *, enum ShaderType, bool)` (216B) — `?Select@RndShaderParticles@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderPostProc::Select(class RndMat *, enum ShaderType, bool)` (224B) — `?Select@RndShaderPostProc@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderStandard::Select(class RndMat *, enum ShaderType, bool)` (360B) — `?Select@RndShaderStandard@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderSyncTrack::Select(class RndMat *, enum ShaderType, bool)` (344B) — `?Select@RndShaderSyncTrack@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderUnwrapUV::Select(class RndMat *, enum ShaderType, bool)` (360B) — `?Select@RndShaderUnwrapUV@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderVelocity::Select(class RndMat *, enum ShaderType, bool)` (244B) — `?Select@RndShaderVelocity@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `protected: virtual void __cdecl RndShaderVelocityCamera::Select(class RndMat *, enum ShaderType, bool)` (228B) — `?Select@RndShaderVelocityCamera@@MAAXPAVRndMat@@W4ShaderType@@_N@Z`
- [ ] `void __cdecl SetColorWriteMask(struct ShaderOptions const &, class RndMat *)` (180B) — `?SetColorWriteMask@@YAXABUShaderOptions@@PAVRndMat@@@Z`

### `system/hamobj/HamNavList` (23 functions)

- [ ] `private: class DataNode __cdecl HamNavList::OnMsg(class ButtonDownMsg const &)` (488B) — `?OnMsg@HamNavList@@AAA?AVDataNode@@ABVButtonDownMsg@@@Z`
- [ ] `private: float __cdecl HamNavList::GetTargetSwellAmount(int)` (424B) — `?GetTargetSwellAmount@HamNavList@@AAAMH@Z`
- [ ] `private: int __cdecl HamNavList::GetDisabledCount(int) const` (256B) — `?GetDisabledCount@HamNavList@@ABAHH@Z`
- [ ] `private: void __cdecl HamNavList::DetermineHighlightedItem(void)` (712B) — `?DetermineHighlightedItem@HamNavList@@AAAXXZ`
- [ ] `private: void __cdecl HamNavList::RealRefresh(void)` (724B) — `?RealRefresh@HamNavList@@AAAXXZ`
- [ ] `private: void __cdecl HamNavList::SetRibbonMode(enum HamListRibbon::RibbonMode)` (412B) — `?SetRibbonMode@HamNavList@@AAAXW4RibbonMode@HamListRibbon@@@Z`
- [ ] `private: void __cdecl HamNavList::SetSelecting(bool)` (888B) — `?SetSelecting@HamNavList@@AAAX_N@Z`
- [ ] `protected: void __cdecl HamNavList::Update(void)` (640B) — `?Update@HamNavList@@IAAXXZ`
- [ ] `public: __cdecl HamListRibbonDrawState::HamListRibbonDrawState(void)` (108B) — `??0HamListRibbonDrawState@@QAA@XZ`
- [ ] `public: char const * __cdecl ResourceDirPtr<class UILabelDir>::GetName(void) const` (76B) — `?GetName@?$ResourceDirPtr@VUILabelDir@@@@QBAPBDXZ`
- [ ] `public: float __cdecl HamNavList::CalculateSwell(int) const` (208B) — `?CalculateSwell@HamNavList@@QBAMH@Z`
- [ ] `public: virtual void __cdecl HamNavList::Clear(void)` (68B) — `?Clear@HamNavList@@UAAXXZ`
- [ ] `public: virtual void __cdecl HamNavList::CompleteScroll(class UIListState const &)` (88B) — `?CompleteScroll@HamNavList@@UAAXABVUIListState@@@Z`
- [ ] `public: virtual void __cdecl HamNavList::DrawShowing(void)` (1028B) — `?DrawShowing@HamNavList@@UAAXXZ`
- [ ] `public: virtual void __cdecl HamNavList::Enter(void)` (496B) — `?Enter@HamNavList@@UAAXXZ`
- [ ] `public: virtual void __cdecl HamNavList::Exit(void)` (252B) — `?Exit@HamNavList@@UAAXXZ`
- [ ] `public: virtual void __cdecl HamNavList::PostUpdate(struct SkeletonUpdateData const *)` (264B) — `?PostUpdate@HamNavList@@UAAXPBUSkeletonUpdateData@@@Z`
- [ ] `public: void __cdecl HamNavList::ClearBigElements(void)` (80B) — `?ClearBigElements@HamNavList@@QAAXXZ`
- [ ] `public: void __cdecl HamNavList::Disengage(void)` (156B) — `?Disengage@HamNavList@@QAAXXZ`
- [ ] `public: void __cdecl HamNavList::DrawDebug(void) const` (612B) — `?DrawDebug@HamNavList@@QBAXXZ`
- [ ] `public: void __cdecl HamNavList::PlayEnterAnim(void)` (436B) — `?PlayEnterAnim@HamNavList@@QAAXXZ`
- [ ] `public: void __cdecl HamNavList::ScrollToIndex(int, int)` (140B) — `?ScrollToIndex@HamNavList@@QAAXHH@Z`
- [ ] `public: void __cdecl HamNavList::SetNavProvider(class HamNavProvider *)` (104B) — `?SetNavProvider@HamNavList@@QAAXPAVHamNavProvider@@@Z`

### `system/synth_xbox/Mic` (18 functions)

- [ ] `class DataNode __cdecl SetLocalGain(class DataArray *)` (72B) — `?SetLocalGain@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `class DataNode __cdecl SetLowCut(class DataArray *)` (72B) — `?SetLowCut@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `class DataNode __cdecl SetNoiseGate(class DataArray *)` (112B) — `?SetNoiseGate@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `class DataNode __cdecl SetRemoteGain(class DataArray *)` (84B) — `?SetRemoteGain@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `public: static class MicManagerXbox * __cdecl MicManagerXbox::GetInstance(void)` (100B) — `?GetInstance@MicManagerXbox@@SAPAV1@XZ`
- [ ] `public: virtual enum Mic::Type __cdecl MicXbox::GetType(void) const` (52B) — `?GetType@MicXbox@@UBA?AW4Type@Mic@@XZ`
- [ ] `public: virtual int __cdecl MicNull::GetSampleRate(void) const` (12B) — `?GetSampleRate@MicNull@@UBAHXZ`
- [ ] `public: virtual short * __cdecl MicXbox::GetContinuousBuf(int &)` (104B) — `?GetContinuousBuf@MicXbox@@UAAPAFAAH@Z`
- [ ] `public: virtual short * __cdecl MicXbox::GetRecentBuf(int &)` (104B) — `?GetRecentBuf@MicXbox@@UAAPAFAAH@Z`
- [ ] `public: virtual void __cdecl MicXbox::SetGain(float)` (48B) — `?SetGain@MicXbox@@UAAXM@Z`
- [ ] `public: virtual void __cdecl MicXbox::StartPlayback(void)` (416B) — `?StartPlayback@MicXbox@@UAAXXZ`
- [ ] `public: virtual void __cdecl MicXbox::StopPlayback(void)` (112B) — `?StopPlayback@MicXbox@@UAAXXZ`
- [ ] `public: void __cdecl MicManagerXbox::AddMic(class MicXbox *)` (112B) — `?AddMic@MicManagerXbox@@QAAXPAVMicXbox@@@Z`
- [ ] `public: void __cdecl MicManagerXbox::Init(void)` (652B) — `?Init@MicManagerXbox@@QAAXXZ`
- [ ] `public: void __cdecl MicManagerXbox::Poll(void)` (524B) — `?Poll@MicManagerXbox@@QAAXXZ`
- [ ] `public: void __cdecl MicManagerXbox::RemoveMic(class MicXbox *)` (124B) — `?RemoveMic@MicManagerXbox@@QAAXPAVMicXbox@@@Z`
- [ ] `public: void __cdecl MicManagerXbox::Shutdown(void)` (216B) — `?Shutdown@MicManagerXbox@@QAAXXZ`
- [ ] `public: void __cdecl MicXbox::AddData(void *, int)` (528B) — `?AddData@MicXbox@@QAAXPAXH@Z`

### `system/rndobj/Text` (16 functions)

- [ ] `protected: static class RndText::FontMapBase * __cdecl RndText::AcquireFontMap(class RndFontBase *)` (608B) — `?AcquireFontMap@RndText@@KAPAVFontMapBase@1@PAVRndFontBase@@@Z`
- [ ] `public: float __cdecl RndText::ComputeCharWidthsForText(class String)` (192B) — `?ComputeCharWidthsForText@RndText@@QAAMVString@@@Z`
- [ ] `public: static void __cdecl RndText::ClearBlacklight(void)` (16B) — `?ClearBlacklight@RndText@@SAXXZ`
- [ ] `public: static void __cdecl RndText::DrawBlacklight(void)` (228B) — `?DrawBlacklight@RndText@@SAXXZ`
- [ ] `public: virtual bool __cdecl RndText::MakeWorldSphere(class Sphere &, bool)` (344B) — `?MakeWorldSphere@RndText@@UAA_NAAVSphere@@_N@Z`
- [ ] `public: virtual float __cdecl RndText::GetDistanceToPlane(class Plane const &, class Vector3&)` (300B) — `?GetDistanceToPlane@RndText@@UAAMABVPlane@@AAVVector3@@@Z`
- [ ] `public: virtual void __cdecl RndText::DrawShowing(void)` (928B) — `?DrawShowing@RndText@@UAAXXZ`
- [ ] `public: virtual void __cdecl RndText::FontMap3d::AllocateMeshes(class RndText *, int)` (452B) — `?AllocateMeshes@FontMap3d@RndText@@UAAXPAV2@H@Z`
- [ ] `public: virtual void __cdecl RndText::FontMap3d::CleanupSyncMeshes(void)` (100B) — `?CleanupSyncMeshes@FontMap3d@RndText@@UAAXXZ`
- [ ] `public: virtual void __cdecl RndText::FontMap3d::IncrementDisplayableChars(unsigned short)` (80B) — `?IncrementDisplayableChars@FontMap3d@RndText@@UAAXG@Z`
- [ ] `public: virtual void __cdecl RndText::FontMap3d::SetupCharacter(unsigned short, float &, float, class RndText::StyleState const &, unsigned short, float, enum RndText::FitType, float)` (596B) — `?SetupCharacter@FontMap3d@RndText@@UAAXGAAMMABVStyleState@2@GMW4FitType@2@M@Z`
- [ ] `public: virtual void __cdecl RndText::FontMap::SetupCharacter(unsigned short, float &, float, class RndText::StyleState const &, unsigned short, float, enum RndText::FitType, float)` (1016B) — `?SetupCharacter@FontMap@RndText@@UAAXGAAMMABVStyleState@2@GMW4FitType@2@M@Z`
- [ ] `public: void __cdecl RndText::GetWidthHeightBox(class Box &) const` (160B) — `?GetWidthHeightBox@RndText@@QBAXAAVBox@@@Z`
- [ ] `public: void __cdecl RndText::ReFitTextScroll(class String)` (312B) — `?ReFitTextScroll@RndText@@QAAXVString@@@Z`
- [ ] `public: void __cdecl RndText::UpdateText(void)` (664B) — `?UpdateText@RndText@@QAAXXZ`
- [ ] `void __cdecl ResetFontMapPageMeshFaces(class RndMesh *, int)` (316B) — `?ResetFontMapPageMeshFaces@@YAXPAVRndMesh@@H@Z`

### `system/gesture/LiveCameraInput` (15 functions)

- [ ] `class DataNode __cdecl OnCameraDebugDepth(class DataArray *)` (36B) — `?OnCameraDebugDepth@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `class DataNode __cdecl OnCameraDumpUnique(class DataArray *)` (76B) — `?OnCameraDumpUnique@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `public: bool __cdecl LiveCameraInput::GetAutoexposure(void) const` (48B) — `?GetAutoexposure@LiveCameraInput@@QBA_NXZ`
- [ ] `public: bool __cdecl LiveCameraInput::GetTweakedAutoexposure(void) const` (328B) — `?GetTweakedAutoexposure@LiveCameraInput@@QBA_NXZ`
- [ ] `public: bool __cdecl LiveCameraInput::SetAutoexposure(bool)` (64B) — `?SetAutoexposure@LiveCameraInput@@QAA_N_N@Z`
- [ ] `public: bool __cdecl LiveCameraInput::SetTweakedAutoexposure(bool)` (236B) — `?SetTweakedAutoexposure@LiveCameraInput@@QAA_N_N@Z`
- [ ] `public: static void __cdecl LiveCameraInput::NuiAudioDataCallback(struct _NUIAUDIO_RESULTS *)` (260B) — `?NuiAudioDataCallback@LiveCameraInput@@SAXPAU_NUIAUDIO_RESULTS@@@Z`
- [ ] `public: void __cdecl CamTexClip::StoreTextureClip(class RndTex *, float, float, float, float)` (296B) — `?StoreTextureClip@CamTexClip@@QAAXPAVRndTex@@MMMM@Z`
- [ ] `public: void __cdecl LiveCameraInput::ClearSnapshots(void)` (228B) — `?ClearSnapshots@LiveCameraInput@@QAAXXZ`
- [ ] `public: void __cdecl LiveCameraInput::SetExposureRegion(float, float, float, float)` (476B) — `?SetExposureRegion@LiveCameraInput@@QAAXMMMM@Z`
- [ ] `public: void __cdecl LiveCameraInput::SetTrackedSkeletons(int, int) const` (44B) — `?SetTrackedSkeletons@LiveCameraInput@@QBAXHH@Z`
- [ ] `public: void __cdecl LiveCameraInput::TextureStore::StoreColorBufferClip(class LiveCameraInput *, float, float, float, float)` (508B) — `?StoreColorBufferClip@TextureStore@LiveCameraInput@@QAAXPAV2@MMMM@Z`
- [ ] `public: void __cdecl LiveCameraInput::TextureStore::StoreDepthBufferClip(class LiveCameraInput *, float, float, float, float)` (508B) — `?StoreDepthBufferClip@TextureStore@LiveCameraInput@@QAAXPAV2@MMMM@Z`
- [ ] `public: void __cdecl LiveCameraInput::TextureStore::UpdateFromColorBuffer(class LiveCameraInput *)` (296B) — `?UpdateFromColorBuffer@TextureStore@LiveCameraInput@@QAAXPAV2@@Z`
- [ ] `public: void __cdecl LiveCameraInput::TextureStore::UpdateFromDepthBuffer(class LiveCameraInput *)` (332B) — `?UpdateFromDepthBuffer@TextureStore@LiveCameraInput@@QAAXPAV2@@Z`

### `system/synth_xbox/Voice` (14 functions)

- [ ] `private: void __cdecl Voice::UpdateMix(void)` (1752B) — `?UpdateMix@Voice@@AAAXXZ`
- [ ] `private: void __cdecl Voice::UpdateSends(void)` (340B) — `?UpdateSends@Voice@@AAAXXZ`
- [ ] `public: bool __cdecl Voice::IsPlaying(void)` (444B) — `?IsPlaying@Voice@@QAA_NXZ`
- [ ] `public: int __cdecl Voice::GetAddr(void)` (196B) — `?GetAddr@Voice@@QAAHXZ`
- [ ] `public: static bool __cdecl Voice::HasPendingVoices(void)` (172B) — `?HasPendingVoices@Voice@@SA_NXZ`
- [ ] `public: void __cdecl Voice::Pause(bool)` (344B) — `?Pause@Voice@@QAAX_N@Z`
- [ ] `public: void __cdecl Voice::SetSend(class FxSend360*)` (20B) — `?SetSend@Voice@@QAAXPAVFxSend360@@@Z`
- [ ] `public: void __cdecl Voice::SetSpeed(float)` (272B) — `?SetSpeed@Voice@@QAAXM@Z`
- [ ] `public: void __cdecl Voice::Stop(bool)` (268B) — `?Stop@Voice@@QAAX_N@Z`
- [ ] `public: void __cdecl Voice::blockingStart(bool)` (220B) — `?blockingStart@Voice@@QAAX_N@Z`
- [ ] `unsigned long __cdecl StartVoiceThreadEntry(void *)` (1168B) — `?StartVoiceThreadEntry@@YAKPAX@Z`
- [ ] `void __cdecl StartSynchronizedVoices(void)` (120B) — `?StartSynchronizedVoices@@YAXXZ`
- [ ] `void __cdecl StopSynchronizedVoices(void)` (152B) — `?StopSynchronizedVoices@@YAXXZ`
- [ ] `void __cdecl TerminateVoiceThread(void)` (100B) — `?TerminateVoiceThread@@YAXXZ`

### `system/rnddx9/ShaderMgr` (13 functions)

- [ ] `public: class SongInfo * __cdecl SongMetadata::SongBlock(void) const` (8B) — `?SongBlock@SongMetadata@@QBAPAVSongInfo@@XZ`
- [ ] `public: virtual unsigned int __cdecl LEAPCORE::CXboxRendererConnection::GetLatency(void) const` (8B) — `?GetLatency@CXboxRendererConnection@LEAPCORE@@UBAIXZ`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetPConstant(enum PShaderConstant, bool)` (60B) — `?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@_N@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetPConstant(enum PShaderConstant, class Hmx::Matrix4const &)` (220B) — `?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@ABVMatrix4@Hmx@@@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetPConstant(enum PShaderConstant, class RndCubeTex *)` (96B) — `?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@PAVRndCubeTex@@@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetPConstant(enum PShaderConstant, class Vector4const &)` (88B) — `?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@ABVVector4@@@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetPConstant(enum PShaderConstant, int)` (56B) — `?SetPConstant@DxShaderMgr@@UAAXW4PShaderConstant@@H@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetPConstant4x3(enum PShaderConstant, class Hmx::Matrix4const &)` (172B) — `?SetPConstant4x3@DxShaderMgr@@UAAXW4PShaderConstant@@ABVMatrix4@Hmx@@@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetVConstant(enum VShaderConstant, bool)` (60B) — `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@_N@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetVConstant(enum VShaderConstant, class Hmx::Matrix4const &)` (220B) — `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@ABVMatrix4@Hmx@@@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetVConstant(enum VShaderConstant, class Vector4const &)` (88B) — `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@ABVVector4@@@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetVConstant(enum VShaderConstant, int)` (56B) — `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@H@Z`
- [ ] `public: virtual void __cdecl DxShaderMgr::SetVConstant4x3(enum VShaderConstant, class Hmx::Matrix4const &)` (172B) — `?SetVConstant4x3@DxShaderMgr@@UAAXW4VShaderConstant@@ABVMatrix4@Hmx@@@Z`

### `system/rnddx9/Tex` (12 functions)

- [ ] `XGHierarchicalZSize` (60B) — `XGHierarchicalZSize`
- [ ] `XGSurfaceSize` (112B) — `XGSurfaceSize`
- [ ] `public: char const * __cdecl DingoJob::GetResponseString(void)` (8B) — `?GetResponseString@DingoJob@@QAAPBDXZ`
- [ ] `public: static void __cdecl DxTex::Init(void)` (60B) — `?Init@DxTex@@SAXXZ`
- [ ] `public: virtual bool __cdecl DxTex::TexelsLock(void *&)` (120B) — `?TexelsLock@DxTex@@UAA_NAAPAX@Z`
- [ ] `public: virtual void __cdecl DxTex::FinishDrawTarget(void)` (148B) — `?FinishDrawTarget@DxTex@@UAAXXZ`
- [ ] `public: virtual void __cdecl DxTex::MakeDrawTarget(void)` (240B) — `?MakeDrawTarget@DxTex@@UAAXXZ`
- [ ] `public: virtual void __cdecl DxTex::Select(int)` (192B) — `?Select@DxTex@@UAAXH@Z`
- [ ] `public: virtual void __cdecl DxTex::UnlockBitmap(void)` (224B) — `?UnlockBitmap@DxTex@@UAAXXZ`
- [ ] `public: void * __cdecl DxTex::StartCompress(enum RndTex::AlphaCompress)` (752B) — `?StartCompress@DxTex@@QAAPAXW4AlphaCompress@RndTex@@@Z`
- [ ] `public: void __cdecl DxTex::DoCompress(void *)` (284B) — `?DoCompress@DxTex@@QAAXPAX@Z`
- [ ] `public: void __cdecl DxTex::FinishCompress(void *)` (284B) — `?FinishCompress@DxTex@@QAAXPAX@Z`

### `system/char/CharEyes` (10 functions)

- [ ] `float __cdecl pow(float, float)` (36B) — `?pow@@YAMMM@Z`
- [ ] `protected: bool __cdecl CharEyes::EyesOnTarget(float)` (356B) — `?EyesOnTarget@CharEyes@@IAA_NM@Z`
- [ ] `protected: class DataNode __cdecl CharEyes::OnAddInterest(class DataArray *)` (168B) — `?OnAddInterest@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: class DataNode __cdecl CharEyes::OnToggleForceFocus(class DataArray *)` (96B) — `?OnToggleForceFocus@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: class DataNode __cdecl CharEyes::OnToggleInterestOverlay(class DataArray *)` (104B) — `?OnToggleInterestOverlay@CharEyes@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: class Vector3 __cdecl CharEyes::GenerateDartOffset(void)` (392B) — `?GenerateDartOffset@CharEyes@@IAA?AVVector3@@XZ`
- [ ] `public: virtual void __cdecl CharEyes::Exit(void)` (196B) — `?Exit@CharEyes@@UAAXXZ`
- [ ] `public: virtual void __cdecl CharEyes::Highlight(void)` (2184B) — `?Highlight@CharEyes@@UAAXXZ`
- [ ] `public: virtual void __cdecl CharEyes::Poll(void)` (1704B) — `?Poll@CharEyes@@UAAXXZ`
- [ ] `public: void __cdecl CharEyes::AddInterestObject(class CharInterest *)` (156B) — `?AddInterestObject@CharEyes@@QAAXPAVCharInterest@@@Z`

### `system/rndobj/Rnd` (9 functions)

- [ ] `protected: class DataNode __cdecl Rnd::OnToggleHeap(class DataArray const *)` (124B) — `?OnToggleHeap@Rnd@@IAA?AVDataNode@@PBVDataArray@@@Z`
- [ ] `protected: class RndTex * __cdecl Rnd::CreateDefaultTexture(enum Rnd::DefaultTextureType)` (880B) — `?CreateDefaultTexture@Rnd@@IAAPAVRndTex@@W4DefaultTextureType@1@@Z`
- [ ] `protected: float __cdecl Rnd::DrawTimers(float)` (1104B) — `?DrawTimers@Rnd@@IAAMM@Z`
- [ ] `protected: virtual void __cdecl Rnd::DrawPreClear(void)` (768B) — `?DrawPreClear@Rnd@@MAAXXZ`
- [ ] `protected: void __cdecl Rnd::UpdateRate(void)` (424B) — `?UpdateRate@Rnd@@IAAXXZ`
- [ ] `public: virtual __cdecl ModalKeyListener::~ModalKeyListener(void)` (4B) — `??1ModalKeyListener@@UAA@XZ`
- [ ] `public: void __cdecl Rnd::Modal(enum Debug::ModalType &, class FixedString &, bool)` (772B) — `?Modal@Rnd@@QAAXAAW4ModalType@Debug@@AAVFixedString@@_N@Z`
- [ ] `public: void __cdecl Rnd::TestPoint(class Vector3const &, class RndFlare *)` (504B) — `?TestPoint@Rnd@@QAAXABVVector3@@PAVRndFlare@@@Z`
- [ ] `unsigned long __cdecl CompressThread(void *)` (100B) — `?CompressThread@@YAKPAX@Z`

### `lazer/meta_ham/HamStorePanel` (9 functions)

- [ ] `protected: bool __cdecl HamStorePanel::BuySpecialOffer(class Symbol)` (304B) — `?BuySpecialOffer@HamStorePanel@@IAA_NVSymbol@@@Z`
- [ ] `protected: bool __cdecl HamStorePanel::IsSpecialOfferOwned(class Symbol) const` (112B) — `?IsSpecialOfferOwned@HamStorePanel@@IBA_NVSymbol@@@Z`
- [ ] `protected: class DataNode __cdecl HamStorePanel::OnMsg(class RCJobCompleteMsg const &)` (832B) — `?OnMsg@HamStorePanel@@IAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z`
- [ ] `protected: void __cdecl HamStorePanel::RefreshSpecialOfferStatus(void)` (176B) — `?RefreshSpecialOfferStatus@HamStorePanel@@IAAXXZ`
- [ ] `public: virtual bool __cdecl HamStorePanel::ContentDiscovered(class Symbol)` (52B) — `?ContentDiscovered@HamStorePanel@@UAA_NVSymbol@@@Z`
- [ ] `public: virtual bool __cdecl HamStorePanel::ContentTitleDiscovered(unsigned int, class Symbol)` (164B) — `?ContentTitleDiscovered@HamStorePanel@@UAA_NIVSymbol@@@Z`
- [ ] `public: virtual void __cdecl HamStorePanel::ContentMounted(char const *, char const *)` (120B) — `?ContentMounted@HamStorePanel@@UAAXPBD0@Z`
- [ ] `public: virtual void __cdecl HamStorePanel::Poll(void)` (1440B) — `?Poll@HamStorePanel@@UAAXXZ`
- [ ] `public: virtual void __cdecl HamStorePanel::Unload(void)` (312B) — `?Unload@HamStorePanel@@UAAXXZ`

### `system/world/SpotlightDrawer` (8 functions)

- [ ] `protected: virtual void __cdecl SpotlightDrawer::DrawShadow(void)` (280B) — `?DrawShadow@SpotlightDrawer@@MAAXXZ`
- [ ] `protected: virtual void __cdecl SpotlightDrawer::DrawWorld(void)` (812B) — `?DrawWorld@SpotlightDrawer@@MAAXXZ`
- [ ] `public: class SpotDrawParams & __cdecl SpotDrawParams::operator=(class SpotDrawParams const &)` (152B) — `??4SpotDrawParams@@QAAAAV0@ABV0@@Z`
- [ ] `public: static void __cdecl SpotlightDrawer::DrawLight(class Spotlight *)` (848B) — `?DrawLight@SpotlightDrawer@@SAXPAVSpotlight@@@Z`
- [ ] `public: static void __cdecl SpotlightDrawer::RemoveFromLists(class Spotlight *)` (256B) — `?RemoveFromLists@SpotlightDrawer@@SAXPAVSpotlight@@@Z`
- [ ] `public: void __cdecl SpotlightDrawer::ClearLights(void)` (192B) — `?ClearLights@SpotlightDrawer@@QAAXXZ`
- [ ] `public: void __cdecl SpotlightDrawer::DeSelect(void)` (116B) — `?DeSelect@SpotlightDrawer@@QAAXXZ`
- [ ] `public: void __cdecl SpotlightDrawer::UpdateBoxMap(void)` (136B) — `?UpdateBoxMap@SpotlightDrawer@@QAAXXZ`

### `system/rnddx9/Mesh` (8 functions)

- [ ] `protected: bool __cdecl DxMesh::CanDraw(void) const` (68B) — `?CanDraw@DxMesh@@IBA_NXZ`
- [ ] `protected: unsigned int __cdecl DxMesh::VertFVF(void) const` (96B) — `?VertFVF@DxMesh@@IBAIXZ`
- [ ] `protected: unsigned int __cdecl DxMesh::VertSize(void) const` (96B) — `?VertSize@DxMesh@@IBAIXZ`
- [ ] `public: virtual void __cdecl DxMesh::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` (252B) — `?Copy@DxMesh@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`
- [ ] `public: virtual void __cdecl DxMesh::DrawFacesInRange(int, int)` (752B) — `?DrawFacesInRange@DxMesh@@UAAXHH@Z`
- [ ] `public: virtual void __cdecl DxMesh::DrawShowing(void)` (308B) — `?DrawShowing@DxMesh@@UAAXXZ`
- [ ] `public: void __cdecl DxMesh::VertexBufferData::Release(void)` (68B) — `?Release@VertexBufferData@DxMesh@@QAAXXZ`
- [ ] `void __cdecl ScaleAddEq(class Transform &, class Transform const &, float)` (120B) — `?ScaleAddEq@@YAXAAVTransform@@ABV1@M@Z`

### `system/synth/Sfx` (8 functions)

- [ ] `public: virtual void __cdecl SfxInst::SetPan(float)` (112B) — `?SetPan@SfxInst@@UAAXM@Z`
- [ ] `public: virtual void __cdecl SfxInst::SetTranspose(float)` (52B) — `?SetTranspose@SfxInst@@UAAXM@Z`
- [ ] `public: virtual void __cdecl SfxInst::StartImpl(void)` (240B) — `?StartImpl@SfxInst@@UAAXXZ`
- [ ] `public: virtual void __cdecl SfxInst::UpdateVolume(void)` (192B) — `?UpdateVolume@SfxInst@@UAAXXZ`
- [ ] `public: void __cdecl SfxInst::Pause(bool)` (148B) — `?Pause@SfxInst@@QAAX_N@Z`
- [ ] `public: void __cdecl SfxInst::SetReverbEnable(bool)` (80B) — `?SetReverbEnable@SfxInst@@QAAX_N@Z`
- [ ] `public: void __cdecl SfxInst::SetReverbMixDb(float)` (112B) — `?SetReverbMixDb@SfxInst@@QAAXM@Z`
- [ ] `public: void __cdecl SfxInst::SetSend(class FxSend *)` (80B) — `?SetSend@SfxInst@@QAAXPAVFxSend@@@Z`

### `lazer/meta_ham/PlaylistSortMgr` (8 functions)

- [ ] `private: class DataNode __cdecl PlaylistSortMgr::OnMsg(class RCJobCompleteMsg const &)` (548B) — `?OnMsg@PlaylistSortMgr@@AAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z`
- [ ] `private: void __cdecl PlaylistSortMgr::HandleCmdChangeProfileOnlineID(void)` (176B) — `?HandleCmdChangeProfileOnlineID@PlaylistSortMgr@@AAAXXZ`
- [ ] `private: void __cdecl PlaylistSortMgr::QueueCmdChangeProfileOnlineID(class String)` (144B) — `?QueueCmdChangeProfileOnlineID@PlaylistSortMgr@@AAAXVString@@@Z`
- [ ] `private: void __cdecl PlaylistSortMgr::QueueCmdGetPlaylistsFromRC(void)` (128B) — `?QueueCmdGetPlaylistsFromRC@PlaylistSortMgr@@AAAXXZ`
- [ ] `private: void __cdecl PlaylistSortMgr::SendPassiveMsg(class Symbol)` (420B) — `?SendPassiveMsg@PlaylistSortMgr@@AAAXVSymbol@@@Z`
- [ ] `private: void __cdecl PlaylistSortMgr::UpdateCurrPlaylistWithRC(void)` (668B) — `?UpdateCurrPlaylistWithRC@PlaylistSortMgr@@AAAXXZ`
- [ ] `public: __cdecl PlaylistSortByType::PlaylistSortByType(void)` (120B) — `??0PlaylistSortByType@@QAA@XZ`
- [ ] `public: void __cdecl PlaylistSortMgr::UpdateList(void)` (376B) — `?UpdateList@PlaylistSortMgr@@QAAXXZ`

### `system/rndobj/Font3d` (7 functions)

- [ ] `public: virtual bool __cdecl RndFont3d::CharAdvance(unsigned short, unsigned short, float &) const` (244B) — `?CharAdvance@RndFont3d@@UBA_NGGAAM@Z`
- [ ] `public: virtual bool __cdecl RndFont3d::SyncProperty(class DataNode &, class DataArray *, int, enum PropOp)` (112B) — `?SyncProperty@RndFont3d@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `public: virtual float __cdecl RndFont3d::AspectRatio(void) const` (20B) — `?AspectRatio@RndFont3d@@UBAMXZ`
- [ ] `public: virtual float __cdecl RndFont3d::CharAdvance(unsigned short) const` (296B) — `?CharAdvance@RndFont3d@@UBAMG@Z`
- [ ] `public: virtual float __cdecl RndFont3d::CharWidth(unsigned short) const` (280B) — `?CharWidth@RndFont3d@@UBAMG@Z`
- [ ] `public: virtual float __cdecl RndFont3d::Kerning(unsigned short, unsigned short) const` (88B) — `?Kerning@RndFont3d@@UBAMGG@Z`
- [ ] `public: virtual void __cdecl RndFont3d::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` (504B) — `?Copy@RndFont3d@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`

### `system/synth/StandardStream` (7 functions)

- [ ] `private: void __cdecl StandardStream::UpdateVolumes(void)` (400B) — `?UpdateVolumes@StandardStream@@AAAXXZ`
- [ ] `private: void __cdecl StandardStream::setJumpSamplesFromMs(float, float)` (164B) — `?setJumpSamplesFromMs@StandardStream@@AAAXMM@Z`
- [ ] `public: bool __cdecl StandardStream::IsPastStreamJumpPointOfNoReturn(void)` (176B) — `?IsPastStreamJumpPointOfNoReturn@StandardStream@@QAA_NXZ`
- [ ] `public: int __cdecl StandardStream::ConsumeData(void **, int, int)` (856B) — `?ConsumeData@StandardStream@@QAAHPAPAXHH@Z`
- [ ] `public: virtual void __cdecl StandardStream::UpdateTime(void)` (368B) — `?UpdateTime@StandardStream@@UAAXXZ`
- [ ] `public: virtual void __cdecl StandardStream::UpdateTimeByFiltering(void)` (252B) — `?UpdateTimeByFiltering@StandardStream@@UAAXXZ`
- [ ] `public: void __cdecl StandardStream::PollStream(void)` (456B) — `?PollStream@StandardStream@@QAAXXZ`

### `system/char/CharDriver` (7 functions)

- [ ] `protected: float __cdecl CharDriver::Display(float)` (1756B) — `?Display@CharDriver@@IAAMM@Z`
- [ ] `public: class CharClip * __cdecl CharDriver::FindClip(class DataNode const &, bool)` (356B) — `?FindClip@CharDriver@@QAAPAVCharClip@@ABVDataNode@@_N@Z`
- [ ] `public: virtual bool __cdecl CharDriver::SyncProperty(class DataNode &, class DataArray *, int, enum PropOp)` (1636B) — `?SyncProperty@CharDriver@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `public: virtual class DataNode __cdecl CharDriver::Handle(class DataArray *, bool)` (2520B) — `?Handle@CharDriver@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `public: virtual void __cdecl CharDriver::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` (220B) — `?Copy@CharDriver@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`
- [ ] `public: virtual void __cdecl CharDriver::Poll(void)` (1444B) — `?Poll@CharDriver@@UAAXXZ`
- [ ] `public: void __cdecl CharDriver::SetClipWeightMap(void)` (128B) — `?SetClipWeightMap@CharDriver@@QAAXXZ`

### `system/world/Crowd` (7 functions)

- [ ] `protected: class DataNode __cdecl WorldCrowd::OnIterateFrac(class DataArray *)` (724B) — `?OnIterateFrac@WorldCrowd@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: class RndMesh * __cdecl WorldCrowd::BuildBillboard(class Character *, float)` (420B) — `?BuildBillboard@WorldCrowd@@IAAPAVRndMesh@@PAVCharacter@@M@Z`
- [ ] `protected: void __cdecl WorldCrowd::AssignRandomColors(bool)` (528B) — `?AssignRandomColors@WorldCrowd@@IAAX_N@Z`
- [ ] `protected: void __cdecl WorldCrowd::Draw3DChars(void)` (440B) — `?Draw3DChars@WorldCrowd@@IAAXXZ`
- [ ] `protected: void __cdecl WorldCrowd::Reset3DCrowd(void)` (324B) — `?Reset3DCrowd@WorldCrowd@@IAAXXZ`
- [ ] `public: virtual void __cdecl WorldCrowd::DrawShowing(void)` (2644B) — `?DrawShowing@WorldCrowd@@UAAXXZ`
- [ ] `public: void __cdecl WorldCrowd::SetFullness(float, float)` (764B) — `?SetFullness@WorldCrowd@@QAAXMM@Z`

### `system/hamobj/HamDirector` (7 functions)

- [ ] `protected: class DataNode __cdecl HamDirector::OnSelectCamera(class DataArray *)` (928B) — `?OnSelectCamera@HamDirector@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: void __cdecl HamDirector::OnPopulateFromMoveMgr(void)` (696B) — `?OnPopulateFromMoveMgr@HamDirector@@IAAXXZ`
- [ ] `protected: void __cdecl HamDirector::OnPopulateMoves(void)` (2712B) — `?OnPopulateMoves@HamDirector@@IAAXXZ`
- [ ] `protected: void __cdecl HamDirector::PlayNextShot(void)` (400B) — `?PlayNextShot@HamDirector@@IAAXXZ`
- [ ] `public: virtual void __cdecl HamDirector::Poll(void)` (1428B) — `?Poll@HamDirector@@UAAXXZ`
- [ ] `public: void __cdecl HamDirector::DrawIconMan(class Symbol, class Symbol, class Symbol, float, float, class RndTex *)` (1144B) — `?DrawIconMan@HamDirector@@QAAXVSymbol@@00MMPAVRndTex@@@Z`
- [ ] `public: void __cdecl HamDirector::DrawIconMan(enum Difficulty, float, float, float, float, class RndTex *)` (872B) — `?DrawIconMan@HamDirector@@QAAXW4Difficulty@@MMMMPAVRndTex@@@Z`

### `system/hamobj/DanceRemixer` (7 functions)

- [ ] `public: class MoveVariant const * __cdecl DanceRemixer::MoveVariantFromHamMove(class HamMove const *) const` (320B) — `?MoveVariantFromHamMove@DanceRemixer@@QBAPBVMoveVariant@@PBVHamMove@@@Z`
- [ ] `public: float __cdecl DanceRemixer::JumpedBeat(float) const` (176B) — `?JumpedBeat@DanceRemixer@@QBAMM@Z`
- [ ] `public: int __cdecl DanceRemixer::JumpedMeasureAdd(int, int) const` (104B) — `?JumpedMeasureAdd@DanceRemixer@@QBAHHH@Z`
- [ ] `public: int __cdecl DanceRemixer::JumpedMeasureStepsBetween(int, int, int) const` (240B) — `?JumpedMeasureStepsBetween@DanceRemixer@@QBAHHHH@Z`
- [ ] `public: int __cdecl DanceRemixer::JumpedMoveIdxAdd(int, int) const` (40B) — `?JumpedMoveIdxAdd@DanceRemixer@@QBAHHH@Z`
- [ ] `public: void __cdecl DanceRemixer::ClearJump(void)` (84B) — `?ClearJump@DanceRemixer@@QAAXXZ`
- [ ] `public: void __cdecl DanceRemixer::SetJump(int, int)` (668B) — `?SetJump@DanceRemixer@@QAAXHH@Z`

### `system/gesture/SkeletonClip` (7 functions)

- [ ] `protected: struct RecordedFrame const * __cdecl SkeletonClip::CurRecordedFrame(int &, int &) const` (240B) — `?CurRecordedFrame@SkeletonClip@@IBAPBURecordedFrame@@AAH0@Z`
- [ ] `public: float __cdecl SkeletonClip::SongStartSeconds(void) const` (80B) — `?SongStartSeconds@SkeletonClip@@QBAMXZ`
- [ ] `public: virtual bool __cdecl SkeletonClip::PrevSkeleton(class Skeleton const &, int, class ArchiveSkeleton &, int &) const` (320B) — `?PrevSkeleton@SkeletonClip@@UBA_NABVSkeleton@@HAAVArchiveSkeleton@@AAH@Z`
- [ ] `public: void __cdecl RecordedFrame::MakeSkeletonFrame(struct SkeletonFrame &, int) const` (324B) — `?MakeSkeletonFrame@RecordedFrame@@QBAXAAUSkeletonFrame@@H@Z`
- [ ] `public: void __cdecl SkeletonClip::FillMoveRatings(void)` (524B) — `?FillMoveRatings@SkeletonClip@@QAAXXZ`
- [ ] `public: void __cdecl SkeletonClip::PollRecording(struct SkeletonFrame const &)` (600B) — `?PollRecording@SkeletonClip@@QAAXABUSkeletonFrame@@@Z`
- [ ] `public: void __cdecl SkeletonClip::SwapMoveRecord(void)` (104B) — `?SwapMoveRecord@SkeletonClip@@QAAXXZ`

### `system/world/LightPreset` (7 functions)

- [ ] `protected: class DataNode __cdecl LightPreset::OnSetKeyframe(class DataArray *)` (176B) — `?OnSetKeyframe@LightPreset@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: void __cdecl LightPreset::Animate(float)` (1176B) — `?Animate@LightPreset@@IAAXM@Z`
- [ ] `protected: void __cdecl LightPreset::CacheFrames(void)` (1000B) — `?CacheFrames@LightPreset@@IAAXXZ`
- [ ] `protected: void __cdecl LightPreset::FillEnvPresetData(class RndEnviron *, struct LightPreset::EnvironmentEntry &)` (168B) — `?FillEnvPresetData@LightPreset@@IAAXPAVRndEnviron@@AAUEnvironmentEntry@1@@Z`
- [ ] `protected: void __cdecl LightPreset::GetKey(float, int &, int &, float &) const` (768B) — `?GetKey@LightPreset@@IBAXMAAH0AAM@Z`
- [ ] `protected: void __cdecl LightPreset::SetKeyframe(struct LightPreset::Keyframe &)` (468B) — `?SetKeyframe@LightPreset@@IAAXAAUKeyframe@1@@Z`
- [ ] `public: void __cdecl LightPreset::SetFrameEx(float, float, bool)` (964B) — `?SetFrameEx@LightPreset@@QAAXMM_N@Z`

### `system/rnddx9/Rnd_Xbox` (6 functions)

- [ ] `private: void __cdecl DxRnd::CopyPostProcess(void)` (336B) — `?CopyPostProcess@DxRnd@@AAAXXZ`
- [ ] `private: void __cdecl DxRnd::FinishPostProcess(void)` (460B) — `?FinishPostProcess@DxRnd@@AAAXXZ`
- [ ] `public: virtual class Vector2& __cdecl DxRnd::DrawString(char const *, class Vector2const &, class Hmx::Color const &, bool)` (888B) — `?DrawString@DxRnd@@UAAAAVVector2@@PBDABV2@ABVColor@Hmx@@_N@Z`
- [ ] `public: virtual void __cdecl DxRnd::BeginDrawing(void)` (980B) — `?BeginDrawing@DxRnd@@UAAXXZ`
- [ ] `public: virtual void __cdecl DxRnd::EndDrawing(void)` (688B) — `?EndDrawing@DxRnd@@UAAXXZ`
- [ ] `public: void __cdecl DxRnd::ReleaseAutoRelease(void)` (680B) — `?ReleaseAutoRelease@DxRnd@@QAAXXZ`

### `system/hamobj/FreestyleMoveRecorder` (6 functions)

- [ ] `public: class BaseSkeleton * __cdecl FreestyleMoveRecorder::GetLiveSkeleton(void)` (220B) — `?GetLiveSkeleton@FreestyleMoveRecorder@@QAAPAVBaseSkeleton@@XZ`
- [ ] `public: float __cdecl FreestyleMoveRecorder::CompareSkeletonPositions(class BaseSkeleton const *, class BaseSkeleton const *, float) const` (400B) — `?CompareSkeletonPositions@FreestyleMoveRecorder@@QBAMPBVBaseSkeleton@@0M@Z`
- [ ] `public: float __cdecl FreestyleMoveRecorder::GetScore(class BaseSkeleton const *, int, float, bool)` (472B) — `?GetScore@FreestyleMoveRecorder@@QAAMPBVBaseSkeleton@@HM_N@Z`
- [ ] `public: void __cdecl FreestyleMoveRecorder::DrawDebug(void)` (588B) — `?DrawDebug@FreestyleMoveRecorder@@QAAXXZ`
- [ ] `public: void __cdecl FreestyleMoveRecorder::Poll(void)` (1196B) — `?Poll@FreestyleMoveRecorder@@QAAXXZ`
- [ ] `public: void __cdecl FreestyleMoveRecorder::StopRecording(void)` (28B) — `?StopRecording@FreestyleMoveRecorder@@QAAXXZ`

### `system/hamobj/MoveDir` (6 functions)

- [ ] `private: void __cdecl MoveDir::PostUpdateFilters(void)` (2032B) — `?PostUpdateFilters@MoveDir@@AAAXXZ`
- [ ] `public: float __cdecl MoveDir::DetectFrac(int, int)` (304B) — `?DetectFrac@MoveDir@@QAAMHH@Z`
- [ ] `public: virtual float __cdecl MoveDir::UpdateOverlay(class RndOverlay *, float)` (5072B) — `?UpdateOverlay@MoveDir@@UAAMPAVRndOverlay@@M@Z`
- [ ] `public: virtual void __cdecl MoveDir::DrawShowing(void)` (1164B) — `?DrawShowing@MoveDir@@UAAXXZ`
- [ ] `public: void __cdecl MoveDir::FinalPoseStateMachine(void)` (1136B) — `?FinalPoseStateMachine@MoveDir@@QAAXXZ`
- [ ] `public: void __cdecl MoveDir::ResetDetectFrames(int, enum Difficulty)` (1204B) — `?ResetDetectFrames@MoveDir@@QAAXHW4Difficulty@@@Z`

### `system/obj/Utl` (5 functions)

- [ ] `class DataNode __cdecl MakeFileList(char const *, bool, bool (__cdecl *)(char *))` (436B) — `?MakeFileList@@YA?AVDataNode@@PBD_NP6A_NPAD@Z@Z`
- [ ] `class DataNode __cdecl MakeFileListFullPath(char const *)` (348B) — `?MakeFileListFullPath@@YA?AVDataNode@@PBD@Z`
- [ ] `public: virtual __cdecl StackString<128>::~StackString<128>(void)` (16B) — `??1?$StackString@$0IA@@@UAA@XZ`
- [ ] `void __cdecl CopyTypeProperties(class Hmx::Object *, class Hmx::Object *)` (1592B) — `?CopyTypeProperties@@YAXPAVObject@Hmx@@0@Z`
- [ ] `void __cdecl MergeObjectsRecurse(class ObjectDir *, class ObjectDir *, class MergeFilter &, bool)` (600B) — `?MergeObjectsRecurse@@YAXPAVObjectDir@@0AAVMergeFilter@@_N@Z`

### `system/rndobj/Lit_NG` (5 functions)

- [ ] `class Hmx::Matrix4 __cdecl Hmx::operator*(class Transform const &, class Hmx::Matrix4const &)` (1036B) — `??DHmx@@YA?AVMatrix4@0@ABVTransform@@ABV10@@Z`
- [ ] `protected: bool __cdecl NgLight::SphereConeTest(class Vector3const &, float)` (1236B) — `?SphereConeTest@NgLight@@IAA_NABVVector3@@M@Z`
- [ ] `protected: virtual void __cdecl NgLight::BlurShadowRT(void)` (572B) — `?BlurShadowRT@NgLight@@MAAXXZ`
- [ ] `protected: virtual void __cdecl NgLight::SetAndClearShadowViewport(void)` (172B) — `?SetAndClearShadowViewport@NgLight@@MAAXXZ`
- [ ] `public: void __cdecl NgLight::CheckShadowMap(void)` (620B) — `?CheckShadowMap@NgLight@@QAAXXZ`

### `system/hamobj/MoveMgr` (5 functions)

- [ ] `public: int __cdecl MoveMgr::ComputeRandomChoiceSet(int)` (588B) — `?ComputeRandomChoiceSet@MoveMgr@@QAAHH@Z`
- [ ] `public: void __cdecl MoveMgr::ComputeLoadedMoveSet(void)` (384B) — `?ComputeLoadedMoveSet@MoveMgr@@QAAXXZ`
- [ ] `public: void __cdecl MoveMgr::FillInRoutineAt(int, int)` (588B) — `?FillInRoutineAt@MoveMgr@@QAAXHH@Z`
- [ ] `public: void __cdecl MoveMgr::FillRoutineFromReplacer(int)` (92B) — `?FillRoutineFromReplacer@MoveMgr@@QAAXH@Z`
- [ ] `public: void __cdecl MoveMgr::FillRoutineFromVerses(int)` (324B) — `?FillRoutineFromVerses@MoveMgr@@QAAXH@Z`

### `system/char/Character` (5 functions)

- [ ] `protected: void __cdecl Character::SyncShadow(void)` (356B) — `?SyncShadow@Character@@IAAXXZ`
- [ ] `protected: void __cdecl Character::UnhookShadow(void)` (156B) — `?UnhookShadow@Character@@IAAXXZ`
- [ ] `public: class RndDrawable * __cdecl DrawPtrVec::CollideShowing(class Segment const &, float &, class Plane &) const` (232B) — `?CollideShowing@DrawPtrVec@@QBAPAVRndDrawable@@ABVSegment@@AAMAAVPlane@@@Z`
- [ ] `public: virtual void __cdecl Character::DrawShowing(void)` (1140B) — `?DrawShowing@Character@@UAAXXZ`
- [ ] `public: void __cdecl Character::FindInterestObjects(class ObjectDir *)` (420B) — `?FindInterestObjects@Character@@QAAXPAVObjectDir@@@Z`

### `system/hamobj/HamCamShot` (5 functions)

- [ ] `private: class DataNode __cdecl HamCamShot::OnAllowableNextShots(class DataArray const *)` (696B) — `?OnAllowableNextShots@HamCamShot@@AAA?AVDataNode@@PBVDataArray@@@Z`
- [ ] `protected: void __cdecl HamCamShot::UpdateTargetsFlipped(void)` (1716B) — `?UpdateTargetsFlipped@HamCamShot@@IAAXXZ`
- [ ] `public: virtual void __cdecl HamCamShot::EndAnim(void)` (316B) — `?EndAnim@HamCamShot@@UAAXXZ`
- [ ] `public: virtual void __cdecl HamCamShot::SetPreFrame(float, float)` (424B) — `?SetPreFrame@HamCamShot@@UAAXMM@Z`
- [ ] `public: void __cdecl HamCamShot::Reteleport(class Vector3const &, bool, class Symbol)` (1252B) — `?Reteleport@HamCamShot@@QAAXABVVector3@@_NVSymbol@@@Z`

### `system/utl/MemHeap` (5 functions)

- [ ] `public: int * __cdecl MemHeap::Alloc(int, int, int &)` (356B) — `?Alloc@MemHeap@@QAAPAHHHAAH@Z`
- [ ] `public: int * __cdecl MemHeap::Truncate(int *, int, int &)` (396B) — `?Truncate@MemHeap@@QAAPAHPAHHAAH@Z`
- [ ] `public: int * __cdecl MemHeap::TryAlloc(int, int, int &)` (572B) — `?TryAlloc@MemHeap@@QAAPAHHHAAH@Z`
- [ ] `public: int __cdecl MemHeap::Free(int *)` (296B) — `?Free@MemHeap@@QAAHPAH@Z`
- [ ] `public: static int __cdecl MemHeap::GetAlignWords(int)` (100B) — `?GetAlignWords@MemHeap@@SAHH@Z`

### `lazer/meta_ham/ChallengeSortNode` (5 functions)

- [ ] `public: class String __cdecl ChallengeHeaderNode::GetSongShortTitle(void)` (200B) — `?GetSongShortTitle@ChallengeHeaderNode@@QAA?AVString@@XZ`
- [ ] `public: int __cdecl ChallengeHeaderNode::GetPotentialChallengeExp(class NavListSortNode *)` (196B) — `?GetPotentialChallengeExp@ChallengeHeaderNode@@QAAHPAVNavListSortNode@@@Z`
- [ ] `public: int __cdecl ChallengeHeaderNode::GetTotalEarnedExp(int)` (204B) — `?GetTotalEarnedExp@ChallengeHeaderNode@@QAAHH@Z`
- [ ] `public: virtual bool __cdecl ChallengeHeaderNode::IsActive(void) const` (64B) — `?IsActive@ChallengeHeaderNode@@UBA_NXZ`
- [ ] `public: virtual char const * __cdecl ChallengeHeaderNode::GetAlbumArtPath(void)` (232B) — `?GetAlbumArtPath@ChallengeHeaderNode@@UAAPBDXZ`

### `lazer/meta_ham/FitnessGoalMgr` (5 functions)

- [ ] `private: bool __cdecl FitnessGoalMgr::IsProfileChanged(void)` (92B) — `?IsProfileChanged@FitnessGoalMgr@@AAA_NXZ`
- [ ] `private: class DataNode __cdecl FitnessGoalMgr::OnMsg(class RCJobCompleteMsg const &)` (532B) — `?OnMsg@FitnessGoalMgr@@AAA?AVDataNode@@ABVRCJobCompleteMsg@@@Z`
- [ ] `private: void __cdecl FitnessGoalMgr::AddPendingProfile(class HamProfile *)` (200B) — `?AddPendingProfile@FitnessGoalMgr@@AAAXPAVHamProfile@@@Z`
- [ ] `private: void __cdecl FitnessGoalMgr::ProcessNextCommand(void)` (216B) — `?ProcessNextCommand@FitnessGoalMgr@@AAAXXZ`
- [ ] `private: void __cdecl PartyModeMgr::OnSmartGlassListen(int)` (156B) — `?OnSmartGlassListen@PartyModeMgr@@AAAXH@Z`

### `system/hamobj/HamWardrobe` (5 functions)

- [ ] `protected: class DataNode __cdecl HamWardrobe::OnAddCrowd(class DataArray *)` (200B) — `?OnAddCrowd@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: class DataNode __cdecl HamWardrobe::OnLoadCharacters(class DataArray *)` (392B) — `?OnLoadCharacters@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: class DataNode __cdecl HamWardrobe::OnSetVenue(class DataArray *)` (552B) — `?OnSetVenue@HamWardrobe@@IAA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `public: void __cdecl HamWardrobe::LoadCharacters(class Symbol, class Symbol, class Symbol, class Symbol, enum HamBackupDancers, class Symbol, class Symbol, bool)` (824B) — `?LoadCharacters@HamWardrobe@@QAAXVSymbol@@000W4HamBackupDancers@@00_N@Z`
- [ ] `public: void __cdecl HamWardrobe::PlayCrowdAnimation(class Symbol, int, bool)` (780B) — `?PlayCrowdAnimation@HamWardrobe@@QAAXVSymbol@@H_N@Z`

### `system/utl/Cache_Xbox` (5 functions)

- [ ] `protected: bool __cdecl CacheXbox::DeleteParentDirs(class String)` (332B) — `?DeleteParentDirs@CacheXbox@@IAA_NVString@@@Z`
- [ ] `protected: int __cdecl CacheXbox::ThreadDelete(void)` (224B) — `?ThreadDelete@CacheXbox@@IAAHXZ`
- [ ] `protected: int __cdecl CacheXbox::ThreadGetDir(class String, class String)` (744B) — `?ThreadGetDir@CacheXbox@@IAAHVString@@0@Z`
- [ ] `protected: int __cdecl CacheXbox::ThreadRead(void)` (288B) — `?ThreadRead@CacheXbox@@IAAHXZ`
- [ ] `public: __cdecl CacheIDXbox::CacheIDXbox(class CacheIDXbox const &)` (80B) — `??0CacheIDXbox@@QAA@ABV0@@Z`

### `system/char/CharHair` (5 functions)

- [ ] `protected: void __cdecl CharHair::DoReset(int)` (688B) — `?DoReset@CharHair@@IAAXH@Z`
- [ ] `protected: void __cdecl CharHair::SimulateLoops(int, float)` (252B) — `?SimulateLoops@CharHair@@IAAXHM@Z`
- [ ] `public: __cdecl CharHair::Point::Point(class Hmx::Object *)` (236B) — `??0Point@CharHair@@QAA@PAVObject@Hmx@@@Z`
- [ ] `public: void __cdecl CharHair::FreezePoseRaw(void)` (256B) — `?FreezePoseRaw@CharHair@@QAAXXZ`
- [ ] `public: void __cdecl CharHair::Strand::SetRoot(class RndTransformable *)` (620B) — `?SetRoot@Strand@CharHair@@QAAXPAVRndTransformable@@@Z`

### `system/utl/MemTracker` (5 functions)

- [ ] `public: static int __cdecl MemTracker::SpitAllocInfo(struct _iobuf *)` (196B) — `?SpitAllocInfo@MemTracker@@SAHPAU_iobuf@@@Z`
- [ ] `public: void __cdecl MemTracker::ReportMemoryAlloc(char const *)` (220B) — `?ReportMemoryAlloc@MemTracker@@QAAXPBD@Z`
- [ ] `public: void __cdecl MemTracker::ReportMemoryUsage(char const *)` (604B) — `?ReportMemoryUsage@MemTracker@@QAAXPBD@Z`
- [ ] `public: void __cdecl MemTracker::ReportMemoryUsageOverview(char const *)` (340B) — `?ReportMemoryUsageOverview@MemTracker@@QAAXPBD@Z`
- [ ] `void __cdecl DiffTblReport(char const *, class BlockStatTable &, class BlockStatTable &, class TextStream &)` (656B) — `?DiffTblReport@@YAXPBDAAVBlockStatTable@@1AAVTextStream@@@Z`

### `system/flow/Flow` (4 functions)

- [ ] `public: __cdecl FlowPtr<class Hmx::Object>::FlowPtr<class Hmx::Object>(class FlowPtr<class Hmx::Object> const &)` (84B) — `??0?$FlowPtr@VObject@Hmx@@@@QAA@ABV0@@Z`
- [ ] `public: virtual void __cdecl Flow::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` (796B) — `?Copy@Flow@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`
- [ ] `public: virtual void __cdecl Flow::Enter(void)` (156B) — `?Enter@Flow@@UAAXXZ`
- [ ] `public: virtual void __cdecl Flow::Exit(void)` (168B) — `?Exit@Flow@@UAAXXZ`

### `system/os/BlockMgr` (4 functions)

- [ ] `public: void __cdecl BlockMgr::AddTask(class AsyncTask const &)` (284B) — `?AddTask@BlockMgr@@QAAXABVAsyncTask@@@Z`
- [ ] `public: void __cdecl BlockMgr::GetAssociatedBlocks(unsigned __int64, int, int &, int &, int &)` (92B) — `?GetAssociatedBlocks@BlockMgr@@QAAX_KHAAH11@Z`
- [ ] `public: void __cdecl BlockMgr::KillBlockRequests(class ArkFile *)` (260B) — `?KillBlockRequests@BlockMgr@@QAAXPAVArkFile@@@Z`
- [ ] `public: void __cdecl BlockMgr::Poll(void)` (1120B) — `?Poll@BlockMgr@@QAAXXZ`

### `system/utl/MemTrack` (4 functions)

- [ ] `void __cdecl BeginMemTrackFileName(char const *)` (212B) — `?BeginMemTrackFileName@@YAXPBD@Z`
- [ ] `void __cdecl BeginMemTrackObjectName(char const *)` (280B) — `?BeginMemTrackObjectName@@YAXPBD@Z`
- [ ] `void __cdecl EndMemTrackFileName(void)` (212B) — `?EndMemTrackFileName@@YAXXZ`
- [ ] `void __cdecl EndMemTrackObjectName(void)` (188B) — `?EndMemTrackObjectName@@YAXXZ`

### `system/os/NetworkSocket_Win` (4 functions)

- [ ] `public: static class NetworkSocket * __cdecl NetworkSocket::Create(bool)` (88B) — `?Create@NetworkSocket@@SAPAV1@_N@Z`
- [ ] `public: static class String __cdecl NetworkSocket::GetHostName(void)` (180B) — `?GetHostName@NetworkSocket@@SA?AVString@@XZ`
- [ ] `public: static class String __cdecl NetworkSocket::IPIntToString(unsigned int)` (88B) — `?IPIntToString@NetworkSocket@@SA?AVString@@I@Z`
- [ ] `public: static unsigned int __cdecl NetworkSocket::IPStringToInt(class String const &)` (52B) — `?IPStringToInt@NetworkSocket@@SAIABVString@@@Z`

### `system/char/ClipDistMap` (4 functions)

- [ ] `OnlyReturns` (4B) — `OnlyReturns`
- [ ] `protected: void __cdecl ClipDistMap::FindBestNodeRecurse(float, float, float, float, float)` (328B) — `?FindBestNodeRecurse@ClipDistMap@@IAAXMMMMM@Z`
- [ ] `public: void __cdecl ClipDistMap::Draw(float, float, class CharDriver *)` (1732B) — `?Draw@ClipDistMap@@QAAXMMPAVCharDriver@@@Z`
- [ ] `public: void __cdecl ClipDistMap::FindDists(float, class DataArray *)` (2688B) — `?FindDists@ClipDistMap@@QAAXMPAVDataArray@@@Z`

### `system/rndobj/Gen` (4 functions)

- [ ] `public: virtual bool __cdecl RndGenerator::MakeWorldSphere(class Sphere &, bool)` (320B) — `?MakeWorldSphere@RndGenerator@@UAA_NAAVSphere@@_N@Z`
- [ ] `public: virtual void __cdecl RndGenerator::DrawShowing(void)` (692B) — `?DrawShowing@RndGenerator@@UAAXXZ`
- [ ] `public: virtual void __cdecl RndGenerator::SetFrame(float, float)` (440B) — `?SetFrame@RndGenerator@@UAAXMM@Z`
- [ ] `public: void __cdecl RndGenerator::Generate(float)` (384B) — `?Generate@RndGenerator@@QAAXM@Z`

### `system/hamobj/HamCamTransform` (4 functions)

- [ ] `protected: void __cdecl HamCamTransform::Setup(bool)` (624B) — `?Setup@HamCamTransform@@IAAX_N@Z`
- [ ] `public: virtual void __cdecl HamCamTransform::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` (276B) — `?Copy@HamCamTransform@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`
- [ ] `public: virtual void __cdecl HamCamTransform::Enter(void)` (8B) — `?Enter@HamCamTransform@@UAAXXZ`
- [ ] `public: void __cdecl HamCamTransform::ClearOldCrowds(void)` (144B) — `?ClearOldCrowds@HamCamTransform@@QAAXXZ`

### `system/char/CharClipGroup` (4 functions)

- [ ] `public: class CharClip * __cdecl CharClipGroup::FindClip(char const *) const` (144B) — `?FindClip@CharClipGroup@@QBAPAVCharClip@@PBD@Z`
- [ ] `public: int __cdecl Rand::Int(void)` (100B) — `?Int@Rand@@QAAHXZ`
- [ ] `public: void __cdecl CharClipGroup::DeleteRemaining(int)` (192B) — `?DeleteRemaining@CharClipGroup@@QAAXH@Z`
- [ ] `public: void __cdecl CharClipGroup::SetClipFlags(int)` (116B) — `?SetClipFlags@CharClipGroup@@QAAXH@Z`

### `system/utl/Cheats` (4 functions)

- [ ] `private: class DataNode __cdecl CheatsManager::OnMsg(class KeyboardKeyMsg const &)` (404B) — `?OnMsg@CheatsManager@@AAA?AVDataNode@@ABVKeyboardKeyMsg@@@Z`
- [ ] `private: int __cdecl CheatsManager::OnMsg(class ButtonDownMsg const &)` (724B) — `?OnMsg@CheatsManager@@AAAHABVButtonDownMsg@@@Z`
- [ ] `void __cdecl InitKeyCheats(class DataArray const *)` (704B) — `?InitKeyCheats@@YAXPBVDataArray@@@Z`
- [ ] `void __cdecl InitQuickJoyCheats(class DataArray const *, enum CheatsManager::ShiftMode)` (260B) — `?InitQuickJoyCheats@@YAXPBVDataArray@@W4ShiftMode@CheatsManager@@@Z`

### `lazer/game/Game` (4 functions)

- [ ] `class DataNode __cdecl OnCycleAutoplay(class DataArray *)` (348B) — `?OnCycleAutoplay@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `class DataNode __cdecl OnCycleTestDancer(class DataArray *)` (376B) — `?OnCycleTestDancer@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `class DataNode __cdecl OnDumpMoves(class DataArray *)` (272B) — `?OnDumpMoves@@YA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `public: void __cdecl Game::Poll(void)` (992B) — `?Poll@Game@@QAAXXZ`

### `lazer/meta_ham/SaveLoadManager` (4 functions)

- [ ] `protected: class DataNode __cdecl SaveLoadManager::OnMsg(class MCResultMsg const &)` (612B) — `?OnMsg@SaveLoadManager@@IAA?AVDataNode@@ABVMCResultMsg@@@Z`
- [ ] `protected: class DataNode __cdecl SaveLoadManager::OnMsg(class SigninChangedMsg const &)` (532B) — `?OnMsg@SaveLoadManager@@IAA?AVDataNode@@ABVSigninChangedMsg@@@Z`
- [ ] `public: void __cdecl SaveLoadManager::HandleEventResponse(class HamProfile *, int)` (844B) — `?HandleEventResponse@SaveLoadManager@@QAAXPAVHamProfile@@H@Z`
- [ ] `public: void __cdecl SaveLoadManager::Poll(void)` (3108B) — `?Poll@SaveLoadManager@@QAAXXZ`

### `system/rndobj/AmbientOcclusion` (4 functions)

- [ ] `protected: bool __cdecl RndAmbientOcclusion::IsValid_Tessellate(class RndMesh const *, class ObjectDir const *) const` (128B) — `?IsValid_Tessellate@RndAmbientOcclusion@@IBA_NPBVRndMesh@@PBVObjectDir@@@Z`
- [ ] `public: bool __cdecl VectorSort<class RndMesh *>::operator()(class RndMesh *, class RndMesh *)` (140B) — `??R?$VectorSort@PAVRndMesh@@@@QAA_NPAVRndMesh@@0@Z`
- [ ] `public: void __cdecl RndAmbientOcclusion::CalculateAO(float *)` (564B) — `?CalculateAO@RndAmbientOcclusion@@QAAXPAM@Z`
- [ ] `public: void __cdecl RndAmbientOcclusion::Tessellate(float *, float *)` (4492B) — `?Tessellate@RndAmbientOcclusion@@QAAXPAM0@Z`

### `system/rndobj/Font` (3 functions)

- [ ] `protected: void __cdecl RndFont::UpdateChars(void)` (1124B) — `?UpdateChars@RndFont@@IAAXXZ`
- [ ] `public: char const * __cdecl HamSongMetadata::Album(void) const` (8B) — `?Album@HamSongMetadata@@QBAPBDXZ`
- [ ] `public: void __cdecl RndFont::BleedTest(void)` (1152B) — `?BleedTest@RndFont@@QAAXXZ`

### `system/synth_xbox/FFT` (3 functions)

- [ ] `int __cdecl CalculateSinCosTable(long, float *)` (260B) — `?CalculateSinCosTable@@YAHJPAM@Z`
- [ ] `int __cdecl FFTComplex(float *, long, long, float *)` (84B) — `?FFTComplex@@YAHPAMJJ0@Z`
- [ ] `int __cdecl FFTRealForward(float *, unsigned long, float *)` (16B) — `?FFTRealForward@@YAHPAMK0@Z`

### `system/synth_xbox/FxSendDelay` (3 functions)

- [ ] `protected: virtual struct IUnknown * __cdecl FxSendDelay360::CreateFx(void)` (72B) — `?CreateFx@FxSendDelay360@@MAAPAUIUnknown@@XZ`
- [ ] `public: virtual void __cdecl FxSendDelay360::OnParametersChanged(void)` (8B) — `?OnParametersChanged@FxSendDelay360@@UAAXXZ`
- [ ] `public: virtual void __cdecl FxSendDelay360::UpdateMix(void)` (8B) — `?UpdateMix@FxSendDelay360@@UAAXXZ`

### `system/synth_xbox/FxSendEQ` (3 functions)

- [ ] `protected: virtual struct IUnknown * __cdecl FxSendEQ360::CreateFx(void)` (72B) — `?CreateFx@FxSendEQ360@@MAAPAUIUnknown@@XZ`
- [ ] `public: virtual void __cdecl FxSendEQ360::OnParametersChanged(void)` (8B) — `?OnParametersChanged@FxSendEQ360@@UAAXXZ`
- [ ] `public: virtual void __cdecl FxSendEQ360::UpdateMix(void)` (8B) — `?UpdateMix@FxSendEQ360@@UAAXXZ`

### `system/synth_xbox/FxSendWah` (3 functions)

- [ ] `protected: virtual struct IUnknown * __cdecl FxSendWah360::CreateFx(void)` (72B) — `?CreateFx@FxSendWah360@@MAAPAUIUnknown@@XZ`
- [ ] `public: virtual void __cdecl FxSendWah360::OnParametersChanged(void)` (8B) — `?OnParametersChanged@FxSendWah360@@UAAXXZ`
- [ ] `public: virtual void __cdecl FxSendWah360::UpdateMix(void)` (8B) — `?UpdateMix@FxSendWah360@@UAAXXZ`

### `system/rnddx9/Rnd` (3 functions)

- [ ] `public: virtual void __cdecl DxRnd::CreateLargeQuad(int, int, struct LargeQuadRenderData &)` (688B) — `?CreateLargeQuad@DxRnd@@UAAXHHAAULargeQuadRenderData@@@Z`
- [ ] `public: virtual void __cdecl DxRnd::DrawRect(class Hmx::Rect const &, class RndMat *, enum ShaderType, class Hmx::Color const &, class Hmx::Color const *, class Hmx::Color const *)` (1608B) — `?DrawRect@DxRnd@@UAAXABVRect@Hmx@@PAVRndMat@@W4ShaderType@@ABVColor@3@PBV63@4@Z`
- [ ] `public: virtual void __cdecl DxRnd::DrawRectDepth(class Vector3const &, class Vector3const (&)[4], class Vector4const &, class RndMat *, enum ShaderType)` (352B) — `?DrawRectDepth@DxRnd@@UAAXABVVector3@@AAY03$$CBV2@ABVVector4@@PAVRndMat@@W4ShaderType@@@Z`

### `lazer/meta_ham/ChallengeSortMgr` (3 functions)

- [ ] `public: char const * __cdecl ChallengeSortMgr::GetChallengerGamertag(int)` (124B) — `?GetChallengerGamertag@ChallengeSortMgr@@QAAPBDH@Z`
- [ ] `public: int __cdecl ChallengeSortMgr::GetChallengerXp(int)` (216B) — `?GetChallengerXp@ChallengeSortMgr@@QAAHH@Z`
- [ ] `public: virtual void __cdecl ChallengeSortMgr::OnEnter(void)` (496B) — `?OnEnter@ChallengeSortMgr@@UAAXXZ`

### `system/rndobj/PostProc` (3 functions)

- [ ] `protected: void __cdecl RndPostProc::UpdateColorModulation(void)` (196B) — `?UpdateColorModulation@RndPostProc@@IAAXXZ`
- [ ] `public: float __cdecl TrueColor::ExposureRecipe::GetLux(void)` (8B) — `?GetLux@ExposureRecipe@TrueColor@@QAAMXZ`
- [ ] `public: void __cdecl RndPostProc::Interp(class RndPostProc const *, class RndPostProc const *, float)` (1928B) — `?Interp@RndPostProc@@QAAXPBV1@0M@Z`

### `system/hamobj/RhythmDetector` (3 functions)

- [ ] `private: void __cdecl RhythmDetector::AddFrame(class BaseSkeleton const &)` (656B) — `?AddFrame@RhythmDetector@@AAAXABVBaseSkeleton@@@Z`
- [ ] `private: void __cdecl RhythmDetector::ProcessFrames(void)` (1232B) — `?ProcessFrames@RhythmDetector@@AAAXXZ`
- [ ] `public: struct RhythmDetector::RecordData const & __cdecl RhythmDetector::GetRecord(float, float, bool, class Symbol, class TextStream *)` (324B) — `?GetRecord@RhythmDetector@@QAAABURecordData@1@MM_NVSymbol@@PAVTextStream@@@Z`

### `system/char/CharIKHand` (3 functions)

- [ ] `public: virtual void __cdecl CharIKHand::Highlight(void)` (972B) — `?Highlight@CharIKHand@@UAAXXZ`
- [ ] `public: virtual void __cdecl CharIKHand::Poll(void)` (2368B) — `?Poll@CharIKHand@@UAAXXZ`
- [ ] `void __cdecl ScaleAddEq(class Hmx::Quat &, class Hmx::Quat const &, float)` (152B) — `?ScaleAddEq@@YAXAAVQuat@Hmx@@ABV12@M@Z`

### `system/synth/Sequence` (3 functions)

- [ ] `public: virtual bool __cdecl RandomIntervalGroupSeqInst::IsRunning(void)` (8B) — `?IsRunning@RandomIntervalGroupSeqInst@@UAA_NXZ`
- [ ] `public: virtual void __cdecl RandomIntervalGroupSeqInst::Stop(void)` (108B) — `?Stop@RandomIntervalGroupSeqInst@@UAAXXZ`
- [ ] `public: void __cdecl RandomGroupSeq::PickNextIndex(void)` (384B) — `?PickNextIndex@RandomGroupSeq@@QAAXXZ`

### `lazer/meta_ham/SongSortMgr` (3 functions)

- [ ] `public: __cdecl SongSortBySong::SongSortBySong(void)` (120B) — `??0SongSortBySong@@QAA@XZ`
- [ ] `public: void __cdecl SongSortMgr::SetSetlistMode(bool)` (424B) — `?SetSetlistMode@SongSortMgr@@QAAX_N@Z`
- [ ] `public: void __cdecl SongSortMgr::SetupQuasiRandomSongs(void)` (376B) — `?SetupQuasiRandomSongs@SongSortMgr@@QAAXXZ`

### `lazer/meta_ham/VoiceInputPanel` (3 functions)

- [ ] `private: void __cdecl VoiceInputPanel::ActivateVoiceContext(class Symbol)` (620B) — `?ActivateVoiceContext@VoiceInputPanel@@AAAXVSymbol@@@Z`
- [ ] `public: class DataNode __cdecl VoiceInputPanel::OnMsg(class SpeechRecoMessage const &)` (1944B) — `?OnMsg@VoiceInputPanel@@QAA?AVDataNode@@ABVSpeechRecoMessage@@@Z`
- [ ] `public: void __cdecl VoiceInputPanel::CreatePlaylistEditorGrammar(void) const` (480B) — `?CreatePlaylistEditorGrammar@VoiceInputPanel@@QBAXXZ`

### `system/math/Geo` (3 functions)

- [ ] `bool __cdecl Intersect(class Transform const &, class Hmx::Polygon const &, class BSPNode const *)` (500B) — `?Intersect@@YA_NABVTransform@@ABVPolygon@Hmx@@PBVBSPNode@@@Z`
- [ ] `public: void __cdecl BSPFace::Set(class Vector3const &, class Vector3const &, class Vector3const &)` (464B) — `?Set@BSPFace@@QAAXABVVector3@@00@Z`
- [ ] `void __cdecl Clip(class Hmx::Polygon const &, class Hmx::Ray const &, class Hmx::Polygon &)` (512B) — `?Clip@@YAXABVPolygon@Hmx@@ABVRay@2@AAV12@@Z`

### `lazer/meta_ham/SkeletonIdentifier` (3 functions)

- [ ] `private: class DataNode __cdecl SkeletonIdentifier::OnMsg(class SigninChangedMsg const &)` (404B) — `?OnMsg@SkeletonIdentifier@@AAA?AVDataNode@@ABVSigninChangedMsg@@@Z`
- [ ] `private: class DataNode __cdecl SkeletonIdentifier::OnMsg(class SkeletonIdentifiedMsg const &)` (1396B) — `?OnMsg@SkeletonIdentifier@@AAA?AVDataNode@@ABVSkeletonIdentifiedMsg@@@Z`
- [ ] `public: void __cdecl SkeletonIdentifier::DrawDebug(void)` (1056B) — `?DrawDebug@SkeletonIdentifier@@QAAXXZ`

### `system/rndobj/Line` (3 functions)

- [ ] `protected: void __cdecl RndLine::MapVerts(int, class RndLine::VertsMap &)` (200B) — `?MapVerts@RndLine@@IAAXHAAVVertsMap@1@@Z`
- [ ] `protected: void __cdecl RndLine::UpdateLine(class Transform const &, float)` (612B) — `?UpdateLine@RndLine@@IAAXABVTransform@@M@Z`
- [ ] `public: void __cdecl RndLine::SetPointsColor(int, int, class Hmx::Color const &)` (448B) — `?SetPointsColor@RndLine@@QAAXHHABVColor@Hmx@@@Z`

### `lazer/meta_ham/ChallengeSort` (2 functions)

- [ ] `public: virtual class SongCmp const * __cdecl NavListItemSortCmp::GetSongCmp(void) const` (80B) — `?GetSongCmp@NavListItemSortCmp@@UBAPBVSongCmp@@XZ`
- [ ] `public: virtual void __cdecl ChallengeSort::BuildTree(void)` (1120B) — `?BuildTree@ChallengeSort@@UAAXXZ`

### `system/gesture/DrawUtl` (2 functions)

- [ ] `void __cdecl DrawBufferMat(class RndMat *, class Hmx::Rect &)` (92B) — `?DrawBufferMat@@YAXPAVRndMat@@AAVRect@Hmx@@@Z`
- [ ] `void __cdecl DrawGestureMgr(class GestureMgr &, enum LiveCameraInput::BufferType, float)` (284B) — `?DrawGestureMgr@@YAXAAVGestureMgr@@W4BufferType@LiveCameraInput@@M@Z`

### `system/gesture/SkeletonViz` (2 functions)

- [ ] `public: virtual void __cdecl SkeletonViz::Poll(void)` (160B) — `?Poll@SkeletonViz@@UAAXXZ`
- [ ] `public: void __cdecl SkeletonViz::DrawPoint3D(class Vector3const &, float, class Hmx::Color const &, float)` (504B) — `?DrawPoint3D@SkeletonViz@@QAAXABVVector3@@MABVColor@Hmx@@M@Z`

### `system/rndobj/ScreenMask` (2 functions)

- [ ] `public: bool __cdecl Hmx::Rect::operator==(class Hmx::Rect const &) const` (80B) — `??8Rect@Hmx@@QBA_NABV01@@Z`
- [ ] `public: virtual void __cdecl RndScreenMask::DrawShowing(void)` (728B) — `?DrawShowing@RndScreenMask@@UAAXXZ`

### `system/world/Dir` (2 functions)

- [ ] `public: virtual void __cdecl WorldDir::DrawShowing(void)` (1136B) — `?DrawShowing@WorldDir@@UAAXXZ`
- [ ] `public: void __cdecl WorldDir::BitmapOverride::Sync(bool)` (468B) — `?Sync@BitmapOverride@WorldDir@@QAAX_N@Z`

### `system/rndobj/Morph` (2 functions)

- [ ] `public: virtual float __cdecl RndMorph::EndFrame(void)` (116B) — `?EndFrame@RndMorph@@UAAMXZ`
- [ ] `public: virtual void __cdecl RndMorph::SetFrame(float, float)` (792B) — `?SetFrame@RndMorph@@UAAXMM@Z`

### `lazer/meta_ham/FitnessCalorieSortNode` (2 functions)

- [ ] `public: virtual bool __cdecl FitnessCalorieHeaderNode::IsActive(void) const` (64B) — `?IsActive@FitnessCalorieHeaderNode@@UBA_NXZ`
- [ ] `public: virtual class NavListSortNode * __cdecl FitnessCalorieHeaderNode::GetFirstActive(void)` (124B) — `?GetFirstActive@FitnessCalorieHeaderNode@@UAAPAVNavListSortNode@@XZ`

### `system/os/ContentMgr_Xbox` (2 functions)

- [ ] `public: virtual bool __cdecl XboxContentMgr::MountContent(class Symbol)` (492B) — `?MountContent@XboxContentMgr@@UAA_NVSymbol@@@Z`
- [ ] `public: virtual void __cdecl XboxContentMgr::PollRefresh(void)` (1004B) — `?PollRefresh@XboxContentMgr@@UAAXXZ`

### `system/synth/Synth` (2 functions)

- [ ] `public: virtual class Stream * __cdecl Synth::NewStream(char const *, float, float, bool)` (88B) — `?NewStream@Synth@@UAAPAVStream@@PBDMM_N@Z`
- [ ] `public: virtual float __cdecl Synth::UpdateOverlay(class RndOverlay *, float)` (784B) — `?UpdateOverlay@Synth@@UAAMPAVRndOverlay@@M@Z`

### `system/hamobj/HamIKEffector` (2 functions)

- [ ] `protected: void __cdecl HamIKEffector::ComputeHandPullAndQuat(class QuatXfm &, class Transform &, class Transform const &, class Vector3const &)` (484B) — `?ComputeHandPullAndQuat@HamIKEffector@@IAAXAAVQuatXfm@@AAVTransform@@ABV3@ABVVector3@@@Z`
- [ ] `public: virtual void __cdecl HamIKEffector::Poll(void)` (1276B) — `?Poll@HamIKEffector@@UAAXXZ`

### `system/flow/FlowSetProperty` (2 functions)

- [ ] `public: virtual __cdecl PropertyTask::~PropertyTask(void)` (216B) — `??1PropertyTask@@UAA@XZ`
- [ ] `public: virtual void __cdecl PropertyTask::Poll(float)` (1028B) — `?Poll@PropertyTask@@UAAXM@Z`

### `system/rndobj/Part` (2 functions)

- [ ] `protected: void __cdecl RndParticleSys::InitParticle(float, class RndParticle *, class Transform const *, class PartOverride &)` (2732B) — `?InitParticle@RndParticleSys@@IAAXMPAVRndParticle@@PBVTransform@@AAVPartOverride@@@Z`
- [ ] `public: virtual void __cdecl RndParticleSys::Poll(void)` (456B) — `?Poll@RndParticleSys@@UAAXXZ`

### `system/obj/Task` (2 functions)

- [ ] `protected: void __cdecl ScriptTask::UpdateVarsObjects(class DataArray *)` (536B) — `?UpdateVarsObjects@ScriptTask@@IAAXPAVDataArray@@@Z`
- [ ] `public: void __cdecl TaskMgr::Poll(void)` (492B) — `?Poll@TaskMgr@@QAAXXZ`

### `system/synth/VorbisReader` (2 functions)

- [ ] `private: bool __cdecl VorbisReader::DoFileRead(void)` (752B) — `?DoFileRead@VorbisReader@@AAA_NXZ`
- [ ] `public: virtual void __cdecl VorbisReader::Poll(float)` (988B) — `?Poll@VorbisReader@@UAAXM@Z`

### `system/net/XLSPConnection` (2 functions)

- [ ] `private: void __cdecl XLSPConnection::SetState(enum XLSPConnection::State)` (376B) — `?SetState@XLSPConnection@@AAAXW4State@1@@Z`
- [ ] `public: void __cdecl XLSPConnection::Poll(void)` (484B) — `?Poll@XLSPConnection@@QAAXXZ`

### `system/gesture/GestureMgr` (2 functions)

- [ ] `public: class Skeleton & __cdecl Skeleton::operator=(class Skeleton const &)` (232B) — `??4Skeleton@@QAAAAV0@ABV0@@Z`
- [ ] `public: virtual void __cdecl GestureMgr::PostUpdate(struct SkeletonUpdateData const *)` (804B) — `?PostUpdate@GestureMgr@@UAAXPBUSkeletonUpdateData@@@Z`

### `system/rndobj/PropKeys` (2 functions)

- [ ] `float __cdecl CalcSpline(float, float *const)` (120B) — `?CalcSpline@@YAMMQAM@Z`
- [ ] `public: virtual int __cdecl QuatKeys::QuatAt(float, class Hmx::Quat &)` (376B) — `?QuatAt@QuatKeys@@UAAHMAAVQuat@Hmx@@@Z`

### `system/rndobj/MeshAnim` (2 functions)

- [ ] `public: virtual void __cdecl RndMeshAnim::SetFrame(float, float)` (632B) — `?SetFrame@RndMeshAnim@@UAAXMM@Z`
- [ ] `public: void __cdecl RndMeshAnim::ShrinkVerts(int)` (376B) — `?ShrinkVerts@RndMeshAnim@@QAAXH@Z`

### `system/char/CharBonesSamples` (2 functions)

- [ ] `public: virtual bool __cdecl CharBonesSamples::SyncProperty(class DataNode &, class DataArray *, int, enum PropOp)` (688B) — `?SyncProperty@CharBonesSamples@@UAA_NAAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `public: void __cdecl CharBonesSamples::EvaluateChannel(void *, int, int, float)` (944B) — `?EvaluateChannel@CharBonesSamples@@QAAXPAXHHM@Z`

### `lazer/meta_ham/AppMiniLeaderboardDisplay` (2 functions)

- [ ] `private: void __cdecl AppMiniLeaderboardDisplay::UpdateSelfInRows(void)` (608B) — `?UpdateSelfInRows@AppMiniLeaderboardDisplay@@AAAXXZ`
- [ ] `public: virtual void __cdecl AppMiniLeaderboardDisplay::Text(int, int, class UIListLabel *, class UILabel *) const` (1124B) — `?Text@AppMiniLeaderboardDisplay@@UBAXHHPAVUIListLabel@@PAVUILabel@@@Z`

### `system/obj/DataFile` (2 functions)

- [ ] `DataInput` (204B) — `DataInput`
- [ ] `class DataArray * __cdecl ParseArray(void)` (168B) — `?ParseArray@@YAPAVDataArray@@XZ`

### `system/world/Instance` (2 functions)

- [ ] `private: void __cdecl SharedGroup::AddPolls(class RndGroup *)` (176B) — `?AddPolls@SharedGroup@@AAAXPAVRndGroup@@@Z`
- [ ] `private: void __cdecl WorldInstance::SyncDir(void)` (2096B) — `?SyncDir@WorldInstance@@AAAXXZ`

### `system/rndobj/EventTrigger` (2 functions)

- [ ] `protected: static class DataNode __cdecl EventTrigger::Cleanup(class DataArray *)` (1528B) — `?Cleanup@EventTrigger@@KA?AVDataNode@@PAVDataArray@@@Z`
- [ ] `protected: void __cdecl EventTrigger::TriggerSelf(void)` (1188B) — `?TriggerSelf@EventTrigger@@IAAXXZ`

### `system/ui/InlineHelp` (2 functions)

- [ ] `public: virtual void __cdecl InlineHelp::DrawShowing(void)` (588B) — `?DrawShowing@InlineHelp@@UAAXXZ`
- [ ] `public: void __cdecl InlineHelp::ClearActionToken(enum JoypadAction)` (128B) — `?ClearActionToken@InlineHelp@@QAAXW4JoypadAction@@@Z`

### `system/rndobj/Group` (2 functions)

- [ ] `public: int __cdecl RndGroup::MoveObject(class Hmx::Object *, int)` (208B) — `?MoveObject@RndGroup@@QAAHPAVObject@Hmx@@H@Z`
- [ ] `public: virtual void __cdecl RndGroup::DrawShowing(void)` (588B) — `?DrawShowing@RndGroup@@UAAXXZ`

### `system/hamobj/SongLayout` (2 functions)

- [ ] `public: void __cdecl SongLayout::SetDefaultPattern(int)` (368B) — `?SetDefaultPattern@SongLayout@@QAAXH@Z`
- [ ] `public: void __cdecl SongLayout::SetDefaultReplacer(void)` (652B) — `?SetDefaultReplacer@SongLayout@@QAAXXZ`

### `system/synth/Sound` (2 functions)

- [ ] `public: void __cdecl Sound::SetPan(float, class Hmx::Object *)` (328B) — `?SetPan@Sound@@QAAXMPAVObject@Hmx@@@Z`
- [ ] `public: void __cdecl Sound::SetSpeed(float, class Hmx::Object *)` (300B) — `?SetSpeed@Sound@@QAAXMPAVObject@Hmx@@@Z`

### `system/ui/UILabel` (1 functions)

- [ ] `public: __cdecl UILabel::LabelStyle::~LabelStyle(void)` (124B) — `??1LabelStyle@UILabel@@QAA@XZ`

### `system/moviebink/BinkMovieImpl` (1 functions)

- [ ] `public: virtual __cdecl MovieImpl::~MovieImpl(void)` (16B) — `??1MovieImpl@@UAA@XZ`

### `system/char/FileMergerOrganizer` (1 functions)

- [ ] `public: bool __cdecl FileMergerSort::operator()(struct FileMerger::Merger const *, struct FileMerger::Merger const *)` (644B) — `??RFileMergerSort@@QAA_NPBUMerger@FileMerger@@0@Z`

### `system/char/CharClip` (1 functions)

- [ ] `public: void __cdecl CharClip::Transitions::AddNode(class CharClip *, struct CharGraphNode const &)` (484B) — `?AddNode@Transitions@CharClip@@QAAXPAV2@ABUCharGraphNode@@@Z`

### `system/synth/Utl` (1 functions)

- [ ] `char const * __cdecl CacheWav(char const *, enum CacheResourceResult &)` (288B) — `?CacheWav@@YAPBDPBDAAW4CacheResourceResult@@@Z`

### `system/synth/SynthSample` (1 functions)

- [ ] `public: virtual void __cdecl SynthSample::Copy(class Hmx::Object const *, enum Hmx::Object::CopyType)` (120B) — `?Copy@SynthSample@@UAAXPBVObject@Hmx@@W4CopyType@23@@Z`

### `system/synth_xbox/FxSendBitCrush` (1 functions)

- [ ] `protected: virtual struct IUnknown * __cdecl FxSendBitCrush360::CreateFx(void)` (72B) — `?CreateFx@FxSendBitCrush360@@MAAPAUIUnknown@@XZ`

### `system/synth_xbox/FxSendDistortion` (1 functions)

- [ ] `protected: virtual struct IUnknown * __cdecl FxSendDistortion360::CreateFx(void)` (72B) — `?CreateFx@FxSendDistortion360@@MAAPAUIUnknown@@XZ`

### `system/synth_xbox/FxSendReverb` (1 functions)

- [ ] `protected: virtual struct IUnknown * __cdecl FxSendReverb360::CreateFx(void)` (40B) — `?CreateFx@FxSendReverb360@@MAAPAUIUnknown@@XZ`

### `system/rndobj/DOFProc_NG` (1 functions)

- [ ] `public: virtual void __cdecl NgDOFProc::DoPost(void)` (1088B) — `?DoPost@NgDOFProc@@UAAXXZ`

### `system/world/PhysicsVolume` (1 functions)

- [ ] `public: virtual void __cdecl PhysicsVolume::DrawShowing(void)` (476B) — `?DrawShowing@PhysicsVolume@@UAAXXZ`

### `system/rndobj/TexBlender` (1 functions)

- [ ] `public: virtual void __cdecl RndTexBlender::DrawShowing(void)` (2636B) — `?DrawShowing@RndTexBlender@@UAAXXZ`

### `system/hamobj/HamListRibbon` (1 functions)

- [ ] `public: virtual float __cdecl HamListRibbon::EndFrame(void)` (296B) — `?EndFrame@HamListRibbon@@UAAMXZ`

### `lazer/meta_ham/SongSortNode` (1 functions)

- [ ] `public: virtual class NavListSortNode * __cdecl SongHeaderNode::GetFirstActive(void)` (124B) — `?GetFirstActive@SongHeaderNode@@UAAPAVNavListSortNode@@XZ`

### `system/hamobj/MoveGraph` (1 functions)

- [ ] `private: class MoveParent * __cdecl MoveGraph::GetNonConstMoveParent(class Symbol) const` (76B) — `?GetNonConstMoveParent@MoveGraph@@ABAPAVMoveParent@@VSymbol@@@Z`

### `system/rndobj/FontBase` (1 functions)

- [ ] `public: virtual class DataNode __cdecl UIListWidget::Handle(class DataArray *, bool)` (292B) — `?Handle@UIListWidget@@UAA?AVDataNode@@PAVDataArray@@_N@Z`

### `system/os/User` (1 functions)

- [ ] `public: virtual bool const __cdecl XGRAPHICS::IRLoadConst::IsLoadConst(void) const` (8B) — `?IsLoadConst@IRLoadConst@XGRAPHICS@@UBA?B_NXZ`

### `system/char/ClipCollide` (1 functions)

- [ ] `public: void __cdecl Transform::LookAt(class Vector3const &, class Vector3const &)` (96B) — `?LookAt@Transform@@QAAXABVVector3@@0@Z`

### `lazer/meta_ham/NavListNode` (1 functions)

- [ ] `public: virtual class NavListHeaderNode * __cdecl SongSortByDiff::NewHeaderNode(class NavListItemNode *, class NavListItemNode *) const` (16B) — `?NewHeaderNode@SongSortByDiff@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z`

### `system/meta/StorePanel` (1 functions)

- [ ] `public: virtual void __cdecl StoreEnumJob::OnCompletion(class Hmx::Object *)` (108B) — `?OnCompletion@StoreEnumJob@@UAAXPAVObject@Hmx@@@Z`

### `system/hamobj/HamMove` (1 functions)

- [ ] `public: float __cdecl HamMove::PSNRToDetectFrac(float) const` (316B) — `?PSNRToDetectFrac@HamMove@@QBAMM@Z`

### `system/char/CharLipSyncDriver` (1 functions)

- [ ] `public: virtual void __cdecl CharLipSyncDriver::Poll(void)` (2068B) — `?Poll@CharLipSyncDriver@@UAAXXZ`

### `system/synth/Emitter` (1 functions)

- [ ] `public: virtual void __cdecl SynthEmitter::Poll(void)` (456B) — `?Poll@SynthEmitter@@UAAXXZ`

### `system/synth_xbox/PitchCorrectedVoice` (1 functions)

- [ ] `public: void __cdecl TrueColor::ExposureRecipe::SetMinIntegrationTime(float)` (8B) — `?SetMinIntegrationTime@ExposureRecipe@TrueColor@@QAAXM@Z`

### `system/synth/MidiInstrument` (1 functions)

- [ ] `public: virtual void __cdecl MidiInstrument::SynthPoll(void)` (228B) — `?SynthPoll@MidiInstrument@@UAAXXZ`

### `system/os/Memcard_Xbox` (1 functions)

- [ ] `public: void __cdecl VirtualKeyboard::Terminate(void)` (4B) — `?Terminate@VirtualKeyboard@@QAAXXZ`

### `system/midi/MidiReader` (1 functions)

- [ ] `float __cdecl pow(float, int)` (80B) — `?pow@@YAMMH@Z`

### `system/os/File` (1 functions)

- [ ] `FileRecursePattern` (8B) — `FileRecursePattern`

### `lazer/meta_ham/SongSort` (1 functions)

- [ ] `public: __cdecl NavListFunctionNode::NavListFunctionNode(class NavListItemSortCmp *, class Symbol, char const *)` (76B) — `??0NavListFunctionNode@@QAA@PAVNavListItemSortCmp@@VSymbol@@PBD@Z`

### `system/net/WebSvcReq` (1 functions)

- [ ] `public: __cdecl RecurseInfo::RecurseInfo(struct RecurseInfo const &)` (56B) — `??0RecurseInfo@@QAA@ABU0@@Z`

### `system/char/CharLipSync` (1 functions)

- [ ] `public: void __cdecl CharLipSync::Generator::AddWeight(int, float)` (192B) — `?AddWeight@Generator@CharLipSync@@QAAXHM@Z`

### `system/utl/BinkIntegration` (1 functions)

- [ ] `unsigned int __cdecl BinkFileIdle(struct BINKIO *)` (116B) — `?BinkFileIdle@@YAIPAUBINKIO@@@Z`

### `lazer/meta_ham/MetaPerformer` (1 functions)

- [ ] `protected: bool __cdecl MetaPerformer::CheckRecommendedPracticeMove(class String, int) const` (368B) — `?CheckRecommendedPracticeMove@MetaPerformer@@IBA_NVString@@H@Z`

### `system/utl/FileStream` (1 functions)

- [ ] `private: void __cdecl FileStream::DeleteChecksum(void)` (72B) — `?DeleteChecksum@FileStream@@AAAXXZ`

### `system/rndobj/PropAnim` (1 functions)

- [ ] `public: class DataNode __cdecl RndPropAnim::ForeachKeyframe(class DataArray const *)` (2468B) — `?ForeachKeyframe@RndPropAnim@@QAA?AVDataNode@@PBVDataArray@@@Z`

### `system/os/UsbMidiKeyboard` (1 functions)

- [ ] `public: bool __cdecl UsbMidiKeyboard::GetSustain(int)` (12B) — `?GetSustain@UsbMidiKeyboard@@QAA_NH@Z`

### `system/rndobj/MetaMaterial` (1 functions)

- [ ] `public: bool __cdecl MetaMaterial::IsEquivalent(class MetaMaterial *)` (200B) — `?IsEquivalent@MetaMaterial@@QAA_NPAV1@@Z`

### `system/os/AsyncFile` (1 functions)

- [ ] `public: bool __cdecl TrueColor::RecipeTable::IsMaxGain(void) const` (8B) — `?IsMaxGain@RecipeTable@TrueColor@@QBA_NXZ`

### `lazer/meta_ham/MainMenuPanel` (1 functions)

- [ ] `private: void __cdecl MainMenuPanel::LoadArt(class String)` (204B) — `?LoadArt@MainMenuPanel@@AAAXVString@@@Z`

### `system/obj/Dir` (1 functions)

- [ ] `public: static class Loader * __cdecl DirLoader::New(class FilePath const &, enum LoaderPos)` (116B) — `?New@DirLoader@@SAPAVLoader@@ABVFilePath@@W4LoaderPos@@@Z`

### `system/hamobj/HollaBackMinigame` (1 functions)

- [ ] `public: void __cdecl HollaBackMinigame::OnBeat(void)` (2796B) — `?OnBeat@HollaBackMinigame@@QAAXXZ`

### `system/flow/FlowSound` (1 functions)

- [ ] `protected: void __cdecl FlowSound::OnMarkerEvent(class Symbol)` (2068B) — `?OnMarkerEvent@FlowSound@@IAAXVSymbol@@@Z`

### `system/world/CameraManager` (1 functions)

- [ ] `public: void __cdecl CameraManager::Poll(void)` (1128B) — `?Poll@CameraManager@@QAAXXZ`

### `system/hamobj/FilterQueue` (1 functions)

- [ ] `public: void __cdecl FilterQueue::Poll(struct SkeletonUpdateData const &)` (556B) — `?Poll@FilterQueue@@QAAXABUSkeletonUpdateData@@@Z`

### `system/utl/Loader` (1 functions)

- [ ] `private: void __cdecl LoadMgr::PollFrontLoader(void)` (636B) — `?PollFrontLoader@LoadMgr@@AAAXXZ`

### `system/ui/UIList` (1 functions)

- [ ] `public: int __cdecl UIList::Selected(void) const` (8B) — `?Selected@UIList@@QBAHXZ`

### `system/os/HolmesKeyboard` (1 functions)

- [ ] `public: unsigned int __cdecl HolmesInput::SendJoypadMessages(void)` (412B) — `?SendJoypadMessages@HolmesInput@@QAAIXZ`

### `system/utl/Song` (1 functions)

- [ ] `public: void __cdecl Song::SyncState(void)` (1160B) — `?SyncState@Song@@QAAXXZ`

### `system/hamobj/HamScrollSpeedIndicator` (1 functions)

- [ ] `public: void __cdecl HamScrollSpeedIndicator::Update(float, float, float)` (384B) — `?Update@HamScrollSpeedIndicator@@QAAXMMM@Z`

### `system/flow/FlowSlider` (1 functions)

- [ ] `protected: void __cdecl FlowSlider::UpdateActivations(void)` (764B) — `?UpdateActivations@FlowSlider@@IAAXXZ`

### `system/ui/LabelShrinkWrapper` (1 functions)

- [ ] `protected: void __cdecl LabelShrinkWrapper::UpdateAndDrawWrapper(void)` (564B) — `?UpdateAndDrawWrapper@LabelShrinkWrapper@@IAAXXZ`

### `system/hamobj/HamRibbon` (1 functions)

- [ ] `public: void __cdecl HamRibbon::UpdateChase(void)` (1648B) — `?UpdateChase@HamRibbon@@QAAXXZ`

### `system/synth/filterdesign` (1 functions)

- [ ] `void __cdecl createFilter(enum FilterType, enum FilterBand, unsigned int, float, float, struct FILTER *, int)` (472B) — `?createFilter@@YAXW4FilterType@@W4FilterBand@@IMMPAUFILTER@@H@Z`

---

## Functions NOT in Database

**188 symbols** not tracked in `decomp.db`.
May be from headers, SDK, or small utility classes not split by jeff.

- [ ] `??0LocationCmp@@QAA@XZ`
- [ ] `??1AppLabel@@UAA@XZ`
- [ ] `??1CriticalSection@@QAA@XZ`
- [ ] `??1DifficultyCmp@@UAA@XZ`
- [ ] `??1FitnessCalorieSortByCalorie@@UAA@XZ`
- [ ] `??1FitnessCalorieSortCmp@@UAA@XZ`
- [ ] `??1MQSongSortByCharacter@@UAA@XZ`
- [ ] `??1MQSongSortNode@@UAA@XZ`
- [ ] `??1SongCmp@@UAA@XZ`
- [ ] `??1SongSortByLocation@@UAA@XZ`
- [ ] `??RSortCmp@@QBA_NPBVStoreOffer@@0@Z`
- [ ] `??_7ChallengeScoreCmp@@6B@`
- [ ] `??_R0PAUSongCollisionOutput@@@8`
- [ ] `?BinkClose@@YAXPAUBINK@@@Z`
- [ ] `?BinkCloseTrack@@YAXPAUBINKTRACK@@@Z`
- [ ] `?BinkGetTrackData@@YAIPAUBINKTRACK@@PAX@Z`
- [ ] `?BinkNextFrame@@YAXPAUBINK@@@Z`
- [ ] `?BinkOpenTrack@@YAPAUBINKTRACK@@PAUBINK@@E@Z`
- [ ] `?BinkSetMemory@@YAXP6APAXH@ZP6AXPAX@Z@Z`
- [ ] `?BinkStartAsyncThread@@YAHHH@Z`
- [ ] `?CHARACTERS@?1??StrToCharacterSym@@YA?AVSymbol@@VString@@@Z@4V2@A`
- [ ] `?CREWS@?1??StrToCrewSym@@YA?AVSymbol@@VString@@@Z@4V2@A`
- [ ] `?DataOwner@RndFont3d@@UBAPBVRndFontBase@@XZ`
- [ ] `?DrawFixedZ@DrawString@@UAAXM@Z`
- [ ] `?DrawShowing@SpotlightDrawer@@UAAXXZ`
- [ ] `?ExitStore@StorePanel@@UBAXW4StoreError@@@Z`
- [ ] `?Flush@HDCache@@AAAXXZ`
- [ ] `?GetAlbumCmp@NavListItemSortCmp@@UBAPBVAlbumCmp@@XZ`
- [ ] `?GetArtistCmp@NavListItemSortCmp@@UBAPBVArtistCmp@@XZ`
- [ ] `?GetBaseFileName@SongInfoCopy@@UBAPBDXZ`
- [ ] `?GetBufferSize@HttpGet@@QAAIXZ`
- [ ] `?GetChallengeScoreCmp@NavListItemSortCmp@@UBAPBVChallengeScoreCmp@@XZ`
- [ ] `?GetColor@UIColor@@QBAABVColor@Hmx@@XZ`
- [ ] `?GetDateCmp@NavListItemSortCmp@@UBAPBVDateCmp@@XZ`
- [ ] `?GetDecadeCmp@NavListItemSortCmp@@UBAPBVDecadeCmp@@XZ`
- [ ] `?GetDifficultyCmp@NavListItemSortCmp@@UBAPBVDifficultyCmp@@XZ`
- [ ] `?GetFailType@NetCacheLoader@@QBA?AW4NetCacheMgrFailType@@XZ`
- [ ] `?GetFitnessCalorieSortCmp@NavListItemSortCmp@@UBAPBVFitnessCalorieSortCmp@@XZ`
- [ ] `?GetJumpBackTotalTime@StandardStream@@UAAMM@Z`
- [ ] `?GetLastResult@Cache@@QAA?AW4CacheResult@@XZ`
- [ ] `?GetLocationCmp@NavListItemSortCmp@@UBAPBVLocationCmp@@XZ`
- [ ] `?GetMQSongCharCmp@NavListItemSortCmp@@UBAPBVMQSongCharCmp@@XZ`
- [ ] `?GetName@MicXbox@@UBAAAVSymbol@@XZ`
- [ ] `?GetName@SongInfoCopy@@UBA?AVSymbol@@XZ`
- [ ] `?GetNumRestarts@Game@@QBAHXZ`
- [ ] `?GetPlaylistTypeCmp@NavListItemSortCmp@@UBAPBVPlaylistTypeCmp@@XZ`
- [ ] `?GetSlipOffset@StreamReceiverFile@@UAAMXZ`
- [ ] `?GetVenueCmp@NavListItemSortCmp@@UBAPBVVenueCmp@@XZ`
- [ ] `?GetVocalPartsCmp@NavListItemSortCmp@@UBAPBVVocalPartsCmp@@XZ`
- [ ] `?Handle@BustAMoveData@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@CharMeshHide@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@FitnessCalorieSortMgr@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@OvershellSlot@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@RndFont3d@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@UIListArrow@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@UIListLabel@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@UIListMesh@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@UIListSlot@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Handle@UIListSubList@@UAA?AVDataNode@@PAVDataArray@@_N@Z`
- [ ] `?Highlight@Waypoint@@UAAXXZ`
- [ ] `?INTRO_CAM_CATS@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?INTRO_PLAYLIST@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?INTRO_QUICK@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?INTRO_SKILLS@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?INTRO_SKILLS_LONG@?7??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?InsertBreak@RndConsole@@QAAXPAVDataArray@@H@Z`
- [ ] `?Intersect@@YA_NABVSegment@@ABVTriangle@@HAAM@Z`
- [ ] `?IsDifficultyUnlockedForProfile@HamProfile@@QAA_NVSymbol@@0@Z`
- [ ] `?Mat@RndFont3d@@UBAPAVRndMat@@XZ`
- [ ] `?NewHeaderNode@ChallengeSortByScore@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z`
- [ ] `?NewHeaderNode@FitnessCalorieSortByCalorie@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z`
- [ ] `?NewHeaderNode@MQSongSortByCharacter@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z`
- [ ] `?NewHeaderNode@SongSortByLocation@@UBAPAVNavListHeaderNode@@PAVNavListItemNode@@0@Z`
- [ ] `?OUTRO_CAM_CATS@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?OnMsg@HamUI@@IAA?AVDataNode@@ABVConnectionStatusChangedMsg@@@Z`
- [ ] `?OnParametersChanged@FxSendFlanger360@@MAAXXZ`
- [ ] `?OnSelect@NgPostProc@@UAAXXZ`
- [ ] `?OnSmartGlassListen@FitnessGoalMgr@@AAAXH@Z`
- [ ] `?OnSync@DxMesh@@UAAXH@Z`
- [ ] `?OnSync@RndMesh@@UAAXH@Z`
- [ ] `?OnUnselect@NgPostProc@@UAAXXZ`
- [ ] `?Poll@LabelShrinkWrapper@@UAAXXZ`
- [ ] `?Poll@RandomIntervalGroupSeqInst@@UAAXXZ`
- [ ] `?PresyncBitmap@RndTex@@UAAXXZ`
- [ ] `?RadAlloc@@YAPAXH@Z`
- [ ] `?RemoveFromLists@Spotlight@@SAXPAV1@@Z`
- [ ] `?Rest@?1??IsRest@MoveVariant@@QBA_NXZ@4VSymbol@@A`
- [ ] `?RootTrans@UIListLabel@@UAAPAVRndTransformable@@XZ`
- [ ] `?RootTrans@UIListSubList@@UAAPAVRndTransformable@@XZ`
- [ ] `?Select@ChallengeHeaderNode@@UAA?AVSymbol@@XZ`
- [ ] `?Set@NgDOFProc@@UAAXPAVRndCam@@MMMM@Z`
- [ ] `?SetVConstant@DxShaderMgr@@UAAXW4VShaderConstant@@PBMI@Z`
- [ ] `?SpewInit@@YAXXZ`
- [ ] `?SpewTerminate@@YAXXZ`
- [ ] `?StartImpl@RandomIntervalGroupSeqInst@@UAAXXZ`
- [ ] `?StoreProfile@StorePanel@@UBAPAVProfile@@XZ`
- [ ] `?SyncBitmap@DxTex@@UAAXXZ`
- [ ] `?SyncBitmap@RndTex@@UAAXXZ`
- [ ] `?Terminate@UILabel@@SAXXZ`
- [ ] `?UpdateApproxLighting@RndEnviron@@UAAXPBVVector3@@@Z`
- [ ] `?UpdateGestures@HamNavList@@AAAXPBVSkeleton@@@Z`
- [ ] `?ValidateCRC@CRC@Hmx@@SA_NHPBD@Z`
- [ ] `?WIN_HYPE_DIFF_CREW@?EB@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?WIN_HYPE_SOLO@?EB@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?active@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?all@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?altCfg@@YA?AVDataArrayPtr@@VDataNode@@0@Z`
- [ ] `?battle_intro_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?battle_outro_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?camp_intro_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?dispose@Voice@@AAAXPAHI@Z`
- [ ] `?groove@?1??IsRest@MoveVariant@@QBA_NXZ@4VSymbol@@A`
- [ ] `?high@?FG@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?intro_playlist@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?intro_quick@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?intro_skills@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?kArkBlockSize@@3HB`
- [ ] `?kStreamEndMs@StandardStream@@2MB`
- [ ] `?lose_camp_char@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?low@?FG@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?med@?FG@??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?merged_82610090@@YAPAXPBXPAI@Z`
- [ ] `?merged_82610090@@YAPBDPBDPCH@Z`
- [ ] `?rest@?1??IsRest@MoveVariant@@QBA_NXZ@4VSymbol@@A`
- [ ] `?sBlacklightModeEnabled@RndText@@1_NA`
- [ ] `?sCheatFinale@MetaPerformer@@0_NA`
- [ ] `?sCurrentSkinned@RndShader@@1_NA`
- [ ] `?sCurrentUseAO@RndShader@@1_NA`
- [ ] `?sDebugHighlight@UILabel@@1_NA`
- [ ] `?sDisableAll@FileMerger@@1_NA`
- [ ] `?sDisableEyeClamping@CharEyes@@1_NA`
- [ ] `?sDisableEyeDart@CharEyes@@1_NA`
- [ ] `?sDisableEyeJitter@CharEyes@@1_NA`
- [ ] `?sDisableInterestObjects@CharEyes@@1_NA`
- [ ] `?sDisableProceduralBlink@CharEyes@@1_NA`
- [ ] `?sEDRamChecksEnabled@DxTex@@0_NA`
- [ ] `?sGameRecord2Player@MoveDir@@2_NA`
- [ ] `?sGameRecord@MoveDir@@2_NA`
- [ ] `?sHasFlippedTextThisRotation@InlineHelp@@0_NA`
- [ ] `?sLastSelectInControllerMode@HamNavList@@2_NA`
- [ ] `?sMatShadersOK@RndShader@@1_NA`
- [ ] `?sMotdCheat@MetaPanel@@2_NA`
- [ ] `?sNeedDraw@SpotlightDrawer@@1_NA`
- [ ] `?sNeedsTextUpdate@InlineHelp@@0_NA`
- [ ] `?sRequireFixedLength@UILabel@@2_NA`
- [ ] `?sRotated@InlineHelp@@0_NA`
- [ ] `?sShowing@PhysicsVolume@@2_NA`
- [ ] `?sUnlockAll@MetaPanel@@2_NA`
- [ ] `?win_camp_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?win_dlg_char@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?win_hype_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?win_hype_diff_crew@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?win_hype_solo@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `?win_mov_char@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4V2@A`
- [ ] `BinkInit`
- [ ] `Curl_if2ip`
- [ ] `FD_SET`
- [ ] `FFTRealForward`
- [ ] `HIBYTE`
- [ ] `JoypadSetActuatorsImp`
- [ ] `LOBYTE`
- [ ] `MAKEWORD`
- [ ] `__real_0000000000000000`
- [ ] `__real_3f50624dd2f1a9fc`
- [ ] `__real_3fe0000000000000`
- [ ] `__real_4000000000000000`
- [ ] `__real_400921fb60000000`
- [ ] `__real_401921fb60000000`
- [ ] `_close`
- [ ] `_fstati64`
- [ ] `cexp`
- [ ] `expand`
- [ ] `expj`
- [ ] `htons`
- [ ] `hypot`
- [ ] `jumptable_8202C1D0`
- [ ] `jumptable_82066CC0`
- [ ] `jumptable_82066CD0`
- [ ] `jumptable_82070798`
- [ ] `jumptable_820707C0`
- [ ] `jumptable_820707E8`
- [ ] `jumptable_82070810`
- [ ] `jumptable_8209C900`
- [ ] `jumptable_8209C910`
- [ ] `ntohs`
- [ ] `read`
- [ ] `strncasecmp`
- [ ] `wmemcpy`

---

## Template Instantiations

**397 stubs.** Resolve when the right headers/templates
are included and the right types are used in decomp source.

### ObjPtrList (138)

- [ ] `??$?6VCamShot@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCamShot@@VObjectDir@@@@@Z`
- [ ] `??$?6VCharBone@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCharBone@@VObjectDir@@@@@Z`
- [ ] `??$?6VEventTrigger@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VEventTrigger@@VObjectDir@@@@@Z`
- [ ] `??$?6VEventTrigger@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VEventTrigger@@VObjectDir@@@@@Z`
- [ ] `??$?6VHamCamShot@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VHamCamShot@@VObjectDir@@@@@Z`
- [ ] `??$?6VHamCamShot@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VHamCamShot@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndFontBase@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndFontBase@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndMat@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndMat@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndMat@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndMat@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndPartLauncher@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndPartLauncher@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndTexBlendController@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@@Z`
- [ ] `??$?6VSequence@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VSequence@@VObjectDir@@@@@Z`
- [ ] `??$?6VSequence@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VSequence@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCharPollable@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VFader@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndDrawable@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VRndLight@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrList@VWaypoint@@VObjectDir@@@@@Z`
- [ ] `?Hookup@CharHair@@QAAXAAV?$ObjPtrList@VCharCollide@@VObjectDir@@@@@Z`
- [ ] `?Link@?$ObjPtrList@VCamShot@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VCharBone@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VCharCollide@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VCharInterest@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VCharPollable@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VEventTrigger@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VFader@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VObjectDir@@V1@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VRndLight@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VRndMat@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VRndMesh@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VRndPartLauncher@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VSeqInst@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VSequence@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VSfxInst@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VUILabel@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VWaypoint@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?Link@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@AAAXViterator@1@PAUNode@1@@Z`
- [ ] `?RefOwner@?$ObjPtrList@VCamShot@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VCharBone@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VCharCollide@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VCharInterest@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VCharPollable@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VFader@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VFlowNode@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VObjectDir@@V1@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VRndLight@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VRndMat@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VSeqInst@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VSfxInst@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VWaypoint@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@EBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VCamShot@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VCharBone@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VCharCollide@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VCharInterest@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VCharPollable@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VFader@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VObjectDir@@V1@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VRndLight@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VRndMat@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VSeqInst@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VSfxInst@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VWaypoint@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?Replace@?$ObjPtrList@VCharBone@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VCharCollide@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VCharInterest@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VCharPollable@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VFader@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VFlowNode@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VObjectDir@@V1@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VRndLight@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VRndMat@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VSeqInst@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VSfxInst@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VWaypoint@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@EAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Unlink@?$ObjPtrList@VCamShot@@VObjectDir@@@@AAAPAUNode@1@PAU21@@Z`
- [ ] `?erase@?$ObjPtrList@VCamShot@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VCharBone@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VCharCollide@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VCharInterest@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VCharPollable@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VCharWeightSetter@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VCharWeightable@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VFader@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VHamCamShot@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VObjectDir@@V1@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VRndLight@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VRndMat@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VRndTexBlendController@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VSeqInst@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VSfxInst@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VThreeDSound@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VWaypoint@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrList@VWorldCrowd@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?front@?$ObjPtrList@VFader@@VObjectDir@@@@QBAPAVFader@@XZ`
- [ ] `?front@?$ObjPtrList@VNoteVoiceInst@@VObjectDir@@@@QBAPAVNoteVoiceInst@@XZ`
- [ ] `?front@?$ObjPtrList@VObjectDir@@V1@@@QBAPAVObjectDir@@XZ`
- [ ] `?front@?$ObjPtrList@VRndFontBase@@VObjectDir@@@@QBAPAVRndFontBase@@XZ`
- [ ] `?front@?$ObjPtrList@VRndMat@@VObjectDir@@@@QBAPAVRndMat@@XZ`
- [ ] `?front@?$ObjPtrList@VRndTransformable@@VObjectDir@@@@QBAPAVRndTransformable@@XZ`
- [ ] `?front@?$ObjPtrList@VSeqInst@@VObjectDir@@@@QBAPAVSeqInst@@XZ`
- [ ] `?front@?$ObjPtrList@VTask@@VObjectDir@@@@QBAPAVTask@@XZ`
- [ ] `?merged_ObjPtrListPopBack@@YAXPAX@Z`
- [ ] `?sort@?$ObjPtrList@VCharBone@@VObjectDir@@@@QAAXP6A_NPAVCharBone@@0@Z@Z`
- [ ] `?sort@?$ObjPtrList@VRndDrawable@@VObjectDir@@@@QAAXP6A_NPAVRndDrawable@@0@Z@Z`

### ObjRef (66)

- [ ] `?CopyRef@?$ObjRefConcrete@VADSR@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VBaseMaterial@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCamShot@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharBonesObject@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharClip@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharCollide@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharDriver@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharEyeDartRuleset@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharFaceServo@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharIKFoot@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharLipSync@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharLipSyncDriver@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharLookAt@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharPollable@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharWeightSetter@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VCharacter@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VDancerSequence@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VFader@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VFlow@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VFxSend@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VFxSendMeterEffect@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VHamCamShot@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VHamIKEffector@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VHamIKSkeleton@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VHamLabel@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VHamMove@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VHamNavProvider@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VHamPhraseMeter@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VLightHue@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VLightPreset@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VMetaMaterial@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VObject@Hmx@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRhythmBattlePlayer@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndCam@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndCubeTex@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndDir@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndEnviron@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndFontBase@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndFur@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndGroup@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndLight@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndMat@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndMultiMesh@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndPostProc@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndPropAnim@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VRndTransAnim@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VSeqInst@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VSequence@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VSfx@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VSkeletonClip@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VSound@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VSpotlight@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VSynthSample@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VUIColor@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VUIComponent@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VUILabelDir@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VUIList@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?CopyRef@?$ObjRefConcrete@VWorldCrowd@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?Replace@CharBonesMeshes@@MAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@CharEyes@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@LightPreset@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@PropertyTask@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@RndEnviron@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@RndMatAnim@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@RndMeshAnim@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z`
- [ ] `?Replace@RndParticleSys@@UAA_NPAVObjRef@@PAVObject@Hmx@@@Z`

### ObjPtrVec (65)

- [ ] `??$?6VCharClip@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VCharClip@@VObjectDir@@@@@Z`
- [ ] `??$?6VFlow@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VFlow@@VObjectDir@@@@@Z`
- [ ] `??$?6VObject@Hmx@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@@Z`
- [ ] `??$?6VRhythmDetector@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@@Z`
- [ ] `??$?6VRhythmDetector@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndDrawable@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndEnviron@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndEnviron@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndLight@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndLight@@VObjectDir@@@@@Z`
- [ ] `??$?6VRndMat@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndMat@@VObjectDir@@@@@Z`
- [ ] `??$?6VSpotlight@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VSpotlight@@VObjectDir@@@@@Z`
- [ ] `??$?6VSpotlightDrawer@@@@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VSpotlightDrawer@@VObjectDir@@@@@Z`
- [ ] `??$PropSync@VCharClip@@@@YA_NAAV?$ObjPtrVec@VCharClip@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??$PropSync@VFlow@@@@YA_NAAV?$ObjPtrVec@VFlow@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??$PropSync@VObject@Hmx@@@@YA_NAAV?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??$PropSync@VRhythmDetector@@@@YA_NAAV?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??$PropSync@VRndDrawable@@@@YA_NAAV?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??$PropSync@VRndMat@@@@YA_NAAV?$ObjPtrVec@VRndMat@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??$PropSync@VRndTransformable@@@@YA_NAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??$PropSync@VRndTransformable@@@@YA_NAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??0EventTask@@QAA@PAVFlowTimer@@PAV?$ObjPtrVec@VFlowNode@@VObjectDir@@@@W4TaskUnits@@M@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VFlowNode@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjPtrVec@VWaypoint@@VObjectDir@@@@@Z`
- [ ] `?FindRef@?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@QAA?AViterator@1@PAVObjRef@@@Z`
- [ ] `?PopClipPlanesInternal@DxRnd@@UAAXAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@@Z`
- [ ] `?PushClipPlanesInternal@DxRnd@@UAAXAAV?$ObjPtrVec@VRndTransformable@@VObjectDir@@@@@Z`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VCharClip@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VFlow@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VFlowLabel@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VHamCharacter@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VHamMove@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VRndEnviron@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VRndGroup@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VRndLight@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VRndMat@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VRndTex@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VSpotlight@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VSpotlightDrawer@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?RefOwner@Node@?$ObjPtrVec@VWaypoint@@VObjectDir@@@@UBAPAVObject@Hmx@@XZ`
- [ ] `?ScanForOutPorts@@YAXAAV?$ObjPtrVec@VFlowOutPort@@VObjectDir@@@@PAVFlowNode@@PAVFlow@@@Z`
- [ ] `?erase@?$ObjPtrVec@VCharClip@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VFlow@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VFlowLabel@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VHamCharacter@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VHamMove@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VRhythmDetector@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VRndEnviron@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VRndGroup@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VRndLight@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VRndMat@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VRndTex@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VSpotlight@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VSpotlightDrawer@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?erase@?$ObjPtrVec@VWaypoint@@VObjectDir@@@@QAA?AViterator@1@V21@@Z`
- [ ] `?merge@?$ObjPtrVec@VRndDrawable@@VObjectDir@@@@QAAXABV1@@Z`
- [ ] `?remove@?$ObjPtrVec@VFlowNode@@VObjectDir@@@@QAA_NPAVFlowNode@@@Z`
- [ ] `?swap@?$ObjPtrVec@VCharClip@@VObjectDir@@@@QAAXHH@Z`
- [ ] `?unique@?$ObjPtrVec@VObject@Hmx@@VObjectDir@@@@QAAXXZ`

### STL/Other (57)

- [ ] `??$GatherObjectsFromGroup@VRndMesh@@@@YAIPAVRndGroup@@AAV?$vector@PAVRndMesh@@V?$StlNodeAlloc@PAVRndMesh@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `??$_M_allocate_and_copy@PAVFace@RndMesh@@@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@IAAPAVFace@RndMesh@@IPAV23@0@Z`
- [ ] `??$_M_find@H@?$_Rb_tree@HU?$less@H@stlpmtx_std@@U?$pair@$$CBHVSongStatus@@@2@U?$_Select1st@U?$pair@$$CBHVSongStatus@@@stlpmtx_std@@@2@U?$_MapTraitsT@U?$pair@$$CBHVSongStatus@@@stlpmtx_std@@@priv@2@V?$StlNodeAlloc@U?$pair@$$CBHVSongStatus@@@stlpmtx_std@@@2@@stlpmtx_std@@ABAPAU_Rb_tree_node_base@1@ABH@Z`
- [ ] `??$_M_find@VString@@@?$_Rb_tree@VString@@U?$less@VString@@@stlpmtx_std@@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@3@U?$_Select1st@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@$$CBVString@@VChallengeBadgeInfo@@@stlpmtx_std@@@3@@stlpmtx_std@@ABAPAU_Rb_tree_node_base@1@ABVString@@@Z`
- [ ] `??$_M_find@VSymbol@@@?$_Rb_tree@VSymbol@@U?$less@VSymbol@@@stlpmtx_std@@U?$pair@$$CBVSymbol@@_N@3@U?$_Select1st@U?$pair@$$CBVSymbol@@_N@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@$$CBVSymbol@@_N@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@$$CBVSymbol@@_N@stlpmtx_std@@@3@@stlpmtx_std@@ABAPAU_Rb_tree_node_base@1@ABVSymbol@@@Z`
- [ ] `??$__uninitialized_copy@PAVTriangle@@PAV1@@stlpmtx_std@@YAPAVTriangle@@PAV1@00ABU__false_type@0@@Z`
- [ ] `??$__uninitialized_copy@PBVSampleMarker@@PAV1@@stlpmtx_std@@YAPAVSampleMarker@@PBV1@0PAV1@ABU__false_type@0@@Z`
- [ ] `??$sort@PAH@stlpmtx_std@@YAXPAH0@Z`
- [ ] `??1?$pair@$$CBVString@@I@stlpmtx_std@@QAA@XZ`
- [ ] `??4exception@std@@QAAAAV01@ABV01@@Z`
- [ ] `?Apply3DCharXfm@WorldCrowd@@QAAXABU?$_List_iterator@UCharData@WorldCrowd@@U?$_Nonconst_traits@UCharData@WorldCrowd@@@stlpmtx_std@@@stlpmtx_std@@HPAVRndCam@@@Z`
- [ ] `?BurnTransform@RndAmbientOcclusion@@IBAXPAVRndMesh@@AAV?$list@PAVRndMesh@@V?$StlNodeAlloc@PAVRndMesh@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?Draw@HamListRibbon@@QAAXABVTransform@@ABV?$vector@UHamListRibbonDrawState@@V?$StlNodeAlloc@UHamListRibbonDrawState@@@stlpmtx_std@@@stlpmtx_std@@_N2@Z`
- [ ] `?DrawMeshVec@SpotlightDrawer@@MAAXAAV?$vector@VSpotMeshEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotMeshEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?EnqueueDetectFrames@MoveDir@@QAAXMHAAV?$vector@VDetectFrame@@V?$StlNodeAlloc@VDetectFrame@@@stlpmtx_std@@@stlpmtx_std@@PBVFilterVersion@@@Z`
- [ ] `?EnumerateFriends@PlatformMgr@@QAAXHAAV?$vector@PAVFriend@@V?$StlNodeAlloc@PAVFriend@@@stlpmtx_std@@@stlpmtx_std@@PAVObject@Hmx@@@Z`
- [ ] `?FindSplit_SAH@kdTreeNode@?$kdTree@VTriangle@@@@QAA_NABVBox@@ABV?$list@PAVTriangle@@V?$StlNodeAlloc@PAVTriangle@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?GetClipStartAndEndBeats@HamDirector@@IAAPAVCharClip@@VSymbol@@AAM1PAU?$pair@MM@stlpmtx_std@@@Z`
- [ ] `?GetCores@SongInfoCopy@@UBAABV?$vector@HV?$StlNodeAlloc@H@stlpmtx_std@@@stlpmtx_std@@XZ`
- [ ] `?GetOfferIDsToEnumerate@HamStorePanel@@UBAXAAV?$vector@_KV?$StlNodeAlloc@_K@stlpmtx_std@@@stlpmtx_std@@_N@Z`
- [ ] `?GetPans@SongInfoCopy@@UBAABV?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@XZ`
- [ ] `?GetPlaylist@PlaylistSortMgr@@QAAPAVPlaylist@@H@Z`
- [ ] `?GetPlaylistID@AddPlaylistJob@@QAAXPAVCustomPlaylist@@@Z`
- [ ] `?GetTracks@SongInfoCopy@@UBAABV?$vector@UTrackChannels@@V?$StlNodeAlloc@UTrackChannels@@@stlpmtx_std@@@stlpmtx_std@@XZ`
- [ ] `?GetWidgets@UIList@@QBAABV?$vector@PAVUIListWidget@@V?$StlNodeAlloc@PAVUIListWidget@@@stlpmtx_std@@@stlpmtx_std@@XZ`
- [ ] `?ListDrawChildren@SpotlightDrawer@@UAAXAAV?$list@PAVRndDrawable@@V?$StlNodeAlloc@PAVRndDrawable@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?ListPollChildren@CharEyes@@UBAXAAV?$list@PAVRndPollable@@V?$StlNodeAlloc@PAVRndPollable@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?ListProperties@@YAXAAV?$list@VSymbol@@V?$StlNodeAlloc@VSymbol@@@stlpmtx_std@@@stlpmtx_std@@VSymbol@@1PAV12@_N@Z`
- [ ] `?LockBitmap@DxTex@@UAAXAAVRndBitmap@@H@Z`
- [ ] `?MakeBSPTree@@YA_NAAPAVBSPNode@@AAV?$list@VBSPFace@@V?$StlNodeAlloc@VBSPFace@@@stlpmtx_std@@@stlpmtx_std@@H@Z`
- [ ] `?Mats@RndParticleSys@@UAAXAAV?$list@PAVRndMat@@V?$StlNodeAlloc@PAVRndMat@@@stlpmtx_std@@@stlpmtx_std@@_N@Z`
- [ ] `?Mats@WorldCrowd@@UAAXAAV?$list@PAVRndMat@@V?$StlNodeAlloc@PAVRndMat@@@stlpmtx_std@@@stlpmtx_std@@_N@Z`
- [ ] `?OnDeletePlaylistFromRC@PlaylistSortMgr@@QAAXPAVPlaylist@@@Z`
- [ ] `?PollDeps@CharEyes@@UAAXAAV?$list@PAVObject@Hmx@@V?$StlNodeAlloc@PAVObject@Hmx@@@stlpmtx_std@@@stlpmtx_std@@0@Z`
- [ ] `?RecordedFrameAt@SkeletonClip@@SAPBURecordedFrame@@ABV?$vector@URecordedFrame@@V?$StlNodeAlloc@URecordedFrame@@@stlpmtx_std@@@stlpmtx_std@@MAAH1@Z`
- [ ] `?Recreate@FxSendDelay360@@UAAXAAV?$vector@PAVFxSend@@V?$StlNodeAlloc@PAVFxSend@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?Recreate@FxSendEQ360@@UAAXAAV?$vector@PAVFxSend@@V?$StlNodeAlloc@PAVFxSend@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?Recreate@FxSendWah360@@UAAXAAV?$vector@PAVFxSend@@V?$StlNodeAlloc@PAVFxSend@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?RenderShadows@NgLight@@MAAXAAV?$vector@PAVRndDrawable@@V?$StlNodeAlloc@PAVRndDrawable@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?SelectChildren@NavListHeaderNode@@UAA?AVSymbol@@AAV?$list@PAVNavListSortNode@@V?$StlNodeAlloc@PAVNavListSortNode@@@stlpmtx_std@@@stlpmtx_std@@H@Z`
- [ ] `?Set3DCharList@WorldCrowd@@QAAXABV?$vector@U?$pair@HH@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@HH@stlpmtx_std@@@2@@stlpmtx_std@@PAVObject@Hmx@@@Z`
- [ ] `?Set3DCharXfm@WorldCrowd@@QAAXABU?$_List_iterator@UCharData@WorldCrowd@@U?$_Nonconst_traits@UCharData@WorldCrowd@@@stlpmtx_std@@@stlpmtx_std@@HABVTransform@@@Z`
- [ ] `?Sort@CharPollableSorter@@QAAXAAV?$vector@PAVRndPollable@@V?$StlNodeAlloc@PAVRndPollable@@@stlpmtx_std@@@stlpmtx_std@@@Z`
- [ ] `?UpdateOffers@HamStorePanel@@MAA?AW4StoreError@@ABV?$list@UEnumProduct@@V?$StlNodeAlloc@UEnumProduct@@@stlpmtx_std@@@stlpmtx_std@@_N@Z`
- [ ] `?Visualize@SkeletonViz@@QAAXABVCameraInput@@ABVBaseSkeleton@@PAV?$vector@PAVSkeletonCallback@@V?$StlNodeAlloc@PAVSkeletonCallback@@@stlpmtx_std@@@stlpmtx_std@@_N@Z`
- [ ] `?_Copy_str@exception@std@@AAAXPBD@Z`
- [ ] `?_M_create_node@?$_Rb_tree@PAVCharClip@@U?$less@PAVCharClip@@@stlpmtx_std@@U?$pair@QAVCharClip@@M@3@U?$_Select1st@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@@stlpmtx_std@@IAAPAU_Rb_tree_node_base@2@ABU?$pair@QAVCharClip@@M@2@@Z`
- [ ] `?_M_erase@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@IAAPAVFace@RndMesh@@PAV34@ABU__false_type@2@@Z`
- [ ] `?_M_fill_insert_aux@?$vector@FV?$StlNodeAlloc@F@stlpmtx_std@@@stlpmtx_std@@AAAXPAFIABFABU__false_type@2@@Z`
- [ ] `?__pop_heap_aux@stlpmtx_std@@YAXPAUMemDiffEntry@@0HU?$less@UMemDiffEntry@@@1@@Z`
- [ ] `?deallocate@?$StlNodeAlloc@USongCollisionOutput@@@stlpmtx_std@@QBAXPAUSongCollisionOutput@@I@Z`
- [ ] `?erase@?$vector@VLine@RndText@@V?$StlNodeAlloc@VLine@RndText@@@stlpmtx_std@@@stlpmtx_std@@QAAPAVLine@RndText@@PAV34@0@Z`
- [ ] `?insert@?$list@VBSPFace@@V?$StlNodeAlloc@VBSPFace@@@stlpmtx_std@@@stlpmtx_std@@QAA?AU?$_List_iterator@VBSPFace@@U?$_Nonconst_traits@VBSPFace@@@stlpmtx_std@@@2@U32@ABVBSPFace@@@Z`
- [ ] `?insert_unique@?$_Rb_tree@PAVCharClip@@U?$less@PAVCharClip@@@stlpmtx_std@@U?$pair@QAVCharClip@@M@3@U?$_Select1st@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@3@V?$StlNodeAlloc@U?$pair@QAVCharClip@@M@stlpmtx_std@@@3@@stlpmtx_std@@QAA?AU?$pair@U?$_Rb_tree_iterator@U?$pair@QAVCharClip@@M@stlpmtx_std@@U?$_MapTraitsT@U?$pair@QAVCharClip@@M@stlpmtx_std@@@priv@2@@stlpmtx_std@@_N@2@ABU?$pair@QAVCharClip@@M@2@@Z`
- [ ] `?push_back@?$vector@V?$Key@VTransform@@@@V?$StlNodeAlloc@V?$Key@VTransform@@@@@stlpmtx_std@@@stlpmtx_std@@QAAXABV?$Key@VTransform@@@@@Z`
- [ ] `?push_back@?$vector@VFace@RndMesh@@V?$StlNodeAlloc@VFace@RndMesh@@@stlpmtx_std@@@stlpmtx_std@@QAAXABVFace@RndMesh@@@Z`
- [ ] `?resize@?$vector@V?$Key@VTransform@@@@V?$StlNodeAlloc@V?$Key@VTransform@@@@@stlpmtx_std@@@stlpmtx_std@@QAAXIABV?$Key@VTransform@@@@@Z`

### BinStream (33)

- [ ] `??5@YAAAVBinStream@@AAV0@AAUPropTriggerDefn@FlowTrigger@@@Z`
- [ ] `??5@YAAAVBinStream@@AAV0@AAVFilePath@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABUConstraintSystem@CharBlendBone@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABUTarget@HamCamShot@@@Z`
- [ ] `?Load@CharEyes@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@DrivenPropertyEntry@@QAAXAAVBinStream@@PAVFlowNode@@@Z`
- [ ] `?Load@HamCamShot@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@HamCamTransform@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@LightPreset@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@PhysicsVolume@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@RndEnviron@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@RndFont3d@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@RndFontBase@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@RndParticleSys@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@RndScreenMask@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@SpotlightDrawer@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@SynthSample@@UAAXAAVBinStream@@@Z`
- [ ] `?Load@WorldInstance@@UAAXAAVBinStream@@@Z`
- [ ] `?LoadData@CharBonesSamples@@QAAXAAVBinStreamRev@@@Z`
- [ ] `?LoadFrame@SkeletonClip@@KAXAAVBinStream@@AAURecordedFrame@@H@Z`
- [ ] `?LoadHeader@CharBonesSamples@@QAAXAAVBinStreamRev@@@Z`
- [ ] `?LoadRev@RndPostProc@@QAAXAAVBinStreamRev@@@Z`
- [ ] `?LoadStages@RndMatAnim@@IAAXAAVBinStreamRev@@@Z`
- [ ] `?OldResourcePreload@LabelShrinkWrapper@@UAAXAAVBinStream@@@Z`
- [ ] `?PostLoad@HamDriver@@UAAXAAVBinStream@@@Z`
- [ ] `?PostLoad@SynthSample@@UAAXAAVBinStream@@@Z`
- [ ] `?PostLoad@WorldInstance@@UAAXAAVBinStream@@@Z`
- [ ] `?PreLoad@InlineHelp@@UAAXAAVBinStream@@@Z`
- [ ] `?PreLoad@SynthSample@@UAAXAAVBinStream@@@Z`
- [ ] `?PreSave@WorldInstance@@UAAXAAVBinStream@@@Z`
- [ ] `?Save@CharBonesSamples@@QAAXAAVBinStream@@@Z`
- [ ] `?Save@RndFont3d@@UAAXAAVBinStream@@@Z`
- [ ] `?SavePersistentObjects@WorldInstance@@AAAXAAVBinStream@@@Z`

### ObjOwner (18)

- [ ] `??$?6VCharInterest@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VCharInterest@@@@@Z`
- [ ] `??$?6VCharLookAt@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VCharLookAt@@@@@Z`
- [ ] `??$?6VEventTrigger@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VEventTrigger@@@@@Z`
- [ ] `??$?6VObjectDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VObjectDir@@@@@Z`
- [ ] `??$?6VRndAnimatable@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndAnimatable@@@@@Z`
- [ ] `??$?6VRndDrawable@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndDrawable@@@@@Z`
- [ ] `??$?6VRndEnviron@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndEnviron@@@@@Z`
- [ ] `??$?6VRndFont@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndFont@@@@@Z`
- [ ] `??$?6VRndLightAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndLightAnim@@@@@Z`
- [ ] `??$?6VRndMatAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndMatAnim@@@@@Z`
- [ ] `??$?6VRndMeshAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndMeshAnim@@@@@Z`
- [ ] `??$?6VRndParticleSysAnim@@@@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndParticleSysAnim@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndCamAnim@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndLight@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndMesh@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndTransformable@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VRndWind@@@@@Z`
- [ ] `??6@YAAAVBinStream@@AAV0@ABV?$ObjOwnerPtr@VSpotlight@@@@@Z`

### ObjDirPtr (11)

- [ ] `??$?6VHamListRibbon@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VHamListRibbon@@@@@Z`
- [ ] `??$?6VHamScrollSpeedIndicator@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VHamScrollSpeedIndicator@@@@@Z`
- [ ] `??$?6VHamScrollSpeedIndicator@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VHamScrollSpeedIndicator@@@@@Z`
- [ ] `??$?6VObjectDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VObjectDir@@@@@Z`
- [ ] `??$?6VObjectDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VObjectDir@@@@@Z`
- [ ] `??$?6VUIListDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VUIListDir@@@@@Z`
- [ ] `??$?6VUIListDir@@@@YAAAVBinStream@@AAV0@ABV?$ObjDirPtr@VUIListDir@@@@@Z`
- [ ] `??$PropSync@VWorldInstance@@@@YA_NAAV?$ObjDirPtr@VWorldInstance@@@@AAVDataNode@@PAVDataArray@@HW4PropOp@@@Z`
- [ ] `??0?$ObjDirPtr@VObjectDir@@@@QAA@PAVObjectDir@@@Z`
- [ ] `?IsLoaded@?$ObjDirPtr@VObjectDir@@@@QBA_NXZ`
- [ ] `?IsLoaded@?$ObjDirPtr@VUILabelDir@@@@QBA_NXZ`

### MakeString (8)

- [ ] `??$MakeString@$$BY0BH@$$CBDHPBD@@YAPBDPBDAAY0BH@$$CBDABHABQBD@Z`
- [ ] `??$MakeString@HK@@YAPBDPBDABHABK@Z`
- [ ] `??$MakeString@HW4State@SaveLoadManager@@@@YAPBDPBDABHABW4State@SaveLoadManager@@@Z`
- [ ] `??$MakeString@PBD$$BY0BE@$$CBDHG@@YAPBDPBDABQBDAAY0BE@$$CBDABHABG@Z`
- [ ] `??$MakeString@VString@@$$BY0BAE@D@@YAPBDPBDABVString@@AAY0BAE@$$CBD@Z`
- [ ] `??$MakeString@VString@@PAD@@YAPBDPBDABVString@@ABQAD@Z`
- [ ] `??$MakeString@W4BlendEaseMode@CamShotFrame@@@@YAPBDPBDABW4BlendEaseMode@CamShotFrame@@@Z`
- [ ] `?TerminateMakeString@@YAXXZ`

### ObjPtr (1)

- [ ] `?SetClips@PlayBack@CharLipSync@@QAAXV?$ObjPtr@VObjectDir@@@@@Z`

---

## Dynamic Initializers (236 stubs)

Compiler-generated `??__E` / `??__F` symbols. Auto-resolve when the parent
symbol is compiled from decomp source.

- [ ] `??__E?gThreadAchievements@Achievements@@0V?$vector@UXUSER_ACHIEVEMENT@@V?$StlNodeAlloc@UXUSER_ACHIEVEMENT@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?kMThd@MidiChunkID@@2V1@B@@YAXXZ`
- [ ] `??__E?kMTrk@MidiChunkID@@2V1@B@@YAXXZ`
- [ ] `??__E?kServerVer@RockCentral@@0VString@@B@@YAXXZ`
- [ ] `??__E?mAssocMicXbox@ExternalMicClientMgr@@0V?$vector@PAVMicXbox@@V?$StlNodeAlloc@PAVMicXbox@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?mDevToMicMaster@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?mMicMasterToDev@ExternalMicClientMgr@@0V?$vector@KV?$StlNodeAlloc@K@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?mMicMasters@ExternalMicClientMgr@@0V?$vector@PAVExternalMicClientProxy@@V?$StlNodeAlloc@PAVExternalMicClientProxy@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?mTimer@UsbMidiGuitar@@0VTimer@@A@@YAXXZ`
- [ ] `??__E?mXLSPRefCountMap@XLSPConnection@@2V?$map@KHU?$less@K@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBKH@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VBitCrushEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VCompressionEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VDelayEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VDistortionEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VEQEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VEnvelopeGenerator@@UEnvelopeGeneratorParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VFlangerEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VGainEffect@@UGainEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VHeadsetPlaybackEffect@@UHeadsetPlaybackEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VHeadsetXferEffect@@UHeadsetXferEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VMeterEffect@@UMeterEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VPitchShiftEffect@@UPitchShiftEffectParams@@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VSynapseAPO@DSP@@USynapseAPOParams@2@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?m_regProps@?$CSampleXAPOBase@VWahEffect@@UParams@1@@ATG@@0UXAPO_REGISTRATION_PROPERTIES@@A@@YAXXZ`
- [ ] `??__E?sActiveMovies@BinkMovieImpl@@0V?$vector@PAVBinkMovieImpl@@V?$StlNodeAlloc@PAVBinkMovieImpl@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sBlacklightPacketPool@RndText@@1V?$vector@VBlacklightPacket@RndText@@V?$StlNodeAlloc@VBlacklightPacket@RndText@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sBloom@NgPostProc@@1V?$BloomTextures@$02@1@A@@YAXXZ`
- [ ] `??__E?sCache@HamCamShot@@1V?$list@UTargetCache@HamCamShot@@V?$StlNodeAlloc@UTargetCache@HamCamShot@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sCans@SpotlightDrawer@@1V?$vector@VSpotMeshEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotMeshEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sCritSec@SkeletonUpdateHandle@@0VCriticalSection@@A@@YAXXZ`
- [ ] `??__E?sCurrentExportEvent@MsgSinks@@0VSymbol@@A@@YAXXZ`
- [ ] `??__E?sFacingPos@FacingSet@CharClip@@2UFacingBones@12@A@@YAXXZ`
- [ ] `??__E?sFacingRotAndPos@FacingSet@CharClip@@2UFacingBones@12@A@@YAXXZ`
- [ ] `??__E?sFactories@Object@Hmx@@0V?$map@VSymbol@@P6APAVObject@Hmx@@XZU?$less@VSymbol@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBVSymbol@@P6APAVObject@Hmx@@XZ@stlpmtx_std@@@5@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sFilterVersions@MoveDir@@0V?$vector@PAVFilterVersion@@V?$StlNodeAlloc@PAVFilterVersion@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sFontMapCache@RndText@@1V?$list@PAVFontMapBase@RndText@@V?$StlNodeAlloc@PAVFontMapBase@RndText@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sGlobalLighting@RndEnviron@@1VBoxMapLighting@@A@@YAXXZ`
- [ ] `??__E?sID@Matrix2@Hmx@@0V12@A@@YAXXZ`
- [ ] `??__E?sID@Matrix3@Hmx@@0V12@A@@YAXXZ`
- [ ] `??__E?sID@Matrix4@Hmx@@0V12@A@@YAXXZ`
- [ ] `??__E?sID@Transform@@0V1@A@@YAXXZ`
- [ ] `??__E?sInterpMessage@PropKeys@@2VMessage@@A@@YAXXZ`
- [ ] `??__E?sLights@SpotlightDrawer@@1V?$vector@VSpotlightEntry@SpotlightDrawer@@V?$StlNodeAlloc@VSpotlightEntry@SpotlightDrawer@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sListStateMaxDisplay@HamNavList@@0HB@@YAXXZ`
- [ ] `??__E?sManualEvents@LightPreset@@1V?$deque@U?$pair@W4KeyframeCmd@LightPreset@@M@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@W4KeyframeCmd@LightPreset@@M@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sMemPointMap@DirLoader@@0V?$map@VString@@UMemPointDelta@@U?$less@VString@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBVString@@UMemPointDelta@@@stlpmtx_std@@@4@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sNull@FilePath@@0V1@A@@YAXXZ`
- [ ] `??__E?sOverlays@RndOverlay@@0V?$list@PAVRndOverlay@@V?$StlNodeAlloc@PAVRndOverlay@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sParsers@MidiParser@@0V?$list@PAVMidiParser@@V?$StlNodeAlloc@PAVMidiParser@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sPollables@SynthPollable@@0V?$list@PAVSynthPollable@@V?$StlNodeAlloc@PAVSynthPollable@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sProxyPool@RndMultiMesh@@1V?$list@U?$pair@PAVRndMultiMeshProxy@@H@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@PAVRndMultiMeshProxy@@H@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sRand@Rand@@2V1@A@@YAXXZ`
- [ ] `??__E?sRemapClipReplace@SkeletonClip@@2VString@@A@@YAXXZ`
- [ ] `??__E?sRemapClipSearch@SkeletonClip@@2VString@@A@@YAXXZ`
- [ ] `??__E?sRoot@FilePath@@0V1@A@@YAXXZ`
- [ ] `??__E?sShadowSpots@SpotlightDrawer@@1V?$vector@PAVSpotlight@@V?$StlNodeAlloc@PAVSpotlight@@@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sSingleton@RndVelocityBuffer@@0V1@A@@YAXXZ`
- [ ] `??__E?sSlowFrameTimer@Timer@@0V1@A@@YAXXZ`
- [ ] `??__E?sTimers@AutoTimer@@0V?$list@U?$pair@VTimer@@VTimerStats@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@VTimer@@VTimerStats@@@stlpmtx_std@@@2@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__E?sUpVectorSmoother@SkeletonFrame@@2VVector3DESmoother@@A@@YAXXZ`
- [ ] `??__E?smNestedStartTimes@GlitchPoker@@0V?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@A@@YAXXZ`
- [ ] `??__ETheBlockMgr@@YAXXZ`
- [ ] `??__ETheCharDebug@@YAXXZ`
- [ ] `??__ETheDebug@@YAXXZ`
- [ ] `??__ETheDxRnd@@YAXXZ`
- [ ] `??__ETheDxShaderMgr@@YAXXZ`
- [ ] `??__ETheDxTexMgr@@YAXXZ`
- [ ] `??__ETheGlitchFinder@@YAXXZ`
- [ ] `??__ETheHDCache@@YAXXZ`
- [ ] `??__ETheHamSongMgr@@YAXXZ`
- [ ] `??__ETheHamUI@@YAXXZ`
- [ ] `??__ETheKnownIssues@@YAXXZ`
- [ ] `??__ETheLoadMgr@@YAXXZ`
- [ ] `??__ETheLocale@@YAXXZ`
- [ ] `??__ETheMC@@YAXXZ`
- [ ] `??__ETheMemcardMgr@@YAXXZ`
- [ ] `??__ETheOSCMessenger@@YAXXZ`
- [ ] `??__EThePlatformMgr@@YAXXZ`
- [ ] `??__EThePresenceMgr@@YAXXZ`
- [ ] `??__ETheProfileMgr@@YAXXZ`
- [ ] `??__ETheRockCentral@@YAXXZ`
- [ ] `??__ETheSongSequence@@YAXXZ`
- [ ] `??__ETheSystemArgs@@YAXXZ`
- [ ] `??__ETheTaskMgr@@YAXXZ`
- [ ] `??__ETheVirtualKeyboard@@YAXXZ`
- [ ] `??__EgAllTextures@@YAXXZ`
- [ ] `??__EgArkFiles@?A0x7f36a62b@@YAXXZ`
- [ ] `??__EgBinkMovieSys@@YAXXZ`
- [ ] `??__EgCaches@@YAXXZ`
- [ ] `??__EgCatPriority@@YAXXZ`
- [ ] `??__EgChecksumData@@YAXXZ`
- [ ] `??__EgChildPolys@@YAXXZ`
- [ ] `??__EgClients@?A0x831dd776@@YAXXZ`
- [ ] `??__EgConditional@@YAXXZ`
- [ ] `??__EgContentMgr@@YAXXZ`
- [ ] `??__EgContextRand@?A0x24773155@@YAXXZ`
- [ ] `??__EgCrit@?A0x49b544a7@@YAXXZ`
- [ ] `??__EgCrit@@YAXXZ`
- [ ] `??__EgCritSection@?A0xf503845b@@YAXXZ`
- [ ] `??__EgCritSection@@YAXXZ`
- [ ] `??__EgDataFuncs@@YAXXZ`
- [ ] `??__EgDataPointMgr@@YAXXZ`
- [ ] `??__EgDataProcessedEvt@?A0x7ea4e606@@YAXXZ`
- [ ] `??__EgDataReadCrit@@YAXXZ`
- [ ] `??__EgDataReadyEvt@?A0x7ea4e606@@YAXXZ`
- [ ] `??__EgDataThisPtr@@YAXXZ`
- [ ] `??__EgDataVars@@YAXXZ`
- [ ] `??__EgDebugGraphs@?A0xb39b74bf@@YAXXZ`
- [ ] `??__EgDecompressionCritSec@?A0x7ea4e606@@YAXXZ`
- [ ] `??__EgDecompressionQueue@?A0x7ea4e606@@YAXXZ`
- [ ] `??__EgDefaultBeatMap@@YAXXZ`
- [ ] `??__EgDefaultTempoMap@@YAXXZ`
- [ ] `??__EgDeferredAwardQueue@?A0xf8e4b4b5@@YAXXZ`
- [ ] `??__EgDingoSvrXbox@@YAXXZ`
- [ ] `??__EgDirList@@YAXXZ`
- [ ] `??__EgEntries@@YAXXZ`
- [ ] `??__EgEvalNode@@YAXXZ`
- [ ] `??__EgExternalArkFiles@?A0x7f36a62b@@YAXXZ`
- [ ] `??__EgFile@@YAXXZ`
- [ ] `??__EgFiles@@YAXXZ`
- [ ] `??__EgGamePanelCallback@@YAXXZ`
- [ ] `??__EgHashTable@@YAXXZ`
- [ ] `??__EgHiResScreen@@YAXXZ`
- [ ] `??__EgHolmesTarget@?A0x49b544a7@@YAXXZ`
- [ ] `??__EgIgnoredContent@@YAXXZ`
- [ ] `??__EgInProgressSyncVoices@@YAXXZ`
- [ ] `??__EgInProgressVoices@@YAXXZ`
- [ ] `??__EgInput@?A0x49b544a7@@YAXXZ`
- [ ] `??__EgJoypadData@?A0xca10770b@@YAXXZ`
- [ ] `??__EgLastCachedResource@@YAXXZ`
- [ ] `??__EgLock@?A0xcb0871ef@@YAXXZ`
- [ ] `??__EgLockPendingLists@@YAXXZ`
- [ ] `??__EgLoopVizCallback@@YAXXZ`
- [ ] `??__EgMacroTable@@YAXXZ`
- [ ] `??__EgMemLogType@@YAXXZ`
- [ ] `??__EgMemTrackSourceFile@@YAXXZ`
- [ ] `??__EgMemTrackSourceObject@@YAXXZ`
- [ ] `??__EgMics@?A0x0c39da7f@@YAXXZ`
- [ ] `??__EgMoveMergeMap@@YAXXZ`
- [ ] `??__EgNewReaders@?A0xcb0871ef@@YAXXZ`
- [ ] `??__EgNoPartOverride@@YAXXZ`
- [ ] `??__EgNotifies@@YAXXZ`
- [ ] `??__EgNotifyThreadSec@@YAXXZ`
- [ ] `??__EgNotifyThreadSync@@YAXXZ`
- [ ] `??__EgOfflineCallback@@YAXXZ`
- [ ] `??__EgOldChars@@YAXXZ`
- [ ] `??__EgOverride@@YAXXZ`
- [ ] `??__EgParentPolys@@YAXXZ`
- [ ] `??__EgPatchVerts@@YAXXZ`
- [ ] `??__EgPendingSyncVoices@@YAXXZ`
- [ ] `??__EgPendingVoices@@YAXXZ`
- [ ] `??__EgPhysicalType@?A0x2be09a71@@YAXXZ`
- [ ] `??__EgPhysicsVolumeBox@?A0x5ba00aca@@YAXXZ`
- [ ] `??__EgPreloaded@?A0xf8b42a02@@YAXXZ`
- [ ] `??__EgPristineSystemArgs@@YAXXZ`
- [ ] `??__EgProfile@?A0x49b544a7@@YAXXZ`
- [ ] `??__EgPropPaths@@YAXXZ`
- [ ] `??__EgQueue@@YAXXZ`
- [ ] `??__EgReadFiles@@YAXXZ`
- [ ] `??__EgReadTime@@YAXXZ`
- [ ] `??__EgReaders@?A0xcb0871ef@@YAXXZ`
- [ ] `??__EgRequests@?A0x49b544a7@@YAXXZ`
- [ ] `??__EgResourceFileCacheHelper@@YAXXZ`
- [ ] `??__EgServerName@?A0x49b544a7@@YAXXZ`
- [ ] `??__EgShaderDepthVolume@@YAXXZ`
- [ ] `??__EgShaderDrawRect@@YAXXZ`
- [ ] `??__EgShaderFur@@YAXXZ`
- [ ] `??__EgShaderMultimesh@@YAXXZ`
- [ ] `??__EgShaderParticles@@YAXXZ`
- [ ] `??__EgShaderPostProc@@YAXXZ`
- [ ] `??__EgShaderSimple@@YAXXZ`
- [ ] `??__EgShaderStandard@@YAXXZ`
- [ ] `??__EgShaderSyncTrack@@YAXXZ`
- [ ] `??__EgShaderUnwrapUV@@YAXXZ`
- [ ] `??__EgShaderVelocity@@YAXXZ`
- [ ] `??__EgShaderVelocityCamera@@YAXXZ`
- [ ] `??__EgSinks@@YAXXZ`
- [ ] `??__EgSystemLanguage@@YAXXZ`
- [ ] `??__EgSystemLocale@@YAXXZ`
- [ ] `??__EgSystemTimer@@YAXXZ`
- [ ] `??__EgTiers@?A0xf8e4b4b5@@YAXXZ`
- [ ] `??__EgTransListAlloc@@YAXXZ`
- [ ] `??__EgUnlockables@?A0xf8e4b4b5@@YAXXZ`
- [ ] `??__EgUseLowestMipExceptions@@YAXXZ`
- [ ] `??__EgUsedContexts@?A0x24773155@@YAXXZ`
- [ ] `??__EgVarStack@@YAXXZ`
- [ ] `??__EgVoiceGC@@YAXXZ`
- [ ] `??__EgWavFileCacheHelper@@YAXXZ`
- [ ] `??__EgWavMgr@@YAXXZ`
- [ ] `??__EgWebSvcMgr@@YAXXZ`
- [ ] `??__EjobQueueMutex@JobQueue@@YAXXZ`
- [ ] `??__EkConvLen@?A0x5c754947@@YAXXZ`
- [ ] `??__EkListChunkID@@YAXXZ`
- [ ] `??__EkMidiChunkID@@YAXXZ`
- [ ] `??__EkMidiHeaderChunkID@@YAXXZ`
- [ ] `??__EkMidiTrackChunkID@@YAXXZ`
- [ ] `??__EkRiffChunkID@@YAXXZ`
- [ ] `??__EkWaveAdditionalChunkID@@YAXXZ`
- [ ] `??__EkWaveChunkID@@YAXXZ`
- [ ] `??__EkWaveCueChunkID@@YAXXZ`
- [ ] `??__EkWaveDataChunkID@@YAXXZ`
- [ ] `??__EkWaveFactChunkID@@YAXXZ`
- [ ] `??__EkWaveFormatChunkID@@YAXXZ`
- [ ] `??__EkWaveInstChunkID@@YAXXZ`
- [ ] `??__EkWaveLabelChunkID@@YAXXZ`
- [ ] `??__EkWaveSampleChunkID@@YAXXZ`
- [ ] `??__EkWaveTextChunkID@@YAXXZ`
- [ ] `??__EmCampaignVO@@YAXXZ`
- [ ] `??__EmFriendEnumRequests@?A0x8a9ffbf2@@YAXXZ`
- [ ] `??__EmServiceIdMap@?A0x8a9ffbf2@@YAXXZ`
- [ ] `??__EmTime@?A0x8a9ffbf2@@YAXXZ`
- [ ] `??__EsAutoplayStates@@YAXXZ`
- [ ] `??__EsCam@@YAXXZ`
- [ ] `??__EsCamFrame@@YAXXZ`
- [ ] `??__EsCollisionUsefulBoneNames@@YAXXZ`
- [ ] `??__EsConditionalTimersEnabled@@YAXXZ`
- [ ] `??__EsDefaultRatingThresholds@@YAXXZ`
- [ ] `??__EsFakes@@YAXXZ`
- [ ] `??__EsFilePaths@@YAXXZ`
- [ ] `??__EsFiles@@YAXXZ`
- [ ] `??__EsFlipYZ@@YAXXZ`
- [ ] `??__EsFrames@@YAXXZ`
- [ ] `??__EsIdentityXfm@?A0x8e417309@@YAXXZ`
- [ ] `??__EsKeyReplace@@YAXXZ`
- [ ] `??__EsLastComparedDancerSkel@@YAXXZ`
- [ ] `??__EsLicense@@YAXXZ`
- [ ] `??__EsLoadedFile@@YAXXZ`
- [ ] `??__EsOverlayWidth@?A0xe50ea9df@@YAXXZ`
- [ ] `??__EsRand@@YAXXZ`
- [ ] `??__EsRatingStates@@YAXXZ`
- [ ] `??__EsShaderTypes@@YAXXZ`
- [ ] `??__EsSuperClassMap@@YAXXZ`
- [ ] `??__EsWarnings@@YAXXZ`
- [ ] `??__Es_voiceGC@@YAXXZ`
- [ ] `??__Es_voiceGCInProgress@@YAXXZ`
- [ ] `??__EtCritSection@?A0x439b694a@@YAXXZ`

---

## Function-Local Statics (4 stubs)

- [ ] `?$S1@?1??StrToCharacterSym@@YA?AVSymbol@@VString@@@Z@4IA`
- [ ] `?$S2@?1??StrToCrewSym@@YA?AVSymbol@@VString@@@Z@4IA`
- [ ] `?$S3@?1??CamShotVOData@@YAXVSymbol@@AAV2@111@Z@4IA`
- [ ] `?$S4@?1??IsRest@MoveVariant@@QBA_NXZ@4IA`

---

## Anonymous Namespace (7 stubs)

- [ ] `??$_Destroy_Range@PAULabel@?A0x81ddebd1@@@stlpmtx_std@@YAXPAULabel@?A0x81ddebd1@@0@Z`
- [ ] `?ClipStart@?A0xf8c6d506@@YAMPAVCharClip@@MAAM1@Z`
- [ ] `?DecodeThreadEntry@?A0xcb0871ef@@YAKPAX@Z`
- [ ] `?JointToVertexData@?A0x790ae044@@YAXAAVVector3@@ABVSkeleton@@W4SkeletonJoint@@ABVVector4@@@Z`
- [ ] `?LoadDebugDepthBuffer@?A0x8e584365@@YAXAAPAVRndTex@@@Z`
- [ ] `?SetColorCameraProperty@?A0x8e584365@@YAXW4_NUI_CAMERA_PROPERTY@@J@Z`
- [ ] `?VertexToWorld@?A0x790ae044@@YAXAAVVector3@@ABVTransform@@MABVVector4@@@Z`

---

## SDK/XDK (24 stubs) — Keep as stubs

- `??$_Copy_Construct@U?$pair@$$CBVString@@I@stlpmtx_std@@@stlpmtx_std@@YAXPAU?$pair@$$CBVString@@I@0@ABU10@@Z`
- `??$_Destroy_Range@PAULevelData@@@stlpmtx_std@@YAXPAULevelData@@0@Z`
- `??$_Param_Construct@URecurseInfo@@U1@@stlpmtx_std@@YAXPAURecurseInfo@@ABU1@@Z`
- `??0CXAPOBase@ATG@@QAA@XZ`
- `??0CXAPOParametersBase@ATG@@QAA@PBXPAXIE@Z`
- `??0ID3DXInclude@@QAA@XZ`
- `??1CXAPOBase@@UAA@XZ`
- `?D3DFORMAT_BitsPerPixel@@YAHW4_D3DFORMAT@@@Z`
- `?GetMultimeshFaces@DxMesh@@QAAPAUD3DVertexBuffer@@XZ`
- `?GetName@CGMClassifier@NUISPEECH@@UBAPB_WXZ`
- `?GetStringKey@CFEModuleDef@NUISPEECH@@QBAPB_WXZ`
- `?NewBufStream@Synth@@UAAPAVStream@@PBXHVSymbol@@M_N@Z`
- `?Release@?$CComContainedObject@VCTextNormMultiResult@NUISPEECH@@@ATL@NUISPEECH@@UAAKXZ`
- `?SyncEffectParams@FxSendChorus360@@UBAXPAUIXAudio2SubmixVoice@@@Z`
- `?SyncEffectParams@FxSendDelay360@@UBAXPAUIXAudio2SubmixVoice@@@Z`
- `?SyncEffectParams@FxSendEQ360@@UBAXPAUIXAudio2SubmixVoice@@@Z`
- `?SyncEffectParams@FxSendFlanger360@@UBAXPAUIXAudio2SubmixVoice@@@Z`
- `?SyncEffectParams@FxSendReverb360@@UBAXPAUIXAudio2SubmixVoice@@@Z`
- `?SyncEffectParams@FxSendWah360@@UBAXPAUIXAudio2SubmixVoice@@@Z`
- `?gathering@CUgtFilter@NUISPEECH@@QAA_NXZ`
- `D3DTexture_GetLevelDesc`
- `D3DTexture_LockRect`
- `D3DTexture_UnlockRect`
- `D3DXSetDXT3DXT5`

## SDK/Audio (43 stubs) — Keep as stubs

- `??0?$CSampleXAPOBase@VSynapseAPO@DSP@@USynapseAPOParams@2@@DSP@@QAA@XZ`
- `??0GranularSynth@Synapse@DSP@@QAA@ABV?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@III@Z`
- `??0HeadsetXferEffect@@QAA@XZ`
- `??0MeterEffect@@QAA@XZ`
- `??0PeakDetector@Synapse@DSP@@QAA@ABV?$vector@MV?$StlNodeAlloc@M@stlpmtx_std@@@stlpmtx_std@@II@Z`
- `??0PitchCorrectedVoice@Synapse@DSP@@QAA@XZ`
- `??1GranularSynth@Synapse@DSP@@QAA@XZ`
- `??1PeakDetector@Synapse@DSP@@QAA@XZ`
- `??1PitchCorrectedVoice@Synapse@DSP@@QAA@XZ`
- `?Analyze@SpectralAnalysis@DSP@@QAAXPBMPAM@Z`
- `?BeginFromFile@BinkMovieImpl@@UAA_NPBDM_N111HPAVBinStream@@W4LoaderPos@@@Z`
- `?CheckOpen@BinkMovieImpl@@UAA_N_N@Z`
- `?Detect@PeakDetector@Synapse@DSP@@QAAXI@Z`
- `?Draw@BinkMovieImpl@@UAAXXZ`
- `?End@BinkMovieImpl@@UAAXXZ`
- `?ExtractGranules@GranularSynth@Synapse@DSP@@QAAXXZ`
- `?Flush@GranularSynth@Synapse@DSP@@QAAXXZ`
- `?GetCorrection@PitchCorrectedVoice@Synapse@DSP@@QAAMXZ`
- `?GetFrame@BinkMovieImpl@@UBAHXZ`
- `?HighpassCoefficients@DSP@@YAXQAMMMM@Z`
- `?IsLoading@BinkMovieImpl@@UBA_NXZ`
- `?IsOpen@BinkMovieImpl@@UBA_NXZ`
- `?LockThread@BinkMovieImpl@@UAAXXZ`
- `?LowpassCoefficients@DSP@@YAXQAMMMM@Z`
- `?MsPerFrame@BinkMovieImpl@@UBAMXZ`
- `?NumFrames@BinkMovieImpl@@UBAHXZ`
- `?Paused@BinkMovieImpl@@UBA_NXZ`
- `?Poll@BinkMovieImpl@@UAA_NXZ`
- `?Ready@BinkMovieImpl@@UBA_NXZ`
- `?Save@BinkMovieImpl@@UAAXPAVBinStream@@@Z`
- `?SetAmount@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z`
- `?SetAttackSmoothing@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z`
- `?SetMode@SpectralAnalysis@DSP@@QAAXII@Z`
- `?SetPaused@BinkMovieImpl@@UAA_N_N@Z`
- `?SetProximityEffect@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z`
- `?SetProximityFocus@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z`
- `?SetReleaseSmoothing@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z`
- `?SetTransposition@PitchCorrectedVoice@Synapse@DSP@@QAAXM@Z`
- `?SetVolume@BinkMovieImpl@@UAAXM@Z`
- `?SetWidthHeight@BinkMovieImpl@@UAAXHH@Z`
- `?Synthesize@GranularSynth@Synapse@DSP@@QAAXIPBQAM@Z`
- `?Terminate@BinkMovieImpl@@QAAXXZ`
- `?UnlockThread@BinkMovieImpl@@UAAXXZ`

## CRT/Compiler (2 stubs) — Keep as stubs

- `__vmx_00000000000000000000000000000000`
- `__vmx_bf8000003f800000bf8000003f800000`
