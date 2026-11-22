// Pointer input in the menus (np-pointer): the rules alone, with no game, GPU
// or window (native/src/platform/PointerGesture.h).
//
//   PointerMapTest      a window or canvas point -> the 1280x720 frame, through
//                       the present scale's bars (a window that is not 16:9).
//   PointerHitTest      which row a point is on; rows that are headers or
//                       disabled are not targets; the row pitch.
//   PointerTrackerTest  tap vs drag vs flick: a press that stays inside the
//                       slop is a tap at the press; one that leaves it is a
//                       drag that scrolls a row per row pitch and never taps;
//                       a fast release flings on and slows to a stop; a press
//                       during a fling stops it and picks nothing; the wheel;
//                       a finger has no hover; the secondary button is back.
//
// Each was sabotaged once in PointerGesture.cpp and failed (np-pointer report):
//   FitFrame ignores the bars (r.x = r.y = 0)        -> PointerMapTest.*Bars*
//   HitRow skips the halfW test                       -> PointerHitTest.OutsideTheRow
//   no slop: every press drags                        -> PointerTrackerTest.ATapIsATap
//   a drag ends in a tap too (no `else`)              -> PointerTrackerTest.ADragNeverTaps
//   no fling (velocity ignored on release)            -> PointerTrackerTest.AFlickFlings
//   a press during a fling still taps                 -> PointerTrackerTest.APressThatStops...

#include <gtest/gtest.h>

#include "platform/PointerGesture.h"

#include <cmath>
#include <vector>

using namespace Dc3PointerGesture;

