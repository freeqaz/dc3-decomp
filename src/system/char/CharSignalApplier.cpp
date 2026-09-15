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
    // RESIDUAL (w7-al, 96.7 canonical): 13 rows, all inside the 20-instruction
    // clamp/mDoSmoothing region.  The image loads mSignalMin (0x2c) before
    // mSignal (0x28), keeps the clamp result in f0 (we use f13), reads
    // mDoSmoothing only AFTER `stfs f0, 0x28`, and reaches the smoothing block
    // with `bne` over a `b` instead of one `beq`.  Refuted spellings, each
    // byte-inert or worse: `Min(Max(mSignalMin, mSignal), mSignalMax)` written
    // out longhand (inert); dropping the `clamped` local so both arms re-read
    // mSignal (inert, kept -- the image does reload it at 0x823AB16C);
    // `if (mDoSmoothing) A else B` (96.7 -> 94.6, see below).  The remainder is
    // the scheduler filling the fsel dependence stalls with the lbz, plus the
    // r3/r28 `this` copy switching over one block later than the image's.
    mSignal = Clamp(mSignalMin, mSignalMax, mSignal);
    // NEGATIVE RESULT (w7-al, 2026-09-14): 0x823AB164 is `bne` over a `b`,
    // which looks like `if (mDoSmoothing) A else B`, but spelling it that way
    // costs more than it buys -- it flips the inner `fabs(...) < inc` branch
    // (0x823AB18C `bge`) the wrong way and drops the shared
    // `stfs f13, 0x3c(r28)` tail.  95.7 -> 94.6.  The `!mDoSmoothing` form is
    // kept; the branch-around-branch at 0x823AB164 is an MSVC peephole miss.
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

DataNode CharSignalApplier::Handle(DataArray *d, bool b) {
    return Hmx::Object::Handle(d, b);
}

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


// w8-c: `merged_823AAA20` (436 B) holds at 0.0000 and is NOT work.
// It is an unscoreable pairing artifact, not a missing body.
//   - config/373307D9/symbols.txt:121732 binds the address with a synthetic
//     placeholder: `merged_823AAA20 = .text:0x823AAA20; // type:function
//     size:0x1B4 scope:global`. dtk therefore carves the target-side COMDAT
//     under that name, and no mangled C++ name on our side can ever pair
//     with it -- objdiff has nothing to match against.
//   - build/373307D9/icf_aliases.map:11012-11015 names the fold group:
//     0x823AAA20 = `CharBoneTwist::Handle` + `CharSignalApplier::Handle`.
//     /Gy COMDAT folding collapsed the two identical Handle bodies into one
//     address, and the linker map records both members.
//   - We DO emit the real body. `strings build/373307D9/src/system/char/
//     CharSignalApplier.obj` shows
//     `?Handle@CharSignalApplier@@UAA?AVDataNode@@PAVDataArray@@_N@Z`, and
//     the vtable thunk `?Handle@CharSignalApplier@@$4PPPPPPPM@A@AA?AVDataNode
//     @@PAVDataArray@@_N@Z` scores 100.0000 (12 B) in report.json.
//   - Target body: CharSignalApplier.s:1155-1275. Referenced as data at
//     CharSignalApplier.s:670 (`.4byte merged_823AAA20`, the vtable slot) and
//     tail-called at CharSignalApplier.s:3015 (`b merged_823AAA20`). Its
//     shape is the ordinary Handle dispatch: `?Sym@DataArray@@QBA?AVSymbol@@H@Z`
//     at 0x823AAA58, `??0Timer@@QAA@XZ` 0x823AAA94, `?Restart@Timer@@QAAXXZ`
//     0x823AAAA4, `?Handle@CharWeightable@@UAA...` 0x823AAAB8,
//     `?Handle@Object@Hmx@@UAA...` 0x823AAB10, the
//     `"%s unhandled msg: %s"` MakeString at 0x823AAB8C, and
//     `?AddTime@MessageTimer@@KAX...` 0x823AABC4 -- i.e. exactly what our
//     CharSignalApplier::Handle already compiles to.
//   - REFUTED: "CharBoneTwist is the fold winner, so name it there instead."
//     `grep -n "Handle@CharBoneTwist" build/373307D9/asm/system/char/
//     CharBoneTwist.s` returns nothing -- CharBoneTwist's Handle is not named
//     in the listings either. Both members of the group are anonymous; the
//     row can only be closed by renaming the address in symbols.txt, which is
//     a config change outside this lane, not a source fix.
