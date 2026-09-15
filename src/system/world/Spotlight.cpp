#include "world\Spotlight.h"
#include "Spotlight.h"
#include "SpotlightDrawer.h"
#include "math\Color.h"
#include "math/Geo.h"
#include "math/Key.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "math\Utl.h"
#include "math\Vec.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os\Debug.h"
#include "os\Timer.h"
#include "rnddx9\Mesh.h"
#include "rndobj/Cam.h"
#include "rndobj\Draw.h"
#include "rndobj\Env.h"
#include "rndobj\Flare.h"
#include "rndobj\Group.h"
#include "rndobj\Mat.h"
#include "rndobj\Poll.h"
#include "rndobj\Rnd.h"
#include "rndobj\Trans.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include "world/LightPreset.h"

#ifdef HX_NATIVE
inline double __fsel(double a, double b, double c) { return a >= 0.0 ? b : c; }
#endif

RndMesh *Spotlight::sDiskMesh;
RndEnviron *Spotlight::sEnviron;

#pragma region BeamDef

Spotlight::BeamDef::BeamDef(Hmx::Object *owner)
    : mBeam(nullptr), mIsCone(false), mLength(100), mTopRadius(4), mBottomRadius(30),
      mTopSideBorder(0.1), mBottomSideBorder(0.3), mBottomBorder(0.5), mOffset(0),
      mTargetOffset(0, 0), mBrighten(1), mExpand(1), mShape(), mNumSections(0),
      mNumSegments(0), mXSection(owner), mCutouts(owner), mMat(owner) {}

Spotlight::BeamDef::BeamDef(const Spotlight::BeamDef &def)
    : mBeam(0), mIsCone(def.mIsCone), mLength(def.mLength), mTopRadius(def.mTopRadius),
      mBottomRadius(def.mBottomRadius), mTopSideBorder(def.mTopSideBorder),
      mBottomSideBorder(def.mBottomSideBorder), mBottomBorder(def.mBottomBorder),
      mOffset(def.mOffset), mTargetOffset(def.mTargetOffset), mBrighten(def.mBrighten),
      mExpand(def.mExpand), mShape(def.mShape), mNumSections(def.mNumSections),
      mNumSegments(def.mNumSegments),
      mXSection(def.mXSection.Owner(), def.mXSection.Ptr()), mCutouts(def.mCutouts),
      mMat(def.mMat.Owner(), def.mMat.Ptr()) {
    if (def.mBeam) {
        mBeam = Hmx::Object::New<RndMesh>();
        mBeam->Copy(def.mBeam, kCopyDeep);
    }
}

Spotlight::BeamDef::~BeamDef() { RELEASE(mBeam); }

void Spotlight::BeamDef::OnSetMat(RndMat *mat) {
    mMat = mat;
    if (mBeam)
        mBeam->SetMat(mMat);
}

void Spotlight::BeamDef::Save(BinStream &bs) const {
    bs << mIsCone;
    bs << mLength;
    bs << mBottomRadius;
    bs << mTopRadius;
    bs << mTopSideBorder;
    bs << mBottomSideBorder;
    bs << mBottomBorder;
    bs << mMat;
    bs << mOffset;
    bs << mTargetOffset;
    bs << mBrighten;
    bs << mXSection;
    bs << mExpand;
    bs << mShape;
    bs << mCutouts;
    bs << mNumSections;
    bs << mNumSegments;
}

void Spotlight::BeamDef::Load(BinStreamRev &d) {
    d >> mIsCone;
    d >> mLength;
    d >> mBottomRadius;
    d >> mTopRadius;
    d >> mTopSideBorder;
    d >> mBottomSideBorder;
    d >> mBottomBorder;
    d >> mMat;
    if (d.rev > 0x11 && d.rev < 0x13) {
        char name[0x80];
        d.stream.ReadString(name, 0x80);
    }
    d >> mOffset;
    if (d.rev < 10) {
        Vector4 v;
        d >> v;
    }
    d >> mTargetOffset;
    if (d.rev > 0x14) {
        d >> mBrighten;
        d >> mXSection;
    }
    if (d.rev > 0x17) {
        d >> mExpand;
    }
    if (d.rev > 0x1A) {
        d >> (int &)mShape;
    }
    if (d.rev > 0x18) {
        d >> mCutouts;
    }
    if (d.rev > 0x1F) {
        d >> mNumSections;
        d >> mNumSegments;
    }
}

Vector2 Spotlight::BeamDef::NGRadii() const {
    Vector2 v;
    float vx = mTopRadius * mExpand;
    float vy = mBottomRadius * mExpand;
    if (!mIsCone) {
        vy *= 1.0f - mBottomSideBorder * 0.7f;
        vx *= 1.0f - mTopSideBorder * 0.7f;
    }
    v.Set(vx, vy);
    return v;
}

#pragma endregion
#pragma region Spotlight

Spotlight::Spotlight()
    : mSpotMaterial(this), mFlare(Hmx::Object::New<RndFlare>()), mFlareEnabled(true),
      mFlareVisibilityTest(true), mFlareOffset(0), mSpotScale(30), mSpotHeight(0.25),
      mColor(1, 1, 1), mIntensity(1), mColorOwner(this, this), mLensSize(0),
      mLensOffset(0), mLensMaterial(this), mBeam(this), mSlaves(this),
      mLightCanMesh(this), mLightCanOffset(0), mTarget(this), mTargetLoaded(true),
      mSpotTarget(this), mFloorSpotTargetZ(-1e33), mTargetShadow(false), mLightCanSort(false),
      mSnapToTarget(true), mDampingConstant(1), mAdditionalObjects(this),
      mAnimateColorFromPreset(true), mAnimateOrientationFromPreset(true), mUpdating(false) {
    mFlare->SetTransParent(this, false);
    mFloorSpotXfm.Reset();
    mLensXfm.Reset();
    mLightCanXfm.Reset();
    mOrientMatrix.Identity();
    mLastTargetPos.Zero();
    mDampQuat.Reset();
    mOrder = -1000;
}

Spotlight::~Spotlight() {
    CloseSlaves();
    SpotlightDrawer::RemoveFromLists(this);
    RELEASE(mFlare);
}

bool Spotlight::Replace(ObjRef *from, Hmx::Object *to) {
    if (&mColorOwner == from) {
        if (!mColorOwner.SetObj(to)) {
            mColorOwner = this;
        }
        return true;
    } else {
        return RndTransformable::Replace(from, to);
    }
}

BEGIN_HANDLERS(Spotlight)
    HANDLE_ACTION(propogate_targeting_to_presets, PropogateToPresets(2))
    HANDLE_ACTION(propogate_coloring_to_presets, PropogateToPresets(1))
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(Spotlight)
    SYNC_PROP_MODIFY(length, mBeam.mLength, Generate())
    SYNC_PROP_MODIFY(top_radius, mBeam.mTopRadius, Generate())
    SYNC_PROP_MODIFY(bottom_radius, mBeam.mBottomRadius, Generate())
    SYNC_PROP_MODIFY(top_side_border, mBeam.mTopSideBorder, Generate())
    SYNC_PROP_MODIFY(bottom_side_border, mBeam.mBottomSideBorder, Generate())
    SYNC_PROP_MODIFY(bottom_border, mBeam.mBottomBorder, Generate())
    SYNC_PROP_SET(material, mBeam.mMat.Ptr(), mBeam.OnSetMat(_val.Obj<RndMat>()))
    SYNC_PROP_MODIFY(offset, mBeam.mOffset, Generate())
    SYNC_PROP_MODIFY(angle_offset, mBeam.mTargetOffset, Generate())
    SYNC_PROP_MODIFY(is_cone, mBeam.mIsCone, Generate())
    SYNC_PROP(brighten, mBeam.mBrighten)
    SYNC_PROP_MODIFY(expand, mBeam.mExpand, Generate())
    SYNC_PROP_MODIFY(shape, (int &)mBeam.mShape, Generate())
    SYNC_PROP(xsection, mBeam.mXSection)
    SYNC_PROP(cutouts, mBeam.mCutouts)
    SYNC_PROP_MODIFY(sections, mBeam.mNumSections, Generate())
    SYNC_PROP_MODIFY(segments, mBeam.mNumSegments, Generate())
    SYNC_PROP_MODIFY(light_can, mLightCanMesh, UpdateBounds())
    SYNC_PROP_MODIFY(light_can_offset, mLightCanOffset, UpdateBounds())
    SYNC_PROP(light_can_sort, mLightCanSort)
    SYNC_PROP_MODIFY(target, mTarget, UpdateTransforms())
    SYNC_PROP(target_shadow, mTargetShadow)
    SYNC_PROP_SET(flare_material, mFlare->GetMat(), mFlare->SetMat(_val.Obj<RndMat>()))
    SYNC_PROP(flare_size, mFlare->Sizes())
    SYNC_PROP(flare_range, mFlare->Range())
    SYNC_PROP_SET(flare_steps, mFlare->GetSteps(), mFlare->SetSteps(_val.Int()))
    SYNC_PROP_MODIFY(flare_offset, mFlareOffset, UpdateBounds())
    SYNC_PROP_MODIFY(flare_enabled, mFlareEnabled, UpdateFlare())
    SYNC_PROP_SET(
        flare_visibility_test, !mFlareVisibilityTest, SetFlareIsBillboard(!_val.Int())
    )
    SYNC_PROP_MODIFY(spot_target, mSpotTarget, UpdateBounds())
    SYNC_PROP_MODIFY(spot_scale, mSpotScale, UpdateBounds())
    SYNC_PROP_MODIFY(spot_height, mSpotHeight, UpdateBounds())
    SYNC_PROP_MODIFY(spot_material, mSpotMaterial, UpdateBounds())
    SYNC_PROP_SET(color, Color().Pack(), SetColor(_val.Int()))
    // NOT a bug, despite how the inlined code reads. SetIntensity ->
    // SetColorIntensity(Color(), f) expands to `mColorOwner->mColor =
    // mColorOwner->mColor`, and the target really does emit that 16-byte
    // self-copy. Two rewrites were measured and are both WORSE, so leave it:
    //   SetColorIntensity(Color(), _val.Float())  -> 8 rows becomes 15
    //   binding `const Hmx::Color &c = Color();` inside SetIntensity
    //                                             -> 8 rows becomes 9 (adds a spill)
    // The 8 residual rows are all anchoring: the target keeps mColorOwner in
    // one register for the destination and mColorOwner+0x1b0 in another for the
    // source, plus two dead address computations; our build CSEs them into a
    // single anchor. Hmx::Color has no user-declared operator= in the target
    // (`??4Color@Hmx@@` appears in 0 of the target objects), so that is not it.
    //   SetColorIntensity(Hmx::Color(Color()), _val.Float()) -- RB3's spelling
    //                                             -> 8 rows becomes 25, 98.9,
    //                                                +0x10 stack frame
    // Decoded fully at 8282EDD0: the image emits exactly two instructions we do
    // not -- `mr r9, r11` (saving mColorOwner before the +0x1b0 clobbers it) and
    // a DEAD `addi r10, r9, 0x1b0` that its stores then fold into their own
    // displacements. That dead address computation is the tell: the image is
    // materialising BOTH sides of `mColorOwner->mColor = c` as pointers, the way
    // MSVC lowers an operator= call, while we lower it as one anchored struct
    // copy. 4764 B at 99.8287 == those two instructions and nothing else.
    SYNC_PROP_SET(intensity, Intensity(), SetIntensity(_val.Float()))
    SYNC_PROP(color_owner, mColorOwner)
    SYNC_PROP(damping_constant, mDampingConstant)
    SYNC_PROP_MODIFY(lens_size, mLensSize, UpdateBounds())
    SYNC_PROP_MODIFY(lens_offset, mLensOffset, UpdateBounds())
    SYNC_PROP_MODIFY(lens_material, mLensMaterial, UpdateBounds())
    SYNC_PROP(additional_objects, mAdditionalObjects)
    SYNC_PROP(slaves, mSlaves)
    SYNC_PROP(animate_orientation_from_preset, mAnimateOrientationFromPreset)
    SYNC_PROP(animate_color_from_preset, mAnimateColorFromPreset)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(RndPollable)
