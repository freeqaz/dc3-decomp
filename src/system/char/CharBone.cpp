#include "char/CharBone.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"

BinStream &operator<<(BinStream &bs, const CharBone::WeightContext &ctx) {
    bs << ctx.mContext;
    bs << ctx.mWeight;
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &bs, CharBone::WeightContext &ctx) {
    bs >> ctx.mContext;
    bs >> ctx.mWeight;
    return bs;
}

BEGIN_CUSTOM_PROPSYNC(CharBone::WeightContext)
    SYNC_PROP(context, o.mContext)
    SYNC_PROP(weight, o.mWeight)
END_CUSTOM_PROPSYNC

void CharBone::ClearContext(int i) {
    int mask = ~i;
    mPositionContext &= mask;
    mScaleContext &= mask;
    mRotationContext &= mask;
}

BEGIN_HANDLERS(CharBone)
    HANDLE_ACTION(clear_context, ClearContext(_msg->Int(2)))
    HANDLE(get_context_flags, OnGetContextFlags)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_SAVES(CharBone)
    SAVE_REVS(10, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mPositionContext;
    bs << mScaleContext;
    bs << mRotation;
    bs << mRotationContext;
    bs << mTarget;
    bs << mWeights;
    bs << mTrans;
    bs << mBakeOutAsTopLevel;
END_SAVES

BEGIN_PROPSYNCS(CharBone)
    SYNC_PROP(position_context, mPositionContext)
    SYNC_PROP(scale_context, mScaleContext)
    SYNC_PROP(rotation, (int &)mRotation)
    SYNC_PROP(rotation_context, mRotationContext)
    SYNC_PROP(target, mTarget)
    SYNC_PROP(weights, mWeights)
    SYNC_PROP(trans, mTrans)
    SYNC_PROP(bake_out_as_top_level, mBakeOutAsTopLevel)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

CharBone::CharBone()
    : mPositionContext(0), mScaleContext(0), mRotation(CharBones::TYPE_END),
      mRotationContext(0), mTarget(this), mWeights(), mTrans(this),
      mBakeOutAsTopLevel(0) {}

BEGIN_LOADS(CharBone)
    LOAD_REVS(bs)
    ASSERT_REVS(10, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.rev < 9) {
        RndTransformableRemover t;
        t.Load(bs);
    }
    if (d.rev > 6) {
        d >> mPositionContext;
    } else {
        bool val;
        d >> val;
        mPositionContext = (int)val;
    }
    if (d.rev > 6) {
        d >> mScaleContext;
    } else if (d.rev > 1) {
        bool val;
        d >> val;
        mScaleContext = (int)val;
    }
    int rot_val;
    d >> rot_val;
    mRotation = (CharBones::Type)rot_val;
    if (d.rev < 5) {
        int dummy;
        d >> dummy;
    }
    if (d.rev < 2) {
        mScaleContext = 0;
        mRotation = (CharBones::Type)(rot_val + 1);
    }
    if ((d.rev < 5) && (rot_val > 6)) {
        mRotation = (CharBones::Type)6;
    }
    if (d.rev > 6) {
        d >> mRotationContext;
    } else {
        mRotationContext = rot_val - 6;
    }
    if ((d.rev > 2) && (d.rev < 8)) {
        int dummy;
        d >> dummy;
    }
    if (d.rev > 3) {
        d >> mTarget;
    }
    if (d.rev == 6) {
        int val;
        d >> val;
        if (mPositionContext != 0) {
            mPositionContext = val;
        }
        if (mScaleContext != 0) {
            mScaleContext = val;
        }
        if (mRotationContext != 0) {
            mRotationContext = val;
        }
    } else {
        if (d.rev > 7) {
            d >> mWeights;
        }
        if (d.rev > 8) {
            d >> mTrans;
        }
        if (d.rev > 9) {
            bool val;
            d >> val;
            mBakeOutAsTopLevel = val;
        }
    }
END_LOADS

BEGIN_COPYS(CharBone)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(CharBone)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mRotationContext)
        COPY_MEMBER(mScaleContext)
        COPY_MEMBER(mPositionContext)
        COPY_MEMBER(mRotation)
        COPY_MEMBER(mTarget)
        COPY_MEMBER(mWeights)
        COPY_MEMBER(mTrans)
        COPY_MEMBER(mBakeOutAsTopLevel)
    END_COPYING_MEMBERS
END_COPYS
