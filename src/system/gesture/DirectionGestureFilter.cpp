#include "gesture\DirectionGestureFilter.h"
#include "BaseSkeleton.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\GestureMgr.h"
#include "gesture\Skeleton.h"
#include "gesture\SkeletonViz.h"
#include "gesture\StandingStillGestureFilter.h"
#include "obj/Task.h"
#include "rndobj\Overlay.h"

float DirectionGestureFilter::sLastSwipeTime[6] = { -100, -100, -100, -100, -100, -100 };

#pragma region DirectionGestureFilterSingleUser

DirectionGestureFilterSingleUser::DirectionGestureFilterSingleUser(
    SkeletonSide s1, SkeletonSide s2, float f3, float f4
)
    : mHandSide(s1), mSwipeSide(s2), mSwipeAmt(0), mPercentPulled(0), mEngaged(0), mAllowAboveShoulder(1),
      mHighButtonMode(0), mSwipeCooldown(0.5) {
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
        if (sLastSwipeTime[skeleton.SkeletonIndex()] + mSwipeCooldown > TheTaskMgr.UISeconds()
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
        mPercentPulled = Clamp(0.0f, 1.0f, (mSwipeAmt - sVal) / (1.0f - sVal));
    }
}

// RESIDUAL (w11-d, 99.77 canonical): the image pins 0.1f in f29 and 0.2f in
// f28 and sets up the first DrawPoint3D's arguments this/radius/color/alpha
// (`mr r3` before `mr r6`, f1 before f2); we get the constants the other way
// round.  Inert: named radius/alpha locals in either order; the second call
// without the `pos` reference is worse (92.25); a decomp-synth beam run
// (6 rounds) found nothing.
void DirectionGestureFilterSingleUser::Draw(const Skeleton &skeleton, SkeletonViz &viz) {
    mArcDetector.Draw(skeleton, viz);
    bool valid = IsValidSwipePosition(skeleton);
    viz.DrawPoint3D(
        skeleton.HandJoint(mHandSide).mJointPos[0],
        0.1f,
        valid ? Hmx::Color(0, 1, 0) : Hmx::Color(1, 0, 0),
        0.2f
    );
    if (sLastSwipeTime[skeleton.SkeletonIndex()] + mSwipeCooldown > TheTaskMgr.UISeconds()
        && sLastSwipeTime[skeleton.SkeletonIndex()] < TheTaskMgr.UISeconds()) {
        const Vector3 &pos = skeleton.HandJoint(mHandSide).mJointPos[0];
        viz.DrawPoint3D(pos, 0.1f, Hmx::Color(0, 1, 0), 0.2f);
    }
}

static float sValidHandFloats[3] = { 0.2f, 2.0f, 0.3f };
// Its own .data object in the target (lbl_82F444AC, right after the array): the
// scroll check anchors r31 here and reads sValidHandFloats[2] as -4 from it.
static float sValidScrollHandRadius = 0.3f;

bool DirectionGestureFilterSingleUser::HandAtSide(
    const Skeleton &skeleton, float radius, float xScale, float elbowBlend
) const {
    float heightDiff =
        skeleton.TrackedJoints()[kJointHead].mJointPos[0].y
        - skeleton.TrackedJoints()[kJointFootRight].mJointPos[0].y;

    const TrackedJoint &hand = skeleton.HandJoint(mHandSide);
    const TrackedJoint &hip = skeleton.HipJoint(mHandSide);
    const TrackedJoint &knee = skeleton.KneeJoint(mHandSide);
    const TrackedJoint &elbow = skeleton.ElbowJoint(mHandSide);

    // The knee/hip midpoint lives in a Vector3: a scalar-local `sum * 0.5f`
    // subtracted from the hand gets contracted into fnmsubs, the image keeps
    // fmuls + fsubs (91.8 -> 100).
    Vector3 hipMid;
    Add(knee.mJointPos[0], hip.mJointPos[0], hipMid);
    hipMid *= 0.5f;
    float elbowOffset = radius * elbowBlend + elbow.mJointPos[0].x;
    float threshold = heightDiff * 0.591715931892395f;
    float dx = hand.mJointPos[0].x - elbowOffset;
    threshold *= radius;
    dx *= xScale;
    float dy = hand.mJointPos[0].y - hipMid.y;
    float dz = hand.mJointPos[0].z - hipMid.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    return dist <= threshold;
}

bool DirectionGestureFilterSingleUser::IsHandValid(const Skeleton &skeleton) const {
    return IsValidSwipePosition(skeleton)
        || (mArcDetector.NumJointsInPath() > 1U
            && !HandAtSide(skeleton, sValidHandFloats[0], sValidHandFloats[1], 0.0f));
}

bool DirectionGestureFilterSingleUser::IsValidScrollPos(const Skeleton &skeleton) const {
    if (IsValidSwipePosition(skeleton)) {
        return true;
    } else if (HandAtSide(skeleton, sValidHandFloats[2], 1.0f, 0.5f)) {
        return !HandAtSide(skeleton, sValidScrollHandRadius, 1.0f, 0.0f);
    } else {
        return false;
    }
}