END_PROPSYNCS

void Spotlight::InitObject() {
    Hmx::Object::InitObject();
    Generate();
}

BEGIN_SAVES(Spotlight)
    SAVE_REVS(0x21, 0)
    SAVE_SUPERCLASS(RndPollable)
    SAVE_SUPERCLASS(RndDrawable)
    SAVE_SUPERCLASS(RndTransformable)
    bs << mSpotScale;
    bs << mSpotHeight;
    mBeam.Save(bs);
    bs << mLightCanMesh;
    bs << mTarget;
    bs << mSpotTarget;
    bs << mLightCanOffset;
    bs << mLightCanSort;
    bs << mColor;
    bs << mIntensity;
    bs << mSpotMaterial;
    bs << mDampingConstant;
    ObjPtr<RndMat> mat(this, mFlare->GetMat());
    bs << mat;
    bs << mFlare->Sizes();
    bs << mFlare->Range();
    bs << mFlare->GetSteps();
    bs << mFlareOffset;
    bs << mFlareEnabled;
    bs << mFlareVisibilityTest;
    bs << mLensSize;
    bs << mLensOffset;
    bs << mLensMaterial;
    bs << mAdditionalObjects;
    bs << mSlaves;
    bs << mTargetShadow;
    bs << mAnimateColorFromPreset;
    bs << mAnimateOrientationFromPreset;
    bs << mColorOwner;
END_SAVES

BEGIN_COPYS(Spotlight)
    COPY_SUPERCLASS(RndPollable)
    COPY_SUPERCLASS(RndTransformable)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(Spotlight)
    BEGIN_COPYING_MEMBERS
        if (ty != kCopyFromMax) {
            mFlare->Copy(c->mFlare, kCopyDeep);
            COPY_MEMBER(mFlareOffset)
            COPY_MEMBER(mLightCanMesh)
            COPY_MEMBER(mTarget)
            COPY_MEMBER(mSpotTarget)
            COPY_MEMBER(mSpotScale)
            COPY_MEMBER(mSpotHeight)
            SetColorIntensity(c->Color(), c->Intensity());
            COPY_MEMBER(mSpotMaterial)
            COPY_MEMBER(mDampingConstant)
            COPY_MEMBER(mLensSize)
            COPY_MEMBER(mLensOffset)
            COPY_MEMBER(mLensMaterial)
            COPY_MEMBER(mLightCanOffset)
            COPY_MEMBER(mLightCanSort)
            COPY_MEMBER(mFlareEnabled)
            COPY_MEMBER(mFlareVisibilityTest)
            UpdateFlare();
            COPY_MEMBER(mTargetShadow)
            COPY_MEMBER(mAnimateColorFromPreset)
            COPY_MEMBER(mAnimateOrientationFromPreset)
            COPY_MEMBER(mAdditionalObjects)
            COPY_MEMBER(mSlaves)
            COPY_MEMBER(mBeam.mIsCone)
            COPY_MEMBER(mBeam.mLength)
            COPY_MEMBER(mBeam.mBottomRadius)
            COPY_MEMBER(mBeam.mTopRadius)
            COPY_MEMBER(mBeam.mTopSideBorder)
            COPY_MEMBER(mBeam.mBottomSideBorder)
            COPY_MEMBER(mBeam.mBottomBorder)
            COPY_MEMBER(mBeam.mMat)
            COPY_MEMBER(mBeam.mTargetOffset)
            COPY_MEMBER(mBeam.mBrighten)
            COPY_MEMBER(mBeam.mExpand)
            COPY_MEMBER(mBeam.mShape)
            COPY_MEMBER(mBeam.mXSection)
            COPY_MEMBER(mBeam.mCutouts)
            COPY_MEMBER(mBeam.mOffset)
            COPY_MEMBER(mBeam.mNumSections)
            COPY_MEMBER(mBeam.mNumSegments)
            if (c->mBeam.mBeam) {
                mBeam.mBeam = Hmx::Object::New<RndMesh>();
                mBeam.mBeam->Copy(c->mBeam.mBeam, kCopyDeep);
            }
            Generate();
        }
    END_COPYING_MEMBERS
END_COPYS

BinStreamRev &operator>>(BinStreamRev &d, Spotlight::BeamDef &bd) {
    bd.Load(d);
    return d;
}

INIT_REVS(0x21, 0)

BEGIN_LOADS(Spotlight)
    char bufSpot[0x80];
    char bufFlare[0x80];
    char bufLens[0x80];
    LOAD_REVS(bs)
    ASSERT_REVS(0x21, 0)
    if (d.rev < 9) {
        MILO_FAIL("Unsupported spotlight version");
    } else {
        RndPollable::Load(bs);
        RndDrawable::Load(bs);
        RndTransformable::Load(bs);
        bs >> mSpotScale;
        bs >> mSpotHeight;
        if (d.rev > 0x16) {
            mBeam.Load(d);
        } else {
            ObjVector<BeamDef> beams(this);
            d >> beams;
            MILO_ASSERT(beams.size() <= 1, 0xCD);
            if (beams.size() != 0) {
                mBeam = beams[0];
            } else {
                mBeam.mLength = 0;
            }
        }
        if (d.rev > 0x15) {
            d >> mLightCanMesh;
        } else {
            ObjPtr<RndGroup> group(this);
            d >> group;
            ConvertGroupToMesh(group);
        }
        if (!mTarget.Load(bs, false, 0)) {
            mTargetLoaded = false;
        }
        if (d.rev > 0x1C) {
            d >> mSpotTarget;
        }
        d >> mLightCanOffset;
        if (d.rev > 0x1E) {
            d >> mLightCanSort;
        }
        d >> mColor;
        mColor.alpha = 1;
        if (d.rev > 9) {
            d >> mIntensity;
        }
        d >> mSpotMaterial;
        if (d.rev > 0x11 && d.rev < 0x13) {
            bs.ReadString(bufSpot, 0x80);
            if (!mSpotMaterial && bufSpot[0] != '\0') {
                mSpotMaterial = LookupOrCreateMat(bufSpot, Dir());
            }
        }
        d >> mDampingConstant;
        if (d.rev < 0x21) {
            Symbol s;
            d >> s;
        }
        if (d.rev > 10) {
            ObjPtr<RndMat> mat(this);
            d >> mat;
            mFlare->SetMat(mat);
            if (d.rev > 0x11 && d.rev < 0x13) {
                bs.ReadString(bufFlare, 0x80);
                if (!mat && bufFlare[0] != '\0') {
                    mat = LookupOrCreateMat(bufFlare, Dir());
                    mFlare->SetMat(mat);
                }
            }
            bs >> (Key<float> &)mFlare->Sizes();
            bs >> (Key<float> &)mFlare->Range();
            int steps;
            d >> steps;
            mFlare->SetSteps(steps);
            d >> mFlareOffset;
        }
        if (d.rev > 0xD) {
            d >> mFlareEnabled;
        }
        if (d.rev > 0xE) {
            d >> mFlareVisibilityTest;
        }
        UpdateFlare();
        if (d.rev > 0xB) {
            d >> mLensSize;
            d >> mLensOffset;
            d >> mLensMaterial;
        }
        if (d.rev > 0x11 && d.rev < 0x13) {
            bs.ReadString(bufLens, 0x80);
            if (!mLensMaterial && bufLens[0] != '\0') {
                mLensMaterial = LookupOrCreateMat(bufLens, Dir());
            }
        }
        if (d.rev > 0xC) {
            d >> mAdditionalObjects;
        }
        if (d.rev > 0x1B) {
            d >> mSlaves;
        }
        if (d.rev > 0xF) {
            d >> mTargetShadow;
        }
        if (d.rev > 0x19) {
            d >> mAnimateColorFromPreset;
            d >> mAnimateOrientationFromPreset;
        } else if (d.rev > 0x10) {
            d >> mAnimateColorFromPreset;
            mAnimateOrientationFromPreset = mAnimateColorFromPreset;
        }
        if (d.rev > 0x1D) {
            d >> mColorOwner;
            if (!mColorOwner) {
                mColorOwner = this;
            }
        }
        Generate();
    }
END_LOADS

