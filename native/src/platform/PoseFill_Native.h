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
// Engaged only while the camera is slow: when the trimmed mean of the last
// kWindow camera periods (RateInterval) is over 1/kEngageHz; released once it
// has stayed under 1/kReleaseHz for kReleaseDwell seconds.  A 30 Hz source
// never engages it, so at 30 Hz every mode is the same stream as `off`.  Not
// the median: a camera between the two thresholds delivers a mix of one- and
// two-frame intervals of a 30 Hz grab (33 / 50 ms), and the median of such a
// window jumps from one to the other as each frame enters it, across both
// thresholds -- 349 engage/release changes in one web song (MEASURED,
// np-web4).  The median still sets the interp delay and the gap limit.
//
// The rate is the camera's own (np-pose-steady).  A camera period is the
// difference of two frames' stamps (Source::ts) over the frames the camera
// delivered between them (Source::frames) -- not the song time between the
// frames the game took (Source::arrival), which it was.  The game polls once
// a frame and takes the newest camera frame, so its takes are whole game
// frames apart whatever the camera does, and a game running near the
// camera's rate read as a slow camera.  MEASURED on an M3 MacBook Air on a
// 30 Hz display (the game at about 31 fps): the pose server sent 29.9-30.4
// poses a second in every 5 s window and the fill engaged 19 times in 401 s,
// each time at "camera 22.5 frames/s" -- six two-game-frame gaps among
// fifteen takes.  The stamps are used while every interval of the window has
// them and the camera's clock and the fill's kept pace (RateIsTheCamerasOwn);
// otherwise the takes decide, as before.  DC3_POSE_FILL_RATE=arrival: the
// takes always.
//
// The release dwell (np-pose-steady).  A tracker whose rate wanders about
// the thresholds changed the fill every few seconds.  MEASURED on the same
// Mac, 1019 s with the tracker slowing for stretches: 26 engagements, 19 of
// the 23 spans under 5 s.  Each change is a change of how the motion between
// two camera frames is made.  The fill lets go only after kReleaseDwell
// seconds over the release rate without a break; engaging is unchanged, so
// it is engaged wherever it was before.  Modelled cameras wandering between
// 18 and 29 Hz: 6-30 changes a minute with no dwell, one engagement in five
// minutes with 6 s or more; the Mac's recorded tracker rates: 13 changes
// with none, 7 with 10 s (PoseFillDwellTest prints the table).
// DC3_POSE_FILL_DWELL=0: no dwell.
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
// DC3_POSE_FILL_MENU: the mode outside a song ("same" = DC3_POSE_FILL's).
//
// Outside a song (np-menu-gestures).  The fill exists for the move scorer,
// and the default mode is generous to it on purpose.  The menus read the same
// skeleton with the image's gesture detectors (the hand's height picks the
// row, a forearm sweep selects it), and for those an extrapolated frame is a
// hand the player never held out.  MEASURED, an 8 Hz camera:
//
//   real footage -- 113 s around the 7 gesture selects of two recorded GPD
//   playtests (7-11 camera frames/s), replayed in front of the main menu;
//   selects, the right hand's list + the left's:
//     extrap   4+0, 3+0, 2+0, 2+0  (the left hand had its list 22-26 s)
//     hold     3+0, 3+0
//     off      4+3, 4+3            (the left hand had its list 39 s)
//     interp   5+2, 4+1
//   Three of the 7 were made on the pause menu, where the song clock stands
//   still and so no fill was being made when they were recorded: no run with
//   extrap or hold repeats any of the three, both runs with off repeat all.
//
//   the synthetic performer (scripts/menu_gesture_bench.py), joint noise at
//   the GPD's level, 4 lists x 30 selects x 3 seeds; selected at the first
//   sweep / within three / a hand resting on a row 5 s saw the highlight move:
//     extrap   330/360 (92 %)   359/360   12/96
//     off      310/360 (86 %)   358/360    0/96
//     interp   224/240 (93 %)   240/240    1/64   (0.2-0.3 s slower: its delay)
//     hold     196/240 (82 %)   237/240    3/64
//
// So the two disagree about off against extrap at the first sweep (the
// performer's sweep is cleaner than a person's), agree that hold is worst, and
// only the footage is of people.  The default is off: the menus get the
// camera's own frames, which is what they already got whenever the song clock
// stood still.  interp is there to try (it needs the StallClock below: on a
// stopped clock it delivers nothing at all).  docs/native/reference/
// MENU_GESTURES.md has the tables.
static const Mode kMenuDefaultMode = kOff;
float ParseGain(const char *env, bool *ok = nullptr);
const char *ModeName(Mode m);
// DC3_POSE_FILL_RATE: what the camera's rate is read from (Filler::
// RateInterval).  Unset, empty or "camera" = kRateCamera, the camera's own
// stamps; "arrival" = kRateArrival, the time between the frames the game
// took, as before np-pose-steady (for an A/B).  Anything else is the default
// and *ok is set false.
enum RateSource { kRateCamera, kRateArrival };
RateSource ParseRateSource(const char *env, bool *ok = nullptr);
// DC3_POSE_FILL_DWELL: seconds the camera must have stayed over the release
// rate before the fill lets go (Filler::kReleaseDwell).  Unset or empty = the
// default; 0 = release with the first reading over it, as before
// np-pose-steady.  Anything else, or outside [0, kMaxReleaseDwell], is the
// default and *ok is set false.
static const double kMaxReleaseDwell = 60.0;
double ParseReleaseDwell(const char *env, bool *ok = nullptr);

