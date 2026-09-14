#include "hamobj\CharCameraInput.h"
#include "char\Character.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\JointUtl.h"
#include "gesture\Skeleton.h"
#include "math\Mtx.h"
#include "os\Debug.h"
#include "rndobj\Trans.h"

const float CharCameraInput::kDrawScale = 39.370079f;

CharCameraInput::CharCameraInput(Character *c) : mChar(c), unk2430(0) {
    MILO_ASSERT(mChar, 0x18);
    for (int i = 0; i < kNumJoints; i++) {
        const char *name = CharBoneName((SkeletonJoint)i);
        mBoneNames[i] = mChar->Find<RndTransformable>(name, false);
        if (!mBoneNames[i]) {
            MILO_NOTIFY("Could not find %s", name);
        }
    }
    memset(&mCharFrame, 0, sizeof(SkeletonFrame));
    mCharFrame.mFloorNormal.Set(0, 1, 0);
    mCharFrame.mFloorClipPlane.Set(0, 0, 0, 0);
    mCharFrame.mFrameNumber = 0;
    mCharFrame.mElapsedMs = 33;
    for (int i = 0; i < 6; i++) {
        SkeletonData &data = mCharFrame.mSkeletonDatas[i];
        if (i == 0) {
            data.mTracking = kSkeletonTracked;
            data.mQualityFlags = 0;
            for (int j = 0; j < kNumJoints; j++) {
                data.mJointTrackingState[j] = kSkeletonTracked;
            }
        }
    }
    ResetSkeletonCharOrigin();
}

bool CharCameraInput::NatalToWorld(Transform &world) const {
    world = mNatalXfm;
    return true;
}

void CharCameraInput::ResetSkeletonCharOrigin() {
    // Residual 95.55% canonical / 95.05% raw, 13 rows / 8 B, two clusters:
    //
    // Rows 5-12 (6 rows): the image emits two DEAD sub-object addresses right
    // after `addi r11, r3, 0x23f0` -- `addi r10, r11, 0x10` (m.y) and
    // `addi r11, r11, 0x20` (m.z) -- and then never reads r10, clobbering r11
    // at the next instruction.  Every Reset() store below uses a full r3
    // displacement on both sides, so this is dead address material inside the
    // INLINED Transform::Reset, i.e. it belongs to math/Mtx.h, not here.
    // REFUTED: binding `Transform &xfm = mNatalXfm;` and using it throughout
    // does produce the two addi's -- but MSVC then parks the transform base in
    // a CALLEE-SAVED register (r30) across the two DrawScale() calls, adds a
    // second std/ld pair to the prologue and epilogue, and the function drops
    // 95.5% -> 90.0% (28 rows).  The image keeps nothing live across those
    // calls; it re-derives everything off r31 = this.
    //
    // Rows 77-85 (7 rows): the final `mNatalXfm.v = worldPos` 4-word copy.
    // Both sides load the same four words from 0x50(r1) and store the same
    // four words to 0x2420(r31); only the interleave differs -- the image
    // loads z,w,x then y and stores z,w,x,y, we load w,x,z, store z, then load
    // y and store x,w,y.  Pure scheduling of one copy; no source order
    // expresses it (the two `worldPos` adjustments must stay in this order,
    // they are two separate virtual DrawScale() calls at 62 and 72).
    mNatalXfm.Reset();
    float s = DrawScale();
    mNatalXfm.m.x.Set(-s, 0.0f, 0.0f);
    mNatalXfm.m.y.Set(0.0f, 0.0f, s);
    mNatalXfm.m.z.Set(0.0f, -s, 0.0f);
    Vector3 worldPos = mChar->WorldXfm().v;
    worldPos.y += DrawScale() * 2.0f;
    worldPos.z += DrawScale();
    mNatalXfm.v = worldPos;
}

const SkeletonFrame *CharCameraInput::PollNewFrame() {
    MILO_ASSERT(mChar, 0x48);

    Transform invXfm;
    Invert(mNatalXfm, invXfm);

    SkeletonData &skelData = mCharFrame.mSkeletonDatas[0];

    for (int i = 0; i < kNumJoints; i++) {
        SkeletonJoint mj = BaseSkeleton::MirrorJoint((SkeletonJoint)i);
        RndTransformable *bone = mBoneNames[i];
        if (bone) {
            Multiply(bone->WorldXfm().v, invXfm, skelData.mJointPositions[mj]);
        } else {
            skelData.mJointPositions[mj].z = 0.0f;
            skelData.mJointPositions[mj].y = 0.0f;
            skelData.mJointPositions[mj].x = 0.0f;
        }
        if (unk2430) {
            skelData.mJointPositions[mj].x *= -1.0f;
            skelData.mJointPositions[mj].z *= -1.0f;
        }
        skelData.mRawPositions[mj] = skelData.mJointPositions[mj];
    }

    return &mCharFrame;
}
