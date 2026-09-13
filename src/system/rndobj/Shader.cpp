#include "rndobj\Shader.h"
#include "Rnd.h"
#include "os\System.h"
#include "rndobj\HiResScreen.h"
#include "rnddx9\RenderState.h"
#include "rndobj/Cam.h"
#include "rndobj\Env.h"
#include "rndobj\Mat_NG.h"
#include "rndobj\Env_NG.h"
#include "os\Debug.h"
#include "rndobj\Mat.h"
#include "rndobj\Rnd.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\ShaderOptions.h"
#include "rndobj/ShaderProgram.h"
#include "rndobj\Shockwave.h"
#include "rndobj\Spline.h"
#include "rndobj\Stats_NG.h"
#include "math\Utl.h"
#include "utl/Loader.h"
#include "utl\Str.h"
#include <set>

bool RndShader::sCurrentUseAO;
bool RndShader::sMatShadersOK;
ModalCallbackFunc *RndShader::mModalCallback;
ShaderType RndShader::sCurrentShader = kMaxShaderTypes;
bool RndShader::sCurrentSkinned;
RndShader *RndShader::sShaders[kMaxShaderTypes];

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
    if (TheRnd.DrawMode() == Rnd::kDrawShadowColor || cullOverride == 1) {
        TheRenderState.SetCullMode((RndRenderState::CullMode)0);
    } else if (s != kShadowmapShader && cullOverride != 3 && TheRnd.DrawMode() != 8) {
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
        || TheRnd.DrawMode() == Rnd::kDrawOcclusion) {
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
    if (TheRnd.DrawMode() == 2) {
        shader_type = kShadowmapShader;
    } else if (TheRnd.DrawMode() == 6) {
        shader_type = kVelocityObjectShader;
    } else if (TheShaderMgr.InDepthVolume()) {
        shader_type = kDepthVolumeShader;
    }
#ifdef HX_NATIVE
    // Native/web: skip shader diagnostic path. On Xbox retail UsingCD()==true
    // so this path is dead code. On native, UsingCD() may be false (no .ark),
    // which would activate editor-mode shader validation that crashes on WASM
    // (virtual calls into unimplemented NG shader subsystems).
    if (!b3 && TheLoadMgr.EditMode()) {
#else
    if (!b3 && (TheLoadMgr.EditMode() || !UsingCD())) {
#endif
        if (!DisplayMatShaderFlagsError(mat, shader_type)) {
            bool doError = false;
            if (mat && TheShaderMgr.ShowMetaMatErrors()) {
                doError = (mat->GetMetaMaterial() == nullptr);
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
#ifdef HX_NATIVE
    if (!shader) {
        // Fallback: unregistered shader type — use error shader
        shader = sShaders[kErrorShader];
        if (!shader) return;
    }
#else
    MILO_ASSERT(shader, 0x1D3);
#endif
    shader->Select(mat, shader_type, b3);
}

void RndShader::Cache(ShaderType s, ShaderOptions opts, RndMat *mat) {
    RndShaderProgram &program = TheShaderMgr.FindShader(s, opts);
    if (!program.Cached()) {
        if (!program.Cache(s, opts, nullptr, nullptr)
#ifdef HX_NATIVE
            && !TheShaderMgr.CacheShaders()
#else
            && (UsingCD() || !TheShaderMgr.CacheShaders())
#endif
        ) {
            MatShaderFlagsOK(mat, s);
        }
    }
    bool select = s == kShadowmapShader || TheRnd.DrawMode() == Rnd::kDrawShadowColor;
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

bool RndShaderStandard::CheckError(MatFlagErrorType) { return true; }

bool RndShaderFur::CheckError(MatFlagErrorType) { return true; }

bool RndShaderSyncTrack::CheckError(MatFlagErrorType) { return true; }

bool RndShaderMultimesh::CheckError(MatFlagErrorType type) {
    return type == (MatFlagErrorType)0 || type == (MatFlagErrorType)1 || type == (MatFlagErrorType)2;
}

bool RndShaderParticles::CheckError(MatFlagErrorType type) {
        return !(type != (MatFlagErrorType)0 && type != (MatFlagErrorType)2) && TheRnd.DrawMode() != 4;
}

void SetColorWriteMask(const ShaderOptions &opts, RndMat *mat) {
    bool optAlpha = (opts.flags & 0x400000) != 0;
    bool offscreen = TheNgRnd.Offscreen();
    bool alpha = mat->mAlphaWrite;
    if (!mat->mForceAlphaWrite) {
        alpha = optAlpha || offscreen || alpha;
    }
    TheRenderState.SetColorWriteMask(alpha ? 15 : 7);
}

void CheckDistortionOpts(RndMat *mat, ShaderOptions &opts) {
    RndSpline *spline = RndSpline::sGlobalDefaultSpline;
    if (spline && !mat->mNeverFitToSpline && spline->mCtrlPoints.size() >= 2) {
        opts.flags |= (u64)1 << 55;
        opts.flags = ((u64)(spline->mPulseDrawing & 1) << 56)
            | (opts.flags & ~((u64)1 << 56));
    }
    RndShockwave *shockwave = RndShockwave::sSelected;
    if (shockwave) {
        bool ampBad = NearlyZero(shockwave->mAmplitude);
        if (!ampBad && mat->mAllowDistortionEffects) {
            bool multBad = NearlyZero(mat->mShockwaveMult);
            if (!multBad) {
                opts.flags |= (u64)1 << 60;
            }
        }
    }
}

void CheckDistortion(RndMat *mat) {
    RndSpline *spline = RndSpline::sGlobalDefaultSpline;
    if (spline
        && !mat->mNeverFitToSpline
        && !spline->mManual
        && spline->mCtrlPoints.size() >= 2) {
        spline->PrepareShader();
    }
    RndShockwave *shock = RndShockwave::sSelected;
    if (shock) {
        bool ampBad = NearlyZero(shock->mAmplitude);
        if (!ampBad && mat->mAllowDistortionEffects) {
            bool multBad = NearlyZero(mat->mShockwaveMult);
            if (!multBad) {
                shock->PrepareShader(mat->mShockwaveMult);
            }
        }
    }
}

void CheckShadow() {
    RndCam *shadowCam = TheRnd.GetShadowCam();
    if (shadowCam) {
        Transform viewXfm;
        Hmx::Matrix4 projMtx;
        shadowCam->GetViewProjectXfms(viewXfm, projMtx);
        Hmx::Matrix4 viewProj = Hmx::operator*(viewXfm, projMtx);
        // Clip space (-1..1, y down) -> shadow-map texture space (0..1), with the
        // D3D9 half-texel offset for a 1024x1024 map: 0.5 + 0.5/1024.
        static Hmx::Matrix4 sShadowTexMatrix(
            Vector4(0.5f, 0.0f, 0.0f, 0.0f),
            Vector4(0.0f, -0.5f, 0.0f, 0.0f),
            Vector4(0.0f, 0.0f, 1.0f, 0.0f),
            Vector4(0.5009765625f, 0.5009765625f, 0.0f, 1.0f)
        );
        viewProj = Hmx::operator*(viewProj, sShadowTexMatrix);
        TheShaderMgr.SetVConstant((VShaderConstant)0x28, viewProj);
    }
}

void CheckExtrude() {
    if (TheRnd.DrawMode() == Rnd::kDrawShadowColor) {
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

u64 RndShaderVelocityCamera::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    return (u64)(TheHiResScreen.IsActive() & 1) << 52;
}

u64 RndShaderVelocity::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    u64 skinned = (u64)(TheShaderMgr.BoneCount() > 0) & 1;
    return (skinned | (u64)(TheHiResScreen.IsActive() & 1) << 40) << 12;
}

u64 RndShaderUnwrapUV::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    u64 opts = ((u64)(bool)mat->GetDiffuseTex() & 1) | 0x10;
    return (opts | ((u64)(TheHiResScreen.IsActive() & 1) << 48)) << 4;
}

u64 RndShaderDepthVolume::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    u64 skinned = (u64)(bool)TheShaderMgr.BoneCount() & 1;
    u64 opts = (((u64)(uint)TheShaderMgr.unk1c & ~0xFFFFFFFCULL) | skinned << 11) << 1;
    u64 shadow = (u64)(TheRnd.DrawMode() == Rnd::kDrawShadowColor) & 1;
    u64 hi = (shadow | (u64)(TheHiResScreen.IsActive() & 1) << 29) << 23;
    return hi | (opts & ~((1ULL << 23) | (1ULL << 52)));
}

u64 RndShaderSimple::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    u64 opts = 0;
    switch (s) {
    case kBlurShader:
        opts = (u64)((TheShaderMgr.unk14 - 1) & 0xf) << 14;
        break;
    case kErrorShader: {
        int boneCount = TheShaderMgr.BoneCount();
        bool displayError = TheShaderMgr.GetShaderErrorDisplay();
        u64 bc = (u64)(bool)boneCount & 1;
        u64 de = (u64)displayError & 1;
        opts = (de << 23 | bc) << 12;
        break;
    }
    case kMovieShader: {
        u64 specBit = (u64)(mat->GetSpecularMap() == nullptr) & 1;
        u64 normBit = (u64)(bool)mat->NormalMap() & 1;
        opts = (specBit | normBit << 4) << 1;
        break;
    }
    case kPostprocessErrorShader: {
        bool displayError = TheShaderMgr.GetShaderErrorDisplay();
        opts = (u64)(displayError & 1) << 35;
        break;
    }
    case kShadowmapShader: {
        int bc = TheShaderMgr.BoneCount();
        opts = (u64)(((bool)bc & 1) << 12);
        break;
    }
    default:
        break;
    }
    opts = (opts & ~((u64)1 << 52)) | ((u64)(TheHiResScreen.IsActive() & 1) << 52);
    int drawDiff = TheRnd.DrawMode() - Rnd::kDrawOcclusion;
    return -(u64)(bool)drawDiff & opts;
}

u64 RndShaderDrawRect::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    if (TheRnd.DrawMode() == Rnd::kDrawOcclusion) return 0;
    int hasDiffuse = mat->GetDiffuseTex() != nullptr;
    bool prelit = mat->Prelit();
    bool offscreen;
    u64 matBits = ((u64)(hasDiffuse & 1) | (u64)(prelit & 1) << 4) << 4;
    if (b) {
        offscreen = TheShaderMgr.GetUnk41();
    } else {
        offscreen = TheNgRnd.Offscreen();
    }
    u64 pseudoHDR = (!offscreen && mat->AllowHDR()) ? 1 : 0;
    u64 hi = pseudoHDR | ((u64)(TheHiResScreen.IsActive() & 1) << 2
        | (u64)(TheRnd.ResourceCached() & 1)) << 28;
    return (hi << 22) | (matBits & (0xAFFFFFFEULL << 22));
}

u64 RndShaderParticles::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    ShaderOptions opts(0);
    bool hasDiffuse = mat->GetDiffuseTex() != nullptr;
    opts.mDiffuseMap = hasDiffuse;
    opts.mPrelit = 1;
    int texGenVal;
    switch (mat->GetTexGen()) {
    case kTexGenSphere:
        texGenVal = 1;
        break;
    case kTexGenProjected:
        texGenVal = 2;
        break;
    case kTexGenEnviron:
        texGenVal = 3;
        break;
    default:
        texGenVal = 0;
        break;
    }
    opts.mTexGen = texGenVal;
    // particles are never lit by the environ
    opts.mRealLights = 0;
    opts.mApproxLights = 0;
    NgEnviron *env = (NgEnviron *)RndEnviron::Current();
    bool fadeOut;
    if (b) {
        fadeOut = mat->FadeOut();
    } else {
        fadeOut = env->FadeOut() && env->FadeEnd() != env->FadeStart();
    }
    u64 pseudoHDR;
    if (!fadeOut) {
        bool offscreen;
        if (b) {
            offscreen = TheShaderMgr.GetUnk41();
        } else {
            offscreen = TheNgRnd.Offscreen();
        }
        if (!offscreen && mat->AllowHDR()) {
            pseudoHDR = 1;
        } else {
            pseudoHDR = 0;
        }
    } else {
        pseudoHDR = 0;
    }
    opts.mPseudoHDR = pseudoHDR;
    bool colorAdjust;
    if (b) {
        colorAdjust = mat->ColorAdjust();
    } else {
        colorAdjust = env->UseColorAdjust();
    }
    opts.mColorXfm = colorAdjust;
    opts.mIntensify = mat->GetIntensify();
    if (fadeOut) {
        Vector4 fadeParams(mat->unk2d8, mat->unk2dc, mat->unk2e0, mat->unk2e4);
        TheShaderMgr.SetPConstant((PShaderConstant)0x68, fadeParams);
        opts.mFadeOut = mat->unk2d4;
    }
    if (mat->GetRefractEnabled(b) && mat->GetRefractNormalMap() != nullptr) {
        opts.mRefractWorld = 1;
    }
    opts.mFog = mat->AllowFog() && mat->GetFog();
    if (TheRnd.DrawMode() == (Rnd::Mode)7) {
        opts.mSoftDepthBlend = 1;
    }
    int drawDiff = TheRnd.DrawMode() - Rnd::kDrawOcclusion;
    ShaderOptions result(-(u64)(bool)drawDiff & opts.flags);
    result.mShowShaderCost = TheRnd.ResourceCached();
    result.mHiResScreen = TheHiResScreen.IsActive();
    return result.flags;
}

