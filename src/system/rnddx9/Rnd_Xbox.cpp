#include "Cam.h"
#include "Env.h"
#include "Lit.h"
#include "Mat.h"
#include "Memory.h"
#include "Mesh.h"
#include "Movie.h"
#include "MultiMesh.h"
#include "Part.h"
#include "RenderState.h"
#include "Tex.h"
#include "TexRenderer.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "os\Timer.h"
#include "rnddx9\CubeTex.h"
#include "rnddx9\OcclusionQueryMgr.h"
#include "rnddx9\Rnd.h"
#include "rndobj/Cam.h"
#include "rndobj\DOFProc_NG.h"
#include "rndobj\Flare.h"
#include "rndobj\HiResScreen.h"
#include "rndobj\Mat_NG.h"
#include "rndobj\Overlay.h"
#include "rndobj\PostProc.h"
#include "rndobj\PostProc_NG.h"
#include "rndobj\Rnd.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\Shader.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\ShadowMap.h"
#include "rndobj\Stats_NG.h"
#include "rndobj\Tex.h"
#include "utl\MemTrack.h"
#include "utl\Option.h"
#include "xdk\D3D9.h"
#include "xdk\d3d9i\d3d9.h"
#include "xdk\d3d9i\d3d9caps.h"
#include "xdk\d3d9i\d3d9types.h"
#include "xdk\win_types.h"
#include "xdk\xapilibi\processthreadsapi.h"
#include "xdk\xapilibi\xbase.h"
#include "xdk\xapilibi\xbox.h"

void CreateBackBuffers(int, int, D3DMULTISAMPLE_TYPE, unsigned int &, unsigned int &, D3DSurface *&, D3DSurface *&);

// RESIDUAL (w7-an, 70.3 canonical): the member LAYOUT is confirmed correct --
// every store the image makes lands on a member we declare at that offset
// (0x304/0x310 vectors, 0x360/0x361 bools, 0x364/0x368/0x36c pointers,
// 0x370/0x374 floats, 0x378/0x3a4 bools), and the whole body after the
// initialiser list (0x3f4 .. 0x34d, indices 62-76) is instruction-for-
// instruction equal.  What differs is a scheduling permutation of ten stores
// inside indices 28-61: (a) the image finishes each inlined vector ctor
// before starting the next -- `addi r11, r30, BASE` / `addi r11, r11, 0x8` /
// three null stores / `stw r11, 0x50(r31)` -- where MSVC interleaves ours and
// defers both `stw ..., 0x50(r31)` homings; (b) the image emits the
// 0x360..0x36c group BEFORE the two `stfs` of mGPUBusyMs/mGPUCountMs and
// splits them around `stb r29, 0x378`, where MSVC hoists our `lis
// __real@00000000@h` to index 28 and both floats ahead of the group; (c) the
// image emits one extra `addi r11, r30, 0x350` (index 44) that nothing
// consumes.  The initialiser list is already in declaration order, which is
// the order MSVC emits regardless of how the list is written, so there is no
// source-order lever here.
DxRnd::DxRnd()
    : mInited(0),
      mD3DDevice(nullptr),
      mFocusWindow(0),
      mDeviceType(D3DDEVTYPE_HAL),
      mReverseZ(1),
      mAsyncSwapNext(false),
      mAsyncSwapCurrent(false),
      mPerfCounterStart(nullptr),
      mPerfCounterEnd(nullptr),
      mGPUTimer(nullptr),
      mGPUBusyMs(0.0f),
      mGPUCountMs(0.0f),
      mCreatedPerfCounters(false),
      mPostProcDone(false),
      mSuspended(false),
      mPIXCaptureState(false),
      mPreInited(false),
      unk408(0) {
    mInited = 1;
    mFrontBuffers[0] = nullptr;
    mFrontBuffers[1] = nullptr;
    mBackBuffer = nullptr;
    mWorldDepth = nullptr;
    mOffscreenRT = nullptr;
    mOffscreenDepth = nullptr;
    mFlags = 0;
    mFrontBufIdx = 0;
    mNumTiles = 0;
    unk34d = true;
}

DxRnd::~DxRnd() {
    if ((unsigned int)mInited) {
        mInited = 0;
    }
}

void CDError() {
    TheDxRnd.Suspend();
    ShowDirtyDiscError();
}

void DxModal(Debug::ModalType &t, FixedString &s, bool b) { TheDxRnd.Modal(t, s, b); }

void DxRnd::PreInit(HWND__ *) {
    if (!mPreInited) {
        mPreInited = true;
        DataArray *cfg = SystemConfig("rnd");
        mDefaultVSRegAlloc = 32;
        mDefaultPSRegAlloc = 96;
        DataArray *gprArr = cfg->FindArray("shader_gpr_alloc", false);
        if (gprArr) {
            mDefaultVSRegAlloc = gprArr->Int(1);
            mDefaultPSRegAlloc = gprArr->Int(2);
        }
        MILO_ASSERT(mDefaultVSRegAlloc + mDefaultPSRegAlloc == GPU_GPRS, 0x1F0);
        MILO_ASSERT(mDefaultVSRegAlloc >= 16, 0x1F1);
        MILO_ASSERT(mDefaultPSRegAlloc >= 16, 0x1F2);
        SetDiskErrorCallback(CDError);
        mPrintGlitches = OptionBool("print_glitches", false);
        mCaptureNextFrame = false;
        mD3DDevice = nullptr;
        mFocusWindow = 0;
        NgRnd::PreInit();
        InitBuffers();
        TheShaderMgr.PreInit();
        TheRenderState.Init();
        Suspend();
        REGISTER_OBJ_FACTORY(DxTexRenderer)
        REGISTER_OBJ_FACTORY(DxCam)
        REGISTER_OBJ_FACTORY(DxEnviron);
        REGISTER_OBJ_FACTORY(DxMesh)
        DxMat::Init();
        REGISTER_OBJ_FACTORY(DxTex)
        REGISTER_OBJ_FACTORY(DxCubeTex);
        DxMultiMesh::Init();
        REGISTER_OBJ_FACTORY(DxMovie)
        DxParticleSys::Init();
        DxLight::Init();
        CreatePostTextures();
        DxTex::SetEDRamChecksEnabled(false);
        NgPostProc::Init();
        NgDOFProc::Init();
        DxTex::SetEDRamChecksEnabled(true);
        RndShadowMap::Init();
        Rnd::CreateDefaults();
        TheDebug.SetModalCallback(DxModal);
    }
}

void DxRnd::Init(HWND__ *h) {
    PreInit(h);
    mOcclusionQueryMgr = new DxRndOcclusionQueryMgr();
    NgRnd::Init();
    DxTex::Init();
    mAsyncSwapNext = false;
}

void DxRnd::Terminate() {
    Resume();
    DxLight::Terminate();
    DxMultiMesh::Shutdown();
    NgPostProc::Terminate();
    NgRnd::Terminate();
    RELEASE(mPreProcessTex);
    RELEASE(mPostProcessTex);
    RELEASE(mPreDepthTex);
    TerminateBuffers();
}

void DxRnd::SetSync(int sync) {
    Rnd::SetSync(sync);
    Resume();
    if (mSync == 0) {
        D3DDevice_SetRenderState_PresentInterval(TheDxRnd.Device(), 0x80000000);
    } else if (mSync == 1) {
        D3DDevice_SetRenderState_PresentInterval(TheDxRnd.Device(), 1);
    } else if (mSync == 2) {
        D3DDevice_SetRenderState_PresentInterval(TheDxRnd.Device(), 2);
    } else {
        MILO_FAIL("Not allowed to sync %d\n", mSync);
    }
}

void DxRnd::SetAspect(Aspect a) {
    if (mAspect != a) {
        Rnd::SetAspect(a);
        UpdateScalerParams();
        ResetDevice();
    }
}

void DxRnd::SetShrinkToSafeArea(bool shrink) {
    if (shrink != mShrinkToSafe) {
        Rnd::SetShrinkToSafeArea(shrink);
        UpdateScalerParams();
        ResetDevice();
    }
}

