#include "gesture/HandsUpGestureFilter.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "gesture/SkeletonQualityFilter.h"
#include "math/Mtx.h"
#include "obj/Object.h"

HandsUpGestureFilter::HandsUpGestureFilter() : mRequiredMs(500) { Clear(); }

HandsUpGestureFilter::~HandsUpGestureFilter() {}

BEGIN_PROPSYNCS(HandsUpGestureFilter)
    SYNC_PROP(required_ms, mRequiredMs)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void HandsUpGestureFilter::Update(Skeleton const &skeleton, int elapsed) {
    static bool sForceHandsUp = false;
    if (sForceHandsUp) {
        mHandsUp = true;
        mRaisedMs = 1;
        return;
    }

    int idx = skeleton.SkeletonIndex();
    if (idx < 0 || idx >= 6)
        return;

    SkeletonQualityFilter &qualityFilter = TheGestureMgr->GetSkeletonQualityFilter(idx);
    if (!skeleton.IsTracked() || qualityFilter.Sitting()) {
        mHandsUp = false;
        mRaisedMs = 0;
        return;
    }
    if (!qualityFilter.IsConfident()) {
        mHandsUp = false;
        mRaisedMs = 0;
        return;
    }

    const TrackedJoint &lShoulder = skeleton.ShoulderJoint(kSkeletonLeft);
    const TrackedJoint &rShoulder = skeleton.ShoulderJoint(kSkeletonRight);

    // Average shoulder y for threshold
    float shoulderAvgY = (lShoulder.mJointPos[kCoordCamera].y + rShoulder.mJointPos[kCoordCamera].y) * 0.5f;

    const TrackedJoint &lHand = skeleton.HandJoint(kSkeletonLeft);
    const TrackedJoint &rHand = skeleton.HandJoint(kSkeletonRight);
    const TrackedJoint &lElbow = skeleton.ElbowJoint(kSkeletonLeft);
    const TrackedJoint &rElbow = skeleton.ElbowJoint(kSkeletonRight);

    // Direction from shoulder to hand for left arm, normalized
    Vector3 lArmDir;
    lArmDir.x = lHand.mJointPos[kCoordCamera].x - lShoulder.mJointPos[kCoordCamera].x;
    lArmDir.y = lHand.mJointPos[kCoordCamera].y - lShoulder.mJointPos[kCoordCamera].y;
    lArmDir.z = lHand.mJointPos[kCoordCamera].z - lShoulder.mJointPos[kCoordCamera].z;
    Vector3 lArmNorm;
    Normalize(lArmDir, lArmNorm);

    // Check left hand above left shoulder
    const TrackedJoint &lShoulderAgain = skeleton.ShoulderJoint(kSkeletonLeft);
    const TrackedJoint &rShoulderAgain = skeleton.ShoulderJoint(kSkeletonRight);

    bool leftHandUp = lHand.mJointPos[kCoordCamera].y > lShoulderAgain.mJointPos[kCoordCamera].y;
    bool rightHandUp = rHand.mJointPos[kCoordCamera].y > rShoulderAgain.mJointPos[kCoordCamera].y;

    // Check elbows above shoulder average
    bool leftElbowUp = lElbow.mJointPos[kCoordCamera].y > shoulderAvgY;
    bool rightElbowUp = rElbow.mJointPos[kCoordCamera].y > shoulderAvgY;

    // Get screen positions
    Vector2 lScreenPos, rScreenPos;
    skeleton.ScreenPos(kJointHandLeft, lScreenPos);
    skeleton.ScreenPos(kJointHandRight, rScreenPos);

    // Check screen position distance
    bool screenCheck = (rScreenPos.x - lScreenPos.x) > 0.0f;

    if (leftHandUp && rightHandUp && leftElbowUp && rightElbowUp
        && lArmNorm.y > 0.0f && screenCheck) {
        mRaisedMs += elapsed;
        if (mRaisedMs >= mRequiredMs) {
            mHandsUp = true;
        }
    } else {
        mHandsUp = false;
        mRaisedMs = 0;
    }
}

void HandsUpGestureFilter::Update(int i, int j) {
    Skeleton *skel = TheGestureMgr->GetSkeletonByTrackingID(i);
    if (skel)
        Update(*skel, j);
    else {
        mHandsUp = false;
        mRaisedMs = 0;
    }
}

void HandsUpGestureFilter::Clear() {
    mHandsUp = false;
    mRaisedMs = 0;
}

BEGIN_HANDLERS(HandsUpGestureFilter)
    HANDLE_ACTION(clear, Clear())
    HANDLE_ACTION(update, Update(_msg->Int(2), _msg->Int(3)))
    HANDLE_EXPR(check, mHandsUp)
    HANDLE_EXPR(raised_ms, mRaisedMs)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