u64 RndShaderMultimesh::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    if (TheRnd.DrawMode() == Rnd::kDrawOcclusion)
        return 0;
    NgEnviron *env = (NgEnviron *)RndEnviron::Current();
    ShaderOptions opts(0);
    bool hasDiffuse = mat->GetDiffuseTex() != nullptr;
    opts.mDiffuseMap = hasDiffuse;
    opts.mPrelit = mat->Prelit();
    opts.mRealLights = mat->UseEnviron() && env->NumLights_Real() > 0;
    opts.mApproxLights = mat->UseEnviron() && env->NumLights_Approx() > 0;
    if (opts.mRealLights || opts.mApproxLights) {
        opts.mSpecular = mat->GetSpecularRGB().Pack() != 0;
        if (TheShaderMgr.AllowPerPixel() && mat->GetPerPixelLit()) {
            int hasNormal = mat->NormalMap() != nullptr;
            opts.mNormalMap = hasNormal;
            opts.mPerPixelLighting = 1;
            opts.mNormDetail = mat->GetNormDetailMap() != nullptr
                && mat->GetNormDetailStrength() > 0.0f;
            bool flipNormal = mat->GetCull() == kCullBackwards;
            opts.mFlipNormal = flipNormal;
            opts.mSpecularMap = opts.mSpecular && mat->GetSpecularMap() != nullptr;
            opts.mRimLight = mat->GetRimRGB().Pack() != 0;
            opts.mRimLightUnder = opts.mRimLight && mat->GetRimLightUnder();
            opts.mRimLightMap = opts.mRimLight && mat->GetRimMap() != nullptr;
        }
        if (mat->GetEnvironMap() != nullptr) {
            opts.mEnvironMapFalloff = mat->GetEnvironMapFalloff();
            opts.mEnvironMap = 1;
            opts.mEnvironMapSpecMask = opts.mSpecularMap && mat->GetEnvironMapSpecMask();
        }
        opts.mNumPoint = env->NumLights_Point();
    }
    bool hasEmissive = mat->GetEmissiveMap() != nullptr;
    opts.mGlowMap = hasEmissive;
    opts.mIntensify = mat->GetIntensify();
    int texGenVal;
    switch (mat->GetTexGen()) {
    case kTexGenSphere:
        texGenVal = 1;
        break;
    case kTexGenProjected:
        texGenVal = 2;
        break;
    case kTexGenEnviron:
        texGenVal = 3;
        break;
    default:
        texGenVal = 0;
        break;
    }
    opts.mTexGen = texGenVal;
    bool fadeOut;
    if (b) {
        fadeOut = mat->FadeOut();
    } else {
        fadeOut = env->FadeOut() && env->FadeEnd() != env->FadeStart();
    }
    u64 pseudoHDR;
    if (!fadeOut) {
        bool offscreen;
        if (b) {
            offscreen = TheShaderMgr.GetUnk41();
        } else {
            offscreen = TheNgRnd.Offscreen();
        }
        if (!offscreen && mat->AllowHDR()) {
            pseudoHDR = 1;
        } else {
            pseudoHDR = 0;
        }
    } else {
        pseudoHDR = 0;
    }
    opts.mPseudoHDR = pseudoHDR;
    opts.mFog = mat->AllowFog() && mat->GetFog();
    opts.mBillboard = s == kMultimeshBBShader;
    bool colorAdjust;
    if (b) {
        colorAdjust = mat->ColorAdjust();
    } else {
        colorAdjust = env->UseColorAdjust();
    }
    opts.mColorXfm = colorAdjust;
    opts.mColorMod = mat->GetColorModFlags();
    opts.mCustomVariation = mat->GetShaderVariation();
    if (!opts.mPrelit && TheShaderMgr.UseAO() && env->AOEnabled()
        && env->AOStrength() > 0.003f) {
        opts.mEnableAO = 1;
    }
    opts.mToneMapping = env->UseToneMapping();
    if (fadeOut) {
        Vector4 fadeParams(mat->unk2d8, mat->unk2dc, mat->unk2e0, mat->unk2e4);
        TheShaderMgr.SetPConstant((PShaderConstant)0x68, fadeParams);
        opts.mFadeOut = mat->unk2d4;
    }
    CheckDistortionOpts((RndMat *)mat, opts);
    bool recvProjLights = mat->GetRecvProjLights() && env->NumLights_Proj() > 0;
    opts.mNumProj = recvProjLights ? env->NumLights_Proj() : 0;
    opts.mShowShaderCost = TheRnd.ResourceCached();
    opts.mHiResScreen = TheHiResScreen.IsActive();
    return opts.flags;
}

