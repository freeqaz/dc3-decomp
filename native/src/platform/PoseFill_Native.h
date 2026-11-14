#pragma once
// Pose frame fill (DC3_POSE_FILL, np-pose-fill).
//
// The move scorer was tuned on a 30 Hz Kinect.  Once per sensor frame,
// MoveDir::PostUpdateFilters compares the skeleton with the choreography at
// song time S - latency, and MoveDir::EnqueueDetectFrames keeps per
// choreography frame the best (pose error + time error) over every sensor
// frame within reach (DetectFrame::AddError).  A webcam on a slow box gives
// fewer frames: the GPD's camera delivers ~8 a second, and at 8 Hz the
// nearest sensor frame is up to 60 ms from each choreography frame, so the
// time-error term alone costs a rating.  Measured (np-pose-rough, footage
// fixtures): 8 Hz scores 0.40 of the same footage at 30 Hz.
//
// The fill hands the game a frame every 1/30 s of song time between camera
// frames.  Modes:
//
//   extrap (the default)  Each camera frame at once, then stepped on by its
//                         joints' velocity (from the frame before) for up to
//                         one camera period, x DC3_POSE_FILL_GAIN (0..1,
//                         default kDefaultGain).  Deliberately generous: it
//                         scores above a real 30 Hz camera on the footage
//                         fixtures (owner decision 2026-11-14: over-credit,
//                         then calibrate down with the gain; the score-vs-gain
//                         table is in POSE_INPUT.md "Frame fill").  Guarded
//                         (ExtrapolateGuarded): a joint not tracked in either
//                         frame is not moved, a joint's velocity is clamped
//                         to kMaxJointSpeed, a person whose step looks like a
//                         left/right swap is held, nothing after one period
//                         (a stall stays a stall), nothing after a frame with
//                         nobody in it, no velocity across a camera gap.
//   interp                Linear interpolation between the two camera frames
//                         around the moment shown, so the whole stream runs
//                         one camera period (x1.2, Delay()) behind the camera.
//                         That delay is added to the scorer's latency
//                         (Dc3SetPoseFillDelaySeconds -> MoveDir's
//                         sLatencySeconds), so the scorer still compares each
//                         frame with the choreography of the moment it was
//                         captured.  Every camera frame is still delivered
//                         exactly; a fill frame only ever lies on the segment
//                         between two real ones, so it cannot overshoot and
//                         never scores above a real 30 Hz camera (0.92 at
//                         8 Hz), it is never later than the newest camera
//                         frame, a person is only interpolated when both
//                         camera frames around the fill have her, and nothing
//                         is filled between frames with nobody in them.
//   hold                  Re-send the newest camera frame (no delay).
//   extrapc               The prototype's step clamped: half a period at
//                         most, tracked joints only, 10 cm a joint.
//   off                   Camera frames only.
//
// No mode bridges a detection dropout: a frame without the dancer reaches
// the slot layer, and P-12's hold, as the camera's own frame, exactly as
// without the fill.
//
// Engaged only while the camera is slow: when the median of the last
// kWindow camera-frame intervals (song time) is over 1/kEngageHz, released
// when it falls under 1/kReleaseHz.  A 30 Hz source never engages it, so at
// 30 Hz every mode is the same stream as `off`.
//
// Fill frames are never recorded (DC3_POSE_RECORD writes camera frames as
// they arrive) and are not counted as pose_fps.
//
// This is the math and the schedule only; GestureMgr_Native.cpp's
// BuildSensorFrame owns the slots, levelling and recording.
#include "math/Vec.h"
#include "platform/Skeleton_Native.h"
#include <cstdint>

