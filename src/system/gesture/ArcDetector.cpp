#include "gesture\ArcDetector.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\GestureMgr.h"
#include "gesture\SkeletonViz.h"
#include "os\Debug.h"
#include "rndobj\Rnd.h"
#include "rndobj\Utl.h"
#include "utl\DebugMeter.h"
#include "utl\Std.h"

static int sDefaultHoverTimer = 600;
static float _acceptablePathErrorRatio = 0.89999998f;
static float _swipeRetentionFactor = 0.5f;
static float sZErrorScale = 2.0f;

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
    for (std::list<Vector3>::const_reverse_iterator it = mJointPath.rbegin();
         it != mJointPath.rend(); ++it) {
        const Vector3 &pos = *it;
        MILO_LOG("%f, %f, %f,\n", pos.x, pos.y, pos.z);
    }
    MILO_LOG("GetPathLength() %f\n", GetPathLength());
    MILO_LOG(
        "pow(GetPathLength(), _swipeRetentionFactor + 1) %f\n",
        (float)pow(GetPathLength(), _swipeRetentionFactor + 1.0f)
    );
    MILO_LOG("GetPathError() %f\n", GetPathError());
    MILO_LOG(
        "GetPathError() / _acceptablePathErrorRatio %f\n",
        GetPathError() / _acceptablePathErrorRatio
    );
    float threshold = mSwipeThreshold * 0.3f;
    MILO_LOG(
        "desiredLength %f\n",
        (float)mHoverTimer / (float)sDefaultHoverTimer * (mSwipeThreshold - threshold) + threshold
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
        {
            // arcPath is block-scoped in the image: there is exactly ONE
            // _List_base::clear (0x1c0, between the two DrawPath calls) and none
            // at the epilogue, so the call we had been spelling as an explicit
            // arcPath.clear() is really the destructor at end of scope.
            std::list<Vector3> arcPath;
            for (int i = 0; i < 100; i++) {
                float f0 = (float)i * mSwipeExtentX * 0.02f;
                int sign = (mSide != 0 ? 1 : -1);
                float comp = mSwipeExtentX * f0 * 2.0f - (f0 * f0);
                float arcY;
                if (comp <= 0.0f) {
                    arcY = 0.0f;
                } else {
                    arcY = sqrtf(comp);
                }
                arcY = -arcY;
                Vector3 vec(f0 * (float)sign, 0.0f, arcY);
                arcPath.insert(arcPath.begin(), vec);
            }

            const Vector3 &jpos =
                skeleton.TrackedJoints()[mSecondaryJoint].mJointPos[0];
            const Vector3 &off = mArcOffset;
            Vector3 pos(jpos.x + off.x, jpos.y + off.y, jpos.z + off.z);

            DrawPath(arcPath, viz, Hmx::Color(1.0f, 1.0f, 0.0f, 1.0f), pos);
        }

        DrawPath(
            mJointPath,
            viz,
            Hmx::Color(1.0f, 0.0f, 1.0f, 1.0f),
            skeleton.TrackedJoints()[mSecondaryJoint].mJointPos[0]
        );
    }
}

void ArcDetector::DrawPath(
    std::list<Vector3> path, SkeletonViz &viz, Hmx::Color color, const Vector3 &offset
) const {
    std::list<Vector3>::const_iterator it = path.begin();
    Vector3 prev = *it;
    prev.z = prev.z + offset.z;
    prev.y = prev.y + offset.y;
    prev.x = prev.x + offset.x;
    Vector3 cur;
    for (++it; it != path.end(); ++it) {
        cur = *it;
        cur.x = cur.x + offset.x;
        cur.y = cur.y + offset.y;
        cur.z = cur.z + offset.z;
        viz.DrawLine3D(prev, cur, 0.01f, color, NULL);
        prev = cur;
    }
}

