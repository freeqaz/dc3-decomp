// BackgroundMute (engine, audio/BackgroundMute.h): the output goes silent when
// the game window is in the background and the main loop is starved, and only
// then. Reported from a handheld session: with the game on another workspace
// the compositor presented one frame a second and the music played in bursts.

#include "audio/BackgroundMute.h"

#include <gtest/gtest.h>

namespace {

// Run frames at a fixed interval from `now`, asking the audio thread every 10 ms
// in between as the device callback does. Returns the time after the last frame.
int64_t RunFrames(BackgroundMute &m, int64_t now, int frames, int64_t intervalMs, bool focused) {
    for (int i = 0; i < frames; i++) {
        for (int64_t t = 10; t < intervalMs; t += 10) m.ShouldMute(now + t);
        now += intervalMs;
        m.OnFrame(now, focused);
    }
    return now;
}

TEST(BackgroundMute, NothingMutesBeforeTheFirstFrame) {
    BackgroundMute m;
    // Headless and the web never call OnFrame.
    EXPECT_FALSE(m.ShouldMute(0));
    EXPECT_FALSE(m.ShouldMute(1000000));
}

TEST(BackgroundMute, StarvedInTheBackgroundMutes) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 60, 16, true);
    m.OnFrame(now, false); // focus goes to another workspace
    EXPECT_FALSE(m.ShouldMute(now + 100));
    EXPECT_FALSE(m.ShouldMute(now + BackgroundMute::kStallMs));
    // Decided by the audio thread while the main loop is still in the long frame.
    EXPECT_TRUE(m.ShouldMute(now + BackgroundMute::kStallMs + 1));
    EXPECT_TRUE(m.IsMuted());
    // One frame a second keeps it muted.
    now = RunFrames(m, now, 20, 1000, false);
    EXPECT_TRUE(m.ShouldMute(now + 10));
}

TEST(BackgroundMute, FocusBringsItBackAtOnce) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 5, 1000, false);
    ASSERT_TRUE(m.IsMuted());
    m.OnFrame(now + 1000, true);
    EXPECT_FALSE(m.IsMuted());
    EXPECT_FALSE(m.ShouldMute(now + 1010));
}

TEST(BackgroundMute, DrawingAgainWithoutFocusBringsItBack) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 5, 1000, false);
    ASSERT_TRUE(m.IsMuted());
    // Visible again beside the focused window: full-rate frames, still no focus.
    now = RunFrames(m, now, BackgroundMute::kResumeFrames - 1, 16, false);
    EXPECT_TRUE(m.IsMuted()) << "resumed before a steady run of frames";
    now = RunFrames(m, now, 1, 16, false);
    EXPECT_FALSE(m.IsMuted());
    EXPECT_FALSE(m.ShouldMute(now + 10));
}

TEST(BackgroundMute, OneQuickFrameAmongSlowOnesDoesNotResume) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 5, 1000, false);
    ASSERT_TRUE(m.IsMuted());
    for (int i = 0; i < 40; i++) {
        now = RunFrames(m, now, 3, 16, false);
        now = RunFrames(m, now, 1, 1000, false);
    }
    EXPECT_TRUE(m.IsMuted());
}

TEST(BackgroundMute, AFocusedWindowNeverMutes) {
    BackgroundMute m;
    // Long frames while loading, window in front.
    int64_t now = RunFrames(m, 1000, 10, 3000, true);
    EXPECT_FALSE(m.IsMuted());
    EXPECT_FALSE(m.ShouldMute(now + 2999));
}

TEST(BackgroundMute, AnUnfocusedWindowAtFullRateNeverMutes) {
    BackgroundMute m;
    // The game beside a terminal that has the keyboard.
    int64_t now = RunFrames(m, 1000, 600, 16, false);
    EXPECT_FALSE(m.IsMuted());
    // Frames a little slow are not a stall either.
    now = RunFrames(m, now, 20, BackgroundMute::kStallMs, false);
    EXPECT_FALSE(m.IsMuted());
}

TEST(BackgroundMute, DisabledNeverMutes) {
    BackgroundMute m;
    m.SetEnabled(false);
    int64_t now = RunFrames(m, 1000, 5, 1000, false);
    EXPECT_FALSE(m.IsMuted());
    EXPECT_FALSE(m.ShouldMute(now + 5000));
}

} // namespace
