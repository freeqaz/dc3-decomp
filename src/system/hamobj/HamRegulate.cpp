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
    float radius;
    if (mArriveRadius < 0.0f) {
        if (mLeftFoot) {
            radius = mLeftFoot->mData->LocalXfm().v.z;
        } else {
            radius = 0.0f;
        }
    } else {
        radius = mArriveRadius;
    }
    float invRadius = 1.0f / Max(radius, 0.01f);

    float absDt = Max(0.0f, TheTaskMgr.DeltaBeat());
    Character *character = mCharacter;

    // The image hoists &character->LocalXfm() into a callee-saved register
    // (`addi r28, r3, 0xf4`) before the mRegulateMode branch and never keeps
    // `character` itself alive past the load -- both arms use the transform.
    const Transform &charXfm = character->LocalXfm();
    // mWaypoint is an ObjPtr; the image reads its pointer at 0x20(this) three
    // times (824C5220, 824C533C, and 824C5374 -- a reload after the store
    // through the float &rotDelta), so there is no reference to the ObjPtr
    // here, and every arm loads all three components before storing any:
    // the Set()-shaped Subtract() from math/Vec.h.
    //
    // w21-b: CLOSED (94.23 -> 100, 150/150 equal).  The w7-bv residual (the
    // else arm's load/store interleave after Multiply) was the rotDelta
    // statement reading mWaypoint->LocalXfm() twice inline; binding it ONCE to
    // a `const Transform &` (one mWaypoint load, as the image has at
    // 824C5338) gives the image's schedule.  Subtract() below still goes
    // through mWaypoint again, which is the image's reload after the store
    // through rotDelta.
    if (mRegulateMode == 1) {
        if (character->Teleported()) {
            const Transform &wpXfm = mWaypoint->WorldXfm();
            Subtract(wpXfm.v, charXfm.v, posDelta);
        } else {
            const Transform &wpXfm = mWaypoint->WorldXfm();
            Subtract(wpXfm.v, mPosDelta, posDelta);
            float factor = Min(1.0f, absDt * invRadius * 1.1f);
            Scale(posDelta, factor, posDelta);
        }
    } else {
        Transform facing;
        facing.Reset();
        CharServoBone *servo = character->BoneServo();
        servo->MoveToFacing(facing);
        FastInvert(facing, facing);
        Multiply(facing, charXfm, facing);

        float posFactor = Clamp(0.0f, 1.0f, absDt * invRadius);

        const Transform &wpXfm = mWaypoint->LocalXfm();
        rotDelta = -(wpXfm.m.x.x * facing.m.x.y - wpXfm.m.x.y * facing.m.x.x);

        Subtract(mWaypoint->LocalXfm().v, facing.v, posDelta);

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

    // REFUTED (w7-ag): hoisting moveX/moveY above the DeltaSeconds() call
    // (declaration order moveX, moveY, moveZ, dt) schedules both lfs BEFORE the
    // call instead of after it -- 98.8 -> 97.6.  The two remaining charged rows
    // are our extra `fmr f28, f0` / `fmr f29, f29` pair: the image loads
    // posDelta.x/.y straight into the callee-saved f29/f30
    // (build/373307D9/asm/system/hamobj/HamRegulate.s, `lfs f29, 0x60(r1)` /
    // `lfs f30, 0x64(r1)`) where we land them in scratch and copy.
    // w19-b (98.81, same 2 inserted fmr + the f28/f29/f30 renumbering they
    // cause): the copy is MSVC propagating the loaded temp into the sqrt and
    // the scale products, so temp and moveX interfere.  Inert: the sqrt over
    // posDelta.x/.y instead of moveX/moveY (identical bytes).  Worse: moveZ
    // declared after moveY (97.6, moves the z=0 fmr below the call).
    // w21-o (98.81, unchanged): behaviour re-read against the image (scale,
    // foot-state mask, accum y/z/x sum, EditMode/teleported/absDt gate all
    // match).  Reproduced the two `fmr f28/f29` copies in a standalone cl.exe
    // probe and tried 11 more shapes there -- all keep the load temp + copy:
    // sqrt over posDelta.x/.y, Teleported() test flipped, a Vector3 `move`
    // aggregate (with and without z zeroed first), declarations split from
    // the loads, DeltaSeconds() folded into Max, `scale * moveX` order,
    // scale products re-reading posDelta, moveRot defaulted before the
    // clamp.  The image loads straight into f29/f30; no spelling found.
    // w21-au: CLOSED (98.81 -> 100).  moveX/moveY are NOT copies of
    // posDelta.x/.y made up front: they are assigned in each arm (scaled,
    // unscaled, teleported) and the sqrt reads posDelta directly.  With the
    // up-front copy MSVC propagated the load temp into the sqrt and the scale
    // products, so temp and moveX interfered (the two fmr); assigned per arm,
    // the temp dies at the copy and coalesces into f29/f30.  Same values on
    // every path (x * scale order kept).
    float moveZ = 0.0f;
    float dt = TheTaskMgr.DeltaSeconds();
    float moveX;
    float moveY;
    int footState = 0;
    float absDt = Max(0.0f, dt);
    float moveRot;

    if (!mCharacter->Teleported()) {
        float maxMove = mMaxSpeed * absDt;
        float posMag = sqrtf(posDelta.x * posDelta.x + posDelta.y * posDelta.y);
        if (posMag > 0.0f && posMag > maxMove) {
            float scale = maxMove / posMag;
            moveX = posDelta.x * scale;
            moveY = posDelta.y * scale;
            moveZ = scale * moveZ;
            moveRot = scale * rotDelta;
        } else {
            moveX = posDelta.x;
            moveY = posDelta.y;
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
        moveX = posDelta.x;
        moveY = posDelta.y;
        moveRot = rotDelta;
    }

    mFootState = footState;

    Transform &xfm = mCharacter->DirtyLocalXfm();

    auto teleported = mCharacter->Teleported();
    if (!TheLoadMgr.EditMode() || teleported || absDt != 0.0f) {
        RotateAboutZ(xfm.m, moveRot, xfm.m);
        xfm.v += Vector3(moveX, moveY, moveZ);
        mWaypoint->Constrain(xfm);
    }
}