float ArcDetector::GetPathLength() const {
    std::list<Vector3>::const_iterator it = mJointPath.begin();
    unsigned int count = 0;
    if (it != mJointPath.end()) {
        do {
            ++it;
            count++;
        } while (it != mJointPath.end());
    }
    if (count <= 1) {
        return 0.0f;
    }
    it = mJointPath.begin();
    float length = 0.0f;
    Vector3 prev = *it;
    ++it;
    if (it == mJointPath.end()) {
        return length;
    }
    do {
        float dx = mArcOffset.x - it->x;
        if (mSide == kSkeletonRight) {
            dx = dx * -1.0f;
        }
        float prevDx = mArcOffset.x - prev.x;
        if (mSide == kSkeletonRight) {
            prevDx = prevDx * -1.0f;
        }
        if (dx > 0.0f && prevDx > 0.0f) {
            float comp1 = mSwipeExtentX * prevDx * 2.0f - prevDx * prevDx;
            float arcY1;
            if (comp1 > 0.0f) {
                arcY1 = sqrtf(comp1);
            } else {
                arcY1 = 0.0f;
            }
            float comp2 = mSwipeExtentX * dx * 2.0f - dx * dx;
            float arcY2;
            if (comp2 > 0.0f) {
                arcY2 = sqrtf(comp2);
            } else {
                arcY2 = 0.0f;
            }
            float dz = mSwipeExtentY - mSwipeExtentY;
            length = sqrtf(dz * dz + (arcY2 - arcY1) * (arcY2 - arcY1) + (dx - prevDx) * (dx - prevDx)) + length;
        }
        prev = *it;
        ++it;
    } while (it != mJointPath.end());
    return length;
}

float ArcDetector::GetPathError() const {
    std::list<Vector3>::const_iterator it = mJointPath.begin();
    std::list<Vector3>::const_iterator pathEnd = mJointPath.end();
    if (it == pathEnd) {
        return 0.0f;
    }
    float error = 0.0f;
    do {
        Vector3 pt = *it;
        float dx = mArcOffset.x - pt.x;
        if (mSide == kSkeletonRight) {
            dx = dx * -1.0f;
        }
        float arcZBase = mArcOffset.z - pt.z;
        float comp = mSwipeExtentX * dx * 2.0f - dx * dx;
        float arcY;
        if (!(comp > 0.0f)) {
            arcY = 0.0f;
        } else {
            arcY = sqrtf(comp);
        }
        float errY = arcZBase - arcY;
        float errZ = (1.0f / sZErrorScale) * (pt.y - mSwipeExtentY);
        float dz = 0.0f;
        error = errZ * errZ + (errY * errY + dz * dz) + error;
        ++it;
    } while (it != pathEnd);
    return error;
}

float ArcDetector::GetSwipeAmount() const {
    float threshold = mSwipeThreshold * 0.3f;
    float adjustedThreshold = (float)mHoverTimer / (float)sDefaultHoverTimer * (mSwipeThreshold - threshold) + threshold;
    float exponent = _swipeRetentionFactor + 1.0f;
    float powered = (float)pow((double)GetPathLength(), (double)exponent);
    float pathErr = GetPathError();
    float swipeAmt = (powered - (pathErr / _acceptablePathErrorRatio)) / adjustedThreshold;

    std::list<Vector3>::const_iterator it = mJointPath.begin();
    unsigned int count = 0;
    if (it != mJointPath.end()) {
        do {
            ++it;
            count++;
        } while (it != mJointPath.end());
    }
    if (count <= 2) {
        swipeAmt = 0.5f - swipeAmt >= 0.0f ? swipeAmt : 0.5f;
    }
    if (mJointPath.begin() != mJointPath.end()) {
        Vector3 front = mJointPath.front();
        Vector3 second = mJointPath.back();
        Vector3 dir(front.x - second.x, front.y - second.y, front.z - second.z);
        Normalize(dir, dir);
        Vector3 boneDir(unk40.z, 0.0f, unk40.x);
        Normalize(boneDir, boneDir);
        if (fabsf(boneDir.y * dir.y + boneDir.z * dir.z + boneDir.x * dir.x) < 0.2f) {
            swipeAmt = 0.9f - swipeAmt >= 0.0f ? swipeAmt : 0.9f;
        }
    }
    return swipeAmt;
}

