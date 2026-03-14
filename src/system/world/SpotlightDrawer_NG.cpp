#include "world/SpotlightDrawer_NG.h"
#include "macros.h"
#include "math/Color.h"
#include "math/Mtx.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rnddx9/RenderState.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/ShaderMgr.h"
#include "utl/Loader.h"
#include "world/Dir.h"
#include "world/Spotlight.h"
#include "world/SpotlightDrawer.h"

void GetLightPosition(Spotlight *s, Vector3 &v) {
    v = s->WorldXfm().v;
    if (s)
        Multiply(v, s->WorldXfm().m, v);
    // clang-format off
    //   if (param_1[0xfd] == 0x0) {
    //     pTVar11 = param_1 + 0x88;
    //   }
    //   else {
    //     pTVar11 = RndTransformable::WorldXfm_Force(param_1 + 0x40);
    //   }
    //   iVar10 = *(param_1 + 0x1f4);
    //   fVar4 = *(iVar10 + 0x80);
    //   fVar5 = *(iVar10 + 0x7c);
    //   fVar6 = *(iVar10 + 0x78);
    // Multiply(V, M, V)
    //   param_2->x += (pTVar11->m).x.x * fVar6 + (pTVar11->m).y.x * fVar5 + (pTVar11->m).z.x * fVar4;
    //   param_2->y += (pTVar11->m).x.y * fVar6 + (pTVar11->m).y.y * fVar5 + (pTVar11->m).z.y * fVar4;
    //   param_2->z += (pTVar11->m).x.z * fVar6 + (pTVar11->m).y.z * fVar5 + (pTVar11->m).z.z * fVar4;
    // clang-format on
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


// TODO: implement rendering functions
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
    } else {
        cam = RndCam::Current();
        if (!cam) {
            cam = TheRnd.GetDefaultCam();
        }
    }
    mSpotCam->Copy(cam, Hmx::Object::kCopyShallow);
    mSpotCam->SetTransParent(nullptr, false);
    return true;
}

void NgSpotlightDrawer::RenderCone(Spotlight *sl) {
    MILO_ASSERT(sl->HasBeam(), 0x45d);
    Spotlight *colorOwner = sl->mColorOwner;
    float scale = colorOwner->mIntensity * 8.0f;
    Hmx::Color color(
        colorOwner->mColor.red * scale,
        colorOwner->mColor.green * scale,
        colorOwner->mColor.blue * scale,
        colorOwner->mColor.alpha * scale
    );
    if (!sl->mAnimateColorFromPreset && sl->mBeam.mMat) {
        const Hmx::Color &matColor = sl->mBeam.mMat->GetColor();
        color.red *= matColor.red;
        color.green *= matColor.green;
        color.blue *= matColor.blue;
        color.alpha *= matColor.alpha;
    }
    RenderConeDefs(sl, color);
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
    float nearPlane = mSpotCam->NearPlane();
    if (nearPlane > 0.0f) {
        fogDensity = 1.0f / nearPlane;
    } else {
        fogDensity = 0.0f;
    }

    Vector4 fogParams(0.0f, fogDensity, 0.0f, 0.0f);
    TheShaderMgr.SetPConstant((PShaderConstant)0x7F, fogParams);
}

#include "rnddx9/Rnd.h"

void NgSpotlightDrawer::BlurRT() {
    BlurRT(0.5f, 0.5f);
}

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
}  // namespace stlpmtx_std
#endif // HX_NATIVE