u64 RndShaderStandard::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    NgEnviron *env = (NgEnviron *)RndEnviron::Current();
    bool skinned = TheShaderMgr.BoneCount() != 0;
    ShaderOptions opts(0);
    opts.mSkinned = skinned;
    if (TheRnd.DrawMode() == Rnd::kDrawOcclusion)
        return opts.flags;
    if (TheRnd.DrawMode() == Rnd::kDrawShadowDepth) {
        bool hasDiffuse = mat->GetDiffuseTex() != nullptr;
        opts.mDiffuseMap = hasDiffuse;
        opts.mPrelit = mat->Prelit();
        if (mat->UseEnviron()) {
            opts.mFastCheapLighting = 1;
        }
        return opts.flags;
    }
    bool fadeOut;
    if (b) {
        fadeOut = mat->FadeOut();
    } else {
        fadeOut = env->FadeOut() && env->FadeEnd() != env->FadeStart();
    }
    u64 pseudoHDR;
    if (mat->AllowHDR() && !fadeOut) {
        bool offscreen;
        if (b) {
            offscreen = TheShaderMgr.GetUnk41();
        } else {
            offscreen = TheNgRnd.Offscreen();
        }
        if (!offscreen) {
            pseudoHDR = 1;
        } else {
            pseudoHDR = 0;
        }
    } else {
        pseudoHDR = 0;
    }
    bool hasDiffuse = mat->GetDiffuseTex() != nullptr;
    opts.mDiffuseMap = hasDiffuse;
    opts.mPrelit = mat->Prelit();
    opts.mPseudoHDR = pseudoHDR;
    opts.mRealLights = mat->UseEnviron() && env->NumLights_Real() > 0;
    opts.mApproxLights = mat->UseEnviron() && env->NumLights_Approx() > 0;
    if (opts.mRealLights || opts.mApproxLights) {
        opts.mSpecular = mat->GetSpecularRGB().Pack() != 0;
        if (TheShaderMgr.AllowPerPixel() && mat->GetPerPixelLit()) {
            int hasNormal = mat->NormalMap() != nullptr;
            opts.mNormalMap = hasNormal;
            opts.mPerPixelLighting = 1;
            opts.mNormDetail = mat->GetNormDetailMap() != nullptr
                && mat->GetNormDetailStrength() > 0.0f;
            bool flipNormal = mat->GetCull() == kCullBackwards;
            opts.mFlipNormal = flipNormal;
            opts.mSpecularMap = opts.mSpecular && mat->GetSpecularMap() != nullptr;
            opts.mRimLight = mat->GetRimRGB().Pack() != 0;
            opts.mRimLightUnder = opts.mRimLight && mat->GetRimLightUnder();
            opts.mRimLightMap = opts.mRimLight && mat->GetRimMap() != nullptr;
            int hasShadowMap = TheRnd.GetShadowMap() != nullptr;
            opts.mShadowBuffer = hasShadowMap;
        }
        if (mat->GetEnvironMap() != nullptr) {
            opts.mEnvironMapFalloff = mat->GetEnvironMapFalloff();
            opts.mEnvironMap = 1;
            opts.mEnvironMapSpecMask = opts.mSpecularMap && mat->GetEnvironMapSpecMask();
        }
        bool recvProjLights = mat->GetRecvProjLights() && env->NumLights_Proj() > 0;
        bool pointCubeTex = mat->GetRecvPointCubeTex() && env->NumLights_Point() > 0
            && env->HasPointCubeTex();
        opts.mAnisotropic = mat->GetAnisotropy() > 0.0f;
        opts.mNumPoint = env->NumLights_Point();
        opts.mNumProj = recvProjLights ? env->NumLights_Proj() : 0;
        opts.mProjLightMultiply = recvProjLights && env->GetProjectedBlend() == 1;
        opts.mPointCubeTex = pointCubeTex;
    }
    if (mat->GetRefractEnabled(b) && mat->GetRefractNormalMap() != nullptr) {
        opts.mRefractWorld = 1;
    }
    bool hasEmissive = mat->GetEmissiveMap() != nullptr;
    opts.mGlowMap = hasEmissive;
    opts.mScreenAligned = mat->GetScreenAligned();
    opts.mIntensify = mat->GetIntensify();
    int texGenVal;
    switch (mat->GetTexGen()) {
    case kTexGenSphere:
        texGenVal = 1;
        break;
    case kTexGenProjected:
        texGenVal = 2;
        break;
    case kTexGenEnviron:
        texGenVal = 3;
        break;
    default:
        texGenVal = 0;
        break;
    }
    opts.mTexGen = texGenVal;
    opts.mFog = mat->AllowFog() && mat->GetFog();
    opts.mBillboard = s == kStandardBBShader;
    bool colorAdjust;
    if (b) {
        colorAdjust = mat->ColorAdjust();
    } else {
        colorAdjust = env->UseColorAdjust();
    }
    opts.mColorXfm = colorAdjust;
    opts.mColorMod = mat->GetColorModFlags();
    opts.mCustomVariation = mat->GetShaderVariation();
    if (!opts.mPrelit && TheShaderMgr.UseAO() && env->AOEnabled()
        && env->AOStrength() > 0.003f) {
        opts.mEnableAO = 1;
    }
    opts.mToneMapping = env->UseToneMapping();
    if (fadeOut && !opts.mFog) {
        Vector4 fadeParams(mat->unk2d8, mat->unk2dc, mat->unk2e0, mat->unk2e4);
        TheShaderMgr.SetPConstant((PShaderConstant)0x68, fadeParams);
        opts.mFadeOut = mat->unk2d4;
    }
    CheckDistortionOpts((RndMat *)mat, opts);
    opts.mShowShaderCost = TheRnd.ResourceCached();
    opts.mHiResScreen = TheHiResScreen.IsActive();
    return opts.flags;
}

