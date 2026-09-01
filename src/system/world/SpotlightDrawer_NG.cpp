#include "world\SpotlightDrawer_NG.h"
#include "macros.h"
#include "math\Color.h"
#include "math\Mtx.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\Timer.h"
#include "rnddx9\RenderState.h"
#include "rnddx9\Rnd.h"
#include "rnddx9\Tex.h"
#include "rndobj/Cam.h"
#include "rndobj\HiResScreen.h"
#include "rndobj\Rnd.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\ShaderMgr.h"
#include "utl/Loader.h"
#include "world\Dir.h"
#include "world\Spotlight.h"
#include "world\SpotlightDrawer.h"
#include <math.h>

NgSpotlightDrawer::SpotlightResources *NgSpotlightDrawer::sSharedResources;
// Target: SpotlightDrawer_NG.obj .bss:0x4 (0x8311692C) = 0, right after sSharedResources.
bool NgSpotlightDrawer::sActiveFrame;

// The tuning scalars below live as function-local statics on purpose -- do not
// hoist them back to file scope.  MSVC-PPC emits every file-scope initialised
// definition into the object's single plain `.data` section, which the linker
// always lays out BEFORE that object's `.data` COMDATs; a function-local static
// instead gets its own `.data` COMDAT, emitted next to its function.  Retail's
// contribution for this TU (`orig/373307D9/ham_xbox_r.map`) starts at the split
// range base with `??_R0?AVSpotlightResources@NgSpotlightDrawer@@@8`, so retail
// had no file-scope initialised data here at all.  Moving the ten scalars into
// their users takes the reconstructed `.data` block from 0x112 to retail's
// exact 0x114 and from 0 to 4 of 6 retail-map anchors, at zero measured cost.

void GetLightPosition(Spotlight *s, Vector3 &v) {
    v = s->WorldXfm().v;
    Vector3 offset;
    Multiply(s->mBeam.mBeam->LocalXfm().v, s->WorldXfm().m, offset);
    float vx = v.x, vy = v.y, vz = v.z;
    v.x = vx + offset.x;
    v.y = vy + offset.y;
    v.z = vz + offset.z;
}

NgSpotlightDrawer::NgSpotlightDrawer()
    : mSpotCam(), mSavedCam(this), mFogDensityMap(0), unkb0(false) {
    mSpotCam = Hmx::Object::New<RndCam>();
}

NgSpotlightDrawer::~NgSpotlightDrawer() { RELEASE(mSpotCam); }

NgSpotlightDrawer::SpotlightResources::~SpotlightResources() {
    Clear();
}

void NgSpotlightDrawer::EndWorld() {
    if (SpotlightDrawer::sNeedDraw) {
        CheckCam();
    }
    SpotlightDrawer::EndWorld();
}

void NgSpotlightDrawer::DoPost() { RenderScene(); }

void NgSpotlightDrawer::SetAmbientColor(const Hmx::Color &color) {
    Hmx::Color c = color;
    sEnviron->SetAmbientColor(color);
    float r = c.red;
    float g = c.green;
    float b = c.blue;
    float a = c.alpha;
    TheShaderMgr.SetVConstant(kVS_AmbientColor, Vector4(r, g, b, a));
    TheShaderMgr.SetPConstant(kPS_AmbientColor, Vector4(r, g, b, a));
}

void NgSpotlightDrawer::ClearPostDraw() { sNeedDraw = false; }

void NgSpotlightDrawer::ClearPostProc() {
    sLights.resize(0);
    sShadowSpots.resize(0);
    sCans.resize(0);
}

void NgSpotlightDrawer::Init() {
    CheckSharedResources();
    REGISTER_OBJ_FACTORY(NgSpotlightDrawer);
    RELEASE(sDefault);
    sDefault = Hmx::Object::New<SpotlightDrawer>();
    ((SpotDrawParams &)sDefault->Params()).mLightingInfluence = 0;
    sDefault->Select();
}

int NgSpotlightDrawer::RTWidth() {
    if (TheNgRnd.PreProcessTexture()) {
        return TheNgRnd.PreProcessTexture()->Width() >> 1;
    } else {
        return TheNgRnd.Width() >> 1;
    }
}

int NgSpotlightDrawer::RTHeight() {
    if (TheNgRnd.PreProcessTexture()) {
        return TheNgRnd.PreProcessTexture()->Height() >> 1;
    } else {
        return TheNgRnd.Height() >> 1;
    }
}

void NgSpotlightDrawer::SpotlightResources::Clear() {
    if (unk4) {
        D3DResource_Release(unk4);
        unk4 = nullptr;
    }
    RELEASE(unk8);
    RELEASE(mDensityMap);
    unk18 = nullptr;
}



