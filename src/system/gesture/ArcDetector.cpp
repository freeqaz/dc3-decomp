#include "gesture/ArcDetector.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/SkeletonViz.h"
#include "os/Debug.h"
#include "utl/Std.h"

float ArcDetector::_swipeRetentionFactor = 0.5;
float ArcDetector::_acceptablePathErrorRatio = 0.89999998;
int sDefaultHoverTimer = 600;

ArcDetector::ArcDetector()
    : mArcOffset(0, 0, 0), mSwipeExtentX(0), mSwipeExtentY(0), mSwipeThreshold(0.15f), mInitialized(0), mHadProgress(0),
      mHoverTimer(sDefaultHoverTimer) {
    Clear();
}

ArcDetector::~ArcDetector() {}

void ArcDetector::ResetHoverTimer() { mHoverTimer = sDefaultHoverTimer; }

void ArcDetector::Initialize(
    SkeletonSide side, SkeletonJoint j1, SkeletonJoint j2, float f4
) {
    mSwipeThreshold = f4;
    mSide = side;
    mInitialized = true;
    mPrimaryJoint = j1;
    mSecondaryJoint = j2;
}

Vector3 ArcDetector::GetCurveStart() const {
    MILO_ASSERT(!mJointPath.empty(), 0xE9);
    return Vector3((mSide == kSkeletonLeft ? 1 : -1) * mSwipeExtentX, mSwipeExtentY, 0.0f);
}

void ArcDetector::Clear() {
    mCurrentSwipeAmt = 0;
    mJointPath.clear();
    mSwipeExtentY = 0;
    mSwipeExtentX = 0;
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
    if (mCurrentSwipeAmt > 0.5)
        mHadProgress = true;
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
    unsigned int count = mJointPath.size();
    if (count != 0U) {
        std::list<Vector3> arcPath;
        float f31 = 0.0f;
        float f29 = 2.0f;
        float f30 = 0.031415927f;
        int i = 0;

        do {
            float f13 = mSwipeExtentX;
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
        const Vector3 &jpos = joints[mPrimaryJoint].mJointPos[0];
        Vector3 pos(
            jpos.x + mArcOffset.x,
            jpos.z + mArcOffset.z,
            jpos.y + mArcOffset.y
        );

        std::list<Vector3> path1(arcPath);
        DrawPath(path1, viz, Hmx::Color(0.0f, 1.0f, 1.0f, 1.0f), pos);
        arcPath.clear();

        const Vector3 &jpos2 = joints[mSecondaryJoint].mJointPos[0];
        std::list<Vector3> path2(mJointPath);
        DrawPath(path2, viz, Hmx::Color(1.0f, 0.0f, 1.0f, 1.0f), jpos2);
    }
}

void ArcDetector::DrawPath(
    const std::list<Vector3> &path, SkeletonViz &viz, Hmx::Color color, const Vector3 &offset
) const {
    for (std::list<Vector3>::const_iterator it = path.begin(); it != path.end(); ++it) {
        viz.DrawPoint3D(Vector3(it->x + offset.x, it->y + offset.y, it->z + offset.z), 1.0f, color, 1.0f);
    }
}