void DxRnd::DoWorldEnd() {
    if (mProcCmds & kProcessWorld) {
        Rnd::DoWorldEnd();
        {
            START_AUTO_TIMER("draw");
            DoPointTests();
        }
        SavePreBuffer();
    }
}

void DxRnd::DoPostProcess() {
    SetFrameBuffersAsSource();
    if (mProcCmds & kProcessPost) {
        if (mRegAlloc != 2) {
            mRegAlloc = (RegisterAlloc)2;
            D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0x10, 0x70);
        }
        NgRnd::DoPostProcess();
        FinishPostProcess();
    }
    D3DDevice_SetRenderTarget_External(mD3DDevice, 0, mOffscreenRT);
    D3DDevice_SetDepthStencilSurface(mD3DDevice, mOffscreenDepth);
    BeginTiling(Hmx::Color(0, 0, 0.3), 0, 0);
    CopyPostProcess();
    if (mRegAlloc != 1) {
        mRegAlloc = (RegisterAlloc)1;
        D3DDevice_SetShaderGPRAllocation(
            mD3DDevice, 0, mDefaultVSRegAlloc, mDefaultPSRegAlloc
        );
    }
    mPostProcDone = true;
}

void DxRnd::Suspend() {
    if (!mD3DDevice || mAsyncSwapCurrent) {
        return;
    }
    MILO_ASSERT(!mDrawing, 0x695);
    if (!mSuspended) {
        static Timer *cpuTimer = AutoTimer::GetTimer("cpu");
        if (mPrintGlitches && cpuTimer->SplitMs() > 30.0f) {
            MILO_LOG("GLITCH (pre-suspend): %i ms\n", (int)cpuTimer->SplitMs());
        }
        mAsyncSwapNext = false;
        D3DDevice_Suspend(mD3DDevice);
    }
    mSuspended = true;
}

void DxRnd::Resume() {
    if ((int)mD3DDevice) {
        if (mSuspended) {
            MILO_ASSERT(mAsyncSwapCurrent == false, 0x6AE);
            D3DDevice_Resume(mD3DDevice);
            mAsyncSwapNext = false;
        }
        mSuspended = false;
    }
}

D3DSurface *DxRnd::BackBuffer() const {
    D3DResource_AddRef(mBackBuffer);
    return mBackBuffer;
}

D3DTexture *DxRnd::FrontBuffer() { return mFrontBuffers[mFrontBufIdx - 1 & 1]; }
D3DTexture *DxRnd::NotFrontBuffer() { return mFrontBuffers[mFrontBufIdx]; }

const char *DxRnd::Error(long code) { return MakeString("code %d", code); }

void DxRnd::Present() {
    mFrontBufIdx = (mFrontBufIdx - 1) & 1;
    if (mAsyncSwapCurrent) {
        static D3DSWAP_STATUS swapStatus;
        while (D3DDevice_QuerySwapStatus(mD3DDevice, &swapStatus),
               swapStatus.EnqueuedCount != 0) {
            Sleep(0);
        }
    } else {
        D3DDevice_SynchronizeToPresentationInterval(mD3DDevice);
    }
    D3DDevice_Swap(mD3DDevice, NotFrontBuffer(), nullptr);
    if (mAsyncSwapCurrent != mAsyncSwapNext) {
        mAsyncSwapCurrent = mAsyncSwapNext;
        D3DDevice_BlockUntilIdle(mD3DDevice);
        D3DDevice_SetSwapMode(mD3DDevice, mAsyncSwapCurrent);
    }
    mPIXCaptureState = PIXGetCaptureState() & 2;
}

void DxRnd::UpdateScalerParams() {
    float width = (float)mVideoMode.dwDisplayWidth;
    float height = (float)mVideoMode.dwDisplayHeight;
    bool letterbox = mAspect == kLetterbox && !mLowRes;
    // Letterbox: constrain to 16:9 aspect ratio (9/16 = 0.5625)
    if (letterbox && width * 0.5625f < height) {
        height = width * 0.5625f;
    }
    // Shrink to safe area: 95% of display dimensions
    if (mShrinkToSafe) {
        width *= 0.95f;
        if (!letterbox) {
            height *= 0.95f;
        }
    }
    D3DVIDEO_SCALER_PARAMETERS& scaler = mPresentParams.VideoScalerParameters;
    scaler.ScaledOutputWidth = width;
    scaler.ScaledOutputHeight = height;
}

void DxRnd::TerminateBuffers() {
    PreDeviceReset();
    if (mD3DDevice) {
        D3DDevice_Release(mD3DDevice);
        mD3DDevice = nullptr;
    }
}

void DxRnd::SetupGamma() {
    DataArray *cfg = SystemConfig("rnd");
    float gamma;
    if (cfg->FindData("gamma", gamma, false)) {
        D3DGAMMARAMP ramp;
        unsigned int i = 0;
        unsigned short i16;
        do {
            float fval = (float)(int)i * 0.00390625f;
            float fpow = std::pow(fval, gamma);
            unsigned long long ival = (long long)(fpow * 1024.0f);
            unsigned short usVal = (unsigned short)((unsigned short)ival << 6);
            ramp.red[i] = usVal;
            ramp.green[i] = usVal;
            ramp.blue[i] = usVal;
            i16 = (unsigned short)((i + 1) & 0xffff);
            i = i16;
        } while (i16 < 0x100);
        D3DDevice_SetGammaRamp(mD3DDevice, 0, &ramp);
    }
}

void DxRnd::SetDefaultRenderStates() {
    D3DCAPS9 caps;
    memset(&caps, 0, sizeof(D3DCAPS9));
    GetDeviceCaps(&caps);
    D3DDevice_SetRenderState_AlphaRef(TheDxRnd.Device(), 0);
    D3DDevice_SetRenderState_AlphaFunc(TheDxRnd.Device(), D3DCMP_GREATER);
    unsigned int maxPointSize = (DWORD &)caps.MaxPointSize;
    D3DDevice_SetRenderState_PointSizeMax(TheDxRnd.Device(), maxPointSize);
    D3DDevice_SetRenderState_SeparateAlphaBlendEnable(TheDxRnd.Device(), 1);
    D3DDevice_SetRenderState_SrcBlendAlpha(TheDxRnd.Device(), 1);
    D3DDevice_SetRenderState_DestBlendAlpha(TheDxRnd.Device(), 1);
    D3DDevice_SetRenderState_BlendOpAlpha(TheDxRnd.Device(), 3);
    for (unsigned int i = 0; i < caps.MaxTextureBlendStages; i++) {
        D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), i, 1);
        D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), i, 1);

        unsigned char* device = reinterpret_cast<unsigned char*>(TheDxRnd.Device());
        unsigned int* stage_ptr = reinterpret_cast<unsigned int*>(device + i * 0x18 + 0x48C);
        *stage_ptr = (*stage_ptr & 0xFE7FFFFF) | 0x800000;

        unsigned long long* state64 = reinterpret_cast<unsigned long long*>(device + 0x18);
        unsigned long long shift64 = (unsigned long long)(i + 0x20);
        unsigned long long mask = 0x8000000000000000ULL;
        *state64 |= mask >> shift64;
    }

    D3DDevice_SetRenderState_PresentImmediateThreshold(TheDxRnd.Device(), 100);
}

void DxRnd::InitRenderState() {
    PhysMemTypeTracker tracker("D3D(phys):DxRnd");
    if (!mD3DDevice) {
        return;
    }
    SetDefaultRenderStates();
    D3DXSetDXT3DXT5(1);
    SetupGamma();
}

void DxRnd::BeginTiling(const Hmx::Color &c, float f, unsigned int ui) {
    if (mNumTiles == 0) {
        D3DDevice_Clear(mD3DDevice, 0, nullptr, 0x31, MakeColor(c), f, ui, 0);
    } else {
        XMVECTOR v = {c.red, c.green, c.blue, c.alpha};
        D3DDevice_BeginTiling(mD3DDevice, 0, mNumTiles, mTileRects, &v, f, ui);
        mTilingActive = true;
    }
}