bool ArcDetector::IsLockedIn() const {
    static float sMinSwipeForLocked = 0.2f;
    static int sMinNodesForLocked = 2;
    std::list<Vector3>::const_iterator it = mJointPath.begin();
    unsigned int count = 0;
    if (it != mJointPath.end()) {
        do {
            ++it;
            count++;
        } while (it != mJointPath.end());
    }
    return count > (unsigned int)sMinNodesForLocked || GetSwipeAmount() > sMinSwipeForLocked;
}

bool ArcDetector::IsPathAcceptable() const {
    unsigned int count = 0;
    const std::list<Vector3> &jointPath = mJointPath;
    std::list<Vector3>::const_iterator it = jointPath.begin();
    static float sSlopeRatioThreshold = 0.5f;
    if (it != jointPath.end()) {
        do {
            ++it;
            count++;
        } while (it != jointPath.end());
    }
    if (count <= 1) {
        return true;
    }
    if (!IsLockedIn()) {
        Vector3 front = jointPath.front();
        const Vector3 &back = jointPath.back();
        float sign = (float)(mSide != 0 ? 1 : -1);
        float diffX = front.x - back.x;
        float dy = -(back.y - front.y);
        float diffZ = front.z - back.z;
        float dx = sign * diffX;
        if (dx < 0.0f) {
            return false;
        }
        if (dy == 0.0f) {
            return true;
        }
        float invDy = 1.0f / dy;
        if (invDy * dx >= sSlopeRatioThreshold) {
            return true;
        }
        if (invDy * diffZ >= sSlopeRatioThreshold) {
            return true;
        }
        return false;
    }
    return GetSwipeAmount() > 0.0f;
}

void ArcDetector::TryToStartSwipe(const Vector3 &pos, const Skeleton &skeleton) {
    MILO_ASSERT(mJointPath.empty(), 0x8B);
    bool tracked = true;
    if (skeleton.TrackedJoints()[mPrimaryJoint].mJointConf != kConfidenceTracked ||
        skeleton.TrackedJoints()[mSecondaryJoint].mJointConf != kConfidenceTracked) {
        tracked = false;
    }
    if (tracked) {
        mJointPath.insert(mJointPath.begin(), pos);
        const TrackedJoint *joints = skeleton.TrackedJoints();
        float dz = joints[mPrimaryJoint].mJointPos[0].z - joints[mSecondaryJoint].mJointPos[0].z;
        float dx = joints[mPrimaryJoint].mJointPos[0].x - joints[mSecondaryJoint].mJointPos[0].x;
        mSwipeExtentX = sqrtf(dx * dx + dz * dz);
    }
}

