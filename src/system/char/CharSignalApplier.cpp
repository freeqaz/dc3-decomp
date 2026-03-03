#include "char/CharSignalApplier.h"
#include "char/CharBoneTwist.h"
#include "char/CharWeightable.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Utl.h"
#include "obj/Object.h"

CharSignalApplier::BoneOp::BoneOp(Hmx::Object *o) : mBone(o) {
    mOp = 0;
    mApplyPercent = 1.0f;
    mMinAngle = -30.0f;
    mMaxAngle = 30.0f;
}

CharSignalApplier::BoneOp &
CharSignalApplier::BoneOp::operator=(const CharSignalApplier::BoneOp &op) {
    mBone = op.mBone;
    mOp = op.mOp;
    mApplyPercent = op.mApplyPercent;
    mMinAngle = op.mMinAngle;
    mMaxAngle = op.mMaxAngle;
    return *this;
}

BinStream &operator<<(BinStream &bs, const CharSignalApplier::BoneOp &op) {
    bs << op.mBone;
    bs << op.mOp;
    bs << op.mApplyPercent;
    bs << op.mMinAngle;
    bs << op.mMaxAngle;
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &d, CharSignalApplier::BoneOp &op) {
    d >> op.mBone;
    d >> op.mOp;
    d >> op.mApplyPercent;
    d >> op.mMinAngle;
    d >> op.mMaxAngle;
    return d;
}

BEGIN_HANDLERS(CharBoneTwist)
    HANDLE_SUPERCLASS(CharWeightable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(CharSignalApplier::BoneOp)
    SYNC_PROP(bone, o.mBone)
    SYNC_PROP(op, o.mOp)
    SYNC_PROP(apply_percent, o.mApplyPercent)
    SYNC_PROP(min_angle, o.mMinAngle)
    SYNC_PROP(max_angle, o.mMaxAngle)
END_CUSTOM_PROPSYNC

CharSignalApplier::CharSignalApplier()
    : mSignal(0), mSignalMin(-1.0f), mSignalMax(1.0f), mDoSmoothing(false),
      mSmoothIncrement(0.1f), mSmoothedSignal(0), mBoneOps(this) {}

BEGIN_PROPSYNCS(CharSignalApplier)
    SYNC_PROP(bone_ops, mBoneOps)
    SYNC_PROP(signal, mSignal)
    SYNC_PROP(do_smoothing, mDoSmoothing)
    SYNC_PROP(smooth_increment, mSmoothIncrement)
    SYNC_PROP(signal_min, mSignalMin)
    SYNC_PROP(signal_max, mSignalMax)
    SYNC_SUPERCLASS(CharWeightable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharSignalApplier)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(CharWeightable)
    bs << mBoneOps;
    bs << mSignal;
    bs << mSignalMin;
    bs << mSignalMax;
    bs << mDoSmoothing << mSmoothIncrement;
END_SAVES

BEGIN_COPYS(CharSignalApplier)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(CharWeightable)
    CREATE_COPY_AS(CharSignalApplier, c)
    BEGIN_COPYING_MEMBERS
        mBoneOps = c->mBoneOps;
        COPY_MEMBER(mSignal)
        COPY_MEMBER(mSignalMin)
        COPY_MEMBER(mSignalMax)
        COPY_MEMBER(mDoSmoothing)
        COPY_MEMBER(mSmoothIncrement)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(CharSignalApplier)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(CharWeightable)
    d >> mBoneOps;
    bs >> mSignal;
    bs >> mSignalMin;
    bs >> mSignalMax;
    d >> mDoSmoothing;
    bs >> mSmoothIncrement;
END_LOADS

void CharSignalApplier::Poll() {
    if (mBoneOps.size() == 0)
        return;
    float clamped = Clamp(mSignalMin, mSignalMax, mSignal);
    mSignal = clamped;
    BoneOp *cur = mBoneOps.begin();
    if (!mDoSmoothing) {
        mSmoothedSignal = clamped;
    } else {
        float target = mSignal;
        float smoothed = mSmoothedSignal;
        if (smoothed != target) {
            float inc = mSmoothIncrement;
            if (fabs(target - smoothed) < inc) {
                mSmoothedSignal = target;
            } else {
                if (target > smoothed) {
                    mSmoothedSignal = inc + smoothed;
                } else {
                    mSmoothedSignal = smoothed - inc;
                }
            }
        }
    }
    mSmoothedSignal *= Weight();
    if (cur != mBoneOps.end()) {
        do {
            BoneOp op(this);
            op = *cur;
            RndTransformable *bone = op.mBone;
            if (bone) {
                Transform boneTf;
                memcpy(&boneTf, &bone->WorldXfm(), sizeof(Transform));
                float t = 1.0f;
                if (mSignalMax != mSignalMin) {
                    t = (mSmoothedSignal * op.mApplyPercent - mSignalMin)
                        / (mSignalMax - mSignalMin);
                }
                float angle
                    = ((op.mMaxAngle - op.mMinAngle) * t + op.mMinAngle) * DEG2RAD;
                Hmx::Matrix3 rotMatX, rotMatY, rotMatZ;
                Hmx::Matrix3 *rotMat;
                switch ((unsigned int)op.mOp) {
                case 0:
                    MakeRotMatrixX(angle, rotMatX);
                    rotMat = &rotMatX;
                    break;
                case 1:
                    MakeRotMatrixY(angle, rotMatY);
                    rotMat = &rotMatY;
                    break;
                default:
                    MakeRotMatrixZ(angle, rotMatZ);
                    rotMat = &rotMatZ;
                    break;
                }
                Multiply(*rotMat, boneTf.m, boneTf.m);
                RndTransformable *parent = bone->TransParent();
                if (parent) {
                    Transform invParent;
                    Invert(parent->WorldXfm(), invParent);
                    Vector3 savedV = bone->LocalXfm().v;
                    Transform localTf;
                    Multiply(boneTf, invParent, localTf);
                    Normalize(localTf.m, localTf.m);
                    localTf.v = savedV;
                    bone->DirtyLocalXfm() = localTf;
                }
            }
            cur++;
        } while (cur != mBoneOps.end());
    }
}

void CharSignalApplier::PollDeps(std::list<Hmx::Object *> &a, std::list<Hmx::Object *> &b) {
    for (size_t i = 0; i < mBoneOps.size(); i++) {
        for (std::list<Hmx::Object *>::iterator it = a.begin(); it != a.end(); ++it) {
            b.push_back(*it);
        }
    }
}