typedef NativeSkeletonProvider::PersonData Person;
static const int kMaxPersons = NativeSkeletonProvider::kMaxPersons;

// One camera frame as the sensor took it.
struct Source {
    double arrival = 0.0; // song seconds when the game took it
    double ts = -1.0;     // the camera's own stamp, seconds (-1 = none)
    uint32_t frameId = 0;
    // Camera frames the source delivered since the frame before this one, this
    // one counted (NativeSkeletonProvider::Delivered).  More than 1: frames
    // that reached the game between two of its polls, of which it took only
    // the newest.
    int frames = 1;
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
//
// A person not valid in `prev` is held -- unless `earlier` has her: her last
// camera frame before `prev` (gpd-menubind: one missed frame used to cost the
// fill its velocity for her whole next period).  Her velocity is then over
// the span from that frame to `last` (its stamps, else its arrivals); the
// caller only passes frames within the camera-gap limit.
struct Earlier {
    Person person;
    double ts = -1.0, arrival = 0.0;
};
void ExtrapolateGuarded(const Source &prev, const Source &last, float seconds, float period, float gain,
                        Output &out, const Earlier *earlier = nullptr, int numEarlier = 0);
// DC3_POSE_FILL_VELOCITY: `seen` (the default) takes a person's velocity from
// her last camera frame within the gap limit; `prev` only from the frame
// before, the behaviour before gpd-menubind.
bool VelocityFromLastSeen();
static const float kMaxJointSpeed = 4.0f; // m/s: p99.6 of the footage's joints at 8 Hz
static const float kSwapMargin = 0.5f;

class Filler {
public:
    static constexpr double kStep = 1.0 / 30.0;     // the Kinect's frame period
    static constexpr double kEngageHz = 24.0;       // engage below this camera rate
    static constexpr double kReleaseHz = 27.0;      // release above it ...
    static constexpr double kReleaseDwell = 10.0;   // ... once it has stayed there this long, seconds
    static constexpr int kWindow = 15;              // camera intervals in the rate estimate
    static constexpr int kMinIntervals = 5;         // before the first decision
    static constexpr double kDelayFactor = 1.2;     // interp delay = this x the median interval
    static constexpr double kMaxDelay = 0.30;       // seconds
    static constexpr double kDelayHysteresis = 0.015;
    static constexpr double kGapFactor = 2.5;       // no fill across a camera gap longer than
    static constexpr double kMaxGap = 0.5;          //   this x the median, or this, seconds
    static constexpr double kHoldCap = 0.5;         // hold/extrap: no fill this long after a camera frame
    static constexpr int kBuffer = 8;
    // The rate from the camera's stamps (RateInterval):
    static constexpr int kMaxUntaken = 64;          // Source::frames beyond this is not believed (taken as 1)
    static constexpr double kPace = 1.5;            // the two clocks keep pace within this factor

