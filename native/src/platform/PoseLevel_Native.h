#pragma once
// Camera tilt for a sensor without an accelerometer (PLAN P-7, np-pose-tilt).
//
// The image levels every skeleton with the Kinect's own gravity reading:
// SkeletonFrame::Create (src/system/gesture/Skeleton.cpp) smooths
// NUI_SKELETON_FRAME::vNormalToGravity with sUpVectorSmoother (config
// kinect/up_vector_smoothing), stores it as mFloorNormal, and multiplies every
// joint by NuiTransformMatrixLevel(normal) before anything scores it
// (mJointPositions = levelled, mRawPositions = raw).  The native sensor builds
// SkeletonFrames directly and used to skip that step, so a webcam that looks
// down at the player reached the scorer as a player leaning toward the
// camera (np-video-calib: +6.9 deg +- 1.4 over the reference, every song).
//
// A webcam has no accelerometer, so this module estimates vNormalToGravity
// from the player's own posture and then runs Create's gravity step on the
// native frame, so the game levels the skeleton itself, as on a 360.
//
//   NativeGravityEstimator   the estimate: the ankles -> HipCenter axis over
//                            the last ~20 s of upright, facing, fully tracked
//                            frames, its median lean minus the reference's
//                            own (kPriorPitchDeg), re-estimated once a second
//                            and blended in slowly.  Held otherwise.
//   NativeLevelSensorFrame   Create's gravity step for a native frame.
//   NativeSensorLevel        the sensor's policy (DC3_POSE_LEVEL), called by
//                            GestureMgr_Native.cpp's BuildSensorFrame once per
//                            new frame.
//
// Which frames need it: only those from a camera that does not know its own
// gravity -- pose_server.py's packets (header flag), the web page's pushes,
// and the in-process performer's replays of recordings.  The performer's
// synthetic modes and synthetic_kinect.py play a Kinect, which is level.
#include "math/Vec.h"
#include "gesture/BaseSkeleton.h"
#include "platform/Skeleton_Native.h"

struct SkeletonFrame;

class NativeGravityEstimator {
public:
    // Measured, docs/native/POSE_INPUT.md "Camera tilt (np-pose-tilt)".
    // The Kinect reference choreography's own ankles -> HipCenter lean toward
    // the camera: median over the 17 story-mode songs' reference skeletons
    // (np-video-calib targets; per-song medians +0.5..+5.2, sd 1.3).  A levelled
    // Kinect dancer shows it, so the estimate keeps it rather than forcing 0.
    static constexpr float kPriorPitchDeg = 2.9f;
    // Posture gates for one sample (all must pass):
    static constexpr float kLeanGateDeg = 25.0f;  // axis within this of camera +y
    static constexpr float kSpineGateDeg = 20.0f; // HipCenter->ShoulderCenter vs the axis
    static constexpr float kKneeGateDeg = 25.0f;  // knee bend in the image plane
    static constexpr float kFacingMin = 0.8f;     // |shoulder line x| / |shoulder line|
    static constexpr int kWindow = 600;           // samples kept (~20 s at 30 Hz)
    static constexpr int kMinSamples = 150;       // before the first estimate (~5 s)
    static constexpr float kUpdatePeriodSecs = 1.0f;
    static constexpr float kBlend = 0.25f;        // per update: ~4 s time constant
    static constexpr float kMaxTiltDeg = 30.0f;   // estimates are clamped to this

    NativeGravityEstimator() { Reset(); }
    void Reset();

    // The lean of one person's camera-space posture: pitch toward the camera
    // (-z) and roll toward +x, degrees, of the ankles -> HipCenter axis.
    // False if the posture fails a gate (not upright, not facing, a joint the
    // axis needs is not kConfidenceTracked).
    static bool Lean(const Vector3 *joints, const JointConfidence *conf,
                     float &pitchDeg, float &rollDeg);
    // Offer one person of one sensor frame; true if it was kept.
    bool AddPerson(const Vector3 *joints, const JointConfidence *conf);
    // Advance the update clock by one sensor frame of `dtSecs`.  Once per
    // kUpdatePeriodSecs it re-estimates (when it has kMinSamples) and blends;
    // between updates the estimate is held.  True when the estimate changed.
    bool Tick(float dtSecs);