void Spotlight::DrawShowing() {
    START_AUTO_TIMER("spotlight");
    if (mLightCanSort && mLightCanMesh) {
        mLightCanMesh->SetWorldXfm(mLightCanXfm);
        Sphere s(mLightCanMesh->GetSphere());
        if (s.GetRadius() > 0) {
            Multiply(s, mLightCanXfm, s);
            if (!(s > RndCam::Current()->WorldFrustum())) {
                mLightCanMesh->DrawShowing();
            }
        }
    }
    // Two early returns, not an if/else-if chain.  82828C9C `bne cr6` skips the
    // DrawLight arm, and 82828CB4 `bne` is followed by its OWN scope exit
    // (`addi r3, r31, 0x80; b <~AutoTimer>`) rather than a branch into the
    // common tail -- that second copy only appears for an explicit `return`.
    if (TheRnd.DrawMode() == Rnd::kDrawNormal) {
        SpotlightDrawer::DrawLight(this);
        return;
    }
    if (!mTargetLoaded)
        return;
    UpdateTransforms();
    // w7-bo (2026-09-15): 97.56 -> 97.60 canonical, 21 mismatch rows -> 8.
    // Lever: SIBLING block scopes for `c` and `tracker`.  The image gives
    // `tracker` the SAME frame word as `c` -- it builds the colour at r31+0x60
    // (82828D08..D18, target idx 87 `addi r9, r31, 0x60`) and then passes
    // r31+0x60 to ??0RndEnvironTracker (82828D44, target idx 92 `addi r3, r31,
    // 0x60`) and to ??1RndEnvironTracker (82828FB0).  With `c` at function
    // scope our build put it at 0x60 and `tracker` at 0xa0; that one extra word
    // shifted `_at` 0x70->0x80 and the Sphere 0x80->0x90, i.e. all fourteen
    // [off:-16] diff_arg rows plus a frame Δ +0x10.
    // Measured negatives:
    //   - `c` alone in its own closing scope: byte-identical, 97.56 both ways
    //     (the earlier note here claimed the reuse was therefore unobtainable;
    //     it is not -- `tracker` has to be scoped TOO, so the two scopes are
    //     siblings and MSVC coalesces the slots).
    // Residual (8 rows) is a store-scheduling group around the tracker ctor --
    // the image sinks `stw r8, 0x50(r31)` past the two `stw`s our build emits
    // first, and hoists `addi r8, r10, 0x13c` (the RndEnviron vtable/field
    // pointer) above them:
    //   [95]  insert   stw  r7, 0x4(r9)        [96]  insert   stw  r6, 0x8(r9)
    //   [102] delete   stw  r8, 0x0(r9)        [103] delete   stw  r7, 0x4(r9)
    //   [104] delete   addi r8, r10, 0x13c     [105] diff_arg stw [reg:r6->r8, off:-8]
    //   [109] replace  stw r8, 0x50(r31) vs addi r8, r10, 0x13c
    //   [117] insert   stw  r8, 0x50(r31)
    // Same instruction MULTISET on both sides -- only the schedule differs.
    // Target spends r8 on `addi r8, r10, 0x13c` at idx 104, which forces
    // `stw r8, 0x0(r9)` ahead of it at 102 and the whole c-copy lands in
    // address order 0x0/0x4/0x8/0xc; our build keeps r8 holding the 0x1b0
    // word longer, fires 0x4/0x8 as soon as their loads retire (95/96), and
    // pushes the `addi`+home store past the fmuls.  Pure MSVC store
    // scheduling driven by r8's live range.
    // Further measured negatives, both 97.60 / 8 rows, byte-for-byte the same
    // eight rows:
    //   - `Hmx::Color c = Color();` copy-initialisation instead of direct-init
    //   - `UpdateTransforms();` moved INSIDE the scope (the RB3 sibling's
    //     shape -- rb3 Spotlight.cpp puts UpdateTransforms, c48 and tracker in
    //     one block; that block shape is what we had at 97.56/21 rows, so the
    //     sibling-scope split is the DC3-specific lever, not a port of rb3).
    {
        Hmx::Color c(Color());
        Multiply(c, Intensity(), c);
        sEnviron->SetAmbientColor(c);
    }
    {
        RndEnvironTracker tracker(sEnviron, nullptr);
        FOREACH (it, mAdditionalObjects) {
            MILO_ASSERT(*it != this, 0x3E3);
            if (*it != this)
                (*it)->DrawShowing();
        }
        if (mLensMaterial) {
            MILO_ASSERT(sDiskMesh, 0x3ED);
            sDiskMesh->SetWorldXfm(mLensXfm);
            sDiskMesh->SetMat(mLensMaterial);
            sDiskMesh->DrawShowing();
        }
        auto& _ref3 = mBeam;
        if (_ref3.mBeam && TheRnd.DrawMode() != 5) {
            _ref3.mBeam->DrawShowing();
        }
        if (mFlare && mFlare->GetMat()) {
            mFlare->Draw();
        }
        if (mTarget) {
            if (mTargetShadow) {
                RndDrawable *drawable = dynamic_cast<RndDrawable *>(mTarget.Ptr());
                if (drawable) {
                    drawable->DrawShadow(WorldXfm(), 3.0f);
                }
            }
            if (DoFloorSpot()) {
                MILO_ASSERT(sDiskMesh, 0x40F);
                sDiskMesh->SetWorldXfm(mFloorSpotXfm);
                sDiskMesh->SetMat(mSpotMaterial);
                sDiskMesh->DrawShowing();
            }
        }
    }
}

bool Spotlight::MakeWorldSphere(Sphere &s, bool b) {
    if (b) {
        s.Zero();
        if (mBeam.mBeam) {
            Sphere s28;
            if (mBeam.mBeam->MakeWorldSphere(s28, true)) {
                s.GrowToContain(s28);
            }
        }
        if (DoFloorSpot()) {
            MILO_ASSERT(sDiskMesh, 0x2FD);
            Sphere s38;
            sDiskMesh->SetWorldXfm(mFloorSpotXfm);
            if (sDiskMesh->MakeWorldSphere(s38, true)) {
                s.GrowToContain(s38);
            }
        }
        if (mFlare) {
            Sphere s48;
            if (mFlare->MakeWorldSphere(s48, true)) {
                s.GrowToContain(s48);
            }
        }
        if (mLightCanMesh) {
            Sphere s58;
            mLightCanMesh->SetWorldXfm(mLightCanXfm);
            if (mLightCanMesh->MakeWorldSphere(s58, true)) {
                s.GrowToContain(s58);
            }
        }
        return true;
    } else if (mSphere.GetRadius()) {
        Multiply(mSphere, WorldXfm(), s);
        return true;
    } else
        return false;
}

void Spotlight::Mats(std::list<RndMat *> &mats, bool addAO) {
    if (mLensMaterial && addAO) {
        mats.push_back(mLensMaterial);
        for (unsigned int i = 0; i < 2U; i++) {
            MatShaderOptions opts;
            opts.SetLast5(0xC);
            opts.mTempMat = true;
            opts.SetHasAOCalc(i);
            RndMat *mat = Hmx::Object::New<RndMat>();
            mat->Copy(mLensMaterial, kCopyDeep);
            mat->SetShaderOpts(opts);
            mats.push_back(mat);
        }
    }
    if (mSpotMaterial) {
        mats.push_back(mSpotMaterial);
    }
    if (mLightCanMesh && mLightCanMesh->Mat()) {
        MatShaderOptions opts;
        opts.SetLast5(0xC);
        RndMat *lightMat = mLightCanMesh->Mat();
        lightMat->SetShaderOpts(opts);
        mats.push_back(lightMat);
        if (addAO) {
            for (unsigned int i = 0; i < 2U; i++) {
                MatShaderOptions opts2;
                opts2.SetLast5(0xC);
                opts2.mTempMat = true;
                opts2.SetHasAOCalc(i);
                RndMat *mat = Hmx::Object::New<RndMat>();
                mat->Copy(mLightCanMesh->Mat(), kCopyDeep);
                mat->SetShaderOpts(opts2);
                mats.push_back(mat);
            }
        }
    }
    if (mBeam.mMat) {
        mats.push_back(mBeam.mMat);
    }
}

void Spotlight::ListDrawChildren(std::list<RndDrawable *> &draws) {
    if (mLightCanMesh)
        draws.push_back(mLightCanMesh);
    FOREACH (it, mAdditionalObjects) {
        draws.push_back(*it);
    }
}

RndDrawable *Spotlight::CollideShowing(const Segment &s, float &f, Plane &p) {
    if (mLightCanMesh) {
        mLightCanMesh->SetWorldXfm(mLightCanXfm);
        bool showing = mLightCanMesh->Showing();
        mLightCanMesh->SetShowing(true);
        bool collide = mLightCanMesh->Collide(s, f, p);
        mLightCanMesh->SetShowing(showing);
        if (collide) {
            return this;
        }
    }
    return nullptr;
}

int Spotlight::CollidePlane(const Plane &pl) {
    if (mLightCanMesh) {
        mLightCanMesh->SetWorldXfm(mLightCanXfm);
        bool oldshowing = mLightCanMesh->Showing();
        mLightCanMesh->SetShowing(true);
        int coll = mLightCanMesh->CollidePlane(pl);
        mLightCanMesh->SetShowing(oldshowing);
        if (coll)
            return coll;
    }
    return -1;
}

void Spotlight::UpdateBounds() {
    UpdateTransforms();
    UpdateSphere();
}

void Spotlight::SetFlareIsBillboard(bool b) {
    mFlareVisibilityTest = b;
    UpdateFlare();
}

void Spotlight::SetColor(int packed) {
    Hmx::Color color;
    color.Unpack(packed);
    color.alpha = 1.0f;
    SetColorIntensity(color, Intensity());
}
// RB3's shared-engine spelling of this line is
//   SetColorIntensity(Hmx::Color(Color()), f);
// (../rb3/src/system/world/Spotlight.cpp:499). Do NOT port it: measured here it
// materialises the temporary in a stack slot and takes SyncProperty from 99.829
// to 98.9 with a +0x10 frame. RB3's SetColorIntensity body also differs from
// DC3's (it Multiplies by f and round-trips through Hmx::Color32(col.Pack())),
// and the DC3 target emits a plain 4-word copy with none of that -- so the two
// games genuinely diverge here and RB3 is not a reference for this function.
void Spotlight::SetIntensity(float f) { SetColorIntensity(Color(), f); }

