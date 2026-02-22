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
    memset(&unk11d8, 0, sizeof(SkeletonFrame));
    unk11d8.mFloorNormal.Set(0, 1, 0);
    unk11d8.mFloorClipPlane.Set(0, 0, 0, 0);
    unk11d8.mElapsedMs = 33;
    for (int i = 0; i < 6; i++) {
        // skeleton frame edits
    }
    ResetSkeletonCharOrigin();
}

bool CharCameraInput::NatalToWorld(Transform &world) const {
    world = mNatalXfm;
    return true;
}
