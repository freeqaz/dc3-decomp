// DC3_POSE_FILL (native/src/platform/PoseFill_Native.h, np-pose-fill): the
// math and the schedule of the frames the sensor makes between a slow
// camera's.  No game, no GPU, no socket.
//
//   PoseFillMathTest      Interpolate stays on the segment between the two
//                         camera frames (it cannot overshoot), only moves a
//                         person both frames have, never draws a line to an
//                         untracked joint; Extrapolate does overshoot, and
//                         its clamped form is bounded; ExtrapolateGuarded (the
//                         default) scales by the gain, clamps the speed, does
//                         not move guesses and holds a left/right swap.
//                         Sabotaged (each caught): no swap guard, no speed
//                         clamp (GuardedExtrapolation...), no one-period
//                         horizon (ExtrapStopsOnePeriod...), filling after a
//                         frame with nobody (NoFillAcrossADropout... and the
//                         sensor's hold test, 8 -> 32 held frames).
//   PoseFillScheduleTest  Filler: inert at 30 Hz; at 8 Hz a frame every
//                         1/30 s, each camera frame delivered exactly once,
//                         in order, a fixed delay behind its arrival, nothing
//                         past the newest camera frame (a stall stays a
//                         stall), no fill across a dropout or a long gap.
//                         AModeChangeKeepsTheCameraRate (np-menu-gestures,
//                         DC3_POSE_FILL_MENU: the mode changes between a song
//                         and the menus): SetModeKeepingRate keeps the rate
//                         window, so the song's fill is engaged with the
//                         first camera frame back.  Sabotaged (each caught):
//                         it calls Reset() (the rate is lost: 4 failures); it
//                         keeps the buffered frames (a frame laid out for
//                         extrap comes out of interp); it leaves `off`
//                         engaged.
//   PoseFillRateTest      np-pose-steady: the camera's rate is its own stamps
//                         over the frames it delivered, so a 30 Hz camera
//                         under a game loop at 30-60 fps never engages
//                         (prints engagements before -> now); a slow camera
//                         engages with the same frame as before; a stream
//                         without stamps, or on another clock's pace, is read
//                         by the game's takes.  Sabotaged (each caught): the
//                         rate read from the takes; the stamp span not
//                         divided by the frames delivered; no pace check; an
//                         unbelievable frame count believed; a window with an
//                         unstamped frame still using the stamps (that one
//                         SURVIVED the first set of cases and got its own).
//   PoseFillDwellTest     the release dwell: the fill lets go only after
//                         kReleaseDwell over the release rate, a break starts
//                         the wait again, `off` is immediate, a wandering
//                         camera changes far less often and is engaged
//                         wherever it was before (prints the table the value
//                         was picked from).  Sabotaged (each caught): no
//                         dwell; a break not starting the wait again; the
//                         dwell holding the fill through the change to off;
//                         counting takes instead of seconds.
//   PoseFillLatencyTest  the delay reaches the scorer's latency, on top of
//                         whichever sensor latency is in force.
//   PoseFillStallClockTest  StallClock (np-menu-gestures): the fill's clock
//                         carries on at UI time while the song clock stands
//                         still (the pause menu, between previews) and is the
//                         song clock otherwise.  The last test drives the real
//                         Filler: an 8 Hz camera, the song clock stopped --
//                         0 fills on the song clock, a stream on the
//                         StallClock.  Sabotaged: Read returning `song` alone
//                         (no carry) fails AStoppedSongClock... and
//                         AnEightHertzCamera...; carrying with no kStall wait
//                         fails AValueRepeated... and two more; carrying while
//                         the song clock moves fails AMovingSongClock...,
//                         TheSongClockGoingBack... and two more.
//
// The sensor end (slots, P-12's hold, the recorder) is PoseFillSensorTest in
// test_native_pose_socket.cpp.

#include <gtest/gtest.h>

#include "platform/PoseFill_Native.h"
#include "platform/Skeleton_Native.h" // Dc3SensorLatencySeconds, Dc3DeclareSourceLatencyMs
#include "hamobj/MoveDir.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Dc3PoseFill;


namespace {

// One person, every joint at (x, 1 + j/10, 2), all tracked.
Person MakePerson(int trackId, float x) {
    Person p;
    p.trackId = trackId;
    p.valid = true;
    for (int j = 0; j < kNumJoints; j++) {
        p.joints[j].Set(x, 1.0f + j * 0.1f, 2.0f);
        p.confidence[j] = kConfidenceTracked;
    }
    return p;
}

Source MakeSource(double arrival, double ts, std::vector<Person> people) {
    Source s;
    s.arrival = arrival;
    s.ts = ts;
    s.numPersons = (int)people.size();
    for (size_t i = 0; i < people.size(); i++)
        s.persons[i] = people[i];
    s.floorNormal.Set(0.0f, 1.0f, 0.0f);
    s.levelled = true;
    return s;
}

} // namespace

TEST(PoseFillMathTest, TheModeDefaultsToExtrapAndNamesParse) {
    bool ok = false;
    EXPECT_EQ(ParseMode(nullptr, &ok), kExtrap);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseMode("", &ok), kExtrap);
    EXPECT_EQ(ParseMode("interp", &ok), kInterp);
    EXPECT_EQ(ParseMode("off", &ok), kOff);
    EXPECT_EQ(ParseMode("0", &ok), kOff);
    EXPECT_EQ(ParseMode("hold", &ok), kHold);
    EXPECT_EQ(ParseMode("extrap", &ok), kExtrap);
    EXPECT_EQ(ParseMode("extrapc", &ok), kExtrapClamped);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseMode("cubic", &ok), kExtrap);
    EXPECT_FALSE(ok);
    EXPECT_EQ(Filler().GetMode(), kExtrap);
}

TEST(PoseFillMathTest, TheGainParsesWithinZeroToOne) {
    bool ok = false;
    EXPECT_FLOAT_EQ(ParseGain(nullptr, &ok), kDefaultGain);
    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(ParseGain("0.5", &ok), 0.5f);
    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(ParseGain("0", &ok), 0.0f);
    EXPECT_FLOAT_EQ(ParseGain("1", &ok), 1.0f);
    EXPECT_TRUE(ok);
    for (const char *bad : { "1.5", "-0.1", "half", "0.5x", "nan" }) {
        EXPECT_FLOAT_EQ(ParseGain(bad, &ok), kDefaultGain) << bad;
        EXPECT_FALSE(ok) << bad;
    }
    Filler f;
    EXPECT_FLOAT_EQ(f.Gain(), kDefaultGain);
    f.SetGain(3.0f);
    EXPECT_FLOAT_EQ(f.Gain(), 1.0f);
    f.SetGain(-1.0f);
    EXPECT_FLOAT_EQ(f.Gain(), 0.0f);
}

TEST(PoseFillMathTest, GuardedExtrapolationScalesClampsAndHoldsGuessesAndSwaps) {
    // 0.125 s apart, every joint moved 0.1 m in x: 0.8 m/s.
    Source prev = MakeSource(0, 0, { MakePerson(7, 0.0f) });
    Source last = MakeSource(0.125, 0.125, { MakePerson(7, 0.1f) });
    Output out;
    ExtrapolateGuarded(prev, last, 0.1f, 0.125f, 1.0f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.1f + 0.08f, 1e-5f) << "v * t";
    ExtrapolateGuarded(prev, last, 0.1f, 0.125f, 0.5f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.1f + 0.04f, 1e-5f) << "x gain";
    ExtrapolateGuarded(prev, last, 0.1f, 0.125f, 0.0f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.1f, 1e-6f) << "gain 0 is hold";

    // An absurd step (2 m in 0.125 s = 16 m/s) is clamped to kMaxJointSpeed.
    Source far = MakeSource(0.125, 0.125, { MakePerson(7, 2.0f) });
    ExtrapolateGuarded(prev, far, 0.1f, 0.125f, 1.0f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHead].x, 2.0f + kMaxJointSpeed * 0.1f, 1e-4f);

    // A joint not tracked in either frame is a guess: not moved.
    Source guess = last;
    guess.persons[0].confidence[kJointHead] = kConfidenceNotTracked;
    ExtrapolateGuarded(prev, guess, 0.1f, 0.125f, 1.0f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHead].x, 0.1f, 1e-6f);
    EXPECT_NEAR(out.persons[0].joints[kJointSpine].x, 0.18f, 1e-5f) << "the rest still moves";
    // Inferred joints do move (they are the image's, just less sure).
    guess.persons[0].confidence[kJointHead] = kConfidenceInferred;
    ExtrapolateGuarded(prev, guess, 0.1f, 0.125f, 1.0f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHead].x, 0.18f, 1e-5f);

    // A left/right swap: hands and wrists 0.6 m apart trade places.  Not
    // amplified: the whole person is held where the camera put her.
    Person a = MakePerson(7, 0.0f), b = MakePerson(7, 0.01f);
    const int L[] = { kJointWristLeft, kJointHandLeft }, Rt[] = { kJointWristRight, kJointHandRight };
    for (int k = 0; k < 2; k++) {
        a.joints[L[k]].Set(-0.3f, 1.5f, 2.0f); // a pair side by side
        a.joints[Rt[k]].Set(0.3f, 1.5f, 2.0f);
        b.joints[L[k]].Set(0.3f, 1.5f, 2.0f);
        b.joints[Rt[k]].Set(-0.3f, 1.5f, 2.0f);
    }
    Source sa = MakeSource(0, 0, { a }), sb = MakeSource(0.125, 0.125, { b });
    ExtrapolateGuarded(sa, sb, 0.125f, 0.125f, 1.0f, out);
    for (int j = 0; j < kNumJoints; j++)
        EXPECT_FLOAT_EQ(out.persons[0].joints[j].x, b.joints[j].x) << "joint " << j;
    // ... while the same hands crossing a little are not a swap.
    for (int k = 0; k < 2; k++) {
        b.joints[L[k]].Set(-0.1f, 1.5f, 2.0f);
        b.joints[Rt[k]].Set(0.1f, 1.5f, 2.0f);
    }
    sb = MakeSource(0.125, 0.125, { b });
    ExtrapolateGuarded(sa, sb, 0.125f, 0.125f, 1.0f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.1f, 1e-5f) << "-0.1 + 0.2";

    // A person not in the earlier frame is copied, not moved.
    Source other = MakeSource(0, 0, { MakePerson(8, 0.0f) });
    ExtrapolateGuarded(other, last, 0.1f, 0.125f, 1.0f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.1f, 1e-6f);
}

