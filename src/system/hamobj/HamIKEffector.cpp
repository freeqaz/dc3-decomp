#include "hamobj/HamIKEffector.h"
#include "HamIKEffector.h"
#ifdef HX_NATIVE
#include <cmath>
#endif
#include "char/CharPollable.h"
#include "char/CharUtl.h"
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


#ifdef HX_NATIVE
// Saved IK-corrected world transforms. Each entry is a (bone, transform) pair
// saved during Poll/IKElbow/DoFancyElbow. Re-applied by ReapplyIKCorrections()
// after all CharPollables finish, to survive dirty cascades from later effectors.
// Stored in top-down skeleton order (grandparent before parent before effector)
// so re-application cascades correctly.
static std::vector<std::pair<RndTransformable*, Transform>> sIKSavedXfms;
#endif

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
#ifdef HX_NATIVE
            // Save IK-corrected parent chain transforms (top-down order)
            sIKSavedXfms.push_back({grandparent, xfm});
            sIKSavedXfms.push_back({parent, tf70});
#endif
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
    HamIKEffector *it = this;
    while (true) {
        RndTransformable *ground = it->mGround;
        if ((int)ground != 0) {
            return ground->WorldXfm().v.z;
        }
        it = it->mMore;
        if ((int)it == 0)
            break;
    }
    return t->WorldXfm().v.z;
}