void Spotlight::SetColorIntensity(const Hmx::Color &c, float f) {
    mColorOwner->mColor = c;
    mColorOwner->mIntensity = f;
}

void Spotlight::Init() {
    REGISTER_OBJ_FACTORY(Spotlight)
    sEnviron = Hmx::Object::New<RndEnviron>();
    BuildBoard();
}

void Spotlight::BuildBoard() {
#ifdef HX_NATIVE
    return; // Skip mesh setup on native — no renderer
#endif
    MILO_ASSERT(!sDiskMesh, 0x42E);
    sDiskMesh = Hmx::Object::New<RndMesh>();
    RndMesh::VertVector &verts = sDiskMesh->Verts();
    std::vector<RndMesh::Face> &faces = sDiskMesh->Faces();
    verts.resize(4);
    faces.resize(2);

    verts[0].pos.Set(-0.5, -0.5, 0);
    verts[0].color = Hmx::Color(1, 1, 1);
    verts[0].tex.Set(0, 0);

    verts[1].pos.Set(0.5, -0.5, 0);
    verts[1].color = Hmx::Color(1, 1, 1);
    verts[1].tex.Set(1, 0);

    verts[2].pos.Set(-0.5, 0.5, 0);
    verts[2].color = Hmx::Color(1, 1, 1);
    verts[2].tex.Set(0, 1);

    verts[3].pos.Set(0.5, 0.5, 0);
    verts[3].color = Hmx::Color(1, 1, 1);
    verts[3].tex.Set(1, 1);

    faces[0].Set(0, 1, 2);
    faces[1].Set(1, 3, 2);
    sDiskMesh->Sync(0x13F);
    DxMesh *dxDiskMesh = static_cast<DxMesh *>(sDiskMesh);
    dxDiskMesh->GetMultimeshFaces();
    sDiskMesh->UpdateSphere();
}

void Spotlight::UpdateFlare() {
    // Configure flare visibility and testing modes based on enabled/visibility flags.
    // Note: Local variable 'flare' required for register allocation match.
    RndFlare *flare;
    if (!mFlareEnabled) {
        // Flare disabled: hide and disable point testing
        flare = mFlare;
        flare->SetOcclusionReady(true);
        flare->SetVisible(false);
        mFlare->SetPointTest(false);
    } else if (mFlareVisibilityTest) {
        // Flare with visibility test: show but disable point testing
        flare = mFlare;
        flare->SetOcclusionReady(true);
        flare->SetVisible(true);
        mFlare->SetPointTest(false);
    } else
        // Flare always visible: enable point testing (billboard mode)
        mFlare->SetPointTest(true);
}

bool Spotlight::DoFloorSpot() const {
    return mSpotMaterial && GetFloorSpotTarget()
        && GetFloorSpotTarget()->WorldXfm().m.y.z;
}

void Spotlight::CalculateDirection(RndTransformable *target, Hmx::Matrix3 &mtx) {
    MILO_ASSERT(target, 0x2CE);
    Vector3 v20;
    Subtract(target->WorldXfm().v, WorldXfm().v, v20);
    Vector3 v2c;
    Cross(v20, Vector3(1.0f, 0.0f, 0.0f), v2c);
    Normalize(v2c, v2c);
    MakeRotMatrix(v20, v2c, mtx);
}

void Spotlight::SetFlareEnabled(bool b) {
    mFlareEnabled = b;
    UpdateFlare();
}

void Spotlight::CloseSlaves() {
    FOREACH (it, mSlaves) {
        RndLight *lit = *it;
        if (lit)
            lit->SetShadowOverride(0);
    }
}

void Spotlight::UpdateSlaves() {
    if (mSlaves.empty())
        return;
    else {
        FOREACH (it, mSlaves) {
            RndLight *lit = *it;
            Transform tf40(WorldXfm());
            if (lit->TransParent()) {
                Transform tf70;
                Invert(lit->TransParent()->WorldXfm(), tf70);
                Multiply(WorldXfm(), tf70, tf40);
            }
            lit->SetLocalXfm(tf40);
            lit->SetShadowOverride(&mBeam.mCutouts);
            lit->SetShowing(Showing());
        }
    }
}

void Spotlight::CheckFloorSpotTransform() {
    if (DoFloorSpot()) {
        if (GetFloorSpotTarget()->WorldXfm().v.z != mFloorSpotTargetZ) {
            UpdateFloorSpotTransform(WorldXfm());
        }
    }
}

void Spotlight::ConvertGroupToMesh(RndGroup *grp) {
    if (grp) {
        int count = 0;
        std::vector<RndDrawable *>::const_iterator it = grp->Draws().begin();
        std::vector<RndDrawable *>::const_iterator itEnd = grp->Draws().end();
        for (; it != itEnd; it++) {
            RndMesh *cur = dynamic_cast<RndMesh *>(*it);
            if (cur) {
                count++;
                if (!mLightCanMesh)
                    mLightCanMesh = cur;
            }
        }
        if (count > 1) {
            MILO_NOTIFY(
                "Multiple meshes (%d) found converting light can group %s to mesh",
                count,
                grp->Name()
            );
        }
    }
}

void Spotlight::PropogateToPresets(int i) {
    for (ObjDirItr<LightPreset> it(Dir(), false); it != nullptr; ++it) {
        it->SetSpotlight(this, i);
    }
}

void Spotlight::Generate() {
    if (!mBeam.mBeam || TheLoadMgr.EditMode()) {
        RELEASE(mBeam.mBeam);
        if (mBeam.HasLength()) {
            if (SpotlightDrawer::DrawNGSpotlights()) {
                BuildNGShaft(mBeam);
            } else if (mBeam.IsCone()) {
                BuildCone(mBeam);
            } else {
                BuildBeam(mBeam);
            }
        }
        UpdateBounds();
        UpdateSphere();
    }
}

void Spotlight::BuildNGShaft(Spotlight::BeamDef &def) {
    switch (def.mShape) {
    case BeamDef::kBeamRect:
        BuildNGCone(def, 4);
        break;
    case BeamDef::kBeamSheet:
        BuildNGSheet(def);
        break;
    case BeamDef::kBeamQuadXYZ:
        BuildNGQuad(def, RndTransformable::kConstraintBillboardXYZ);
        break;
    case BeamDef::kBeamQuadZ:
        BuildNGQuad(def, RndTransformable::kConstraintBillboardZ);
        break;
    default:
        int num = def.mNumSegments;
        if (def.mNumSegments <= 3) {
            num = 10;
        }
        BuildNGCone(def, num);
        break;
    }
}

void Spotlight::Poll() {
    if (!TheLoadMgr.EditMode()) {
        if (!Showing())
            return;
        if (mIntensity == 0)
            return;
    }
    Hmx::Matrix3 m;
    if (!mUpdating) {
        RndTransformable *target = nullptr;
        if (mTargetLoaded)
            target = mTarget;
        if (!target
            || (!TheLoadMgr.EditMode() && !mSnapToTarget
                && target->WorldXfm().v == mLastTargetPos)) {
            if (!target && !mAnimateOrientationFromPreset && !DoFloorSpot()) {
                UpdateTransforms();
            } else {
                CheckFloorSpotTransform();
                mOrientMatrix = WorldXfm().m;
                UpdateSlaves();
            }
            Normalize(mLocalXfm.m, m);
            SetLocalRot(m);
            return;
        }
        mLastTargetPos = target->WorldXfm().v;
        CalculateDirection(target, m);
        if (!mSnapToTarget && mDampingConstant != 1.0f) {
            Interp(mOrientMatrix, m, TheTaskMgr.DeltaSeconds() * mDampingConstant, m);
        } else {
            mSnapToTarget = false;
        }
    } else {
        MakeRotMatrix(mDampQuat, m);
    }
    Normalize(m, m);
    SetLocalRot(m);
    mOrientMatrix = m;
    UpdateTransforms();
    mUpdating = false;
}

void Spotlight::UpdateTransforms() {
    START_AUTO_TIMER("spotlight_xfm");
    const Transform &thetf = WorldXfm();
    mLightCanXfm = thetf;
    Vector3 vcc(mLightCanXfm.m.y);
    vcc *= mLightCanOffset;
    Add(mLightCanXfm.v, vcc, mLightCanXfm.v);
    static Hmx::Matrix3 ident(
        Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)
    );
    static Hmx::Matrix3 rot(
        Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, -1.0f, 0.0f)
    );
    if (mLensMaterial) {
        Vector3 vd8(0.0f, mLensOffset, 0.0f);
        // Multiply(vd8, thetf.m, vd8) written out with the sums right
        // associated.  vd8's x and z are literal 0.0f, and fed to the shared
        // overload in Mtx.h that lets /fp:fast reassociate
        // `m.x.c*0 + m.y.c*off + m.z.c*0` into `(m.x.c + m.z.c)*0 + ...`,
        // emitting a leading `fadds` of two matrix elements.  The target emits
        // the three products straight and seeds each accumulator from the z
        // term.  The parentheses are load-bearing; without them this function
        // reads 91.96.  See the comment above the overload in Mtx.h for why the
        // fix belongs here and not there.
        {
            const Hmx::Matrix3 &m = thetf.m;
            vd8.Set(
                m.x.x * vd8.x + (m.y.x * vd8.y + m.z.x * vd8.z),
                m.x.y * vd8.x + (m.y.y * vd8.y + m.z.y * vd8.z),
                m.x.z * vd8.x + (m.y.z * vd8.y + m.z.z * vd8.z)
            );
        }
        Add(vd8, thetf.v, vd8);
        Hmx::Matrix3 m48;
        m48.Set(
            Vector3(-mLensSize, 0.0f, 0.0f),
            Vector3(0.0f, 0.0f, mLensSize),
            Vector3(0.0f, mLensSize, 0.0f)
        );
        Multiply(m48, thetf.m, m48);
        mLensXfm = Transform(m48, vd8);
    }
    if (mBeam.mBeam) {
        Vector3 ve4(0.0f, mBeam.mOffset, 0.0f);
        mBeam.mBeam->SetLocalPos(ve4);
        Hmx::Matrix3 m6c(mBeam.mIsCone ? rot : ident);
        Hmx::Matrix3 m90;
        MakeRotMatrix(
            Vector3(
                mBeam.mTargetOffset.x * DEG2RAD, 0.0f, mBeam.mTargetOffset.y * DEG2RAD
            ),
            m90,
            true
        );
        Multiply(m6c, m90, m6c);
        mBeam.mBeam->SetLocalRot(m6c);
    }
    if (mFlare && mFlare->GetMat()) {
        Vector3 vf0(0.0f, mFlareOffset, 0.0f);
        mFlare->SetLocalPos(vf0);
        mFlare->SetLocalRot(ident);
    }
    UpdateFloorSpotTransform(thetf);
    UpdateSlaves();
}