TEST(PoseFillMathTest, InterpolationStaysOnTheSegmentBetweenTheTwoFrames) {
    // A sweep of awkward pairs: every coordinate of every output joint lies
    // between the two camera frames' values, for every alpha (and alpha
    // outside [0, 1] is clamped onto the segment).
    uint32_t seed = 12345;
    auto rnd = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f;
    };
    for (int trial = 0; trial < 200; trial++) {
        Person pa = MakePerson(7, 0.0f), pb = MakePerson(7, 0.0f);
        for (int j = 0; j < kNumJoints; j++) {
            pa.joints[j].Set(rnd(), rnd(), 2.0f + rnd());
            pb.joints[j].Set(rnd(), rnd(), 2.0f + rnd());
        }
        Source a = MakeSource(0.0, 0.0, { pa }), b = MakeSource(0.125, 0.125, { pb });
        for (float alpha : { -0.5f, 0.0f, 0.1f, 0.33f, 0.5f, 0.77f, 1.0f, 1.7f }) {
            Output out;
            Interpolate(a, b, alpha, out);
            ASSERT_EQ(out.numPersons, 1);
            for (int j = 0; j < kNumJoints; j++) {
                const Vector3 &x = pa.joints[j], &y = pb.joints[j], &o = out.persons[0].joints[j];
                const float c[3][3] = { { x.x, y.x, o.x }, { x.y, y.y, o.y }, { x.z, y.z, o.z } };
                for (int k = 0; k < 3; k++) {
                    float lo = std::fmin(c[k][0], c[k][1]) - 1e-6f;
                    float hi = std::fmax(c[k][0], c[k][1]) + 1e-6f;
                    ASSERT_GE(c[k][2], lo) << "trial " << trial << " alpha " << alpha << " joint " << j;
                    ASSERT_LE(c[k][2], hi) << "trial " << trial << " alpha " << alpha << " joint " << j;
                }
            }
        }
    }
    // And the midpoint is the midpoint.
    Source a = MakeSource(0, 0, { MakePerson(7, 0.0f) }), b = MakeSource(0.1, 0.1, { MakePerson(7, 0.4f) });
    Output out;
    Interpolate(a, b, 0.5f, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.2f, 1e-6f);
}

TEST(PoseFillMathTest, OnlyAPersonInBothFramesMovesAndUntrackedJointsAreNotDrawnTowards) {
    Person a7 = MakePerson(7, 0.0f), a8 = MakePerson(8, -1.0f);
    Person b7 = MakePerson(7, 1.0f), b9 = MakePerson(9, 3.0f);
    b7.confidence[kJointHandLeft] = kConfidenceNotTracked; // lost in b: a guess
    b7.confidence[kJointElbowLeft] = kConfidenceInferred;
    a7.confidence[kJointKneeLeft] = kConfidenceNotTracked; // lost in a
    Source a = MakeSource(0, 0, { a7, a8 }), b = MakeSource(0.1, 0.1, { b7, b9 });
    Output out;
    Interpolate(a, b, 0.5f, out);
    ASSERT_EQ(out.numPersons, 2) << "the persons of the earlier frame";
    EXPECT_EQ(out.persons[0].trackId, 7);
    EXPECT_NEAR(out.persons[0].joints[kJointHead].x, 0.5f, 1e-6f);
    EXPECT_EQ(out.persons[0].confidence[kJointHead], kConfidenceTracked);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.0f, 1e-6f) << "untracked in b: kept";
    EXPECT_EQ(out.persons[0].confidence[kJointHandLeft], kConfidenceTracked);
    EXPECT_NEAR(out.persons[0].joints[kJointKneeLeft].x, 0.0f, 1e-6f) << "untracked in a: kept";
    EXPECT_EQ(out.persons[0].confidence[kJointKneeLeft], kConfidenceNotTracked);
    EXPECT_NEAR(out.persons[0].joints[kJointElbowLeft].x, 0.5f, 1e-6f);
    EXPECT_EQ(out.persons[0].confidence[kJointElbowLeft], kConfidenceInferred) << "the lower";
    EXPECT_EQ(out.persons[1].trackId, 8);
    EXPECT_NEAR(out.persons[1].joints[kJointHead].x, -1.0f, 1e-6f) << "not in b: not moved";
    EXPECT_FALSE(SamePeople(a, b));
    EXPECT_TRUE(SamePeople(MakeSource(0, 0, { a7, a8 }), MakeSource(0, 0, { a8, b7 })));
    EXPECT_FALSE(SamePeople(MakeSource(0, 0, { a7 }), MakeSource(0, 0, {})));
    EXPECT_FALSE(SamePeople(MakeSource(0, 0, {}), MakeSource(0, 0, { a7 })));
    EXPECT_TRUE(SamePeople(MakeSource(0, 0, {}), MakeSource(0, 0, {})));
}

TEST(PoseFillMathTest, ExtrapolationOvershootsAndItsClampedFormIsBounded) {
    Source prev = MakeSource(0, 0, { MakePerson(7, 0.0f) });
    Source last = MakeSource(0.125, 0.125, { MakePerson(7, 0.3f) });
    Output out;
    Extrapolate(prev, last, 1.0f, false, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.6f, 1e-6f)
        << "a full period on: past anything the camera saw";
    Extrapolate(prev, last, 0.5f, true, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHandLeft].x, 0.3f + kMaxStepMetres, 1e-6f);
    last.persons[0].confidence[kJointHead] = kConfidenceInferred;
    Extrapolate(prev, last, 0.5f, true, out);
    EXPECT_NEAR(out.persons[0].joints[kJointHead].x, 0.3f, 1e-6f) << "clamped: tracked joints only";
}

// ---------------------------------------------------------------------------
// The schedule.  A clock of song seconds in 1/120 s game frames (DC3_FAST_TIME's
// rate); a camera whose frames arrive every `period` and show a hand moving at
// 1 m/s, so a frame's x is the moment it shows.
// ---------------------------------------------------------------------------
namespace {

struct Delivery {
    double song;  // when the game got it
    float x;      // what it shows
    bool real;
    double shows;
    int persons;
};

struct FillRun {
    std::vector<Delivery> out;
    std::vector<double> arrivals;
    double lastArrivalX = -1.0;
};

// Camera frames every `period` from t0 to t1 (song seconds); `present(t)`
// says whether the person is in the frame.
template <class Present>
FillRun Drive(Filler &f, double period, double t0, double t1, double runTo, Present present) {
    FillRun r;
    const double tick = 1.0 / 120.0;
    double next = t0;
    for (int i = 0;; i++) {
        double song = t0 + i * tick;
        if (song > runTo)
            break;
        bool engaged = f.Engaged();
        if (song + 1e-9 >= next && next <= t1) {
            std::vector<Person> people;
            if (present(next))
                people.push_back(MakePerson(5, (float)next));
            Source s = MakeSource(song, next, people);
            engaged = f.Push(s);
            r.arrivals.push_back(song);
            if (!engaged)
                r.out.push_back({ song, people.empty() ? NAN : (float)next, true, song, (int)people.size() });
            next += period;
        }
        Output o;
        if (engaged && f.Poll(song, o))
            r.out.push_back({ song, o.numPersons ? o.persons[0].joints[kJointHandLeft].x : NAN, o.real,
                              o.shows, o.numPersons });
    }
    return r;
}

auto Always = [](double) { return true; };

} // namespace