void DxRnd::SetFrameBuffersAsSource() {
    D3DDevice_SetTexture(mD3DDevice, 6, mPreProcessBuffer, 0x02000000);
    D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), 6, 1);
    D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), 6, 1);
    D3DDevice_SetSamplerState_MipFilter(TheDxRnd.Device(), 6, 2, 0x02000000);
    D3DDevice_SetSamplerState_AddressU(TheDxRnd.Device(), 6, 2, 0x02000000);

    D3DDevice_SetTexture(mD3DDevice, 9, mFrontBufferDepth, 0x400000);
    D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), 9, 0);
    D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), 9, 0);
    D3DDevice_SetSamplerState_MipFilter(TheDxRnd.Device(), 9, 2, 0x400000);
    D3DDevice_SetSamplerState_AddressU(TheDxRnd.Device(), 9, 2, 0x400000);

    D3DDevice_SetTexture(mD3DDevice, 14, mPostProcessBuffer, 0x20000);
    D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), 14, 1);
    D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), 14, 1);
    D3DDevice_SetSamplerState_MipFilter(TheDxRnd.Device(), 14, 2, 0x20000);
    D3DDevice_SetSamplerState_AddressU(TheDxRnd.Device(), 14, 2, 0x20000);
}

void DxRnd::FinishPostProcess() {
    SetFrameBuffersAsSource();
    D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), 3, 1);
    D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), 3, 1);
    D3DDevice_SetSamplerState_MipFilter(TheDxRnd.Device(), 3, 2, 0x10000000);
    D3DDevice_SetSamplerState_AddressU(TheDxRnd.Device(), 3, 2, 0x10000000);
    D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), 0xD, 1);
    D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), 0xD, 1);
    D3DDevice_SetRenderTarget_External(mD3DDevice, 0, mBackBuffer);
    D3DDevice_SetDepthStencilSurface(mD3DDevice, mWorldDepth);
    // ADJUDICATED AND REFUSED -- do not "fix" the MakeColor row here.  Retail
    // CALLS MakeColor out of line at this one site (`bl ?MakeColor@@YAKABV
    // Color@Hmx@@@Z` at 0x82617DF4, after storing 0,0,0.3,1.0 to a Color temp);
    // we inline it and constant-fold the whole thing to 0xff00004c.  That
    // single `bl` is the ONLY one in the image -- 1 across all 2,223 target
    // objects -- while the ARGB pack sequence appears inlined in eight target
    // functions, so it is a per-site inliner decision, not a linkage one.
    // Measured, whole report, name_check:
    //   * three call-site spellings (temporary / named Color local / Rect via
    //     Set() / Rect via member stores) are INERT -- byte-identical 29
    //     mismatch rows at identical offsets, 83.608696 every time;
    //   * `__declspec(noinline)` on MakeColor buys this function +10.90pp
    //     (83.91304 -> 94.81739) and costs EIGHT neighbours: BeginTiling
    //     99.96 -> 29.72, Clear 100.0 -> 42.89, DrawRect 100.0 -> 57.67,
    //     DrawLine 98.64 -> 54.98, DrawParticles 99.98 -> 71.14, DrawString
    //     96.75 -> 84.81, ModalDraw 89.78 -> 66.19; matched_functions -1,
    //     matched_code -220 B;
    //   * `#pragma inline_depth(0)` around this function takes it to 39.65 --
    //     it also un-inlines DxRnd::Device() and the D3D setters.
    // All 29 residual rows have this one cause; the two `lwa 0x40/0x44` order
    // rows and the 0x68/0x6c store swap are downstream scheduling, not a
    // wrong field (both sides put mWidth in .w and mHeight in .h).
    D3DDevice_Clear(mD3DDevice, 0, nullptr, 0x31, MakeColor(Hmx::Color(0, 0, 0.3f)), 0, 0, 0);
    Hmx::Rect rect(0, 0, (float)mWidth, (float)mHeight);
    RndMat *mat = TheShaderMgr.GetPostProcMat();
    mat->SetBlend((BaseMaterial::Blend)1);
    mat->SetZMode((ZMode)0);
    TheShaderMgr.unk30 = 0;
    DrawRect(rect, mat, (ShaderType)0x10, Hmx::Color(), nullptr, nullptr);
    SavePostBuffer();
}

void DxRnd::CopyPostProcess() {
    if (mRegAlloc != 2) {
        mRegAlloc = (RegisterAlloc)2;
        D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0x10, 0x70);
    }
    Hmx::Rect rect(0, 0, (float)mWidth, (float)mHeight);
    RndMat *mat = TheShaderMgr.GetPostProcMat();
    mat->SetBlend((BaseMaterial::Blend)1);
    mat->SetZMode((ZMode)0);
    TheShaderMgr.unk30 = 1;
    // Starts TRUE in the shipped image (.data holds 01), so this block runs on
    // every CopyPostProcess -- the inner store is redundant, not a one-shot latch.
    // Dropping the initializer left it in .bss reading 0, which made the block dead.
    static bool sCopyPostInited = true;
    if (sCopyPostInited) {
        sCopyPostInited = true;
        D3DDevice_SetSamplerState_MinFilter(TheDxRnd.Device(), 0xE, 1);
        D3DDevice_SetSamplerState_MagFilter(TheDxRnd.Device(), 0xE, 1);
    }
    DrawRect(rect, mat, (ShaderType)0x10, Hmx::Color(), nullptr, nullptr);
    if (mRegAlloc != 1) {
        mRegAlloc = (RegisterAlloc)1;
        D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, mDefaultVSRegAlloc, mDefaultPSRegAlloc);
    }
}

void DxRnd::PerfCountersInit() {
    if (!mCreatedPerfCounters) {
        mCreatedPerfCounters = true;
        mPerfCounterStart = D3DDevice_CreatePerfCounters(mD3DDevice, 1);
        DX_ASSERT(mPerfCounterStart, 0x230);
        mPerfCounterEnd = D3DDevice_CreatePerfCounters(mD3DDevice, 1);
        DX_ASSERT(mPerfCounterEnd, 0x231);
        D3DPERFCOUNTER_EVENTS perfEvents;
        memset(&perfEvents, 0, sizeof(D3DPERFCOUNTER_EVENTS));
        perfEvents.RBBM[0] = GPUPE_RBBM_NRT_BUSY;
        perfEvents.CP[0] = GPUPE_CP_COUNT;
        perfEvents.RBBM[1] = GPUPE_RBBM_COUNT;
        D3DDevice_EnablePerfCounters(mD3DDevice, true);
        D3DDevice_SetPerfCounterEvents(mD3DDevice, &perfEvents, 0);
        mGPUTimer = AutoTimer::GetTimer("gs");
    }
}

void DxRnd::PerfCountersStart() {
    MILO_ASSERT(mGSTiming == true, 0x249);
    MILO_ASSERT(mCreatedPerfCounters == true, 0x24A);
    MILO_ASSERT(mGPUTimer != NULL, 0x24B);
    MILO_ASSERT(mPerfCounterStart != NULL, 0x24C);
    MILO_ASSERT(mPerfCounterEnd != NULL, 0x24D);
    mGPUTimer->SetLastMs(mGPUBusyMs * 1.075f);
    D3DDevice_QueryPerfCounters(mD3DDevice, mPerfCounterStart, 1);
}