void Spotlight::UpdateFloorSpotTransform(const Transform &tf) {
    mFloorSpotXfm.Reset();
    if (DoFloorSpot()) {
        float f1 = GetFloorSpotTarget()->WorldXfm().v.z;
        Vector3 vac(tf.m.y);
        if (vac.z != 0) {
            float absed = std::fabs(((f1 - tf.v.z) / vac.z) / (f1 - tf.v.z));
            vac = tf.m.y;
            float curz = vac.z;
            vac.z = 0;
            Hmx::Matrix3 m70;
            if (curz > -0.9999999f && curz < 0.9999999f)
                MakeRotMatrix(vac, Vector3(0.0f, 0.0f, 1.0f), m70);
            else
                m70.Identity();
            vac.Set(mSpotScale, mSpotScale * absed, 1.0f);
            Scale(vac, m70, m70);
            float scalar = (f1 + mSpotHeight - tf.v.z) / curz;
            vac = tf.m.y;
            vac *= scalar;
            Add(vac, tf.v, vac);
            mFloorSpotXfm = Transform(m70, vac);
        }
        mFloorSpotTargetZ = f1;
    }
}

// w7-bs (2026-09-15): 70.20 -> 85.3 canonical, 1628 B, three levers on top of
// w7-bo's two, each measured alone:
//  - pos.Set(x, y, z) per column (73.9): the image writes each column's pos
//    with ONE verts.begin() reload (0x8282D988 `lwz r9, 0(r30)` / `add` /
//    `stfs 0x8/0x0/0x4(r9)`); three pos.x/y/z statements reloaded begin
//    three times.
//  - the RB3-shaped index family `int c0 = 0; c0 += 4; c1..n3 = c0+1..c0+7`
//    with verts[c0..c3] (80.3): the image's vertex byte cursor is
//    0x60*(4i+2) (`li r27, 0xc0` at 0x8282D908, +0x180 at 0x8282DCE0) and
//    its face-index IV starts at exactly 6 (`mr r11, r28` at 0x8282D904,
//    sharing the resize divisor) -- both are MSVC re-biases of a c0 family.
//    `verts[i*4+k]` never produces a biased cursor: MSVC folds k*0x60 into
//    the displacement instead. The old `short s = 6; s += 4` was the
//    re-biased IV spelled as the source variable, which forced extsh.
//  - faces[i*6+k] instead of a separate `fi += 6` (85.3): the image's face
//    cursor r10 = 6*fi is UNbiased with folded displacements 0x0..0x22
//    (0x8282DB1C..0x8282DC54); a separate int fi re-biases to fi+3
//    (`li r6, 0x12`) and derives the other bases.
//
// MEASURED NEGATIVES, each alone from the 85.3 state:
//  - RB3's two-level derivation n1 = n0+1, n2 = n0+2, n3 = n0+3: 77.7.
//  - n0..n3 declared inside the `i != totalSections-1` block: INERT.
//  - `int c0 = i * 4;` as an expression instead of the += 4 IV: INERT (85.3,
//    128 vs 137 diff_arg rows, same structure).
//  - RB3's clamp `numSectionsTop = 4; if (rawTop > 4) numSectionsTop =
//    rawTop;`: 78.9 (gives `ble; mr`, the image is `mr r14, r11; bgt; li
//    r14, 4` at 0x8282D7BC-0x8282D7C8).
//  - `rawTop > 4 ? rawTop : 4` for the top clamp only: matches those four
//    rows but the extra live value cascades through the loop, 80.8.
//  - w7-bo's `int s` + `unsigned short c0..n3` re-measured from the pos.Set
//    state: 71.8, and the listing shows WHY it loses -- MSVC re-biases to c1
//    and applies the 16-bit modular addend to the 32-bit start (`lis r21, 1;
//    ori r21, r21, 1` = 0x10001), which is what proves the image's IV starts
//    at a plain 6 from a base of 0.
//
// RESIDUAL (77 register rows + ~30 structural): the face-index family's
// representative. Ours re-biases to c2 (`li r10, 0x2`; c0/c1 via two
// hoisted 0xfffe/0xffff addends); the image's is n2 = 4i+6 with SIX hoisted
// 16-bit addends r20-r25 (0x8282D90C-0x8282D920). Whatever rule picks the
// representative, it is not use count (c1/c2 tie at 11, n2 has 6), not
// declaration order or scope (INERT above), and not two-level derivation
// (77.7). The image also keeps lVar31 as its own IV (`neg r15, r14` at
// 0x8282D900, `addi r15, r15, 1` at 0x8282DCDC) where ours folds it into
// `add r9, r18, r23`; the two together are what push the image to
// r14-r31 and spill `this` to its home slot (0x8282D710 `stw r3, 0x144(r1)`,
// reloaded at 0x8282DCF0).
//
// w7-bo (2026-09-15, superseded above): 68.77 -> 70.20 canonical. Two levers, each
// measured alone: a plain counted `for` over totalSections (the image guards
// with `cmplwi cr6, r16, 0x0` at 0x8282D898 and then `mtctr r16` at
// 0x8282D8D4 -- our `int count` do/while cost a second IV), and inverting the
// halfWidth step so the BOTTOM arm is the fall-through (0x8282DCC4 is
// `cmplw cr6, r18, r14` / `blt cr6, <top arm>`; that closed the one blt/bge
// diff_op).
//
// MEASURED NEGATIVES, each alone from the 70.20 state:
//  - `unsigned short` for c0..n3 (69.60). This is what the image's TYPE is --
//    every index is materialised with `clrlwi ...,16` (0x8282DAF4) and
//    RndMesh::Face's members are unsigned short -- and it does remove the
//    frame-size delta and move the prologue from r16-r31 to r15-r31. It
//    scores lower because MSVC then rebiases the `s` induction variable from
//    4i+6 to 4i+1, so it hoists ONE modular addend (`addis r9,r21,1` /
//    `subi r9,r9,1` = +0xffff) where the image hoists SIX
//    (`ori r20, r7, 0xfffa` .. `ori r25, r8, 0xfffd`, 0x8282D90C-0x8282D920).
//  - `unsigned short s` as well (69.50). The image's `s` is an untruncated
//    32-bit IV (`addi r11, r11, 0x4` at 0x8282DCE4), so this is wrong anyway.
//  - `int s` + `short c0..c3` (69.80).
//  - spelling the addends `s + (unsigned short)-6` to force the modular
//    constants: MSVC folds it straight back to `s - 6`, byte-identical object.
//  - inverting the even/odd face arms to `if (i & 1)`: INERT (kept, because it
//    is what 0x8282DAEC `clrlwi. r9, r18, 31` / `beq .L_8282DBAC` reads as).
//  - declaring bottomSideBorderVal before verts/faces: INERT.
//
// RESIDUAL is a register-pressure cascade, not a row: the image saves
// r14-r31 + f26-f31 (18 GPR / 6 FPR), we save r16-r31 + f25-f31 (16 / 7). It
// spills `this` to 0x144(r1) (0x8282D710) so r27 can carry a vertex BYTE
// cursor biased +0xc0 (two Verts) and derive three more bases per iteration
// (`subi r8, r27, 0xc0` / `subi r7, r27, 0x60` / `addi r6, r27, 0x60` at
// 0x8282D98C/0x8282D9A4/0x8282D9AC); ours runs one cursor at bias 0 and folds
// k*0x60 into the store displacements instead. It also keeps lVar31 as its own
// IV (`neg r15, r14`, `addi r15, r15, 0x1` at 0x8282DCDC) where MSVC
// eliminates ours into `i - numSectionsTop`.
//
// The MakeString name difference under name_check is the known per-TU ICF
// alias, NOT a wrong string: the target's own relocations at 0x8282D730 and
// 0x8282D734 name "Spotlight.cpp" (_0O@ = 14) and
// "!SpotlightDrawer::DrawNGSpotligh..." (_0CF@ = 37), which is exactly the
// instantiation we emit.
void Spotlight::BuildBeam(BeamDef &def) {
    MILO_ASSERT(!SpotlightDrawer::DrawNGSpotlights(), 0x609);
    def.mIsCone = false;
    def.mBeam = Hmx::Object::New<RndMesh>();
    float bottomBorderLen = def.mBottomBorder * def.mLength;
    float topSideBorderVal = def.mTopSideBorder * def.mTopRadius;
    RndMesh::VertVector &verts = def.mBeam->Verts();
    std::vector<RndMesh::Face> &faces = def.mBeam->Faces();
    float bottomSideBorderVal = def.mBottomSideBorder * def.mBottomRadius;

    int numSectionsTop = (int)((def.mLength - bottomBorderLen) / 15.0f);
    if (numSectionsTop <= 4) numSectionsTop = 4;

    int numSectionsBottom = (int)(bottomBorderLen / 15.0f);
    if (numSectionsBottom <= 1) numSectionsBottom = 1;

    int totalSections = numSectionsBottom + numSectionsTop;

    verts.resize(totalSections * 4);
    faces.resize(totalSections * 6);

    float topLen = def.mLength - bottomBorderLen;
    float topRadius = def.mTopRadius;
    float borderTopRadius = (topLen / def.mLength) * (def.mBottomRadius - topRadius) + topRadius;
    float radiusStepTop = borderTopRadius - topRadius;
    float topSectionLen = 1.0f / (float)numSectionsTop;
    float botSectionLen = 1.0f / (float)numSectionsBottom;
    float radiusStepTopVal = radiusStepTop * topSectionLen;
    float radiusStepBotVal = (def.mBottomRadius - borderTopRadius) * botSectionLen;

    float halfWidth = topRadius;
    int lVar31 = -numSectionsTop;
    int c0 = 0;
    {
        for (unsigned int i = 0; i < (unsigned int)totalSections; i++) {
            int c1 = c0 + 1;
            int c2 = c0 + 2;
            int c3 = c0 + 3;
            int n0 = c0 + 4;
            int n1 = c0 + 5;
            int n2 = c0 + 6;
            int n3 = c0 + 7;
            float y;
            float alpha;
            if (i == (unsigned int)(totalSections - 1)) {
                y = def.mLength;
                alpha = 0.0f;
            } else if (!(i < (unsigned int)numSectionsTop)) {
                y = (botSectionLen * bottomBorderLen) * (float)lVar31 + topLen;
                alpha = 1.0f - (float)lVar31 / (float)numSectionsBottom;
            } else {
                y = (topLen * topSectionLen) * (float)i;
                alpha = 1.0f;
            }

            float yFrac = y / def.mLength;
            float negY = -y;
            float sideBorder = (bottomSideBorderVal - topSideBorderVal) * yFrac + topSideBorderVal;
            float borderRatio = sideBorder / (halfWidth * 2.0f);

            float leftInner = sideBorder - halfWidth;
            float rightInner = halfWidth - sideBorder;

            // Column 0: left edge
            verts[c0].pos.Set(-halfWidth, 0.0f, negY);
            verts[c0].color.Set(0.0f, 0.0f, 0.0f, 0.0f);
            verts[c0].tex.Set(0.0f, yFrac);

            // Column 1: left inner.  Target idx 193/208 is
            // `fneg f28, f27` / `fsel f28, f28, f27, f0` -- keep leftInner
            // while -leftInner >= 0, else 0, i.e. clamp to <= 0.
            leftInner = -leftInner < 0.0f ? 0.0f : leftInner;
            verts[c1].pos.Set(leftInner, 0.0f, negY);
            verts[c1].color.Set(alpha, alpha, alpha, alpha);
            verts[c1].tex.Set(borderRatio, yFrac);

            // Column 2: right inner.  Target idx 210/222 is
            // `fneg f27, f26` / `fsel f27, f27, f0, f26` -- the OTHER way
            // round from column 1: 0 while -rightInner >= 0, else rightInner,
            // i.e. clamp to >= 0.  We had this clamp inverted.
            rightInner = -rightInner < 0.0f ? rightInner : 0.0f;
            verts[c2].pos.Set(rightInner, 0.0f, negY);
            verts[c2].color.Set(alpha, alpha, alpha, alpha);
            verts[c2].tex.Set(1.0f - borderRatio, yFrac);

            // Column 3: right edge
            verts[c3].pos.Set(halfWidth, 0.0f, negY);
            verts[c3].color.Set(0.0f, 0.0f, 0.0f, 0.0f);
            verts[c3].tex.Set(1.0f, yFrac);

            if (i != (unsigned int)(totalSections - 1)) {
                // Target 0x8282DAEC is `clrlwi. r9, r18, 31` / `beq .L_8282DBAC`
                // -- the ODD arm is the fall-through, so the source tests
                // `i & 1` and the even arm is the else.
                if (i & 1) {
                    faces[i * 6].Set(c0, n0, n1);
                    faces[i * 6 + 1].Set(c0, n1, c1);
                    faces[i * 6 + 2].Set(c1, n1, c2);
                    faces[i * 6 + 3].Set(c2, n1, n2);
                    faces[i * 6 + 4].Set(c2, n3, c3);
                    faces[i * 6 + 5].v1 = c2;
                } else {
                    faces[i * 6].Set(c0, n0, c1);
                    faces[i * 6 + 1].Set(c1, n0, n1);
                    faces[i * 6 + 2].Set(c1, n2, c2);
                    faces[i * 6 + 3].Set(c1, n1, n2);
                    faces[i * 6 + 4].Set(c2, n2, c3);
                    faces[i * 6 + 5].v1 = c3;
                }
                faces[i * 6 + 5].v2 = n2;
                faces[i * 6 + 5].v3 = n3;

                if (i == (unsigned int)(totalSections - 2)) {
                    faces[i * 6].Set(c0, n0, c1);
                    faces[i * 6 + 1].Set(c1, n0, n1);
                    faces[i * 6 + 4].Set(c2, n2, n3);
                    faces[i * 6 + 5].Set(c3, c2, n3);
                }
            }

            // Target 0x8282DCC4 is `cmplw cr6, r18, r14` / `blt cr6, <top arm>`
            // -- the BOTTOM arm is the fall-through, so the source tests
            // `i >= numSectionsTop` and the top step is the else.
            if (i >= (unsigned int)numSectionsTop) {
                halfWidth = radiusStepBotVal + halfWidth;
            } else {
                halfWidth = radiusStepTopVal + halfWidth;
            }

            lVar31++;
            c0 += 4;
        }
    }

    def.mBeam->Sync(0x13F);
    def.mBeam->SetMat(def.mMat);
    def.mBeam->SetTransConstraint(kConstraintBillboardZ, nullptr, false);
    RndTransformable *parent;
    parent = this ? static_cast<RndTransformable *>(this) : nullptr;
    def.mBeam->SetTransParent(parent, false);
}

