#include "rndobj/Shader.h"
#include "Rnd.h"
#include "os/System.h"
#include "rnddx9/RenderState.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Mat_NG.h"
#include "rndobj/Env_NG.h"
#include "os/Debug.h"
#include "rndobj/Mat.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/ShaderOptions.h"
#include "rndobj/ShaderProgram.h"
#include "rndobj/Shockwave.h"
#include "rndobj/Spline.h"
#include "rndobj/Stats_NG.h"
#include "math/Utl.h"
#include "utl/Loader.h"
#include "utl/Str.h"
#include <set>

std::set<unsigned int> sWarnings;
RndShaderSimple gShaderSimple;
RndShaderParticles gShaderParticles;
RndShaderMultimesh gShaderMultimesh;
RndShaderStandard gShaderStandard;
RndShaderPostProc gShaderPostProc;
RndShaderDrawRect gShaderDrawRect;
RndShaderUnwrapUV gShaderUnwrapUV;
RndShaderVelocity gShaderVelocity;
RndShaderVelocityCamera gShaderVelocityCamera;
RndShaderDepthVolume gShaderDepthVolume;
RndShaderFur gShaderFur;
RndShaderSyncTrack gShaderSyncTrack;

unsigned int StrHash(const char *str) {
    unsigned int hash = 0;
    int constMult = 0xF8C9;
    for (const unsigned char *p = (const unsigned char *)str; *p != '\0'; p++) {
        hash = hash * constMult + *p;
        constMult *= 0x5C6B7;
    }
    return hash;
}

void CheckDistortionOpts(RndMat *, ShaderOptions &);
void CheckDistortion(RndMat *);
void SetColorWriteMask(const ShaderOptions &, RndMat *);
void CheckShadow();
void CheckExtrude();

void RndShader::Init() {
    sShaders[kBlurShader] = &gShaderSimple;
    sShaders[kBloomShader] = &gShaderSimple;
    sShaders[kDepthVolumeShader] = &gShaderDepthVolume;
    sShaders[kBloomGlareShader] = &gShaderSimple;
    sShaders[kDrawRectShader] = &gShaderDrawRect;
    sShaders[kDownsampleShader] = &gShaderSimple;
    sShaders[kDownsampleDepthShader] = &gShaderSimple;
    sShaders[kDownsample4xShader] = &gShaderSimple;
    sShaders[kMultimeshShader] = &gShaderMultimesh;
    sShaders[kFurShader] = &gShaderFur;
    sShaders[kErrorShader] = &gShaderSimple;
    sShaders[kLineNozShader] = &gShaderSimple;
    sShaders[kMovieShader] = &gShaderSimple;
    sShaders[kMultimeshBBShader] = &gShaderMultimesh;
    sShaders[kLineShader] = &gShaderSimple;
    sShaders[kShadowmapShader] = &gShaderSimple;
    sShaders[kPostprocessErrorShader] = &gShaderSimple;
    sShaders[kPlayerDepthVisShader] = &gShaderSimple;
    sShaders[kParticlesShader] = &gShaderParticles;
    sShaders[kPlayerDepthShellShader] = &gShaderSimple;
    sShaders[kSyncTrackShader] = &gShaderSyncTrack;
    sShaders[kStandardShader] = &gShaderStandard;
    sShaders[kStandardBBShader] = &gShaderStandard;
    sShaders[kPostprocessShader] = &gShaderPostProc;
    sShaders[kPlayerDepthShell2Shader] = &gShaderSimple;
    sShaders[kDepthBuffer3DShader] = &gShaderSimple;
    sShaders[kYUVtoRGBShader] = &gShaderSimple;
    sShaders[kSyncTrackChargeEffectShader] = &gShaderSyncTrack;
    sShaders[kVelocityCameraShader] = &gShaderVelocityCamera;
    sShaders[kUnwrapUVShader] = &gShaderUnwrapUV;
    sShaders[kVelocityObjectShader] = &gShaderVelocity;
    sShaders[kYUVtoBlackAndWhiteShader] = &gShaderSimple;
    sShaders[kPlayerGreenScreenShader] = &gShaderSimple;
    sShaders[kPlayerDepthGreenScreenShader] = &gShaderSimple;
    sShaders[kCrewPhotoShader] = &gShaderSimple;
    sShaders[kTwirlShader] = &gShaderSimple;
    sShaders[kKillAlphaShader] = &gShaderSimple;
    sShaders[kAllWhiteShader] = &gShaderStandard;
}

