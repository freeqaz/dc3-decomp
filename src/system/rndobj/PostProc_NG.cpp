#include "rndobj/PostProc_NG.h"
#include "Memory.h"
#include "Tex.h"
#include "math/Rand.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/PostProc.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/Tex.h"
#include "rndobj/VelocityBuffer.h"
#include "rndobj/HiResScreen.h"
#include "rndobj/ShaderMgr.h"
#include "utl/Loader.h"

extern void merged_ObjPtrListPopBack(void *);

Hmx::Color NgPostProc::s_prevBloomColor(-1, -1, -1, -1);
float NgPostProc::s_prevBloomIntensity = -1;
NgPostProc::BloomTextures<3> NgPostProc::sBloom;

NgPostProc::BloomTextureSet::BloomTextureSet() {
    mBloomTexture[0] = (RndTex*)0;
    mBloomTexture[1] = (RndTex*)0;
}

NgPostProc::BloomTextureSet::~BloomTextureSet() { FreeTextures(); }

void NgPostProc::BloomTextureSet::AllocateTextures(unsigned int w, unsigned int h) {
    MILO_ASSERT(mBloomTexture[0] == NULL, 0x48);
    mBloomTexture[0] = Hmx::Object::New<RndTex>();
    mBloomTexture[0]->SetBitmap(w, h, TheRnd.Bpp(), RndTex::kRenderedNoZ, false, nullptr);
    mBloomTexture[1] = mBloomTexture[0];
}

void NgPostProc::BloomTextureSet::FreeTextures() { RELEASE(mBloomTexture[0]); }

NgPostProc::NgPostProc()
    : mRandomSeed1(RandomFloat()), mRandomSeed2(RandomFloat()), unk234(0), unk238(0),
      mMotionBlurDrawList(this), mMotionBlurEnabled(1) {}

NgPostProc::~NgPostProc() {}

void NgPostProc::Select() {
    RndPostProc::Select();
    mRandomSeed1 = RandomFloat();
    mRandomSeed2 = RandomFloat();
}

void NgPostProc::Init() {
    REGISTER_OBJ_FACTORY(NgPostProc);
    PhysMemTypeTracker tracker("D3D(phys):NgPostProc");
    RebuildTex();
}

void NgPostProc::RebuildTex() {
    ReleaseTex();
    int w = 0x80;
    int h = 0x80;
    if (TheLoadMgr.GetPlatform() != kPlatformNone) {
        MILO_ASSERT(TheNgRnd.PreProcessTexture(), 0x3AB );
        w = TheNgRnd.PreProcessTexture()->Width();
        h = TheNgRnd.PreProcessTexture()->Height();
    }
    RndVelocityBuffer::Singleton().AllocateData(w, h, TheRnd.Bpp());
    sBloom.AllocateTextures(w * 4, h * 4);
}

#ifdef HX_NATIVE
void NgPostProc::DoVelocity() {
    // Post-processing uses hardcoded ILP32 struct offsets throughout — stub on native
    // Will be reimplemented when the WebGPU renderer is built
}
#else
void NgPostProc::DoVelocity() {
    typedef void (*ShaderFunc)(void*, int, float*);
    *(s8*)((u8*)&TheShaderMgr + 0x39) = 0;
    if ((mMotionBlurVelocity) && (*(u8*)((u8*)&TheHiResScreen + 0x4) == 0) &&
        (RndVelocityBuffer::Singleton().Draw(*(RndCam**)((u8*)&TheRnd + 0xE4), mMotionBlurDrawList) != 0)) {
        *(s8*)((u8*)&TheShaderMgr + 0x39) = 1;
        float sp50 = *(float*)((u8*)&RndVelocityBuffer::Singleton() + 0x36BE8);
        void* shaderMgrVTable = *(void**)&TheShaderMgr;
        ShaderFunc func = *(ShaderFunc*)((u8*)shaderMgrVTable + 0x40);
        func(&TheShaderMgr, 0x7A, &sp50);
    }
    int head = *(int*)((u8*)this + 0x240);
    if (head != 0) {
        void* pList = (u8*)this + 0x23C;
        do {
            merged_ObjPtrListPopBack(pList);
            head = *(int*)((u8*)pList + 4);
        } while (head != 0);
    }
}
#endif

void NgPostProc::SetBloomColor() {
    float diff = 1.0f - mBloomThreshold;
    float divisor = (diff >= 0.0f) ? 1.0f : mBloomThreshold;
    float scale = 1.0f / divisor;
    float invScale = 1.0f / scale;

    Vector4 bloomParams(scale * 0.3f, scale * 0.59f, scale * 0.11f, invScale);
    TheShaderMgr.SetPConstant(kPS_BloomParams, bloomParams);
}

void NgPostProc::QueueMotionBlurObject(RndDrawable *draw) {
    if (!mMotionBlurDrawList.find(draw)) {
        mMotionBlurDrawList.insert(mMotionBlurDrawList.end(), draw);
    }
}