// w7-bs (2026-09-15): 91.76 -> 92.6 canonical (fuzzy 89.66 -> 91.19), 1140 B,
// 292 rows: 81 diff_arg / 3 diff_op / 11 replace / 7 insert / 7 delete. Two
// levers, both behaviourally identical to what was here (same reads, same
// stores, same order):
//  - the two IN-LOOP length uses read `def.mLength` (`borderY / def.mLength`
//    for verts[s-1].tex.y, `def.mLength` as verts[s+15].pos.y) like the tail
//    ones already did. The image reloads `lfs 0x8(r29)` inside the loop and
//    re-divides; with this our callee-saved FPR set is f23-f31 EXACTLY
//    (`__savefpr_23` / `__restfpr_23` both sides, vs _21 before). w7-bo's
//    "61.5" for this lever alone is real but is NOT a regression of the loop
//    body: alone it collapses the three vertex rows onto one cursor at verts[s]
//    with folded displacements (-0xc00/-0x630/-0x60) while the image keeps one
//    cursor at verts[s-1] (`li r28, 0x600` 8282D3A8) and DERIVES the top and
//    bottom rows (`subi r10, r28, 0x600` 8282D3EC / `addi r9, r28, 0x600`
//    8282D40C), so every store row mis-pairs. Measured alone from HEAD: 60.4.
//  - `unsigned int top = s - 17; mid = s - 1; bot = s + 15;` index locals for
//    the vertex rows only (faces keep the inline expressions). This gives MSVC a
//    byte-offset register per row like the image (rows 100-160 now mostly
//    equal: `add r11, r11, r2x` after each `lwz r11, 0(r31)` verts.begin
//    reload), which is what turns the 60.4 into 92.6.
//
// MEASURED NEGATIVES, each from the state above:
//  - `unsigned short s` loop variable (the image's arithmetic is 16-bit modular
//    off r30: s-1 = `add r8, r30, r25` 8282D514 with r25 = 0xffff hoisted at
//    8282D3AC; s-17 = `addis r10, r30, 1; subi r10, r10, 0x11` 8282D50C): 74.6.
//  - one `mid` index local with `verts[mid - 16]` / `verts[mid + 16]` for the
//    other rows, aiming at the image's derived-cursor form: 61.8 (336 rows,
//    51 insert / 53 delete -- MSVC folds it back to the single-cursor shape).
//  - `RndMesh::Vert *mid = &verts[s - 1]; top = mid - 16; bot = mid + 16;`
//    pointer locals: 78.9 (loses the per-Set `lwz r11, 0(r31)` reload the image
//    does before EVERY Set on all three rows).
//  - w7-bo's `unsigned short` face-index locals: 47.3 (their measurement).
//
// RESIDUAL (all 109 non-equal rows accounted for):
//  - ours keeps THREE induction variables for the vertex rows (`addi r27/r26/
//    r25, +0x60` at the loop bottom, rows 200-204) where the image keeps ONE
//    (`addi r28, r28, 0x60` 8282D578) and derives the other two; prologue is
//    therefore r21-r31 (`__savegprlr_21`) vs the image's r23-r31 (8282D284),
//    frame 0x100 vs 0xf0, and the loop-carried GPRs relabel (r24<->r26 etc.,
//    45 rows).
//  - the face-index arithmetic and loop test are rebuilt around an i-biased
//    face base (r30) and a 0x10000 bias register (r23, `subf r4, r23, r10`
//    loop test vs the image's `subi r6, r30, 0x11; cmplwi r6, 0xf` 8282D584):
//    face args become `addi r10, r30, 0x10/0x11/0x1/0x20/0x21` and the sth
//    displacements -0x6/-0x2/-0x4 instead of 0xe/0x10/0xc (rows 161-206).
//  - mid row pos store order: image 0x0, 0x4, 0x8 (f13, f29, f11); ours
//    0x8, 0x0, 0x4 (rows 123-125). Not source-controlled -- explicit z,x,y
//    scalar stores schedule identically to Set() (see Text.cpp SetupCharacter).
//  - tail rows 212-215: verts[15]/verts[31] first-write store order swapped.
// Nothing here reads or writes anything the image does not; do not chase the
// score with a semantic change.
//
// w7-bo (2026-09-15, superseded above): 90.22 -> 91.83 canonical. The two TAIL
// uses of the length read `def.mLength` fresh -- the image reloads it at
// 0x8282D6xx for verts[31].tex (`lfs f0, 0x8(r29)` / `fdivs f0, f29, f0`,
// target idx 257-258) and again for verts[47].pos.y (`lfs f13, 0x8(r29)`, idx
// 263). The duplicated verts[15]/verts[31] writes below are REAL: the image
// writes both twice, the second verts[31] differing only in tex.y (1.0 vs
// borderY/mLength).
void Spotlight::BuildCone(BeamDef &def) {
    MILO_ASSERT(!SpotlightDrawer::DrawNGSpotlights(), 0x5B6);
    def.mIsCone = true;
    def.mBeam = Hmx::Object::New<RndMesh>();
    RndMesh::VertVector &verts = def.mBeam->Verts();
    std::vector<RndMesh::Face> &faces = def.mBeam->Faces();

    verts.resize(0x30);
    faces.resize(60);

    // The image never caches mLength: it reloads `lfs ..., 0x8(r29)` at every
    // use inside the loop and again for verts[31]/[47] in the tail, and
    // recomputes borderY/mLength rather than holding it. `len` is only for the
    // three pre-loop derivations; every later use reads def.mLength (w7-bs).
    float len = def.mLength;
    float bottomBorderLen = def.mBottomBorder * len;
    bottomBorderLen = (float)__fsel(len - bottomBorderLen, bottomBorderLen, len);
    float borderY = len - bottomBorderLen;
    float borderRadius = (borderY / len) * (def.mBottomRadius - def.mTopRadius) + def.mTopRadius;

    float angle = 0.0f;
    float uvStep = 1.0f / 15.0f;
    float angleStep = 2.0f * PI / 15.0f;

    for (unsigned int s = 17; s - 17 != 15; s++) {
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        float uvX = (float)(s - 17) * uvStep;

        unsigned int top = s - 17;
        unsigned int mid = s - 1;
        unsigned int bot = s + 15;

        verts[top].pos.Set(def.mTopRadius * cosA, 0.0f, def.mTopRadius * sinA);
        verts[top].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
        verts[top].tex.Set(uvX, 0.0f);

        verts[mid].pos.Set(borderRadius * cosA, borderY, borderRadius * sinA);
        verts[mid].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
        verts[mid].tex.Set(uvX, borderY / def.mLength);

        verts[bot].pos.Set(def.mBottomRadius * cosA, def.mLength, def.mBottomRadius * sinA);
        verts[bot].color.Set(0.0f, 0.0f, 0.0f, 0.0f);
        verts[bot].tex.Set(uvX, 1.0f);

        int fi = (s - 17) * 4;
        faces[fi].Set(s - 17, s - 1, s);
        faces[fi + 1].Set(s - 17, s, s - 16);
        faces[fi + 2].Set(s - 1, s + 15, s + 16);
        faces[fi + 3].Set(s - 1, s + 16, s);

        angle += angleStep;
    }

    verts[15].pos.Set(def.mTopRadius, 0.0f, 0.0f);
    verts[15].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
    verts[15].tex.Set(1.0f, 0.0f);

    verts[31].pos.Set(borderRadius, borderY, 0.0f);
    verts[31].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
    verts[31].tex.Set(1.0f, 1.0f);

    verts[15].pos.Set(def.mTopRadius, 0.0f, 0.0f);
    verts[15].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
    verts[15].tex.Set(1.0f, 0.0f);

    verts[31].pos.Set(borderRadius, borderY, 0.0f);
    verts[31].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
    verts[31].tex.Set(1.0f, borderY / def.mLength);

    verts[47].pos.Set(def.mBottomRadius, def.mLength, 0.0f);
    verts[47].color.Set(0.0f, 0.0f, 0.0f, 0.0f);
    verts[47].tex.Set(1.0f, 1.0f);

    def.mBeam->Sync(0x13F);
    RndTransformable *parent = this ? static_cast<RndTransformable *>(this) : nullptr;
    def.mBeam->SetTransParent(parent, false);
    def.mBeam->SetMat(def.mMat);
}

