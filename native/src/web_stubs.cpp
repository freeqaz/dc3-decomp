// web_stubs.cpp — Proper C++ stub implementations for WASM build
// These replace the asm-label stubs in engine_stubs_generated.cpp that wasm-ld
// can't handle (asm labels produce wrong mangled names or `unreachable` traps).
// Each stub here uses proper C++ types so the Itanium ABI mangler generates
// the correct symbol names for libc++ (std::__2 namespace).

#ifdef __EMSCRIPTEN__

#include "math/Mtx.h"
#include "math/Geo.h"
#include "math/Vec.h"
#include "math/kdTree.h"
#include "utl/TextStream.h"
#include "utl/BinStream.h"
#include "utl/Cache.h"
#include "obj/Dir.h"
#include "obj/ObjPtr_p.h"
#include "rndobj/Utl.h"
#include "rndobj/Lit_NG.h"
#include "rndobj/AmbientOcclusion.h"
#include "rndobj/TexBlendController.h"
#include "rndobj/Part.h"
#include "rndobj/SoftParticleBuffer.h"
#include "rndobj/Draw.h"
#include "rndobj/Mesh.h"
#include "rndobj/Font.h"
#include "ui/UILabelDir.h"

#include <list>
#include <vector>

// ============================================================================
// Math / Geometry stubs
// ============================================================================

// ScaleAddEq — now in Vec.cpp
// MakeBSPTree, BSPFace::Set, Intersect overloads — now in Geo.cpp

void BuildSphereStratified(unsigned int, std::vector<Vector3> &) {}

BuildPoly::BuildPoly() : mPoly(), mTransform() {}

// kdTree::FindSplit_SAH — now in Geo.cpp

// ============================================================================
// Rendering stubs
// ============================================================================

// NgLight::RenderShadows — now in Lit_NG.cpp

// RndAmbientOcclusion::BurnTransform — now in AmbientOcclusion.cpp

// TextStream::operator<<(double) now in TextStream.cpp

// ============================================================================
// ObjPtr / BinStream operator<< template instantiations
// ============================================================================

template <>
BinStream &operator<<(BinStream &bs, const ObjDirPtr<UILabelDir> &) { return bs; }

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndFont3d> &) { return bs; }

// ============================================================================
// Object property listing
// ============================================================================

// ListProperties — now in Utl.cpp

// ============================================================================
// Holmes (debug network) stub
// ============================================================================

// HolmesClientCacheResource — now in HolmesClient.cpp

void HolmesClientPrint(const char *) {}

// ============================================================================
// Engine init stubs — functions called during SystemInit that are either
// unimplemented or Xbox-specific. Emscripten weak stubs don't resolve
// at runtime, so we need strong definitions here.
// ============================================================================

// Spew (debug output system)
void SpewInit() {}
void SpewTerminate() {}

// MemFindHeap, GetCurrentHeapNum — now in MemMgr.cpp

// File system operations not available in MEMFS
void FileRecursePattern(const char *, void (*)(const char *, const char *), bool) {}
// MakeFileList — now in Utl.cpp

// Bink video — not supported in browser
void BinkSetMemory(void *(*)(int), void (*)(void *)) {}
int BinkStartAsyncThread(int, int) { return 1; }  // 1 = success
void *RadAlloc(int size) { return malloc(size); }

// EstimateDraw — now in Draw.cpp

// Xbox/Kinect/network stubs
#include "os/File.h"
#include "xdk/xbdm/xbdm.h"

HRESULT DmMapDevkitDrive() { return 0; }

// System — locale/region stubs
bool HongKongExceptionMet() { return false; }

// Memory profiling — no-ops
#include "Memory.h"
PhysMemTypeTracker::PhysMemTypeTracker(Symbol) {}
PhysMemTypeTracker::~PhysMemTypeTracker() {}
void JoypadSetActuatorsImp(int, int, int) {}

// RecursePatternInternal — now in File.cpp

// NUI speech stubs (Kinect) — use exact XDK signatures
#include "xdk/nui/nuispeech.h"