TEST(PoseFillScheduleTest, AThirtyHertzCameraIsNeverFilled) {
    for (Mode m : { kInterp, kHold, kExtrap, kExtrapClamped }) {
        Filler f;
        f.SetMode(m);
        FillRun r = Drive(f, 1.0 / 30.0, 10.0, 20.0, 20.0, Always);
        EXPECT_FALSE(f.Engaged()) << ModeName(m);
        EXPECT_EQ(f.Fills(), 0u) << ModeName(m);
        EXPECT_EQ(r.out.size(), r.arrivals.size()) << "every camera frame, at once";
        EXPECT_EQ(f.Delay(), 0.0);
    }
}

TEST(PoseFillScheduleTest, OffNeverFills) {
    Filler f;
    f.SetMode(kOff);
    FillRun r = Drive(f, 0.125, 10.0, 20.0, 20.0, Always);
    EXPECT_FALSE(f.Engaged());
    EXPECT_EQ(r.out.size(), r.arrivals.size());
}

TEST(PoseFillScheduleTest, InterpAtEightHertzIsThirtyHertzAFixedDelayBehindAndExact) {
    Filler f;
    f.SetMode(kInterp);
    FillRun r = Drive(f, 0.125, 10.0, 20.0, 20.5, Always);
    ASSERT_TRUE(f.Engaged());
    EXPECT_NEAR(f.Delay(), 0.125 * Filler::kDelayFactor, 1e-6);
    EXPECT_NEAR(f.MedianInterval(), 0.125, 1e-6);

    // Steady state: after engagement settles.
    double from = 12.0, to = 19.5;
    int n = 0, reals = 0;
    double prevShows = -1, prevSong = -1;
    std::vector<double> realArrivals;
    for (const Delivery &d : r.out) {
        if (d.song < from || d.song > to)
            continue;
        n++;
        // What it shows is the camera at the moment `shows`: linear motion
        // makes interpolation exact, and no frame is ahead of the newest
        // camera frame that had arrived.
        double newest = -1;
        for (double a : r.arrivals) {
            if (a <= d.song + 1e-9)
                newest = a;
        }
        EXPECT_LE(d.shows, newest + 1e-9) << "ahead of the camera at " << d.song;
        EXPECT_NEAR(d.x, d.shows - 10.0 + 10.0, 2e-5) << "at " << d.song; // x = capture time = arrival
        EXPECT_GT(d.shows, prevShows) << "shows time moves forward only";
        prevShows = d.shows;
        // The delay: what it shows is Delay() behind when it is shown.
        EXPECT_NEAR(d.song - d.shows, f.Delay(), 1.0 / 120.0 + 1e-9) << "at " << d.song;
        if (prevSong >= 0)
            EXPECT_LE(d.song - prevSong, 1.0 / 30.0 + 1.0 / 120.0 + 1e-9) << "a gap at " << d.song;
        prevSong = d.song;
        if (d.real) {
            reals++;
            realArrivals.push_back(d.shows);
        }
    }
    double secs = to - from;
    EXPECT_NEAR(n / secs, 32.0, 2.5) << "30 Hz of fill plus the camera's own frames";
    EXPECT_NEAR(reals / secs, 8.0, 0.3);
    // Every camera frame in the window, exactly once and in order.
    std::vector<double> expected;
    for (double a : r.arrivals) {
        if (a + f.Delay() >= from + 1.0 / 120.0 && a + f.Delay() <= to - 1.0 / 120.0)
            expected.push_back(a);
    }
    for (double a : expected) {
        int count = 0;
        for (double s : realArrivals)
            count += std::fabs(s - a) < 1e-9;
        EXPECT_EQ(count, 1) << "camera frame of " << a;
    }
}

TEST(PoseFillScheduleTest, AStalledCameraStallsTheStream) {
    Filler f;
    f.SetMode(kInterp);
    FillRun r = Drive(f, 0.125, 10.0, 15.0, 17.0, Always);
    double lastArrival = r.arrivals.back();
    const Delivery &last = r.out.back();
    EXPECT_TRUE(last.real) << "the last thing shown is the newest camera frame";
    EXPECT_NEAR(last.shows, lastArrival, 1e-9);
    for (const Delivery &d : r.out)
        EXPECT_LE(d.shows, lastArrival + 1e-9);
    EXPECT_LT(last.song, lastArrival + f.Delay() + 1.0 / 60.0) << "nothing after it";
}

TEST(PoseFillScheduleTest, ExtrapStopsOnePeriodAfterTheNewestCameraFrame) {
    for (float gain : { 1.0f, 0.5f }) {
        Filler f;
        f.SetMode(kExtrap);
        f.SetGain(gain);
        FillRun r = Drive(f, 0.125, 10.0, 15.0, 17.0, Always);
        double lastArrival = r.arrivals.back();
        ASSERT_TRUE(f.Engaged());
        EXPECT_LE(r.out.back().song, lastArrival + 0.125 + 1e-9) << "a stall stays a stall";
        for (const Delivery &d : r.out) {
            double newest = -1;
            for (double a : r.arrivals) {
                if (a <= d.song + 1e-9)
                    newest = a;
            }
            // x = capture time; the step on is at most gain x one period.
            EXPECT_LE(d.x, (float)(newest + gain * 0.125) + 1e-4f) << "at " << d.song;
            EXPECT_GE(d.x, (float)newest - 1e-4f) << "at " << d.song;
            if (!d.real && d.song > 11.0)
                EXPECT_NEAR(d.x, (float)(newest + gain * (d.song - newest)), 1e-4f) << "at " << d.song;
        }
    }
}

TEST(PoseFillScheduleTest, NoFillAcrossADropoutTheDropoutFrameArrivesExactly) {
    // The person is missing from 13.0 to 13.5: a detection dropout.
    auto present = [](double t) { return t < 13.0 - 1e-9 || t > 13.5 + 1e-9; };
    {
        Filler f;
        f.SetMode(kInterp);
        FillRun r = Drive(f, 0.125, 10.0, 16.0, 16.5, present);
        int emptyReal = 0;
        for (const Delivery &d : r.out) {
            if (d.shows > 12.875 + 1e-9 && d.shows < 13.625 - 1e-9) {
                // Between the last frame with her and the first one with her
                // again only camera frames, each exactly as it came: nobody in it.
                EXPECT_TRUE(d.real) << "a fill frame showing " << d.shows;
                EXPECT_EQ(d.persons, 0) << "showing " << d.shows;
                emptyReal += d.real && d.persons == 0;
            }
        }
        EXPECT_EQ(emptyReal, 5) << "13.0 .. 13.5, every empty camera frame";
    }
    {
        // extrap: after the last frame with her, at most one period of her
        // (the camera has not yet said she is gone); from the first empty
        // camera frame to the first one with her again, nothing but those.
        Filler f;
        f.SetMode(kExtrap);
        FillRun r = Drive(f, 0.125, 10.0, 16.0, 16.5, present);
        int emptyReal = 0;
        for (const Delivery &d : r.out) {
            if (d.song > 13.0 - 1e-9 && d.song < 13.625 - 1e-9) {
                EXPECT_TRUE(d.real) << "a fill frame at " << d.song;
                EXPECT_EQ(d.persons, 0) << "at " << d.song;
                emptyReal += d.real && d.persons == 0;
            }
        }
        EXPECT_EQ(emptyReal, 5) << "13.0 .. 13.5, every empty camera frame";
    }
}

TEST(PoseFillScheduleTest, NoFillAcrossALongCameraGap) {
    Filler f;
    f.SetMode(kInterp);
    FillRun r = Drive(f, 0.125, 10.0, 13.0, 13.0, Always);
    ASSERT_TRUE(f.Engaged());
    // The camera hiccups for 0.6 s, then carries on.
    FillRun r2 = Drive(f, 0.125, 13.6, 15.0, 15.0, Always);
    for (const Delivery &d : r2.out)
        EXPECT_FALSE(!d.real && d.shows > 13.0 && d.shows < 13.6) << "a fill across the gap at " << d.shows;
    (void)r;
}

TEST(PoseFillScheduleTest, TheClockGoingBackStartsAgainWithNothingOld) {
    Filler f;
    Drive(f, 0.125, 50.0, 55.0, 55.0, Always);
    ASSERT_TRUE(f.Engaged());
    FillRun r = Drive(f, 0.125, -4.0, -2.0, -2.0, Always);
    for (const Delivery &d : r.out)
        EXPECT_LT(d.shows, 0.0) << "a frame of the old timeline";
    EXPECT_TRUE(f.Engaged()) << "the camera's rate is the same";
}

TEST(PoseFillScheduleTest, AFasterCameraReleasesTheFill) {
    // np-pose-steady: once the camera has stayed fast for the release dwell
    // (PoseFillDwellTest); with no dwell, within the 1.9 s this test gave it
    // before there was one.
    for (double dwell : { Filler::kReleaseDwell, 0.0 }) {
        Filler f;
        f.SetReleaseDwell(dwell);
        Drive(f, 0.125, 10.0, 12.0, 12.0, Always);
        ASSERT_TRUE(f.Engaged());
        double until = 14.0 + dwell;
        Drive(f, 1.0 / 30.0, 12.1, until, until, Always);
        EXPECT_FALSE(f.Engaged()) << "dwell " << dwell;
        EXPECT_EQ(f.Delay(), 0.0);
    }
}

