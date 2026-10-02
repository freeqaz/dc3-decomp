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
    // FLOOR 93.929 canonical (w8-h).  The image stores New()'s result into
    // sDefault FIRST (stw at 828275E4), writes 0.0f to 0x64 through the
    // still-live result, then RELOADS sDefault (lwz at 828275F0) for Select();
    // we store sDefault last and never reload, which swaps the two `lis` and
    // moves the stw.  MEASURED NEGATIVE (w8-h): naming the global on all three
    // lines -- `sDefault = New(); sDefault->mParams... = 0.0f; sDefault->
    // Select();`, the RB3 spelling -- reads 90.357, WORSE: MSVC then emits the
    // reload but ALSO reloads for the 0.0f store, adding rows.  Reverted.
    //
    // TWO MORE MEASURED NEGATIVES (w8-q).  The 4-byte shortfall is WHERE MSVC
    // puts the store to the global, and source order does not decide it:
    //   `ptr = New(); sDefault = ptr; ptr->...= 0.0f; sDefault->Select();`
    //       -- the target's statement order exactly -- is BYTE-INERT, 94.3% and
    //       the same 3 rows.  MSVC sinks the global store past the 0x64 field
    //       store on its own, so writing it earlier buys nothing.
    //   `sDefault = New(); ptr = sDefault; ptr->...= 0.0f; sDefault->Select();`
    //       reads 90.7, WORSE: it adds a `clrrwi r3, r3, 0` for the read-back.
    // Reading the global for the field store is the only thing that moves the
    // store, and that is the 90.357 negative above.  Treat this as a floor until
    // someone finds a construct that pins the global store ahead of the field
    // store without also forcing a reload for the field store.
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

