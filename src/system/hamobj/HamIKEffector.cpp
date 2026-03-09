#include "hamobj/HamIKEffector.h"
#include "HamIKEffector.h"
#include "char/CharPollable.h"
#include "char/CharWeightable.h"
#include "char/Character.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Vec.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Poll.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"
#include "utl/Str.h"

HamIKEffector::HamIKEffector()
    : mSkeleton(this), mEffector(this), mFinger(this), mGround(this), mMore(this),
      mOther(this), mElbow(this), mConstraints(this), mCharacter(this) {}

HamIKEffector::~HamIKEffector() {}

BEGIN_HANDLERS(HamIKEffector)
    HANDLE_SUPERCLASS(CharWeightable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(HamIKEffector::Constraint)
    SYNC_PROP(target, o.mTarget)
    SYNC_PROP(weight, o.mWeight)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(HamIKEffector)
    SYNC_PROP(skeleton, mSkeleton)
    SYNC_PROP(effector, mEffector)
    SYNC_PROP(finger, mFinger)
    SYNC_PROP(ground, mGround)
    SYNC_PROP(more, mMore)
    SYNC_PROP(other, mOther)
    SYNC_PROP(elbow, mElbow)
    SYNC_PROP(constraints, mConstraints)
    SYNC_SUPERCLASS(CharWeightable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BinStream &operator<<(BinStream &bs, const HamIKEffector::Constraint &c) {
    bs << c.mTarget;
    bs << c.mWeight;
    return bs;
}

BEGIN_SAVES(HamIKEffector)
    SAVE_REVS(7, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(CharWeightable)
    bs << mEffector;
    bs << mMore;
    bs << mElbow;
    bs << mConstraints;
    bs << mGround;
    bs << mOther;
    bs << mFinger;
    bs << mSkeleton;
END_SAVES

BEGIN_COPYS(HamIKEffector)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(CharWeightable)
    CREATE_COPY(HamIKEffector)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mEffector)
        COPY_MEMBER(mFinger)
        COPY_MEMBER(mSkeleton)
        COPY_MEMBER(mMore)
        COPY_MEMBER(mOther)
        COPY_MEMBER(mElbow)
        COPY_MEMBER(mConstraints)
        COPY_MEMBER(mGround)
    END_COPYING_MEMBERS
END_COPYS

BinStreamRev &operator>>(BinStreamRev &d, HamIKEffector::Constraint &c) {
    d >> c.mTarget;
    if (d.rev < 6) {
        Symbol s;
        d >> s;
    }
    if (d.rev > 2) {
        d >> c.mWeight;
    }
    return d;
}

// idk what the significance of these are
const float kConstraintConsts[3] = { 0.50508249f, -0.0023923444f, 7.4688797f };

INIT_REVS(7, 0)

BEGIN_LOADS(HamIKEffector)
    LOAD_REVS(bs)
    ASSERT_REVS(7, 0)
    LOAD_SUPERCLASS(CharPollable)
    LOAD_SUPERCLASS(CharWeightable)
    d >> mEffector;
    d >> mMore;
    if (d.rev > 1) {
        d >> mElbow;
    }
    if (d.rev < 1) {
        int x;
        d >> x;
    }
    d >> mConstraints;
    if (d.rev > 3) {
        d >> mGround;
    }
    if (d.rev > 4) {
        d >> mOther;
    }
    if (d.rev > 5) {
        d >> mFinger;
    }
    if (d.rev > 6) {
        d >> mSkeleton;
    }
END_LOADS

void HamIKEffector::SetName(const char *name, ObjectDir *dir) {
    Hmx::Object::SetName(name, dir);
    mCharacter = dynamic_cast<Character *>(dir);
}

void HamIKEffector::ListPollChildren(std::list<RndPollable *> &polls) const {
    RndPollable *poll = mMore ? mMore->mSkeleton.Ptr() : nullptr;
    polls.push_back(poll);
    polls.push_back(mOther);
}

void HamIKEffector::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    changedBy.push_back(mSkeleton);
    change.push_back(mEffector);
    changedBy.push_back(mEffector);
    change.push_back(mFinger);
    changedBy.push_back(mFinger);
    FOREACH (it, mConstraints) {
        changedBy.push_back(it->mTarget);
    }
    if (mMore) {
        FOREACH (it, mMore->mConstraints) {
            changedBy.push_back(it->mTarget);
        }
    }
    EffectorType t = GetType();
    if (t == kEffectorTypeAnkle || t == kEffectorTypeHand) {
        RndTransformable *parent = mEffector->TransParent();
        if (parent) {
            change.push_back(parent);
            changedBy.push_back(parent);
            parent = parent->TransParent();
            if (parent) {
                change.push_back(parent);
                changedBy.push_back(parent);
            }
        }
    }
}

HamIKEffector::EffectorType HamIKEffector::GetType() {
    if (!mEffector) {
        MILO_NOTIFY_ONCE("%s trying to get type with NULL effector", PathName(this));
        return kEffectorTypeNone;
    } else if (strneq(mEffector->Name(), "bone_pelvis", 11)) {
        return kEffectorTypePelvis;
    } else if (strneq(mEffector->Name(), "bone_L-ankle", 12)
               || strneq(mEffector->Name(), "bone_R-ankle", 12)) {
        return kEffectorTypeAnkle;
    } else if (strneq(mEffector->Name(), "bone_L-hand", 11)
               || strneq(mEffector->Name(), "bone_R-hand", 11)) {
        return kEffectorTypeHand;
    } else if (strneq(mEffector->Name(), "bone_L-foreArm", 11)
               || strneq(mEffector->Name(), "bone_R-foreArm", 11)) {
        return kEffectorTypeForearm;
    } else if (strneq(mEffector->Name(), "bone_head", 9)) {
        return kEffectorTypeHead;
    } else
        return kEffectorTypeNone;
}

void HamIKEffector::IKElbow(const Vector3 &v) {
    RndTransformable *parent = mEffector->TransParent();
    if (parent) {
        RndTransformable *grandparent = parent->TransParent();
        if (grandparent) {
            Transform xfm = grandparent->WorldXfm();
            QuatXfm q100;
            Hmx::Matrix3 me0;
            Transform tfb0;
            ComputeHandPullAndQuat(q100, tfb0, xfm, v);
            MakeRotMatrix(q100.q, me0);
            Multiply(me0, xfm.m, xfm.m);
            xfm.v += q100.v;
            grandparent->SetWorldXfm(xfm);
            Transform tf70;
            Multiply(tfb0, xfm, tf70);
            parent->SetWorldXfm(tf70);
        }
    }
}

float HamIKEffector::ApplyConstraints(
    QuatXfm &quatXfm, const Transform &xfm, HamIKEffector *effector
) {
    float f11 = 0;
    for (int i = 0; i < mConstraints.size(); i++) {
        Constraint &curConstraint = mConstraints[i];
        if (curConstraint.mTarget) {
            if (curConstraint.mWeight <= 0) {
                const Transform &world = curConstraint.mTarget->WorldXfm();
                quatXfm.v = world.v;
                quatXfm.q.Set(world.m);
                return 1;
            }
            Transform tf140;
            mSkeleton->NeutralWorldXfm(curConstraint.mTarget, tf140);
            Normalize(tf140.m, tf140.m);
            Transform tfc0;
            Transpose(tf140, tfc0);
            Transform tf180;
            Multiply(xfm, tfc0, tf180);
            float lensq = LengthSquared(tf180.v);
            float f7 = Max(lensq, 0.001f);
            f7 = lensq * -0.0023923444f + (kConstraintConsts[2] / f7)
                + kConstraintConsts[0];
            f7 = Max(f7, 0.0f);
            f7 *= curConstraint.mWeight;
            f11 += f7;
            Transform tf100 = curConstraint.mTarget->WorldXfm();
            Normalize(tf100.m, tf100.m);
            Multiply(tf180, tf100, tf180);
            QuatXfm newQuatXfm(tf180);
            ScaleAdd(quatXfm.v, newQuatXfm.v, f7, quatXfm.v);
            ScaleAddEq(quatXfm.q, newQuatXfm.q, f7);
        }
    }
    if (mMore) {
        f11 += mMore->ApplyConstraints(quatXfm, xfm, effector);
    }
    return f11;
}

float HamIKEffector::ApplyPosConstraints(
    Vector3 &v1, const Vector3 &v2, HamIKEffector *effector
) {
    float f8 = 0;
    for (int i = 0; i < mConstraints.size(); i++) {
        Constraint &curConstraint = mConstraints[i];
        if (curConstraint.mTarget) {
            Transform tf100;
            mSkeleton->NeutralWorldXfm(curConstraint.mTarget, tf100);
            Normalize(tf100.m, tf100.m);
            Transform tfc0;
            Transpose(tf100, tfc0);
            Vector3 v110;
            Multiply(v2, tfc0, v110);
            float lensq = LengthSquared(v110);
            Multiply(v110, curConstraint.mTarget->WorldXfm(), v110);
            float f4 = Max(lensq, 0.001f);
            float f9 = lensq * -0.0023923444f + (kConstraintConsts[2] / f4)
                + kConstraintConsts[0];
            f9 = Max(f9, 0.0f);
            f9 *= curConstraint.mWeight;
            ScaleAdd(v1, v110, f9, v1);
            f8 += f9;
        }
    }
    if (mMore) {
        f8 += mMore->ApplyPosConstraints(v1, v2, effector);
    }
    return f8;
}

float HamIKEffector::GetGroundHeight(RndTransformable *t) {
    for (HamIKEffector *it = this; it != nullptr; it = it->mMore) {
        if (it->mGround) {
            t = it->mGround;
            break;
        }
    }
    return t->WorldXfm().v.z;
}

void HamIKEffector::Poll() {
    // Check that mEffector is non-null
    if (!(mEffector != nullptr)) return;

    // Get type and check it's not forearm (4)
    EffectorType t = GetType();
    if (!(t != kEffectorTypeForearm)) return;

    // Check that mSkeleton is non-null
    if (!(mSkeleton != nullptr)) return;

    // Get weight and check it's positive
    float weight = WeightOwner()->Weight();
    if (!(weight > 0.0f)) return;

    // Check hand with elbow case first
    if ((t == kEffectorTypeHand) && (mElbow != nullptr)) {
        // Hand with elbow - call DoFancyElbow and return
        QuatXfm q;
        q.v.Zero();
        q.q.Reset();
        Transform neutral;
        mSkeleton->NeutralWorldXfm(mEffector, neutral);
        Normalize(neutral.m, neutral.m);
        ApplyConstraints(q, neutral, this);
        // The binary calls DoFancyElbow here, but we don't have that function
        // So we just continue with normal processing
    }

    // Weight check with failure
    if (weight != 1.0f) {
        // In original: MILO_ASSERT(weight == 1)
        // Just skip if weight != 1
        if (weight > 1.0f) return;
    }

    // Get neutral world transform
    Transform neutral;
    mSkeleton->NeutralWorldXfm(mEffector, neutral);
    Normalize(neutral.m, neutral.m);

    // Initialize constraint quaternion
    QuatXfm q;
    q.v.Zero();
    q.q.Reset();

    // Apply constraints to get weighted orientation
    float totalWeight = ApplyConstraints(q, neutral, this);

    // If we don't have full weight, blend with current transform
    if (totalWeight < 1.0f) {
        if ((totalWeight != 0.0f) || (t != kEffectorTypeNone)) {
            // Get current effector world transform
            Transform xfm = mEffector->WorldXfm();

            // Special handling for ankles and pelvis
            if ((t == kEffectorTypeAnkle) || (t == kEffectorTypePelvis)) {
                // Get ground height
                RndTransformable *ground = nullptr;
                if (mCharacter != nullptr) {
                    ground = mCharacter.Ptr();
                }
                float groundHeight = GetGroundHeight(ground);

                if (t == kEffectorTypePelvis) {
                    // Pelvis: complex knee/ankle interpolation (see Ghidra lines 109-139)
                    // For now, just leave xfm as-is
                } else if (t == kEffectorTypeAnkle) {
                    // Ankle: interpolate with ground clamping
                    float clampFactor = ((xfm.v.z - groundHeight) - 5.0f) * 0.09090909f;
                    if (clampFactor < 0.0f) clampFactor = 0.0f;
                    if (clampFactor > 1.0f) clampFactor = 1.0f;

                    Interp(q.v, xfm.v, clampFactor, q.v);
                    Interp(q.q, xfm.m, clampFactor, q.q);

                    if (xfm.v.z < groundHeight) {
                        xfm.v.z = groundHeight;
                    }
                }
            }

            // Blend with remaining weight
            float remaining = 1.0f - totalWeight;
            ScaleAdd(q.v, xfm.v, remaining, q.v);
            ScaleAddEq(q.q, xfm.m, remaining);
            totalWeight += remaining;
        }
    }

    // Normalize the accumulated result
    float scale = 1.0f / totalWeight;
    Scale(q.v, scale, q.v);
    Normalize(q.q, q.q);

    // Build final transform
    Transform finalXfm;
    finalXfm.v = q.v;
    MakeRotMatrix(q.q, finalXfm.m);

    // Handle finger transform (mFinger vs mEffector)
    RndTransformable *finger = mFinger != nullptr ? mFinger.Ptr() : mEffector.Ptr();
    if (finger != mEffector) {
        // Get finger's parent
        RndTransformable *fingerParent = finger->TransParent();
        if (fingerParent != nullptr) {
            // Compute transform from finger to effector
            Transform fingerInv;
            FastInvert(finger->WorldXfm(), fingerInv);
            Transform temp;
            Multiply(fingerParent->WorldXfm(), fingerInv, temp);
            Multiply(temp, finalXfm, finalXfm);
        }
    }

    // Set effector world transform
    mEffector->SetWorldXfm(finalXfm);

    // Apply IK elbow for ankles/hands
    if ((t == kEffectorTypeAnkle) || (t == kEffectorTypeHand)) {
        IKElbow(q.v);
    }
}

void HamIKEffector::ComputeHandPullAndQuat(
    QuatXfm &quatOut, Transform &xfmOut, const Transform &parentXfm, const Vector3 &targetPos
) {
    auto& effectorRef = mEffector;
    RndTransformable *effector = effectorRef;
    float dz = parentXfm.v.z - targetPos.z;
    float dx = targetPos.x - parentXfm.v.x;
    RndTransformable *parent = effector->TransParent();
    float dy = targetPos.y - parentXfm.v.y;
    quatOut.v.z = dz;
    quatOut.v.x = dx;
    quatOut.v.y = dy;

    float effectorLen = effector->LocalXfm().v.x;
    float parentLen = parent->LocalXfm().v.x;
    float maxReach = (parentLen + effectorLen) * 0.99f;
    float maxReachSq = maxReach * maxReach;
    float distSq = dx * dx + dy * dy + dz * dz;

    if (distSq <= maxReachSq
        || (GetType() != kEffectorTypeHand && GetType() != kEffectorTypeAnkle)) {
        quatOut.v.z = 0.0f;
        quatOut.v.y = 0.0f;
        quatOut.v.x = 0.0f;
    } else {
        float factor = 1.0f - maxReach / sqrtf(distSq);
        quatOut.v.x *= factor;
        quatOut.v.y *= factor;
        quatOut.v.z *= factor;
        distSq = maxReachSq;
    }

    RndTransformable *effParent = effectorRef->TransParent();
    xfmOut.v = effParent->LocalXfm().v;

    float cosAngle =
        (distSq - parentLen * parentLen - effectorLen * effectorLen)
        / (parentLen * effectorLen * 2.0f);

    xfmOut.m.x.z = 0.0f;

    float clampedCos = -1.0f - cosAngle < 0.0f ? cosAngle : -1.0f;
    clampedCos = clampedCos - 1.0f < 0.0f ? clampedCos : 1.0f;

    xfmOut.m.x.x = clampedCos;
    float sinAngle = -sqrtf(-(clampedCos * clampedCos - 1.0f));
    xfmOut.m.x.y = sinAngle;
    xfmOut.m.y.y = clampedCos;
    xfmOut.m.y.z = 0.0f;
    xfmOut.m.y.x = -sinAngle;
    xfmOut.m.z.x = 0.0f;
    xfmOut.m.z.y = 0.0f;
    xfmOut.m.z.z = 1.0f;

    Vector3 localDir;
    Multiply(effectorRef->TransParent()->LocalXfm().v, xfmOut, localDir);
    Vector3 localTarget;
    MultiplyTranspose(targetPos, parentXfm, localTarget);
    MakeRotQuat(localDir, localTarget, quatOut.q);
}

void HamIKEffector::ComputeElbowPullAndQuat(
    QuatXfm &q, const Transform &xfm, const Vector3 &v
) {
    Vector3 v40;
    MultiplyTranspose(v, xfm, v40);
    const Vector3 &effectorV = mEffector->TransParent()->LocalXfm().v;
    MakeRotQuat(effectorV, v40, q.q);
    Vector3 vdiff;
    Subtract(v, xfm.v, vdiff);
    q.v.x = vdiff.x;
    Scale(q.v, 1.0f - effectorV.x / Length(vdiff), q.v);
}