void HamIKEffector::Poll() {
    if (mSkeleton) {
        EffectorType t = GetType();
        if (t != kEffectorTypeForearm) {
            float weight = Weight();
            if (mEffector && weight != 0.0f) {
                ObjPtr<RndTransformable> &fingerRef =
                    (int)mFinger.Ptr() != 0 ? mFinger : mEffector;
                RndTransformable *finger = fingerRef.Ptr();

                Transform neutral;
                mSkeleton->NeutralWorldXfm(finger, neutral);
                Normalize(neutral.m, neutral.m);
                Transform finalXfm;
                QuatXfm neutralQ(neutral);
                QuatXfm q;
                q.v.x = 0.0f;
                q.v.y = 0.0f;
                q.v.z = 0.0f;
                q.q.x = 0.0f;
                q.q.y = 0.0f;
                q.q.z = 0.0f;
                q.q.w = 0.0f;
                float totalWeight = ApplyConstraints(q, neutral, this);

                if (t == kEffectorTypeHand && mElbow != nullptr) {
                    DoFancyElbow(q, totalWeight);
                } else {
                    if (weight != 1.0f) {
                        MILO_ASSERT(weight == 1, 0x135);
                    }

                    if (totalWeight < 1.0f) {
                        if (totalWeight == 0.0f && t == kEffectorTypeNone)
                            goto done;

                        QuatXfm effQ(finger->WorldXfm());
                        if (t == kEffectorTypeAnkle || t == kEffectorTypePelvis) {
                            Character *character = mCharacter.Ptr();
                            RndTransformable *ground =
                                character ? (RndTransformable *)character : nullptr;
                            float groundHeight = GetGroundHeight(ground);

                            if (t == kEffectorTypePelvis) {
                                RndTransformable *knee =
                                    CharUtlFindBoneTrans("bone_L-knee", Dir());
                                RndTransformable *ankle =
                                    CharUtlFindBoneTrans("bone_L-ankle", Dir());
                                if (knee != nullptr && ankle != nullptr) {
                                    Vector3 localPos;
                                    mSkeleton->NeutralLocalPos(ankle, localPos);
                                    float ankleLen = localPos.x;
                                    mSkeleton->NeutralLocalPos(knee, localPos);
                                    float kneeLen = localPos.x;
                                    float totalLen = kneeLen + ankleLen;
                                    float worldHeight =
                                        ankle->mLocalXfm.v.x + knee->mLocalXfm.v.x;
                                    float ratio = worldHeight / totalLen;
                                    float lowerBound = kneeLen * 0.3f + ankleLen;
                                    float upperBound = kneeLen * 0.8f + ankleLen;
                                    float blend =
                                        (effQ.v.z - groundHeight - lowerBound)
                                        / (upperBound - lowerBound);
                                    blend = Max(0.0f, blend);
                                    blend = Min(blend, 1.0f);
                                    effQ.v.z =
                                        (((ratio - 1.0f) * blend) + 1.0f)
                                        * (effQ.v.z - groundHeight)
                                        + groundHeight;
                                }
                            } else if (t == kEffectorTypeAnkle) {
                                Vector3 savedPos = effQ.v;
                                float clampFactor =
                                    (neutralQ.v.z - groundHeight - 5.0f) * 0.09090909f;
                                clampFactor = Max(0.0f, clampFactor);
                                clampFactor = Min(clampFactor, 1.0f);
                                Interp(neutralQ.v, effQ.v, clampFactor, q.v);
                                Interp(neutralQ.q, effQ.q, clampFactor, q.q);
                                if (effQ.v.z < groundHeight) {
                                    effQ.v.z = groundHeight;
                                }
                                effQ.v.x = savedPos.x;
                                effQ.v.y = savedPos.y;
                            }
                        }

                        float remaining = 1.0f - totalWeight;
                        q.v.x += remaining * effQ.v.x;
                        q.v.y += remaining * effQ.v.y;
                        q.v.z += remaining * effQ.v.z;
                        ScaleAddEq(q.q, effQ.q, remaining);
                        totalWeight += remaining;
                    }

                    float invWeight = 1.0f / totalWeight;
                    q.v.x *= invWeight;
                    q.v.y *= invWeight;
                    q.v.z *= invWeight;
                    Normalize(q.q, q.q);

                    finalXfm.v = q.v;
                    MakeRotMatrix(q.q, finalXfm.m);

                    if (finger != mEffector.Ptr()) {
                        Transform inv;
                        if (finger->TransParent() == mEffector.Ptr()) {
                            FastInvert(finger->LocalXfm(), inv);
                        } else {
                            FastInvert(finger->WorldXfm(), inv);
                            Multiply(mEffector->WorldXfm(), inv, inv);
                        }
                        Multiply(inv, finalXfm, finalXfm);
                    }

                    if (mOther) {
                        mEffector->SetWorldXfm(finalXfm);
                        mOther->Poll();
                        finalXfm = mEffector->WorldXfm();
                    }

                    if (t == kEffectorTypeAnkle || t == kEffectorTypeHand) {
                        IKElbow(finalXfm.v);
                    }

#ifdef HX_NATIVE
                    // Ground clamp for ankle effectors. Raise the ankle enough
                    // to keep the toe bone above ground. Uses the finger (toe)
                    // bone's local rest-pose length as foot depth — this is
                    // orientation-invariant unlike NeutralWorldXfm().
                    if (t == kEffectorTypeAnkle) {
                        Character *character = mCharacter.Ptr();
                        RndTransformable *ground =
                            character ? (RndTransformable *)character : nullptr;
                        float groundHeight = GetGroundHeight(ground);

                        // Use the toe bone's rest-pose local offset length as
                        // foot depth. LocalXfm().v.x is the bone length along
                        // the skeleton chain — orientation-invariant.
                        float footDepth = 0.0f;
                        if (mFinger.Ptr() && mFinger.Ptr() != mEffector.Ptr()) {
                            footDepth = std::abs(mFinger->LocalXfm().v.x);
                            // Fallback: if finger has no length, use a
                            // conservative estimate from empirical testing
                            if (footDepth < 1.0f) footDepth = 3.5f;
                        }

                        float minAnkleZ = groundHeight + footDepth;
                        if (finalXfm.v.z < minAnkleZ
                            && std::isfinite(minAnkleZ)) {
                            finalXfm.v.z = minAnkleZ;
                        }
                    }
#endif

                    mEffector->SetWorldXfm(finalXfm);
#ifdef HX_NATIVE
                    // Save IK-corrected effector transform for post-poll re-application
                    sIKSavedXfms.push_back({mEffector.Ptr(), finalXfm});

                    // Foot inversion guard: after IK, verify the ankle-to-toe
                    // vector points generally downward (toward ground), not upward
                    // through the shin. The toe bone (mFinger) should be BELOW
                    // the ankle bone (mEffector) in world Z. If inverted, the
                    // mLocalXfm back-computation or dirty cascade has gone wrong.
                    if (t == kEffectorTypeAnkle && mFinger.Ptr()
                        && mFinger.Ptr() != mEffector.Ptr()) {
                        RndTransformable *toe = mFinger.Ptr();
                        RndTransformable *ankle = mEffector.Ptr();
                        float ankleZ = ankle->WorldXfm().v.z;
                        float toeZ = toe->WorldXfm().v.z;
                        // Also check the ankle's local Z-axis direction.
                        // In a correct pose, the rotation matrix's Z column
                        // (m.z) should point roughly downward (z component < 0)
                        // or at least not strongly upward.
                        float zAxisUp = ankle->WorldXfm().m.z.z;
                        // Toe above ankle by more than 2 units = inverted foot
                        if (toeZ > ankleZ + 2.0f) {
                            MILO_NOTIFY_ONCE(
                                "FOOT INVERTED: %s toe Z=%.1f > ankle Z=%.1f "
                                "(delta=%.1f, zAxisUp=%.2f)",
                                PathName(this), toeZ, ankleZ,
                                toeZ - ankleZ, zAxisUp
                            );
                        }
                        // Z-axis pointing strongly upward = rotation is flipped
                        if (zAxisUp > 0.7f) {
                            MILO_NOTIFY_ONCE(
                                "FOOT FLIPPED: %s ankle Z-axis points up "
                                "(z.z=%.2f), expected downward",
                                PathName(this), zAxisUp
                            );
                        }
                    }
#endif
                }
            }
        }
    }
done:;
}