// A camera delivering at ~23 Hz as a mix of one- and two-frame intervals of a
// 30 Hz grab (33 and 53 ms, alternating -- what the web worker delivered on the
// rig, MEASURED np-web4). The median of 15 such intervals is 33 ms with eight
// short ones in the window and 53 ms with seven, so a median-rate fill
// engaged and released on every frame: 349 changes in one song. The rate
// estimate is the window's trimmed mean, ~23 Hz: engaged once, and stays.
TEST(PoseFillScheduleTest, AnAlternatingCameraRateDoesNotFlapTheFill) {
    for (Mode m : { kExtrap, kInterp }) {
        Filler f;
        f.SetMode(m);
        double t = 10.0;
        int changes = 0;
        bool was = f.Engaged();
        for (int i = 0; i < 300; i++) {
            t += (i % 2) ? 0.053 : 1.0 / 30.0;
            std::vector<Person> people{ MakePerson(5, (float)t) };
            bool now = f.Push(MakeSource(t, t, people));
            if (now != was)
                changes++;
            was = now;
        }
        EXPECT_TRUE(f.Engaged()) << ModeName(m);
        EXPECT_EQ(changes, 1) << ModeName(m) << ": the fill flapped";
        EXPECT_NEAR(1.0 / f.RateInterval(), 23.2, 1.0) << ModeName(m);
    }
    // ...and a mix that averages over the release rate (33 / 37 ms, ~28.5 Hz)
    // is never engaged.
    Filler f;
    double t = 10.0;
    for (int i = 0; i < 300; i++) {
        t += (i % 2) ? 0.037 : 1.0 / 30.0;
        std::vector<Person> people{ MakePerson(5, (float)t) };
        EXPECT_FALSE(f.Push(MakeSource(t, t, people))) << "frame " << i;
    }
}

TEST(PoseFillScheduleTest, HoldAndExtrapAreNotDelayed) {
    for (Mode m : { kHold, kExtrap, kExtrapClamped }) {
        Filler f;
        f.SetMode(m);
        FillRun r = Drive(f, 0.125, 10.0, 15.0, 15.0, Always);
        ASSERT_TRUE(f.Engaged()) << ModeName(m);
        EXPECT_EQ(f.Delay(), 0.0);
        for (const Delivery &d : r.out) {
            if (d.real)
                EXPECT_NEAR(d.song, d.shows, 1e-9) << ModeName(m);
        }
        EXPECT_GT(f.Fills(), 0u) << ModeName(m);
    }
    // extrap shows past the newest camera frame (the overshoot), hold never.
    Filler e;
    e.SetMode(kExtrap);
    FillRun r = Drive(e, 0.125, 10.0, 15.0, 15.0, Always);
    float most = 0.0f;
    for (const Delivery &d : r.out) {
        double newest = -1;
        for (double a : r.arrivals) {
            if (a <= d.song + 1e-9)
                newest = a;
        }
        most = std::fmax(most, d.x - (float)newest);
    }
    EXPECT_GT(most, 0.09f) << "extrap reaches ahead of the camera";
}

// ---------------------------------------------------------------------------
// The scorer sees the delay.
// ---------------------------------------------------------------------------
TEST(PoseFillLatencyTest, TheDelayIsAddedToTheScorersLatency) {
    unsetenv("DC3_POSE_LATENCY_MS");
    Dc3SetPoseFillDelaySeconds(0.0f);
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.1f);
    MoveDir::NativeSetLatencySeconds(0.1f);
    Dc3SetPoseFillDelaySeconds(0.15f);
    EXPECT_FLOAT_EQ(MoveDir::NativeLatencySeconds(), 0.25f) << "at once";
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.25f) << "and at the next LoadScoring";
    setenv("DC3_POSE_LATENCY_MS", "40", 1);
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.19f) << "on top of the knob";
    Dc3SetPoseFillDelaySeconds(0.0f);
    EXPECT_FLOAT_EQ(MoveDir::NativeLatencySeconds(), 0.04f) << "released: the knob alone";
    unsetenv("DC3_POSE_LATENCY_MS");
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.1f);
    MoveDir::NativeSetLatencySeconds(0.1f);
}

TEST(PoseFillLatencyTest, TheDelayRidesOnWhicheverSensorLatencyWins) {
    // np-latency-harness's precedence (explicit > declared > default) picks the
    // CAMERA's latency; the fill's delay is ours, on top of whichever it is.
    unsetenv("DC3_POSE_LATENCY_MS");
    Dc3DeclareSourceLatencyMs(-1);
    Dc3SetPoseFillDelaySeconds(0.0f);
    MoveDir::NativeSetLatencySeconds(Dc3SensorLatencySeconds(0.1f));
    Dc3SetPoseFillDelaySeconds(0.16f);
    EXPECT_FLOAT_EQ(MoveDir::NativeLatencySeconds(), 0.26f) << "default 100 + fill";
    Dc3DeclareSourceLatencyMs(0); // a recorded file
    EXPECT_FLOAT_EQ(MoveDir::NativeLatencySeconds(), 0.16f) << "declared 0 + fill";
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.16f) << "and at the next LoadScoring";
    setenv("DC3_POSE_LATENCY_MS", "50", 1);
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.21f) << "explicit 50 + fill";
    Dc3SetPoseFillDelaySeconds(0.0f);
    EXPECT_FLOAT_EQ(MoveDir::NativeLatencySeconds(), 0.05f) << "released: explicit alone";
    unsetenv("DC3_POSE_LATENCY_MS");
    Dc3DeclareSourceLatencyMs(-1);
    EXPECT_FLOAT_EQ(MoveDir::NativeLatencySeconds(), 0.1f) << "back to the default";
    MoveDir::NativeSetLatencySeconds(Dc3SensorLatencySeconds(0.1f));
}

// ---- the fill's clock outside a song -----------------------------------------

TEST(PoseFillStallClockTest, AMovingSongClockPassesThroughUnchanged) {
    StallClock c;
    double song = 12.0, ui = 500.0;
    for (int i = 0; i < 600; i++) {
        song += 1.0 / 60.0;
        ui += 1.0 / 60.0;
        EXPECT_DOUBLE_EQ(c.Read(song, ui), song);
        EXPECT_DOUBLE_EQ(c.Read(song, ui), song) << "a second read in the same frame";
    }
    EXPECT_DOUBLE_EQ(c.Carried(), 0.0);
    EXPECT_FALSE(c.Carrying());
}

TEST(PoseFillStallClockTest, AValueRepeatedForAFrameOrTwoIsNotCountedTwice) {
    // An audio clock read between its own updates: 0, 0, +50 ms, 0, 0, +50 ms.
    StallClock c;
    double song = 30.0, ui = 100.0;
    for (int i = 0; i < 300; i++) {
        ui += 1.0 / 60.0;
        if (i % 3 == 2)
            song += 0.05;
        c.Read(song, ui);
    }
    EXPECT_DOUBLE_EQ(c.Carried(), 0.0);
}

TEST(PoseFillStallClockTest, AStoppedSongClockCarriesOnAtUiPace) {
    StallClock c;
    double song = 40.0, ui = 200.0;
    for (int i = 0; i < 60; i++) { // 1 s of a running song clock
        song += 1.0 / 60.0;
        ui += 1.0 / 60.0;
        c.Read(song, ui);
    }
    double last = c.Read(song, ui);
    double stopped = last;
    for (int i = 0; i < 180; i++) { // 3 s stopped (the pause menu)
        ui += 1.0 / 60.0;
        double now = c.Read(song, ui);
        EXPECT_GE(now, last) << "never backwards";
        EXPECT_LE(now - last, 1.0 / 60.0 + 1e-9) << "never faster than the UI clock";
        last = now;
    }
    EXPECT_TRUE(c.Carrying());
    EXPECT_NEAR(last - stopped, 3.0 - StallClock::kStall, 0.02) << "3 s, less the 0.25 s it waited";
    // The song clock moves again, from where it stopped: the clock goes on
    // from where IT is, by the song clock's own steps.
    for (int i = 0; i < 60; i++) {
        song += 1.0 / 60.0;
        ui += 1.0 / 60.0;
        double now = c.Read(song, ui);
        EXPECT_NEAR(now - last, 1.0 / 60.0, 1e-9);
        last = now;
    }
    EXPECT_FALSE(c.Carrying());
}

TEST(PoseFillStallClockTest, TheSongClockGoingBackStillGoesBack) {
    // A new song: the filler drops the old timeline when its clock goes back
    // (Filler::Push), so the step has to come through.
    StallClock c;
    double ui = 10.0;
    c.Read(180.0, ui);
    for (int i = 0; i < 120; i++) { // stopped on the results screen for 2 s
        ui += 1.0 / 60.0;
        c.Read(180.0, ui);
    }
    double before = c.Read(180.0, ui);
    ui += 1.0 / 60.0;
    double after = c.Read(-5.0, ui); // the next song's count-in
    EXPECT_NEAR(before - after, 185.0, 1e-9);
}

