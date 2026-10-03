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
    // 100 (w21-g, was 95.55 canonical / 13 rows).  The image's two dead
    // `addi r10, r11, 0x10` / `addi r11, r11, 0x20` after `addi r11, r3,
    // 0x23f0` are &m.y and &m.z formed off a MATRIX reference: the three
    // Set() calls go through a local `Hmx::Matrix3 &m`, while Reset() is
    // still called on the member.  That one binding also fixes the final
    // 4-word `mNatalXfm.v = worldPos` copy schedule (rows 77-85, which the
    // earlier note filed as "pure scheduling, no source order expresses it").
    // Binding the whole Transform instead (`Transform &xfm`) parks the base
    // in r30 across the DrawScale() calls -- 90.0; `m.Identity(); v.Zero();`
    // spelled at the call site -- 94.4.
    Hmx::Matrix3 &m = mNatalXfm.m;
    mNatalXfm.Reset();
    float s = DrawScale();
    m.x.Set(-s, 0.0f, 0.0f);
    m.y.Set(0.0f, 0.0f, s);
    m.z.Set(0.0f, -s, 0.0f);
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
