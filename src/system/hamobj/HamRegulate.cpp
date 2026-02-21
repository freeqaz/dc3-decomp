#include "hamobj/HamRegulate.h"
#include "char/Character.h"
#include "char/Waypoint.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "rndobj/Poll.h"

const float kConstFloats[2] = { 4, 4 };

HamRegulate::HamRegulate()
    : mWaypoint(this), mRegulateMode(0), mArriveRadius(0), mPosDelta(0, 0, 0), mAccumVelocity(0, 0, 0), mFootState(0),
      mMaxSpeed(kConstFloats[0]), mLeftFoot(this), mRightFoot(this) {}

HamRegulate::~HamRegulate() {}

BEGIN_HANDLERS(HamRegulate)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamRegulate)
    SYNC_PROP(left_foot, mLeftFoot)
    SYNC_PROP(right_foot, mRightFoot)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HamRegulate)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mLeftFoot;
    bs << mRightFoot;
END_SAVES

INIT_REVS(2, 0)

BEGIN_LOADS(HamRegulate)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.rev > 1) {
        bs >> mLeftFoot;
        bs >> mRightFoot;
    }
END_LOADS

BEGIN_COPYS(HamRegulate)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(HamRegulate)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mLeftFoot)
        COPY_MEMBER(mRightFoot)
    END_COPYING_MEMBERS
END_COPYS

void HamRegulate::SetName(const char *name, ObjectDir *dir) {
    Hmx::Object::SetName(name, dir);
    mCharacter = dynamic_cast<Character *>(Dir());
}

void HamRegulate::Enter() {
    RegulateWay(nullptr, 0);
    mAccumVelocity.Zero();
    mFootState = 0;
}

void HamRegulate::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    changedBy.push_back(mCharacter->BoneServo());
    change.push_back(mCharacter);
}

void HamRegulate::RegulateWay(Waypoint *w, float f) {
    mWaypoint = w;
    mArriveRadius = f;
    mPosDelta.Zero();
    mRegulateMode = 0;
}