TEST(PoseFillStallClockTest, AnEightHertzCameraIsFilledWhileTheSongClockIsStopped) {
    // 3 s with the song clock running (the fill engages), then 3 s with it
    // stopped.  Camera frames every 1/8 s of real time, the game at 60 fps.
    auto run = [](Mode mode, bool stallClock, unsigned *fillsStopped, unsigned *realsStopped) {
        Filler f;
        f.SetMode(mode);
        f.Reset();
        StallClock clock;
        double song = 20.0, ui = 300.0, nextCamera = 0.0;
        unsigned fills0 = 0, reals0 = 0;
        for (int i = 0; i < 360; i++) {
            double t = i / 60.0;
            ui += 1.0 / 60.0;
            if (t < 3.0)
                song += 1.0 / 60.0;
            if (i == 180) {
                fills0 = f.Fills();
                reals0 = f.Reals();
            }
            double now = stallClock ? clock.Read(song, ui) : song;
            if (t + 1e-9 >= nextCamera) {
                Source s;
                s.arrival = now;
                s.ts = 1000.0 + nextCamera;
                s.frameId = (uint32_t)i + 1;
                s.numPersons = 1;
                s.persons[0].trackId = 5;
                s.persons[0].valid = true;
                for (int j = 0; j < kNumJoints; j++) {
                    s.persons[0].joints[j].Set((float)nextCamera * 0.1f, 1.0f + j * 0.1f, 2.0f);
                    s.persons[0].confidence[j] = kConfidenceTracked;
                }
                s.floorNormal.Set(0.0f, 1.0f, 0.0f);
                s.levelled = true;
                f.Push(s);
                nextCamera += 1.0 / 8.0;
            }
            Output o;
            if (f.Engaged())
                f.Poll(now, o);
        }
        EXPECT_TRUE(f.Engaged());
        *fillsStopped = f.Fills() - fills0;
        *realsStopped = f.Reals() - reals0;
    };
    unsigned fillsSong = 0, realsSong = 0, fillsStall = 0, realsStall = 0;
    run(kExtrap, false, &fillsSong, &realsSong);
    run(kExtrap, true, &fillsStall, &realsStall);
    // The control, and the defect: on the song clock alone the stopped 3 s
    // get the camera's 24 frames and not one fill.
    EXPECT_EQ(fillsSong, 0u);
    EXPECT_NEAR((double)realsSong, 24.0, 1.0);
    // On the StallClock they get a stream: at least 2 fills per camera frame
    // (30 Hz would be 2.75) once the 0.25 s wait is over.
    EXPECT_NEAR((double)realsStall, 24.0, 1.0);
    EXPECT_GE(fillsStall, 44u) << "fills in 3 s of a stopped song clock";

    // interp is worse off on the song clock alone: its stream runs a delay
    // BEHIND the clock, so with the clock stopped not even the camera's own
    // frames come due -- the menus would get no skeleton at all.
    run(kInterp, false, &fillsSong, &realsSong);
    run(kInterp, true, &fillsStall, &realsStall);
    EXPECT_EQ(fillsSong, 0u);
    EXPECT_LE(realsSong, 2u) << "camera frames delivered in 3 s of a stopped song clock (interp)";
    EXPECT_NEAR((double)realsStall, 24.0, 2.0);
    EXPECT_GE(fillsStall, 40u);
}

// DC3_POSE_FILL_MENU: the change of mode between a song and the menus.
TEST(PoseFillScheduleTest, AModeChangeKeepsTheCameraRate) {
    double t = 10.0;
    uint32_t id = 0;
    auto push = [&](Filler &f) {
        std::vector<Person> people;
        people.push_back(MakePerson(5, (float)t));
        Source s = MakeSource(t, t, people);
        s.frameId = ++id;
        bool engaged = f.Push(s);
        t += 1.0 / 8.0;
        return engaged;
    };
    Filler f;
    f.SetMode(kExtrap);
    f.Reset();
    for (int i = 0; i < 16; i++)
        push(f);
    ASSERT_TRUE(f.Engaged()) << "an 8 Hz camera engages the fill";

    // Into the menus with no fill: off is off at once ...
    f.SetModeKeepingRate(kOff);
    EXPECT_EQ(f.GetMode(), kOff);
    EXPECT_FALSE(f.Engaged());
    for (int i = 0; i < 8; i++)
        EXPECT_FALSE(push(f)) << "off never engages";
    // ... and back into a song: engaged with the FIRST camera frame, because
    // the camera's rate was kept.
    f.SetModeKeepingRate(kExtrap);
    EXPECT_TRUE(push(f)) << "the rate window was lost with the mode";
    // The control: SetMode forgets the camera, and takes kMinIntervals frames.
    Filler g;
    g.SetMode(kExtrap);
    g.Reset();
    for (int i = 0; i < 16; i++)
        push(g);
    g.SetMode(kOff);
    g.SetMode(kExtrap);
    EXPECT_FALSE(push(g)) << "the control engaged at once: this test cannot tell the two apart";

    // Between two filling modes the fill stays engaged, and the frames
    // buffered for the old mode are not shown by the new one.
    f.SetModeKeepingRate(kInterp);
    EXPECT_TRUE(f.Engaged());
    Output o;
    EXPECT_FALSE(f.Poll(t, o)) << "a frame buffered for extrap came out of interp";
    EXPECT_TRUE(push(f));
    EXPECT_GT(f.Delay(), 0.0) << "interp runs behind the camera";
    f.SetModeKeepingRate(kInterp); // no change: nothing dropped
    EXPECT_GT(f.Delay(), 0.0);
}

// ---------------------------------------------------------------------------
// The camera's rate under a game loop (np-pose-steady).
//
// The game polls the sensor once a frame and takes the newest camera frame;
// one that arrived between two polls together with a later one is never
// taken.  So the times at which the game took its frames are whole game
// frames apart, whatever the camera does.  SampleCamera is that: a camera
// with its own stamps and an uneven delivery, a game loop with its own frame
// time, and the Filler fed as GestureMgr_Native.cpp's BuildSensorFrame feeds
// it.
// ---------------------------------------------------------------------------
namespace {

// Deterministic uniform [0, 1).
struct Lcg {
    uint32_t x;
    double Next() {
        x = x * 1664525u + 1013904223u;
        return (double)(x >> 8) / 16777216.0;
    }
};

struct CameraModel {
    const char *name;
    double latency;     // stamp -> at the game's socket (inference), seconds
    double jitter;      // +- on that, seconds
    double late;        // fraction of frames delivered 30 ms late: they land with the next
    bool lateStamped;   // ... and stamped 25 ms late too (the camera handed it over late)
    double stampJitter; // +- on the stamp (pose_server.py's "read clock"), seconds
    double loopJitter;  // +- fraction of the game's frame time
    bool stamps;        // false: the stream carries no stamps (ts = -1)
};

// What the Mac's log shows of a pose server at a steady 30 a second
// (infer_ms_mean 21.7, p95 25.5, stamps from its read clock), and three
// rougher deliveries.
const CameraModel kQuiet = { "quiet: delivery +-4 ms, stamps +-1 ms, game frame +-10 %",
                             0.021, 0.004, 0.0, false, 0.001, 0.10, true };
const CameraModel kUneven = { "uneven: delivery +-8 ms, stamps +-3 ms, game frame +-10 %",
                              0.021, 0.008, 0.0, false, 0.003, 0.10, true };
const CameraModel kLate = { "late: 15 % of frames delivered 30 ms late, game frame +-5 %",
                            0.021, 0.004, 0.15, false, 0.001, 0.05, true };
const CameraModel kLateStamped = { "late and stamped late: 15 % of frames, game frame +-5 %",
                                   0.021, 0.004, 0.15, true, 0.001, 0.05, true };

struct RateRun {
    int taken = 0;          // camera frames the game took
    int made = 0;           // camera frames the camera made
    int engages = 0, releases = 0;
    int firstEngaged = -1;  // index (0-based) of the first taken frame the fill was engaged for
    unsigned fills = 0;     // frames the fill made
    double engagedSeconds = 0.0;
    double lastHz = 0.0;    // the estimate at the end
};

// `hzAt(t)`: the camera's rate at its own time t.  `onPush(source, t)` after
// every frame taken.
template <class HzAt, class OnPush>
RateRun SampleCamera(Filler &f, HzAt hzAt, const CameraModel &cam, double gameFps, double seconds,
                     uint32_t seed, OnPush onPush) {
    RateRun r;
    Lcg rng{ seed };
    double grab = 0.0;                 // when the camera makes its next frame
    double stamp = 0.0;                // ... the stamp it gets (strictly increasing)
    double due = cam.latency;          // ... and when it is at the game
    double lastDue = -1.0;
    uint32_t id = 0;
    bool was = f.Engaged();
    double t = 0.0, prevT = 0.0;
    while (t < seconds) {
        // Everything the camera delivered up to now; the game sees the newest.
        int delivered = 0;
        double newestStamp = -1.0;
        while (due <= t) {
            delivered++;
            id++;
            r.made++;
            newestStamp = stamp;
            lastDue = due;
            grab += 1.0 / hzAt(grab);
            double next = grab + (rng.Next() * 2.0 - 1.0) * cam.stampJitter;
            due = grab + cam.latency + (rng.Next() * 2.0 - 1.0) * cam.jitter;
            if (cam.late > 0.0 && rng.Next() < cam.late) {
                due += 0.030;
                if (cam.lateStamped)
                    next += 0.025;
            }
            stamp = next > stamp + 1e-4 ? next : stamp + 1e-4;
            if (due <= lastDue)
                due = lastDue + 1e-4; // a stream socket keeps the order
        }
        if (f.Engaged())
            r.engagedSeconds += t - prevT;
        if (delivered > 0) {
            std::vector<Person> people{ MakePerson(5, (float)newestStamp) };
            Source s = MakeSource(100.0 + t, cam.stamps ? 5000.0 + newestStamp : -1.0, people);
            s.frameId = id;
            s.frames = delivered;
            bool now = f.Push(s);
            if (now && r.firstEngaged < 0)
                r.firstEngaged = r.taken;
            if (now != was)
                (now ? r.engages : r.releases)++;
            was = now;
            r.taken++;
            onPush(s, t);
        }
        Output o;
        if (f.Engaged() && f.Poll(100.0 + t, o) && !o.real)
            r.fills++;
        prevT = t;
        t += (1.0 / gameFps) * (1.0 + (rng.Next() * 2.0 - 1.0) * cam.loopJitter);
    }
    double iv = f.RateInterval();
    r.lastHz = iv > 0.0 ? 1.0 / iv : 0.0;
    return r;
}

auto NoHook = [](const Source &, double) {};
auto Steady(double hz) {
    return [hz](double) { return hz; };
}

} // namespace

