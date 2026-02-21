#include "hamobj/Pose.h"
#include "utl/Std.h"

Pose::Pose(int x, ScoreMode s) : unk18(x), mScoreMode(s) {}
Pose::~Pose() { DeleteAll(mElements); }

void Pose::AddElement(PoseElement *e) { mElements.push_back(e); }

JointDistPoseElement::JointDistPoseElement(
    SkeletonJoint j1, SkeletonJoint j2, float minDist, float maxDist
)
    : unk4(1.0f), mJoint1(j1), mJoint2(j2), mMinDist(minDist), mMaxDist(maxDist), mCoordSys(0) {
    MILO_ASSERT(minDist <= maxDist, 0x2f);
}

float JointDistPoseElement::Score(const Skeleton &skeleton) const {
    Vector3 pos1, pos2;
    skeleton.JointPos((SkeletonCoordSys)mCoordSys, mJoint1, pos1);
    skeleton.JointPos((SkeletonCoordSys)mCoordSys, mJoint2, pos2);

    float dy = pos1.y - pos2.y;
    float dx = pos1.x - pos2.x;
    float dz = pos1.z - pos2.z;

    float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (!(dist < mMinDist) && !(dist > mMaxDist)) {
        return 1.0f;
    }
    return 0.0f;
}

BoneAngleRangePoseElement::BoneAngleRangePoseElement(
    SkeletonBone bone, const Vector3 &v, float f1, float f2
)
    : unk4(f2), mBone(bone), unk1c(f1) {
    Normalize(v, mAngle);
}
