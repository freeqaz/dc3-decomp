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
    // Lane history (10 lines moved): docs/decomp/notes/system/hamobj/CharCameraInput.md
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
            // Vector3::Zero (z, y, x stores) rather than three field writes:
            // the &mJointPositions[mj] candidate then ranks above the row base
            // (c2 pri 51 vs 45; was 41 vs 55) and takes r31, as in the image.
            skelData.mJointPositions[mj].Zero();
        }
        if (unk2430) {
            skelData.mJointPositions[mj].x *= -1.0f;
            skelData.mJointPositions[mj].z *= -1.0f;
        }
        skelData.mRawPositions[mj] = skelData.mJointPositions[mj];
    }

    return &mCharFrame;
}