// The defect (MEASURED, an M3 MacBook Air on a 30 Hz display, 401 s): the pose
// server sent 29.9-30.4 poses a second in every 5 s window and the fill
// engaged 19 times, each time reading "camera 22.5 frames/s" (or 23.3, 23.9)
// -- six two-game-frame gaps among fifteen takes.  The rate was the time
// between the GAME's takes.
TEST(PoseFillRateTest, ASteadyThirtyHertzCameraNeverEngagesWhateverTheGamesFrameRate) {
    int beforeAtThirty = 0;
    for (const CameraModel *cam : { &kQuiet, &kUneven, &kLate, &kLateStamped }) {
        printf("  a steady 30.0 Hz camera, %s; 3 x 120 s a row\n", cam->name);
        for (double fps : { 30.0, 31.0, 33.0, 45.0, 60.0 }) {
            // before: the time between the game's takes (DC3_POSE_FILL_RATE=
            // arrival, the estimate until np-pose-steady).  now: the default.
            int before = 0, now = 0;
            unsigned fillsBefore = 0;
            double engagedBefore = 0.0, lowest = 1e9, taken = 0.0;
            for (uint32_t seed : { 1u, 2u, 3u }) {
                Filler old;
                old.SetMode(kExtrap);
                old.SetRateSource(kRateArrival);
                old.SetReleaseDwell(0.0);
                RateRun b = SampleCamera(old, Steady(30.0), *cam, fps, 120.0, seed, NoHook);
                before += b.engages;
                fillsBefore += b.fills;
                engagedBefore += b.engagedSeconds;

                Filler f;
                f.SetMode(kExtrap);
                RateRun r = SampleCamera(f, Steady(30.0), *cam, fps, 120.0, seed, NoHook);
                now += r.engages;
                taken += r.taken / 120.0 / 3.0;
                lowest = std::min(lowest, r.lastHz);
                EXPECT_TRUE(f.RateIsTheCamerasOwn());
                EXPECT_NEAR(r.made / 120.0, 30.0, 0.1);
                EXPECT_EQ(r.fills, 0u);
            }
            printf("    game %2.0f fps (it took %.1f frames/s): engaged %3d times before (%5.1f s, "
                   "%2u fill frames), %d now\n",
                   fps, taken, before, engagedBefore, fillsBefore, now);
            EXPECT_EQ(now, 0) << cam->name << ", game " << fps << " fps";
            EXPECT_GT(lowest, Filler::kReleaseHz) << cam->name << ", game " << fps << " fps";
            if (fps == 30.0)
                beforeAtThirty += before;
        }
    }
    // The control: these deliveries do make the takes read as a slow camera.
    EXPECT_GT(beforeAtThirty, 50) << "the test's cameras no longer provoke the defect it is for";
}

TEST(PoseFillRateTest, TheRateSourceParses) {
    bool ok = false;
    EXPECT_EQ(ParseRateSource(nullptr, &ok), kRateCamera);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseRateSource("", &ok), kRateCamera);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseRateSource("camera", &ok), kRateCamera);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseRateSource("arrival", &ok), kRateArrival);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseRateSource("fast", &ok), kRateCamera);
    EXPECT_FALSE(ok);
    EXPECT_EQ(Filler().GetRateSource(), kRateCamera);
}

// The other half: a camera that IS slow engages, with the same camera frame
// as before (the kMinIntervals-th interval), and stays engaged.
TEST(PoseFillRateTest, ASlowCameraEngagesWithTheSameFrameAsBeforeAndStays) {
    const CameraModel still = { "still", 0.021, 0.002, 0.0, false, 0.001, 0.02, true };
    for (double hz : { 8.0, 10.0, 12.0, 15.0, 20.0 }) {
        for (double fps : { 30.0, 60.0 }) {
            Filler f;
            f.SetMode(kExtrap);
            RateRun r = SampleCamera(f, Steady(hz), still, fps, 60.0, 7u, NoHook);
            EXPECT_EQ(r.firstEngaged, Filler::kMinIntervals) << hz << " Hz, game " << fps << " fps";
            EXPECT_EQ(r.engages, 1) << hz << " Hz, game " << fps << " fps";
            EXPECT_EQ(r.releases, 0) << hz << " Hz, game " << fps << " fps";
            EXPECT_NEAR(r.lastHz, hz, hz * 0.06) << "game " << fps << " fps";
            EXPECT_GT(r.fills, 0u);
        }
    }
    // A stream with no stamps is read by the takes, as before: a slow camera
    // engages with the same frame.
    const CameraModel bare = { "no stamps", 0.021, 0.002, 0.0, false, 0.0, 0.02, false };
    for (double hz : { 8.0, 15.0, 20.0 }) {
        Filler f;
        f.SetMode(kExtrap);
        RateRun r = SampleCamera(f, Steady(hz), bare, 60.0, 30.0, 7u, NoHook);
        EXPECT_FALSE(f.RateIsTheCamerasOwn());
        EXPECT_EQ(r.firstEngaged, Filler::kMinIntervals) << hz << " Hz";
        EXPECT_EQ(r.releases, 0) << hz << " Hz";
    }
}

// A frame count that cannot be (a restarted source's counter, a wrapped one)
// is one frame, and a stamp that jumps is no stamp.
TEST(PoseFillRateTest, AnUnbelievableFrameCountOrStampIsNotUsed) {
    for (int frames : { 0, -3, Filler::kMaxUntaken + 1, 1 << 20 }) {
        Filler f;
        double t = 10.0;
        for (int i = 0; i < 20; i++) {
            t += 0.125;
            std::vector<Person> people{ MakePerson(5, (float)t) };
            Source s = MakeSource(t, t, people);
            s.frames = frames;
            f.Push(s);
        }
        EXPECT_TRUE(f.Engaged()) << frames << " frames: an 8 Hz camera was not engaged";
        EXPECT_NEAR(1.0 / f.RateInterval(), 8.0, 0.01) << frames;
    }
    // Stamps that step 5 s a frame (another clock): the takes decide.
    Filler f;
    double t = 10.0, ts = 0.0;
    for (int i = 0; i < 20; i++) {
        t += 0.125;
        ts += 5.0;
        std::vector<Person> people{ MakePerson(5, (float)t) };
        f.Push(MakeSource(t, ts, people));
    }
    EXPECT_FALSE(f.RateIsTheCamerasOwn());
    EXPECT_NEAR(1.0 / f.RateInterval(), 8.0, 0.01);
    // A window holding a frame without a stamp is read by the takes: the two
    // intervals around that frame have no camera period at all, and counted
    // as zero they would make an 8 Hz camera read as a fast one.
    Filler g;
    t = 10.0;
    for (int i = 0; i < 30; i++) {
        t += 0.125;
        std::vector<Person> people{ MakePerson(5, (float)t) };
        g.Push(MakeSource(t, i % 3 == 0 ? -1.0 : t, people));
    }
    EXPECT_FALSE(g.RateIsTheCamerasOwn());
    EXPECT_TRUE(g.Engaged()) << "an 8 Hz camera with unstamped frames among its own was not engaged";
    EXPECT_NEAR(1.0 / g.RateInterval(), 8.0, 0.01);
}

