#pragma once
// Joints the camera cannot see (DC3_POSE_ESTIMATE, np-cutoff, PLAN H-90..H-95).
//
// A webcam frames less of the player than a Kinect does: on the GPD's
// built-in camera the dancer's legs are below the frame for most of a song
// (MEASURED, owner YMCA session 2026-11-14: ankles 0-1 % and feet 0 % tracked
// for the first 40 s, knees 8-42 %).  MediaPipe still reports every joint; a
// joint below its visibility threshold arrives NotTracked (Skeleton_Native's
// ConfidenceFromScore), and FillSkeletonData then puts it at the slot's last
// position with confidence > 0 -- frozen there while the body moves -- or, if
// the slot never saw it, at the tracker's raw guess.  The decompiled move
// scorer reads positions only (no hamobj code reads joint confidence), so a
// frozen knee is scored as a knee that does not move.
//
// This step runs on each person just before the slot layer lays her into her
// skeleton slot (GestureMgr_Native.cpp FillPersonSlots), for camera frames
// and fill frames alike, so desktop (socket) and web (PoseInput_Web) share
// it.  It only touches joints that arrived NotTracked; every joint it writes
// is marked kConfidenceInferred, which is also what stops FillSkeletonData's
// freeze from replacing it.  Modes:
//
//   off        nothing (the joint hold freezes NotTracked joints, as before).
//   tracker    the tracker's own guess, marked Inferred: MediaPipe's
//              out-of-frame landmark instead of the frozen last position.
//   kinematic  (the default) legs and head completed from the visible body:
//              per-player bone lengths (learned from tracked frames, else
//              torso proportions measured on the footage fixtures).  Where
//              the tracker's guess implies a bone within 0.5-1.6x its length
//              the bone keeps the guess's direction at the learned length
//              (on cropped footage the guess is the best direction there is:
//              MEASURED, POSE_INPUT.md "Cut-off joints"); where it does not
//              (no guess, or a wild one) a floor the ankles stand on (from
//              the standing hip height) and a two-bone solve for the knee
//              that bends it forward when the hips drop.  The last tracked
//              pose of each joint is held relative to the body, decaying to
//              that estimate (kHoldSeconds).  Arms have no
//              kinematic answer: they get the hold, decaying to the tracker's
//              guess.  DC3_POSE_ESTIMATE_GAIN (0..1, default 1) blends from
//              the tracker's guess (0) to the completion (1).
//
// Frames the fill made (not real camera frames) are estimated but learn
// nothing, so the bone lengths and the floor come from the camera only.
#include "math/Vec.h"
#include "platform/Skeleton_Native.h"

namespace Dc3PoseEstimate {

enum Mode { kOff, kTracker, kKinematic };

// DC3_POSE_ESTIMATE: unset or empty = kKinematic; "off" / "0"; "tracker";
// "kinematic".  Anything else is the default and *ok is set false.
Mode ParseMode(const char *env, bool *ok = nullptr);
static const float kDefaultGain = 1.0f;
// DC3_POSE_ESTIMATE_GAIN: 0..1, unset = kDefaultGain; anything else is the
// default and *ok is set false.
float ParseGain(const char *env, bool *ok = nullptr);
const char *ModeName(Mode m);

typedef NativeSkeletonProvider::PersonData Person;

// Proportions of a body, in units of the torso (hip centre to shoulder
// centre), MEASURED as medians over the 17 footage fixtures' tracked frames
// (np-cutoff, 88,071 frames, docs/native/POSE_INPUT.md "Cut-off joints").
static const float kThighPerTorso = 1.07f;
static const float kShinPerTorso = 0.96f;
static const float kFootPerTorso = 0.205f;
static const float kHeadPerTorso = 0.43f;
// Hip centre above the lower ankle when standing, along the body's up: the
// fixtures' p90 (0.846 m over a 0.383 m torso).
static const float kStandHipPerTorso = 2.2f;
// How long a joint's last tracked pose (relative to the body) is held before
// it has decayed to 1/e of the way to the estimate, seconds.
static const float kHoldSeconds = 0.35f;
static const float kDefaultTorso = 0.38f; // metres, before any torso is seen

// Two-bone solve: from `root`, bones of length a then b, the middle joint
// bent towards `bendDir`, the end as near `target` as the bones reach.
// Writes the middle and end joints; returns false (and straightens the limb
// towards the target) when the target is out of reach.
bool TwoBone(const Vector3 &root, const Vector3 &target, float a, float b, const Vector3 &bendDir,
             Vector3 &mid, Vector3 &end);

class Estimator {
public:
    static const int kSlots = 6;

    void SetMode(Mode m) { mMode = m; }
    Mode GetMode() const { return mMode; }
    void SetGain(float g) { mGain = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g); }
    float Gain() const { return mGain; }
    void Reset();
    // A new occupant in the slot: forget the held joints and the body (the
    // floor stays: the camera has not moved).
    void ResetSlot(int slot);

    // In place.  `seconds` = sensor time since the slot's previous frame;
    // `learn` = a real camera frame (bone lengths, floor, holds update).
    // Returns how many joints it wrote.
    int Apply(int slot, Person &p, float seconds, bool learn);

    // State, for tests and the log.
    float Torso(int slot) const { return mBody[slot].torso; }
    float Thigh(int slot, int side) const { return mBody[slot].thigh[side]; }
    float Shin(int slot, int side) const { return mBody[slot].shin[side]; }
    bool HaveFloor(int slot) const { return mBody[slot].haveStand; }
    // Height of the floor along the body's up (camera metres), valid when
    // HaveFloor.
    float FloorUp(int slot) const;
    unsigned Estimated() const { return mEstimated; }

private:
    struct Body {
        float torso = kDefaultTorso;
        int torsoSamples = 0;
        float thigh[2] = {0, 0}, shin[2] = {0, 0}, foot[2] = {0, 0}; // metres, 0 = not seen
        float standHip = 0.0f;   // learned hip-above-ankle when standing, 0 = not seen
        Vector3 up;              // slow body up (camera space)
        bool haveUp = false;
        bool haveStand = false;
        float hipStand = 0.0f;   // standing hip height along up (a slow peak follower)
        // held joints: offset from the anchor in the body frame (r, u, f)
        Vector3 held[kNumJoints];
        float age[kNumJoints];
        bool haveHeld[kNumJoints];
        Body() {
            for (int j = 0; j < kNumJoints; j++) {
                age[j] = 0.0f;
                haveHeld[j] = false;
            }
        }
    };
    Mode mMode = kKinematic;
    float mGain = kDefaultGain;
    Body mBody[kSlots];
    unsigned mEstimated = 0;
};

} // namespace Dc3PoseEstimate

// GestureMgr_Native.cpp's estimator (DC3_POSE_ESTIMATE / _GAIN, read once).
// Tests: force a mode (-1 = the environment again) and a gain (< 0 = the
// environment); resets it.
void NativePoseEstimateTestSet(int mode, float gain);
const Dc3PoseEstimate::Estimator &NativePoseEstimator();