u64 RndShaderPostProc::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    bool v2e = TheShaderMgr.unk2e;
    bool v25 = TheShaderMgr.unk25;
    bool v2a = TheShaderMgr.unk2a;
    bool v39 = TheShaderMgr.unk39;
    bool v3d = TheShaderMgr.unk3d;
    bool v3f = TheShaderMgr.unk3f;
    bool v29 = TheShaderMgr.unk29;
    bool v2d = TheShaderMgr.unk2d;
    bool v26 = TheShaderMgr.unk26;
    bool v27 = TheShaderMgr.unk27;
    bool v28 = TheShaderMgr.unk28;
    bool v2f = TheShaderMgr.unk2f;
    bool v30 = TheShaderMgr.unk30;
    bool v2c = TheShaderMgr.unk2c;
    bool v31 = TheShaderMgr.unk31;
    bool v2b = TheShaderMgr.unk2b;
    uint v34 = TheShaderMgr.unk34;
    bool v38 = TheShaderMgr.unk38;
    bool v3a = TheShaderMgr.unk3a;
    bool v3b = TheShaderMgr.unk3b;
    bool v3c = TheShaderMgr.unk3c;
    bool v3e = TheShaderMgr.unk3e;
    TheShaderMgr.unk29 = false;
    TheShaderMgr.unk2d = false;
    TheShaderMgr.unk2e = false;
    TheShaderMgr.unk26 = false;
    TheShaderMgr.unk27 = false;
    TheShaderMgr.unk28 = false;
    TheShaderMgr.unk2f = false;
    TheShaderMgr.unk30 = false;
    TheShaderMgr.unk2c = false;
    TheShaderMgr.unk31 = false;
    TheShaderMgr.unk25 = false;
    TheShaderMgr.unk2b = false;
    TheShaderMgr.unk38 = false;
    TheShaderMgr.unk39 = false;
    TheShaderMgr.unk3a = false;
    TheShaderMgr.unk2a = false;
    TheShaderMgr.unk3b = false;
    TheShaderMgr.unk3c = false;
    TheShaderMgr.unk3d = false;
    TheShaderMgr.unk34 = 0;
    TheShaderMgr.unk3e = false;
    TheShaderMgr.unk3f = false;
    return ((((((((((((((((((((((((u64)(v2a & 1) << 10
        | (u64)(TheHiResScreen.IsActive() & 1)) << 1 | (u64)(v25 & 1)) << 4 | (u64)(v2e & 1))
        << 2 | (u64)(v3f & 1)) << 2 | (u64)(v3d & 1)) << 1 | (u64)(v39 & 1)) << 5
        | (u64)(v28 & 1)) << 1 | (u64)(v3e & 1)) << 0xb
        | (u64)(v3a & 1)) << 1 | (u64)(v38 & 1)) << 2
        | (u64)(v34 & 3)) << 1 | (u64)(v29 & 1)) << 6 | (u64)(v3c & 1)) << 1
        | (u64)(v31 & 1)) << 6 | (u64)(v3b & 1)) << 1 | (u64)(v2c & 1)) << 1 | (u64)(v30 & 1))
        << 1 | (u64)(v2f & 1)) << 1 | (u64)(v27 & 1)) << 1 | (u64)(v26 & 1)) << 1
        | (u64)(v2d & 1)) << 1 | (u64)(v2b & 1)) << 1);
}

