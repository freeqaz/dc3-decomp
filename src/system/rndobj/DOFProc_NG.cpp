#include "rndobj/DOFProc_NG.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Cam.h"
#include "rndobj/DOFProc.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/Tex.h"
#include "ui/UI.h"

NgDOFProc::NgDOFProc()
    : mEnabled(0), mDepthOfFieldScale(1), mDepthOfFieldBias(0), mFocalPlane(1), mBlurDepth(1), mMinBlur(0),
      mMaxBlur(1) {
    TheRnd.RegisterPostProcessor(this);
    MILO_ASSERT(TheNgRnd.PreProcessTexture(), 0x41);
    int w = TheNgRnd.PreProcessTexture()->Width() >> 2;
    int h = TheNgRnd.PreProcessTexture()->Height() >> 2;
    mBlurTex[0] = Hmx::Object::New<RndTex>();
    mBlurTex[0]->SetBitmap(w, h, TheRnd.Bpp(), RndTex::kRenderedNoZ, false, nullptr);
    mBlurTex[1] = mBlurTex[0];
}

bool NgDOFProc::Enabled() const { return mEnabled; }

void NgDOFProc::Set(const RndCam *cam, float focalPlane, float blurDepth, float maxBlur, float minBlur) {
    MILO_ASSERT(cam, 0xBF);

    mFocalPlane = focalPlane;

    DOFOverrideParams &dof = RndPostProc::DOFOverrides();

    mBlurDepth = Max(dof.mDepthScale * blurDepth + dof.mDepthOffset, 0.0f);
    mMaxBlur = Clamp(0.0f, 1.0f, dof.mMaxBlurScale * maxBlur + dof.mMaxBlurOffset);
    mMinBlur = Clamp(0.0f, 1.0f, dof.mMinBlurScale * minBlur + dof.mMinBlurOffset);

    if (mMaxBlur > 0.0f && TheUI->IsGameScreenActive()) {
        mEnabled = true;
    }

    if (mBlurDepth <= 0.001f) {
        mBlurDepth = 0.001f;
    }

    float nearPlane = cam->NearPlane();
    float farPlane = cam->FarPlane();

    float scale = 0.0f;
    if (nearPlane <= focalPlane) {
        scale = (farPlane - farPlane / focalPlane * nearPlane) / (farPlane - nearPlane)
            * (cam->ZRange().y - cam->ZRange().x) + cam->ZRange().x;
    }
    mDepthOfFieldScale = scale;

    float farFocal = focalPlane - focalPlane * mBlurDepth;

    float bias = 0.0f;
    if (nearPlane <= farFocal) {
        bias = (farPlane - farPlane / farFocal * nearPlane) / (farPlane - nearPlane)
            * (cam->ZRange().y - cam->ZRange().x) + cam->ZRange().x;
    }
    mDepthOfFieldBias = bias;

    if (scale < bias + 0.001f) {
        mDepthOfFieldScale = bias + 0.001f;
    }
}

NgDOFProc::~NgDOFProc() {
    RELEASE(mBlurTex[0]);
    TheRnd.UnregisterPostProcessor(this);
}

void NgDOFProc::Init() {
    REGISTER_OBJ_FACTORY(NgDOFProc);
    if (TheDOFProc && !dynamic_cast<NgDOFProc *>(TheDOFProc)) {
        RELEASE(TheDOFProc);
        TheDOFProc = Hmx::Object::New<DOFProc>();
        MILO_ASSERT(dynamic_cast< NgDOFProc* >(TheDOFProc) != NULL, 0x175);
        static DataNode &n = DataVariable("the_dof_proc");
        n = TheDOFProc;
    }
}

void NgDOFProc::Terminate() {
    RELEASE(TheDOFProc);
    static DataNode &n = DataVariable("the_dof_proc");
    n = NULL_OBJ;
}