extern "C" {
HRESULT NuiSpeechDisable() { return 0; }
HRESULT NuiSpeechEnable(NUI_SPEECH_INIT_PROPERTIES *, DWORD) { return 0; }
HRESULT NuiSpeechLoadGrammar(LPCWSTR, ULONG, NUI_SPEECH_LOADOPTIONS, NUI_SPEECH_GRAMMAR *) { return 0; }
HRESULT NuiSpeechUnloadGrammar(NUI_SPEECH_GRAMMAR *) { return 0; }
HRESULT NuiSpeechSetEventInterest(ULONG) { return 0; }
HRESULT NuiSpeechSetGrammarState(NUI_SPEECH_GRAMMAR *, NUI_SPEECH_GRAMMARSTATE) { return 0; }
HRESULT NuiSpeechStartRecognition() { return 0; }
HRESULT NuiSpeechStopRecognition() { return 0; }
HRESULT NuiSpeechSetRuleState(NUI_SPEECH_GRAMMAR *, LPCWSTR, NUI_SPEECH_RULESTATE) { return 0; }
HRESULT NuiSpeechCreateRule(NUI_SPEECH_GRAMMAR *, LPCWSTR, DWORD, BOOL, HANDLE) { return 0; }
HRESULT NuiSpeechCreateState(NUI_SPEECH_GRAMMAR *, HANDLE, HANDLE *) { return 0; }
HRESULT NuiSpeechAddWordTransition(NUI_SPEECH_GRAMMAR *, HANDLE, HANDLE, LPCWSTR, LPCWSTR, NUI_SPEECH_WORDTYPE, float, NUI_SPEECH_SEMANTIC *) { return 0; }
HRESULT NuiSpeechCommitGrammar(NUI_SPEECH_GRAMMAR *) { return 0; }
HRESULT NuiWaveSetEnabled(BOOL) { return 0; }
}

// RndParticleSys::Replace — now in Part.cpp
// RndSoftParticleBuffer::DoPost — now in SoftParticleBuffer.cpp

// Debug::Modal — now in Debug.cpp

// NormalizeSystemArgs — now in System.cpp

// ============================================================================
// Xbox Debug Monitor (xbdm) stubs
// ============================================================================

#include "xdk/xbdm/xbdm.h"

extern "C" {
HRESULT DmGetSystemInfo(DM_SYSTEM_INFO *) { return -1; }  // E_FAIL
}

// ============================================================================
// Vtable anchor stubs — classes with virtual methods that lack implementations
// ============================================================================

// CharSignalApplier — only Handle() is missing (vtable anchor)
#include "char/CharSignalApplier.h"

DataNode CharSignalApplier::Handle(DataArray *d, bool b) { return Hmx::Object::Handle(d, b); }

// ChallengeSortByScore / ChallengeScoreCmp — now in ChallengeSortByScore.cpp

// ============================================================================
// Bink video — not supported in browser
// ============================================================================

#include "moviebink/BinkMovieSys.h"

void BinkMovieSys::PlatformInit() {}

// BinkMovieImpl — all platform-specific methods (were in Xbox .cpp files)
#include "moviebink/BinkMovieImpl.h"

void BinkMovieImpl::SetWidthHeight(int, int) {}
bool BinkMovieImpl::Ready() const { return true; }
bool BinkMovieImpl::BeginFromFile(const char *, float, bool, bool, bool, bool, int, BinStream *, LoaderPos) { return false; }
void BinkMovieImpl::Draw() {}
bool BinkMovieImpl::Poll() { return true; }
void BinkMovieImpl::Save(BinStream *) {}
void BinkMovieImpl::End() {}
bool BinkMovieImpl::IsOpen() const { return false; }
bool BinkMovieImpl::IsLoading() const { return false; }
bool BinkMovieImpl::CheckOpen(bool) { return false; }
bool BinkMovieImpl::SetPaused(bool) { return false; }
void BinkMovieImpl::UnlockThread() {}
void BinkMovieImpl::LockThread() {}
int BinkMovieImpl::GetFrame() const { return 0; }
float BinkMovieImpl::MsPerFrame() const { return 33.33f; }
int BinkMovieImpl::NumFrames() const { return 0; }
void BinkMovieImpl::SetVolume(float) {}
void BinkMovieImpl::Terminate() {}
bool BinkMovieImpl::PlatformCacheFile(const char *) { return false; }

// Xbox memory card — not available on web
#include "os/Memcard_Xbox.h"
void MemcardXbox::Poll() {}

// WebSvcMgr — network web services, not available on web
#include "net/WebSvcMgr.h"
void WebSvcMgr::Poll() {}

#include "os/VirtualKeyboard.h"
void VirtualKeyboard::PlatformPoll() {}

// Profiling/debug stubs (AutoGlitchReport::EndExternal now defined inline in Timer.h)

// Kinect voice/speech — not available on web
#include "meta_ham/VoiceInputPanel.h"
void VoiceInputPanel::ActivateVoiceContext(Symbol) {}
void VoiceInputPanel::CreatePlaylistEditorGrammar() const {}

