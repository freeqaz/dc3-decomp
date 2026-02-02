#include "char/CharSignalApplier.h"
#include "char/CharWeightable.h"
#include "obj/Object.h"

CharSignalApplier::CharSignalApplier()
    : mSignal(0), mSignalMin(-1.0f), mSignalMax(1.0f), mDoSmoothing(false),
      mSmoothIncrement(0.1f), mSmoothedSignal(0), mBoneOps(this) {}

BEGIN_PROPSYNCS(CharSignalApplier)
    // SYNC_PROP(bone_ops, mBoneOps)
    SYNC_PROP(signal, mSignalMin) // NOTE: likely should be mSignal, but matches original
    SYNC_PROP(do_smoothing, mDoSmoothing)
    SYNC_PROP(smooth_increment, mSmoothIncrement)
    SYNC_PROP(signal_min, mSignalMin) // duplicates "signal" above - possible original bug
    SYNC_PROP(signal_max, mSignalMax)
    SYNC_SUPERCLASS(CharWeightable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharSignalApplier)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(CharWeightable)
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

BEGIN_LOADS(CharSignalApplier)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(CharWeightable)
    bs >> mDoSmoothing;
    bs >> mSmoothIncrement;
    bs >> mSignalMin;
    bs >> mSignalMax;
END_LOADS

void CharSignalApplier::Poll() {}

void CharSignalApplier::PollDeps(std::list<Hmx::Object *> &a, std::list<Hmx::Object *> &b) {
    for (size_t i = 0; i < mBoneOps.size(); i++) {
        for (std::list<Hmx::Object *>::iterator it = a.begin(); it != a.end(); ++it) {
            b.push_back(*it);
        }
    }
}