// w7-bj (2026-09-14): 70.76 -> 77.78 canonical. Two behavioural fixes against
// the image (csAngle reset per segment at 0x8282C510; cap faces at [4n+seg] /
// [5n+seg], 0x8282C4B8/0x8282C7AC) plus two shape levers (own divisor temp for
// segU, accumulator Multiply in the v==2 arm). Residual is a register/slot
// cascade, not a single row: the image keeps halfAngle (0x50) and flip (0x54)
// memory-resident, hoists only 8 matrix elements (x.x reloaded from 0xb0) and
// gives f23/f24 to uvV/segU, spills numVerts to 0x80 and rematerialises it as
// r29-2, and walks ONE vertex byte cursor (r30) across segments where we get an
// extra outer IV (+0x120). Refuted here, each measured alone: capBase/topBase
// declared after faces.resize (71.99), before the loop (72.52); arm-1 Set as
// three explicit y/x/z stores (74.60, re-indexes verts per store); orientMtx
// declared first (inert, slots do not follow declaration order); halfStep
// declared inside the seg loop (inert); accumulator statement order X,Z,Y
// (inert). Do not respell the face indices as short/unsigned short -- the
// image's cur/nextRow arithmetic is untruncated int (add r6,r7,r11 at
// 0x8282C6D4) and the wave-1 archaeology pass already measured that regression.
void Spotlight::BuildNGCone(BeamDef &def, int numSegments) {
    Hmx::Matrix3 identMtx;
    identMtx.x.Set(1.0f, 0.0f, 0.0f);
    identMtx.y.Set(0.0f, 1.0f, 0.0f);
    identMtx.z.Set(0.0f, 0.0f, 1.0f);

    Hmx::Matrix3 *pMtx;
    Hmx::Matrix3 rotMtx;
    if (def.mIsCone) {
        pMtx = &identMtx;
    } else {
        rotMtx.Set(
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 0.0f, -1.0f),
            Vector3(0.0f, 1.0f, 0.0f)
        );
        pMtx = &rotMtx;
    }
    Hmx::Matrix3 orientMtx;
    memcpy(&orientMtx, pMtx, 0x30);

    def.mBeam = Hmx::Object::New<RndMesh>();
    int numVerts = numSegments * 3;
    int baseVertIdx = numVerts + 1;
    int capBase = numSegments * 4;
    int topBase = capBase + numSegments;
    RndMesh *mesh = def.mBeam;
    RndMesh::VertVector &verts = mesh->Verts();
    std::vector<RndMesh::Face> &faces = mesh->Faces();

    verts.resize(numVerts + 2);
    faces.resize(numSegments * 6);

    float length = def.mLength;
    Vector2 radii = def.NGRadii();
    float halfStep = 0.5f;
    float numSegsF = (float)numSegments;
    float angleStep = 6.2831855f / numSegsF;
    float halfAngle = angleStep * 0.5f;
    float invCosHalf = 1.0f / (float)std::cos((double)halfAngle);
    float topRadius = radii.x * invCosHalf;
    float bottomRadius = radii.y * invCosHalf;

    int flip = 0;
    int iVert = 0;
    float xsAngle = 0.7853982f;
    int baseIdx = 2;
    int iFace = 0;
    for (int seg = 0; seg != numSegments; seg++) {
        float cosH = (float)std::cos((double)halfAngle);
        float sinH = (float)std::sin((double)halfAngle);
        float csAngle = 0.0f;
        // Own divisor temp: naming numSegsF here too lets /fp:fast fold both
        // divisions into one reciprocal (fdivs f31/x + fmuls), the image divides
        // twice (0x8282C448, 0x8282C4F4).
        float segU = (float)seg / (float)numSegments;

        for (unsigned int v = 0; v < 3; v++) {
            float uvV = (float)v * halfStep;
            if (v <= 1) {
                float t = (float)v;
                float radius = (bottomRadius - topRadius) * t + topRadius;
                verts[iVert].pos.Set(radius * cosH, t * length, radius * sinH);
                Multiply(verts[iVert].pos, orientMtx, verts[iVert].pos);
            } else {
                float cosCs = (float)std::cos((double)csAngle);
                float sinCs = (float)std::sin((double)csAngle);
                csAngle = csAngle + xsAngle;
                verts[iVert].pos.Set(
                    cosCs * cosH * bottomRadius,
                    sinCs * bottomRadius + length,
                    cosCs * sinH * bottomRadius
                );
                {
                    Vector3 &p = verts[iVert].pos;
                    float px = p.x, py = p.y, pz = p.z;
                    float rx = orientMtx.y.x * py;
                    rx += orientMtx.z.x * pz;
                    rx += orientMtx.x.x * px;
                    float rz = orientMtx.x.z * px;
                    rz += orientMtx.y.z * py;
                    rz += orientMtx.z.z * pz;
                    float ry = orientMtx.x.y * px;
                    ry += orientMtx.y.y * py;
                    ry += orientMtx.z.y * pz;
                    p.Set(rx, ry, rz);
                }
            }
            verts[iVert].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
            verts[iVert].tex.Set(segU, uvV);
            iVert++;
        }

        int sideWidth;
        if (seg < numSegments - 1) {
            sideWidth = 3;
        } else {
            sideWidth = 3 - numVerts;
        }

        int cur = baseIdx - 1;
        int curFlip = flip;
        int fCount = 2;
        do {
            flip = curFlip + 1;
            int nextRow = cur - 1 + sideWidth;
            // Bitwise, not logical: the target emits `clrlwi. rN, rM, 31`
            // (an explicit AND with 1), so the winding alternates every
            // iteration. `curFlip && 1` compiles to `cmpwi rM, 0` instead and
            // is true for every iteration after the first.
            if (curFlip & 1) {
                faces[iFace].Set(nextRow, cur - 1, nextRow + 1);
                faces[iFace + 1].Set(nextRow + 1, cur - 1, cur);
            } else {
                faces[iFace].Set(cur - 1, cur, nextRow);
                faces[iFace + 1].Set(nextRow, cur, nextRow + 1);
            }
            cur = cur + 1;
            iFace += 2;
            curFlip = flip;
            fCount--;
        } while (fCount != 0);

        halfAngle = halfAngle + angleStep;
        // The two cap faces of each segment live after ALL the side faces:
        // bottom caps at [4n, 5n), top caps at [5n, 6n). The image walks
        // two extra face cursors seeded at 4n*6 and 5n*6 (0x8282C4B8-C4C4)
        // and the side-face cursor advances only 4 faces per segment
        // (0x8282C7AC); interleaving them 4+2 per segment was wrong.
        faces[capBase + seg].Set(baseIdx - 2, baseIdx + sideWidth - 2, numVerts);
        faces[topBase + seg].Set(baseIdx + sideWidth, baseIdx, numVerts + 1);
        baseIdx = baseIdx + 3;
    }

    verts[numVerts].pos.Set(0.0f, 0.0f, 0.0f);
    verts[numVerts].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
    verts[numVerts].tex.Set(0.0f, 0.0f);

    verts[baseVertIdx].pos.Set(0.0f, length, 0.0f);
    Multiply(verts[baseVertIdx].pos, orientMtx, verts[baseVertIdx].pos);
    verts[baseVertIdx].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
    verts[baseVertIdx].tex.Set(0.0f, 1.0f);

    def.mBeam->Sync(0x13F);
    def.mBeam->SetMat(def.mMat);
    RndTransformable *parent = this ? static_cast<RndTransformable *>(this) : nullptr;
    def.mBeam->SetTransParent(parent, false);
}
void Spotlight::BuildNGSheet(BeamDef &def) {
    Hmx::Matrix3 identMtx;
    identMtx.x.Set(1.0f, 0.0f, 0.0f);
    identMtx.y.Set(0.0f, 1.0f, 0.0f);
    identMtx.z.Set(0.0f, 0.0f, 1.0f);

    Hmx::Matrix3 rotMtx;
    Hmx::Matrix3 *pMtx;
    if (def.mIsCone) {
        pMtx = &identMtx;
    } else {
        rotMtx.Set(
            Vector3(1.0f, 0.0f, 0.0f),
            Vector3(0.0f, 0.0f, -1.0f),
            Vector3(0.0f, 1.0f, 0.0f)
        );
        pMtx = &rotMtx;
    }
    Hmx::Matrix3 orientMtx;
    memcpy(&orientMtx, pMtx, 0x30);

    def.mBeam = Hmx::Object::New<RndMesh>();
    int defSections = def.mNumSections;
    RndMesh::VertVector &verts = def.mBeam->Verts();

    std::vector<RndMesh::Face> &faces = def.mBeam->Faces();
    int numSections = defSections > 1 ? defSections : 5;
    int numSegments = def.mNumSegments > 2 ? def.mNumSegments : 10;

    // NEGATIVE RESULT (w7-am, 2026-09-14): swapping these two declarations to
    // try to flip the r23<->r24 / r26<->r27 cascade keeps the score at 96.3
    // (95 rows either way) and is very slightly worse on the raw ruler
    // (94.2 vs 94.3), so the residual is not a declaration-order effect.
    int numRows = numSections + 1;
    int numCols = numSegments + 1;
    int kNumVerts = numRows * numCols;
    int kNumFaces = (numSegments * (numSections * 2));

    verts.resize(kNumVerts);
    faces.resize(kNumFaces);

    Vector2 radii = def.NGRadii();
    float topRadius = radii.x;
    float bottomRadius = radii.y;

    static float kSheetFade = 1.0f; // lbl_82F1987C

    int iVert = 0;
    for (int row = 0; row < numRows; row++) {
        float t = (float)row / (float)numSections;
        for (int col = 0; col < numCols; col++) {
            // Stays INSIDE the col loop.  RB3's Spotlight.cpp has it in the row
            // loop, but hoisting it here measures 93.7 against 96.3 and permutes
            // a stack slot; DC3's codegen wants it recomputed per column.
            float oneMinusT = 1.0f - t;
            float segFrac = (float)col / (float)numSegments * 2.0f - 1.0f;
            float xTop = segFrac * topRadius;
            float xBot = segFrac * bottomRadius;
            float absSegFrac = std::fabs(segFrac);

            verts[iVert].pos.Set(
                (xBot - xTop) * t + xTop,
                def.mLength * t,
                (1.0f - absSegFrac) * kSheetFade
            );

            Vector3 &p = verts[iVert].pos;
            float px = p.x, pz = p.z, py = p.y;
            p.z = pz * orientMtx.z.z + px * orientMtx.x.z + py * orientMtx.y.z;
            p.y = pz * orientMtx.z.y + px * orientMtx.x.y + py * orientMtx.y.y;
            p.x = pz * orientMtx.z.x + px * orientMtx.x.x + py * orientMtx.y.x;

            verts[iVert].norm.Set(0.0f, 0.0f, 1.0f);

            Vector3 &n = verts[iVert].norm;
            Multiply(n, orientMtx, n);

            verts[iVert].color.Set(oneMinusT, oneMinusT, oneMinusT, oneMinusT);
            verts[iVert].tex.Set(std::fabs(segFrac), t);
            iVert++;
        }
    }
    MILO_ASSERT(iVert == kNumVerts, 0x526);

    int iFace = 0;
    int rowStart = 0;
    for (int row = 0; row < numSections; row++) {
        for (int col = 0; col < numSegments; col++) {
            // `base` really is a u16 here: measured, spelling all four indices as
            // plain ints -- which is what 8282CD40's untruncated `add r9, r4, r3`
            // looks like in isolation -- drops the function from 96.3 to 94.4 and
            // adds 35 rows of GPR renumbering across the whole body.
            unsigned short base = (unsigned short)(rowStart + col);
            int next = base + 1;
            int baseNext = base + numCols;
            int nextNext = baseNext + 1;
            if (iFace & 2) {
                faces[iFace].Set(
                    (unsigned short)baseNext, (unsigned short)base, (unsigned short)nextNext
                );
                iFace++;
                faces[iFace].Set(
                    (unsigned short)nextNext, (unsigned short)base, (unsigned short)next
                );
            } else {
                faces[iFace].Set(
                    (unsigned short)base, (unsigned short)next, (unsigned short)baseNext
                );
                iFace++;
                faces[iFace].Set(
                    (unsigned short)baseNext, (unsigned short)next, (unsigned short)nextNext
                );
            }
            iFace++;
        }
        rowStart += numCols;
    }
    MILO_ASSERT(iFace == kNumFaces, 0x53F);

    def.mBeam->Sync(0x13F);
    def.mBeam->SetMat(def.mMat);
    RndTransformable *parent = this ? static_cast<RndTransformable *>(this) : nullptr;
    def.mBeam->SetTransParent(parent, false);
}