void DirectionGestureFilterSingleUser::ClearSwipe() {
    mSwipeAmt = 0;
    mHasDirection = false;
    mPercentPulled = 0;
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
    // .data:0x82F4453C / 0x40 / 0x44 / 0x48 -- four MUTABLE function-local
    // statics: the half-axes, in shoulder-widths, of the torso exclusion ellipse
    // the hand must be OUTSIDE of for the pose to count as a valid swipe.  The
    // image addresses each one with its own `lis`/`lfs` pair; as file-scope
    // statics MSVC anchors all four off one base (`addi r11, r11, sym@l` then
    // 0x4/0x8/0xc displacements) -- 97.16 -> 99.98.
    static float sSwipeEllipseWidth = 0.9f;
    static float sSwipeEllipseHeight = 1.3f;
    static float sSwipeEllipseWidthEngaged = 0.8f;
    static float sSwipeEllipseHeightEngaged = 1.1f;
    // w21-as: written with Vector3 helpers over skeleton.TrackedJoints() (no
    // `joints` local), the closest-point delta and the hip direction as
    // Subtract()s, and the ellipse sum as two statements -- 99.98 -> see the
    // note below.  The value-numbering order MSVC uses for its /fp:fast
    // operand canonicalisation followed the old over-named locals (hipX,
    // deltaX, closestDeltaX, ...) and emitted every commutative pair the
    // other way round: `hipX + deltaX` (target 0x82DEA580 fadds f12, f11, f9),
    // the rotation products (0x82DEA65C..68) and the ellipse sum.
    const Vector3 &hip = skeleton.TrackedJoints()[kJointHipCenter].mJointPos[0];
    const Vector3 &shoulder = skeleton.TrackedJoints()[kJointShoulderCenter].mJointPos[0];
    Vector3 delta;
    Subtract(hip, shoulder, delta);
    Vector3 corner1, corner2;
    Subtract(shoulder, delta, corner1);
    Add(hip, delta, corner2);
    Vector3 shoulderVec;
    Subtract(
        skeleton.TrackedJoints()[kJointShoulderLeft].mJointPos[0],
        skeleton.TrackedJoints()[kJointShoulderRight].mJointPos[0],
        shoulderVec
    );
    float shoulderDist = Length(shoulderVec);

    Vector3 closest;
    ClosestPoint(corner1, corner2, skeleton.HandJoint(mHandSide).mJointPos[0], &closest);
    Vector3 closestDelta;
    Subtract(closest, skeleton.HandJoint(mHandSide).mJointPos[0], closestDelta);

    Vector3 direction;
    Subtract(
        skeleton.TrackedJoints()[kJointHipRight].mJointPos[0],
        skeleton.TrackedJoints()[kJointHipLeft].mJointPos[0],
        direction
    );
    Normalize(direction, direction);

    float angle = atan2(direction.z, direction.x);
    float s1 = Sine(1.5707963705062866f - angle); // Use exact constant from binary
    float s2 = Sine(-angle);

    float rotX = closestDelta.x * s2 + closestDelta.z * s1;
    float rotY = closestDelta.x * s1 - closestDelta.z * s2;

    float width, height;
    if (mEngaged) {
        height = sSwipeEllipseHeightEngaged;
        width = sSwipeEllipseWidthEngaged;
    } else {
        height = sSwipeEllipseHeight;
        width = sSwipeEllipseWidth;
    }

    height *= shoulderDist;
    width *= shoulderDist;

    float ellipseTest = (rotX * rotX) / (width * width);
    ellipseTest += (rotY * rotY) / (height * height);

    if (ellipseTest < 1.0f) {
        return 0;
    }

    if (!mAllowAboveShoulder || mHighButtonMode) {
        // Call HandJoint AGAIN for Y-test
        const TrackedJoint &handJoint3 = skeleton.HandJoint(mHandSide);
        // Re-derived from `skeleton`, NOT from the `joints` local: the image
        // reads this through the skeleton pointer it already keeps in r31
        // (`lfs f0, 0xf0(r31)`). Reusing `joints` here is its only use after the
        // calls, so MSVC pins it in a third callee-saved GPR for the whole
        // function -- that is the `bl __savegprlr_29` / +0x10 frame we emit
        // where the image inlines `std r30`/`std r31`.
        float shoulderYFresh =
            skeleton.TrackedJoints()[kJointShoulderCenter].mJointPos[0].y;
        float yTest = handJoint3.mJointPos[0].y - shoulderYFresh;
        if (mHighButtonMode) {
            if (yTest < 0.0f) {
                return 0;
            }
        } else {
            if (yTest > 0.0f) {
                return 0;
            }
        }
    }
    return 1;
}

#pragma endregion
#pragma region DirectionGestureFilterDoubleUser

