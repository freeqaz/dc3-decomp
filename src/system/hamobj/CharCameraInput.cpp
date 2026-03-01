#include "hamobj/CharCameraInput.h"
#include "char/Character.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/JointUtl.h"
#include "gesture/Skeleton.h"
#include "math/Mtx.h"
#include "os/Debug.h"
#include "rndobj/Trans.h"

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
    mCharFrame.mElapsedMs = 33;
    for (int i = 0; i < 6; i++) {
        if (i == 0) {
            mCharFrame.mSkeletonDatas[i].mTracking = kSkeletonTracked;
            mCharFrame.mSkeletonDatas[i].mQualityFlags = 0;
            for (int j = 0; j < kNumJoints; j++) {
                mCharFrame.mSkeletonDatas[i].mJointTrackingState[j] = kSkeletonTracked;
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
    // Set the natal transform to the character's current world transform
    // This establishes the origin point for skeleton tracking
    if (mChar) {
        mNatalXfm = mChar->WorldXfm();
    } else {
        mNatalXfm.Reset();
    }
}

const SkeletonFrame *CharCameraInput::PollNewFrame() {
    if (!mChar)
        return nullptr;

    SkeletonData &skelData = mCharFrame.mSkeletonDatas[0];

    // Update joint positions from character bone transforms
    for (int i = 0; i < kNumJoints; i++) {
        RndTransformable *bone = mBoneNames[i];
        if (bone) {
            // Convert world position to Kinect-space coordinates
            // Kinect uses meters, Milo uses cm-scale units
            Vector3 worldPos = bone->WorldXfm().v;
            // Scale from game units to Kinect meter space (1/kDrawScale)
            skelData.mJointPositions[i].Set(
                worldPos.x / kDrawScale,
                worldPos.y / kDrawScale,
                worldPos.z / kDrawScale
            );
            skelData.mRawPositions[i] = skelData.mJointPositions[i];
        }
    }

    // Set hip center from average of left/right hip bones
    // Hip center is index 0 in Kinect joints
    skelData.mHipCenter = skelData.mJointPositions[0];

    mCharFrame.mFrameNumber++;
    return &mCharFrame;
}
