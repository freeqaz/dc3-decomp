#include "gesture/DirectionGestureFilter.h"
#include "BaseSkeleton.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "gesture/SkeletonViz.h"
#include "gesture/StandingStillGestureFilter.h"
#include "obj/Task.h"
#include "rndobj/Overlay.h"

float DirectionGestureFilter::sLastSwipeTime[6] = { -100, -100, -100, -100, -100, -100 };

#pragma region DirectionGestureFilterSingleUser

DirectionGestureFilterSingleUser::DirectionGestureFilterSingleUser(
    SkeletonSide s1, SkeletonSide s2, float f3, float f4
)
    : unk4(s1), unkc(s2), mSwipeAmt(0), unk18(0), mEngaged(0), mAllowAboveShoulder(1),
      mHighButtonMode(0), unk20(0.5) {
    Clear();
    SkeletonJoint wristJoint, elbowJoint;
    if (s1 == kSkeletonRight) {
        elbowJoint = kJointElbowRight;
        wristJoint = kJointWristRight;
    } else {
        elbowJoint = kJointElbowLeft;
        wristJoint = kJointWristLeft;
    }
    mArcDetector.Initialize(s2, wristJoint, elbowJoint, f3);
}

DirectionGestureFilterSingleUser::~DirectionGestureFilterSingleUser() {
    RndOverlay *swipeOverlay = RndOverlay::Find("swipe_direction", false);
    if (swipeOverlay && swipeOverlay->GetCallback() == this) {
        swipeOverlay->SetCallback(nullptr);
    }
}

void DirectionGestureFilterSingleUser::Clear() {
    mConfidence = kConfidenceNotTracked;
    ClearSwipe();
}

void DirectionGestureFilterSingleUser::Update(const Skeleton &skeleton, int elapsedMs) {
    static bool print = false;
    static float sVal = 0.2f;
    mConfidence = kConfidenceTracked;
    if (IsHandValid(skeleton)) {
        mArcDetector.Update(skeleton, elapsedMs);
    }
    if (!skeleton.IsTracked()) {
        mHasDirection = false;
        mConfidence = kConfidenceNotTracked;
    } else if (!mHasDirection) {
        mSwipeAmt = mArcDetector.GetSwipeAmount();
        if (sLastSwipeTime[skeleton.SkeletonIndex()] + unk20 > TheTaskMgr.UISeconds()
            && sLastSwipeTime[skeleton.SkeletonIndex()] < TheTaskMgr.UISeconds()) {
            ClearSwipe();
        } else if (mSwipeAmt >= 1.0f) {
            if (print) {
                mArcDetector.PrintJointPath();
            }
            ClearSwipe();
            mHasDirection = true;
            sLastSwipeTime[skeleton.SkeletonIndex()] = TheTaskMgr.UISeconds();
        }
        unk18 = Clamp(0.0f, 1.0f, (mSwipeAmt - sVal) / (1.0f - sVal));
    }
}

void DirectionGestureFilterSingleUser::Draw(const Skeleton &skeleton, SkeletonViz &viz) {
    mArcDetector.Draw(skeleton, viz);
    bool valid = IsValidSwipePosition(skeleton);
    viz.DrawPoint3D(
        skeleton.HandJoint(unk4).mJointPos[0],
        0.1f,
        valid ? Hmx::Color(0, 1, 0) : Hmx::Color(1, 0, 0),
        0.2f
    );
    if (sLastSwipeTime[skeleton.SkeletonIndex()] + unk20 > TheTaskMgr.UISeconds()
        && sLastSwipeTime[skeleton.SkeletonIndex()] < TheTaskMgr.UISeconds()) {
        const Vector3 &pos = skeleton.HandJoint(unk4).mJointPos[0];
        viz.DrawPoint3D(pos, 0.1f, Hmx::Color(0, 1, 0), 0.2f);
    }
}

static float sValidHandFloats[4] = { 0.2f, 2.0f, 0.3f, 0.3f };

bool DirectionGestureFilterSingleUser::IsHandValid(const Skeleton &skeleton) const {
    return IsValidSwipePosition(skeleton)
        || (mArcDetector.NumJointsInPath() > 1U
            && !HandAtSide(skeleton, sValidHandFloats[0], sValidHandFloats[1], 0.0f));
}

bool DirectionGestureFilterSingleUser::IsValidScrollPos(const Skeleton &skeleton) const {
    if (IsValidSwipePosition(skeleton)) {
        return true;
    } else if (HandAtSide(skeleton, sValidHandFloats[2], 1.0f, 0.5f)) {
        return !HandAtSide(skeleton, sValidHandFloats[3], 1.0f, 0.0f);
    } else {
        return false;
    }
}

void DirectionGestureFilterSingleUser::ClearSwipe() {
    mSwipeAmt = 0;
    mHasDirection = false;
    unk18 = 0;
    mArcDetector.Clear();
}

bool DirectionGestureFilterSingleUser::IsLockedIn() const {
    return mArcDetector.IsLockedIn();
}

void DirectionGestureFilterSingleUser::ResetHoverTimer() {
    mArcDetector.ResetHoverTimer();
}

float DirectionGestureFilterSingleUser::UpdateOverlay(RndOverlay *overlay, float f1) {
    return mArcDetector.UpdateOverlay(overlay, f1);
}

