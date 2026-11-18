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

// A frame that spent `waitMs` of its `intervalMs` inside one wait on the
// compositor (acquire or present), the audio thread asking every 10 ms.
int64_t RunWaitingFrames(BackgroundMute &m, int64_t now, int frames, int64_t intervalMs,
                         int64_t waitMs, bool focused) {
    for (int i = 0; i < frames; i++) {
        m.BeginCompositorWait(now);
        for (int64_t t = 10; t < waitMs; t += 10) m.ShouldMute(now + t);
        m.EndCompositorWait(now + waitMs);
        for (int64_t t = waitMs + 10; t < intervalMs; t += 10) m.ShouldMute(now + t);
        now += intervalMs;
        m.OnFrame(now, focused);
    }
    return now;
}

// Hyprland, an empty workspace: the hidden window keeps focus and gets one
// frame a second (GPD, 2026-11-15). The wait on the compositor is the signal.
TEST(BackgroundMute, AFocusedWindowStuckOnTheCompositorMutes) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 60, 16, true);
    m.BeginCompositorWait(now);
    EXPECT_FALSE(m.ShouldMute(now + BackgroundMute::kStallMs));
    EXPECT_TRUE(m.ShouldMute(now + BackgroundMute::kStallMs + 1));
    EXPECT_TRUE(m.IsMuted());
}

TEST(BackgroundMute, AFocusedHiddenWindowStaysMutedBetweenItsFrames) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 60, 16, true);
    now = RunWaitingFrames(m, now, 1, 1000, 990, true);
    ASSERT_TRUE(m.IsMuted());
    // Each one-a-second frame arrives with focus; none of them may resume, or
    // half of every second would play.
    for (int i = 0; i < 10; i++) {
        now = RunWaitingFrames(m, now, 1, 1000, 990, true);
        EXPECT_TRUE(m.IsMuted()) << "frame " << i << " resumed a hidden window";
        EXPECT_TRUE(m.ShouldMute(now + 10));
    }
}

TEST(BackgroundMute, ShownAgainWithFocusItComesBackOnTheFirstFrameThatDidNotWait) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 60, 16, true);
    now = RunWaitingFrames(m, now, 3, 1000, 990, true);
    ASSERT_TRUE(m.IsMuted());
    now = RunWaitingFrames(m, now, 1, 16, 8, true); // an ordinary vsync wait
    EXPECT_FALSE(m.IsMuted());
    EXPECT_FALSE(m.ShouldMute(now + 10));
}

TEST(BackgroundMute, OrdinaryVsyncWaitsNeverMute) {
    BackgroundMute m;
    int64_t now = RunWaitingFrames(m, 1000, 600, 16, 12, true);
    EXPECT_FALSE(m.IsMuted());
    now = RunWaitingFrames(m, now, 600, 42, 40, false); // 24 Hz, beside another window
    EXPECT_FALSE(m.IsMuted());
}

TEST(BackgroundMute, AWaitThatEndedDoesNotMuteALongFocusedFrame) {
    BackgroundMute m;
    int64_t now = RunFrames(m, 1000, 60, 16, true);
    // The wait is over; the rest of a 3 s frame is the game's own (a load).
    m.BeginCompositorWait(now);
    m.EndCompositorWait(now + 10);
    for (int64_t t = 20; t < 3000; t += 10) EXPECT_FALSE(m.ShouldMute(now + t));
    m.OnFrame(now + 3000, true);
    EXPECT_FALSE(m.IsMuted());
}

} // namespace