// Forward declare to avoid nuispeech.h conflicts
class SpeechRecoMessage;
DataNode VoiceInputPanel::OnMsg(const SpeechRecoMessage &) { return DataNode(); }


// SpotlightDrawer stubs — now in SpotlightDrawer.cpp / SpotlightDrawer_NG.cpp
// MemPrintOverview — now in MemMgr.cpp
// CopyTypeProperties — now in Utl.cpp

// BeginMemTrackFileName etc. — now in MemMgr.cpp
void HolmesClientTerminate() {}
void TerminateMakeString() {}
// MakeFileListFullPath — now in Utl.cpp

#include "rndobj/Mat.h"
void SetColorWriteMask(const MatShaderOptions &, RndMat *) {}

// Platform-specific inits — no-ops on web
#include "meta/Achievements.h"
void Achievements::PlatformInit() {}

// PlaylistSortByType — now in PlaylistSortMgr.cpp

// Network — not available on web
#include "os/NetworkSocket.h"
unsigned int NetworkSocket::IPStringToInt(const String &) { return 0; }

// Win32/XDK stubs — APIs that don't exist on web
int WSACreateEvent() { return 0; }
int WaitForSingleObject(int, int) { return 0; }
int CloseHandle(int) { return 0; }
int WideCharToMultiByte(int, int, const void *, int, char *, int, const char *, int *) { return 0; }
int XNetDnsLookup(int, int, void *) { return 0; }
void XNetDnsRelease(void *) {}
int XSetThreadProcessor(int, int) { return 0; }

// Scoring / game utilities — stubs for web
// SynthUtlTerm — now in synth/Utl.cpp
// altCfg: removed — was most vexing parse in Locale.cpp (now fixed, no function exists)
void CacheWav(const char *, CacheResourceResult &r) { r = kCacheUnnecessary; }
// DateTimeCmp — now in DateTime.cpp

// AudioDevice — now in AudioDevice_Web.cpp (AudioWorklet + SharedArrayBuffer)

#endif // __EMSCRIPTEN__

// HX_NATIVE-only stubs — guarded against __EMSCRIPTEN__ for symbols
// already defined in the __EMSCRIPTEN__ section above
#if defined(HX_NATIVE) && !defined(__EMSCRIPTEN__)

#include "gesture/LiveCameraInput.h"
#include "math/Geo.h"
#include "obj/Dir.h"
#include "obj/ObjPtr_p.h"
#include "rnddx9/RenderState.h"
#include "rndobj/Font.h"
#include "rndobj/Utl.h"
#include "utl/BinStream.h"
#include "utl/JobMgr.h"
#include "utl/TextStream.h"
#include "xdk/LIBCMT/ppcintrinsics.h"
#include "xdk/LIBCMT/vectorintrinsics.h"
#include "xdk/nui/nuiidentity.h"
#include "xdk/nui/nuiskeleton.h"
#include "xdk/xbdm/xbdm.h"

#include <cstring>

struct symmetric_CTR;

// TextStream::operator<<(double) now in TextStream.cpp

template <>
BinStream &operator<<(BinStream &bs, const ObjOwnerPtr<RndFont3d> &) {
    return bs;
}

BuildPoly::BuildPoly() : mPoly(), mTransform() {}

void BuildSphereStratified(unsigned int, std::vector<Vector3> &) {}

void LiveCameraInput::LockStream(const void *, LockedRect &) {}

void LiveCameraInput::UnlockStream(const void *) {}

void RndRenderState::SetColorWriteMask(uint) {}

extern "C" {

double __fsel(double fComparand, double fValGE, double fValLT) {
    return fComparand >= 0.0 ? fValGE : fValLT;
}

XMVECTOR __vspltw(XMVECTOR vSrcA, unsigned int uImmed) {
    XMVECTOR out = {};
    float value = vSrcA.v[uImmed & 3];
    out.x = value;
    out.y = value;
    out.z = value;
    out.w = value;
    return out;
}

XMVECTOR __vmaddfp(XMVECTOR mul1, XMVECTOR mul2, XMVECTOR addend) {
    XMVECTOR out = {};
    out.x = mul1.x * mul2.x + addend.x;
    out.y = mul1.y * mul2.y + addend.y;
    out.z = mul1.z * mul2.z + addend.z;
    out.w = mul1.w * mul2.w + addend.w;
    return out;
}

HRESULT DmIsDebuggerPresent() { return 0; }

HRESULT NuiIdentityEnroll(
    DWORD, int, DWORD, NUI_IDENTITY_CALLBACK *, VOID *
) {
    return 0;
}

HRESULT NuiSkeletonGetNextFrame(DWORD, NUI_SKELETON_FRAME *pSkeletonFrame) {
    if (pSkeletonFrame) {
        std::memset(pSkeletonFrame, 0, sizeof(*pSkeletonFrame));
    }
    return 0;
}

#ifdef __APPLE__
__attribute__((weak))
#endif
int ctr_decrypt(
    const unsigned char *ct, unsigned char *pt, unsigned long len, symmetric_CTR *
) {
    if (ct != pt) {
        std::memcpy(pt, ct, len);
    }
    return 0;
}

}

