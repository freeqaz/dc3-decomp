// web_stubs.cpp — Proper C++ stub implementations for WASM build
// These replace the asm-label stubs in engine_stubs_generated.cpp that wasm-ld
// can't handle (asm labels produce wrong mangled names or `unreachable` traps).
// Each stub here uses proper C++ types so the Itanium ABI mangler generates
// the correct symbol names for libc++ (std::__2 namespace).

#ifdef __EMSCRIPTEN__

#include <cstdlib>

// ============================================================================
// Bink video — not supported in browser
// ============================================================================

void BinkSetMemory(void *(*)(int), void (*)(void *)) {}
int BinkStartAsyncThread(int, int) { return 1; }  // 1 = success
void *RadAlloc(int size) { return malloc(size); }

// Xbox Debug Monitor — not available on web
#include "xdk/xbdm/xbdm.h"

HRESULT DmMapDevkitDrive() { return 0; }

extern "C" {
HRESULT DmGetSystemInfo(DM_SYSTEM_INFO *) { return -1; }  // E_FAIL
}

// Joypad actuator (force feedback) — no haptics on web
#include "os/Joypad.h"
void JoypadSetActuatorsImp(int, int, int) {}

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

// Bink movie platform stubs — not supported in browser
#include "moviebink/BinkMovieSys.h"
void BinkMovieSys::PlatformInit() {}

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

// Network — NetworkSocket_Stub.cpp provides IPStringToInt

// Win32/XDK stubs — APIs that don't exist on web
int WSACreateEvent() { return 0; }
int WaitForSingleObject(int, int) { return 0; }
int CloseHandle(int) { return 0; }
int WideCharToMultiByte(int, int, const void *, int, char *, int, const char *, int *) { return 0; }
int XNetDnsLookup(int, int, void *) { return 0; }
void XNetDnsRelease(void *) {}
int XSetThreadProcessor(int, int) { return 0; }

// Color write mask stub (rendering) — web uses different pipeline
#include "rndobj/Mat.h"
void SetColorWriteMask(const MatShaderOptions &, RndMat *) {}

#endif // __EMSCRIPTEN__

// HX_NATIVE-only stubs — guarded against __EMSCRIPTEN__ for symbols
// already defined in the __EMSCRIPTEN__ section above
#if defined(HX_NATIVE) && !defined(__EMSCRIPTEN__)

#include "gesture/LiveCameraInput.h"
#include "utl/JobMgr.h"
#include "utl/TextStream.h"
#include "xdk/LIBCMT/ppcintrinsics.h"
#include "xdk/LIBCMT/vectorintrinsics.h"
#include "xdk/nui/nuiidentity.h"
#include "xdk/nui/nuiskeleton.h"
#include "xdk/xbdm/xbdm.h"

#include <cstring>

struct symmetric_CTR;

void LiveCameraInput::LockStream(const void *, LockedRect &) {}

void LiveCameraInput::UnlockStream(const void *) {}

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
