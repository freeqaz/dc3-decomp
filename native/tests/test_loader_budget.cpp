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
    // The game panel's load handler sets 14.
    applied = b.Step(14.0f, 120.0);
    EXPECT_NEAR(applied, 7.0f, 1e-5f);
    EXPECT_EQ(b.ImageMs(), 14.0f);
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
    // At 84 fps 14 ms scales to exactly 10: the image's later 10 must still
    // read as the image's and be scaled, not kept.
    Dc3LoaderBudget b;
    float applied = b.Step(14.0f, 84.0);
    EXPECT_NEAR(applied, 10.0f, 1e-5f);
    EXPECT_NE(applied, 10.0f);
    applied = b.Step(10.0f, 84.0);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    EXPECT_NEAR(applied, 10.0f * 60.0f / 84.0f, 1e-4f);
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
