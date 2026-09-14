#include "hamobj\HamRegulate.h"
#include "char\CharIKFoot.h"
#include "char\CharServoBone.h"
#include "char\Character.h"
#include "char\Waypoint.h"
#include "math\Rot.h"
#include "math\Utl.h"
#include "math\Vec.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\System.h"
#include "rndobj\Poll.h"
#include "utl/Loader.h"

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

void HamRegulate::Regulate(Vector3 &posDelta, float &rotDelta) {
    float radius = mArriveRadius;
    if (mArriveRadius < 0.0f) {
        if (mLeftFoot) {
            radius = mLeftFoot->mData->LocalXfm().v.z;
        } else {
            radius = 0.0f;
        }
    }
    radius = Max(radius, 0.01f);
    float invRadius = 1.0f / radius;

    float absDt = Max(0.0f, TheTaskMgr.DeltaBeat());
    Character *character = mCharacter;

    auto& waypoint = mWaypoint;
    // The image hoists &character->LocalXfm() into a callee-saved register
    // (`addi r28, r3, 0xf4`) before the mRegulateMode branch and never keeps
    // `character` itself alive past the load -- both arms use the transform.
    const Transform &charXfm = character->LocalXfm();
    // RESIDUAL (w7-ab, 89.2 canonical).  Two rows remain, both measured:
    //  1) We emit an extra anchor `addi r30, r29, 0x14` for the `waypoint`
    //     alias, so mWaypoint is reached as 0xc(r30) where the image uses
    //     0x20(this) -- one extra callee-saved GPR.  Dropping the alias is
    //     WORSE both with the charXfm hoist (89.2 -> 88.2) and without it
    //     (86.5 -> 83.2), so the alias is not the defect it looks like.
    //  2) The image evaluates the three components z, y, x in BOTH arms; we
    //     evaluate x, z, y from the same source order.  Pure scheduling: both
    //     sides store posDelta.x last, only the subtraction order differs.
    if (mRegulateMode == 1) {
        float dy, dz;
        if (character->Teleported()) {
            const Transform &wpXfm = waypoint->WorldXfm();
            dz = wpXfm.v.z - charXfm.v.z;
            dy = wpXfm.v.y - charXfm.v.y;
            posDelta.x = wpXfm.v.x - charXfm.v.x;
        } else {
            const Transform &wpXfm = waypoint->WorldXfm();
            dz = wpXfm.v.z - mPosDelta.z;
            dy = wpXfm.v.y - mPosDelta.y;
            float dx = wpXfm.v.x - mPosDelta.x;
            posDelta.x = dx;
            float factor = Min(absDt * invRadius * 1.1f, 1.0f);
            posDelta.x = dx * factor;
            dy = dy * factor;
            dz = dz * factor;
        }
        posDelta.y = dy;
        posDelta.z = dz;
    } else {
        Transform facing;
        facing.Reset();
        CharServoBone *servo = character->BoneServo();
        servo->MoveToFacing(facing);
        FastInvert(facing, facing);
        Multiply(facing, charXfm, facing);

        rotDelta = -(waypoint->LocalXfm().m.x.x * facing.m.x.y
                    - waypoint->LocalXfm().m.x.y * facing.m.x.x);

        float posFactor = Min(1.0f, Max(0.0f, absDt * invRadius));

        posDelta.x = waypoint->LocalXfm().v.x - facing.v.x;
        posDelta.z = waypoint->LocalXfm().v.z - facing.v.z;
        posDelta.y = waypoint->LocalXfm().v.y - facing.v.y;

        rotDelta *= posFactor;
        posDelta.x *= posFactor;
        posDelta.y *= posFactor;
        posDelta.z *= posFactor;
    }
}

void HamRegulate::Poll() {
    if (!mWaypoint || !mCharacter) return;
    CharServoBone *servo = mCharacter->BoneServo();
    if (!servo) return;

    Vector3 posDelta(0, 0, 0);
    float rotDelta = 0.0f;
    Regulate(posDelta, rotDelta);

    float moveZ = 0.0f;
    float dt = TheTaskMgr.DeltaSeconds();
    float moveX = posDelta.x;
    float moveY = posDelta.y;
    int footState = 0;
    float absDt = Max(0.0f, dt);
    float moveRot;
    Character *character = mCharacter;

    if (!mCharacter->Teleported()) {
        float maxMove = mMaxSpeed * absDt;
        float posMag = sqrtf(moveX * moveX + moveY * moveY);
        if (posMag > 0.0f && posMag > maxMove) {
            float scale = maxMove / posMag;
            moveX = moveX * scale;
            moveY = moveY * scale;
            moveZ = scale * moveZ;
            moveRot = scale * rotDelta;
        } else {
            moveRot = rotDelta;
        }

        if (mLeftFoot && mRightFoot) {
            int leftState = (mLeftFoot->mFootFsmState == 1) ? 1 : 0;
            int rightState = (mRightFoot->mFootFsmState == 1) ? 2 : 0;
            footState = leftState | rightState;

            if (footState == 3) {
                mAccumVelocity.Zero();
                moveZ = 0.0f;
                moveY = 0.0f;
                moveX = 0.0f;
                moveRot = 0.0f;
            } else {
                if ((mFootState ^ footState) & footState) {
                    mAccumVelocity.Zero();
                }
                float accumX = mAccumVelocity.x + moveX;
                float accumY = mAccumVelocity.y + moveY;
                float accumZ = mAccumVelocity.z + moveZ;
                posDelta.Set(accumX, accumY, accumZ);
                if (accumY * accumY + accumZ * accumZ + accumX * accumX > 16.0f) {
                    moveZ = 0.0f;
                    moveY = 0.0f;
                    moveX = 0.0f;
                    moveRot = 0.0f;
                }
                mAccumVelocity = posDelta;
            }
        }
    } else {
        moveRot = rotDelta;
    }

    mFootState = footState;

    Transform &xfm = character->DirtyLocalXfm();

    auto teleported = mCharacter->Teleported();
    if (!TheLoadMgr.EditMode() || teleported || absDt != 0.0f) {
        RotateAboutZ(xfm.m, moveRot, xfm.m);
        xfm.v.x += moveX;
        xfm.v.y += moveY;
        xfm.v.z += moveZ;
        mWaypoint->Constrain(xfm);
    }
}
