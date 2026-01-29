#include "gesture/ArcDetector.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/SkeletonViz.h"
#include "os/Debug.h"
#include "utl/Std.h"

float ArcDetector::_swipeRetentionFactor = 0.5;
float ArcDetector::_acceptablePathErrorRatio = 0.89999998;
int sDefaultHoverTimer = 600;

ArcDetector::ArcDetector()
    : unk18(0, 0, 0), unk28(0), unk2c(0), unk30(0.15f), unk34(0), unk35(0),
      unk3c(sDefaultHoverTimer) {
    Clear();
}

ArcDetector::~ArcDetector() {}

void ArcDetector::ResetHoverTimer() { unk3c = sDefaultHoverTimer; }

void ArcDetector::Initialize(
    SkeletonSide side, SkeletonJoint j1, SkeletonJoint j2, float f4
) {
    unk30 = f4;
    mSide = side;
    unk34 = true;
    unk8 = j1;
    unkc = j2;
}

Vector3 ArcDetector::GetCurveStart() const {
    MILO_ASSERT(!mJointPath.empty(), 0xE9);
    return Vector3((0 - (int)mSide) * unk28, unk2c, 0.0f);
}

void ArcDetector::Clear() {
    unk38 = 0;
    mJointPath.clear();
    unk2c = 0;
    unk28 = 0;
}

void ArcDetector::PrintJointPath() const {
    MILO_LOG("*** Hand path:\n");
    FOREACH (it, mJointPath) {
        MILO_LOG("%f, %f, %f,\n", it->x, it->y, it->z);
    }
    MILO_LOG("GetPathLength() %f\n", GetPathLength());
    MILO_LOG(
        "pow(GetPathLength(), _swipeRetentionFactor + 1) %f\n",
        pow(GetPathLength(), _swipeRetentionFactor + 1)
    );
    MILO_LOG("GetPathError() %f\n", GetPathError());
    MILO_LOG(
        "GetPathError() / _acceptablePathErrorRatio %f\n",
        GetPathError() / _acceptablePathErrorRatio
    );
    MILO_LOG("GetSwipeAmount() %f\n", GetSwipeAmount());
}

void ArcDetector::SwipeFailed(const Skeleton &skeleton) {
    if (unk38 > 0.5)
        unk35 = true;
    Vector3 vec = mJointPath.front();
    Clear();
    TryToStartSwipe(vec, skeleton);
}

void ArcDetector::CullPath() {
    if (!mJointPath.empty()) {
        std::list<Vector3> other;
        float first = mJointPath.front().x;
        FOREACH (it, mJointPath) {
            const Vector3 &cur = *it;
            if (mSide == kSkeletonLeft && cur.x >= first) {
                other.push_back(cur);
            }
            if (mSide == kSkeletonRight && cur.x <= first) {
                other.push_back(cur);
            }
        }
        mJointPath = other;
    }
}

void ArcDetector::Draw(const Skeleton &skeleton, SkeletonViz &viz) {
    std::list<Vector3>::iterator it = mJointPath.begin();
    std::list<Vector3>::iterator end_it = mJointPath.end();
    unsigned int count = 0;
    if (it != end_it) {
        do {
            it++;
            count++;
        } while (it != end_it);

        if (count != 0U) {
            std::list<Vector3> arcPath;
            float f31 = 0.0f;
            float f29 = 2.0f;
            float f30 = 0.031415927f;
            int i = 0;

            do {
                float f13 = unk28;
                double temp_d = (double)i;
                float f0 = (float)temp_d * f13 * f30;
                float f12 = f13 * f0;
                float comp = f12 * f29 - (f0 * f0);

                if (!(comp > f31)) {
                    f13 = f31;
                } else {
                    f13 = sqrtf(comp);
                }

                f13 = -f13;
                int sign = ((-(0 - mSide)) & 2) - 1;
                Vector3 vec(f0 * (float)sign, f31, f13);
                arcPath.insert(arcPath.end(), vec);

                i++;
            } while (i < 100);

            const TrackedJoint *joints = skeleton.TrackedJoints();
            const Vector3 &jpos = joints[unk8].mJointPos[0];
            Vector3 pos(
                jpos.x + unk18.x,
                jpos.z + unk18.z,
                jpos.y + unk18.y
            );

            std::list<Vector3> path1(arcPath);
            DrawPath(path1, viz, Hmx::Color(0.0f, 1.0f, 1.0f, 1.0f), pos);
            arcPath.clear();

            const Vector3 &jpos2 = joints[unkc].mJointPos[0];
            std::list<Vector3> path2(mJointPath);
            DrawPath(path2, viz, Hmx::Color(1.0f, 0.0f, 1.0f, 1.0f), jpos2);
        }
    }
}

void ArcDetector::DrawPath(
    const std::list<Vector3> &path, SkeletonViz &viz, Hmx::Color color, const Vector3 &offset
) const {
    for (std::list<Vector3>::const_iterator it = path.begin(); it != path.end(); ++it) {
        viz.DrawPoint3D(Vector3(it->x + offset.x, it->y + offset.y, it->z + offset.z), 1.0f, color, 1.0f);
    }
}
