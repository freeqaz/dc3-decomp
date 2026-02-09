#include "hamobj/HamCamTransform.h"
#include "obj/Object.h"
#include "rndobj/Poll.h"
#include "utl/BinStream.h"
#include "world/CameraShot.h"

HamCamTransform::HamCamTransform() : mAreas(this) {}

HamCamTransform::~HamCamTransform() { ClearOldCrowds(); }

BEGIN_HANDLERS(HamCamTransform)
    HANDLE_ACTION(update_camshots, Setup(true))
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(TransformCrowd)
    SYNC_PROP(crowd, o.mCrowd)
    SYNC_PROP(crowd_rotate, (int &)o.mCrowdRotate)
END_CUSTOM_PROPSYNC

BEGIN_CUSTOM_PROPSYNC(TransformArea)
    SYNC_PROP(area, o.mArea)
    SYNC_PROP(camshots, o.mCamshots)
    SYNC_PROP(anims, o.mAnims)
    SYNC_PROP(crowds, o.mCrowds)
    SYNC_PROP(flow, o.mFlow)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(HamCamTransform)
    SYNC_PROP(areas, mAreas)
    SYNC_SUPERCLASS(RndPollable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HamCamTransform)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mAreas;
END_SAVES

BinStream &operator<<(BinStream &bs, const TransformCrowd &c) {
    c.Save(bs);
    return bs;
}

void TransformCrowd::Save(BinStream &bs) const { bs << mCrowd << mCrowdRotate; }

BinStream &operator>>(BinStream &bs, TransformCrowd &c) {
    c.Load(bs);
    return bs;
}

void TransformCrowd::Load(BinStream &bs) {
    bs >> mCrowd >> (int &)mCrowdRotate;
}

TransformArea::TransformArea(Hmx::Object *owner)
    : mArea(owner), mCamshots(owner), mAnims(owner), mCrowds(owner), mFlow(owner) {}

TransformArea::TransformArea(Hmx::Object *owner, const TransformArea &other)
    : mArea(other.mArea), mCamshots(other.mCamshots), mAnims(other.mAnims),
      mCrowds(other.mCrowds), mFlow(other.mFlow) {}

BinStream &operator<<(BinStream &bs, const TransformArea &a) {
    a.Save(bs);
    return bs;
}

void TransformArea::Save(BinStream &bs) const {
    bs << mArea;
    bs << mCamshots;
    bs << mAnims;
    bs << mCrowds;
    bs << mFlow;
}

void TransformArea::Load(BinStreamRev &d) {
    d >> mArea;
    mCamshots.Load(d.stream, false, nullptr, true);
    d >> mAnims;
    if (d.rev > 1) {
        d >> mCrowds;
    }
    if (d.rev > 2) {
        d >> mFlow;
    }
}