void DxRnd::PerfCountersStop() {
    MILO_ASSERT(mGSTiming == true, 0x25D);
    MILO_ASSERT(mCreatedPerfCounters == true, 0x25E);
    MILO_ASSERT(mGPUTimer != NULL, 0x25F);
    MILO_ASSERT(mPerfCounterStart != NULL, 0x260);
    MILO_ASSERT(mPerfCounterEnd != NULL, 0x261);
    D3DDevice_QueryPerfCounters(mD3DDevice, mPerfCounterEnd, 1);
    D3DPERFCOUNTER_VALUES startValues;
    HRESULT code = D3DPerfCounters_GetValues(mPerfCounterStart, &startValues, 0, nullptr);
    DX_ASSERT_CODE(code, 0x269);
    D3DPERFCOUNTER_VALUES endValues;
    code = D3DPerfCounters_GetValues(mPerfCounterEnd, &endValues, 0, nullptr);
    DX_ASSERT_CODE(code, 0x26A);
    auto *startLargeIntegers = (ULARGE_INTEGER *)&startValues;
    auto *endLargeIntegers = (ULARGE_INTEGER *)&endValues;
    for (size_t i = 0; i < sizeof(D3DPERFCOUNTER_VALUES) / sizeof(ULARGE_INTEGER); i++) {
        endLargeIntegers[i].QuadPart -= startLargeIntegers[i].QuadPart;
    }
    mGPUBusyMs = endLargeIntegers[1].QuadPart * 2e-06f;
    mGPUCountMs = endLargeIntegers[2].QuadPart * 2e-06f;
}

void DxRnd::EndTiling(D3DBaseTexture *tex, int flags) {
    int tileMode = 0;
    if (tex && flags) {
        tileMode = (flags & 0x3F) << 0x1A;
    }
    if (mTilingActive) {
        MILO_ASSERT(mNumTiles > 0, 0x480);
        HRESULT hr = D3DDevice_EndTiling(mD3DDevice, tileMode, nullptr, tex, nullptr, 0, 0, nullptr);
        DX_ASSERT_CODE(hr, 0x481);
        mTilingActive = false;
    } else {
        MILO_ASSERT(mNumTiles == 0, 0x486);
        D3DDevice_Resolve(mD3DDevice, tileMode, nullptr, tex, nullptr, 0, 0, nullptr, 0, 0, nullptr);
    }
}

void CreateBackBuffers(
    int width,
    int height,
    D3DMULTISAMPLE_TYPE multisample,
    unsigned int &edramBase,
    unsigned int &edramHzBase,
    D3DSurface *&colorSurface,
    D3DSurface *&depthSurface
) {
    UINT colorSize = XGSurfaceSize(width, height, D3DFMT_A8R8G8B8, multisample);
    UINT depthSize = XGSurfaceSize(width, height, D3DFMT_D24FS8, multisample);

    unsigned int adjustedWidth = width;
    unsigned int adjustedHeight = height;
    if ((int)multisample >= 1) {
        adjustedHeight *= 2;
    }
    if ((int)multisample == 2) {
        adjustedWidth *= 2;
    }

    edramBase = 0x800;
    edramHzBase = 0xE10;

    D3DSURFACE_PARAMETERS params;
    memset(&params, 0, sizeof(params));

    edramBase -= depthSize;
    params.Base = edramBase;

    edramHzBase -= (((adjustedWidth + 0x1F) >> 5) * ((adjustedHeight + 0xF) >> 4)) & 0x7FFFFF;
    params.HierarchicalZBase = edramHzBase;

    depthSurface = D3DDevice_CreateSurface(width, height, D3DFMT_D24FS8, multisample, &params);
    DX_ASSERT(depthSurface, 0x2CE);

    edramBase -= colorSize;

    params.Base = edramBase;
    params.HierarchicalZBase = -1;
    colorSurface = D3DDevice_CreateSurface(width, height, D3DFMT_A8R8G8B8, multisample, &params);
    DX_ASSERT(colorSurface, 0x2D4);
}

void DxRnd::SavePreBuffer() {
    XMVECTOR vector;
    Hmx::Color c = mClearColor;
    vector.x = c.red;
    vector.y = c.green;
    vector.z = c.blue;
    D3DDevice_Resolve(
        mD3DDevice, 0x14, nullptr, mFrontBufferDepth, nullptr, 0, 0, nullptr, 1, 0, nullptr
    );

    D3DDevice_Resolve(
        mD3DDevice, 0x300, nullptr, mPreProcessBuffer, nullptr, 0, 0, &vector, 0, 0, nullptr
    );

    vector.w = 0.f;
}

void DxRnd::SavePostBuffer() {
    D3DDevice_Resolve(
        mD3DDevice, 0, nullptr, mPostProcessBuffer, nullptr, 0, 0, nullptr, 0, 0, nullptr
    );
}

void DxRnd::SetShaderRegisterAlloc(RegisterAlloc s) {
    MILO_ASSERT(s >=0 && s < kNumRegAlloc, 0x6BA);
    if (mRegAlloc != s) {
        mRegAlloc = s;
        switch (s) {
        case 0:
            D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0, 0);
            break;
        case 1:
            D3DDevice_SetShaderGPRAllocation(
                mD3DDevice, 0, mDefaultVSRegAlloc, mDefaultPSRegAlloc
            );
            break;
        case 2:
            D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0x10, 0x70);
            break;
        case 3:
            D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0x10, 0x70);
            break;
        default:
            MILO_NOTIFY("Invalid Shader Register Allocation");
            break;
        }
    }
}

RndTex *DxRnd::GetCurrentFrameTex(bool resolvePreProcess) {
    if (!mPostProcDone) {
        if (resolvePreProcess) {
            D3DDevice_Resolve(
                mD3DDevice, 0, nullptr, mPreProcessBuffer, nullptr, 0, 0, nullptr, 0, 0, nullptr
            );
        }
        return PreProcessTexture();
    }
    return PostProcessTexture();
}

// Debug text: each glyph is a list of polylines held in the `font` DataArray,
// indexed by character code, each point a pair of floats scaled to a 9x12 cell
// on a 13.5 x 18 pixel grid.  Returns a reference to a shared cursor holding
// the end of the string, which DrawStringScreen scales back to 0..1.
Vector2 &DxRnd::DrawString(
    const char *s, const Vector2 &pos, const Hmx::Color &color, bool drawGlyphs
) {
    MILO_ASSERT(s, 0x11F);
    D3DDevice_SetFVF(mD3DDevice, 0x42);
    Transform screenXfm;
    screenXfm.Reset();
    TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, Hmx::Matrix4(screenXfm));
    TheShaderMgr.SetTransform(screenXfm);
    RndShader::SelectConfig(nullptr, kLineNozShader, false);
    D3DDevice_SetRenderState_ViewportEnable(TheDxRnd.Device(), 0);
    static Vector2 cursor;
    cursor = pos;
    float widest = pos.x;
    while (*s) {
        char c = *s;
        if (c == '\n') {
            s++;
            if (*s) {
                widest = Max(widest, cursor.x);
                cursor.y += 18.0f;
                cursor.x = pos.x;
            }
            continue;
        }
        if (drawGlyphs && c > 0 && c + 1 < Font()->Size()) {
            DataArray *glyph = Font()->Node(c + 1).UncheckedArray();
            for (int i = 0; i < glyph->Size(); i++) {
                DataArray *stroke = glyph->Node(i).UncheckedArray();
                struct StrokeVert {
                    float x, y, z;
                    unsigned long color;
                } verts[12];
                int numVerts = 0;
                for (int j = 0; j < stroke->Size(); j += 2) {
                    verts[numVerts].x = stroke->Float(j) * 9.0f + cursor.x;
                    verts[numVerts].y = stroke->Float(j + 1) * 12.0f + cursor.y;
                    verts[numVerts].z = 1.0f;
                    verts[numVerts].color = MakeColor(color);
                    numVerts++;
                }
                D3DDevice_DrawVerticesUP(
                    mD3DDevice, D3DPT_LINESTRIP, numVerts, verts, 0x10
                );
            }
        }
        cursor.x += 13.5f;
        s++;
    }
    D3DDevice_SetRenderState_ViewportEnable(TheDxRnd.Device(), 1);
    if (RndCam::Current()) {
        TheShaderMgr.SetVConstant(
            kVS_ViewProjMatrix, RndCam::Current()->GetViewProjMatrix()
        );
    }
    cursor.y += 18.0f;
    cursor.x = Max(cursor.x, widest);
    return cursor;
}

