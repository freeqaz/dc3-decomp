#include "char/CharBone.h"
#include "char/CharBoneDir.h"
#include "char/CharBones.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"

BinStream &operator<<(BinStream &bs, const CharBone::WeightContext &ctx) {
    bs << ctx.mContext;
    bs << ctx.mWeight;
    return bs;
}

BinStream &operator>>(BinStream &bs, CharBone::WeightContext &ctx) {
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

void CharBone::StuffBones(std::list<CharBones::Bone> &bonelist, int i) const {
    if (mPositionContext & i) {
        Symbol s = CharBones::ChannelName(Name(), CharBones::TYPE_POS);
        bonelist.push_back(CharBones::Bone(s, GetWeight(i)));
    }
    if (mScaleContext & i) {
        Symbol s = CharBones::ChannelName(Name(), CharBones::TYPE_SCALE);
        bonelist.push_back(CharBones::Bone(s, GetWeight(i)));
    }
    if (mRotation != CharBones::TYPE_END && mRotationContext & i) {
        Symbol s = CharBones::ChannelName(Name(), mRotation);
        bonelist.push_back(CharBones::Bone(s, GetWeight(i)));
    }
}

float CharBone::GetWeight(int i) const {
    const WeightContext *ctx = FindWeight(i);
    if (ctx)
        return ctx->mWeight;
    else
        return 1.0f;
}

const CharBone::WeightContext *CharBone::FindWeight(int i) const {
    for (std::list<WeightContext>::const_iterator it = mWeights.begin();
         it != mWeights.end();
         ++it) {
        if ((*it).mContext & i)
            return &(*it);
    }
    return 0;
}

DataNode CharBone::OnGetContextFlags(DataArray *da) {
    CharBoneDir *dir = dynamic_cast<CharBoneDir *>(Dir());
    if (dir)
        return dir->GetContextFlags();
    else {
        MILO_NOTIFY("CharBone: No CharBoneDir for context flags.");
        return DataArrayPtr();
    }
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

INIT_REVS(10, 0)

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
    d >> (int&)mRotation;
    if (d.rev < 5) {
        int dummy;
        d >> dummy;
    }
    if (d.rev < 2) {
        mScaleContext = 0;
        mRotation = (CharBones::Type)((int)mRotation + 1);
    }
    if ((d.rev < 5) && ((int)mRotation > 6)) {
        mRotation = (CharBones::Type)6;
    }
    if (d.rev > 6) {
        d >> mRotationContext;
    } else {
        mRotationContext = (mRotation != CharBones::TYPE_END);
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
        if (mRotationContext == 0) {
            return;
        }
        mRotationContext = val;
    }
    if (d.rev > 7) {
        d >> mWeights;
    }
    if (d.rev > 8) {
        d >> mTrans;
    }
    if (d.rev > 9) {
        d >> mBakeOutAsTopLevel;
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
