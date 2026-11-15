// PLAN H-107: on native the loader's per-poll budget follows the frame period,
// so the image's duty ratio (10 ms of a 16.7 ms frame) is kept at any cap.
// The engine-run half (a capped boot logs the budget the loader sees) is in
// test_headless_boot.cpp (HeadlessBootTest.LoaderBudget*).

#include <gtest/gtest.h>

#include <cmath>

#include "platform/LoaderBudget_Native.h"

TEST(LoaderBudget, ScalesByFramePeriod) {
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(10.0, 120.0), 5.0);
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(10.0, 60.0), 10.0);
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(14.0, 120.0), 7.0);
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(10.0, 240.0), 2.5);
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(10.0, 30.0), 20.0);
}

TEST(LoaderBudget, NoTargetKeepsTheImagesValue) {
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(10.0, 0.0), 10.0);
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(14.0, -1.0), 14.0);
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(10.0, INFINITY), 10.0);
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(10.0, NAN), 10.0);
    // <= 0 is the image's "do not slice"; it is never scaled into a budget.
    EXPECT_DOUBLE_EQ(Dc3LoaderBudgetMs(0.0, 120.0), 0.0);
}

TEST(LoaderBudget, StepTracksWhatTheImageSets) {
    Dc3LoaderBudget b;
    // LoadMgr's default.
    float applied = b.Step(10.0f, 120.0);
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    // Unchanged since: still the image's 10, not 5 scaled again.
    EXPECT_EQ(b.Step(applied, 120.0), applied);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    // The game panel's load handler sets 14, a loading screen's share: kept.
    applied = b.Step(14.0f, 120.0);
    EXPECT_EQ(applied, 14.0f);
    EXPECT_EQ(b.ImageMs(), 14.0f);
    EXPECT_EQ(b.Step(applied, 120.0), 14.0f);
    // finish_load sets 10 again.
    applied = b.Step(10.0f, 120.0);
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
    EXPECT_EQ(b.ImageMs(), 10.0f);
}

TEST(LoaderBudget, StepAtSixtyAndUncappedLeavesTheValue) {
    Dc3LoaderBudget b;
    EXPECT_EQ(b.Step(10.0f, 60.0), 10.0f);
    EXPECT_EQ(b.Step(14.0f, 60.0), 14.0f);
    Dc3LoaderBudget u;
    EXPECT_EQ(u.Step(10.0f, 0.0), 10.0f);
    EXPECT_EQ(u.Step(14.0f, 0.0), 14.0f);
    EXPECT_EQ(u.Step(10.0f, 0.0), 10.0f);
}

TEST(LoaderBudget, AWholeScaledValueIsNotMistakenForTheImages) {
    // At 100 fps 10 ms scales to exactly 6: a later 6 from the image (the DTA
    // can set any value with set_loader_period) must still read as the
    // image's and be scaled, not kept.
    Dc3LoaderBudget b;
    float applied = b.Step(10.0f, 100.0);
    EXPECT_NEAR(applied, 6.0f, 1e-5f);
    EXPECT_NE(applied, 6.0f);
    applied = b.Step(6.0f, 100.0);
    EXPECT_EQ(b.ImageMs(), 6.0f);
    EXPECT_NEAR(applied, 3.6f, 1e-4f);
}

TEST(LoaderBudget, ALoadingScreensRaisedShareIsKept) {
    // Above LoadMgr's 10 ms default is the image's loading-screen share.
    Dc3LoaderBudget b;
    EXPECT_EQ(b.Step(14.0f, 120.0), 14.0f);
    EXPECT_EQ(b.FloorMs(), 14.0f);
    b.EndFrame(1.0, 120.0, true);
    EXPECT_EQ(b.Step(14.0f, 120.0), 14.0f);
    EXPECT_EQ(b.Step(14.0f, 240.0), 14.0f);
    // At or under the default it is scaled.
    EXPECT_NEAR(b.Step(10.0f, 120.0), 5.0f, 1e-5f);
    Dc3LoaderBudget c;
    EXPECT_NEAR(c.Step(8.0f, 120.0), 4.0f, 1e-5f);
}