#ifdef HX_NATIVE
void NgSpotlightDrawer::SetupFogDensityMap() {}
void NgSpotlightDrawer::RenderFogProxy() {}
void NgSpotlightDrawer::RenderSphere(Spotlight *) {}
void NgSpotlightDrawer::RenderSheet(Spotlight *) {}
bool NgSpotlightDrawer::CheckRTs(NgSpotlightDrawer::SpotlightResources *) { return false; }
void NgSpotlightDrawer::SetupXSection(Spotlight *, const Spotlight::BeamDef &) {}
void NgSpotlightDrawer::RenderConeDefs(Spotlight *, const Hmx::Color &) {}
void NgSpotlightDrawer::RenderCone(Spotlight *) {}
void NgSpotlightDrawer::RenderBeams(const Hmx::Matrix4 &) {}
bool NgSpotlightDrawer::CheckCam() { return true; }
void NgSpotlightDrawer::BlurRT(float, float) {}
void NgSpotlightDrawer::SetupForPostProcess() {}
void NgSpotlightDrawer::RenderScene() {}
#endif

#ifndef HX_NATIVE
bool NgSpotlightDrawer::CheckCam() {
    mSavedCam = RndCam::Current();
    RndCam *cam;
    if (TheLoadMgr.EditMode()) {
        cam = RndCam::Current();
    } else if (TheWorld && TheWorld->Cam()) {
        cam = TheWorld->Cam();
    } else if (RndCam::Current()) {
        cam = RndCam::Current();
    } else {
        cam = TheRnd.GetDefaultCam();
    }
    mSpotCam->Copy(cam, Hmx::Object::kCopyShallow);
    mSpotCam->SetTransParent(nullptr, false);
    return true;
}

void NgSpotlightDrawer::RenderCone(Spotlight *sl) {
    static float sBeamIntensity = 8.0f;
    MILO_ASSERT(sl->HasBeam(), 0x45d);
    Spotlight *colorOwner = sl->mColorOwner;
    float scale = colorOwner->mIntensity * sBeamIntensity;
    Hmx::Color color(
        colorOwner->mColor.red * scale,
        colorOwner->mColor.green * scale,
        colorOwner->mColor.blue * scale,
        colorOwner->mColor.alpha * scale
    );
    if (!sl->mAnimateColorFromPreset && sl->mBeam.mMat) {
        const Hmx::Color &matColor = sl->mBeam.mMat->GetColor();
        color.red = matColor.red * color.red;
        color.green = matColor.green * color.green;
        color.blue = matColor.blue * color.blue;
        color.alpha = matColor.alpha * color.alpha;
    }
    RenderConeDefs(sl, color);
}

void NgSpotlightDrawer::RenderSphere(Spotlight *sl) {
    static float sBeamBrighten = 2.0f;
    static float sSphereScale = 0.5f;
    MILO_ASSERT(sl->HasBeam(), 0x470);
    float zero = 0.0f;
    Vector4 sphereParams(zero, zero, 0.625f, sl->mBeam.mTopRadius * sSphereScale);
    TheShaderMgr.SetPConstant((PShaderConstant)0x5b, sphereParams);

    Spotlight *colorOwner = sl->mColorOwner;
    float intensity = (colorOwner->mIntensity * (sl->mBeam.mBrighten * sBeamBrighten));
    float g = colorOwner->mColor.green * intensity;
    float a = colorOwner->mColor.alpha * intensity;
    float r = colorOwner->mColor.red * intensity;
    float b = colorOwner->mColor.blue * intensity;

    if (!sl->mAnimateColorFromPreset && sl->mBeam.mMat) {
        const Hmx::Color &matColor = sl->mBeam.mMat->GetColor();
        r = r * matColor.red;
        g = matColor.green * g;
        b = matColor.blue * b;
        a = matColor.alpha * a;
    }

    TheShaderMgr.mCullModeOverride = 1;
    Vector4 colorVec(r, g, b, a);
    TheShaderMgr.SetPConstant((PShaderConstant)0x5a, colorVec);

    SetXSectionTexture(sl->mBeam);
    sl->mBeam.mBeam->DrawShowing();
}

void NgSpotlightDrawer::RenderSheet(Spotlight *sl) {
    static float sSheetIntensity = 8.0f;
    static float sSheetW = 0.5f;
    SetXSectionTexture(sl->mBeam);

    float brighten = sl->mBeam.mBrighten;
    const Transform &camXfm = mSpotCam->WorldXfm();

    Vector4 camPos(camXfm.v.x, camXfm.v.y, camXfm.v.z, 1.0f);
    TheShaderMgr.SetPConstant((PShaderConstant)0xa, camPos);

    const Transform &slXfm = sl->WorldXfm();
    Vector4 lightDir(slXfm.m.z.x, slXfm.m.z.y, slXfm.m.z.z, sSheetW);
    TheShaderMgr.SetPConstant((PShaderConstant)0x5b, lightDir);

    Spotlight *colorOwner = sl->mColorOwner;
    float intensity = colorOwner->mIntensity * sSheetIntensity;
    float r = colorOwner->mColor.red * intensity;
    float g = colorOwner->mColor.green * intensity;
    float b = colorOwner->mColor.blue * intensity;

    if (!sl->mAnimateColorFromPreset && sl->mBeam.mMat) {
        RndMat *mat = sl->mBeam.mMat;
        r = mat->GetColor().red * r;
        g = mat->GetColor().green * g;
        b = mat->GetColor().blue * b;
    }

    Vector4 colorVec(r * brighten, g * brighten, b * brighten, 1.0f);
    TheShaderMgr.SetPConstant((PShaderConstant)0x5a, colorVec);

    int prevUnlit = TheShaderMgr.CullModeOverride();
    TheShaderMgr.mCullModeOverride = 1;
    sl->mBeam.mBeam->DrawShowing();
    TheShaderMgr.mCullModeOverride = prevUnlit;
}