namespace Dc3PoseFill {

enum Mode { kOff, kHold, kExtrap, kExtrapClamped, kInterp };

// DC3_POSE_FILL: unset or empty = kExtrap (the default); "off" / "0";
// "hold"; "extrap"; "extrapc"; "interp".  Anything else is the default, and
// *ok (when given) is set false.
Mode ParseMode(const char *env, bool *ok = nullptr);
// DC3_POSE_FILL_GAIN: kExtrap's strength, 0 (hold) .. 1 (the full linear
// step).  Unset = kDefaultGain; anything else, or outside [0, 1], is the
// default and *ok is set false.
static const float kDefaultGain = 1.0f;
float ParseGain(const char *env, bool *ok = nullptr);
const char *ModeName(Mode m);

typedef NativeSkeletonProvider::PersonData Person;
static const int kMaxPersons = NativeSkeletonProvider::kMaxPersons;

// One camera frame as the sensor took it.
struct Source {
    double arrival = 0.0; // song seconds when the game took it
    double ts = -1.0;     // the camera's own stamp, seconds (-1 = none)
    uint32_t frameId = 0;
    int numPersons = 0;
    Person persons[kMaxPersons];
    Vector3 floorNormal;   // the normal it was levelled by when it arrived
    bool levelled = false; // whether it was levelled at all
};

// A frame to hand the game.
struct Output {
    int numPersons = 0;
    Person persons[kMaxPersons];
    double ts = -1.0;      // sensor time of what it shows (camera clock), -1 = none
    double shows = 0.0;    // the arrival time (song seconds) whose pose it shows
    Vector3 floorNormal;
    bool levelled = false;
    bool real = false;     // exactly a camera frame
};

// The math, exposed for the unit tests.
// `a` and `b` around the moment shown, alpha in [0, 1] from a towards b.
// Every person of `a` is copied; a person with the same tracking id (>= 0)
// valid in both is moved alpha of the way to b, joint by joint, where the
// joint is at least inferred in both (confidence = the lower of the two).
void Interpolate(const Source &a, const Source &b, float alpha, Output &out);
// Whether every valid person of `a` is valid in `b` under the same tracking
// id and the other way round: a fill is only made between such frames.
bool SamePeople(const Source &a, const Source &b);
// `last` stepped on by (last - prev) * a for every person valid in both.
// clamped: only joints tracked in both, and each joint moves at most
// kMaxStepMetres.
void Extrapolate(const Source &prev, const Source &last, float a, bool clamped, Output &out);
static const float kMaxStepMetres = 0.10f;
// kExtrap (the default): each person of `last` valid in `prev` is moved on
// by her joints' velocity between the two frames (over `period` seconds) for
// `seconds` x `gain`, where
//   - a joint not tracked in either frame is not moved (a guess);
//   - a joint's velocity is clamped to kMaxJointSpeed;
//   - a person whose step looks like a left/right swap (any L/R pair nearer
//     the other side's joint than its own by more than kSwapMargin) is not
//     moved at all, so a tracker flip is held, never amplified.
// The caller bounds `seconds` to one camera period.
void ExtrapolateGuarded(const Source &prev, const Source &last, float seconds, float period, float gain,
                        Output &out);
static const float kMaxJointSpeed = 4.0f; // m/s: p99.6 of the footage's joints at 8 Hz
static const float kSwapMargin = 0.5f;

class Filler {
public:
    static constexpr double kStep = 1.0 / 30.0;     // the Kinect's frame period
    static constexpr double kEngageHz = 24.0;       // engage below this camera rate
    static constexpr double kReleaseHz = 27.0;      // release above it
    static constexpr int kWindow = 15;              // camera intervals in the rate estimate
    static constexpr int kMinIntervals = 5;         // before the first decision
    static constexpr double kDelayFactor = 1.2;     // interp delay = this x the median interval
    static constexpr double kMaxDelay = 0.30;       // seconds
    static constexpr double kDelayHysteresis = 0.015;
    static constexpr double kGapFactor = 2.5;       // no fill across a camera gap longer than
    static constexpr double kMaxGap = 0.5;          //   this x the median, or this, seconds
    static constexpr double kHoldCap = 0.5;         // hold/extrap: no fill this long after a camera frame
    static constexpr int kBuffer = 8;

    void SetMode(Mode m);
    Mode GetMode() const { return mMode; }
    void SetGain(float g); // clamped to [0, 1]
    float Gain() const { return mGain; }
    void Reset();

    // A camera frame arrived (src.arrival = song seconds now).  Updates the
    // rate estimate; returns whether the fill is engaged for it.  When it is
    // not, the caller delivers the frame itself and need not Poll.
    bool Push(const Source &src);
    // The normal the newest camera frame was levelled by when it arrived
    // ((0,0,0) = not levelled); a late delivery is levelled by the same.
    void MarkLevel(const Vector3 &floorNormal);
    // A frame due at song time `song`, if any (at most one per call).
    bool Poll(double song, Output &out);

    bool Engaged() const { return mEngaged; }
    // The delay the stream runs behind the camera (interp; 0 otherwise), s.
    double Delay() const { return mEngaged && mMode == kInterp ? mDelay : 0.0; }
    double MedianInterval() const; // s, 0 before kMinIntervals
    unsigned Fills() const { return mFills; }
    unsigned Reals() const { return mReals; }
    // The elapsed sensor time of the newest camera frame, ms (the camera's
    // stamps when they advance, else song time), clamped to [1, 200]:
    // what the gravity estimate integrates when the frame arrives.
    int ArrivalElapsedMs() const { return mArrivalElapsedMs; }

private:
    int FindDue(double sigma) const; // newest source with arrival <= sigma, -1 if none
    void Drop(int n);                // drop the oldest n sources

    Mode mMode = kExtrap;
    float mGain = kDefaultGain;
    bool mEngaged = false;
    double mDelay = 0.0;
    double mIntervals[kWindow] = {};
    int mNumIntervals = 0, mIntervalHead = 0;
    Source mBuf[kBuffer];
    int mCount = 0;
    int mDelivered = -1;        // index in mBuf of the newest source delivered exactly
    bool mHaveOut = false;
    double mLastShows = 0.0;    // `shows` of the last output
    double mSigma = -1e30;      // interp: the newest moment shown (monotonic)
    int mArrivalElapsedMs = 33;
    unsigned mFills = 0, mReals = 0;
};

} // namespace Dc3PoseFill

// The scorer's side (Skeleton_Native.cpp): the fill's delay is added to the
// sensor latency MoveDir compares against (DC3_POSE_LATENCY_MS or
// scoring.dta's latency_offset), now and at every later LoadScoring.
void Dc3SetPoseFillDelaySeconds(float seconds);
float Dc3PoseFillDelaySeconds();

// GestureMgr_Native.cpp's filler.  Tests: force a mode (-1 = DC3_POSE_FILL
// again) and a song clock (nullptr = MoveDir::SongSeconds); resets it.
void NativePoseFillTestSet(int mode, double (*clock)());
const Dc3PoseFill::Filler &NativePoseFiller();