TEST(LoaderBudget, FollowsAChangedFrameTarget) {
    Dc3LoaderBudget b;
    EXPECT_NEAR(b.Step(10.0f, 120.0), 5.0f, 1e-5f);
    // The web gate decides later, or the cap changes: the image's 10 is
    // rescaled, never the 5 already applied.
    float applied = b.Step(b.AppliedMs(), 60.0);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    EXPECT_EQ(applied, 10.0f);
    applied = b.Step(applied, 0.0);
    EXPECT_EQ(applied, 10.0f);
}

// The scaled value is a floor: a loading frame's spare time (its period less
// a 0.5 ms margin less what it took) is added to the next frame's budget, up
// to the image's value; an overrun takes it back; an idle loader drops it.
TEST(LoaderBudget, SpareTimeRaisesTheBudgetUpToTheImagesValue) {
    Dc3LoaderBudget b;
    const double period = 1000.0 / 120.0;
    float applied = b.Step(10.0f, 120.0);
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
    // A loading frame that took 6 ms of its 8.33: 1.83 ms to spare.
    b.EndFrame(6.0, 120.0, true);
    EXPECT_NEAR(b.SpareMs(), period - 0.5 - 6.0, 1e-4);
    applied = b.Step(applied, 120.0);
    EXPECT_NEAR(applied, 5.0f + (float)(period - 0.5 - 6.0), 1e-4f);
    EXPECT_EQ(b.ImageMs(), 10.0f) << "our own raised value was read as the image's";
    // A frame that uses its period exactly (less the margin) keeps it.
    const float held = b.SpareMs();
    b.EndFrame(period - 0.5, 120.0, true);
    EXPECT_NEAR(b.SpareMs(), held, 1e-5f);
    // Never more than the image's own value.
    for (int i = 0; i < 20; i++) {
        applied = b.Step(applied, 120.0);
        b.EndFrame(0.5, 120.0, true);
    }
    EXPECT_NEAR(b.Step(applied, 120.0), 10.0f, 1e-5f);
    // A frame 3 ms over its period takes 3 ms back.
    b.EndFrame(period - 0.5 + 3.0, 120.0, true);
    EXPECT_NEAR(b.SpareMs(), 2.0f, 1e-4f);
    // Over by more than the spare: back to the floor, never below it.
    b.EndFrame(period + 20.0, 120.0, true);
    EXPECT_EQ(b.SpareMs(), 0.0f);
    EXPECT_NEAR(b.Step(b.AppliedMs(), 120.0), 5.0f, 1e-5f);
}

TEST(LoaderBudget, AnIdleLoaderStartsTheNextLoadAtTheFloor) {
    Dc3LoaderBudget b;
    float applied = b.Step(10.0f, 120.0);
    b.EndFrame(1.0, 120.0, true);
    applied = b.Step(applied, 120.0);
    EXPECT_GT(applied, 5.5f);
    // Nothing loading at the end of a frame (the song playing): a frame with
    // room to spare must not bank it for the next load's first frame.
    for (int i = 0; i < 10; i++) {
        b.EndFrame(1.0, 120.0, false);
        applied = b.Step(applied, 120.0);
    }
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
}

TEST(LoaderBudget, ANewImageValueStartsAtItsFloor) {
    Dc3LoaderBudget b;
    float applied = b.Step(10.0f, 120.0);
    b.EndFrame(1.0, 120.0, true);
    b.Step(applied, 120.0);
    // finish_load-style 10 again after a 14: 5 ms, no carried spare time.
    EXPECT_EQ(b.Step(14.0f, 120.0), 14.0f);
    EXPECT_NEAR(b.Step(10.0f, 120.0), 5.0f, 1e-5f);
}

TEST(LoaderBudget, NoSpareTimeWithoutAFrameTargetOrAtSixty) {
    Dc3LoaderBudget u;
    EXPECT_EQ(u.Step(10.0f, 0.0), 10.0f);
    u.EndFrame(1.0, 0.0, true);
    EXPECT_EQ(u.SpareMs(), 0.0f);
    EXPECT_EQ(u.Step(10.0f, 0.0), 10.0f);
    Dc3LoaderBudget s;
    EXPECT_EQ(s.Step(10.0f, 60.0), 10.0f);
    s.EndFrame(1.0, 60.0, true);
    EXPECT_EQ(s.SpareMs(), 0.0f);
    EXPECT_EQ(s.Step(10.0f, 60.0), 10.0f);
}