u64 RndShaderFur::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    NgEnviron *env = (NgEnviron *)RndEnviron::Current();
    bool skinned = TheShaderMgr.BoneCount() != 0;
    ShaderOptions opts(0);
    opts.mSkinned = skinned;
    if (TheRnd.DrawMode() == Rnd::kDrawOcclusion)
        return opts.flags;
    bool hasDiffuse = mat->GetDiffuseTex() != nullptr;
    opts.mDiffuseMap = hasDiffuse;
    opts.mPrelit = mat->Prelit();
    opts.mRealLights = mat->UseEnviron() && env->NumLights_Real() > 0;
    opts.mApproxLights = mat->UseEnviron() && env->NumLights_Approx() > 0;
    if (opts.mRealLights || opts.mApproxLights) {
        if (TheShaderMgr.AllowPerPixel() && mat->GetPerPixelLit()) {
            // fur is never per-pixel lit; the target clears the bit here anyway
            opts.mPerPixelLighting = 0;
            int hasShadowMap = TheRnd.GetShadowMap() != nullptr;
            opts.mShadowBuffer = hasShadowMap;
        }
        bool recvProjLights = mat->GetRecvProjLights() && env->NumLights_Proj() > 0;
        bool pointCubeTex = mat->GetRecvPointCubeTex() && env->NumLights_Point() > 0
            && env->HasPointCubeTex();
        opts.mAnisotropic = mat->GetAnisotropy() > 0.0f;
        opts.mNumPoint = env->NumLights_Point();
        opts.mNumProj = recvProjLights ? env->NumLights_Proj() : 0;
        opts.mProjLightMultiply = recvProjLights && env->GetProjectedBlend() == 1;
        opts.mPointCubeTex = pointCubeTex;
    }
    opts.mScreenAligned = mat->GetScreenAligned();
    bool fog;
    if (b) {
        fog = mat->GetFog();
    } else {
        fog = env->FogEnable();
    }
    opts.mFog = fog && env->FogEnable();
    bool colorAdjust;
    if (b) {
        colorAdjust = mat->ColorAdjust();
    } else {
        colorAdjust = env->UseColorAdjust();
    }
    opts.mColorXfm = colorAdjust;
    RndFur *fur = mat->GetFur();
    bool furDetail = fur && fur->GetFurDetail();
    opts.mFurDetail = furDetail;
    bool fadeOut;
    if (b) {
        fadeOut = mat->FadeOut();
    } else {
        fadeOut = env->FadeOut() && env->FadeEnd() != env->FadeStart();
    }
    if (fadeOut && !opts.mFog) {
        Vector4 fadeParams(mat->unk2d8, mat->unk2dc, mat->unk2e0, mat->unk2e4);
        TheShaderMgr.SetPConstant((PShaderConstant)0x68, fadeParams);
        opts.mFadeOut = mat->unk2d4;
    }
    opts.mShowShaderCost = TheRnd.ResourceCached();
    opts.mHiResScreen = TheHiResScreen.IsActive();
    return opts.flags;
}