bool DxRnd::CanModal(Debug::ModalType t) {
    if (mTilingActive) {
        if (t == Debug::kModalFail) {
            EndTiling(FrontBuffer(), 0);
        } else {
            return false;
        }
    }
    return true;
}

void DxRnd::ModalDraw(Debug::ModalType t, const char *cc) {
    bool wasSuspended = mSuspended;
    Resume();
    D3DSurface *savedRenderTarget = D3DDevice_GetRenderTarget(mD3DDevice, 0);
    D3DSurface *savedStencilSurface = D3DDevice_GetDepthStencilSurface(mD3DDevice);
    D3DDevice_SetRenderTarget_External(mD3DDevice, 0, mBackBuffer);
    D3DDevice_SetDepthStencilSurface(mD3DDevice, 0);
    // BUG FIX (w7-bl).  0x82618DAC-0x82618DB4 packs the clear colour as
    // A=0xff (a CONSTANT -- `lis r8, 0xffff`, so alpha is a compile-time
    // 1.0f), R=f13, G=f12, B=f11, and 0x82618D34/0x82618D3C load f12=0.5f
    // and f11=0.1f with f13=0.0f; the kModalFail arm at 0x82618D48-0x82618D50
    // sets f13=0.25f and zeroes f12/f11.  So the image clears to an OPAQUE
    // (0, 0.5, 0.1) green for a normal modal and an OPAQUE (0.25, 0, 0) red
    // for a failure.  We had green/blue swapped, alpha 0 instead of 1, and
    // the failure arm writing 0.25 into ALPHA instead of RED -- our modal
    // cleared to 0x0000197f (fully transparent blue) and our failure screen
    // to 0x3f000000 instead of 0xff3f0000.
    Hmx::Color color(0, 0.5f, 0.1f);
    if (t == Debug::kModalFail) {
        color.red = 0.25f;
        color.green = 0;
        color.blue = 0;
    }
    D3DDevice_Clear(mD3DDevice, 0, nullptr, 0x31, MakeColor(color), 0, 0, 0);
    Rnd::DrawStringScreen(cc, Vector2(0.025f, 0.025f), Hmx::Color(1, 1, 1, 1), true);
    RndOverlay::DrawAll(true);
    D3DDevice_Resolve(
        mD3DDevice, 0, nullptr, FrontBuffer(), nullptr, 0, 0, nullptr, 0, 0, nullptr
    );
    if (mRegAlloc != 0) {
        mRegAlloc = (RegisterAlloc)0;
        D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0, 0);
    }
    // w7-bl RESIDUAL (89.7%): the image keeps the `__real@00000000` PAGE BASE
    // in a callee-saved GPR (r30 at 0x82618D20) and issues two `lfs` -- one
    // for the colour components, one for this Resolve's ClearZ -- where MSVC
    // gives us one `lfs` into a callee-saved f31 that spans D3DDevice_Clear.
    // That is the whole r25..r31 vs r26..r31 renumbering: the image spends a
    // GPR where we spend an FPR.  Allocator choice, no source lever found.
    Present();
    D3DDevice_SetRenderTarget_External(mD3DDevice, 0, savedRenderTarget);
    D3DDevice_SetDepthStencilSurface(mD3DDevice, savedStencilSurface);
    if (wasSuspended) {
        Suspend();
    }
}

