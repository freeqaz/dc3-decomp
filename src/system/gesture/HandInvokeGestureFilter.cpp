#include "gesture\HandInvokeGestureFilter.h"
#include "math\Rot.h"
#include <cmath>

HandInvokeGestureFilter::HandInvokeGestureFilter()
    : unk4(Vector3(0, 0, -1), 3, 0), unk50(Vector3::GetZero(), 3, 0),
      unk8c(Vector3::GetZero(), 6, 0), unkc8(Vector3::GetZero(), 3, 0),
      unk104(Vector3::GetZero(), 6, 0), mInvokeDetected(0), unk144(0) {}

HandInvokeGestureFilter::~HandInvokeGestureFilter() {}

float HandInvokeGestureFilter::GetBend(
    const Vector3 &a, const Vector3 &b, const Vector3 &c
) const {
    Vector3 d1(b.x - c.x, b.y - c.y, b.z - c.z);
    Normalize(d1, d1);
    Vector3 d2(a.x - c.x, a.y - c.y, a.z - c.z);
    Normalize(d2, d2);
    return std::acos(d2.x * d1.x + d2.y * d1.y + d2.z * d1.z);
}

bool HandInvokeGestureFilter::UpdateBodyPlane(const Skeleton &skel, float dt) {
    const TrackedJoint &rightShoulder = skel.ShoulderJoint(kSkeletonRight);
    const TrackedJoint &leftShoulder = skel.ShoulderJoint(kSkeletonLeft);

    float dx = leftShoulder.mJointPos[0].x - rightShoulder.mJointPos[0].x;
    float dy = leftShoulder.mJointPos[0].y - rightShoulder.mJointPos[0].y;
    float dz = leftShoulder.mJointPos[0].z - rightShoulder.mJointPos[0].z;

    bool _result = false;
    bool valid = skel.ShoulderJoint(kSkeletonLeft).mJointConf != kConfidenceNotTracked
        && skel.ShoulderJoint(kSkeletonRight).mJointConf != kConfidenceNotTracked
        && dx * dx + dy * dy + dz * dz > 0.0f;

    if (valid) {
        // Cross(yAxis, shoulderVec) = body forward normal in XZ plane
        Vector3 bodyNormal(dz - dy * 0.0f, dx * 0.0f - dz * 0.0f, dy * 0.0f - dx);
        Normalize(bodyNormal, bodyNormal);
        unk4.Smooth(bodyNormal, dt, true);

        // Compute body "side" vector as Cross(yAxis, smoothedBodyNormal)
        {
            Vector3 smoothed = unk4.Value();
            unk40.y = smoothed.x * 0.0f - smoothed.z * 0.0f;
            unk40.z = smoothed.y * 0.0f - smoothed.x;
            unk40.x = smoothed.z - smoothed.y * 0.0f;
        }
        Normalize(unk40, unk40);

        // Check body orientation angle: project bodyNormal to XZ plane and get angle
        Vector3 projected = bodyNormal;
        projected.y = 0.0f;
        Normalize(projected, projected);
        float angle = std::acos((projected.x + projected.y) * 0.0f + projected.z);
        if (angle < 32.0f * DEG2RAD) {
            _result = true;
        }
    }
    return _result;
}
bool HandInvokeGestureFilter::CalcInPose(const Skeleton &skel, float dt) {
    // 0x82DFE038 `li r25, 0x0` seeds the result in a callee-saved register and
    // 0x82DFE514 `mr r3, r25` returns it -- one result variable, not two
    // `return` statements.  (r25 is also why the prologue is __savegprlr_25.)
    bool inPose = false;

    // Smooth hand and shoulder positions.  Right (kSkeletonSide 1) first:
    // 0x82DFE034 `li r4, 0x1`.
    unk50.Smooth(skel.HandJoint(kSkeletonRight).mJointPos[0], dt, false);
    unk8c.Smooth(skel.ShoulderJoint(kSkeletonRight).mJointPos[0], dt, false);
    unkc8.Smooth(skel.HandJoint(kSkeletonLeft).mJointPos[0], dt, false);
    unk104.Smooth(skel.ShoulderJoint(kSkeletonLeft).mJointPos[0], dt, false);

    // Arm direction: shoulder - hand, normalized.
    //
    // Every Vector3DESmoother::Value() result stays an unnamed TEMPORARY here.
    // 0x82DFE0E8/0x82DFE0F8 hand the two calls the stack temps at r1+0x70 and
    // r1+0x60, read the components straight off the returned pointer, and then
    // let MSVC colour those slots for the next user -- r1+0x60 becomes
    // leftArmDir at 0x82DFE184 and r1+0x70 becomes `lateral` at 0x82DFE21C.
    // The whole function owns just FIVE 16-byte vector slots (0x50, 0x60,
    // 0x70, 0x80, 0x90) in a 0x130 frame; a named `Vector3` per Value() pins
    // six more and costs 0x60 of frame.
    Vector3 rightArmDir;
    Subtract(unk8c.Value(), unk50.Value(), rightArmDir);
    Normalize(rightArmDir, rightArmDir);

    Vector3 leftArmDir;
    Subtract(unk104.Value(), unkc8.Value(), leftArmDir);
    Normalize(leftArmDir, leftArmDir);

    // Spine vector: shoulderCenter - hipCenter.  0x82DFE198-0x82DFE1C4 reads
    // 0xec/0xf0/0xf4 and 0x4/0x8/0xc off the Skeleton -- mTrackedJoints[2] and
    // mTrackedJoints[0].  It never reaches memory: `spine` and `proj` below are
    // only ever handed to INLINE helpers, so MSVC keeps all six components in
    // FPRs and the frame stays at five vector slots.
    Vector3 spine;
    Subtract(
        skel.TrackedJoints()[kJointShoulderCenter].mJointPos[0],
        skel.TrackedJoints()[kJointHipCenter].mJointPos[0],
        spine
    );

    // Project the spine onto the smoothed body normal and subtract that
    // component off to get the lateral (near-vertical) axis.  unk4.Value() is
    // called FOUR separate times (0x82DFE1C8, 0x82DFE1F0, 0x82DFE264,
    // 0x82DFE2B0); each result is consumed straight off the returned pointer
    // (`mr r11, r3`), never bound to a named object.
    float spineDot = Dot(unk4.Value(), spine);

    // 0x82DFE1D8-0x82DFE22C is three `fmuls` and then three `fsubs`, never an
    // `fnmsubs`: the scale and the subtraction are two separate inline calls,
    // so /fp:fast has no single `a - b*c` tree to contract.
    Vector3 proj;
    Scale(unk4.Value(), spineDot, proj);
    Vector3 lateral;
    Subtract(spine, proj, lateral);
    Normalize(lateral, lateral);

    // Project the arm directions onto the body side vector (unk40) and onto
    // the body normal.
    float rightElevation = -Dot(unk40, rightArmDir);
    float leftElevation = -Dot(unk40, leftArmDir);
    float rightForward = Dot(rightArmDir, unk4.Value());
    float leftForward = Dot(unk4.Value(), leftArmDir);

    float negZero = -0.0f;

    // Compute angles for the right arm
    float rightAngle1 = std::atan2(
        rightElevation, (rightArmDir.z + rightArmDir.x) * negZero - rightArmDir.y
    );
    float rightAngle2 = std::atan2(rightElevation, rightForward);
    float rightBend = GetBend(
        skel.ShoulderJoint(kSkeletonRight).mJointPos[0],
        skel.ElbowJoint(kSkeletonRight).mJointPos[0],
        skel.HandJoint(kSkeletonRight).mJointPos[0]
    );

    // Compute angles for the left arm
    float leftAngle1 = std::atan2(
        leftElevation, (leftArmDir.z + leftArmDir.x) * negZero - leftArmDir.y
    );
    float leftAngle2 = std::atan2(leftElevation, leftForward);
    float leftBend = GetBend(
        skel.ShoulderJoint(kSkeletonLeft).mJointPos[0],
        skel.ElbowJoint(kSkeletonLeft).mJointPos[0],
        skel.HandJoint(kSkeletonLeft).mJointPos[0]
    );

    // Lean of the lateral axis away from vertical: Dot(lateral, yAxis), which
    // /fp:fast folds to `(x + z) * 0 + y`.  0x82DFE3A0-0x82DFE3BC loads
    // 0x70(r1) and 0x78(r1) (x and z) into the fadds and 0x74(r1) (y) into the
    // fmadds addend -- the axis is Y, not Z.
    float tiltAngle = std::acos((lateral.x + lateral.z) * 0.0f + lateral.y);

    // Wrap negative angles to [0, 2*PI]
    if (rightAngle1 < 0.0f) {
        rightAngle1 += 2.0f * PI;
    }
    if (rightAngle2 < 0.0f) {
        rightAngle2 += 2.0f * PI;
    }
    if (leftAngle1 < 0.0f) {
        leftAngle1 += 2.0f * PI;
    }
    if (leftAngle2 < 0.0f) {
        leftAngle2 += 2.0f * PI;
    }

    // 0x82DFE410-0x82DFE458: every failing test branches to one shared
    // `li r11, 0`, and the last test falls through to a `li r11, 0x1` that is
    // set up before it -- the positive `&&` chain, not an initialise-true /
    // conditionally-clear pair (which inverts all five branch polarities).
    bool rightInPose = rightAngle1 < 155.0f * DEG2RAD
        && rightAngle1 > 115.0f * DEG2RAD && rightAngle2 < 110.0f * DEG2RAD
        && rightAngle2 > 70.0f * DEG2RAD && rightBend < 35.0f * DEG2RAD;

    bool leftInPose = leftAngle1 < 200.0f * DEG2RAD && leftAngle1 > 160.0f * DEG2RAD
        && leftAngle2 < 2.0f * PI && leftAngle2 > 0.0f && leftBend < 110.0f * DEG2RAD;

    // If the left arm is not in pose, an untracked left hand or shoulder
    // excuses it (0x82DFE4A4-0x82DFE4D8).
    if (!leftInPose) {
        if (skel.HandJoint(kSkeletonLeft).mJointConf != kConfidenceNotTracked
            && skel.ShoulderJoint(kSkeletonLeft).mJointConf != kConfidenceNotTracked) {
            leftInPose = false;
        } else {
            leftInPose = true;
        }
    }

    // 0x82DFE4DC-0x82DFE4F4 materialises the tilt test into its own byte
    // (`li r11, 0x1` / `fcmpu` / `blt` / `li r11, 0x0`) BEFORE the three
    // `clrlwi.`+`beq` tests at 0x82DFE4F8-0x82DFE50C.  Written inline in the
    // `&&`, the compare short-circuits behind the two bools instead.
    bool tiltOk = tiltAngle < 9.0f * DEG2RAD;
    if (leftInPose && rightInPose && tiltOk) {
        inPose = true;
    }
    return inPose;
}

void HandInvokeGestureFilter::Update(const Skeleton &skel, int ms) {
    bool wasInvokeDetected = mInvokeDetected;
    float dt = (float)(double)(long long)ms * 0.001f;
    if (skel.IsTracked()) {
        bool bodyPlaneValid = UpdateBodyPlane(skel, dt);
        mInvokeDetected = false;
        if (bodyPlaneValid) {
            mInvokeDetected = CalcInPose(skel, dt);
            if (mInvokeDetected) {
                return;
            }
        }
        if (wasInvokeDetected) {
            return;
        }
    }
    mInvokeDetected = false;
    unk144 = 0;
}