void NgPostProc::CheckBlendPrevious() {
    Vector4 blendParams(mBlendVec.x, mBlendVec.y, mBlendVec.z, 0.0f);
    TheShaderMgr.SetPConstant(kPS_BlendPrevious, blendParams);
}

void NgPostProc::CheckVignette() {
    if (DoVignette()) {
        Vector4 vignetteParams(mVignetteColor.red, mVignetteColor.green, mVignetteColor.blue, mVignetteIntensity);
        TheShaderMgr.SetPConstant(kPS_Vignette, vignetteParams);
        TheShaderMgr.unk3e = true;
    }
}

void NgPostProc::CheckMotionBlur() {
    if (DoMotionBlur()) {
        float blend = mMotionBlurBlend;
        Vector4 motionParams(
            mMotionBlurWeight.red * blend,
            mMotionBlurWeight.green * blend,
            mMotionBlurWeight.blue * blend,
            mMotionBlurWeight.alpha
        );
        TheShaderMgr.SetPConstant(kPS_MotionBlur, motionParams);
        TheShaderMgr.unk38 = true;
    }
}

void NgPostProc::CheckChromaticAberration() {
    if (DoChromaticAberration()) {
        Vector4 chromaParams(
            mChromaticAberrationOffset / (float)TheRnd.Width(),
            mChromaticAberrationOffset / (float)TheRnd.Height(),
            0.0f,
            0.0f
        );
        TheShaderMgr.SetPConstant(kPS_ChromaticAberration, chromaParams);
        if (mChromaticSharpen) {
            TheShaderMgr.unk3d = true;
        } else {
            TheShaderMgr.unk3c = true;
        }
    }
}

void NgPostProc::CheckHallOfTime() {
    if (HallOfTime() && !mMotionBlurEnabled) {
        Vector4 hallParams(mHallOfTimeRate, mHallOfTimeMix, 0.0f, 0.0f);
        TheShaderMgr.SetPConstant(kPS_HallOfTimeParams, hallParams);
        Vector4 hallColor(mHallOfTimeColor.red, mHallOfTimeColor.green, mHallOfTimeColor.blue, 1.0f);
        TheShaderMgr.SetPConstant(kPS_HallOfTimeColor, hallColor);
        TheShaderMgr.unk34 = mHallOfTimeType + 1;
    }
}

void NgPostProc::CheckPosterizeAndKaleidoscope() {
    Vector4 posterParams(0.0f, 0.0f, 0.0f, 0.0f);
    Vector4 kaleidoParams(0.0f, 0.0f, 0.0f, 0.0f);
    if (0.0f < mPosterLevels * mPosterMin) {
        TheShaderMgr.unk2b = true;
        posterParams.x = mPosterLevels;
        posterParams.y = mPosterLevels * mPosterMin;
    }
    if (0.0f < mKaleidoscopeComplexity) {
        TheShaderMgr.unk2c = true;
        kaleidoParams.w = mKaleidoscopeRadius;
        kaleidoParams.y = mKaleidoscopeSize;
        float angle = mKaleidoscopeAngle * 0.017453292f;
        kaleidoParams.x = 6.2831855f / mKaleidoscopeComplexity;
        kaleidoParams.z = angle;
        if (mKaleidoscopeFlipUVs) {
            posterParams.z = 2.0f;
        } else {
            posterParams.z = 1.0f;
        }
    }
    TheShaderMgr.SetPConstant(kPS_Posterize, posterParams);
    TheShaderMgr.SetPConstant(kPS_Kaleidoscope, kaleidoParams);
}

void NgPostProc::CheckGradientMap() {
    if (DoGradientMap()) {
        Vector4 gradParams(mGradientMapStart, mGradientMapEnd, mGradientMapIndex, mGradientMapOpacity);
        TheShaderMgr.SetPConstant(kPS_EmissiveTex, mGradientMap.Ptr());
        TheShaderMgr.SetPConstant(kPS_GradientMap, gradParams);
        TheShaderMgr.unk3a = (mGradientMap.Ptr() != NULL);
    }
}

void NgPostProc::ReleaseTex() {
    for (int i = 0; i < 3; i++) {
        sBloom.mTextures[i].FreeTextures();
    }
    RndVelocityBuffer::Singleton().FreeData();
}

void NgPostProc::Terminate() { ReleaseTex(); }

void NgPostProc::EndWorld() {
    RndVelocityBuffer::Singleton().CacheCameraSettings(TheRnd.mWorldCamCopy);
}

void NgPostProc::OnSelect() {
    RndPostProc::OnSelect();
    mMotionBlurDrawList.clear();
}

void NgPostProc::OnUnselect() {
    RndPostProc::OnUnselect();
    mMotionBlurDrawList.clear();
}
