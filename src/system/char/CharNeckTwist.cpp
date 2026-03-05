#include "char/CharNeckTwist.h"
#include "math/Rot.h"
#include "math/Trig.h"
#include "obj/Object.h"

CharNeckTwist::CharNeckTwist() : mTwist(this), mHead(this) {}

BEGIN_HANDLERS(CharNeckTwist)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(CharNeckTwist)
    SYNC_PROP(head, mHead)
    SYNC_PROP(twist, mTwist)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharNeckTwist)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mHead;
    bs << mTwist;
END_SAVES

BEGIN_COPYS(CharNeckTwist)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(CharNeckTwist)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mHead)
        COPY_MEMBER(mTwist)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(1, 0)

BEGIN_LOADS(CharNeckTwist)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mHead;
    d >> mTwist;
END_LOADS

void CharNeckTwist::Poll() {
    if (!mHead || !mTwist)
        return;
    // Get the Z-axis rotation angle of the head (yaw/twist around neck axis)
    float headAngle = GetZAngle(mHead->LocalXfm().m);
    // Apply half of the head's yaw to the neck twist bone
    float twist = headAngle * 0.5;
    // Build rotation matrix for twist around Z axis
    Hmx::Matrix3 rotMat;
    rotMat.x.Set(Cosine(twist), Sine(twist), 0);
    rotMat.y.Set(-Sine(twist), Cosine(twist), 0);
    rotMat.z.Set(0, 0, 1);
    Multiply(mTwist->LocalXfm().m, rotMat, mTwist->DirtyLocalXfm().m);
}

void CharNeckTwist::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    changedBy.push_back(mHead);
    change.push_back(mTwist);
}