namespace {

TEST(PointerMapTest, A16by9WindowIsTheFrameScaled) {
    float fx, fy;
    ASSERT_TRUE(BoxToFrame(960, 540, 1920, 1080, 1280, 720, true, fx, fy));
    EXPECT_FLOAT_EQ(fx, 640);
    EXPECT_FLOAT_EQ(fy, 360);
    ASSERT_TRUE(BoxToFrame(0, 0, 1920, 1080, 1280, 720, true, fx, fy));
    EXPECT_FLOAT_EQ(fx, 0);
    EXPECT_FLOAT_EQ(fy, 0);
}

// PLAN H-18's own measurement: a 1600x1000 window shows the picture at
// 1600x900+0+50.
TEST(PointerMapTest, ATallWindowHasBarsAboveAndBelow) {
    const Rect r = FitFrame(1280, 720, 1600, 1000);
    EXPECT_FLOAT_EQ(r.x, 0);
    EXPECT_FLOAT_EQ(r.y, 50);
    EXPECT_FLOAT_EQ(r.w, 1600);
    EXPECT_FLOAT_EQ(r.h, 900);
    float fx, fy;
    // the top of the picture is 50 px down the window
    ASSERT_TRUE(BoxToFrame(800, 50, 1600, 1000, 1280, 720, true, fx, fy));
    EXPECT_NEAR(fx, 640, 1e-3);
    EXPECT_NEAR(fy, 0, 1e-3);
    ASSERT_TRUE(BoxToFrame(1600 * 0.75f, 50 + 900 * 0.25f, 1600, 1000, 1280, 720, true, fx, fy));
    EXPECT_NEAR(fx, 960, 1e-3);
    EXPECT_NEAR(fy, 180, 1e-3);
    // on the bar: not in the picture
    EXPECT_FALSE(BoxToFrame(800, 20, 1600, 1000, 1280, 720, true, fx, fy));
    EXPECT_FALSE(BoxToFrame(800, 980, 1600, 1000, 1280, 720, true, fx, fy));
}

// 1800x700 -> 1244x700+278+0 (H-18): bars left and right.
TEST(PointerMapTest, AWideWindowHasBarsLeftAndRight) {
    const Rect r = FitFrame(1280, 720, 1800, 700);
    EXPECT_NEAR(r.w, 1244.44f, 0.01f);
    EXPECT_NEAR(r.x, 277.78f, 0.01f);
    EXPECT_FLOAT_EQ(r.y, 0);
    float fx, fy;
    EXPECT_FALSE(BoxToFrame(100, 350, 1800, 700, 1280, 720, true, fx, fy));
    ASSERT_TRUE(BoxToFrame(900, 350, 1800, 700, 1280, 720, true, fx, fy));
    EXPECT_NEAR(fx, 640, 1e-2);
    EXPECT_NEAR(fy, 360, 1e-2);
}

// A phone's canvas in CSS pixels, 16:9 inside a portrait page.
TEST(PointerMapTest, APhoneCanvasIsTheFrameShrunk) {
    float fx, fy;
    ASSERT_TRUE(BoxToFrame(412 / 2.0f, 231.75f / 2, 412, 231.75f, 1280, 720, true, fx, fy));
    EXPECT_NEAR(fx, 640, 0.01);
    EXPECT_NEAR(fy, 360, 0.01);
}

TEST(PointerMapTest, StretchedHasNoBars) {
    float fx, fy;
    ASSERT_TRUE(BoxToFrame(800, 20, 1600, 1000, 1280, 720, false, fx, fy));
    EXPECT_FLOAT_EQ(fx, 640);
    EXPECT_FLOAT_EQ(fy, 14.4f);
}

std::vector<Row> Rows() {
    std::vector<Row> rows;
    for (int i = 0; i < 5; i++) {
        Row r;
        r.index = 10 + i;
        r.cx = 900;
        r.cy = 200 + 60.0f * i;
        r.halfW = 200;
        r.halfH = 30;
        rows.push_back(r);
    }
    return rows;
}

TEST(PointerHitTest, APointOnARowIsThatRow) {
    const auto rows = Rows();
    EXPECT_EQ(HitRow(rows, 900, 200), 10);
    EXPECT_EQ(HitRow(rows, 1050, 325), 12);
    EXPECT_EQ(HitRow(rows, 750, 445), 14);
}

TEST(PointerHitTest, OutsideTheRow) {
    const auto rows = Rows();
    EXPECT_EQ(HitRow(rows, 650, 200), -1); // left of the ribbon
    EXPECT_EQ(HitRow(rows, 900, 120), -1); // above the first row
    EXPECT_EQ(HitRow(rows, 900, 520), -1); // below the last
}

TEST(PointerHitTest, AHeaderIsNotATarget) {
    auto rows = Rows();
    rows[2].active = false;
    EXPECT_EQ(HitRow(rows, 900, 320), -1);
}

TEST(PointerHitTest, OverlappingRowsGoToTheNearestCentre) {
    auto rows = Rows();
    for (Row &r : rows)
        r.halfH = 50; // a swelled ribbon
    EXPECT_EQ(HitRow(rows, 900, 285), 11);
    EXPECT_EQ(HitRow(rows, 900, 295), 12);
}

TEST(PointerHitTest, RowPitchIsTheMedianGap) {
    auto rows = Rows();
    rows[4].cy = 520; // one swelled gap
    EXPECT_FLOAT_EQ(RowPitch(rows, 1), 60);
    EXPECT_FLOAT_EQ(RowPitch({}, 42), 42);
}

// A finger, events at 60 Hz.
struct Finger {
    Tracker tr;
    Params p;
    float t = 0;
    std::vector<Output> outs;
    int steps = 0, taps = 0;
    float tapX = 0, tapY = 0;

