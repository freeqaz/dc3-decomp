#include "rndobj/TexBlendController.h"
#include "math\Mtx.h"
#include "math\Utl.h"
#include "obj/Object.h"

RndTexBlendController::RndTexBlendController()
    : mMesh(this), mObject1(this), mObject2(this), mReferenceDistance(0), mMinDistance(0),
      mMaxDistance(0), mTex(this) {}

BEGIN_HANDLERS(RndTexBlendController)
    HANDLE_ACTION(set_min_distance, UpdateMinDistance())
    HANDLE_ACTION(set_max_distance, UpdateMaxDistance())
    HANDLE_ACTION(set_base_distance, UpdateReferenceDistance())
    HANDLE_ACTION(set_all_distances, UpdateAllDistances())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndTexBlendController)
    SYNC_PROP_MODIFY(reference_object_1, mObject1, UpdateAllDistances())
    SYNC_PROP_MODIFY(reference_object_2, mObject2, UpdateAllDistances())
    SYNC_PROP(mesh, mMesh)
    SYNC_PROP(base_distance, mReferenceDistance)
    SYNC_PROP(min_distance, mMinDistance)
    SYNC_PROP(max_distance, mMaxDistance)
    SYNC_PROP(override_map, mTex)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndTexBlendController)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mMesh;
    bs << mObject1;
    bs << mObject2;
    bs << mReferenceDistance;
    bs << mMinDistance;
    bs << mMaxDistance;
    bs << mTex;
END_SAVES

BEGIN_COPYS(RndTexBlendController)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(RndTexBlendController)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mMesh)
        COPY_MEMBER(mObject1)
        COPY_MEMBER(mObject2)
        COPY_MEMBER(mReferenceDistance)
        COPY_MEMBER(mMinDistance)
        COPY_MEMBER(mMaxDistance)
        COPY_MEMBER(mTex)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(2, 0)

BEGIN_LOADS(RndTexBlendController)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mMesh;
    d >> mObject1;
    d >> mObject2;
    d >> mReferenceDistance;
    d >> mMinDistance;
    d >> mMaxDistance;
    if (d.rev > 1) {
        d >> mTex;
    }
END_LOADS

bool RndTexBlendController::IsValid() const {
    if (!mMesh) {
        return false;
    }
    if (!mTex) {
        float refDist = mReferenceDistance;
        bool distValid;
        if (!(mMinDistance > refDist) || !(mMaxDistance < refDist)) {
            distValid = true;
        } else {
            distValid = false;
        }
        return mObject1 && mObject2 && refDist > 0 && distValid;
    }
    return true;
}

bool RndTexBlendController::GetCurrentDistance(float &dist) const {
    if (mObject1 && mObject2) {
        const Transform &t = mObject1->WorldXfm();
        dist = Distance(t.v, mObject2->WorldXfm().v);
        return true;
    } else {
        dist = 0;
        return false;
    }
}

RndTexBlendController::BlendState
RndTexBlendController::GetBlendState(float &blend, float influence) const {
    BlendState state = kBlendNone;
    blend = 0.0f;

    if (IsValid()) {
        if (mTex) {
            blend = 1.0f;
            state = kBlendCustom;
        } else {
            float dist;
            if (GetCurrentDistance(dist) && (bool)(mReferenceDistance > 0.0f)) {
                if (dist < mReferenceDistance) {
                    float denom = mReferenceDistance - mMinDistance;
                    if (denom > 0.0f) {
                        state = kBlendNear;
                        blend = (mReferenceDistance - Max(dist, mMinDistance)) / denom;
                    }
                } else if (dist > mReferenceDistance) {
                    float denom = mMaxDistance - mReferenceDistance;
                    if (denom > 0.0f) {
                        state = kBlendFar;
                        blend = (Min(dist, mMaxDistance) - mReferenceDistance) / denom;
                    }
                }
            }
            float t2 = blend * blend;
            float t3 = blend * t2;
            // Smoothstep. The term order is load-bearing: MSVC forms the FMA
            // from the SECOND multiply and computes the first into the addend,
            // so `t3*-2 + t2*3` becomes `fmuls 2*t3` + `fmsubs t2*3 - that`,
            // while this order keeps -2.0f as a literal and emits `fmadds`.
            blend = t2 * 3.0f + t3 * (-2.0f);
        }
    }

    blend *= influence;
    blend = Clamp(0.0f, 1.0f, blend);
    // Quantise to 8 bits. The image narrows with a byte load out of the fctidz
    // spill slot (`lbz r11, 0x57(r1)`), which is an `unsigned char` conversion;
    // a `& 0xFF` on the 64-bit value spells `ld` + `rldicl` instead.
    blend = (float)(unsigned char)(blend * 255.0f) * (1.0f / 255.0f);
    if (blend < 1.0f / 255.0f) {
        state = kBlendNone;
    }

    return state;
}

void RndTexBlendController::UpdateReferenceDistance() {
    GetCurrentDistance(mReferenceDistance);
    mMinDistance = Min(mMinDistance, mReferenceDistance);
    mMaxDistance = Max(mMaxDistance, mReferenceDistance);
}

void RndTexBlendController::UpdateMinDistance() {
    GetCurrentDistance(mMinDistance);
    mMinDistance = Min(mMinDistance, mReferenceDistance);
}

void RndTexBlendController::UpdateMaxDistance() {
    GetCurrentDistance(mMaxDistance);
    mMaxDistance = Max(mMaxDistance, mReferenceDistance);
}

void RndTexBlendController::UpdateAllDistances() {
    UpdateReferenceDistance();
    mMinDistance = mReferenceDistance * 0.5f;
    mMaxDistance = mReferenceDistance * 1.5f;
}