void RndShader::CheckForceCull(ShaderType s) {
    int cullOverride = TheShaderMgr.CullModeOverride();
    if (TheRnd.GetDrawMode() == Rnd::kDrawShadowColor || cullOverride == 1) {
        TheRenderState.SetCullMode((RndRenderState::CullMode)0);
    } else if (s != kShadowmapShader && cullOverride != 3 && TheRnd.GetDrawMode() != 8) {
        if (cullOverride == 2) {
            TheRenderState.SetCullMode((RndRenderState::CullMode)2);
        }
    } else {
        TheRenderState.SetCullMode((RndRenderState::CullMode)6);
    }
}

bool RndShader::RedundantState(
    const RndMat *mat, ShaderType s, bool skinned, bool useAO, bool b5
) {
    if (!b5 && mat && (NgMat *)mat == NgMat::Current() && !mat->Dirty()
        && s == sCurrentShader && skinned == sCurrentSkinned && useAO == sCurrentUseAO) {
        if (s == kStandardShader || s == kStandardBBShader || s == kParticlesShader
            || s == kMultimeshShader || s == kMultimeshBBShader || s == kSyncTrackShader
            || s == kSyncTrackChargeEffectShader || s == kAllWhiteShader) {
            return true;
        }
    }
    sCurrentUseAO = useAO;
    sCurrentShader = s;
    sCurrentSkinned = skinned;
    return false;
}

void RndShader::ShaderWarn(const char *msg) {
    unsigned int hash = StrHash(msg);
    if (sWarnings.end() == sWarnings.find(hash)) {
        MILO_NOTIFY(msg);
        sWarnings.insert(hash);
    }
    if (TheLoadMgr.EditMode()) {
        Debug::ModalType ty = Debug::kModalNotify;
        if (mModalCallback) {
            StackString<1024> str(msg);
            (*mModalCallback)(ty, str, true);
        }
    }
}

void RndShader::WarnMatProp(const char *prop, NgMat *mat, NgEnviron *env, ShaderType s) {
    ShaderWarn(MakeString(
        "[%s] must have %s.  (%s, %s)",
        PathName(mat),
        prop,
        PathName(env),
        ShaderTypeName(s)
    ));
    sMatShadersOK = false;
}

bool RndShader::MatShaderFlagsOK(RndMat *mat, ShaderType s) {
    if (!mat || TheRnd.DefaultEnv() == RndEnviron::Current()
        || TheRnd.GetDrawMode() == Rnd::kDrawOcclusion) {
        return true;
    }
    NgEnviron *curEnv = (NgEnviron *)RndEnviron::Current();
    sMatShadersOK = true;
    RndShader *curShader = sShaders[s];
    bool b1824 = mat->UseEnviron() && RndEnviron::Current()->NumLights_Real() != 0;
    if (curShader->CheckError((MatFlagErrorType)0) && !mat->FadeOut()) {
        bool fadeoutCheck = curEnv->FadeOut() && curEnv->FadeEnd() != curEnv->FadeStart();
        if (fadeoutCheck) {
            WarnMatProp("fadeout checked", (NgMat *)mat, curEnv, s);
        }
    } else if (mat->FadeOut()) {
        bool fadeoutUncheck =
            curEnv->FadeOut() && curEnv->FadeEnd() != curEnv->FadeStart();
        if (!fadeoutUncheck) {
            WarnMatProp("fadeout unchecked", (NgMat *)mat, curEnv, s);
        }
    }
    if (curShader->CheckError((MatFlagErrorType)1) && b1824 && !mat->PointLights()
        && curEnv->NumLights_Point()) {
        WarnMatProp("point_lights checked", (NgMat *)mat, curEnv, s);
    }
    if (curShader->CheckError((MatFlagErrorType)2) && !mat->ColorAdjust()
        && curEnv->UseColorAdjust()) {
        WarnMatProp("color_adjust checked", (NgMat *)mat, curEnv, s);
    }
    return sMatShadersOK;
}

