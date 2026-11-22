// test_perf_window.cpp -- the engine's MILO_PERF line (milo-native-engine
// gfx/PerfWindow.h).
//
// The line used to print "N fps" with N = frames / summed render time, i.e.
// 1 / the render time, and closed its window after 5 s of summed render TIME:
// on the GPD (2026-11-22) a game presenting at 72 printed "433.6 fps", and one
// line covered ~30 s of play, so a 642 ms frame could not be placed. Now the
// window is 5 s of WALL time and the rate is frames per wall second; the
// render time is reported as render time.
#include "gfx/PerfWindow.h"

#include <gtest/gtest.h>
#include <string>

namespace {

// Frames every `period` s, each rendering for `render` s, until the window
// closes. Returns the number of frames fed.
int FeedUntilFull(milo::PerfWindow &w, double &t, double period, double render) {
    int n = 0;
    while (true) {
        n++;
        const bool full = w.AddFrame(t, t + render, 100, 10);
        t += period;
        if (full) return n;
        if (n > 100000) return -1;
    }
}

} // namespace

// A game presenting at 60 whose frames render in 2 ms: the rate is 60, not
// 500, and the window closes after 5 s of wall time, not 5 s of rendering.
TEST(PerfWindow, RateIsFramesPerWallSecondNotOneOverRenderTime) {
    milo::PerfWindow w(5.0);
    double t = 100.0;
    const int n = FeedUntilFull(w, t, 1.0 / 60.0, 0.002);
    EXPECT_NEAR(n, 301, 1) << "the window must close after 5 s of wall time";
    EXPECT_NEAR(w.FramesPerSec(), 60.0, 0.25);
    EXPECT_NEAR(w.RenderAvgMs(), 2.0, 1e-6);
    EXPECT_EQ(w.OverBudget(), 0);
    // The first window opens at a frame's START (one render early); from the
    // second on, a window opens where the last closed and steady 60 reads 60.0.
    w.Reset();
    FeedUntilFull(w, t, 1.0 / 60.0, 0.002);
    char line[256];
    w.Format(line, sizeof(line));
    const std::string s(line);
    EXPECT_EQ(s.find("fps"), std::string::npos) << s;
    EXPECT_NE(s.find("(60.0 a second)"), std::string::npos) << s;
    EXPECT_NE(s.find("render avg 2.00 ms"), std::string::npos) << s;
    EXPECT_NE(s.find("100 draws/frame (10 not sent: invisible)"), std::string::npos) << s;
}

// One long frame is reported as the max render time and counted over budget;
// the window still closes on wall time, and Reset starts a fresh one.
TEST(PerfWindow, LongFrameIsMaxAndOverBudgetAndResetClears) {
    milo::PerfWindow w(5.0);
    double t = 0.0;
    w.AddFrame(t, t + 0.642, 1, 0);
    t += 0.642;
    FeedUntilFull(w, t, 1.0 / 72.0, 0.002);
    EXPECT_NEAR(w.RenderMaxMs(), 642.0, 1e-6);
    EXPECT_EQ(w.OverBudget(), 1);
    EXPECT_GE(w.WallSec(), 5.0);
    EXPECT_LT(w.WallSec(), 5.1);
    w.Reset();
    EXPECT_EQ(w.Frames(), 0);
    EXPECT_EQ(w.RenderMaxMs(), 0.0);
    EXPECT_EQ(w.FramesPerSec(), 0.0);
    // The next window opens where this one closed: steady 72 reads 72.
    FeedUntilFull(w, t, 1.0 / 72.0, 0.002);
    EXPECT_NEAR(w.FramesPerSec(), 72.0, 0.1);
    EXPECT_EQ(w.OverBudget(), 0);
}
