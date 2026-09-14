#include "hamobj\Pose.h"
#include "math\Utl.h"
#include "os\Debug.h"
#include "utl\Std.h"

Pose::Pose(int x, ScoreMode s) : unk18(x), mScoreMode(s) {}
Pose::~Pose() { DeleteAll(mElements); }

void Pose::AddElement(PoseElement *e) { mElements.push_back(e); }

void Pose::Update(const Skeleton &skeleton) {
    MILO_ASSERT(!mElements.empty(), 0x8a);
    float weightedSum = 0.0f;
    float totalWeight = 0.0f;
    for (std::vector<PoseElement *>::iterator it = mElements.begin(); it != mElements.end();
         ++it) {
        PoseElement *elem = *it;
        weightedSum += elem->Score(skeleton) * elem->unk4;
        totalWeight += elem->unk4;
    }
    MILO_ASSERT(totalWeight != 0.0f, 0x95);
    float score = weightedSum / totalWeight;
    unk10.push_back(score);
    unsigned int count = 0;
    for (std::list<float>::iterator it = unk10.begin(); it != unk10.end(); ++it) {
        count++;
    }
    if (count > (unsigned int)unk18) {
        unk10.erase(unk10.begin());
    }
}

float Pose::CurrentScore() const {
    float minVal = 1.0f;
    float sum = 0.0f;
    // RESIDUAL (w7-al, 87.5 canonical): the image enters this loop with a bare
    // `b` to the bottom test (0x...  `b 0x9dc`), i.e. unrotated; MSVC rotates it
    // for us and pays a guard -- `mr r9, r10` / `cmplw cr6, r10, r11` / `beq` --
    // which is the whole insert cluster, and the copy into r9 is what repaints
    // the FPR/GPR ranking below.  Byte-inert here: writing the loop as a `for`
    // with an empty increment clause, and hoisting the iterator's declaration
    // above minVal/sum.
    std::list<float>::const_iterator it = unk10.begin();
    while (it != unk10.end()) {
        float val = *it;
        ++it;
        sum += val;
        // Min<float> from math/Utl.h, whose fsel specialisation is exactly the
        // image's `fsubs f11, f0, f12` / `fsel f0, f11, f12, f0`.  Spelled out
        // as `minVal - val < 0.0 ? minVal : val` the `0.0` is a DOUBLE literal,
        // which blocks the fsel and leaves a real `fcmpu`/`blt`/`fmr`.
        minVal = Min(minVal, val);
    }
    switch (mScoreMode) {
    case (ScoreMode)0:
        return sum / (float)unk18;
    case (ScoreMode)1: {
        // The image's second count loop reloads begin() through the END pointer
        // (`lwz r10, 0x0(r11)` where r11 == &unk10 == end()), i.e. the list's own
        // `this`, not `0x10(r3)`.  That is `size()`'s inlined `distance(begin(),
        // end())`, not a hand-written loop over `unk10.begin()`; spelling it by
        // hand lets MSVC CSE the two begin() loads and costs the extra `mr`.
        unsigned int count = unk10.size();
        // ONE return, not an early `return 0.0f`: the image's short-count arm is
        // `fmr f0, f31` (minVal = 0.0f) falling into the shared `fmr f1, f0`
        // / `b` epilogue at 0x82528EE0-0x82528EE4.
        if (count < (unsigned int)unk18) {
            minVal = 0.0f;
        }
        return minVal;
    }
    default:
        MILO_FAIL("Bad Pose ScoreMode!");
        return 0.0f;
    }
}

JointDistPoseElement::JointDistPoseElement(
    SkeletonJoint j1, SkeletonJoint j2, float minDist, float maxDist
)
    : PoseElement(1.0f), mJoint1(j1), mJoint2(j2), mMinDist(minDist), mMaxDist(maxDist), mCoordSys(0) {
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

float CamDistancePoseElement::Score(const Skeleton &skeleton) const {
    Vector3 pos;
    skeleton.JointPos(kCoordCamera, kJointSpine, pos);
    if (pos.z > unk8)
        return 1.0f;
    return 0.0f;
}

float BoneAngleRangePoseElement::Score(const Skeleton &skeleton) const {
    Vector3 boneDir;
    skeleton.BoneVec(mBone, kCoordCamera, boneDir);
    Normalize(boneDir, boneDir);
    const Vector3& angle = mAngle;
    // The assert literal in the shipped image is, byte for byte,
    //   "(1.0f)-(0.001f) <= (Length(mAngle)) && (Length(mAngle)) <= (1.0f)+(0.001f)"
    // (?? _C@_0EL@EIPCKLDA@ at 0x8205d490).  The parenthesisation is the shape a
    // range-check macro leaves behind, and it names the member, not the local
    // alias -- so spell it that way here.
    MILO_ASSERT((1.0f)-(0.001f) <= (Length(mAngle)) && (Length(mAngle)) <= (1.0f)+(0.001f), 0x21);
    float dot = Dot(boneDir, angle);
    float acosAngle = acosf(dot);
    if (acosAngle <= unk1c)
        return 1.0f;
    return 0.0f;
}

BoneAngleRangePoseElement::BoneAngleRangePoseElement(
    SkeletonBone bone, const Vector3 &v, float f1, float f2
)
    : PoseElement(f2), mBone(bone), unk1c(f1) {
    Normalize(v, mAngle);
}