bool DirectionGestureFilterSingleUser::IsValidSwipePosition(const Skeleton &skeleton) const {
    const Vector3 &handPos = skeleton.HandJoint(unk4).mJointPos[0];
    float handX = handPos.x;
    float handY = handPos.y;
    float handZ = handPos.z;

    // ShoulderCenter camera-space position (raw offset into mTrackedJoints)
    float shoulderX = *((float*)(&skeleton) + 0x3B);
    float shoulderY = *((float*)(&skeleton) + 0x3C);
    float shoulderZ = *((float*)(&skeleton) + 0x3D);

    float deltaX = handX - shoulderX;
    float deltaY = handY - shoulderY;
    float deltaZ = handZ - shoulderZ;

    Vector3 corner1, corner2;
    corner1.x = shoulderX - deltaX;
    corner2.x = handX + deltaX;
    corner1.y = shoulderY - deltaY;
    corner2.y = handY + deltaY;
    corner1.z = shoulderZ - deltaZ;
    corner2.z = handZ + deltaZ;

    Vector3 closest;
    ClosestPoint(corner1, corner2, handPos, &closest);

    // HipRight - HipLeft (raw offset into mTrackedJoints)
    Vector3 direction;
    direction.x = *((float*)(&skeleton) + 0x1B4) - *((float*)(&skeleton) + 0x15D);
    direction.y = *((float*)(&skeleton) + 0x1B5) - *((float*)(&skeleton) + 0x15E);
    direction.z = *((float*)(&skeleton) + 0x1B6) - *((float*)(&skeleton) + 0x15F);

    Normalize(direction, direction);

    float angle = atan2(direction.z, direction.x);
    float s1 = Sine(3.14159265f * 0.5f - angle);
    float s2 = Sine(-angle);

    float rotX = closest.x * s2;
    rotX += closest.y * s1;
    float rotY = closest.x * s1;
    rotY -= closest.y * s2;

    float height = 0.15f;
    float width = 0.15f;

    float ellipseTest = ((rotX * rotX) / (height * height)) + ((rotY * rotY) / (width * width));

    if (ellipseTest < 1.0f) {
        return 0;
    }

    if ((!mAllowAboveShoulder) || mHighButtonMode) {
        float yTest = skeleton.HandJoint(unk4).mJointPos[0].y - *((float*)(&skeleton) + 0x3C);
        if ((mHighButtonMode && yTest < 0.0f) || (!mHighButtonMode && yTest > 0.0f)) {
            return 0;
        }
    }
    return 1;
}

#pragma endregion
#pragma region DirectionGestureFilterDoubleUser

DirectionGestureFilterDoubleUser::DirectionGestureFilterDoubleUser(
    SkeletonSide s1, SkeletonSide s2, float f3, float f4
)
    : unk4(new DirectionGestureFilterSingleUser(s1, s2, f3, f4)),
      unk8(new DirectionGestureFilterSingleUser(s1, s2, f3, f4)) {
    for (int i = 0; i < 2; i++) {
        unkc[i] = new StandingStillGestureFilter();
        unkc[i]->SetRequiredMs(750);
        unkc[i]->SetUnk4c(true);
    }
}

DirectionGestureFilterDoubleUser::~DirectionGestureFilterDoubleUser() {
    delete unk4;
    delete unk8;
    for (int i = 0; i < 2; i++) {
        delete unkc[i];
    }
}

void DirectionGestureFilterDoubleUser::Clear() {
    unk4->Clear();
    unk8->Clear();
}

JointConfidence DirectionGestureFilterDoubleUser::Confidence() const {
    return Max(unk4->Confidence(), unk8->Confidence());
}

void DirectionGestureFilterDoubleUser::Update(const Skeleton &skeleton, int ms) {
    for (int i = 0; i < 2; i++) {
        unkc[i]->Update(TheGestureMgr->GetPlayerSkeletonID(i), ms);
    }
    int i1, i2;
    GetValidSkeletons(i1, i2);
    unk4->Update(TheGestureMgr->GetSkeleton(i1), ms);
    unk8->Update(TheGestureMgr->GetSkeleton(i2), ms);
}

void DirectionGestureFilterDoubleUser::Draw(const Skeleton &skeleton, SkeletonViz &viz) {
    unk4->Draw(skeleton, viz);
}

bool DirectionGestureFilterDoubleUser::HasDirection() const {
    return unk4->HasDirection() || unk8->HasDirection();
}

float DirectionGestureFilterDoubleUser::GetPercentPulled() const {
    return Max(unk4->GetPercentPulled(), unk8->GetPercentPulled());
}

void DirectionGestureFilterDoubleUser::ClearSwipe() {
    unk4->ClearSwipe();
    unk8->ClearSwipe();
}

bool DirectionGestureFilterDoubleUser::IsLockedIn() const {
    return unk4->IsLockedIn() || unk8->IsLockedIn();
}

void DirectionGestureFilterDoubleUser::SetEngaged(bool engaged) {
    unk4->SetEngaged(engaged);
    unk8->SetEngaged(engaged);
}

void DirectionGestureFilterDoubleUser::ResetHoverTimer() {
    unk4->ResetHoverTimer();
    unk8->ResetHoverTimer();
}

void DirectionGestureFilterDoubleUser::SetAllowAboveShoulder(bool allow) {
    unk4->SetAllowAboveShoulder(allow);
    unk8->SetAllowAboveShoulder(allow);
}

void DirectionGestureFilterDoubleUser::SetHighButtonMode(bool set) {
    unk4->SetHighButtonMode(set);
    unk8->SetHighButtonMode(set);
}