    bool HasEstimate() const { return mHave; }
    float PitchDeg() const { return mEstPitch; } // camera pitch: + looks down
    float RollDeg() const { return mEstRoll; }
    int NumSamples() const { return mCount; }
    // The camera-space up vector, unit: (0,1,0) until HasEstimate().
    Vector3 NormalToGravity() const;
    // The unit up vector whose pitch (atan2(-z, y)) and roll (atan2(x, y))
    // are the given angles.
    static Vector3 NormalFromPitchRoll(float pitchDeg, float rollDeg);

private:
    float mPitch[kWindow];
    float mRoll[kWindow];
    int mCount;
    int mHead;
    float mClock;
    bool mHave;
    float mEstPitch;
    float mEstRoll;
};

// SkeletonFrame::Create's gravity step on a native frame whose tracked slots
// hold raw camera-space joints in both mJointPositions and mRawPositions:
// smooth `normal` with the image's SkeletonFrame::sUpVectorSmoother over the
// frame's mElapsedMs, store the smoothed value as mFloorNormal, and multiply
// every tracked slot's mJointPositions and mHipCenter by
// NuiTransformMatrixLevel(smoothed).  mRawPositions stays raw, as in Create.
void NativeLevelSensorFrame(SkeletonFrame &frame, const Vector3 &normal);

// DC3_POSE_LEVEL: unset / "auto" / "1" = estimate (the default), "0" / "off"
// = never level (the frames reach the game exactly as before np-pose-tilt),
// a number in [-30, 30] = a fixed camera pitch in degrees (+ = the camera
// looks down), no estimate.
struct NativePoseLevelPolicy {
    enum Mode { kOff, kAuto, kFixed } mode = kAuto;
    float fixedPitchDeg = 0.0f;
};
NativePoseLevelPolicy NativePoseLevelParse(const char *env);

// The sensor's per-frame step (BuildSensorFrame, after the slots are filled,
// for every new frame including the no-provider dummy): with `unlevelled`
// (the frame came from a camera that does not know its gravity) the persons
// feed the estimator and the frame is levelled by the estimate (or by the
// fixed pitch); otherwise it is levelled by (0,1,0), the identity.  Policy
// kOff returns at once.  `dtSecs` is the frame's elapsed time.
void NativeSensorLevel(SkeletonFrame &frame, const NativeSkeletonProvider::PersonData *persons,
                       int numPersons, bool unlevelled, float dtSecs);

// Whether NativeSensorLevel levels frames at all (DC3_POSE_LEVEL not off).
bool NativeSensorLevelApplies();
// Level `frame` by a floor normal that was ALREADY smoothed (a recorded
// frame's mFloorNormal, PoseSession_Native.h): the smoother is forced to it,
// so the next live frame continues from there, and every tracked slot is
// levelled exactly as NativeLevelSensorFrame levelled it when it was recorded.
void NativeLevelSensorFrameAt(SkeletonFrame &frame, const Vector3 &floorNormal);
// Level `frame` by `floorNormal` and leave the smoother alone: a frame the
// pose fill delivers late (PoseFill_Native.h) is levelled by the normal its
// camera frame got when it arrived, and the smoother keeps following the
// camera.
void NativeLevelSensorFrameBy(SkeletonFrame &frame, const Vector3 &floorNormal);

// The sensor's estimator (read by tests and the HTTP debug surface).
const NativeGravityEstimator &NativeSensorGravityEstimator();
// Tests: replace the policy read from DC3_POSE_LEVEL and reset the estimator.
void NativeSensorLevelSetPolicy(const NativePoseLevelPolicy &policy);