void NgSpotlightDrawer::RenderBeams(const Hmx::Matrix4 &viewProj) {
    TheShaderMgr.mInDepthVolume = 1;
    D3DDevice_SetDepthStencilSurface(TheDxRnd.Device(), 0);

    SpotlightResources &sr = SR();
    TheShaderMgr.SetPConstant((PShaderConstant)0xc, sr.unk18);

    SetupFogDensityState();

    SpotlightEntry *it = sLights.begin();
    SpotlightEntry *itEnd = sLights.end();
    if (it != itEnd) {
        float zero = 0.0f;
        do {
            Spotlight *sl = it->mSpotlight;
            if (sl->mBeam.mLength > zero) {
                unsigned int shape = sl->mBeam.mShape;
                int shaderShape;
                if (shape < 2) {
                    shaderShape = 0;
                } else if (shape == 2) {
                    shaderShape = 1;
                } else {
                    if (shape >= 5) {
                        MILO_ASSERT(false, 0x456);
                    }
                    shaderShape = 2;
                }
                TheShaderMgr.unk1c = shaderShape;

                int shapeVal = sl->mBeam.mShape;
                if (shapeVal == 2) {
                    RenderSphere(sl);
                } else if (shapeVal < 3 || shapeVal > 4) {
                    RenderSheet(sl);
                } else {
                    RenderCone(sl);
                }

                TheShaderMgr.SetPConstant((PShaderConstant)0xc, sr.unk18);
            }
            it++;
        } while (it != itEnd);
    }

    TheShaderMgr.mCullModeOverride = 0;
    SetupFogDensityMap();
    TheShaderMgr.SetVConstant((VShaderConstant)4, viewProj);
    TheShaderMgr.SetPConstant((PShaderConstant)0xc, (RndTex *)0);
    TheShaderMgr.mInDepthVolume = 0;
}

void NgSpotlightDrawer::RenderConeDefs(Spotlight *sl, const Hmx::Color &color) {
    TheShaderMgr.mCullModeOverride = 3;
    TheShaderMgr.unk24 = 0;

    RndMesh *beam = sl->mBeam.mBeam;
    if (beam && sl->mBeam.mLength > 0.0f) {
        float brighten = sl->mBeam.mBrighten;
        Vector4 colorVec(
            color.red * brighten, color.green * brighten, color.blue * brighten, 1.0f
        );
        TheShaderMgr.SetPConstant((PShaderConstant)0x5a, colorVec);

        SetupXSection(sl, sl->mBeam);

        const Transform &camXfm = mSpotCam->WorldXfm();
        Vector3 camPos = camXfm.v;
        const Transform &camXfm2 = mSpotCam->WorldXfm();
        Vector3 camUp = camXfm2.m.y;

        Vector4 camPosVec(camPos.x, camPos.y, camPos.z, 1.0f);
        TheShaderMgr.SetPConstant((PShaderConstant)0xa, camPosVec);

        float dotProduct = -(camUp.x * camPos.x + camUp.y * camPos.y + camUp.z * camPos.z);
        Vector4 camPlane(camUp.x, camUp.y, camUp.z, dotProduct);
        TheShaderMgr.SetPConstant((PShaderConstant)0x1e, camPlane);

        float farPlane = mSpotCam->FarPlane();
        float zero = 0.0f;
        float invFarPlane = zero;
        if (zero < farPlane) {
            invFarPlane = 1.0f / farPlane;
        }

        Vector4 fogParams(mParams.mHalfDistance, invFarPlane, zero, zero);
        TheShaderMgr.SetPConstant((PShaderConstant)0x5b, fogParams);

        Vector3 lightPos;
        GetLightPosition(sl, lightPos);

        const Transform &slXfm = sl->WorldXfm();
        float dirX = slXfm.m.y.x;
        float dirY = slXfm.m.y.y;
        float dirZ = slXfm.m.y.z;

        Vector2 radii = sl->mBeam.NGRadii();
        float topRad = radii.x;
        float botRad = radii.y;
        float minRad = (topRad - botRad) < 0.0f ? topRad : botRad;

        float offset = 0.0f;
        if (0.0f < botRad) {
            offset = (minRad * sl->mBeam.mLength) / (botRad - minRad);
        }

        float negOffset = -offset;
        float totalLength = offset + sl->mBeam.mLength;
        float invTotalLength = 1.0f / totalLength;

        float apexX = lightPos.x + dirX * negOffset;
        float apexY = lightPos.y + dirY * negOffset;
        float apexZ = lightPos.z + dirZ * negOffset;

        Vector4 apex(apexX, apexY, apexZ, invTotalLength);
        TheShaderMgr.SetPConstant((PShaderConstant)0x19, apex);

        const Transform &slXfm2 = sl->WorldXfm();
        Vector4 direction(slXfm2.m.y.x, slXfm2.m.y.y, slXfm2.m.y.z, totalLength);
        TheShaderMgr.SetPConstant((PShaderConstant)0x1a, direction);

        const Transform &camXfm3 = mSpotCam->WorldXfm();
        Vector3 relCam = camXfm3.v;

        float relX = relCam.x - apexX;
        float relY = relCam.y - apexY;
        float relZ = relCam.z - apexZ;

        Vector4 relCamPos(relX, relY, relZ, 1.0f);
        TheShaderMgr.SetPConstant((PShaderConstant)0x1b, relCamPos);

        Vector4 radiiVec(
            minRad, botRad,
            dirX * apexX + dirY * apexY + dirZ * apexZ,
            dirX * apexX + dirY * apexY + dirZ * apexZ + totalLength
        );
        TheShaderMgr.SetPConstant((PShaderConstant)0x1d, radiiVec);

        float radiusDiff = botRad - minRad;
        float dotRelDir = dirX * relX + dirY * relY + dirZ * relZ;
        float tanSlope = invTotalLength * radiusDiff;

        float shift = 0.0f;
        if (radiusDiff != 0.0f) {
            shift = (minRad / radiusDiff) * totalLength;
        }

        float extProj = shift + dotRelDir;
        float cosAngle = (float)cos((float)atan(invTotalLength * botRad));

        Vector4 coneParams(
            tanSlope * tanSlope + 1.0f,
            extProj * tanSlope * tanSlope + dotRelDir,
            -(extProj * extProj * tanSlope * tanSlope -
              -(dotRelDir * dotRelDir - (relX * relX + relY * relY + relZ * relZ))),
            cosAngle * cosAngle
        );
        TheShaderMgr.SetPConstant((PShaderConstant)0x1c, coneParams);

        beam->DrawShowing();
    }
}

