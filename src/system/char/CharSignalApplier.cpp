#include "char\CharSignalApplier.h"
#include "char\CharBoneTwist.h"
#include "char\CharWeightable.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "math\Utl.h"
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

// Lane history (23 lines moved): docs/decomp/notes/system/char/CharSignalApplier.md
// > The whole residual is the three `__new_finish = <call>(...)` assignments inside the
// > template's body. (w28-b)
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
    if (0 == mBoneOps.size())
        return;
    // Lane history (5 lines moved): docs/decomp/notes/system/char/CharSignalApplier.md
    ClampEq(mSignal, mSignalMin, mSignalMax);
    // Lane history (6 lines moved): docs/decomp/notes/system/char/CharSignalApplier.md
    if (!mDoSmoothing) {
        mSmoothedSignal = mSignal;
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
    BoneOp *cur = mBoneOps.begin();
    mSmoothedSignal *= Weight();
    for (; cur != mBoneOps.end(); cur++) {
        {
            BoneOp op = *cur;
            RndTransformable *bone = op.mBone;
            if (bone) {
                Transform boneTf;
                memcpy(&boneTf, &bone->WorldXfm(), sizeof(Transform));
                float t;
                if (mSignalMax != mSignalMin) {
                    t = (mSmoothedSignal * op.mApplyPercent - mSignalMin)
                        / (mSignalMax - mSignalMin);
                } else {
                    t = 1.0f;
                }
                float angle
                    = ((op.mMaxAngle - op.mMinAngle) * t + op.mMinAngle) * DEG2RAD;
                // The Multiply lives INSIDE each case, and MSVC cross-jumps the
                // three identical tails into the one at 0x823AB2DC -- which is
                // why `cmplwi r11, 0x3 / bge .L_823AB2EC` (0x823AB2A8) skips the
                // Multiply entirely for an out-of-range mOp instead of jumping
                // to it.  Writing it as one call after the switch composes an
                // UNINITIALISED rotation matrix into the bone transform whenever
                // mOp >= 3.
                switch ((unsigned int)op.mOp) {
                case 0: {
                    Hmx::Matrix3 rotMatX;
                    MakeRotMatrixX(angle, rotMatX);
                    Multiply(rotMatX, boneTf.m, boneTf.m);
                    break;
                }
                case 1: {
                    Hmx::Matrix3 rotMatY;
                    MakeRotMatrixY(angle, rotMatY);
                    Multiply(rotMatY, boneTf.m, boneTf.m);
                    break;
                }
                case 2: {
                    Hmx::Matrix3 rotMatZ;
                    MakeRotMatrixZ(angle, rotMatZ);
                    Multiply(rotMatZ, boneTf.m, boneTf.m);
                    break;
                }
                }
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
        }
    }
}

// Lane history (12 lines moved): docs/decomp/notes/system/char/CharSignalApplier.md
BEGIN_HANDLERS(CharSignalApplier)
    HANDLE_SUPERCLASS(CharWeightable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void CharSignalApplier::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    // 0x823AB420: the bones go into the FIRST list (r4 -> r25, and
    // `stw r4, 0x54(r31)` hoisted out of the loop is that list's end()
    // iterator), not the second, and the push is UNCONDITIONAL -- the
    // `bne / stw 0 / b` at 0x823AB484 is MSVC's null-safe
    // RndTransformable* -> Hmx::Object* virtual-base adjustment
    // (vbptr at +4, vbtable[1], +4), not a user-written `if (bone)`.
    // Both halves were wrong here: we pushed into `change` and we dropped
    // null bones on the floor.
    for (BoneOp *cur = mBoneOps.begin(); cur != mBoneOps.end(); cur++) {
        BoneOp op = *cur;
        changedBy.push_back(op.mBone);
    }
}

// Lane history (6 lines moved): docs/decomp/notes/system/char/CharSignalApplier.md
// > The image stores __new_finish to its EH home (`stw r3, 0x54(r31)`) right after the
// > first __uninitialized_copy, before the fill_len==1 _Copy_Construct; we only store it
// > at the join, i.e. our build treats that window as unable to throw. (w15-i1)
#ifndef HX_NATIVE
// Minimal __uninitialized_fill_n implementation for BoneOp
namespace stlpmtx_std {

template <>
CharSignalApplier::BoneOp* __uninitialized_fill_n<CharSignalApplier::BoneOp*, unsigned int, CharSignalApplier::BoneOp>(
    CharSignalApplier::BoneOp* first,
    unsigned int count,
    const CharSignalApplier::BoneOp& value,
    __false_type const&
) {
    // Plain copy-CONSTRUCTION, not hand-poked offsets.  BoneOp's copy ctor
    // already has the image's exact shape -- `mBone(op.mBone.Owner())` then
    // `*this = op` -- which is the vtable store at 0x0, the 0 at 0xc, the
    // `lwz 0x10(value)` / `stw 0x10(cur)` owner copy, and the
    // `bl ??4BoneOp@CharSignalApplier@@QAAAAU01@ABU01@@Z` at 0x823AB80C.
    // The `if (cur != NULL)` guard the old body wrote by hand is MSVC's own
    // null check for placement new (0x823AB7E8), not something the algorithm
    // asks for; and the 0x10000000 vtable literal was simply wrong -- the
    // image stores &??_7?$ObjPtr@VRndTransformable@@@@6B@, hoisted out of the
    // loop into r28.
    CharSignalApplier::BoneOp* cur = first;
    if (count != 0U) {
        do {
            _Copy_Construct(cur, value);
            count--;
            cur++;
        } while (count != 0U);
    }
    return cur;
}

}
#endif


// Lane history (60 lines moved): docs/decomp/notes/system/char/CharSignalApplier.md