bool RndShader::DisplayMatShaderFlagsError(RndMat *mat, ShaderType s) {
    bool ret = false;
    if (TheShaderMgr.ShowShaderErrors()) {
        ret = !MatShaderFlagsOK(mat, s);
    }
    return ret;
}

void RndShader::SelectConfig(RndMat *mat, ShaderType shader_type, bool b3) {
    RndShader *shader;
    MILO_ASSERT(shader_type >= ShaderType(0) && shader_type < kMaxShaderTypes, 0x1BB);
    if (TheRnd.GetDrawMode() == 2) {
        shader_type = kShadowmapShader;
    } else if (TheRnd.GetDrawMode() == 6) {
        shader_type = kVelocityObjectShader;
    } else if (TheShaderMgr.InDepthVolume()) {
        shader_type = kDepthVolumeShader;
    }
    if (!b3 && (TheLoadMgr.EditMode() || !UsingCD())) {
        if (!DisplayMatShaderFlagsError(mat, shader_type)) {
            bool doError = true;
            void *metaMat;
            if (mat && TheShaderMgr.ShowMetaMatErrors()) {
                metaMat = mat->GetMetaMaterial();
                doError = doError && (metaMat == nullptr);
            }
            if (!doError) {
                goto done;
            }
        }
        shader_type = shader_type == kPostprocessShader
            ? kPostprocessErrorShader
            : kErrorShader;
    }
done:
    shader = sShaders[shader_type];
    MILO_ASSERT(shader, 0x1D3);
    shader->Select(mat, shader_type, b3);
}

void RndShader::Cache(ShaderType s, ShaderOptions opts, RndMat *mat) {
    RndShaderProgram &program = TheShaderMgr.FindShader(s, opts);
    if (!program.Cached()) {
        if (!program.Cache(s, opts, nullptr, nullptr)
            && (UsingCD() || !TheShaderMgr.CacheShaders())) {
            MatShaderFlagsOK(mat, s);
        }
    }
    bool select = s == kShadowmapShader || TheRnd.GetDrawMode() == Rnd::kDrawShadowColor;
    program.Select(select);
}

