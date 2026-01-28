#include "rndobj/Wind.h"

#include "obj/Object.h"
#include "utl/BinStream.h"

float gUnitsPerMeter = 39.370079f;

RndWind::RndWind()
    : mPrevailing(0.0f, 0.0f, 0.0f), mRandom(0.0f, 0.0f, 0.0f), mTimeLoop(100.0f),
      mSpaceLoop(100.0f), mTrans(this), mWindOwner(this, this) {
    SyncLoops();
}

RndWind::~RndWind() {}

BEGIN_HANDLERS(RndWind)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndWind)
    SYNC_PROP(prevailing, mPrevailing)
    SYNC_PROP(random, mRandom)
    SYNC_PROP(max_speed, mMaxSpeed)
    SYNC_PROP(min_speed, mMinSpeed)
    SYNC_PROP_SET(wind_owner, mWindOwner.Ptr(), SetWindOwner(_val.Obj<RndWind>()))
    SYNC_PROP_MODIFY(time_loop, mTimeLoop, SyncLoops())
    SYNC_PROP_MODIFY(space_loop, mSpaceLoop, SyncLoops())
    SYNC_PROP(trans, mTrans)
    SYNC_PROP(about_z, mAboutZ)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndWind)
    SAVE_REVS(4, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mPrevailing;
    bs << mRandom;
    bs << mTimeLoop;
    bs << mSpaceLoop;
    bs << mWindOwner;
    bs << mTrans;
    bs << mAboutZ;
    bs << mMinSpeed;
    bs << mMaxSpeed;
END_SAVES

BEGIN_COPYS(RndWind)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(RndWind)
    BEGIN_COPYING_MEMBERS
        if (ty == kCopyShallow)
            mWindOwner = c->mWindOwner;
        else {
            mWindOwner = this;
            COPY_MEMBER(mWindOwner)
            COPY_MEMBER(mPrevailing)
            COPY_MEMBER(mRandom)
            COPY_MEMBER(mTimeLoop)
            COPY_MEMBER(mSpaceLoop)
            COPY_MEMBER(mTrans)
            COPY_MEMBER(mAboutZ)
            COPY_MEMBER(mMinSpeed)
            COPY_MEMBER(mMaxSpeed)
            SyncLoops();
        }
    END_COPYING_MEMBERS
END_COPYS

void RndWind::SyncLoops() {
    float f1 = (mTimeLoop == 0.0f) ? 0.0f : 1.0f / mTimeLoop;
    mTimeRate.Set(f1, f1 * 0.773437f, f1 * 1.38484f);
    f1 = (mSpaceLoop == 0.0f) ? 0.0f : 1.0f / mSpaceLoop;
    mSpaceRate.Set(f1, f1 * 0.773437f, f1 * 1.38484f);
}

void RndWind::SetWindOwner(RndWind *wind) { mWindOwner = wind ? wind : this; }