void ArcDetector::Update(const Skeleton &skeleton, int elapsed) {
    if (!mInitialized) {
        MILO_ASSERT(false, 0x4A);
    }
    if (!skeleton.IsTracked()) {
        Clear();
    } else {
        const TrackedJoint *joints = skeleton.TrackedJoints();
        float dx = joints[mPrimaryJoint].mJointPos[0].x - joints[mSecondaryJoint].mJointPos[0].x;
        float dy = joints[mPrimaryJoint].mJointPos[0].y - joints[mSecondaryJoint].mJointPos[0].y;
        float dz = joints[mPrimaryJoint].mJointPos[0].z - joints[mSecondaryJoint].mJointPos[0].z;
        Vector3 boneVec(dx, dy, dz);
        unk40 = boneVec;

        if (mJointPath.begin() == mJointPath.end()) {
            TryToStartSwipe(boneVec, skeleton);
        } else if (mHadProgress) {
            Vector3 frontPt = *mJointPath.begin();
            Clear();
            mJointPath.insert(mJointPath.begin(), boneVec);
            if (mSide == kSkeletonLeft && !(dx < frontPt.x + 0.01f)) {
                mHadProgress = false;
            }
            if (mSide == kSkeletonRight && !(dx > frontPt.x - 0.01f)) {
                mHadProgress = false;
            }
        } else {
            mArcOffset = GetCurveStart();
            Vector3 frontPt = *mJointPath.begin();
            float distX = dx - frontPt.x;
            float distY = dy - frontPt.y;
            float distZ = dz - frontPt.z;
            if (distY * distY + distZ * distZ + distX * distX > 0.0001f) {
                mJointPath.insert(mJointPath.begin(), boneVec);
            }
            float armDx = joints[mPrimaryJoint].mJointPos[0].x - joints[mSecondaryJoint].mJointPos[0].x;
            float armDz = joints[mPrimaryJoint].mJointPos[0].z - joints[mSecondaryJoint].mJointPos[0].z;
            mSwipeExtentX = (sqrtf(armDx * armDx + armDz * armDz) + mSwipeExtentX) * 0.5f;
        }
        mSwipeExtentY = joints[mPrimaryJoint].mJointPos[0].y - joints[mSecondaryJoint].mJointPos[0].y;
        CullPath();
        float swipe = GetSwipeAmount();
        mCurrentSwipeAmt = mCurrentSwipeAmt - swipe >= 0.0f ? mCurrentSwipeAmt : swipe;
        if (!IsPathAcceptable()) {
            SwipeFailed(skeleton);
        }
        float swipe2 = GetSwipeAmount();
        if (swipe2 < 0.1f) {
            mHoverTimer = Max(0, mHoverTimer - elapsed);
        }
    }
}