void DxRnd::InitBuffers() {
    PhysMemTypeTracker tracker("D3D(phys):DxRndBuffer");
    memset(&mPresentParams, 0, sizeof(D3DPRESENT_PARAMETERS));
    memset(&mVideoMode, 0, sizeof(XVIDEO_MODE));
    XGetVideoMode(&mVideoMode);
    static Symbol rnd("rnd");
    static Symbol low_res("low_res");
    static Symbol force_hd("force_hd");
    if (SystemConfig(rnd)->FindInt(force_hd) != 0) {
        mVideoMode.fIsHiDef = true;
        mVideoMode.fIsWideScreen = true;
    } else if (SystemConfig(rnd)->FindInt(low_res) != 0) {
        mFlags |= 1;
    }
    // 0x82619060: the bool at 0x1f8 is loaded from mVideoMode.fIsWideScreen
    // (0x328), NOT from mFlags (0x37c), and mAspect is kWidescreen/kLetterbox
    // (`addi r11, r11, 0x2`), not kWidescreen/kRegular.  mHeight keys off the
    // low_res bit in mFlags directly (the `clrlwi.` at 0x8261906C feeds the
    // `beq` at 0x82619090).
    mLowRes = mVideoMode.fIsWideScreen != 0;
    mAspect = mLowRes ? kWidescreen : kLetterbox;
    unsigned int lowResFlag = mFlags & 1;
    if (!lowResFlag) {
        mHeight = 720;
    } else {
        mHeight = 540;
    }
    int tileHeight = mHeight;
    int tileWidth;
    int width;
    if (mVideoMode.fIsHiDef != 0 || mLowRes != 0) {
        width = (mHeight << 4) / 9;
        tileWidth = (tileHeight << 4) / 9;
    } else {
        width = (mHeight << 2) / 3;
        tileWidth = (tileHeight << 2) / 3;
    }
    mWidth = width;
    if (!lowResFlag) {
        mNumTiles = 2;
        // 0x826190EC-0x8261916C: two tile rects covering the frame.  Bit 1 of
        // mFlags picks a horizontal split line (stacked tiles, full width,
        // half height) over the default vertical one (side-by-side tiles,
        // half width, full height).  The loop re-reads mNumTiles from the
        // member every iteration (`lwz r8, 0x3b0(r30)`).
        // w7-bl RESIDUAL (95.2%): the image computes `offset + tile` TWICE in
        // each of the two rect loops (0x82619110 `add r8, r10, r23` and
        // 0x82619124 `add r10, r10, r23`), where MSVC CSEs ours into one add
        // (loop 1) or an add plus `mr` (loop 2).  The rest is a uniform
        // renumbering of the callee-saved set: the image gets by with
        // __savegprlr_19 and we need _18, i.e. one more simultaneously-live
        // value, which is what shifts r25->r22, r27->r24, r24->r25, r26->r27.
        int i = 0;
        int offset = 0;
        if (mFlags & 2) {
            tileHeight = tileHeight / 2;
            for (; i < mNumTiles; i++) {
                mTileRects[i].x1 = 0;
                mTileRects[i].y1 = offset;
                mTileRects[i].x2 = tileWidth;
                mTileRects[i].y2 = offset + tileHeight;
                offset += tileHeight;
            }
        } else {
            tileWidth = tileWidth / 2;
            for (; i < mNumTiles; i++) {
                mTileRects[i].x1 = offset;
                mTileRects[i].y1 = 0;
                mTileRects[i].x2 = offset + tileWidth;
                mTileRects[i].y2 = tileHeight;
                offset += tileWidth;
            }
        }
    }
    mPresentParams.Windowed = 0;
    mPresentParams.DisableAutoBackBuffer = 1;
    mPresentParams.DisableAutoFrontBuffer = 1;
    mPresentParams.BackBufferWidth = mWidth;
    mPresentParams.BackBufferHeight = mHeight;
    mPresentParams.PresentationInterval = 0;
    mPresentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    mPresentParams.RingBufferParameters.SecondarySize = 0x600000;
    mPresentParams.RingBufferParameters.SegmentCount = 12;
    D3DVIDEO_SCALER_PARAMETERS &scaler = mPresentParams.VideoScalerParameters;
    scaler.ScalerSourceRect.x1 = 0;
    scaler.ScalerSourceRect.y1 = 0;
    scaler.ScalerSourceRect.x2 = mWidth;
    scaler.ScalerSourceRect.y2 = mHeight;
    scaler.FilterProfile = 0;
    UpdateScalerParams();
    mRenderThreadId = GetCurrentThreadId();
    {
        BeginMemTrackObjectName("D3D->CreateDevice");
        HRESULT hr = Direct3D_CreateDevice(
            0, mDeviceType, mFocusWindow, 1, &mPresentParams, &mD3DDevice
        );
        DX_ASSERT_CODE(hr, 0x367);
        EndMemTrackObjectName();
    }
    if (!(mFlags & 1)) {
        MILO_ASSERT(mNumTiles > 0, 0x36D);
        BeginMemTrackObjectName("CreateBackBuffers:World");
        CreateBackBuffers(
            mWidth, mHeight, D3DMULTISAMPLE_NONE, mEdramBase, mEdramHzBase, mBackBuffer, mWorldDepth
        );
        EndMemTrackObjectName();
        BeginMemTrackObjectName("CreateBackBuffers:UI");
        CreateBackBuffers(
            tileWidth, tileHeight, D3DMULTISAMPLE_2_SAMPLES, mEdramBase, mEdramHzBase, mOffscreenRT, mOffscreenDepth
        );
    } else {
        MILO_ASSERT(mNumTiles == 0, 0x37E);
        BeginMemTrackObjectName("CreateBackBuffers:World");
        CreateBackBuffers(
            mWidth, mHeight, D3DMULTISAMPLE_2_SAMPLES, mEdramBase, mEdramHzBase, mBackBuffer, mWorldDepth
        );
        EndMemTrackObjectName();
        BeginMemTrackObjectName("CreateBackBuffers:UI");
        CreateBackBuffers(
            mWidth, mHeight, D3DMULTISAMPLE_2_SAMPLES, mEdramBase, mEdramHzBase, mOffscreenRT, mOffscreenDepth
        );
    }
    EndMemTrackObjectName();
    {
        BeginMemTrackObjectName("CreateTexture:PreProcessBuffer");
        mPreProcessBuffer = static_cast<D3DTexture *>(D3DDevice_CreateTexture(
            mWidth, mHeight, 1, 1, 0, D3DFMT_A8R8G8B8, 0, D3DRTYPE_TEXTURE
        ));
        DX_ASSERT(mPreProcessBuffer, 0x390);
        EndMemTrackObjectName();
    }
    {
        BeginMemTrackObjectName("CreateTexture:PostProcessBuffer");
        mPostProcessBuffer = static_cast<D3DTexture *>(D3DDevice_CreateTexture(
            mWidth, mHeight, 1, 1, 0, D3DFMT_A8R8G8B8, 0, D3DRTYPE_TEXTURE
        ));
        DX_ASSERT(mPostProcessBuffer, 0x394);
        EndMemTrackObjectName();
    }
    for (int i = 0; i < 2; i++) {
        BeginMemTrackObjectName("CreateTexture:FrontBuffer");
        mFrontBuffers[i] = static_cast<D3DTexture *>(D3DDevice_CreateTexture(
            mWidth, mHeight, 1, 1, 0, D3DFMT_A8R8G8B8, 0, D3DRTYPE_TEXTURE
        ));
        DX_ASSERT(mFrontBuffers[i], 0x39C);
        EndMemTrackObjectName();
    }

    BeginMemTrackObjectName("CreateTexture:FrontBufferDepth");
    mFrontBufferDepth = static_cast<D3DTexture *>(D3DDevice_CreateTexture(
        mWidth, mHeight, 1, 1, 0, D3DFMT_D24FS8, 0, D3DRTYPE_TEXTURE
    ));
    DX_ASSERT(mFrontBufferDepth, 0x3A2);
    EndMemTrackObjectName();
    PostDeviceReset();
    // 0x8261963C-0x82619658: srawi+addze on BOTH terms -- a signed divide by
    // 32, not an arithmetic shift.  With `>> 5` MSVC fuses one of them into a
    // single `extlwi` and the addze pair disappears.
    int temp27 = ((((mHeight + 0x1F) / 32) * ((mWidth + 0x1F) / 32)) << 0xC);
    // w7-bl: `rect` is hoisted OUT of the loop on purpose.  Scoped inside the
    // body it shares r1+0x58 with the MakeString scratch slot; the image keeps
    // the two apart (Symbol temp at 0x5c, D3DLOCKED_RECT at 0x60), and hoisting
    // reproduces that (-3 rows).  Swapping the mullw operands above to match
    // the image's mHeight/mWidth LOAD order is inert (measured, 0 rows).
    D3DLOCKED_RECT rect;
    for (int i = 0; i < 2; i++) {
        D3DTexture_LockRect(mFrontBuffers[i], 0, &rect, nullptr, 0);
        memset(rect.pBits, 0, temp27);
        D3DTexture_UnlockRect(mFrontBuffers[i], 0);
    }
    mRegAlloc = (RegisterAlloc)0;
    D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0, 0);
    Present();
    SetSync(mSync);
}

void DxRnd::CreatePostTextures() {
    RELEASE(mPreProcessTex);
    mPreProcessTex = Hmx::Object::New<DxTex>();
    mPreProcessTex->SetDeviceTex(mPreProcessBuffer);
    RELEASE(mPreDepthTex);
    mPreDepthTex = Hmx::Object::New<DxTex>();
    mPreDepthTex->SetDeviceTex(mFrontBufferDepth);
    RELEASE(mPostProcessTex);
    mPostProcessTex = Hmx::Object::New<DxTex>();
    mPostProcessTex->SetDeviceTex(mPostProcessBuffer);
}

void DxRnd::EndDrawing() {
    EndWorld();
    if (mShowSafeArea) {
        Hmx::Color titleSafeColor(1.0f, 0.0f, 0.0f, 1.0f);
        Hmx::Color actionSafeColor(0.0f, 1.0f, 0.0f, 1.0f);
        if (mAspect == kWidescreen)
            DrawSafeArea(0.9f, true, titleSafeColor);
        DrawSafeArea(0.9f, false, titleSafeColor);
        if (mAspect == kWidescreen)
            DrawSafeArea(0.95f, true, actionSafeColor);
        DrawSafeArea(0.95f, false, actionSafeColor);
    }
    Rnd::EndDrawing();
    mPostProcDone = false;
    EndTiling(FrontBuffer(), 0);
    D3DDevice_SetRenderTarget_External(mD3DDevice, 0, mBackBuffer);
    // 2/172 instructions from byte-identity: the target schedules the
    // mD3DDevice load (r3) ahead of the mWorldDepth load (r4) here and we
    // schedule them the other way round.  Neither a Device() accessor nor a
    // named D3DDevice* local moves it.
    D3DDevice_SetDepthStencilSurface(mD3DDevice, mWorldDepth);
    if (mRegAlloc != 0) {
        mRegAlloc = (RegisterAlloc)0;
        D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, 0, 0);
    }
    {
        static Timer *drawStop = AutoTimer::GetTimer("draw");
        if (drawStop)
            drawStop->Stop();
    }
    if (mGSTiming) {
        {
            static Timer *cpuStop = AutoTimer::GetTimer("cpu");
            if (cpuStop)
                cpuStop->Stop();
        }
        PerfCountersStop();
        {
            static Timer *cpuStart = AutoTimer::GetTimer("cpu");
            if (cpuStart)
                cpuStart->Start();
        }
    }
}

