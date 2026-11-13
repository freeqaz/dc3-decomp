// xbox_link_stubs.cpp — native definitions for Xbox-360-only entry points that
// the decomp calls but no native back end provided.
//
// Why this file exists
// --------------------
// The native link used to pass `-Wl,--unresolved-symbols=ignore-all`, so every
// one of these symbols was simply *absent* from dc3-native and the link still
// reported success. They are lazily bound, so the binary starts and runs; the
// first call to any of them kills the process with
//     dc3-native: symbol lookup error: dc3-native: undefined symbol: <name>
// and exit status 127 (verified directly: `LD_BIND_NOW=1 ./dc3-native` exits
// 127 on BinkRegisterFrameBuffers).
//
// The link now passes `-Wl,--no-undefined`, so a gap here is a build failure
// instead of a latent crash. Everything below is what that flag surfaced.
//
// Two kinds of definition live here, and the distinction matters:
//
//   REAL   — the function has honest native semantics and is implemented.
//            (__vspltw / __vmaddfp are plain vector math; the gesture code in
//            src/system/gesture/Skeleton.cpp does its floor-normal transform
//            with them, so a zero-returning stub would silently corrupt every
//            skeleton position rather than fail loudly.)
//
//   STUB   — the function wraps hardware or a middleware library that does not
//            exist off-console (Bink video, the Kinect NUI driver, the Xbox
//            debug monitor, the pad-EEPROM back end). These follow the
//            established convention in engine_stubs_generated.cpp: announce the
//            hit through HX_STUB_TRACE and return a benign value. Set
//            DC3_STUB_TRACE=1 to see whether a run reaches one.

#include "StubTrace.h"

#include <cstring>

// ============================================================================
// PowerPC/VMX vector intrinsics — REAL
// ----------------------------------------------------------------------------
// Declared in src/xdk/LIBCMT/vectorintrinsics.h. Implementations ported from
// native/src/web_stubs.cpp, which had them correct but is guarded by
// `#ifdef __EMSCRIPTEN__` and is not in any source list, so the native build
// never saw them.
// ============================================================================

#include "xdk/LIBCMT/vectorintrinsics.h"