void NgSpotlightDrawer::SetupFogDensityMap() {
    float base = mParams.mBaseIntensity * 0.01f;
    float smoke = mParams.mSmokeIntensity * 0.01f;
    smoke *= 1.0f - base;
    Vector4 fogParams(base, smoke, 0.0f, 0.0f);
    TheShaderMgr.SetPConstant((PShaderConstant)0x7F, fogParams);
}

void NgSpotlightDrawer::SetupForPostProcess() {
    static float sPostIntensityScale = 32.0f;
    Vector4 zero(0.0f, 0.0f, 0.0f, 0.0f);
    TheShaderMgr.SetPConstant((PShaderConstant)0x5A, zero);
    BlurRT();
    float farPlane = mSpotCam->FarPlane();
    float recipFarPlane;
    if (farPlane > 0.0f) {
        recipFarPlane = 1.0f / farPlane;
    } else {
        recipFarPlane = 0.0f;
    }
    Vector4 intensityParams(
        mParams.mIntensity * sPostIntensityScale, 0.0f, 0.0f, recipFarPlane
    );
    TheShaderMgr.SetPConstant((PShaderConstant)0x5B, intensityParams);
    Hmx::Color c = mParams.mColor;
    Vector4 colorVec(c.red, c.green, c.blue, c.alpha);
    TheShaderMgr.SetPConstant((PShaderConstant)0x81, colorVec);
    TheShaderMgr.SetPConstant((PShaderConstant)0xC, SR().unk8);
    TheRenderState.SetTextureFilter(0xC, (RndRenderState::FilterMode)1, false);
    TheRenderState.SetTextureClamp(0xC, (RndRenderState::ClampMode)2);
    TheRenderState.SetTextureFilter(5, (RndRenderState::FilterMode)1, false);
    TheRenderState.SetTextureClamp(5, (RndRenderState::ClampMode)2);
    ClearPostProc();
    sActiveFrame = true;
}