// Retires everything AutoRelease()/AutoDelete() queued while mReleaseImmediate
// was false.  A resource that is still bound to the device cannot be freed yet,
// so it is carried over into the next frame's pending list.
//
// The two shift expressions are the XDK's D3DTAG "pending mask" arithmetic with
// a *runtime* index: for a literal stream/sampler MSVC folds it to a constant
// (see the 0x80000000 / 0x8000 / 1 literals elsewhere in this file), but inside
// these loops the whole expansion survives into the code.
void DxRnd::ReleaseAutoRelease() {
    D3DDevice_SetVertexShader(mD3DDevice, nullptr);
    D3DDevice_SetPixelShader(mD3DDevice, nullptr);
    D3DDevice_SetIndices(mD3DDevice, nullptr);
    for (int sampler = 0; sampler < 16; sampler++) {
        D3DDevice_SetTexture(
            mD3DDevice, sampler, nullptr, 0x8000000000000000 >> (sampler + 0x20U)
        );
    }
    for (int stream = 0; stream < 4; stream++) {
        D3DDevice_SetStreamSource(
            mD3DDevice,
            stream,
            nullptr,
            0,
            0,
            0x8000000000000000 >> (((0x5FU - stream) * 0x5556U >> 16) + 0x20U)
        );
    }

    std::vector<D3DResource *> stillBoundResources;
    for (std::vector<D3DResource *>::iterator it = mPendingReleases.begin();
         it != mPendingReleases.end();
         ++it) {
        if (D3DResource_IsSet(*it, mD3DDevice)) {
            stillBoundResources.push_back(*it);
        } else if (*it) {
            (*it)->Release();
            *it = nullptr;
        }
    }
    mPendingReleases.clear();
    mPendingReleases.swap(stillBoundResources);

    std::vector<D3DBaseTexture *> stillBoundTextures;
    for (std::vector<D3DBaseTexture *>::iterator it = mPendingDeletes.begin();
         it != mPendingDeletes.end();
         ++it) {
        D3DBaseTexture *tex = *it;
        if (tex) {
            if (D3DResource_IsSet(tex, mD3DDevice)) {
                stillBoundTextures.push_back(tex);
            } else {
                UINT data;
                XGGetTextureLayout(
                    tex,
                    &data,
                    nullptr,
                    nullptr,
                    nullptr,
                    0,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    0
                );
                PhysicalFreeTracked((void *)data, __FILE__, 0x452, "");
                delete tex;
            }
        }
    }
    mPendingDeletes.clear();
    mPendingDeletes.swap(stillBoundTextures);
}

void DxRnd::BeginDrawing() {
    {
        static Timer *cpuStop = AutoTimer::GetTimer("cpu");
        if (cpuStop)
            cpuStop->Stop();
    }
    static Timer *cpuTimer = AutoTimer::GetTimer("cpu");
    if (mSuspended) {
        Resume();
    } else if (mPrintGlitches && cpuTimer->Ms() > 66.0f) {
        MILO_LOG("GLITCH: %i ms\n", (int)cpuTimer->Ms());
    }
    if (mCaptureNextFrame) {
        PIXCaptureGpuFrame("capture.pix2");
        mCaptureNextFrame = false;
    }
    Present();
    if (MainThread())
        ReleaseAutoRelease();
    Rnd::BeginDrawing();
    if (mGSTiming) {
        PerfCountersInit();
        PerfCountersStart();
    }
    DrawPreClear();
    Hmx::Color clearColor = mClearColor;
    D3DDevice_Clear(
        mD3DDevice,
        0,
        nullptr,
        0x31,
        ((unsigned long)(clearColor.red * 255.0f) & 0xFF) << 16
            | ((unsigned long)(clearColor.green * 255.0f) & 0xFF) << 8
            | ((unsigned long)(clearColor.blue * 255.0f) & 0xFF),
        0,
        0,
        0
    );
    // 99.98771, 3 rows, and they are one rotation of the three argument loads
    // for the D3DDevice_SetShaderGPRAllocation that this call inlines. Target
    // [182]/[184]/[186] = mD3DDevice(0x224)->r3, mDefaultPSRegAlloc(0x400)->r6,
    // mDefaultVSRegAlloc(0x3fc)->r5; ours is r6, r5, r3 -- MSVC's plain
    // right-to-left argument order, with the target hoisting the device load
    // above the other two. Same three registers, same call, same values: only
    // the schedule differs, and the rest of the inlined block (the cmpwi, the
    // beq, the li 1 and the stw to mRegAlloc at 0x3f8) is equal.
    // Measured INERT (w7-m): spelling the block out here the way DoPostProcess
    // and CopyPostProcess already do --
    //   if (mRegAlloc != 1) { mRegAlloc = (RegisterAlloc)1;
    //     D3DDevice_SetShaderGPRAllocation(mD3DDevice, 0, mDefaultVSRegAlloc,
    //                                      mDefaultPSRegAlloc); }
    // -- gives a byte-identical object, same three rows. The inlined call and
    // the hand-written block are indistinguishable, so the call stays (it also
    // keeps SetShaderRegisterAlloc, which is already 100%, untouched).
    SetShaderRegisterAlloc((RegisterAlloc)1);
    ResetStats();
    {
        static Timer *cpuStart = AutoTimer::GetTimer("cpu");
        if (cpuStart)
            cpuStart->Start();
    }
    {
        static Timer *drawStart = AutoTimer::GetTimer("draw");
        if (drawStart)
            drawStart->Start();
    }
    NgMat::SetCurrent(nullptr);
}

static DWORD sPointTestFence = -1;