// By hand: fifteen takes of a 30 Hz camera by a 30 fps game, six of them two
// game frames after the one before -- the log's "22.5 frames/s".
TEST(PoseFillRateTest, TheRateIsTheCamerasOwnStampsOverTheFramesItDelivered) {
    auto feed = [](Filler &f, bool stamps, double clockPace) {
        double arrival = 50.0, ts = 9000.0;
        bool engaged = false;
        for (int i = 0; i < 60; i++) {
            // Two takes in five come two game frames after the one before
            // (a poll found no new frame) and find two camera frames waiting.
            int k = i % 5;
            bool twoGameFrames = (k == 1 || k == 3);
            int delivered = twoGameFrames ? 2 : 1;
            arrival += (twoGameFrames ? 2.0 : 1.0) / 30.0 * clockPace;
            ts += delivered / 30.0;
            std::vector<Person> people{ MakePerson(5, (float)ts) };
            Source s = MakeSource(arrival, stamps ? ts : -1.0, people);
            s.frames = delivered;
            engaged = f.Push(s) || engaged;
        }
        return engaged;
    };
    {
        Filler f;
        EXPECT_FALSE(feed(f, true, 1.0)) << "a 30 Hz camera engaged the fill";
        EXPECT_NEAR(1.0 / f.RateInterval(), 30.0, 0.01);
        // The median is still the game's: the delay, the horizon and the gap
        // limit are song time between the frames the game took.
        EXPECT_NEAR(f.MedianInterval(), 1.0 / 30.0, 1e-9);
    }
    {
        // No stamps: the time between the game's takes, as before.
        Filler f;
        EXPECT_TRUE(feed(f, false, 1.0));
        EXPECT_NEAR(1.0 / f.RateInterval(), 22.5, 0.01);
    }
    {
        // Stamps on another clock's pace (DC3_FAST_TIME with a camera on the
        // wall clock: here 4 song seconds a camera second).  What the scorer
        // gets is frames a SONG second, so the game's takes decide, as before.
        Filler f;
        EXPECT_TRUE(feed(f, true, 4.0));
        EXPECT_NEAR(1.0 / f.RateInterval(), 22.5 / 4.0, 0.01);
    }
}

// ---------------------------------------------------------------------------
// The release dwell (np-pose-steady, DC3_POSE_FILL_DWELL).
//
// MEASURED on the M3 MacBook Air, 1019 s with the tracker slowing down for
// stretches (12-19 poses a second while the machine was hot): the fill
// engaged 26 times; 19 of its 23 spans were under 5 s (median 2.1 s), and of
// the 24 releases followed by another engagement in the same song 17 were
// followed within 5 s, 18 within 10 s.  Every change is a change of how the
// motion between two camera frames is made.
// ---------------------------------------------------------------------------
namespace {

// Camera frames at `hz` for `seconds`, each taken as it comes; *released is
// set to the song time of a release.
void FeedCamera(Filler &f, double &t, double hz, double seconds, double *released = nullptr) {
    double end = t + seconds;
    while (t < end - 1e-9) {
        t += 1.0 / hz;
        std::vector<Person> people{ MakePerson(5, (float)t) };
        bool was = f.Engaged();
        bool now = f.Push(MakeSource(t, t, people));
        if (was && !now && released)
            *released = t;
    }
}

// Cameras whose rate wanders between 18 and 29 a second (camera time t).
const double kTwoPi = 6.283185307179586;
double WanderSine4(double t) { return 23.5 + 5.5 * std::sin(kTwoPi * t / 4.0); }
double WanderSine9(double t) { return 23.5 + 5.5 * std::sin(kTwoPi * t / 9.0); }
double WanderSine20(double t) { return 23.5 + 5.5 * std::sin(kTwoPi * t / 20.0); }
// A new level every 2.5 s, as a tracker on a hot machine: 18 .. 29.
double WanderSteps(double t) {
    static const double kLevels[8] = { 18.0, 20.0, 22.0, 25.0, 27.0, 29.0, 29.0, 29.0 };
    uint32_t k = (uint32_t)(t / 2.5);
    return kLevels[(k * 2654435761u) >> 29];
}
// 29 a second, with 2 s at 18 every 8 s.
double WanderDips(double t) { return std::fmod(t, 8.0) < 2.0 ? 18.0 : 29.0; }
// The cost: 20 s at 15, then 30 for good -- one engagement, one release.
double SlowThenFast(double t) { return t < 20.0 ? 15.0 : 30.0; }
// The M3 MacBook Air's tracker: poses a second in each 5 s window of the
// 1019 s session (its POSE_STATS lines, in order).  The rate inside a window
// is not known, so this camera is steadier than the Mac's was.
const double kMacTrackerHz[] = {
    4.2, 2.7, 3.0, 3.2, 3.0, 2.9, 3.7, 1.7, 4.4, 15.5, 30.0, 30.0, 30.0, 30.0,
    30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 29.8, 26.5, 30.0,
    30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0,
    30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0,
    30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0,
    30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0,
    30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 30.0, 29.9, 30.0, 29.3, 28.9, 29.6, 30.0,
    30.0, 30.0, 30.0, 30.0, 30.0, 24.4, 24.2, 23.2, 22.8, 24.1, 17.4, 17.3, 17.9, 19.1,
    18.1, 18.5, 18.4, 18.4, 18.2, 17.9, 19.3, 19.4, 20.2, 19.7, 19.7, 20.2, 20.2, 21.7,
    27.2, 27.5, 20.1, 20.3, 20.4, 20.5, 20.5, 20.3, 20.2, 20.3, 20.3, 19.3, 28.1, 28.4,
    26.2, 26.8, 24.4, 25.3, 24.8, 22.3, 13.7, 14.7, 12.8, 12.0, 12.1, 12.8, 15.6, 17.0,
    15.0, 18.4, 28.4, 22.2, 21.3, 23.3, 20.1, 22.0, 24.1, 24.3, 24.4, 20.7, 26.4, 22.0,
    28.8, 30.0, 26.9, 30.0, 29.8, 27.0, 28.8, 26.6, 25.6, 29.2, 30.0, 29.3, 29.8, 21.6,
    29.9, 30.0, 29.9, 21.8, 25.7, 21.5, 19.6, 13.7, 18.3, 19.6, 20.1, 16.0, 18.7, 13.5,
    16.2,
};
const int kMacTrackerWindows = (int)(sizeof(kMacTrackerHz) / sizeof(kMacTrackerHz[0]));
double MacTracker(double t) {
    int k = (int)(t / 5.0);
    return kMacTrackerHz[k < 0 ? 0 : k >= kMacTrackerWindows ? kMacTrackerWindows - 1 : k];
}

struct Wander {
    const char *name;
    double (*hz)(double);
};

struct DwellRun {
    double flipsPerMinute;
    double engagedSeconds;
    int notEngagedWhereBeforeItWas; // takes at which the fill with no dwell was engaged and this one not
};

DwellRun RunDwell(const Wander &w, double dwell, double seconds) {
    Filler f, before;
    f.SetMode(kExtrap);
    f.SetReleaseDwell(dwell);
    before.SetMode(kExtrap);
    before.SetReleaseDwell(0.0);
    int worse = 0;
    RateRun r = SampleCamera(f, w.hz, kQuiet, 60.0, seconds, 5u, [&](const Source &s, double) {
        bool b = before.Push(s);
        worse += b && !f.Engaged();
    });
    return { (r.engages + r.releases) * 60.0 / seconds, r.engagedSeconds, worse };
}

} // namespace

