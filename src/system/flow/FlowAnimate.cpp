#include "flow/FlowAnimate.h"
#include "flow/FlowNode.h"
#include "rndobj/Anim.h"
#include "obj/Object.h"

FlowAnimate::FlowAnimate()
    : unk5c(this), mAnim(this), mStopMode(kStopLastFrame), unk98(0), mBlend(0), mWait(0),
      mDelay(0), mEnable(0), mRate(RndAnimatable::k30_fps), mStart(0), mEnd(0),
      mPeriod(0), mScale(1), unkc4(0), mEase(kEaseLinear), mEasePower(2), mWrap(0),
      mImmediateRelease(0) {
    static Symbol range("range");
    mType = range;
}

FlowAnimate::~FlowAnimate() {
    if (unk5c != nullptr) {
    }
    if (mAnim != nullptr) {
    }
}


BEGIN_HANDLERS(FlowAnimate)
    HANDLE_ACTION(on_anim_event, OnAnimEvent(_msg->Sym(2)))
    HANDLE_ACTION(on_flow_finished, ChildFinished(_msg->Obj<FlowNode>(2)))
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowAnimate)
    SYNC_PROP_MODIFY(anim, mAnim, ResetAnim())
    SYNC_PROP(blend, mBlend)
    SYNC_PROP(wait, mWait)
    SYNC_PROP(delay, mDelay)
    SYNC_PROP(stop_mode, (int &)mStopMode)
    SYNC_PROP(enable, mEnable)
    SYNC_PROP(rate, (int &)mRate)
    SYNC_PROP(start, mStart)
    SYNC_PROP(end, mEnd)
    SYNC_PROP(scale, mScale)
    SYNC_PROP(period, mPeriod)
    SYNC_PROP(type, mType)
    SYNC_PROP(ease, (int &)mEase)
    SYNC_PROP(ease_power, mEasePower)
    SYNC_PROP(wrap, mWrap)
    SYNC_PROP(immediate_release, mImmediateRelease)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowAnimate)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mAnim << mBlend << mWait << mDelay;
    bs << mStopMode << mEnable;
    bs << mRate << mStart;
    bs << mEnd << mPeriod;
    bs << mType;
    bs << mScale << mEase << mEasePower;
    bs << mWrap;
    bs << mImmediateRelease;
END_SAVES

void FlowAnimate::Copy(const Hmx::Object *o, CopyType ty) {
    FlowNode::Copy(o, ty);
    const FlowAnimate *c = dynamic_cast<const FlowAnimate *>(o);
    if (c) {
        Symbol typeVal = c->mType;
        float blendVal = c->mBlend;

        mAnim = c->mAnim;
        mBlend = blendVal;
        mDelay = c->mDelay;
        mStopMode = c->mStopMode;
        mEnable = c->mEnable;
        mRate = c->mRate;
        mStart = c->mStart;
        mEnd = c->mEnd;
        mPeriod = c->mPeriod;
        mType = typeVal;
        mScale = c->mScale;
        mEase = c->mEase;
        mEasePower = c->mEasePower;
        mWrap = c->mWrap;
        mImmediateRelease = c->mImmediateRelease;
    }
}

void FlowAnimate::Load(BinStream &bs) {
    int revs;
    bs >> revs;
    BinStreamRev d(bs, revs);

    ASSERT_REVS(3, 0)
    FlowNode::Load(bs);

    if (d.rev < 3) {
        mAnim.LoadFromMainOrDir(d.stream);
    }

    d >> mBlend >> mWait >> mDelay;
    d >> (int&)mStopMode >> mEnable;
    d >> (int&)mRate >> mStart;
    d >> mEnd >> mPeriod;
    d >> mType;
    d >> mScale >> (int&)mEase >> mEasePower;
    d >> mWrap;
    d >> mImmediateRelease;
}

void FlowAnimate::ResetAnim() {
    if (mAnim && !FlowNode::sPushDrivenProperties) {
        mRate = mAnim->GetRate();
        mStart = mAnim->StartFrame();
        mEnd = mAnim->EndFrame();
        mEase = kEaseLinear;
        mWrap = false;
        mPeriod = 0;
        mScale = 1;
        mEasePower = 2;
        static Symbol range("range");
        static Symbol loop("loop");
        mType = mAnim->Loop() ? loop : range;
    }
}