u64 RndShaderSyncTrack::CalcShaderOpts(NgMat *mat, ShaderType s, bool b) {
    NgEnviron *env = (NgEnviron *)RndEnviron::Current();
    if (TheRnd.DrawMode() == Rnd::kDrawOcclusion)
        return 0;
    bool fadeOut;
    if (b) {
        fadeOut = mat->FadeOut();
    } else {
        fadeOut = env->FadeOut() && env->FadeEnd() != env->FadeStart();
    }
    u64 pseudoHDR;
    if (mat->AllowHDR() && !fadeOut) {
        bool offscreen;
        if (b) {
            offscreen = TheShaderMgr.GetUnk41();
        } else {
            offscreen = TheNgRnd.Offscreen();
        }
        if (!offscreen) {
            pseudoHDR = 1;
        } else {
            pseudoHDR = 0;
        }
    } else {
        pseudoHDR = 0;
    }
    ShaderOptions opts(0);
    bool hasDiffuse = mat->GetDiffuseTex() != nullptr;
    opts.mDiffuseMap = hasDiffuse;
    opts.mPrelit = mat->Prelit();
    opts.mPseudoHDR = pseudoHDR;
    opts.mRealLights = mat->UseEnviron() && env->NumLights_Real() > 0;
    opts.mApproxLights = mat->UseEnviron() && env->NumLights_Approx() > 0;
    if (opts.mRealLights || opts.mApproxLights) {
        opts.mSpecular = mat->GetSpecularRGB().Pack() != 0;
        if (TheShaderMgr.AllowPerPixel() && mat->GetPerPixelLit()) {
            int hasNormal = mat->NormalMap() != nullptr;
            opts.mNormalMap = hasNormal;
            opts.mPerPixelLighting = 1;
            opts.mNormDetail = mat->GetNormDetailMap() != nullptr
                && mat->GetNormDetailStrength() > 0.0f;
            bool flipNormal = mat->GetCull() == kCullBackwards;
            opts.mFlipNormal = flipNormal;
            opts.mSpecularMap = opts.mSpecular && mat->GetSpecularMap() != nullptr;
            opts.mRimLight = mat->GetRimRGB().Pack() != 0;
            opts.mRimLightUnder = opts.mRimLight && mat->GetRimLightUnder();
            opts.mRimLightMap = opts.mRimLight && mat->GetRimMap() != nullptr;
            int hasShadowMap = TheRnd.GetShadowMap() != nullptr;
            opts.mShadowBuffer = hasShadowMap;
        }
        if (mat->GetEnvironMap() != nullptr) {
            opts.mEnvironMapFalloff = mat->GetEnvironMapFalloff();
            opts.mEnvironMap = 1;
            opts.mEnvironMapSpecMask = opts.mSpecularMap && mat->GetEnvironMapSpecMask();
        }
        bool recvProjLights = mat->GetRecvProjLights() && env->NumLights_Proj() > 0;
        bool pointCubeTex = mat->GetRecvPointCubeTex() && env->NumLights_Point() > 0
            && env->HasPointCubeTex();
        opts.mAnisotropic = mat->GetAnisotropy() > 0.0f;
        opts.mNumPoint = env->NumLights_Point();
        opts.mNumProj = recvProjLights ? env->NumLights_Proj() : 0;
        opts.mProjLightMultiply = recvProjLights && env->GetProjectedBlend() == 1;
        opts.mPointCubeTex = pointCubeTex;
    }
    if (mat->GetRefractEnabled(b) && mat->GetRefractNormalMap() != nullptr) {
        opts.mRefractWorld = 1;
    }
    bool hasEmissive = mat->GetEmissiveMap() != nullptr;
    opts.mGlowMap = hasEmissive;
    opts.mScreenAligned = mat->GetScreenAligned();
    opts.mIntensify = mat->GetIntensify();
    int texGenVal;
    switch (mat->GetTexGen()) {
    case kTexGenSphere:
        texGenVal = 1;
        break;
    case kTexGenProjected:
        texGenVal = 2;
        break;
    case kTexGenEnviron:
        texGenVal = 3;
        break;
    default:
        texGenVal = 0;
        break;
    }
    opts.mTexGen = texGenVal;
    opts.mFog = mat->AllowFog() && mat->GetFog();
    // the sync-track shaders are never billboarded
    opts.mBillboard = 0;
    bool colorAdjust;
    if (b) {
        colorAdjust = mat->ColorAdjust();
    } else {
        colorAdjust = env->UseColorAdjust();
    }
    opts.mColorXfm = colorAdjust;
    opts.mColorMod = mat->GetColorModFlags();
    opts.mCustomVariation = mat->GetShaderVariation();
    if (!opts.mPrelit && TheShaderMgr.UseAO() && env->AOEnabled()
        && env->AOStrength() > 0.003f) {
        opts.mEnableAO = 1;
    }
    opts.mToneMapping = env->UseToneMapping();
    if (fadeOut && !opts.mFog) {
        Vector4 fadeParams(mat->unk2d8, mat->unk2dc, mat->unk2e0, mat->unk2e4);
        TheShaderMgr.SetPConstant((PShaderConstant)0x68, fadeParams);
        opts.mFadeOut = mat->unk2d4;
    }
    opts.mShowShaderCost = TheRnd.ResourceCached();
    opts.mHiResScreen = TheHiResScreen.IsActive();
    opts.mFitToSpline = 1;
    if (RndSpline::sGlobalDefaultSpline != nullptr) {
        opts.mSplinePulse = RndSpline::sGlobalDefaultSpline->mPulseDrawing;
    }
    opts.mSyncTrackChargeEffect = s == kSyncTrackChargeEffectShader;
    return opts.flags;
}

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

