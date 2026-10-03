#include "char\CharBone.h"
#include "char\CharBoneDir.h"
#include "char\CharBones.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"

CharBone::CharBone()
    : mPositionContext(0), mScaleContext(0), mRotation(CharBones::TYPE_END),
      mRotationContext(0), mTarget(this), mWeights(), mTrans(this),
      mBakeOutAsTopLevel(0) {}

void CharBone::ClearContext(int mask) {
    mPositionContext &= ~mask;
    mScaleContext &= ~mask;
    mRotationContext &= ~mask;
}

// RESIDUAL (w9-b, 99.70149 canonical, 268 B, 20 rows).  Every row is an
// offset-only diff on an (r1) stack slot -- no wrong field, no wrong constant:
// it is a stack-SLOT POOLING difference, and the two frames are the same size
// (0x90 both).  The image's scalar frame, read off the target listing, is
//
//   0x50  Symbol temp of the FIRST  ChannelName() call
//   0x54  the end() iterator temp   (shared by all three push_back()s)
//   0x58  Symbol temp of the SECOND ChannelName() call
//   0x5c  Symbol temp of the THIRD  ChannelName() call
//   0x60  the insert() sret slot    (shared)
//   0x68  the Bone (name 0x68, weight 0x6c)   (shared)
//
// i.e. each ChannelName destination gets its OWN slot while the push_back
// internals are pooled.  Ours pools the Symbols WITH the iterator temp and
// alternates them: block 1 puts the Symbol at 0x50 and the iterator at 0x54,
// blocks 2 and 3 put the Symbol at 0x54 and the iterator at 0x50.  Symbol and
// _List_iterator are both 4-byte one-pointer structs, which is why MSVC is
// willing to pool them.
//
// NEGATIVE RESULTS (w9-b), all measured with a full ninja:
//   * unnamed ChannelName temporaries (`bone.name = CharBones::ChannelName(...)`,
//     no `Symbol name` local): 86.239, frame shrinks to 0x80.  This is also the
//     diagnostic that fixes the spelling of the Symbol: with a temporary MSVC
//     reads it back through the sret pointer (`mr r11, r3; lwz r11, 0x0(r11)`),
//     where the image reads the known slot (`lwz r11, 0x50(r1)`).  The image's
//     Symbol is therefore a NAMED local, as written below -- do not "simplify"
//     this to the RB3 one-liner.
//   * `CharBones::Bone bone;` hoisted to function scope, Symbols left per block:
//     90.746.
//   * keeping the named Symbol but making the Bone a temporary
//     (`bones.push_back(CharBones::Bone(name, GetWeight(mask)))`): exactly inert,
//     99.70149 with an identical row set.  Readable either way; left as-is.
// What is still unexplained is why MSVC reuses the named Symbol's slot across
// the three sibling `if` scopes here and does not in the image.  A spelling that
// keeps the declaration-order allocation (sym1, iterator, sym2, sym3) while
// stopping the reuse is the open lead; three-at-function-scope does not, because
// it would allocate the three Symbols contiguously ahead of the iterator temp.
// w21-aq (stopped at 99.70149, same 20 r1-slot rows as w9-b; no new lever).
// Measured, all byte-inert (identical 20-row set): `const Symbol &name =
// ChannelName(...)` (lifetime-extended temporary instead of a named local);
// moving each block into a `static inline` helper (inlinee locals are pooled
// exactly like block locals); declaring the Bone before the Symbol; the
// two-arg `CharBones::Bone bone(name, GetWeight(mask))` ctor.  The RB3
// one-liner `push_back(Bone(ChannelName(...), GetWeight(mask)))` is 58.8: MSVC
// evaluates GetWeight first (f31 spill), the image calls ChannelName first.
// rb3-xenon's CharBone.cpp records the same 20 rows and a 0-for-3 record for
// the "make the slots match" direction.  The image keeps three DISTINCT Symbol
// slots (0x50/0x58/0x5c) while pooling the push_back temps (0x54/0x60/0x68),
// which reads like the Symbols interfering with each other -- a spelling with
// function-lifetime Symbols that does not default-construct them is the only
// lead left, and none is known.
void CharBone::StuffBones(std::list<CharBones::Bone> &bones, int mask) const {
    if (mPositionContext & mask) {
        Symbol name = CharBones::ChannelName(Name(), CharBones::TYPE_POS);
        CharBones::Bone bone;
        bone.name = name;
        bone.weight = GetWeight(mask);
        bones.push_back(bone);
    }
    if (mScaleContext & mask) {
        Symbol name = CharBones::ChannelName(Name(), CharBones::TYPE_SCALE);
        CharBones::Bone bone;
        bone.name = name;
        bone.weight = GetWeight(mask);
        bones.push_back(bone);
    }
    if (mRotation != CharBones::TYPE_END && mRotationContext & mask) {
        Symbol name = CharBones::ChannelName(Name(), mRotation);
        CharBones::Bone bone;
        bone.name = name;
        bone.weight = GetWeight(mask);
        bones.push_back(bone);
    }
}

const CharBone::WeightContext *CharBone::FindWeight(int ctx) const {
    FOREACH (it, mWeights) {
        if (it->mContext & ctx) {
            return &*it;
        }
    }
    return nullptr;
}

float CharBone::GetWeight(int mask) const {
    const WeightContext *ctx = FindWeight(mask);
    if (ctx) {
        return ctx->mWeight;
    } else {
        return 1.0f;
    }
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

BEGIN_CUSTOM_PROPSYNC(CharBone::WeightContext)
    SYNC_PROP(context, o.mContext)
    SYNC_PROP(weight, o.mWeight)
END_CUSTOM_PROPSYNC

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

BinStream &operator<<(BinStream &bs, const CharBone::WeightContext &ctx) {
    bs << ctx.mContext;
    bs << ctx.mWeight;
    return bs;
}

BinStream &operator>>(BinStream &d, CharBone::WeightContext &w) {
    d >> w.mContext >> w.mWeight;
    return d;
}

INIT_REVS(10, 0)

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

BEGIN_LOADS(CharBone)
    LOAD_REVS(bs)
    ASSERT_REVS(10, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.rev < 9) {
        RndTransformableRemover t;
        t.Load(d.stream);
    }
    if (d.rev > 6) {
        d >> mPositionContext;
    } else {
        bool b;
        d >> b;
        mPositionContext = b;
    }
    if (d.rev > 6) {
        d >> mScaleContext;
    } else if (d.rev > 1) {
        bool b;
        d >> b;
        mScaleContext = b;
    }
    d >> (int &)mRotation;
    if (d.rev < 5) {
        int x;
        d >> x;
    }
    if (d.rev < 2) {
        mScaleContext = 0;
        mRotation = (CharBones::Type)(mRotation + 1);
    }
    if (d.rev < 5 && mRotation > CharBones::TYPE_END) {
        mRotation = CharBones::TYPE_END;
    }
    if (d.rev > 6) {
        d >> mRotationContext;
    } else {
        mRotationContext = mRotation != CharBones::TYPE_END;
    }
    if (d.rev > 2 && d.rev < 8) {
        int x;
        d >> x;
    }
    if (d.rev > 3) {
        d >> mTarget;
    }
    if (d.rev == 6) {
        int ctx;
        d >> ctx;
        if (mPositionContext != 0) {
            mPositionContext = ctx;
        }
        if (mScaleContext != 0) {
            mScaleContext = ctx;
        }
        if (mRotationContext != 0) {
            mRotationContext = ctx;
        }
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