    void Ev(Kind k, float x, float y, bool touch = true) {
        Input in;
        in.kind = k;
        in.x = x;
        in.y = y;
        in.t = t;
        in.touch = touch;
        tr.Feed(in, p);
    }
    void Frame(float dt = 1.0f / 60) {
        t += dt;
        Output o = tr.Tick(t, 60.0f, p);
        steps += o.steps;
        if (o.tap) {
            taps++;
            tapX = o.tapX;
            tapY = o.tapY;
        }
        outs.push_back(o);
    }
    // a straight drag from y0 to y1 over `seconds`, then release
    void Drag(float y0, float y1, float seconds, bool release = true) {
        Ev(kDown, 640, y0);
        Frame();
        const int n = std::max(1, (int)(seconds * 60));
        for (int i = 1; i <= n; i++) {
            Ev(kMove, 640, y0 + (y1 - y0) * i / n);
            Frame();
        }
        if (release)
            Ev(kUp, 640, y1);
        Frame();
    }
};

TEST(PointerTrackerTest, ATapIsATap) {
    Finger f;
    f.Ev(kDown, 900, 320);
    f.Frame();
    f.Ev(kMove, 906, 326); // a finger's wobble, inside the slop
    f.Frame();
    f.Ev(kUp, 906, 326);
    f.Frame();
    EXPECT_EQ(f.taps, 1);
    EXPECT_FLOAT_EQ(f.tapX, 900); // where it went down
    EXPECT_FLOAT_EQ(f.tapY, 320);
    EXPECT_EQ(f.steps, 0);
}

TEST(PointerTrackerTest, ADragNeverTaps) {
    Finger f;
    f.Drag(500, 260, 1.0f); // slow: 240 px/s
    EXPECT_EQ(f.taps, 0);
    // 240 px of travel, 60 px a row: 4 rows later
    EXPECT_EQ(f.steps, 4);
    for (int i = 0; i < 60; i++)
        f.Frame();
    EXPECT_EQ(f.steps, 4); // slow release: no fling
    EXPECT_FALSE(f.tr.Flinging());
}

TEST(PointerTrackerTest, ADragDownGoesBack) {
    Finger f;
    f.Drag(200, 380, 1.0f);
    EXPECT_EQ(f.steps, -3);
}

TEST(PointerTrackerTest, AFlickFlings) {
    Finger f;
    f.Drag(500, 380, 0.1f); // 1200 px/s
    const int atRelease = f.steps;
    EXPECT_TRUE(f.tr.Flinging());
    for (int i = 0; i < 300 && f.tr.Flinging(); i++)
        f.Frame();
    EXPECT_FALSE(f.tr.Flinging()); // it stops
    EXPECT_GT(f.steps, atRelease + 2); // and it went on after the finger left
    EXPECT_LE(f.steps, atRelease + 40 + 1); // never past the cap
    EXPECT_EQ(f.taps, 0);
}

TEST(PointerTrackerTest, APressThatStopsAFlingPicksNothing) {
    Finger f;
    f.Drag(500, 380, 0.1f);
    ASSERT_TRUE(f.tr.Flinging());
    f.Frame();
    f.Ev(kDown, 640, 300);
    f.Frame();
    EXPECT_FALSE(f.tr.Flinging());
    const int stopped = f.steps;
    f.Ev(kUp, 640, 300);
    f.Frame();
    f.Frame();
    EXPECT_EQ(f.taps, 0);
    EXPECT_EQ(f.steps, stopped);
    // the next tap is a tap again
    f.Ev(kDown, 640, 300);
    f.Frame();
    f.Ev(kUp, 640, 300);
    f.Frame();
    EXPECT_EQ(f.taps, 1);
}

TEST(PointerTrackerTest, TheWheelScrollsWholeRows) {
    Finger f;
    Input in;
    in.kind = kWheel;
    in.wheel = 1.5f;
    f.tr.Feed(in, f.p);
    f.Frame();
    EXPECT_EQ(f.steps, 1);
    f.tr.Feed(in, f.p);
    f.Frame();
    EXPECT_EQ(f.steps, 3);
}

TEST(PointerTrackerTest, AFingerHasNoHoverAMouseDoes) {
    Finger f;
    f.Ev(kMove, 100, 100, true);
    f.Frame();
    EXPECT_FALSE(f.outs.back().hover);
    f.Ev(kMove, 120, 140, false);
    f.Frame();
    ASSERT_TRUE(f.outs.back().hover);
    EXPECT_FLOAT_EQ(f.outs.back().x, 120);
    f.Frame();
    EXPECT_FALSE(f.outs.back().hover); // only when it moved
}

TEST(PointerTrackerTest, TheSecondaryButtonIsBack) {
    Finger f;
    f.Ev(kBack, 0, 0, false);
    f.Frame();
    EXPECT_TRUE(f.outs.back().back);
    EXPECT_EQ(f.taps, 0);
}

TEST(PointerTrackerTest, ACancelledPressIsNotATap) {
    Finger f;
    f.Ev(kDown, 900, 320);
    f.Frame();
    f.Ev(kCancel, 900, 320);
    f.Frame();
    f.Ev(kUp, 900, 320);
    f.Frame();
    EXPECT_EQ(f.taps, 0);
}

} // namespace
