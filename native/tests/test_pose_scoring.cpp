// Scoring believability (PLAN P-7, docs/native/POSE_INPUT.md "Scoring
// believability").
//
// PosePerformerClockTest: the in-process performer (DC3_POSE_PERFORM) is the
// instrument the P-7 matrix was measured with.  Move scoring keeps, per
// reference frame and per error-node component, the BEST error over every
// player frame inside the ham2 time window (DetectFrame::AddError, +-0.23 s),
// so the number of player frames per song second is an input to the score:
// measured on Better Off Alone (Easy) with the same noisy choreography, 120 Hz
// gives 77/77 perfect, 30 Hz 75/77, 15 Hz 50/77.  The Kinect delivers 30 Hz.
// The performer must therefore emit 30 frames per second of SONG time however
// fast the game loop runs -- that is what these tests pin.
//
// SensorLatencyTest: DC3_POSE_LATENCY_MS replaces scoring.dta's Kinect-tuned
// latency_offset when MoveDir::LoadScoring runs (HX_NATIVE call there).

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "../src/platform/PosePerformer_Native.h"
#include "hamobj/MoveDir.h"
#include "obj/Data.h"
#include "os/System.h"

#include <cmath>
#include <cstdlib>

float Dc3SensorLatencySeconds(float scoringDtaSeconds);

namespace {

int FramesOver(double seconds, double gameHz, double rateHz, double start = 0.0) {
    double next = -1e30;
    int n = 0;
    int steps = (int)llround(seconds * gameHz);
    for (int i = 0; i < steps; i++) {
        if (Dc3PosePerformer::FrameDue(start + i / gameHz, rateHz, next))
            n++;
    }
    return n;
}

TEST(PosePerformerClockTest, ThirtyFramesPerSongSecondAtAnyGameRate) {
    // DC3_FAST_TIME steps song time 1/120 s per game frame; a rendered run is
    // ~60 Hz; a loaded box is slower than 30.
    EXPECT_NEAR(FramesOver(10.0, 120.0, 30.0), 300, 1);
    EXPECT_NEAR(FramesOver(10.0, 60.0, 30.0), 300, 1);
    EXPECT_NEAR(FramesOver(10.0, 144.0, 30.0), 300, 1);
    // Slower game than sensor: one frame per game frame, never a burst.
    EXPECT_NEAR(FramesOver(10.0, 20.0, 30.0), 200, 1);
    EXPECT_NEAR(FramesOver(10.0, 120.0, 15.0), 150, 1);
}

TEST(PosePerformerClockTest, ANewSongRestartsTheClock) {
    double next = -1e30;
    int n = 0;
    for (int i = 0; i < 1200; i++) // 10 s of song A
        n += Dc3PosePerformer::FrameDue(100.0 + i / 120.0, 30.0, next);
    // song B starts at -5 s (the count-in): the clock goes back
    EXPECT_TRUE(Dc3PosePerformer::FrameDue(-5.0, 30.0, next));
    int m = 0;
    for (int i = 1; i < 1200; i++)
        m += Dc3PosePerformer::FrameDue(-5.0 + i / 120.0, 30.0, next);
    EXPECT_NEAR(n, 300, 1);
    EXPECT_NEAR(m, 299, 1);
}

TEST(PosePerformerClockTest, HalfTempoPlaysEachMeasureFromItsDownbeat) {
    EXPECT_FLOAT_EQ(Dc3PosePerformer::HalfTempoBeat(8.0f), 8.0f);
    EXPECT_FLOAT_EQ(Dc3PosePerformer::HalfTempoBeat(10.0f), 9.0f);
    EXPECT_FLOAT_EQ(Dc3PosePerformer::HalfTempoBeat(11.5f), 9.75f);
    EXPECT_FLOAT_EQ(Dc3PosePerformer::HalfTempoBeat(12.0f), 12.0f);
}

TEST(PosePerformerClockTest, NoiseIsSeededAndStandardNormal) {
    Dc3PosePerformer::Rng a(7), b(7), c(8);
    double sum = 0, sum2 = 0;
    bool same = true, differs = false;
    for (int i = 0; i < 20000; i++) {
        double x = a.Gauss();
        same = same && x == b.Gauss();
        differs = differs || x != c.Gauss();
        sum += x;
        sum2 += x * x;
    }
    EXPECT_TRUE(same);
    EXPECT_TRUE(differs);
    EXPECT_NEAR(sum / 20000, 0.0, 0.03);
    EXPECT_NEAR(sum2 / 20000, 1.0, 0.05);
}

struct ScopedEnv {
    const char *name;
    explicit ScopedEnv(const char *n, const char *v) : name(n) {
        if (v)
            setenv(n, v, 1);
        else
            unsetenv(n);
    }
    ~ScopedEnv() { unsetenv(name); }
};

TEST(SensorLatencyTest, UnsetKeepsTheKinectValue) {
    ScopedEnv e("DC3_POSE_LATENCY_MS", nullptr);
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.1f);
}

TEST(SensorLatencyTest, TheSensorsOwnLatencyReplacesIt) {
    ScopedEnv e("DC3_POSE_LATENCY_MS", "180");
    EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.18f);
}

TEST(SensorLatencyTest, NonsenseIsIgnored) {
    for (const char *bad : { "abc", "-20", "5000", "" }) {
        ScopedEnv e("DC3_POSE_LATENCY_MS", bad);
        EXPECT_FLOAT_EQ(Dc3SensorLatencySeconds(0.1f), 0.1f) << bad;
    }
}

// The knob must reach the scorer: MoveDir::LoadScoring on the game's own
// config/scoring.dta (latency_offset 100).
TEST(SensorLatencyTest, LoadScoringAppliesIt) {
    if (!EnsureEngineInit())
        return;
    DataArray *cfg = SystemConfig()->FindArray("scoring", false);
    ASSERT_NE(cfg, nullptr);
    {
        ScopedEnv e("DC3_POSE_LATENCY_MS", nullptr);
        MoveDir::LoadScoring(cfg);
        EXPECT_NEAR(MoveDir::NativeLatencySeconds(), 0.100f, 1e-6f);
    }
    {
        ScopedEnv e("DC3_POSE_LATENCY_MS", "230");
        MoveDir::LoadScoring(cfg);
        EXPECT_NEAR(MoveDir::NativeLatencySeconds(), 0.230f, 1e-6f);
    }
    MoveDir::LoadScoring(cfg); // leave the image's value for later tests
    EXPECT_NEAR(MoveDir::NativeLatencySeconds(), 0.100f, 1e-6f);
}

} // namespace