void DxRnd::DoPointTests() {
    // Block on previous fence if set
    if (sPointTestFence != (DWORD)-1) {
        D3DDevice_BlockOnFence(sPointTestFence);
        sPointTestFence = -1;
    }

    // Early out if no occlusion query manager or hi-res screen is active
    if (!mOcclusionQueryMgr)
        return;
    if (TheHiResScreen.IsActive())
        return;

    // Process query results from previous frame
    for (std::vector<RndPointTest>::iterator it = mPointTestQueries.begin(); it !=mPointTestQueries.end(); ++it) {
        unsigned int result;
        // 0x8261AFD8-0x8261AFE8 and 0x8261B008-0x8261B020 each load
        // `it->mFlare` ONCE and use it for both stores; writing
        // `it->mFlare->` twice makes MSVC reload it, because the store to
        // 0x144/0x102 may alias the pointer.
        if (mOcclusionQueryMgr->GetQueryResults(it->mPointQueryIdx, result)) {
            // 0x8261AFD0: the result is loaded and bool-ified BEFORE the flare
            // pointer is loaded, so the visibility value is a local of its own.
            bool visible = result != 0;
            RndFlare *flare = it->mFlare;
            flare->SetOcclusionReady(true);
            flare->SetVisible(visible);
        }
        if (mOcclusionQueryMgr->GetQueryResults(it->mAreaQueryIdx, result)) {
            RndFlare *flare = it->mFlare;
            flare->SetOcclusionResult((float)(int)result);
            flare->SetOcclusionReady(true);
        }
    }

    // Update frame index - both direct manipulation and virtual call.
    // BUG FIX (w7-bl): the two virtual calls were the wrong way round.
    // 0x8261B008 calls vtable slot 0x20 (OnEndFrame) right after the 0x1804
    // toggle, and 0x8261B018 calls slot 0x1c (OnBeginFrame) after the 0x1808
    // increment -- we emitted 0x1c then 0x20.  Semantically the image retires
    // the previous frame's queries, bumps the counter, then opens the new
    // frame; we were opening the new frame before retiring the old one.
    mOcclusionQueryMgr->ToggleFrameIndex();
    mOcclusionQueryMgr->OnEndFrame();
    mOcclusionQueryMgr->IncrementFrameCounter();
    mOcclusionQueryMgr->OnBeginFrame();

    // Count point tests needed
    int numTests = 0;
    for (std::list<PointTest>::iterator it = mPointTests.begin(); it !=mPointTests.end(); ++it) {
        numTests++;
    }

    // Resize mPointTestQueries to match mPointTests count
    mPointTestQueries.resize(numTests);

    // Early out if no point tests
    if (mPointTests.empty())
        return;

    // The image's frame is 0x40 larger than a naive one because the unnamed
    // Hmx::Matrix4 temporary below does NOT share a slot with the vertex
    // buffers -- it sits at r1+0x130, right above `xfm`, while the vertex
    // scratch stays at r1+0x70/0x90.  Declaring the vertex locals here keeps
    // them live across the SetVConstant call so the slots cannot be merged.
    struct PointVertex {
        float x, y, z;
        float w;
        DWORD color;
    };
    struct QuadVertex {
        float x, y, z;
        float w;
        DWORD color;
    };
    PointVertex vtx;
    QuadVertex verts[4];

    // Setup identity transform
    Transform xfm;
    xfm.Reset();
    TheShaderMgr.SetTransform(xfm);

    // Setup view matrix.  0x8261B160-0x8261B16C passes the Matrix4
    // CONSTRUCTOR'S return value (`mr r5, r3`) straight to SetVConstant -- an
    // unnamed temporary, not a named local whose address is re-taken.
    TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, Hmx::Matrix4(xfm));

    // Setup shader state
    RndShader::SelectConfig(nullptr, kStandardShader, false);
    D3DDevice_SetPixelShader(mD3DDevice, nullptr);
    D3DDevice_SetFVF(mD3DDevice, 0x4042);

    // Disable color writes and blending for occlusion testing
    D3DDevice_SetRenderState_ColorWriteEnable(TheDxRnd.Device(), 0);
    D3DDevice_SetRenderState_AlphaBlendEnable(TheDxRnd.Device(), 0);
    D3DDevice_SetRenderState_AlphaTestEnable(TheDxRnd.Device(), 0);
    D3DDevice_SetRenderState_ZWriteEnable(TheDxRnd.Device(), 0);
    D3DDevice_SetRenderState_ZEnable(TheDxRnd.Device(), 1);

    // Set z-compare function based on mReverseZ.  0x8261B1A4-0x8261B1B4
    // bool-ifies mReverseZ, masks with 3 (`clrlwi r11, r11, 30`) and adds 1 --
    // so the two values are 4 and 1.  On the Xbox 360 the D3DCMPFUNC enum is
    // shifted down by one from desktop D3D9, making those D3DCMP_GREATER and
    // D3DCMP_LESS.  We had 3 (D3DCMP_LESSEQUAL) on the reverse-Z arm, which
    // masks with 2 (`rlwinm r11, r11, 0, 30, 30`) instead.
    D3DDevice_SetRenderState_ZFunc(
        TheDxRnd.Device(), (D3DCMPFUNC)(mReverseZ ? D3DCMP_GREATER : D3DCMP_LESS)
    );

    // Set point size
    float pointSize = 1.0f;
    D3DDevice_SetRenderState_PointSize(TheDxRnd.Device(), *(DWORD*)&pointSize);
    D3DDevice_SetRenderState_ViewportEnable(TheDxRnd.Device(), 0);
    D3DDevice_SetRenderState_HalfPixelOffset(TheDxRnd.Device(), 1);

    // Process each point test
    int idx = 0;
    for (std::list<PointTest>::iterator it = mPointTests.begin(); it !=mPointTests.end(); ++it, ++idx) {
        TheNgStats->mFlares++;

        RndFlare *flare = it->mFlare;
        RndPointTest &test = mPointTestQueries[idx];
        // 0x8261B268-0x8261B284: mFlare is stored FIRST, then mAreaQueryIdx
        // (0x8) and only then mPointQueryIdx (0x4).
        test.mFlare = flare;
        test.mAreaQueryIdx = -1;
        test.mPointQueryIdx = -1;

        // Point test
        if (flare->GetPointTest()) {
            // NEGATIVE RESULT (w7-bl, byte-identical): the image stores w and
            // color between the z load and the z conversion, but writing the
            // fields in that order (x, y, w, color, z) changes not one
            // instruction -- MSVC schedules stores to a local struct freely.
            vtx.x = (float)it->x;
            vtx.y = (float)it->y;
            vtx.z = (float)it->z * 5.9604651881e-08f;
            vtx.w = 1.0f;
            vtx.color = 0;

            // 0x8261B2D8-0x8261B32C: CreateQuery is handed the MEMBER by
            // reference (`addi r27, r30, 0x4` / `mr r4, r27`), not a local
            // temp, and its result gates TWO separate `if`s -- BeginQuery
            // under the first, DrawVerticesUP+EndQuery under the second, each
            // reloading the index from 0x0(r27).
            RndOcclusionQueryMgr *mgr = mOcclusionQueryMgr;
            bool ok = mgr->CreateQuery(test.mPointQueryIdx);
            if (ok) {
                mgr->BeginQuery(test.mPointQueryIdx);
            }
            if (ok) {
                D3DDevice_DrawVerticesUP(mD3DDevice, D3DPT_POINTLIST, 1, &vtx, sizeof(PointVertex));
                mOcclusionQueryMgr->EndQuery(test.mPointQueryIdx);
            }
        }

        // Area test.  0x8261B330 reloads the flare from `test.mFlare`
        // (`lwz r11, 0x0(r30)`), not from the `flare` local -- and that ONE
        // load then serves the whole block: the rect base at 0x8261B33C, the
        // three GetArea() reads, and both stores in the else arm.
        RndFlare *areaFlare = test.mFlare;
        if (areaFlare->GetAreaTest()) {
            // 0x8261B33C `addi r10, r11, 0x134`: the rect is held BY
            // REFERENCE, so w/h are read as 0x8(r10)/0xc(r10) rather than
            // 0x13c/0x140 off the flare.
            Hmx::Rect &area = areaFlare->GetArea();
            verts[0].x = area.x;
            verts[0].y = area.y;
            verts[0].z = (float)it->z * 5.9604651881e-08f;
            verts[0].w = 1.0f;
            verts[0].color = 0;

            // 0x8261B388-0x8261B40C: every one of the three copies is
            // `verts[n] = verts[0]` (a 5-word lwzu/stwu loop off r1+0x8c),
            // and the adjusted components are read back from verts[0], not
            // from the copy -- except verts[3].y, which reloads its own slot
            // at 0xd0(r1).  Every `fadds` takes the RECT term first.
            verts[1] = verts[0];
            verts[1].y = area.h + verts[0].y;

            verts[2] = verts[0];
            verts[2].x = area.w + verts[0].x;

            verts[3] = verts[0];
            verts[3].x = area.w + verts[0].x;
            verts[3].y = area.h + verts[3].y;

            // w7-bl RESIDUAL (92.90%): what is left is a flat renumbering of
            // the callee-saved set (`this` is r29 in the image, r30 for us;
            // the iterator, the byte index and the two query-index addresses
            // shift with it), four `fadds` whose operands MSVC canonicalises
            // (writing `verts[0].y + area.h` is byte-identical), and the
            // mFlare store at 0x8261B268, which the image does off the
            // computed `&test` where we emit an indexed `stwx`.
            // 0x8261B430/0x8261B444: the manager is read ONCE into a
            // callee-saved register and reused for BeginQuery (`mr r3, r30`);
            // EndQuery at 0x8261B478 reloads the member.
            RndOcclusionQueryMgr *mgr = mOcclusionQueryMgr;
            bool ok = mgr->CreateQuery(test.mAreaQueryIdx);
            if (ok) {
                mgr->BeginQuery(test.mAreaQueryIdx);
            }
            if (ok) {
                D3DDevice_DrawVerticesUP(mD3DDevice, D3DPT_TRIANGLESTRIP, 4, verts, sizeof(QuadVertex));
                mOcclusionQueryMgr->EndQuery(test.mAreaQueryIdx);
            }
        } else {
            areaFlare->SetOcclusionReady(true);
            areaFlare->SetVisible(true);
        }
    }

    // Insert fence for next frame
    sPointTestFence = D3DDevice_InsertFence(mD3DDevice);

    // Clear current material
    NgMat::SetCurrent(nullptr);

    // Restore render states
    D3DDevice_SetRenderState_ColorWriteEnable(TheDxRnd.Device(), 0xF);
    D3DDevice_SetRenderState_ViewportEnable(TheDxRnd.Device(), 1);
    D3DDevice_SetRenderState_HalfPixelOffset(TheDxRnd.Device(), 0);

    // Restore camera if set
    if (RndCam::Current()) {
        TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, RndCam::Current()->GetViewProjMatrix());
    }
}