float ArcDetector::UpdateOverlay(RndOverlay *overlay, float y) {
    static std::list<Vector3> jointPathCopy;
    int numPts = mJointPath.size();
    if (numPts > 1) {
        jointPathCopy.clear();
        for (std::list<Vector3>::const_iterator it = mJointPath.begin(); it != mJointPath.end(); ++it) {
            jointPathCopy.insert(jointPathCopy.end(), *it);
        }
    }
    if (jointPathCopy.begin() != jointPathCopy.end()) {
        // The shipped build loads mWidth (TheRnd+0x40) first; MSVC evaluates the
        // operands right to left, so Width() is the *divisor*.  Cross-checked against
        // rb3-xenon's ArcDetector::UpdateOverlay, which spells Height() / Width().
        float aspectRatio = (float)TheRnd.Height() / (float)TheRnd.Width();
        float halfArcScale = aspectRatio / (mSwipeExtentX * 2.0f);
        float drawY = y;

        Vector2 prevScaled;
        for (std::list<Vector3>::const_iterator it = jointPathCopy.begin(); it != jointPathCopy.end(); ++it) {
            Vector3 pt = *it;
            float dx = mArcOffset.x - pt.x;
            if (mSide == kSkeletonRight) {
                dx = dx * -1.0f;
            }
            float scaledX = dx * halfArcScale;
            float comp = mSwipeExtentX * dx * 2.0f - dx * dx;
            float arcY = 0.0f;
            if (comp > 0.0f) {
                arcY = sqrtf(comp);
            }

            TheRnd.DrawStringScreen(
                MakeString("%f %f", dx, (mArcOffset.z - pt.z)),
                Vector2(0.6f, drawY),
                Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
                true
            );
            TheRnd.DrawStringScreen(
                MakeString("%f", arcY),
                Vector2(0.8f, drawY),
                Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
                true
            );
            UtilDrawCircle2D(Vector2(scaledX, arcY), 0.01f, Hmx::Color(0.0f, 0.0f, 0.0f, 1.0f), 4);
            UtilDrawCircle2D(Vector2(pt.x, pt.y), 0.004f, Hmx::Color(0.0f, 0.0f, 0.0f, 1.0f), 4);

            if (pt.x != jointPathCopy.front().x || pt.y != jointPathCopy.front().y || pt.z != jointPathCopy.front().z) {
                UtilDrawLine(prevScaled, Vector2(scaledX, arcY), Hmx::Color(0.0f, 0.0f, 0.0f, 1.0f));
            }

            Vector2 refPt(scaledX, 0.75f);
            UtilDrawCircle2D(refPt, 0.01f, Hmx::Color(0.0f, 0.0f, 0.0f, 1.0f), 4);

            Vector2 errPt(scaledX, (mArcOffset.y - pt.y) + 0.75f);
            UtilDrawCircle2D(errPt, 0.01f, Hmx::Color(0.0f, 0.0f, 0.0f, 1.0f), 4);

            prevScaled = Vector2(scaledX, arcY);
            drawY = drawY + 0.03125f;
        }

        const Vector3 &front = *jointPathCopy.begin();
        Skeleton *skel = TheGestureMgr->GetActiveSkeleton();
        float handX, handY, handZ;
        if (skel != NULL) {
            const TrackedJoint *joints = skel->TrackedJoints();
            handX = joints[mPrimaryJoint].mJointPos[0].x - joints[mSecondaryJoint].mJointPos[0].x;
            handY = joints[mPrimaryJoint].mJointPos[0].y - joints[mSecondaryJoint].mJointPos[0].y;
            handZ = joints[mPrimaryJoint].mJointPos[0].z - joints[mSecondaryJoint].mJointPos[0].z;
        } else {
            handX = front.x;
            handY = front.y;
            handZ = front.z;
        }

        float curDx = mArcOffset.x - handX;
        if (mSide == kSkeletonRight) {
            curDx = curDx * -1.0f;
        }
        float curScaledX = curDx * halfArcScale;
        float curScaledY = mArcOffset.z - handZ;
        UtilDrawCircle2D(Vector2(curScaledX, curScaledY), 0.015f, Hmx::Color(1.0f, 1.0f, 0.0f, 1.0f), 4);
        UtilDrawLine(Vector2(curScaledX, curScaledY), Vector2(aspectRatio * 0.5f, 0.0f), Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f));

        Vector2 curErrPt(curScaledX, (mArcOffset.y - handY) + 0.75f);
        UtilDrawCircle2D(curErrPt, 0.015f, Hmx::Color(0.0f, 0.0f, 1.0f, 1.0f), 4);

        float pathErr = GetPathError();
        TheRnd.DrawStringScreen(
            MakeString("Sum of error squares: %f", pathErr),
            Vector2(0.1f, y),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );

        float pathLen = GetPathLength();
        TheRnd.DrawStringScreen(
            MakeString("Length of path: %f", pathLen),
            Vector2(0.1f, y + 0.03125f),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );

        if (IsLockedIn()) {
            TheRnd.DrawStringScreen(
                "LOCKED IN",
                Vector2(0.1f, y + 0.0625f),
                Hmx::Color(0.0f, 1.0f, 0.0f, 1.0f),
                true
            );
        } else {
            TheRnd.DrawStringScreen(
                "NOT LOCKED IN",
                Vector2(0.1f, y + 0.0625f),
                Hmx::Color(1.0f, 0.0f, 0.0f, 1.0f),
                true
            );
        }

        TheRnd.DrawStringScreen(
            MakeString("Arc size: %f", mSwipeExtentX),
            Vector2(0.1f, y + 0.09375f),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );

        static DebugMeter swipeMeter(0.1f, 0.55f, 0.8f, 0.03f, Hmx::Color(0.1f, 0.1f, 0.1f, 0.5f));
        swipeMeter.Draw();
        swipeMeter.DrawBar(0.0f, GetSwipeAmount(), Hmx::Color(0.0f, 1.0f, 0.0f, 1.0f), 1.0f, 0.0f);

        static DebugMeter hoverMeter(0.1f, 0.6f, 0.4f, 0.03f, Hmx::Color(0.1f, 0.1f, 0.1f, 0.5f));
        hoverMeter.Draw();
        hoverMeter.DrawBar(0.0f, (float)mHoverTimer / (float)sDefaultHoverTimer, Hmx::Color(0.0f, 0.0f, 1.0f, 1.0f), 1.0f, 0.0f);

        y = drawY;
    }
    return y;
}