void NgSpotlightDrawer::RenderFogProxy() {
    static float kFogScale = 10.0f;
    RndDrawable *proxy = mParams.mProxy;
    if (proxy) {
        MILO_ASSERT(mFogDensityMap == SR().mDensityMap, 0x400);
        TheShaderMgr.SetPConstant((PShaderConstant)5, (RndTex *)0);
        float nearPlane = mSpotCam->NearPlane();
        Vector4 vsParams(nearPlane, -1.0f / nearPlane, 1.0f, 0.0f);
        TheShaderMgr.SetVConstant((VShaderConstant)0x37, vsParams);
        float farPlane = mSpotCam->FarPlane();
        float recipFarPlane;
        if (farPlane > 0.0f) {
            recipFarPlane = 1.0f / farPlane;
        } else {
            recipFarPlane = 0.0f;
        }
        Vector4 psParams(1.0f / kFogScale, 1.0f, 0.0f, recipFarPlane);
        TheShaderMgr.SetPConstant((PShaderConstant)0x5B, psParams);
        mSpotCam->SetTargetTex(mFogDensityMap);
        mSpotCam->Select();
        proxy->Draw();
        RestoreCam();
        if (mFogDensityMap) {
            TheShaderMgr.SetPConstant((PShaderConstant)5, mFogDensityMap);
            TheRenderState.SetTextureClamp(5, (RndRenderState::ClampMode)2);
        }
    }
}
#endif

void NgSpotlightDrawer::SetupFogDensityState() {
    if (mFogDensityMap) {
        TheShaderMgr.SetPConstant((PShaderConstant)5, mFogDensityMap);
        TheRenderState.SetTextureClamp(5, (RndRenderState::ClampMode)2);
    }

    Hmx::Matrix4 viewProj;
    RndCam::Current()->GetInfiniteViewProj(viewProj);
    TheShaderMgr.SetVConstant((VShaderConstant)4, viewProj);

    float fogDensity;
    float farPlane = mSpotCam->FarPlane();
    if (farPlane > 0.0f) {
        fogDensity = 1.0f / farPlane;
    } else {
        fogDensity = 0.0f;
    }

    Vector4 fogParams(0.0f, fogDensity, 0.0f, 0.0f);
    TheShaderMgr.SetPConstant((PShaderConstant)0x7F, fogParams);
}


void NgSpotlightDrawer::BlurRT() {
    static float sBlurAmount = 1.0f;
    static bool sSeparateBlurPasses = true;
    D3DDevice_SetDepthStencilSurface(TheDxRnd.Device(), 0);
    if (sSeparateBlurPasses) {
        BlurRT(sBlurAmount, 0.0f);
        BlurRT(0.0f, sBlurAmount);
    } else {
        BlurRT(sBlurAmount, sBlurAmount);
    }
}

#ifndef HX_NATIVE
void NgSpotlightDrawer::BlurRT(float amountX, float amountY) {
    float fw = (float)RTWidth();
    float fh = (float)RTHeight();
    Hmx::Rect rect;
    rect.x = 0.0f;
    rect.y = 0.0f;
    rect.w = fw;
    rect.h = fh;

    TheShaderMgr.unk14 = 5;

    float invW = 1.0f / fw;
    float invH = 1.0f / fh;

    float kWeights[] = { 0.1f, 0.25f, 0.3f, 0.25f, 0.1f };

    int i = -2;
    const float *pWeight = kWeights - 1;
    do {
        float fi = (float)(int)i;
        Vector4 offset(fi * invW * amountX, fi * invH * amountY, 1.0f, 1.0f);
        TheShaderMgr.SetPConstant((PShaderConstant)(0x8c + i), offset);

        pWeight++;
        float wt = *pWeight;
        Vector4 weight(wt, wt, wt, wt);
        TheShaderMgr.SetPConstant((PShaderConstant)(0x9c + i), weight);

        i++;
    } while (i <= 2);

    TheRenderState.SetTextureClamp(0, (RndRenderState::ClampMode)2);
    TheRenderState.SetTextureFilter(0, (RndRenderState::FilterMode)1, false);

    SpotlightResources &sr = SR();
    DxTex *tex = (DxTex *)sr.unk8;
    D3DDevice *dev = TheDxRnd.Device();
    D3DSurface *rt = tex->GetRT();
    D3DDevice_SetRenderTarget_External(dev, 0, rt);

    RndMat *workMat = TheShaderMgr.GetWork();
    RndTex *srTex = SR().unk8;
    workMat->SetDiffuseTex(srTex);
    workMat->SetZMode(kZModeDisable);
    workMat->SetTexWrap(kTexWrapClamp);
    workMat->SetBlend(BaseMaterial::kBlendSrc);

    TheNgRnd.DrawRect(rect, workMat, (ShaderType)1, Hmx::Color(), 0, 0);

    D3DDevice_Resolve(TheDxRnd.Device(), 0, 0, tex->Tex(), 0, 0, 0, 0, 1.0f, 0, 0);
    TheShaderMgr.unk14 = 1;
}

