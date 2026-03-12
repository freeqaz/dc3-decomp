#include "rndobj/ShadowMap.h"
#include "Memory.h"
#include "macros.h"
#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

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

bool RndShadowMap::PrepShadow(RndDrawable *, RndEnviron *) {
    // TODO: implement shadow map preparation
    return false;
}