TEST(PoseFillDwellTest, TheFillLetsGoOnlyOnceTheCameraHasStayedFastForTheDwell) {
    EXPECT_EQ(Filler().ReleaseDwell(), Filler::kReleaseDwell);
    for (Mode m : { kExtrap, kInterp }) {
        Filler f;
        f.SetMode(m);
        double t = 10.0, released = -1.0;
        FeedCamera(f, t, 8.0, 3.0);
        ASSERT_TRUE(f.Engaged());
        double changed = t;
        FeedCamera(f, t, 30.0, Filler::kReleaseDwell - 0.2, &released);
        EXPECT_TRUE(f.Engaged()) << ModeName(m) << ": released " << released - changed
                                 << " s after the camera sped up";
        FeedCamera(f, t, 30.0, 1.2, &released);
        EXPECT_FALSE(f.Engaged()) << ModeName(m);
        // The window reads over the release rate ~0.4 s after the change
        // (twelve fast frames in); the dwell counts from there.
        EXPECT_GE(released - changed, Filler::kReleaseDwell) << ModeName(m);
        EXPECT_LE(released - changed, Filler::kReleaseDwell + 0.6) << ModeName(m);
    }
    {
        // No dwell (DC3_POSE_FILL_DWELL=0): with the first reading over the
        // release rate, as before.
        Filler f;
        f.SetReleaseDwell(0.0);
        double t = 10.0, released = -1.0;
        FeedCamera(f, t, 8.0, 3.0);
        ASSERT_TRUE(f.Engaged());
        double changed = t;
        FeedCamera(f, t, 30.0, 1.0, &released);
        EXPECT_FALSE(f.Engaged());
        EXPECT_LE(released - changed, 0.5);
    }
    {
        // A break starts the wait again: 3 s at 30, then 1.5 s at 25.5 --
        // between the two thresholds, neither slow nor fast -- then 30 again.
        Filler f;
        double t = 10.0, released = -1.0;
        FeedCamera(f, t, 8.0, 3.0);
        FeedCamera(f, t, 30.0, 3.0, &released);
        FeedCamera(f, t, 25.5, 1.5, &released);
        ASSERT_TRUE(f.Engaged());
        double back = t;
        FeedCamera(f, t, 30.0, Filler::kReleaseDwell - 0.2, &released);
        EXPECT_TRUE(f.Engaged()) << "released " << released - back << " s after the camera was fast again";
        FeedCamera(f, t, 30.0, 1.5, &released);
        EXPECT_FALSE(f.Engaged());
        EXPECT_GE(released - back, Filler::kReleaseDwell);
    }
    {
        // The dwell is of the camera, not of the mode: off is off at once.
        Filler f;
        double t = 10.0;
        FeedCamera(f, t, 8.0, 3.0);
        FeedCamera(f, t, 30.0, 2.0);
        ASSERT_TRUE(f.Engaged());
        f.SetModeKeepingRate(kOff);
        EXPECT_FALSE(f.Engaged());
        f.SetModeKeepingRate(kExtrap);
        FeedCamera(f, t, 30.0, 1.0);
        EXPECT_FALSE(f.Engaged()) << "a 30 Hz camera engaged the fill coming back from the menus";
    }
}

TEST(PoseFillDwellTest, AWanderingCameraChangesFarLessOftenAndIsFilledWheneverItWasBefore) {
    const Wander wanders[] = {
        { "18..29, a 4 s sine", WanderSine4 },   { "18..29, a 9 s sine", WanderSine9 },
        { "18..29, a 20 s sine", WanderSine20 }, { "18..29, a level every 2.5 s", WanderSteps },
        { "29 with 2 s at 18 every 8 s", WanderDips },
    };
    const double dwells[] = { 0.0, 1.0, 2.0, 3.0, 5.0, 6.0, 8.0, 10.0, 12.0, 15.0 };
    const double seconds = 300.0;
    printf("  engage/release changes a minute (300 s, the game at 60 fps), by release dwell in s:\n   ");
    printf(" %-30s", "camera");
    for (double d : dwells)
        printf(" %5.0f", d);
    printf("   engaged s: dwell 0 -> %.0f\n", Filler::kReleaseDwell);
    for (const Wander &w : wanders) {
        printf("    %-30s", w.name);
        DwellRun none = RunDwell(w, 0.0, seconds), chosen = RunDwell(w, Filler::kReleaseDwell, seconds);
        for (double d : dwells) {
            DwellRun r = RunDwell(w, d, seconds);
            printf(" %5.1f", r.flipsPerMinute);
            // Engaged wherever the fill without a dwell was: the dwell only
            // ever keeps the fill, so a slow camera gets no fewer fill frames.
            EXPECT_EQ(r.notEngagedWhereBeforeItWas, 0) << w.name << ", dwell " << d;
            EXPECT_GE(r.engagedSeconds, none.engagedSeconds - 1e-6) << w.name << ", dwell " << d;
        }
        printf("   %.0f -> %.0f\n", none.engagedSeconds, chosen.engagedSeconds);
        // The control: these cameras do make the fill without a dwell flap.
        EXPECT_GE(none.flipsPerMinute, 6.0) << w.name << ": no flapping to cure";
        EXPECT_LE(chosen.flipsPerMinute, 0.25 * none.flipsPerMinute) << w.name;
        EXPECT_LE(chosen.flipsPerMinute, 1.0) << w.name;
    }
    {
        // The Mac's own tracker rates, the whole session.
        const Wander mac = { "the Mac's tracker, 985 s", MacTracker };
        const double whole = kMacTrackerWindows * 5.0;
        printf("    %-30s", mac.name);
        DwellRun none = RunDwell(mac, 0.0, whole), chosen = RunDwell(mac, Filler::kReleaseDwell, whole);
        for (double d : dwells) {
            DwellRun r = RunDwell(mac, d, whole);
            printf(" %5.1f", r.flipsPerMinute);
            EXPECT_EQ(r.notEngagedWhereBeforeItWas, 0) << mac.name << ", dwell " << d;
        }
        printf("   %.0f -> %.0f\n", none.engagedSeconds, chosen.engagedSeconds);
        printf("    (the Mac's tracker: %.0f changes with no dwell, %.0f with %.0f s)\n",
               none.flipsPerMinute * whole / 60.0, chosen.flipsPerMinute * whole / 60.0,
               Filler::kReleaseDwell);
        EXPECT_LE(chosen.flipsPerMinute, 0.6 * none.flipsPerMinute);
    }
    // What it costs: a camera that was slow and is fast for good stays filled
    // the dwell longer, and changes exactly as often.
    const Wander recover = { "15, then 30 for good", SlowThenFast };
    DwellRun none = RunDwell(recover, 0.0, 60.0), chosen = RunDwell(recover, Filler::kReleaseDwell, 60.0);
    printf("    %-30s %5.1f -> %.1f changes a minute, engaged %.1f -> %.1f s\n", recover.name,
           none.flipsPerMinute, chosen.flipsPerMinute, none.engagedSeconds, chosen.engagedSeconds);
    EXPECT_EQ(none.flipsPerMinute, 2.0);
    EXPECT_EQ(chosen.flipsPerMinute, 2.0);
    EXPECT_NEAR(chosen.engagedSeconds - none.engagedSeconds, Filler::kReleaseDwell, 0.3);
}

TEST(PoseFillDwellTest, TheDwellParsesWithinZeroToAMinute) {
    bool ok = false;
    EXPECT_EQ(ParseReleaseDwell(nullptr, &ok), Filler::kReleaseDwell);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseReleaseDwell("", &ok), Filler::kReleaseDwell);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseReleaseDwell("0", &ok), 0.0);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseReleaseDwell("2.5", &ok), 2.5);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ParseReleaseDwell("60", &ok), 60.0);
    EXPECT_TRUE(ok);
    for (const char *bad : { "-1", "61", "soon", "3s", "nan" }) {
        EXPECT_EQ(ParseReleaseDwell(bad, &ok), Filler::kReleaseDwell) << bad;
        EXPECT_FALSE(ok) << bad;
    }
    Filler f;
    f.SetReleaseDwell(-4.0);
    EXPECT_EQ(f.ReleaseDwell(), 0.0);
    f.SetReleaseDwell(1e9);
    EXPECT_EQ(f.ReleaseDwell(), kMaxReleaseDwell);
}

// gpd-menubind: a dancer the camera missed for one frame is moved on from her
// last frame, not held for the next period.  Two people at 8 Hz, both moving
// at 1 m/s (x = capture time); the second is missing from the frame at 12.0 s.
TEST(PoseFillScheduleTest, OneMissedFrameKeepsHerVelocity) {
    for (bool lastSeen : { true, false }) {
        if (lastSeen)
            unsetenv("DC3_POSE_FILL_VELOCITY");
        else
            setenv("DC3_POSE_FILL_VELOCITY", "prev", 1);
        Filler f;
        f.SetMode(kExtrap);
        f.SetGain(1.0f);
        const double period = 0.125, tick = 1.0 / 120.0;
        double next = 10.0;
        int checked = 0;
        float worst = 0.0f;
        for (int i = 0;; i++) {
            double song = 10.0 + i * tick;
            if (song > 13.0)
                break;
            bool engaged = f.Engaged();
            if (song + 1e-9 >= next) {
                std::vector<Person> people;
                people.push_back(MakePerson(5, (float)next));
                if (fabs(next - 12.0) > 1e-6)
                    people.push_back(MakePerson(6, (float)next + 10.0f));
                engaged = f.Push(MakeSource(song, next, people));
                next += period;
            }
            Output o;
            if (!engaged || !f.Poll(song, o) || o.real)
                continue;
            // The fills in the period after she is back (12.125 s).
            if (o.shows < 12.125 + 1e-6 || o.shows > 12.25 - 1e-6)
                continue;
            for (int k = 0; k < o.numPersons; k++) {
                if (o.persons[k].trackId != 6)
                    continue;
                float want = (float)song + 10.0f;
                float err = fabsf(o.persons[k].joints[kJointHandLeft].x - want);
                worst = std::max(worst, err);
                checked++;
            }
        }
        ASSERT_TRUE(f.Engaged());
        ASSERT_GE(checked, 2);
        if (lastSeen)
            EXPECT_LT(worst, 1e-3f) << "moved on at her own velocity";
        else
            EXPECT_GT(worst, 0.02f) << "DC3_POSE_FILL_VELOCITY=prev holds her (the behaviour before)";
    }
    unsetenv("DC3_POSE_FILL_VELOCITY");
}
