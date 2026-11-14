#include "rndobj\DOFProc.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "rndobj/Cam.h"
#ifdef HX_NATIVE
#include "math/Utl.h"
#include "rndobj/PostProc.h"
#include "ui/UI.h"
#endif

DOFProc *TheDOFProc;

#ifdef HX_NATIVE
namespace {
// Native: the parameter half of NgDOFProc (rndobj/DOFProc_NG.cpp). Retail
// installs NgDOFProc from rnddx9 (NgRnd init, Rnd_Xbox.cpp), which native does
// not run, so TheDOFProc stayed this base class: Set was a no-op, Enabled() 0,
// and the engine's DofPass (gfx/DofPass.cpp, the downsample_4x / blur / DOF
// composite of NgDOFProc::DoPost and postprocess bit 3) never ran for a
// camera shot with use_depth_of_field. This keeps what NgDOFProc::Set keeps,
// with its enable rule: set only when maxBlur > 0 on the game screen, cleared
// only by UnSet. The projected-depth scale and bias Set also derives are
// computed by DofPass from the same values and the current camera.
class NativeDOFProc : public DOFProc {
public:
    NativeDOFProc()
        : mEnabled(false), mFocalPlane(1), mBlurDepth(1), mMinBlur(0), mMaxBlur(1) {}
    NEW_OBJ(NativeDOFProc)

    virtual void Set(const RndCam *, float focalPlane, float blurDepth, float maxBlur,
                     float minBlur) {
        mFocalPlane = focalPlane;
        DOFOverrideParams &dof = RndPostProc::DOFOverrides();
        mBlurDepth = Max(dof.mDepthScale * blurDepth + dof.mDepthOffset, 0.0f);
        mMaxBlur = Clamp(0.0f, 1.0f, dof.mMaxBlurScale * maxBlur + dof.mMaxBlurOffset);
        mMinBlur = Clamp(0.0f, 1.0f, dof.mMinBlurScale * minBlur + dof.mMinBlurOffset);
        if (mMaxBlur > 0.0f && TheUI->IsGameScreenActive())
            mEnabled = true;
        if (mBlurDepth <= 0.001f)
            mBlurDepth = 0.001f;
    }
    virtual void UnSet() { mEnabled = false; }
    virtual bool Enabled() const { return mEnabled; }
    virtual float FocalPlane() { return mFocalPlane; }
    virtual float BlurDepth() { return mBlurDepth; }
    virtual float MaxBlur() { return mMaxBlur; }
    virtual float MinBlur() { return mMinBlur; }

private:
    bool mEnabled;
    float mFocalPlane;
    float mBlurDepth;
    float mMinBlur;
    float mMaxBlur;
};
} // namespace
#endif

DOFProc::DOFProc() {}
DOFProc::~DOFProc() {}

void DOFProc::Init() {
#ifdef HX_NATIVE
    Hmx::Object::RegisterFactory(DOFProc::StaticClassName(), NativeDOFProc::NewObject);
#else
    REGISTER_OBJ_FACTORY(DOFProc);
#endif
    if (!TheDOFProc) {
        TheDOFProc = Hmx::Object::New<DOFProc>();
        static DataNode &n = DataVariable("the_dof_proc");
        n = TheDOFProc;
    }
}

void DOFProc::Terminate() {
    RELEASE(TheDOFProc);
    static DataNode &n = DataVariable("the_dof_proc");
    n = NULL_OBJ;
}

BEGIN_HANDLERS(DOFProc)
    HANDLE_ACTION(unset, UnSet())
    HANDLE_ACTION(
        set,
        Set(_msg->Obj<RndCam>(2),
            _msg->Float(3),
            _msg->Float(4),
            _msg->Float(5),
            _msg->Float(6))
    )
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
