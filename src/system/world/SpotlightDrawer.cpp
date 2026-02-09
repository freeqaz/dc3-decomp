#include "world/SpotlightDrawer.h"
#include "obj/Object.h"
#include "rndobj/Draw.h"
#include "rndobj/Env.h"
#include "rndobj/Rnd.h"
#include "utl/BinStream.h"
#include "world/Spotlight.h"

RndEnviron *SpotlightDrawer::sEnviron;
SpotlightDrawer *SpotlightDrawer::sDefault;
int SpotlightDrawer::sNeedBoxMap = -1;
bool SpotlightDrawer::sHaveAdditionals;
bool SpotlightDrawer::sHaveLenses;
bool SpotlightDrawer::sHaveFlares;

SpotlightDrawer::SpotlightDrawer() : mParams(this) { mOrder = -100000; }

SpotlightDrawer::~SpotlightDrawer() {
    if (sCurrent == this) {
        DeSelect();
        ClearAndShrink(sLights);
        ClearAndShrink(sShadowSpots);
        ClearAndShrink(sCans);
    }
}

BEGIN_HANDLERS(SpotlightDrawer)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(Hmx::Object)
    HANDLE_ACTION(select, Select())
    HANDLE_ACTION(deselect, DeSelect())
END_HANDLERS

BEGIN_PROPSYNCS(SpotlightDrawer)
    SYNC_PROP(total, mParams.mIntensity)
    SYNC_PROP(base_intensity, mParams.mBaseIntensity)
    SYNC_PROP(smoke_intensity, mParams.mSmokeIntensity)
    SYNC_PROP(color, mParams.mColor)
    SYNC_PROP(proxy, mParams.mProxy)
    SYNC_PROP(light_influence, mParams.mLightingInfluence)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_COPYS(SpotlightDrawer)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY_AS(SpotlightDrawer, c)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mParams)
    END_COPYING_MEMBERS
END_COPYS

SpotDrawParams::SpotDrawParams(SpotlightDrawer *owner)
    : mIntensity(1.0f), mColor(1.0f, 1.0f, 1.0f), mBaseIntensity(0.1f),
      mSmokeIntensity(0.5f), mHalfDistance(250.0f), mLightingInfluence(1.0f),
      mTexture(owner, 0), mProxy(owner, 0), mOwner(owner) {
    MILO_ASSERT(owner, 0x37c);
}

void SpotDrawParams::Save(BinStream &bs) {
    bs << mIntensity;
    bs << mBaseIntensity;
    bs << mSmokeIntensity;
    bs << mHalfDistance;
    bs << mColor;
    bs << mTexture;
    bs << mProxy;
    bs << mLightingInfluence;
}

BEGIN_SAVES(SpotlightDrawer)
    SAVE_REVS(6, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    mParams.Save(bs);
END_SAVES

void SpotlightDrawer::Init() {
    sEnviron = Hmx::Object::New<RndEnviron>();
    sEnviron->SetUseApproxes(false);
    REGISTER_OBJ_FACTORY(SpotlightDrawer)
    SpotlightDrawer* ptr = Hmx::Object::New<SpotlightDrawer>();
    ptr->mParams.mLightingInfluence = 0.0f;
    sDefault = ptr;
    ptr->Select();
}

void SpotlightDrawer::Select() {
    if (sCurrent != this) {
        if (sCurrent) {
            TheRnd.UnregisterPostProcessor(sCurrent);
        }
        sCurrent = this;
        TheRnd.RegisterPostProcessor(this);
    }
    sNeedBoxMap = -1;
}

void SpotlightDrawer::DrawBeams(
    SpotlightDrawer::SpotlightEntry *spotIter,
    SpotlightDrawer::SpotlightEntry *const &spotEnd
) {
    MILO_ASSERT(spotIter != spotEnd, 0x2c7);
    for (; spotIter != spotEnd; ++spotIter) {
        Spotlight *sl = spotIter->unk4;
        Spotlight::BeamDef &def = sl->mBeam;
        if (def.mBeam) {
            MILO_ASSERT(def.mBeam->Showing(), 0x2e4);
            def.mBeam->DrawShowing();
        }
    }
}

void SpotlightDrawer::DrawFlares(
    SpotlightDrawer::SpotlightEntry *spotIter,
    SpotlightDrawer::SpotlightEntry *const &spotEnd
) {
    MILO_ASSERT(spotIter != spotEnd, 0x2f4);
    for (; spotIter != spotEnd; ++spotIter) {
        Spotlight *sl = spotIter->unk4;
        if (sl->GetFlare() && sl->GetFlare()->GetMat()) {
            sl->GetFlare()->Draw();
        }
    }
}

void SpotlightDrawer::DrawAdditional(
    SpotlightDrawer::SpotlightEntry *spotIter,
    SpotlightDrawer::SpotlightEntry *const &spotEnd
) {
    MILO_ASSERT(spotIter != spotEnd, 0x298);
    for (; spotIter != spotEnd; ++spotIter) {
        Spotlight *sl = spotIter->unk4;
        FOREACH (it, sl->GetAdditionalObjects()) {
            RndDrawable *add = *it;
            MILO_ASSERT(add != sl, 0x2a3);
            if (add != sl)
                add->Draw();
        }
    }
}

void SpotlightDrawer::DrawLenses(
    SpotlightDrawer::SpotlightEntry *spotIter,
    SpotlightDrawer::SpotlightEntry *const &spotEnd
) {
    MILO_ASSERT(spotIter != spotEnd, 0x2b1);
    for (; spotIter != spotEnd; ++spotIter) {
        Spotlight *sl = spotIter->unk4;
        if (Spotlight::sDiskMesh) {
            MILO_ASSERT(sl->LensMesh(), 0x2b9);
            Spotlight::sDiskMesh->SetMat(sl->LensMesh());
            Spotlight::sDiskMesh->Draw();
        }
    }
}

void SpotlightDrawer::SortLights() {
    if (sLights.size() > 2) {
        std::sort(sLights.begin(), sLights.end(), ByColor());
    }
    if (sCans.size() > 2) {
        std::sort(sCans.begin(), sCans.end(), ByEnvMesh());
    }
}

void SpotlightDrawer::ClearPostDraw() {
    ClearLights();
    sNeedDraw = false;
}

void SpotlightDrawer::EndWorld() {
    UpdateBoxMap();
    if (sNeedDraw) {
        DrawWorld();
        ClearPostDraw();
    }
    if (TheRnd.DisablePP()) {
        ClearLights();
    }
    MILO_ASSERT(!sNeedDraw, 0x165);
}