void NgSpotlightDrawer::RenderScene() {
    static float sFogScale = 1.0f;
    START_AUTO_TIMER("world_draw");

    sActiveFrame = false;

    int numLights = sLights.end() - sLights.begin();
    if (numLights == 0) {
        // No lights this frame: reset post-process state and bail. The target
        // checks the light count separately from the showing/resource checks
        // (numLights==0 short-circuits to its own inline ClearPostProc + return).
        ClearPostProc();
    } else if (Showing() && CheckSharedResources() && CheckFogTexture()) {
        MILO_ASSERT(sEnviron->GetUseApprox() == false, 0x595);

        sEnviron->Select(0);
        TheShaderMgr.unk25 = 1;
        TheHiResScreen.mOverride = true;

        TheRenderState.SetTextureFilter(9, (RndRenderState::FilterMode)0, false);
        TheRenderState.SetTextureClamp(9, (RndRenderState::ClampMode)2);

        float farPlane = mSpotCam->mFarPlane;
        float nearPlane = mSpotCam->mNearPlane;

        int h = RTHeight();
        float invH = 1.0f / (float)h;
        int w = RTWidth();
        float invW = 1.0f / (float)w;

        Vector4 camParams(nearPlane, farPlane, invW, invH);
        TheShaderMgr.SetPConstant((PShaderConstant)0x82, camParams);

        Vector4 depthRange;
        mSpotCam->GetDepthRangeValues(depthRange);
        TheShaderMgr.SetPConstant((PShaderConstant)0x59, depthRange);

        Vector4 fogParam(sFogScale, sFogScale, sFogScale, sFogScale);
        TheShaderMgr.SetPConstant((PShaderConstant)9, fogParam);

        RenderFogProxy();

        mSpotCam->SetTargetTex(SR().unk8);
        mSpotCam->Select();

        RenderBeams(RndCam::sCurrent->mViewProjMatrix);

        TheRenderState.SetTextureFilter(9, (RndRenderState::FilterMode)0, false);
        TheRenderState.SetTextureClamp(9, (RndRenderState::ClampMode)2);

        RestoreCam();
        TheHiResScreen.mOverride = false;
        SetupForPostProcess();
    } else {
        // Target dispatches the virtual at vtable+0x60 = ClearPostProc (clears the
        // light/shadow/can vectors), not +0x5c = ClearPostDraw (which only clears
        // sNeedDraw). When there are no lights to render (or the showing/resource
        // checks fail) the spotlight post-process state is fully reset here.
        ClearPostProc();
    }
}

namespace {

// Slides a beam corner along `dir` by `scale`.  `dir` arrives by value AND IS
// SCALED IN PLACE -- that is what makes the copy survive.  Xenon MSVC at /O1
// folds an *unmodified* local-to-local 16-byte Vector3 copy unconditionally,
// but a by-value parameter the callee writes to is a distinct object it has to
// materialise.  Each of the five corner sites therefore keeps its own
// lwz/lwz/lwz/lwz + stw/stw/stw/stw run, exactly as retail does.  (The scaled
// stores themselves are dead and get removed, so all five copies can share
// stack slots with one another.)
void SlideCorner(Vector3 &pt, Vector3 dir, float scale) {
    dir *= scale;
    pt += dir;
}

void SlideCornerBack(Vector3 &pt, Vector3 dir, float scale) {
    dir *= scale;
    pt -= dir;
}

// Normal of the plane through the eye and the silhouette edge a..b.  Both
// endpoints arrive by value and are rebased onto the eye in place, for the same
// reason as above -- that keeps both 16-byte copies.
void EyeEdgePlane(Vector3 a, Vector3 b, const Vector3 &eye, Vector3 &dst) {
    a -= eye;
    b -= eye;
    Cross(a, b, dst);
}

void NormalizeCopy(Vector3 v, Vector3 &dst) { Normalize(v, dst); }

// Divides a silhouette plane through by its projection onto the bisector and
// packs it as a shader plane equation.  `n` is by value and scaled in place for
// the same copy-preserving reason as SlideCorner.
void PlaneEquation(Vector3 n, float inv, float d, Vector4 &out) {
    n *= inv;
    out.Set(n.x, n.y, n.z, inv * d);
}

}