void RndShaderSimple::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) {
        if (s == kLineNozShader) {
            mat = TheShaderMgr.DrawHighlightMat();
            mat->SetZMode(kZModeForce);
            s = kLineShader;
        } else {
            mat = TheRnd.DefaultMat();
        }
    }
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    bool isSkinned = TheShaderMgr.BoneCount() && (s == kErrorShader || s == kShadowmapShader);
    if (!RedundantState(mat, s, isSkinned, TheShaderMgr.UseAO(), b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        CheckForceCull(s);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

bool RndShaderMultimesh::CheckError(MatFlagErrorType type) {
    return type == (MatFlagErrorType)0 || type == (MatFlagErrorType)1 || type == (MatFlagErrorType)2;
}

bool RndShaderParticles::CheckError(MatFlagErrorType type) {
        return !(type != (MatFlagErrorType)0 && type != (MatFlagErrorType)2) && TheRnd.GetDrawMode() != 4;
}

void SetColorWriteMask(const ShaderOptions &opts, RndMat *mat) {
    bool writeAlpha = mat->mAlphaWrite;
    if (!mat->mForceAlphaWrite
        && ((opts.flags & 0x400000) != 0 || ((NgMat *)mat)->AllowHDR() || writeAlpha)) {
        writeAlpha = true;
    }
    TheRenderState.SetColorWriteMask((-(unsigned int)writeAlpha & 8) + 7);
}

void CheckDistortionOpts(RndMat *mat, ShaderOptions &opts) {
    RndSpline *spline = RndSpline::sGlobalDefaultSpline;
    if (spline && !mat->mNeverFitToSpline && spline->mCtrlPoints.size() > 1) {
        opts.flags = ((u64)(spline->mPulseDrawing & 1) << 56)
            | (opts.flags & ~((u64)1 << 56))
            | ((u64)1 << 55);
    }
    if (RndShockwave::sSelected
        && Abs(RndShockwave::sSelected->mAmplitude) >= 0.0001f
        && mat->mAllowDistortionEffects
        && Abs(mat->mShockwaveMult) >= 0.0001f) {
        opts.flags |= (u64)1 << 60;
    }
}

void CheckDistortion(RndMat *mat) {
    if (RndSpline::sGlobalDefaultSpline
        && !mat->mNeverFitToSpline
        && !RndSpline::sGlobalDefaultSpline->mManual
        && RndSpline::sGlobalDefaultSpline->mCtrlPoints.size() > 1) {
        RndSpline::sGlobalDefaultSpline->PrepareShader();
    }
    if (RndShockwave::sSelected
        && Abs(RndShockwave::sSelected->mAmplitude) >= 0.0001f
        && mat->mAllowDistortionEffects
        && Abs(mat->mShockwaveMult) >= 0.0001f) {
        RndShockwave::sSelected->PrepareShader(mat->mShockwaveMult);
    }
}

void CheckShadow() {
    RndCam *shadowCam = TheNgRnd.GetShadowCam();
    if (shadowCam) {
        Transform viewXfm;
        Hmx::Matrix4 projMtx;
        shadowCam->GetViewProjectXfms(viewXfm, projMtx);
        Hmx::Matrix4 viewProj = Hmx::operator*(viewXfm, projMtx);
        static Hmx::Matrix4 sShadowTexMatrix;
        static bool sInit;
        if (!sInit) {
            sShadowTexMatrix.x = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
            sShadowTexMatrix.y = Vector4(0.0f, -0.5f, 0.0f, 0.0f);
            sShadowTexMatrix.z = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
            sShadowTexMatrix.w = Vector4(0.0f, 0.501953125f, 0.0f, 1.0f);
            sInit = true;
        }
        Hmx::Matrix4 result = Hmx::operator*(viewProj, sShadowTexMatrix);
        TheShaderMgr.SetVConstant((VShaderConstant)0x28, result);
    }
}

void CheckExtrude() {
    if (TheRnd.GetDrawMode() == Rnd::kDrawShadowColor) {
        TheRenderState.SetDepthTestEnable(true);
        TheRenderState.SetDepthWriteEnable(true);
        TheRenderState.SetBlendEnable(true);
        TheRenderState.SetBlend(
            (RndRenderState::Blend)0, (RndRenderState::Blend)1,
            (RndRenderState::Blend)1, (RndRenderState::Blend)1
        );
        TheRenderState.SetDepthFunc((RndRenderState::TestFunc)1);
        TheRenderState.SetAlphaTestEnable(false);
        Transform viewXfm;
        Hmx::Matrix4 projMtx;
        RndCam::Current()->GetViewProjectXfms(viewXfm, projMtx);
        Hmx::Matrix4 viewProj = Hmx::operator*(viewXfm, projMtx);
        TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, viewProj);
    }
}

u64 RndShaderSimple::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderParticles::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderMultimesh::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderStandard::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderPostProc::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderDrawRect::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderUnwrapUV::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderVelocity::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderVelocityCamera::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderDepthVolume::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderFur::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }
u64 RndShaderSyncTrack::CalcShaderOpts(NgMat *, ShaderType, bool) { return 0; }