DirectionGestureFilterDoubleUser::DirectionGestureFilterDoubleUser(
    SkeletonSide s1, SkeletonSide s2, float f3, float f4
)
    : mFilter1(new DirectionGestureFilterSingleUser(s1, s2, f3, f4)),
      mFilter2(new DirectionGestureFilterSingleUser(s1, s2, f3, f4)) {
    for (int i = 0; i < 2; i++) {
        mStillFilters[i] = new StandingStillGestureFilter();
        mStillFilters[i]->SetRequiredMs(750);
        mStillFilters[i]->SetUnk48(true);
    }
}

DirectionGestureFilterDoubleUser::~DirectionGestureFilterDoubleUser() {
    delete mFilter1;
    delete mFilter2;
    for (int i = 0; i < 2; i++) {
        delete mStillFilters[i];
    }
}

void DirectionGestureFilterDoubleUser::Clear() {
    mFilter1->Clear();
    mFilter2->Clear();
}

JointConfidence DirectionGestureFilterDoubleUser::Confidence() const {
    return Max(mFilter1->Confidence(), mFilter2->Confidence());
}

void DirectionGestureFilterDoubleUser::Update(const Skeleton &skeleton, int ms) {
    for (int i = 0; i < 2; i++) {
        mStillFilters[i]->Update(TheGestureMgr->GetPlayerSkeletonID(i), ms);
    }
    int i1, i2;
    GetValidSkeletons(i1, i2);
    int s1 = i1 == -1 ? 0 : i1;
    int s2 = i2 == -1 ? 0 : i2;
    mFilter1->Update(TheGestureMgr->GetSkeleton(s1), ms);
    mFilter2->Update(TheGestureMgr->GetSkeleton(s2), ms);
}

void DirectionGestureFilterDoubleUser::Draw(const Skeleton &skeleton, SkeletonViz &viz) {
    mFilter1->Draw(skeleton, viz);
}

bool DirectionGestureFilterDoubleUser::HasDirection() const {
    return mFilter1->HasDirection() || mFilter2->HasDirection();
}

float DirectionGestureFilterDoubleUser::GetPercentPulled() const {
    return Max(mFilter1->GetPercentPulled(), mFilter2->GetPercentPulled());
}

void DirectionGestureFilterDoubleUser::ClearSwipe() {
    mFilter1->ClearSwipe();
    mFilter2->ClearSwipe();
}

bool DirectionGestureFilterDoubleUser::IsLockedIn() const {
    return mFilter1->IsLockedIn() || mFilter2->IsLockedIn();
}

void DirectionGestureFilterDoubleUser::SetEngaged(bool engaged) {
    mFilter1->SetEngaged(engaged);
    mFilter2->SetEngaged(engaged);
}

void DirectionGestureFilterDoubleUser::ResetHoverTimer() {
    mFilter1->ResetHoverTimer();
    mFilter2->ResetHoverTimer();
}

void DirectionGestureFilterDoubleUser::SetAllowAboveShoulder(bool allow) {
    mFilter1->SetAllowAboveShoulder(allow);
    mFilter2->SetAllowAboveShoulder(allow);
}

void DirectionGestureFilterDoubleUser::SetHighButtonMode(bool set) {
    mFilter1->SetHighButtonMode(set);
    mFilter2->SetHighButtonMode(set);
}

void DirectionGestureFilterDoubleUser::GetValidSkeletons(int &out1, int &out2) const {
    int id1 = TheGestureMgr->GetPlayerSkeletonID(0);
    int id2 = TheGestureMgr->GetPlayerSkeletonID(1);
    int idx;
    if (id1 != -1) {
        idx = TheGestureMgr->GetSkeletonIndexByTrackingID(id1);
    } else {
        idx = -1;
    }
    out1 = idx;
    if (id2 != -1) {
        idx = TheGestureMgr->GetSkeletonIndexByTrackingID(id2);
    } else {
        idx = -1;
    }
    out2 = idx;
    if (out1 != -1) {
        if (!TheGestureMgr->IsSkeletonValid(out1)) {
            out1 = -1;
        }
    }
    if (out2 != -1) {
        if (!TheGestureMgr->IsSkeletonValid(out2)) {
            out2 = -1;
        }
    }
}

bool DirectionGestureFilterDoubleUser::IsHandValid(const Skeleton &skel) const {
    int i1, i2;
    GetValidSkeletons(i1, i2);
    return (i1 >= 0 && mFilter1->IsHandValid(TheGestureMgr->GetSkeleton(i1))
            && mStillFilters[0]->StandingStill())
        || (i2 >= 0 && mFilter2->IsHandValid(TheGestureMgr->GetSkeleton(i2))
            && mStillFilters[1]->StandingStill());
}

bool DirectionGestureFilterDoubleUser::IsValidScrollPos(const Skeleton &skeleton) const {
    int i1, i2;
    GetValidSkeletons(i1, i2);
    return (i1 >= 0 && mFilter1->IsValidScrollPos(TheGestureMgr->GetSkeleton(i1)))
        || (i2 >= 0 && mFilter2->IsValidScrollPos(TheGestureMgr->GetSkeleton(i2)));
}
