#include "world\SpotlightDrawer.h"
#include "char\Character.h"
#include "math/Geo.h"
#include "math/Key.h"
#include "obj/Object.h"
#include "os\Platform.h"
#include "os\System.h"
#include "rndobj\BoxMap.h"
#include "rndobj/Cam.h"
#include "rndobj\Draw.h"
#include "rndobj\Env.h"
#include "rndobj/MultiMesh.h"
#include "rndobj\Rnd.h"
#include "rndobj\Stats_NG.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "world\Spotlight.h"

RndEnviron *SpotlightDrawer::sEnviron;
SpotlightDrawer *SpotlightDrawer::sDefault;
int SpotlightDrawer::sNeedBoxMap = -1;
bool SpotlightDrawer::sHaveAdditionals;
bool SpotlightDrawer::sHaveLenses;
bool SpotlightDrawer::sHaveFlares;
std::vector<SpotlightDrawer::SpotlightEntry> SpotlightDrawer::sLights;
std::vector<SpotlightDrawer::SpotMeshEntry> SpotlightDrawer::sCans;
std::vector<Spotlight *> SpotlightDrawer::sShadowSpots;
bool SpotlightDrawer::sNoBeams;
SpotlightDrawer *SpotlightDrawer::sCurrent;
bool SpotlightDrawer::sNeedDraw;

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

SpotDrawParams &SpotDrawParams::operator=(const SpotDrawParams &other) {
    mIntensity = other.mIntensity;
    mBaseIntensity = other.mBaseIntensity;
    mSmokeIntensity = other.mSmokeIntensity;
    mHalfDistance = other.mHalfDistance;
    mLightingInfluence = other.mLightingInfluence;
    mColor = other.mColor;
    mTexture = other.mTexture;
    mProxy = other.mProxy;
    return *this;
}