void RndShaderParticles::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    if (!RedundantState(mat, s, false, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(false, true);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderMultimesh::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    if (!RedundantState(mat, s, false, TheShaderMgr.UseAO(), b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        CheckForceCull(kMultimeshShader);
        CheckDistortion(mat);
        Cache(kMultimeshShader, ShaderOptions(optsVal), mat);
    }
}

void RndShaderStandard::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    bool skinned = TheShaderMgr.BoneCount() != 0;
    if (!RedundantState(mat, s, skinned, TheShaderMgr.UseAO(), b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        CheckShadow();
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        MILO_ASSERT(s == kStandardShader || s == kStandardBBShader || s == kAllWhiteShader, 0x4BB);
        if (s != kStandardShader) {
            s = kStandardShader;
        }
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        CheckExtrude();
        CheckForceCull(s);
        CheckDistortion(mat);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderPostProc::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    if (!RedundantState(mat, s, false, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), false);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        TheRenderState.SetColorWriteMask(0xF);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderDrawRect::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheShaderMgr.DrawRectMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    if (!RedundantState(mat, s, false, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        TheShaderMgr.SetVConstant(kVS_AmbientColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        TheShaderMgr.SetPConstant(kPS_AmbientColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        CheckForceCull(kStandardShader);
        Cache(kStandardShader, ShaderOptions(optsVal), mat);
    }
}

void RndShaderUnwrapUV::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    if (!RedundantState(mat, s, false, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        TheRenderState.SetColorWriteMask(7);
        TheShaderMgr.SetVConstant(kVS_AmbientColor, Vector4(mat->GetColor().red, mat->GetColor().green, mat->GetColor().blue, mat->GetColor().alpha));
        TheShaderMgr.SetPConstant(kPS_AmbientColor, Vector4(mat->GetColor().red, mat->GetColor().green, mat->GetColor().blue, mat->GetColor().alpha));
        CheckForceCull(s);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderVelocity::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    bool skinned = TheShaderMgr.BoneCount() != 0;
    if (!RedundantState(mat, s, skinned, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(false, false);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        CheckForceCull(s);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderVelocityCamera::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    if (!RedundantState(mat, s, false, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(false, false);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        CheckForceCull(s);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderDepthVolume::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    bool skinned = TheShaderMgr.BoneCount() != 0;
    if (!RedundantState(mat, s, skinned, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        if (TheShaderMgr.InDepthVolume()) {
            TheRenderState.SetBlendOp(TheShaderMgr.unk24
                ? (RndRenderState::BlendOp)4 : (RndRenderState::BlendOp)0);
            TheRenderState.SetBlendEnable(true);
            TheRenderState.SetBlend(
                (RndRenderState::Blend)1, (RndRenderState::Blend)1,
                (RndRenderState::Blend)1, (RndRenderState::Blend)1
            );
            TheRenderState.SetDepthTestEnable(false);
            TheRenderState.SetDepthWriteEnable(false);
        }
        CheckExtrude();
        TheShaderMgr.SetVConstant(kVS_AmbientColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        TheShaderMgr.SetPConstant(kPS_AmbientColor, Vector4(1.0f, 1.0f, 1.0f, 1.0f));
        CheckForceCull(s);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderFur::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    bool skinned = TheShaderMgr.BoneCount() != 0;
    if (!RedundantState(mat, s, skinned, false, b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(false, true);
        CheckShadow();
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        CheckForceCull(s);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}

void RndShaderSyncTrack::Select(RndMat *mat, ShaderType s, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    bool skinned = TheShaderMgr.BoneCount() != 0;
    if (!RedundantState(mat, s, skinned, TheShaderMgr.UseAO(), b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        CheckShadow();
        u64 optsVal = CalcShaderOpts((NgMat *)mat, s, b);
        MILO_ASSERT(s == kSyncTrackShader || s == kSyncTrackChargeEffectShader, 0x749);
        if (s != kSyncTrackShader) {
            s = kSyncTrackShader;
        }
        SetColorWriteMask(ShaderOptions(optsVal), mat);
        CheckExtrude();
        CheckForceCull(s);
        Cache(s, ShaderOptions(optsVal), mat);
    }
}