void HamIKEffector::ComputeHandPullAndQuat(
    QuatXfm &quatOut, Transform &xfmOut, const Transform &parentXfm, const Vector3 &targetPos
) {
    float dz = targetPos.z - parentXfm.v.z;
    RndTransformable *effector = mEffector;
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
    float distSq = dz * dz + dy * dy + dx * dx;

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

    RndTransformable *effParent = mEffector->TransParent();
    xfmOut.v = effParent->LocalXfm().v;

    float sumSq = effectorLen * effectorLen + parentLen * parentLen;
    float cosAngle = (distSq - sumSq) / (parentLen * effectorLen * 2.0f);

    xfmOut.m.x.z = 0.0f;

    float clampedCos = -1.0f - cosAngle < 0.0f ? cosAngle : -1.0f;
    clampedCos = clampedCos - 1.0f < 0.0f ? clampedCos : 1.0f;

    xfmOut.m.x.x = clampedCos;
    float sinAngle = -sqrtf(-(clampedCos * clampedCos - 1.0f));
    xfmOut.m.x.y = sinAngle;
    const Vector3 &effLocalV = mEffector->LocalXfm().v;
    xfmOut.m.y.y = clampedCos;
    xfmOut.m.y.z = 0.0f;
    xfmOut.m.y.x = -sinAngle;
    xfmOut.m.z.x = 0.0f;
    xfmOut.m.z.y = 0.0f;
    xfmOut.m.z.z = 1.0f;

    Vector3 localDir;
    Multiply(effLocalV, xfmOut, localDir);
    Vector3 localTarget;
    MultiplyTranspose(targetPos, parentXfm, localTarget);
    MakeRotQuat(localDir, localTarget, quatOut.q);
}

