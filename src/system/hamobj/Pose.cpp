#include "hamobj/Pose.h"
#include "utl/Std.h"

Pose::Pose(int x, ScoreMode s) : unk18(x), mScoreMode(s) {}
Pose::~Pose() { DeleteAll(mElements); }

void Pose::AddElement(PoseElement *e) { mElements.push_back(e); }

JointDistPoseElement::JointDistPoseElement(
    SkeletonJoint j1, SkeletonJoint j2, float minDist, float maxDist
)
    : unk8(j1), unkc(j2), unk10(minDist), unk14(maxDist) {}

float JointDistPoseElement::Score(const Skeleton &skeleton) const {
    Vector3 pos1, pos2;
    skeleton.JointPos((SkeletonCoordSys)unk18, unk8, pos1);
    skeleton.JointPos((SkeletonCoordSys)unk18, unkc, pos2);

    float dx = pos1.x - pos2.x;
    float dy = pos1.y - pos2.y;
    float dz = pos1.z - pos2.z;

    float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (!(dist < unk10) && !(dist > unk14)) {
        return 1.0f;
    }
    return 0.0f;
}

BoneAngleRangePoseElement::BoneAngleRangePoseElement(
    SkeletonBone bone, const Vector3 &v, float f1, float f2
)
    : unk4(f2), unk8(bone), unk1c(f1) {
    Normalize(v, mAngle);
}
