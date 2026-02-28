#include "world/SpotlightDrawer_NG.h"
#include "macros.h"
#include "math/Color.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rnddx9/RenderState.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/ShaderMgr.h"
#include "world/Spotlight.h"
#include "world/SpotlightDrawer.h"

void GetLightPosition(Spotlight *s, Vector3 &v) {
    v = s->WorldXfm().v;
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
    TheShaderMgr.SetVConstant((VShaderConstant)1, Vector4(r, g, b, a));
    TheShaderMgr.SetPConstant((PShaderConstant)1, Vector4(r, g, b, a));
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

void NgSpotlightDrawer::SetXSectionTexture(const Spotlight::BeamDef &def) {
    RndTex *tex = def.mXSection;
    if (!tex) {
        tex = SR().unk14;
    }
    TheShaderMgr.SetPConstant((PShaderConstant)0xB, tex);
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