XMMATRIX NuiTransformMatrixLevel(XMVECTOR) { return XMMATRIX(); }

void NuiTransformSkeletonToDepthImage(
    XMVECTOR, LONG *plDepthX, LONG *plDepthY, USHORT *pusDepthValue
) {
    if (plDepthX) *plDepthX = 0;
    if (plDepthY) *plDepthY = 0;
    if (pusDepthValue) *pusDepthValue = 0;
}

#endif // HX_NATIVE && !__EMSCRIPTEN__

// ============================================================================
// Shared HX_NATIVE stubs (both desktop native and web/emscripten)
// ============================================================================
#ifdef HX_NATIVE

#include "utl/JobMgr.h"
#include "xdk/LIBCMT/vectorintrinsics.h"
#include <cstring>

_XMMATRIX::_XMMATRIX() { std::memset(this, 0, sizeof(*this)); }

SingleItemEnumJob::SingleItemEnumJob(Hmx::Object *obj, int idx, u64 id)
    : Job(), mObject(obj), mUnkc(idx), mItemID(id), mStatus(0), mSuccess(false),
      unk20(0), unk24(0), mOverlapped() {
    std::memset(&mOverlapped, 0, sizeof(mOverlapped));
}
SingleItemEnumJob::~SingleItemEnumJob() {}
void SingleItemEnumJob::Start() { mStatus = 2; }
bool SingleItemEnumJob::IsFinished() { return true; }
void SingleItemEnumJob::Cancel(Hmx::Object *) {}
void SingleItemEnumJob::OnCompletion(Hmx::Object *) {}

MultipleItemsEnumJob::MultipleItemsEnumJob(
    Hmx::Object *obj, int userIndex, std::vector<u64> &itemIDs
) : Job(), mObject(obj), mUserIndex(userIndex), mItemIDs(itemIDs), mPurchased(), mStatus(0),
    mSuccess(false), mEnumBuffer(nullptr), mEnumHandle(0), mOverlapped(), mOfferSymbol(),
    mPurchaserID(0) {
    std::memset(&mOverlapped, 0, sizeof(mOverlapped));
}

MultipleItemsEnumJob::~MultipleItemsEnumJob() {}

void MultipleItemsEnumJob::Start() { mStatus = 2; }

bool MultipleItemsEnumJob::IsFinished() { return true; }

void MultipleItemsEnumJob::Cancel(Hmx::Object *) {}

void MultipleItemsEnumJob::OnCompletion(Hmx::Object *) {}

PostPurchaseEnumJob::PostPurchaseEnumJob(
    Hmx::Object *obj, int userIndex, u64 itemID, Symbol offerSym, unsigned int purchaserID
) : SingleItemEnumJob(obj, userIndex, itemID), mOfferSymbol(offerSym),
    mPurchaserID(purchaserID) {}
PostPurchaseEnumJob::~PostPurchaseEnumJob() {}
void PostPurchaseEnumJob::OnCompletion(Hmx::Object *obj) {
    SingleItemEnumJob::OnCompletion(obj);
}

MultipleItemsPostPurchaseEnumJob::MultipleItemsPostPurchaseEnumJob(
    Hmx::Object *obj, int userIndex, std::vector<u64> &itemIDs, Symbol offerSym,
    unsigned int purchaserID
) : MultipleItemsEnumJob(obj, userIndex, itemIDs) {
    mOfferSymbol = offerSym;
    mPurchaserID = purchaserID;
}
MultipleItemsPostPurchaseEnumJob::~MultipleItemsPostPurchaseEnumJob() {}
void MultipleItemsPostPurchaseEnumJob::OnCompletion(Hmx::Object *obj) {
    MultipleItemsEnumJob::OnCompletion(obj);
}

unsigned long long SingleItemEnumCompleteMsg::OfferID() const { return 0ULL; }

#endif // HX_NATIVE
