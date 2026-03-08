#include "gesture/ArcDetector.h"
#include "gesture/BaseSkeleton.h"
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
    return Vector3((mSide) * mSwipeExtentX, mSwipeExtentY, 0);
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