// RESIDUAL 97.705 canonical (352 B), and the two charged rows say we are calling
// the WRONG VIRTUAL.  At 0x1D48..0x1D60 the image does
//   li   r4, 0x0
//   lwz  r30, 0x4(r31)        ; envMesh
//   mr   r3, r30              ; this, UNADJUSTED
//   lwz  r11, 0x0(r30)        ; vptr at object offset 0
//   lwz  r11, 0x4(r11)        ; SLOT 1
//   bctrl
// and the same again at 0x1DB8.  We emit slot 0 and no r4.  Two things follow.
// (1) The image dispatches on the UNADJUSTED pointer, so the reinterpret_cast
//     below is faithful and must stay.  MEASURED NEGATIVE (w8-q): spelling it
//     `envMesh->Highlight()` -- legal, since RndMesh -> RndDrawable ->
//     virtual RndHighlightable really does expose Highlight() -- drops this row
//     from 97.705 to 87.227, because MSVC then emits the full virtual-base
//     adjustment the image has nowhere: `lwz r11,0x4(r30)` (vbptr),
//     `lwz r11,0x8(r11)` (vbase displacement), `add r11,r11,r30`,
//     `addi r3,r11,0x4`.  Reverted.
// (2) `li r4, 0x0` is an ARGUMENT, and RndHighlightable::Highlight() has no
//     parameter -- ?Highlight@RndDrawable@@UAAXXZ in ham_xbox_r.map is `XZ`,
//     void(void).  So slot 1 of the unadjusted vptr is a ONE-ARGUMENT virtual
//     and this call is not Highlight() at all.  Whatever it is, it is reached by
//     a cast to some class whose slot 0 is occupied and whose slot 1 takes one
//     pointer/bool; finding it is the next step, not another spelling of
//     Highlight.
void SpotlightDrawer::DrawMeshVec(std::vector<SpotMeshEntry> &entries) {
    if (entries.size() != 0) {
        std::vector<SpotMeshEntry>::iterator it = entries.begin();
        RndMesh *canMesh = it->mCanMesh;
        RndMultiMesh *multiMesh = canMesh->CreateMultiMesh();
        multiMesh->Instances().push_back(RndMultiMesh::Instance(it->mTransform));
        RndMesh *envMesh = it->mEnvMesh;
        reinterpret_cast<RndHighlightable *>(envMesh)->Highlight();
        std::vector<SpotMeshEntry>::iterator itEnd = entries.end();
        for (++it; it != itEnd; ++it) {
            bool envChanged = it->mEnvMesh != envMesh;
            bool canChanged = it->mCanMesh != canMesh;
            if (envChanged || canChanged) {
                multiMesh->DrawShowing();
                if (envChanged && envMesh) {
                    envMesh = it->mEnvMesh;
                    reinterpret_cast<RndHighlightable *>(envMesh)->Highlight();
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
    for (; spotEnd != spotIter; ++spotIter) {
        Spotlight *sl = spotIter->mSpotlight;
        // The guard is the LENS MATERIAL and the assert is on sDiskMesh -- that
        // is the image's nesting, not the other way round, and it is a
        // BEHAVIOURAL correction (w8-q, 90.381 -> 100.0).  Decoded at
        // 0x82823EEC..0x82823F54: `lwz r11,0x1ec(r25)` / `cmpwi cr6,r11,0` /
        // `beq cr6,.L_82823F54` jumps straight to the loop LATCH, so a spotlight
        // with no lens material is skipped entirely and never asserts; only then
        // is `sDiskMesh` loaded (0x82823EFC) and `cmplwi`-tested, with
        // `bne cr6,.L_82823F38` hopping over the line-0x2B9 Fail block.  So the
        // image fails iff mLensMaterial != 0 && sDiskMesh == 0.  We had it
        // inverted (guard on sDiskMesh, assert on LensMesh), which asserted on
        // the wrong condition AND drew the disk mesh with a null material for
        // every lens-less spotlight.  Spelling the image's nesting removed all 3
        // inserts, all 3 deletes and the beq/bne inversion at once; RB3's
        // SpotlightDrawer.cpp:DrawLenses has our old inverted shape, so it is not
        // a reference here.
        //
        // HAND-EXPANDED ASSERT, and it has to be: the image's line-0x2B9 message
        // string is "sl->LensMesh()" -- 14 chars, `??_C@_0P@ICJAFDBB@...` at
        // 0x820e6400, the ONLY assert literal in world:SpotlightDrawer.obj -- and
        // `_0P@` pins its length at 15 bytes including the NUL, so it cannot be
        // the 21-byte "Spotlight::sDiskMesh" that MILO_ASSERT's `#cond` would
        // produce for the condition the image actually tests.  There is no
        // "Spotlight::sDiskMesh" literal anywhere in ham_xbox_r.map.  Harmonix
        // moved the guard and left the old assert text behind; MILO_ASSERT
        // stringifies its own condition and so cannot reproduce that, and
        // `MILO_ASSERT(Spotlight::sDiskMesh, 0x2b9)` holds this row at 99.84127
        // on exactly one charged relocation-name row.  The `if` form (rather than
        // MILO_ASSERT's do/while) is free here: DrawLenses holds no
        // function-local static, so the scope-ordinal difference between the two
        // spellings has nothing to number.
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
            meshEntry.mEnvMesh = reinterpret_cast<RndMesh *>(RndEnviron::sCurrent);
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
        // The residual is a REGISTER BUDGET, not an expression (w8-q, measured):
        // the image spends one more callee-saved GPR and one fewer callee-saved
        // FPR than we do.  Target prologue: `bl __savegprlr_26` + a single
        // `stfd f31, -0x40(r1)`, with `lis r26, __real@40000000@ha` hoisted
        // pre-loop and `lfs f0, __real@40000000@l(r26)` reloaded every iteration.
        // Ours: `bl __savegprlr_27` + `stfd f30` AND `stfd f31`, with the value
        // itself parked in f30 for the whole function.  That accounts for all six
        // prologue/epilogue rows (idx 1,2,3,133,134,135) and the three around the
        // materialisation (35,37,92) -- nine of the twenty-nine.  Any fix has to
        // make MSVC prefer the GPR-plus-reload trade, so it is about pressure, not
        // about how `* 2.0f` is spelled.
        // RESIDUAL (w7-am, 94.2 canonical): MSVC hoists this 2.0f out of the
        // loop into a second callee-saved FPR (f30, plus the extra stfd), where
        // the image keeps only `lis r26, __real@40000000@ha` live and reloads
        // the literal every iteration at 0x82823CAC.  That is a register-
        // allocation heuristic, not a spelling: hoisting `params` out of the
        // loop (which the image's pre-loop `lwz r31,0x50(r1)` shows it does)
        // is byte-for-byte inert.
        params->mTopRadius = curSpotlight->mBeam.mTopRadius;
        params->mBottomRadius = curSpotlight->mBeam.mBottomRadius * 2.0f;
        params->mBeamLength = curSpotlight->mBeam.mLength * 2.0f;
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
        Spotlight *sl = it->mSpotlight;
        if (sl->LensMesh() != nullptr) {
            RndMesh *disk = Spotlight::GetDiskMesh();
            RndMultiMesh *nextMesh;
            if (disk != curDisk) {
                nextMesh = disk->CreateMultiMesh();
            } else {
                nextMesh = multiMesh;
            }
            const Transform &lensXfm = sl->LensXfm();
            bool visible;
            // MEASURED NEGATIVE (w8-q, 94.277 -> 92.858): flipping this to
            // `if (disk->Showing()) { sphere path } else { visible = false; }`.
            // The image tests mShowing and branches AWAY to the sphere path
            // (`lbz r10,0x8(r28)` / `cmplwi r10,0` / `bne 0x2448` at 0x2430) with
            // the visible=false arm in the fall-through, which is this spelling's
            // source order and NOT what MSVC gives us -- it inverts ours to
            // `beq` into the false arm.  Writing the arms the other way round does
            // not make MSVC invert a second time; it just loses rows elsewhere.
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
                bool diskChanged = (curDisk != disk);
                RndMat *lensMat = sl->LensMesh();
                bool matChanged = (curMat != lensMat);
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
    int numLights = sLights.size();
    if (numLights < TheNgStats->mSpotlights) {
        numLights = TheNgStats->mSpotlights;
    }
    TheNgStats->mSpotlights = numLights;
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
                    // RESIDUAL (w7-am, 98.5 canonical): the image multiplies
                    // blue, then green, then red -- Set()'s arguments evaluated
                    // right-to-left -- and we emit red, green, blue.  Refuted:
                    // inlining spot->Intensity() into all three arguments (no
                    // change), and writing the three products as separate
                    // member assignments in blue/green/red order (94.1, it
                    // splits the Color() base load in two).
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
                    it = std::vector<SpotlightEntry>::iterator(const_cast<SpotlightEntry *>(e2));
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