// Builds the two silhouette planes of the beam's cross section and hands the
// shader the plane equations (divided through by the plane's projection onto
// the bisector) plus the view-angle visibility fade.
//
// Everything here is Vector3-valued: retail works with whole vectors, copies
// them field-wise into the corner locals, and only drops to scalars for the
// final shader constants.
void NgSpotlightDrawer::SetupXSection(Spotlight *sl, const Spotlight::BeamDef &def) {
    Vector3 lightPos;
    GetLightPosition(sl, lightPos);

    const Transform &camXfm = mSpotCam->WorldXfm();
    mSpotCam->WorldXfm();

    // The beam points down the spotlight's local +Y axis.
    const Vector3 &beamDir = sl->WorldXfm().m.y;

    Vector3 toCam = lightPos;
    toCam -= camXfm.v;

    Vector3 viewDir = toCam;
    Normalize(viewDir, viewDir);

    // Screen-horizontal axis of the beam's billboard.
    Vector3 perp;
    Cross(viewDir, beamDir, perp);
    Normalize(perp, perp);

    Vector2 radii = def.NGRadii();
    float topR = radii.x;
    float botR = radii.y;
    float len = def.mLength;

    Vector3 topRight = lightPos;
    SlideCorner(topRight, perp, topR);

    Vector3 botCenter = lightPos;
    SlideCorner(botCenter, beamDir, len);

    Vector3 topLeft = lightPos;
    SlideCornerBack(topLeft, perp, topR);

    Vector3 botRight = botCenter;
    SlideCorner(botRight, perp, botR);

    Vector3 botLeft = botCenter;
    SlideCornerBack(botLeft, perp, botR);

    Vector3 rightPlane;
    EyeEdgePlane(topRight, botRight, camXfm.v, rightPlane);
    Normalize(rightPlane, rightPlane);

    Vector3 leftPlane;
    EyeEdgePlane(topLeft, botLeft, camXfm.v, leftPlane);
    Normalize(leftPlane, leftPlane);

    // Plane constants: the eye lies on both planes, so d == dot(eye, n).
    float rightD = camXfm.v.x * rightPlane.x
        + (camXfm.v.y * rightPlane.y + camXfm.v.z * rightPlane.z);
    float leftD = camXfm.v.x * leftPlane.x
        + (camXfm.v.y * leftPlane.y + camXfm.v.z * leftPlane.z);

    // Bisector of the two silhouette planes.
    Vector3 bisector = rightPlane;
    bisector += leftPlane;

    Vector3 axis;
    NormalizeCopy(bisector, axis);

    float rightCos = Dot(rightPlane, axis);
    float invRight;
    if (rightCos == 0.0f) {
        invRight = 0.0f;
    } else {
        invRight = 1.0f / rightCos;
    }

    float leftCos = Dot(leftPlane, axis);
    float invLeft;
    if (leftCos == 0.0f) {
        invLeft = 0.0f;
    } else {
        invLeft = 1.0f / leftCos;
    }

    Vector4 leftEq;
    PlaneEquation(leftPlane, invLeft, leftD, leftEq);
    Vector4 rightEq;
    PlaneEquation(rightPlane, invRight, rightD, rightEq);

    // Narrow end of the cone, and the distance from the apex to the light.
    float minR = (topR - botR) >= 0.0f ? botR : topR;

    float apexDist;
    if (0.0f < botR) {
        apexDist = (len * minR) / (botR - minR);
    } else {
        apexDist = 0.0f;
    }

    float halfAngle = (botR * 0.5f) / (len + apexDist);
    float axisDot = Dot(beamDir, viewDir);
    float sq = halfAngle * halfAngle;
    float fade = (1.0f - sq) / (sq + 1.0f);

    if (axisDot <= 0.0f) {
        axisDot = -axisDot;
    }

    float vis;
    if (axisDot < fade) {
        float slack = fade - axisDot;
        if (slack < 0.02f) {
            float t = -(slack * 50.0f - 1.0f);
            vis = -(t * t * t * t - 1.0f);
        } else {
            vis = 1.0f;
        }
    } else {
        vis = 0.0f;
    }

    Vector4 visConst;
    visConst.Set(vis, 0.0f, 0.0f, 0.0f);
    TheShaderMgr.SetPConstant((PShaderConstant)0x56, visConst);
    TheShaderMgr.SetPConstant((PShaderConstant)0x57, rightEq);
    TheShaderMgr.SetPConstant((PShaderConstant)0x58, leftEq);

    SetXSectionTexture(def);
}
#endif

void NgSpotlightDrawer::SetXSectionTexture(const Spotlight::BeamDef &def) {
    RndTex *tex = def.mXSection;
    if (!tex) {
        tex = SR().unk14;
    }
    TheShaderMgr.SetPConstant(kPS_SpotlightTex, tex);
    TheRenderState.SetTextureClamp(0xB, (RndRenderState::ClampMode)2);
    TheRenderState.SetTextureFilter(0xB, (RndRenderState::FilterMode)1, false);
}

bool NgSpotlightDrawer::RestoreCam() {
    if (mSavedCam) {
        mSavedCam->Select();
    } else {
        TheRnd.GetDefaultCam()->Select();
    }
    return true;
}

bool NgSpotlightDrawer::CheckFogTexture() {
    if (mParams.mProxy) {
        mFogDensityMap = SR().mDensityMap;
    } else if (mParams.mTexture) {
        mFogDensityMap = mParams.mTexture;
    } else {
        mFogDensityMap = SR().unk10;
    }
    return mFogDensityMap;
}

