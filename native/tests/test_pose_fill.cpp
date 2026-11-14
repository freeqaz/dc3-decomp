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
//   PoseFillLatencyTest   the delay reaches the scorer's latency, on top of
//                         whichever sensor latency is in force.
//
// The sensor end (slots, P-12's hold, the recorder) is PoseFillSensorTest in
// test_native_pose_socket.cpp.

#include <gtest/gtest.h>

#include "platform/PoseFill_Native.h"
#include "platform/Skeleton_Native.h" // Dc3SensorLatencySeconds, Dc3DeclareSourceLatencyMs
#include "hamobj/MoveDir.h"

#include <cmath>
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
    Filler f;
    Drive(f, 0.125, 10.0, 12.0, 12.0, Always);
    ASSERT_TRUE(f.Engaged());
    Drive(f, 1.0 / 30.0, 12.1, 14.0, 14.0, Always);
    EXPECT_FALSE(f.Engaged());
    EXPECT_EQ(f.Delay(), 0.0);
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