void Spotlight::BuildNGQuad(BeamDef &def, RndTransformable::Constraint constraint) {
    auto mesh = Hmx::Object::New<RndMesh>();
    def.mBeam = mesh;
    std::vector<RndMesh::Face> &faces = def.mBeam->Faces();
    int gridSize = def.mNumSegments;
    RndMesh::VertVector &verts = def.mBeam->Verts();
    if (def.mNumSections >= gridSize) {
        gridSize = def.mNumSections;
    }
    static int sGridSize = (gridSize > 0) ? gridSize + 1 : 2;

    int nMinus1 = sGridSize - 1;
    int totalVerts = sGridSize * sGridSize;
    int totalFaces = (nMinus1 * (nMinus1 * 2));

    verts.resize(totalVerts);
    faces.resize(totalFaces);

    int n = sGridSize;
    float topRadius = def.mLength;
    float bottomRadius = def.mBottomRadius;

    // SURVEY 2026-09-14 (w7-ae), 88.1% canonical, 145 mismatch rows, no edit.
    // The pos matrix-multiply block (diff rows 113-127) is structurally IDENTICAL
    // to the image, term for term and store for store: both sides load y,z,x,
    // both multiply by the ZERO elements rather than folding them away, both
    // associate the three-term dot product left to right, and both store z,y,x in
    // that order.  Exactly ONE row differs, and it is instruction selection for
    // the -1 element:
    //     target  .L_82684f80  fmadds f4, f4, f9, f1     (f9 = -1.0, hoisted from
    //                                                     __real@bf800000 at
    //                                                     .L_82684edc/.L_82684ee4,
    //                                                     BEFORE the loop)
    //     ours    0x10fc8      fsubs  f5, f2, f5
    // i.e. the image carries the nine matrix elements in REGISTERS across the
    // whole loop (f0 = 0.0, f13 = 1.0, f9 = -1.0) and therefore multiplies by a
    // register, while our build still knows the multiplier is literally -1.0 at
    // the multiply site and strength-reduces `a + b * -1.0f` to `a - b`.  That
    // one choice is what forces the image to hold TWO callee-saved FPRs where we
    // hold one (`stfd f30`/`stfd f31` vs `stfd f31`) and one extra GPR
    // (`bl __savegprlr_22` vs `__savegprlr_23`), which is the whole reported
    // frame delta of -0x10 and nearly all 21 register-swap pairs -- so the single
    // fsubs row is worth ~12pp of renaming behind it.
    // NOT a spelling of Multiply(): the association and store order it produces
    // already match.  What would have to change is whether MSVC can see the
    // literal at the multiply, and no value-preserving source form of a
    // Matrix3 built from literals was found that hides it.  Recorded, not fixed.
    Hmx::Matrix3 rot;
    rot.Set(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f);

    int idx = 0;
    float rowFrac;
    for (int row = 0; row < n; row++) {
                rowFrac = (float)row / (float)(n - 1);
        float colFrac;
        for (int col = 0; col < n; col++) {
                        colFrac = (float)col / (float)(n - 1);

            verts[idx].pos.Set(
                (colFrac * 2.0f - 1.0f) * bottomRadius,
                (rowFrac * 2.0f - 1.0f) * topRadius,
                0.0f
            );
            Multiply(verts[idx].pos, rot, verts[idx].pos);

            verts[idx].norm.Set(0.0f, 0.0f, 1.0f);
            Multiply(verts[idx].norm, rot, verts[idx].norm);

            verts[idx].color.Set(1.0f, 1.0f, 1.0f, 1.0f);
            verts[idx].tex.Set(colFrac, rowFrac);
            idx++;
        }
    }

    int iFace = 0;
    for (int row = 0; row < nMinus1; row++) {
        for (int col = 0; col < nMinus1; col++) {
            int base = row + 1 + col * n;
            int uBaseN = base + n - 1;
            unsigned short uPrev = base - 1;
            if (iFace & 2) {
                faces[iFace++].Set(uBaseN, uPrev, base + n);
                faces[iFace++].Set(base + n, uPrev, base);
            } else {
                faces[iFace++].Set(uPrev, base, uBaseN);
                faces[iFace++].Set(uBaseN, base, base + n);
            }
        }
    }

    def.mBeam->Sync(0x13F);
    def.mBeam->SetMat(def.mMat);
    def.mBeam->SetTransConstraint(constraint, nullptr, false);
    RndTransformable *parent = this ? static_cast<RndTransformable *>(this) : nullptr;
    def.mBeam->SetTransParent(parent, false);
}