    void SetMode(Mode m);
    // The same, keeping what is known of the camera: its rate window and --
    // unless the new mode is kOff -- whether the fill is engaged.  The frames
    // buffered are dropped (they were laid out for the other mode), so the
    // stream picks up with the next camera frame.  For the change between a
    // song and the menus (DC3_POSE_FILL_MENU), which must not cost the song
    // the kMinIntervals camera frames a Reset takes to engage again.
    void SetModeKeepingRate(Mode m);
    Mode GetMode() const { return mMode; }
    void SetGain(float g); // clamped to [0, 1]
    float Gain() const { return mGain; }
    void SetRateSource(RateSource s) { mRateSource = s; }
    RateSource GetRateSource() const { return mRateSource; }
    // Seconds over the release rate before a release; clamped to
    // [0, kMaxReleaseDwell].  0 = at the first reading over it.
    void SetReleaseDwell(double seconds);
    double ReleaseDwell() const { return mReleaseDwell; }
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
    // The camera's rate for engage/release: the mean of the window's
    // camera periods without its shortest and longest fifth, s; 0 before
    // kMinIntervals.  Not the median: see the file comment.
    //
    // A camera period is the camera's own: the difference of two frames'
    // stamps over the frames the camera delivered between them
    // (Source::frames) -- not the time between the game's takes, which is a
    // whole number of game frames whatever the camera does.  That holds while
    // RateIsTheCamerasOwn(); otherwise it is the time between the takes, as
    // it was before np-pose-steady.
    double RateInterval() const;
    // Whether RateInterval() is read from the camera's stamps: kRateCamera,
    // every interval in the window has advancing stamps under a second apart,
    // and over the window the camera's clock and the fill's ran at the same
    // pace (within kPace).  They do not under DC3_FAST_TIME with a camera on
    // the wall clock, or on a slowed song; what the scorer gets is frames a
    // song second, so there the takes decide.
    bool RateIsTheCamerasOwn() const;
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
    RateSource mRateSource = kRateCamera;
    double mReleaseDwell = kReleaseDwell;
    double mFastFor = 0.0; // engaged: song seconds the rate has read over kReleaseHz without a break
    // One entry per camera-frame interval, the three in step:
    double mIntervals[kWindow] = {};  // song time between the two takes (the median's)
    double mStampSpans[kWindow] = {}; // the camera's stamps across them, 0 = none
    double mPeriods[kWindow] = {};    // ... over the frames the camera delivered
    int mNumIntervals = 0, mIntervalHead = 0;
    Source mBuf[kBuffer];
    int mCount = 0;
    int mDelivered = -1;        // index in mBuf of the newest source delivered exactly
    bool mHaveOut = false;
    double mLastShows = 0.0;    // `shows` of the last output
    double mSigma = -1e30;      // interp: the newest moment shown (monotonic)
    int mArrivalElapsedMs = 33;
    unsigned mFills = 0, mReals = 0;
    // Each person's newest camera frame from before the newest-but-one
    // (ExtrapolateGuarded's `earlier`); kept as the frames are delivered.
    Earlier mSeen[kMaxPersons];
    int mNumSeen = 0;
    void NoteSeen(const Source &s);
};

// The filler's clock outside a song (np-menu-gestures).
//
// The filler runs on the song clock (MoveDir::SongSeconds): a camera frame's
// arrival and every Poll are song times, and a fill is made only once that
// clock has moved a Kinect frame on.  While a song plays that is the clock the
// scorer runs on.  Outside one it is whatever drives TaskMgr's seconds at the
// moment -- the shell music, a song preview -- and that STOPS: on the pause
// menu, between previews, on the results screens.  MEASURED on two playtest
// recordings from the GPD (7-11 camera frames a second): outside the songs the
// song clock had not moved across 283 of 749 (38 %) and 992 of 2111 (47 %)
// camera-frame intervals.  With the clock still, extrap and hold make no fill
// and interp (whose stream runs behind the clock) delivers nothing at all,
// camera frames included (PoseFillStallClockTest: 2 camera frames in 3 s).
// The menus therefore got a stream that changed character every time a
// preview ended.  MEASURED with the synthetic performer at 8 Hz, no noise, on
// the left hand's Back (the song list, where the clock stops with every
// preview), DC3_POSE_FILL=extrap: selected within three sweeps in 56 of 73
// tries on the song clock -- 16 of the 17 misses in three runs of 5, 6 and 5
// tries on end, the arc detector left on one node -- and in 90 of 90, each at
// the first sweep, on this one.
//
// StallClock is the song clock while it moves.  Once it has stood still for
// kStall of UI time it carries on at the UI clock's pace (which never stops:
// the menus animate on it), and goes back to following the song clock, from
// wherever it then is, the moment that moves again.  The song clock's own
// steps -- forward, or back for a new song -- pass through unchanged, so
// inside a song this is the song clock plus a constant, and the filler's
// schedule there is what it was.  kStall keeps a clock that merely repeats a
// value for a frame or two (an audio clock read twice between its updates)
// from being counted twice.
class StallClock {
public:
    static constexpr double kStall = 0.25;
    // `song` and `ui` in seconds; call with the current values (any number of
    // times a frame: with no UI time passed nothing changes).
    double Read(double song, double ui) {
        if (!mHave) {
            mHave = true;
            mSong = song;
            mUi = ui;
        }
        double dUi = ui - mUi;
        if (dUi < 0.0 || dUi > 1.0)
            dUi = 0.0; // the UI clock was set, or a second-long hitch: not time to make up
        if (song != mSong) {
            mStalled = 0.0;
        } else if (dUi > 0.0) {
            if (mStalled >= kStall)
                mCarried += dUi;
            else
                mStalled += dUi;
        }
        mSong = song;
        mUi = ui;
        return song + mCarried;
    }
    bool Carrying() const { return mStalled >= kStall; }
    double Carried() const { return mCarried; } // seconds added so far
    void Reset() { *this = StallClock(); }

private:
    bool mHave = false;
    double mSong = 0.0, mUi = 0.0;
    double mStalled = 0.0; // UI seconds the song clock has stood still (up to kStall)
    double mCarried = 0.0;
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
// Seconds the filler's clock has been carried on at UI time while the song
// clock stood still (StallClock); 0 under DC3_POSE_FILL_CLOCK=song.
double NativePoseFillClockCarried();