void HamIKEffector::DoFancyElbow(QuatXfm &handQ, float handWeight) {
    RndTransformable *parent = mEffector->TransParent();
    if (parent != nullptr) {
        RndTransformable *grandparent = parent->TransParent();
        if (grandparent != nullptr) {
            // Get neutral world transform of parent
            Transform neutralParent;
            mSkeleton->NeutralWorldXfm(parent, neutralParent);

            // Apply elbow position constraints
            Vector3 posAccum;
            posAccum.z = 0.0f;
            posAccum.y = 0.0f;
            posAccum.x = 0.0f;
            float elbowWeight = mElbow->ApplyPosConstraints(posAccum, neutralParent.v, this);

            float totalWeight = elbowWeight + handWeight;
            if (totalWeight == 0.0f)
                return;

            // Initialize pull and quaternion accumulators
            QuatXfm accum;
            accum.v.x = 0.0f;
            accum.v.y = 0.0f;
            accum.v.z = 0.0f;
            accum.q.x = 0.0f;
            accum.q.y = 0.0f;
            accum.q.z = 0.0f;
            accum.q.w = 0.0f;
            float remaining = 0.0f;

            if (totalWeight < 1.0f) {
                remaining = 1.0f - totalWeight;
                totalWeight += remaining;
            }

            // Copy grandparent world transform
            Transform elbowXfm;
            Transform gpXfm = grandparent->WorldXfm();

            // Apply elbow contribution
            if (elbowWeight > 0.0f) {
                float invElbow = 1.0f / elbowWeight;
                posAccum.x *= invElbow;
                posAccum.y *= invElbow;
                posAccum.z *= invElbow;

                QuatXfm elbowQ;
                ComputeElbowPullAndQuat(elbowQ, gpXfm, posAccum);

                accum.v.x += elbowQ.v.x * elbowWeight;
                accum.v.y += elbowQ.v.y * elbowWeight;
                accum.v.z += elbowQ.v.z * elbowWeight;
                ScaleAddEq(accum.q, elbowQ.q, elbowWeight);
            }

            // Apply hand contribution
            if (handWeight > 0.0f) {
                float invHand = 1.0f / handWeight;
                Vector3 handPos;
                handPos.x = handQ.v.x * invHand;
                handPos.y = handQ.v.y * invHand;
                handPos.z = handQ.v.z * invHand;

                QuatXfm handPullQ;
                ComputeHandPullAndQuat(handPullQ, elbowXfm, gpXfm, handPos);

                accum.v.x += handPullQ.v.x * handWeight;
                accum.v.y += handPullQ.v.y * handWeight;
                accum.v.z += handPullQ.v.z * handWeight;
                ScaleAddEq(accum.q, handPullQ.q, handWeight);
            }

            // Normalize quaternion and compute final rotation
            Normalize(accum.q, accum.q);
            float invTotal = 1.0f / totalWeight;
            accum.v.x *= invTotal;
            accum.v.y *= invTotal;
            accum.v.z *= invTotal;

            Hmx::Matrix3 rotMat;
            MakeRotMatrix(accum.q, rotMat);
            Multiply(rotMat, gpXfm.m, gpXfm.m);

            // Apply scaled pull to grandparent position
            gpXfm.v.x += accum.v.x;
            gpXfm.v.y += accum.v.y;
            gpXfm.v.z += accum.v.z;

            grandparent->SetWorldXfm(gpXfm);
#ifdef HX_NATIVE
            sIKSavedXfms.push_back({grandparent, gpXfm});
#endif

            // If hand contributes, blend parent (forearm) and effector (hand) rotations
            if (handWeight > 0.0f) {
                // Blend parent rotation between local neutral and hand-computed elbow xfm
                Hmx::Quat parentQ;
                parentQ.Set(parent->LocalXfm().m);

                float otherWeight = remaining + elbowWeight;
                parentQ.x *= otherWeight;
                parentQ.y *= otherWeight;
                parentQ.z *= otherWeight;
                parentQ.w *= otherWeight;

                Hmx::Quat elbowXfmQ;
                elbowXfmQ.Set(elbowXfm.m);
                ScaleAddEq(parentQ, elbowXfmQ, handWeight);
                Normalize(parentQ, parentQ);

                MakeRotMatrix(parentQ, rotMat);

                // Build new parent world transform: blended rotation + current position
                Transform parentNewXfm;
                const Transform &parentWorld = parent->WorldXfm();
                parentNewXfm.v = parentWorld.v;
                Multiply(rotMat, gpXfm.m, parentNewXfm.m);
                parent->SetWorldXfm(parentNewXfm);
#ifdef HX_NATIVE
                sIKSavedXfms.push_back({parent, parentNewXfm});
#endif

                // Blend effector rotation
                const Transform &effWorld = mEffector->WorldXfm();
                Transform effXfm;
                effXfm.v = effWorld.v;

                Hmx::Quat effQ;
                effQ.Set(effWorld.m);
                ScaleAddEq(handQ.q, effQ, otherWeight);
                Normalize(handQ.q, handQ.q);

                MakeRotMatrix(handQ.q, effXfm.m);
                mEffector->SetWorldXfm(effXfm);
#ifdef HX_NATIVE
                sIKSavedXfms.push_back({mEffector.Ptr(), effXfm});
#endif
            }
        }
    }
}

void HamIKEffector::ComputeElbowPullAndQuat(
    QuatXfm &q, const Transform &xfm, const Vector3 &v
) {
    Vector3 v40;
    MultiplyTranspose(v, xfm, v40);
    const Vector3 &effectorV = mEffector->TransParent()->LocalXfm().v;
    MakeRotQuat(effectorV, v40, q.q);
    float dy = v.y - xfm.v.y;
    float dx = v.x - xfm.v.x;
    float dz = v.z - xfm.v.z;
    q.v.x = dx;
    float len = sqrtf(dy * dy + q.v.x * q.v.x + dz * dz);
    float factor = 1.0f - effectorV.x / len;
    q.v.x = q.v.x * factor;
    q.v.y = dy * factor;
    q.v.z = dz * factor;
}

#ifdef HX_NATIVE
void HamIKEffector::ReapplyIKCorrections() {
    if (sIKSavedXfms.empty()) return;

    // Re-apply saved transforms. They're already in top-down order
    // (grandparent → parent → effector) from how they were saved,
    // so each SetWorldXfm's dirty cascade to children is immediately
    // corrected by the next entry.
    for (auto& entry : sIKSavedXfms) {
        // Skip entries with NaN/Inf — don't propagate garbage
        const Vector3& v = entry.second.v;
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            continue;
        entry.first->SetWorldXfm(entry.second);
    }

    static int sFixupLogCount = 0;
    if (sFixupLogCount < 5 || sFixupLogCount % 5000 == 0) {
        fprintf(stderr, "DC3_IK_DIAG ReapplyIK: restored %d bone transforms\n",
                (int)sIKSavedXfms.size());
    }
    sFixupLogCount++;

    sIKSavedXfms.clear();
}
#endif