SpotDrawParams::SpotDrawParams(SpotlightDrawer *owner)
    : mIntensity(1.0f), mColor(1.0f, 1.0f, 1.0f), mBaseIntensity(0.1f),
      mSmokeIntensity(0.5f), mHalfDistance(250.0f), mLightingInfluence(1.0f),
      mTexture(owner, 0), mProxy(owner, 0), mOwner(owner) {
    MILO_ASSERT(owner, 0x351);
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
    // Lane history (31 lines moved): docs/decomp/notes/system/world/SpotlightDrawer.md
    // > MECHANISM (w8-h; re-measured w33-c): 93.929 canonical.
    SpotlightDrawer *ptr = Hmx::Object::New<SpotlightDrawer>();
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

void SpotlightDrawer::ListDrawChildren(std::list<RndDrawable *> &draws) {
    draws.push_back(mParams.mProxy);
}

// Lane history (16 lines moved): docs/decomp/notes/system/world/SpotlightDrawer.md
// > We had a reinterpret_cast to RndHighlightable calling slot 0, which on the image's
// > layout is OnlyReturns -- the environment was never selected. (w8-q)
void SpotlightDrawer::DrawMeshVec(std::vector<SpotMeshEntry> &entries) {
    if (entries.size() != 0) {
        std::vector<SpotMeshEntry>::iterator it = entries.begin();
        RndMesh *canMesh = it->mCanMesh;
        RndMultiMesh *multiMesh = canMesh->CreateMultiMesh();
        multiMesh->Instances().push_back(RndMultiMesh::Instance(it->mTransform));
        RndEnviron *envMesh = it->mEnvMesh;
        envMesh->Select(nullptr);
        std::vector<SpotMeshEntry>::iterator itEnd = entries.end();
        for (++it; it != itEnd; ++it) {
            bool envChanged = it->mEnvMesh != envMesh;
            bool canChanged = it->mCanMesh != canMesh;
            if (envChanged || canChanged) {
                multiMesh->DrawShowing();
                if (envChanged && envMesh) {
                    envMesh = it->mEnvMesh;
                    envMesh->Select(nullptr);
                }
                if (canChanged) {
                    canMesh = it->mCanMesh;
                    multiMesh = canMesh->CreateMultiMesh();
                }
            }
            multiMesh->Instances().push_back(RndMultiMesh::Instance(it->mTransform));
        }
        multiMesh->DrawShowing();
    }
}

void SpotlightDrawer::DrawBeams(
    SpotlightDrawer::SpotlightEntry *spotIter,
    SpotlightDrawer::SpotlightEntry *const &spotEnd
) {
    MILO_ASSERT(spotIter != spotEnd, 0x2c7);
    for (; spotIter != spotEnd; ++spotIter) {
        Spotlight *sl = spotIter->mSpotlight;
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
        Spotlight *sl = spotIter->mSpotlight;
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
        Spotlight *sl = spotIter->mSpotlight;
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
        Spotlight *sl = spotIter->mSpotlight;
        // Lane history (30 lines moved): docs/decomp/notes/system/world/SpotlightDrawer.md
        // > The guard is the LENS MATERIAL and the assert is on sDiskMesh -- that is the
        // > image's nesting, not the other way round, and it is a BEHAVIOURAL correction
        // > (w8-q, 90.381 -> 100.0).
        if (sl->LensMesh()) {
            if (!Spotlight::sDiskMesh) {
                TheDebugFailer << MakeString(kAssertStr, __FILE__, 0x2b9, "sl->LensMesh()");
            }
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

void SpotlightDrawer::DrawShowing() {
    if (sCurrent && sCurrent != sDefault && sCurrent != this) {
        MILO_NOTIFY_ONCE(
            "Drawing 2 spotlightdrawers in one frame, %s and %s",
            PathName(sCurrent),
            PathName(this)
        );
    } else {
        Select();
    }
}

void SpotlightDrawer::SetAmbientColor(const Hmx::Color &c) {
    sEnviron->SetAmbientColor(c);
    sEnviron->Select(nullptr);
}

void SpotlightDrawer::RemoveFromLists(Spotlight *spot) {
    for (std::vector<SpotlightEntry>::iterator it = sLights.begin(); it != sLights.end();) {
        if (it->mSpotlight == spot) {
            it = sLights.erase(it);
        } else {
            ++it;
        }
    }
    for (std::vector<SpotMeshEntry>::iterator it = sCans.begin(); it != sCans.end();) {
        if (it->mSpotlight == spot) {
            it = sCans.erase(it);
        } else {
            ++it;
        }
    }
    for (std::vector<Spotlight *>::iterator it = sShadowSpots.begin();
         it != sShadowSpots.end();) {
        if (*it == spot) {
            it = sShadowSpots.erase(it);
        } else {
            ++it;
        }
    }
}

void SpotlightDrawer::DrawLight(Spotlight *spot) {
    if (!spot)
        return;

    const Hmx::Color& color = spot->Color();
    float intensity = spot->Intensity();

    float scaledR = color.red * intensity;
    float scaledG = color.green * intensity;
    float scaledB = color.blue * intensity;
    // w13-c: packing through a Hmx::Color temporary's Pack() (same expression,
    // alpha unused) is what makes the image load green before blue and gives
    // the two fctiwz scratch slots the image's order; the hand-written packing
    // expression (in either bit order) left a 4-row G/B load/slot swap at 99.981.
    uint packedColor = Hmx::Color(scaledR, scaledG, scaledB).Pack();

    unsigned char byteR = packedColor;
    unsigned char byteG = packedColor >> 8;
    unsigned char byteB = packedColor >> 16;
    bool shouldProcess = byteR > 5 || byteG > 3 || byteB > 7;

    if (shouldProcess && spot->mTargetLoaded && spot->Showing()) {
        if (GetGfxMode() == kOldGfx && spot->GetTarget() && spot->GetCastShadow()) {
            sShadowSpots.push_back(spot);
        }

        SpotlightEntry entry;
        entry.mSpotlight = spot;
        entry.mColorKey = packedColor;
        sLights.push_back(entry);

        sHaveAdditionals = sHaveAdditionals || (int)spot->GetAdditionalObjects().size() > 0;

        sHaveFlares = sHaveFlares || (spot->IsFlareEnabled() && spot->GetFlare());

        sHaveLenses = sHaveLenses || spot->LensMesh();

        if ((unsigned int)sNeedBoxMap == TheRnd.GetFrameID()) {
            MILO_NOTIFY_ONCE("%s drawn after SpotlightEnder", PathName(spot));
        }

        sNeedDraw = true;
    }

    if (spot->mLightCanMesh && !spot->mLightCanSort) {
        RndMesh *canMesh = spot->mLightCanMesh;
        const Transform &canXfm = spot->mLightCanXfm;
        bool visible;
        if (!canMesh->Showing()) {
            visible = false;
        } else {
            Sphere sphere = canMesh->GetSphere();
            if (sphere.radius > 0.0f) {
                Multiply(sphere, canXfm, sphere);
                visible = !(sphere > RndCam::Current()->WorldFrustum());
            } else {
                visible = true;
            }
        }
        if (visible) {
            SpotMeshEntry meshEntry;
            meshEntry.mCanMesh = spot->mLightCanMesh;
            meshEntry.mEnvMesh = RndEnviron::sCurrent;
            meshEntry.mSpotlight = spot;
            meshEntry.mTransform = canXfm;
            sCans.push_back(meshEntry);
            sNeedDraw = true;
        }
    }
}

void SpotlightDrawer::DeSelect() {
    if (sCurrent != this)
        return;
    if (sDefault != this) {
        sDefault->Select();
    } else {
        PostProcessor *pp = sCurrent ? static_cast<PostProcessor *>(sCurrent) : nullptr;
        TheRnd.UnregisterPostProcessor(pp);
        sCurrent = nullptr;
    }
}

void SpotlightDrawer::ApplyLightingApprox(BoxMapLighting &boxMap, float f2) const {
    MILO_ASSERT(boxMap.NumQueuedLights() == 0, 0x20b);
    std::vector<SpotlightEntry>::iterator it = sLights.begin();
    std::vector<SpotlightEntry>::iterator itEnd = sLights.end();
    // The image materialises `params` once before the loop (lwz r31,0x50(r1)
    // reads its still-uninitialised slot at 0x82823BCC), so it is loop-carried,
    // not a fresh declaration per iteration.
    BoxMapLighting::LightParams_Spot *params;
    for (; it != itEnd; ++it) {
        Spotlight *curSpotlight = it->mSpotlight;
        const Transform &xfm = curSpotlight->WorldXfm();
        Hmx::Color c50(curSpotlight->Color());
        Multiply(c50, f2, c50);
        Multiply(c50, curSpotlight->Intensity(), c50);
        if (!boxMap.ParamsAt(params))
            break;
        params->mPosition = xfm.v;
        params->mDirection = xfm.m.y;
        params->mColor = c50;
        // Lane history (35 lines moved): docs/decomp/notes/system/world/SpotlightDrawer.md
        // > The residual is a REGISTER BUDGET, not an expression (w8-q, measured): the
        // > image spends one more callee-saved GPR and one fewer callee-saved FPR than we
        // > do.
        Spotlight::BeamDef &beam = curSpotlight->mBeam;
        params->mTopRadius = beam.mTopRadius;
        params->mBottomRadius = beam.mBottomRadius * 2.0f;
        params->mBeamLength = beam.mLength * 2.0f;
        boxMap.CacheData(*params);
    }
}

void SpotlightDrawer::DrawShadow() {
    std::vector<Spotlight *>::iterator it = sShadowSpots.begin();
    std::vector<Spotlight *>::iterator itEnd = sShadowSpots.end();
    for (; it != itEnd; ++it) {
        Spotlight *shadowSpot = *it;
        MILO_ASSERT(shadowSpot->GetTarget() && shadowSpot->GetCastShadow(), 0x288);
        RndDrawable *draw = dynamic_cast<RndDrawable *>(shadowSpot->GetTarget());
        if (draw) {
            draw->DrawShadow(shadowSpot->WorldXfm(), 1.5f);
        }
    }
}

void SpotlightDrawer::UpdateBoxMap() {
    if ((unsigned int)sNeedBoxMap != TheRnd.GetFrameID()) {
        RndEnviron::sGlobalLighting.Clear();
        float lightingInf = mParams.mLightingInfluence;
        if (lightingInf > 0) {
            ApplyLightingApprox(RndEnviron::sGlobalLighting, lightingInf);
        }
        sNeedBoxMap = TheRnd.GetFrameID();
    }
}

void SpotDrawParams::Load(BinStreamRev &d) {
    d >> mIntensity;
    if (d.rev > 3) {
        d >> mBaseIntensity >> mSmokeIntensity >> mHalfDistance;
    } else {
        float x, y, z, w;
        d >> x >> y >> z >> w;
        if (z < 0.5f) {
            mSmokeIntensity = 0.5f;
            mBaseIntensity = 0.1f;
        } else {
            mBaseIntensity = 0.15f;
            mSmokeIntensity = 1.0f;
        }
    }
    d >> mColor;
    if (d.rev < 4) {
        float x;
        Vector2 vx, vy;
        d >> x >> vx >> vy;
    }
    d >> mTexture;
    d >> mProxy;
    if (d.rev < 3) {
        bool b;
        d >> b;
    }
    if (d.rev > 4) {
        d >> mLightingInfluence;
    }
}

INIT_REVS(6, 0)

BEGIN_LOADS(SpotlightDrawer)
    LOAD_REVS(bs)
    ASSERT_REVS(6, 0)
    if (d.rev > 0) {
        if (d.rev > 5) {
            Hmx::Object::Load(d.stream);
        }
        RndDrawable::Load(d.stream);
    } else {
        Hmx::Object::Load(d.stream);
    }
    mOrder = -100000;
    mParams.Load(d);
END_LOADS

class LensExtract {};

template <class T>
void DrawAccessories(
    SpotlightDrawer::SpotlightEntry *const &,
    SpotlightDrawer::SpotlightEntry *const &
);

// COMDAT selection must be ANY, not NODUPLICATES: ham_xbox_r.map flags
// ??$DrawAccessories@VLensExtract@@@@YAX... with `f i`, and an explicit
// specialization without `inline` compiles to a NODUPLICATES COMDAT here
// (verified by reading the section aux record's Selection byte).
template <>
inline void DrawAccessories<LensExtract>(
    SpotlightDrawer::SpotlightEntry *const &spotBegin,
    SpotlightDrawer::SpotlightEntry *const &spotEnd
) {
    SpotlightDrawer::SpotlightEntry *it = spotBegin;
    RndMat *curMat = nullptr;
    RndMesh *curDisk = nullptr;
    RndMultiMesh *multiMesh = nullptr;
    if (it == spotEnd)
        return;
    do {
        // Lane history (5 lines moved): docs/decomp/notes/system/world/SpotlightDrawer.md
        if (it->mSpotlight->LensMesh() != nullptr) {
            RndMesh *disk = Spotlight::GetDiskMesh();
            RndMultiMesh *nextMesh;
            if (disk != curDisk) {
                nextMesh = disk->CreateMultiMesh();
            } else {
                nextMesh = multiMesh;
            }
            const Transform &lensXfm = it->mSpotlight->LensXfm();
            bool visible;
            // Lane history (8 lines moved): docs/decomp/notes/system/world/SpotlightDrawer.md
            if (!disk->Showing()) {
                visible = false;
            } else {
                Sphere sphere = disk->GetSphere();
                if (sphere.radius > 0.0f) {
                    Multiply(sphere, lensXfm, sphere);
                    visible = !(sphere > RndCam::Current()->WorldFrustum());
                } else {
                    visible = true;
                }
            }
            if (visible) {
                bool diskChanged = (disk != curDisk);
                RndMat *lensMat = it->mSpotlight->LensMesh();
                bool matChanged = (lensMat != curMat);
                if ((diskChanged || matChanged) && multiMesh != nullptr
                    && !multiMesh->Instances().empty()) {
                    multiMesh->DrawShowing();
                    multiMesh->Instances().resize(0, RndMultiMesh::Instance());
                }
                if (diskChanged) {
                    curDisk = disk;
                    nextMesh = disk->CreateMultiMesh();
                }
                if (matChanged || diskChanged) {
                    curMat = lensMat;
                    curDisk->SetMat(lensMat);
                }
                RndMultiMesh::Instance inst(lensXfm);
                nextMesh->Instances().insert(nextMesh->Instances().end(), inst);
                multiMesh = nextMesh;
            }
        }
        ++it;
    } while (it != spotEnd);
    if (multiMesh != nullptr && !multiMesh->Instances().empty()) {
        multiMesh->DrawShowing();
        multiMesh->Instances().resize(0, RndMultiMesh::Instance());
    }
}

void SpotlightDrawer::DrawWorld() {
    // w17-b: one Max<int>() assignment.  The image loads TheNgStats (the
    // store's base) BEFORE sLights.size() (0x82826990..A8); a local `numLights`
    // computed first put the size loads ahead of it (9 rows).
    TheNgStats->mSpotlights = Max<int>(sLights.size(), TheNgStats->mSpotlights);
    if ((!sLights.empty() || !sCans.empty()) && Showing()) {
        SortLights();
        DrawMeshVec(sCans);
        sCans.resize(0);
        if (!sLights.empty()) {
            RndEnviron *cur = RndEnviron::sCurrent;
            Vector3 *pos = RndEnviron::CurrentPos();
            MILO_ASSERT(sEnviron->GetUseApprox() == false, 0x1dc);
            sEnviron->Select(nullptr);
            if (GetGfxMode() == kOldGfx) {
                DrawShadow();
            }
            std::vector<SpotlightEntry>::iterator it = sLights.begin();
            std::vector<SpotlightEntry>::iterator itEnd = sLights.end();
            if (it != itEnd) {
                do {
                    Spotlight *spot = it->mSpotlight;
                    const SpotlightEntry *e1 = &(*it);
                    const SpotlightEntry *e2 = &(*it) + 1;
                    Hmx::Color c;
                    float intensity = spot->Intensity();
                    // Lane history (33 lines moved): docs/decomp/notes/system/world/SpotlightDrawer.md
                    // > RESIDUAL (w7-am, 98.5 canonical): the image multiplies blue, then
                    // > green, then red -- Set()'s arguments evaluated right-to-left --
                    // > and we emit red, green, blue.
                    c.Set(
                        spot->Color().red * intensity,
                        spot->Color().green * intensity,
                        spot->Color().blue * intensity,
                        1.0f
                    );
                    for (; e2 != &(*itEnd); ++e2) {
                        if (e2->mColorKey != it->mColorKey)
                            break;
                    }
                    SetAmbientColor(c);
                    if (sHaveAdditionals) {
                        DrawAdditional(
                            const_cast<SpotlightEntry *>(e1),
                            const_cast<SpotlightEntry *const &>(e2)
                        );
                    }
                    if (sHaveLenses) {
                        DrawAccessories<LensExtract>(
                            const_cast<SpotlightEntry *>(e1),
                            const_cast<SpotlightEntry *const &>(e2)
                        );
                    }
                    if (!DrawNGSpotlights() && !sNoBeams
                        && TheRnd.DrawMode() != Rnd::kDrawOcclusionDepth) {
                        DrawBeams(
                            const_cast<SpotlightEntry *>(e1),
                            const_cast<SpotlightEntry *const &>(e2)
                        );
                    }
                    if (sHaveFlares) {
                        DrawFlares(
                            const_cast<SpotlightEntry *>(e1),
                            const_cast<SpotlightEntry *const &>(e2)
                        );
                    }
                    // The image reloads e2 straight out of its stack slot and
                    // assigns it to `it` (lwz r11,0x50(r1); mr r31,r11).  Going
                    // back through sLights.begin() keeps &sLights live across the
                    // whole loop, which costs one extra callee-saved register and
                    // 16 bytes of frame.
#ifdef _LIBCPP_VERSION
                    // libc++ (web) keeps __wrap_iter's pointer constructor private.
                    it = sLights.begin()
                        + (const_cast<SpotlightEntry *>(e2) - (SpotlightEntry *)sLights.begin());
#else
                    it = std::vector<SpotlightEntry>::iterator(const_cast<SpotlightEntry *>(e2));
#endif
                } while (it != itEnd);
            }
            if (cur) {
                cur->Select(pos);
            }
        }
    }
}

void SpotlightDrawer::ClearLights() {
    sLights.resize(0);
    sShadowSpots.resize(0);
    sCans.resize(0);
    sHaveAdditionals = false;
    sHaveLenses = false;
    sHaveFlares = false;
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

#ifndef HX_NATIVE
// Manual specialization for single-element erase to match target memcpy codegen
// Target uses pointer comparison, then loop with dst in r3 and src = dst + 0x50
namespace stlpmtx_std {
typedef SpotlightDrawer::SpotMeshEntry SpotMeshEntry_;
template <>
SpotMeshEntry_* vector<SpotMeshEntry_, StlNodeAlloc<SpotMeshEntry_>>::_M_erase(
    SpotMeshEntry_* __pos,
    const __false_type&
) {
    SpotMeshEntry_* __next = __pos + 1;
    if (__next != this->_M_finish) {
        int __count = ((char*)this->_M_finish - (char*)__next) / (int)sizeof(SpotMeshEntry_);
        SpotMeshEntry_* __dst = __pos;
        // Same shape as the 3-arg overload in SpotlightDrawer_NG.cpp: the image
        // tests the division result in a volatile and copies it into the
        // callee-saved loop counter inside the taken branch (`mr r31, r11`).
        if (__count > 0) {
            int __n = __count;
            do {
                SpotMeshEntry_* __src = __dst + 1;
                memcpy(__dst, __src, sizeof(SpotMeshEntry_));
                __n--;
                __dst = __src;
            } while (__n != 0);
        }
    }

    this->_M_finish--;
    return __pos;
}
}  // namespace stlpmtx_std
#endif