XMVECTOR __vspltw(XMVECTOR vSrcA, unsigned int uImmed) {
    XMVECTOR out = {};
    const float value = vSrcA.v[uImmed & 3];
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

// ============================================================================
// Kinect NUI
// ----------------------------------------------------------------------------
// NuiTransformMatrixLevel: the XDK's tilt correction, ported from the image
// (build/373307D9/asm/xdk/nuiapi/nuiskeleton.s, 0x829C3568, 0xF4 bytes).
// SkeletonFrame::Create multiplies every joint by it (XMVector3Transform, row
// vectors), and the native sensor frame does the same with its estimated
// gravity (platform/PoseLevel_Native.cpp).  What the image does:
//   n   = vNormalToGravity / |vNormalToGravity|   (vrsqrtefp + one Newton
//         step; a zero vector is kept as it is)
//   ax  = cross(n, (0,1,0))                       (vpermwi 99/135 + vnmsubfp)
//   all four lanes of ax == 0  ->  the identity   (vupkd3d128 + vpermwi)
//   else XMMatrixRotationAxis(ax, acos(dot((0,1,0), n)))
// so the matrix rotates the sensor's "up" onto +y about the axis
// perpendicular to both: the minimal rotation, which levels pitch and roll and
// leaves yaw alone.  An "up" of exactly (0,1,0) -- or (0,-1,0), or zero --
// gives the identity.  XMMatrixRotationAxis normalises the axis and builds
// XMMatrixRotationNormal's matrix (a right-handed rotation of a row vector,
// v * M).  Not bit-exact: an exact 1/sqrt and libm sin/cos stand in for the
// VMX estimate and XMScalarSinCos (differences ~1e-7).
// ============================================================================

#include "xdk/nui/nuiskeleton.h"
#include "xdk/nui/nuiidentity.h"

#include <cmath>

XMMATRIX NuiTransformMatrixLevel(XMVECTOR vNormalToGravity) {
    XMMATRIX m;
    for (int row = 0; row < 4; ++row) {
        m.r[row].x = (row == 0) ? 1.0f : 0.0f;
        m.r[row].y = (row == 1) ? 1.0f : 0.0f;
        m.r[row].z = (row == 2) ? 1.0f : 0.0f;
        m.r[row].w = (row == 3) ? 1.0f : 0.0f;
    }
    float nx = vNormalToGravity.x, ny = vNormalToGravity.y, nz = vNormalToGravity.z;
    float nw = vNormalToGravity.w;
    float lenSq = nx * nx + ny * ny + nz * nz + nw * nw; // vmsum4fp128: all four lanes
    if (lenSq != 0.0f) {
        float inv = 1.0f / sqrtf(lenSq);
        nx *= inv;
        ny *= inv;
        nz *= inv;
    }
    // cross(n, up) with up = (0, 1, 0): (n.y*0 - n.z*1, n.z*0 - n.x*0, n.x*1 - n.y*0)
    float ax = -nz, ay = 0.0f, az = nx;
    if (ax == 0.0f && az == 0.0f)
        return m;
    float angle = (float)acos((double)ny); // dot(up, n)
    float axLen = sqrtf(ax * ax + ay * ay + az * az);
    ax /= axLen;
    ay /= axLen;
    az /= axLen;
    float s = sinf(angle), c = cosf(angle), t = 1.0f - c;
    // XMMatrixRotationNormal: r0 = (c + t x^2, t x y + s z, t x z - s y), ...
    m.r[0].x = c + t * ax * ax;
    m.r[0].y = t * ax * ay + s * az;
    m.r[0].z = t * ax * az - s * ay;
    m.r[1].x = t * ax * ay - s * az;
    m.r[1].y = c + t * ay * ay;
    m.r[1].z = t * ay * az + s * ax;
    m.r[2].x = t * ax * az + s * ay;
    m.r[2].y = t * ay * az - s * ax;
    m.r[2].z = c + t * az * az;
    return m;
}

extern "C" {

// The native sensor's frame clock (native/src/platform/GestureMgr_Native.cpp):
// SkeletonUpdate::Update calls this once per PostUpdate natively, and must see
// "no new frame" when the sensor delivered none, or it re-polls the previous
// frame and integrates it twice.  Null outside the sensor's lifetime.
extern HRESULT (*gNativeNuiSkeletonFrameSource)(NUI_SKELETON_FRAME *);

HRESULT NuiSkeletonGetNextFrame(DWORD, NUI_SKELETON_FRAME *pSkeletonFrame) {
    HX_STUB_TRACE("NuiSkeletonGetNextFrame");
    // Zero the frame rather than leaving it uninitialised: SkeletonUpdate.cpp
    // treats a 0 return as success and reads the frame straight away.  It only
    // reads it when no override camera is active (SkeletonFrame::Create), and
    // the native sensor is an override camera, so zeroes are never consumed.
    if (pSkeletonFrame) {
        std::memset(pSkeletonFrame, 0, sizeof(*pSkeletonFrame));
    }
    if (gNativeNuiSkeletonFrameSource)
        return gNativeNuiSkeletonFrameSource(pSkeletonFrame);
    return 0;
}

HRESULT NuiIdentityEnroll(DWORD, int, DWORD, NUI_IDENTITY_CALLBACK *, VOID *) {
    HX_STUB_TRACE("NuiIdentityEnroll");
    return 0;
}

// PLATFORM: no Kinect identity pipeline.  Moved here from the untyped
// generated stubs (which are compiled out under __EMSCRIPTEN__) so web links a
// definition too: ShellInput::Init now builds the SkeletonIdentifier, whose
// Poll reaches Skeleton::RequestIdentity -> NuiIdentityIdentify, and
// GestureMgr::SetIdentificationEnabled can reach NuiIdentityAbort.  Same
// return as before (0): Identify "completes" synchronously with no
// enrollment, which IdentityInfo::PostUpdate reports as unidentified.
HRESULT NuiIdentityIdentify(DWORD, DWORD, NUI_IDENTITY_CALLBACK *, VOID *) {
    HX_STUB_TRACE("NuiIdentityIdentify");
    return 0;
}

HRESULT NuiIdentityAbort() {
    HX_STUB_TRACE("NuiIdentityAbort");
    return 0;
}

// PLATFORM: no Kinect identity store natively, so no enrollment slot is in
// use -- report every slot empty (dwEnrollmentFlags == 0), which is what
// SkeletonIdentifier::UpdateEnrolledPlayers reads as "nobody enrolled" (pad -1).
// The untyped generated stub returned 0 without writing the struct, so the
// caller read its own uninitialised stack.
HRESULT NuiIdentityGetEnrollmentInformation(
    DWORD, NUI_ENROLLMENT_INFORMATION *pEnrollmentInformation
) {
    HX_STUB_TRACE("NuiIdentityGetEnrollmentInformation");
    if (pEnrollmentInformation) {
        std::memset(pEnrollmentInformation, 0, sizeof(*pEnrollmentInformation));
    }
    return 0;
}

} // extern "C"

// ============================================================================
// Xbox LIVE title-server enumeration — STUB
// ----------------------------------------------------------------------------
// NetCacheMgrXbox (the store/MOTD download cache, created by NetCacheMgrInit)
// reaches this through XLSPConnection::Connect -> StartEnumeration when a load
// starts. There is no Xbox LIVE service natively, so the enumeration cannot be
// created: the image's own failure arm then runs (MILO_NOTIFY, XLSPConnection
// -> kFailed, NetCacheMgrXbox::Poll -> SetFail), and the cache reports
// "store server unavailable" to MainMenuPanel / StorePanel rather than staying
// in kNCMS_Load forever.
//
// The previous untyped generated stub returned 0 (ERROR_SUCCESS) without
// writing *hEnum, which leaves XLSPConnection waiting in its enumerate state
// for good. Any non-ERROR_SUCCESS value drives the same image path; the
// specific code is not taken from an XDK trace. Typed here (not in the
// generated stubs, which are compiled out under __EMSCRIPTEN__) so web links
// a definition too.
// ============================================================================

#include "xdk/xapilibi/winerror.h"
#include "xdk/xonline/xonline.h"

extern "C" DWORD XTitleServerCreateEnumerator(LPCSTR, DWORD, DWORD *pcbBuffer, HANDLE *) {
    HX_STUB_TRACE("XTitleServerCreateEnumerator");
    if (pcbBuffer) *pcbBuffer = 0;
    return ERROR_NO_SUCH_USER; // no signed-in LIVE user, so no title servers
}

// ============================================================================
// Xbox Debug Monitor — STUB
// ============================================================================

#include "xdk/xbdm/xbdm.h"

HRESULT DmIsDebuggerPresent() {
    HX_STUB_TRACE("DmIsDebuggerPresent");
    return 0; // FALSE — no Xbox debug monitor attached
}

// ============================================================================
// Joypad platform back end — STUB
// ----------------------------------------------------------------------------
// The real bodies live in Joypad_Xbox.cpp, which is excluded from the native
// build. Input actually reaches the game through the engine's Joypad_Native
// path, so these are only hit if decomp Joypad::Poll runs directly.
// kJoypadNone (0) means "nothing attached", which is the safe answer.
// ============================================================================

#include "os/Joypad.h"

extern "C" {

int ReadSingleJoypad(
    int, unsigned int *buttons, char *lx, char *ly, char *rx, char *ry,
    char *lt, char *rt, float *sensors, float *pressures,
    unsigned char *pro_guitar
) {
    HX_STUB_TRACE("ReadSingleJoypad");
    if (buttons) *buttons = 0;
    if (lx) *lx = 0;
    if (ly) *ly = 0;
    if (rx) *rx = 0;
    if (ry) *ry = 0;
    if (lt) *lt = 0;
    if (rt) *rt = 0;
    if (sensors) std::memset(sensors, 0, sizeof(float) * 4);
    if (pressures) std::memset(pressures, 0, sizeof(float) * 12);
    if (pro_guitar) std::memset(pro_guitar, 0, 8);
    return 0; // kJoypadNone
}

// The PPC body is `b XamInputSendStayAliveRequest`, a Xam call that tells the
// wireless stack to keep an idle pad associated. There is no off-console
// equivalent and nothing to emulate, so this is a pure no-op.
//
// It was NOT surfaced by -Wl,--no-undefined, and that is the whole point of
// project task #172: clang at -O2 proves the only call site unreachable
// (`gPadsToKeepAlive` has internal linkage in Joypad.cpp and nothing ever
// stores a non-zero value into it) and deletes the reference before the
// linker sees it. The symbol was undefined for as long as the file has
// existed and the link stayed green. At -O0 the reference survives and the
// link fails. See scripts/check_undefined_decomp_symbols.py, which reads the
// MSVC objects instead and does not depend on an optimizer decision.
void JoypadSendKeepAlive(int) { HX_STUB_TRACE("JoypadSendKeepAlive"); }

} // extern "C"

bool requestBreedWrite(int, unsigned char *) {
    HX_STUB_TRACE("requestBreedWrite");
    return false;
}

// ============================================================================
// Bink video — STUB
// ----------------------------------------------------------------------------
// The RAD Bink middleware is Xbox-only. engine_stubs_generated.cpp already
// stubs BinkOpen/BinkClose/BinkInit/etc.; these are the ones it missed.
// bink.h wraps its declarations in RADDEFSTART (== `extern "C" {`), so
// including it is what gives these C linkage — do not hand-write `extern "C"`
// with a guessed signature, which is exactly how XNotifyCreateListener ended up
// mangled and undefined.
// ============================================================================

#include "binkxenon/bink.h"

S32 RADEXPLINK BinkDoFrame(HBINK) {
    HX_STUB_TRACE("BinkDoFrame");
    return 0;
}

S32 RADEXPLINK BinkDoFrameAsync(HBINK, U32, U32) {
    HX_STUB_TRACE("BinkDoFrameAsync");
    return 0;
}

S32 RADEXPLINK BinkDoFrameAsyncWait(HBINK, S32) {
    HX_STUB_TRACE("BinkDoFrameAsyncWait");
    return 1; // "async work finished" — never leave a caller spinning
}

void RADEXPLINK BinkGetFrameBuffersInfo(HBINK, BINKFRAMEBUFFERS *fbset) {
    HX_STUB_TRACE("BinkGetFrameBuffersInfo");
    if (fbset) std::memset(fbset, 0, sizeof(*fbset));
}

void RADEXPLINK BinkRegisterFrameBuffers(HBINK, BINKFRAMEBUFFERS *) {
    HX_STUB_TRACE("BinkRegisterFrameBuffers");
}

void RADEXPLINK BinkGetSummary(HBINK, BINKSUMMARY *sum) {
    HX_STUB_TRACE("BinkGetSummary");
    if (sum) std::memset(sum, 0, sizeof(*sum));
}

HBINKTRACK RADEXPLINK BinkOpenTrack(HBINK, U32) {
    HX_STUB_TRACE("BinkOpenTrack");
    return nullptr;
}

S32 RADEXPLINK BinkPause(HBINK, S32) {
    HX_STUB_TRACE("BinkPause");
    return 0;
}

S32 RADEXPLINK BinkSetSoundOnOff(HBINK, S32) {
    HX_STUB_TRACE("BinkSetSoundOnOff");
    return 0;
}

void RADEXPLINK BinkSetVolume(HBINK, U32, S32) {
    HX_STUB_TRACE("BinkSetVolume");
}

S32 RADEXPLINK BinkShouldSkip(HBINK) {
    HX_STUB_TRACE("BinkShouldSkip");
    return 0; // never ask the caller to skip a frame
}

S32 RADEXPLINK BinkWait(HBINK) {
    HX_STUB_TRACE("BinkWait");
    return 0; // 0 == "don't wait, decode now"
}

// Only referenced on the non-FFmpeg path: BinkMovieSys::Init calls this inside
// `#ifndef HX_FFMPEG`, and only dc3-native defines HX_FFMPEG. render-test and
// the three export tools do not, so they emitted the reference and nothing
// satisfied it. engine_stubs_generated.cpp appears to cover this symbol, but
// its stub is an asm-label alias for `_Z20BinkStartAsyncThreadii` -- C++
// mangling, and the wrong parameter types -- whereas bink.h declares it
// extern "C" as (S32, void const *). It could never have matched the call.
// Returns 1 because BinkMovieSys::Init asserts on the result.
S32 RADEXPLINK BinkStartAsyncThread(S32, void const *) {
    HX_STUB_TRACE("BinkStartAsyncThread");
    return 1; // success
}

// ============================================================================
// BinkMovie platform hooks — STUB
// ----------------------------------------------------------------------------
// These two are DC3 decomp members whose bodies live in BinkMovieImpl_Xbox.cpp
// and BinkMovieSys_Xbox.cpp. native/src/platform/BinkMovie_Stub.cpp claims in
// its header comment to "replace" both files, but it contains only that comment
// — three lines, no code — so neither symbol was ever defined natively.
// ============================================================================

#include "moviebink/BinkMovieImpl.h"
#include "moviebink/BinkMovieSys.h"

bool BinkMovieImpl::PlatformCacheFile(const char *) {
    HX_STUB_TRACE("BinkMovieImpl::PlatformCacheFile");
    return false; // "not cached" — caller falls back to streaming from disk
}

void BinkMovieSys::PlatformStoreCache(void *, unsigned int) {
    HX_STUB_TRACE("BinkMovieSys::PlatformStoreCache");
}
