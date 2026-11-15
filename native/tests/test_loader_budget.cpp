// PLAN H-107: on native, while gameplay is on screen, the loader's per-poll
// budget follows the frame period, so the image's duty ratio (10 ms of a
// 16.7 ms frame) is kept at any cap; everywhere else it is the image's value.
// The engine-run halves: a capped boot keeps the image's value outside
// gameplay (HeadlessBootTest.LoaderBudget*, test_headless_boot.cpp), and the
// budget the loader holds in gameplay at a 120 and a 60 fps target
// (LoaderBudgetGameplayTest, test_native_rehearse.cpp).

#include <gtest/gtest.h>

#include <cmath>

#include "platform/LoaderBudget_Native.h"

constexpr bool kGameplay = true;
constexpr bool kNoGameplay = false;

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

TEST(LoaderBudget, GameplayIsScaled) {
    Dc3LoaderBudget b;
    float applied = b.Step(10.0f, 120.0, kGameplay);
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    // Unchanged since: still the image's 10, not 5 scaled again.
    EXPECT_EQ(b.Step(applied, 120.0, kGameplay), applied);
    EXPECT_EQ(b.ImageMs(), 10.0f);
}

TEST(LoaderBudget, OutsideGameplayTheImagesValueIsKept) {
    // Boot, the menus and the loading screens (10, and the 14 a loading
    // screen sets) at any target.
    Dc3LoaderBudget b;
    EXPECT_EQ(b.Step(10.0f, 120.0, kNoGameplay), 10.0f);
    EXPECT_EQ(b.Step(14.0f, 120.0, kNoGameplay), 14.0f);
    EXPECT_EQ(b.Step(14.0f, 240.0, kNoGameplay), 14.0f);
    EXPECT_EQ(b.Step(10.0f, 240.0, kNoGameplay), 10.0f);
    EXPECT_EQ(b.ImageMs(), 10.0f);
}

TEST(LoaderBudget, LeavingGameplayPutsTheImagesValueBack) {
    // Song select -> game -> results -> the menus: what the image had is what
    // the loader holds again, never our 5 left behind.
    Dc3LoaderBudget b;
    float applied = b.Step(10.0f, 120.0, kNoGameplay);
    EXPECT_EQ(applied, 10.0f);
    applied = b.Step(applied, 120.0, kGameplay);
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
    applied = b.Step(applied, 120.0, kGameplay);
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
    applied = b.Step(applied, 120.0, kNoGameplay);
    EXPECT_EQ(applied, 10.0f);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    // And back into gameplay (the next song).
    EXPECT_NEAR(b.Step(applied, 120.0, kGameplay), 5.0f, 1e-5f);
}

TEST(LoaderBudget, StepTracksWhatTheImageSetsDuringGameplay) {
    // The DTA can set any value with set_loader_period during gameplay: it is
    // the image's from then on, scaled.
    Dc3LoaderBudget b;
    float applied = b.Step(10.0f, 120.0, kGameplay);
    applied = b.Step(14.0f, 120.0, kGameplay);
    EXPECT_EQ(b.ImageMs(), 14.0f);
    EXPECT_NEAR(applied, 7.0f, 1e-5f);
    applied = b.Step(10.0f, 120.0, kGameplay);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    EXPECT_NEAR(applied, 5.0f, 1e-5f);
}

TEST(LoaderBudget, AtSixtyAndUncappedGameplayLeavesTheValue) {
    Dc3LoaderBudget b;
    EXPECT_EQ(b.Step(10.0f, 60.0, kGameplay), 10.0f);
    EXPECT_EQ(b.Step(14.0f, 60.0, kGameplay), 14.0f);
    Dc3LoaderBudget u;
    EXPECT_EQ(u.Step(10.0f, 0.0, kGameplay), 10.0f);
    EXPECT_EQ(u.Step(14.0f, 0.0, kGameplay), 14.0f);
    EXPECT_EQ(u.Step(10.0f, 0.0, kGameplay), 10.0f);
}

TEST(LoaderBudget, AWholeScaledValueIsNotMistakenForTheImages) {
    // At 100 fps 10 ms scales to exactly 6: a later 6 from the image must
    // still read as the image's and be scaled, not kept.
    Dc3LoaderBudget b;
    float applied = b.Step(10.0f, 100.0, kGameplay);
    EXPECT_NEAR(applied, 6.0f, 1e-5f);
    EXPECT_NE(applied, 6.0f);
    applied = b.Step(6.0f, 100.0, kGameplay);
    EXPECT_EQ(b.ImageMs(), 6.0f);
    EXPECT_NEAR(applied, 3.6f, 1e-4f);
}

TEST(LoaderBudget, FollowsAChangedFrameTarget) {
    Dc3LoaderBudget b;
    EXPECT_NEAR(b.Step(10.0f, 120.0, kGameplay), 5.0f, 1e-5f);
    // The web gate decides later, or the cap changes: the image's 10 is
    // rescaled, never the 5 already applied.
    float applied = b.Step(b.AppliedMs(), 60.0, kGameplay);
    EXPECT_EQ(b.ImageMs(), 10.0f);
    EXPECT_EQ(applied, 10.0f);
    applied = b.Step(applied, 0.0, kGameplay);
    EXPECT_EQ(applied, 10.0f);
}