#ifndef HX_NATIVE
bool NgSpotlightDrawer::CheckRTs(NgSpotlightDrawer::SpotlightResources *sr) {
    PhysMemTypeTracker tracker(Symbol("D3D(phys):NgSpotlightDrawer"));
    DxTex::SetEDRamChecksEnabled(false);
    int w = RTWidth();
    int h = RTHeight();
    if (!sr->unk8) {
        RndTex *tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(w, h, 32, RndTex::kDepthVolumeMap, false, nullptr);
        sr->unk8 = tex;
    }
    DxTex::SetEDRamChecksEnabled(true);
    if (!sr->unk4) {
        D3DSURFACE_DESC desc;
        ((DxTex *)sr->unk8)->GetDepthRT()->GetDesc(&desc);
        D3DFORMAT fmt = desc.Format;
        int createH = RTHeight();
        int createW = RTWidth();
        sr->unk4 = (D3DResource *)D3DDevice_CreateTexture(
            createW, createH, 1, 1, 0, fmt, 0, D3DRTYPE_TEXTURE
        );
        DX_ASSERT(sr->unk4, 0x12C);
    }
    if (!sr->unk10) {
        sr->unk10 = TheRnd.GetDefaultTex(Rnd::kDefaultTex_Black);
    }
    if (!sr->unk14) {
        sr->unk14 = TheRnd.GetDefaultTex(Rnd::kDefaultTex_WhiteTransparent);
    }
    sr->unk18 = sr->unk10;
    if (!sr->mDensityMap) {
        sr->mDensityMap = Hmx::Object::New<RndTex>();
        int dh = RTHeight() >> 1;
        int dw = RTWidth() >> 1;
        sr->mDensityMap->SetBitmap(dw, dh, 32, RndTex::kDensityMap, false, nullptr);
    }
    return true;
}
#endif

bool NgSpotlightDrawer::CheckSharedResources() {
    if (sSharedResources) {
        if (sSharedResources->unk8 && sSharedResources->unk8->Width() != RTWidth()) {
            RELEASE(sSharedResources);
        }
    }
    if (!sSharedResources) {
        sSharedResources = new SpotlightResources();
        return CheckRTs(sSharedResources);
    }
    return true;
}

#ifndef HX_NATIVE
// Manual vector implementations to match target code generation
typedef std::vector<SpotlightDrawer::SpotMeshEntry> SpotMeshEntryVector;
typedef SpotlightDrawer::SpotMeshEntry SpotMeshEntry;

namespace stlpmtx_std {

// Manual specialization for SpotMeshEntry vector to match target codegen
// The target binary uses manual memcpy loops instead of STL helpers
template <>
void vector<SpotMeshEntry, StlNodeAlloc<SpotMeshEntry>>::_M_fill_insert_aux(
    SpotMeshEntry* __pos,
    unsigned int __n,
    const SpotMeshEntry& __x,
    const __false_type&
) {
    // Self-reference check required for non-movable types
    if (_M_is_inside(__x)) {
        SpotMeshEntry __x_copy = __x;
        _M_fill_insert_aux(__pos, __n, __x_copy, __false_type());
        return;
    }

    pointer __old_finish = this->_M_finish;
    const size_type __elems_after = __old_finish - __pos;

    if (__elems_after > __n) {
        // Move tail elements forward
        __uninitialized_copy(__old_finish - __n, __old_finish, __old_finish, _TrivialUCpy());
        this->_M_finish += __n;

        // Manual backward copy loop to match target codegen
        pointer src = __old_finish - __n;
        pointer dst = __old_finish;
        for (int count = (src - __pos) / sizeof(SpotMeshEntry); count > 0; count--) {
            dst--;
            src--;
            memcpy(dst, src, sizeof(SpotMeshEntry));
        }

        // Manual fill loop to match target codegen
        pointer end = __pos + __n;
        for (pointer p = __pos; p != end; p++) {
            memcpy(p, &__x, sizeof(SpotMeshEntry));
        }
    } else {
        // Fill new elements beyond old finish
        this->_M_finish = __uninitialized_fill_n(this->_M_finish, __n - __elems_after, __x, _PODType());
        // Copy remaining elements
        __uninitialized_copy(__pos, __old_finish, this->_M_finish, _TrivialUCpy());
        this->_M_finish += __elems_after;
        // Fill elements within old range
        for (pointer p = __pos; p != __old_finish; p++) {
            memcpy(p, &__x, sizeof(SpotMeshEntry));
        }
    }
}

template <>
SpotMeshEntry* vector<SpotMeshEntry, StlNodeAlloc<SpotMeshEntry>>::_M_erase(
    SpotMeshEntry* __first,
    SpotMeshEntry* __last,
    const __false_type&
) {
    SpotMeshEntry* __pos = __first;
    SpotMeshEntry* __src = __last;
    int __count = (this->_M_finish - __src) / 0x50;

    if (__count > 0) {
        do {
            memcpy(__pos, __src, 0x50);
            __count--;
            __pos += 1;
            __src += 1;
        } while (__count != 0);
    }

    this->_M_finish = __pos;
    return __first;
}

template <>
SpotMeshEntry* vector<SpotMeshEntry, StlNodeAlloc<SpotMeshEntry>>::_M_erase(
    SpotMeshEntry* __pos,
    const __false_type&
) {
    SpotMeshEntry* __next = __pos + 1;
    if (__next != this->_M_finish) {
        int __bytes = (this->_M_finish - __next) * sizeof(SpotMeshEntry);
        memcpy(__pos, __next, __bytes);
    }
    --this->_M_finish;
    return __pos;
}

}  // namespace stlpmtx_std
#endif // HX_NATIVE



