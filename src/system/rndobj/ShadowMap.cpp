#include "rndobj\ShadowMap.h"
#include "Memory.h"
#include "macros.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "rndobj/Cam.h"
#include "rndobj\Lit.h"
#include "rndobj\Rnd.h"
#include "rndobj\Tex.h"
#include <cmath>

RndCam *RndShadowMap::sLightCam;
RndTex *RndShadowMap::sShadowTex;

void RndShadowMap::Terminate() {
    RELEASE(sLightCam);
    RELEASE(sShadowTex);
}

void RndShadowMap::Init() {
    PhysMemTypeTracker tracker("D3D(phys):Global");
    delete sLightCam;
    sLightCam = Hmx::Object::New<RndCam>();
    delete sShadowTex;
    sShadowTex = Hmx::Object::New<RndTex>();
    sShadowTex->SetBitmap(512, 512, 32, RndTex::kShadowMap, false, nullptr);
    sLightCam->SetTargetTex(sShadowTex);
}

void RndShadowMap::EndShadow() { TheRnd.SetShadowMap(nullptr, nullptr, nullptr); }

// This is Multiply(v, m, out) from math/Mtx.h written out, and it has to be,
// because PrepShadow calls it with x and z of v literal 0.0f.  Fed to the
// shared overload, /fp:fast lets MSVC reassociate
// `m.x.c*0 + m.y.c*(-dist) + m.z.c*0` into `(m.x.c + m.z.c)*0 + ...` and emit a
// leading `fadds` of two matrix elements; the target emits the two zero terms
// straight as `fmuls fN,fN,f31`.  Accumulator statements pin the association --
// MSVC does not reassociate across a `+=` -- so each row can seed from the
// element the target seeds from: m.z.x for x, m.x.c for y and z.  Do NOT fold
// these back into three expressions; measured 94.66 that way (the factoring
// returns) and 99.985 with uniform right-association.  The full accounting is
// in the comment above the overload in Mtx.h.
//
// It is an inline function rather than a block inside PrepShadow because of
// the y terms' operand order (`fmadds f0, f30(-dist), f9(m.y.x), f12`).  c2
// orders a commutative multiply's operands by key, and two named variables go
// higher symbol id first (C2RS-BRIDGE 8.7).  Written inline in PrepShadow, v.y
// was sid 258 against the matrix elements' 280/421/431, so the element came
// first; as an inlined parameter v.y is minted later (sid 428) and leads.
static inline void MultiplyAccum(const Vector3 &v, const Hmx::Matrix3 &m, Vector3 &out) {
    float ox = m.z.x * v.z;
    ox += m.y.x * v.y;
    ox += m.x.x * v.x;
    float oy = m.x.y * v.x;
    oy += m.y.y * v.y;
    oy += m.z.y * v.z;
    float oz = m.x.z * v.x;
    oz += m.y.z * v.y;
    oz += m.z.z * v.z;
    out.Set(ox, oy, oz);
}

bool RndShadowMap::PrepShadow(RndDrawable *draw, RndEnviron *env) {
    if (GetGfxMode() != kNewGfx || sLightCam == NULL || sShadowTex == NULL)
        return false;

    RndEnviron *e = env != NULL ? env : RndEnviron::Current();

    RndLight *light = NULL;
    ObjPtrList<RndLight>::iterator it;
    for (it = e->LightsApprox().begin(); it != e->LightsApprox().end(); ++it) {
        if ((*it)->GetType() == RndLight::kFloorSpot) {
            light = *it;
            break;
        }
    }
    if (light)
        goto found;

    for (it = e->LightsReal().begin(); it != e->LightsReal().end(); ++it) {
        RndLight *cur = *it;
        if (cur->GetType() == RndLight::kDirectional
            || cur->GetType() == RndLight::kPoint) {
            light = cur;
            goto found;
        }
    }

    return false;

found:
    Sphere sphere;
    RndCam *curCam = RndCam::Current();
    if (!draw->MakeWorldSphere(sphere, false)) {
        MILO_NOTIFY_ONCE(
            "Can't self-shadow %s; MakeWorldSphere failed.", PathName(draw)
        );
        return false;
    }

    Transform lightXfm;
    memcpy(&lightXfm.m, &light->WorldXfm().m, sizeof(Hmx::Matrix3));
    lightXfm.v = sphere.center;

    if (light->GetType() == RndLight::kPoint) {
        Subtract(sphere.center, light->WorldXfm().v, lightXfm.m.y);
        Normalize(lightXfm.m, lightXfm.m);
    }

    float tanHalfFov = (float)std::tan(PI / 8.0f);
    float dist = sphere.radius / tanHalfFov;
    float farPlane = sphere.radius + dist;
    float nearPlane = dist - sphere.radius;

    Vector3 offset;
    MultiplyAccum(Vector3(0.0f, -dist, 0.0f), lightXfm.m, offset);
    Add(lightXfm.v, offset, lightXfm.v);

    sLightCam->SetWorldXfm(lightXfm);
    sLightCam->SetFrustum(nearPlane, farPlane, PI / 4.0f, 1.0f);
    sLightCam->Select();

    Rnd::Mode oldMode = TheRnd.DrawMode();
    TheRnd.SetDrawMode(Rnd::kDrawExtrude);
    draw->DrawShowing();
    TheRnd.SetDrawMode(oldMode);

    curCam->Select();

    static Hmx::Color sDefaultShadowColor(0.0f, 0.0f, 0.0f, 0.0f);
    const Hmx::Color *shadowColor = &sDefaultShadowColor;
    if (light->GetType() == RndLight::kFloorSpot) {
        shadowColor = &light->GetColor();
    }

    Hmx::Color invertedColor = *shadowColor;
    invertedColor.red = 1.0f - invertedColor.red;
    invertedColor.green = 1.0f - invertedColor.green;
    invertedColor.blue = 1.0f - invertedColor.blue;

    TheRnd.SetShadowMap(sShadowTex, sLightCam, &invertedColor);
    return true;
}
