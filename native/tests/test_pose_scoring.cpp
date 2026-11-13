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
#include "shell_timeout.h"
#include "platform/ExecutablePath.h"
#include "../src/platform/PosePerformer_Native.h"
#include "hamobj/MoveDir.h"
#include "obj/Data.h"
#include "os/System.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <vector>

float Dc3SensorLatencySeconds(float scoringDtaSeconds);
float Dc3ScoringDtaLatencySeconds(float scorerSeconds);

namespace {

int FramesOver(double seconds, double gameHz, double rateHz, double start = 0.0) {
    double next = -1e30;
    int n = 0;
    int steps = (int)llround(seconds * gameHz);
    for (int i = 0; i < steps; i++) {
        if (Dc3PosePerformer::FrameDue(start + i / gameHz, rateHz, next) != Dc3PosePerformer::kNotDue)
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

// The performer restarts its noise stream on kResync, which is what makes a
// noisy cell repeat run to run (measured before: 2,391,589 vs 2,412,351 for
// one cell, because the menus' frame count shifted the stream).
TEST(PosePerformerClockTest, ANewSongRestartsTheClock) {
    using namespace Dc3PosePerformer;
    double next = -1e30;
    int n = 0, resyncs = 0;
    EXPECT_EQ(FrameDue(100.0, 30.0, next), kResync); // first frame
    for (int i = 1; i < 1200; i++) { // 10 s of song A
        FrameTick t = FrameDue(100.0 + i / 120.0, 30.0, next);
        n += t != kNotDue;
        resyncs += t == kResync;
    }
    // song B starts at -5 s (the count-in): the clock goes back
    EXPECT_EQ(FrameDue(-5.0, 30.0, next), kResync);
    int m = 0;
    for (int i = 1; i < 1200; i++) {
        FrameTick t = FrameDue(-5.0 + i / 120.0, 30.0, next);
        m += t != kNotDue;
        resyncs += t == kResync;
    }
    EXPECT_NEAR(n, 299, 1);
    EXPECT_NEAR(m, 299, 1);
    EXPECT_EQ(resyncs, 0); // a steady clock never resyncs
    EXPECT_EQ(FrameDue(-5.0 + 1200 / 120.0 + 3.0, 30.0, next), kResync); // a seek
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

// DC3_POSE_PERFORM=replay:FILE reads the same dc3-pose-fixture v1 files as
// scripts/synthetic_kinect.py --replay: the checked-in MediaPipe fixture loads
// whole, and an untracked line becomes a frame with nobody in it.
TEST(PosePerformerReplayTest, ReadsTheCheckedInFixture) {
    std::vector<Dc3PosePerformer::ReplayFrame> frames;
    ASSERT_TRUE(Dc3PosePerformer::LoadReplay(
        DC3_DECOMP_SRC_DIR "/../native/tests/data/pose/taye01_stand_dance.dc3pose", frames
    ));
    ASSERT_EQ(frames.size(), 240u);
    EXPECT_DOUBLE_EQ(frames[0].t, 0.0);
    EXPECT_NEAR(frames[1].t, 1.0 / 30.0, 1e-4);
    EXPECT_NEAR(frames[0].joints[0].x, -0.0133f, 1e-6f);
    EXPECT_NEAR(frames[0].joints[0].z, 2.5023f, 1e-6f);
    EXPECT_NEAR(frames[0].conf[0], 0.9994f, 1e-6f);
    for (size_t i = 1; i < frames.size(); i++)
        ASSERT_GT(frames[i].t, frames[i - 1].t) << i;
}

TEST(PosePerformerReplayTest, AnUntrackedLineIsNobody) {
    char path[] = "/tmp/dc3poseXXXXXX";
    int fd = mkstemp(path);
    ASSERT_GE(fd, 0);
    FILE *f = fdopen(fd, "w");
    fprintf(f, "# dc3-pose-fixture v1\n0.5 0\n0.6 1");
    for (int j = 0; j < kNumJoints; j++)
        fprintf(f, " %d 1 2 0.7", j);
    fprintf(f, "\n0.7 1 1 2 3\n"); // truncated: skipped
    fclose(f);
    std::vector<Dc3PosePerformer::ReplayFrame> frames;
    ASSERT_TRUE(Dc3PosePerformer::LoadReplay(path, frames));
    unlink(path);
    ASSERT_EQ(frames.size(), 2u);
    for (int j = 0; j < kNumJoints; j++) {
        EXPECT_EQ(frames[0].conf[j], 0.0f);
        EXPECT_FLOAT_EQ(frames[1].joints[j].x, (float)j);
        EXPECT_FLOAT_EQ(frames[1].conf[j], 0.7f);
    }
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
    // ...for the scorer only: the performer keeps modelling the Kinect's 100 ms
    // (otherwise it follows the knob and the knob can never be measured --
    // it was, once: late300 with the knob at 400 scored as late300 without).
    EXPECT_FLOAT_EQ(Dc3ScoringDtaLatencySeconds(0.18f), 0.1f);
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

// ===========================================================================
// PLAN P-13: DC3_POSE_PERFORM on its own drives a headless engine.
// ===========================================================================
//
// GestureMgr_NativeInit skips every pose provider under MILO_HEADLESS unless
// DC3_POSE is set.  The in-process performer spawns nothing, but it lives
// behind that provider, so before the fix a headless DC3_POSE_PERFORM run with
// no DC3_POSE silently scored the standing dummy (np-dogfood3's story run).
// Now the performer counts as asking for the provider; a value that is not a
// performer mode keeps the dummy and says so.

static std::string PerformerHeadlessBoot(const char *performValue) {
    char buf[4096];
    ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = '\0';
    std::string dir(buf);
    dir = dir.substr(0, dir.rfind('/'));
    std::string binary = dir + "/dc3-native";
    if (access(binary.c_str(), X_OK) != 0) return "";
    char tmpl[] = "/tmp/dc3-p13-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) return "";
    std::string R = root;
    std::string cmd = std::string(kShellTimeoutLookup) +
        "env -u DC3_POSE -u DC3_POSE_NO_SPAWN MILO_HEADLESS=1 MILO_NORENDER=1 "
        "MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1 MILO_MAX_FRAMES=30 "
        "DC3_CACHE_DIR=" + R + "/cache DC3_SAVE_DIR=" + R + "/saves "
        "DC3_POSE_PERFORM=" + performValue + " ${t:+$t 120} " + binary + " 2>&1";
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return "";
    std::string out;
    char line[4096];
    while (fgets(line, sizeof line, p)) out += line;
    pclose(p);
    return out;
}

TEST(PosePerformerHeadlessTest, PerformerAloneStartsTheProvider) {
    if (!getenv("DC3_DTA_FLOW_TESTS"))
        GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    std::string out = PerformerHeadlessBoot("stand");
    ASSERT_NE(out.find("DC3_EXIT:"), std::string::npos)
        << "dc3-native did not run (missing next to milo-tests?):\n" << out;
    EXPECT_NE(out.find("DC3_POSE_PERFORM: in-process synthetic performer"), std::string::npos)
        << "headless DC3_POSE_PERFORM=stand without DC3_POSE never started the performer";
    EXPECT_EQ(out.find("using dummy skeleton"), std::string::npos)
        << "headless DC3_POSE_PERFORM=stand fell back to the dummy skeleton";
}

TEST(PosePerformerHeadlessTest, AnUnknownModeSaysWhy) {
    if (!getenv("DC3_DTA_FLOW_TESTS"))
        GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    std::string out = PerformerHeadlessBoot("dancing");
    ASSERT_NE(out.find("DC3_EXIT:"), std::string::npos) << out;
    EXPECT_NE(out.find("using dummy skeleton"), std::string::npos);
    EXPECT_NE(out.find("DC3_POSE_PERFORM='dancing' is not a performer mode"), std::string::npos)
        << "an unrecognised DC3_POSE_PERFORM kept the dummy without saying why";
}