void RndShaderStandard::Select(RndMat *mat, ShaderType shader_type, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    bool skinned = TheShaderMgr.BoneCount() != 0;
    if (!RedundantState(mat, shader_type, skinned, TheShaderMgr.UseAO(), b)) {
        TheNgStats->mMats++;
        ((NgMat *)mat)->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        CheckShadow();
        ShaderOptions opts(CalcShaderOpts((NgMat *)mat, shader_type, b));
        MILO_ASSERT((shader_type == kStandardShader) || (shader_type == kStandardBBShader) || (shader_type == kAllWhiteShader), 0x4BB);
        if (shader_type == kStandardBBShader) {
            shader_type = kStandardShader;
        }
        SetColorWriteMask(opts, mat);
        CheckExtrude();
        CheckForceCull(shader_type);
        CheckDistortion(mat);
        Cache(shader_type, opts, mat);
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
        const Hmx::Color &color = mat->GetColor();
        TheShaderMgr.SetVConstant(kVS_AmbientColor, Vector4(color.red, color.green, color.blue, color.alpha));
        TheShaderMgr.SetPConstant(kPS_AmbientColor, Vector4(color.red, color.green, color.blue, color.alpha));
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
            if (TheShaderMgr.unk24) {
                TheRenderState.SetBlendOp((RndRenderState::BlendOp)4);
            } else {
                TheRenderState.SetBlendOp((RndRenderState::BlendOp)0);
            }
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

void RndShaderSyncTrack::Select(RndMat *mat, ShaderType shader_type, bool b) {
    if (!mat) mat = TheRnd.DefaultMat();
    TheRenderState.SetFillMode((RndRenderState::FillMode)0);
    if (!RedundantState(mat, shader_type, TheShaderMgr.BoneCount() != 0, TheShaderMgr.UseAO(), b)) {
        TheNgStats->mMats++;
        NgMat *ngMat = static_cast<NgMat *>(mat);
        ngMat->SetupShader(TheShaderMgr.AllowPerPixel(), true);
        CheckShadow();
        ShaderOptions opts(CalcShaderOpts(ngMat, shader_type, b));
        MILO_ASSERT((shader_type == kSyncTrackShader) || (shader_type == kSyncTrackChargeEffectShader), 0x749);
        if (shader_type == kSyncTrackChargeEffectShader) {
            shader_type = kSyncTrackShader;
        }
        SetColorWriteMask(opts, mat);
        CheckExtrude();
        CheckForceCull(shader_type);
        Cache(shader_type, opts, mat);
    }
}
