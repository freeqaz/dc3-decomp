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
// Kinect NUI — STUB
// ----------------------------------------------------------------------------
// NuiTransformMatrixLevel returns the identity matrix: Skeleton.cpp multiplies
// joint positions through it to correct for sensor tilt, so identity is the
// honest "no tilt correction applied" answer. A zero matrix would collapse
// every joint to the origin.
// ============================================================================

#include "xdk/nui/nuiskeleton.h"
#include "xdk/nui/nuiidentity.h"

XMMATRIX NuiTransformMatrixLevel(XMVECTOR) {
    HX_STUB_TRACE("NuiTransformMatrixLevel");
    XMMATRIX m;
    for (int row = 0; row < 4; ++row) {
        m.r[row].x = (row == 0) ? 1.0f : 0.0f;
        m.r[row].y = (row == 1) ? 1.0f : 0.0f;
        m.r[row].z = (row == 2) ? 1.0f : 0.0f;
        m.r[row].w = (row == 3) ? 1.0f : 0.0f;
    }
    return m;
}

extern "C" {

HRESULT NuiSkeletonGetNextFrame(DWORD, NUI_SKELETON_FRAME *pSkeletonFrame) {
    HX_STUB_TRACE("NuiSkeletonGetNextFrame");
    // Zero the frame rather than leaving it uninitialised: SkeletonUpdate.cpp
    // treats a 0 return as success and reads the frame straight away.
    if (pSkeletonFrame) {
        std::memset(pSkeletonFrame, 0, sizeof(*pSkeletonFrame));
    }
    return 0;
}

HRESULT NuiIdentityEnroll(DWORD, int, DWORD, NUI_IDENTITY_CALLBACK *, VOID *) {
    HX_STUB_TRACE("NuiIdentityEnroll");
    return 0;
}

} // extern "C"

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
