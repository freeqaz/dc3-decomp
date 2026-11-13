// A dancer the pose detector loses for a moment is not a player who left
// (branch np-tracking, PLAN P-12).
//
// np-dogfood3 played Cupid Shuffle through the live pose server on real
// footage: MediaPipe lost the dancer, who never left the frame, for 1.43 s at
// song 42.0, and the game paused at 43.25 ("stand in front of the sensor").
// The image gives a lost tracking id GestureMgr::sMaxRecoveryTime (1.0 s,
// SkeletonRecoverer) before SkeletonChooser unbinds the player and
// Game::CheckForSkeletonLoss pauses; a Kinect never loses a person who is in
// view, so on a 360 that grace is for real losses.  The native sensor now
// holds a vanished person for DC3_POSE_HOLD_MS (default 1000) of sensor time
// (GestureMgr_Native.cpp AssignSlots).
//
// Three engine runs, each real time (the recoverer counts UI seconds, which
// DC3_FAST_TIME does not compress), headless with no GPU, YMCA on Perform, the
// in-process performer replaying a standing person (DC3_POSE_PERFORM=replay:)
// with frames blanked where the detector "loses" her:
//
//   PoseDropoutTest       two 1.43 s dropouts (song 20.0 and 28.0) -> never
//                         paused, the song runs past both.
//   PoseDropoutHoldOffTest  the same with DC3_POSE_HOLD_MS=0 -> paused about
//                         1 s into the first dropout.  The control: it shows
//                         the dropouts land in pause-eligible gameplay, so the
//                         first test is not passing for want of a pause check.
//   PoseExitTest          the person is gone from song 20.0 on -> paused
//                         (hold 1 s + the image's 1 s), as on a 360.
//
// One test per fixture (ctest runs each in its own process).  Gated by
// DC3_DTA_FLOW_TESTS=1.  The engine is killed by PID; every wait is bounded.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "platform/ExecutablePath.h"
#include "shell_timeout.h"

namespace {

std::string BinaryDir() {
    char buf[4096];
    ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
    if (len <= 0)
        return ".";
    buf[len] = '\0';
    std::string p(buf);
    size_t slash = p.rfind('/');
    return slash != std::string::npos ? p.substr(0, slash) : ".";
}

bool Exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

// Every tutorial seen, so none sits between multiuser_screen and loading
// (test_native_twoplayer.cpp's SeedAllTutorialsSeen; layout measured in
// test_dta_flow.cpp).
bool SeedAllTutorialsSeen(const std::string &root) {
    static const unsigned char kGlobalOptions[0x39] = {
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xc1, 0x00, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    unsigned char bytes[sizeof(kGlobalOptions)];
    memcpy(bytes, kGlobalOptions, sizeof(bytes));
    const unsigned seen = 0x7fffffffu;
    for (int i = 0; i < 4; i++)
        bytes[0x28 + i] = (unsigned char)(seen >> (8 * i));
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    mkdir((root + "/cache/global").c_str(), 0755);
    std::ofstream f(root + "/cache/global/global", std::ios::binary);
    if (!f.is_open())
        return false;
    f.write((const char *)bytes, sizeof(bytes));
    return f.good();
}

// title -> main -> Perform -> YMCA (one down from the first song, as
// battle-duo.txt) -> multiuser (difficulty, play, skip waiting).
const char *kRoute =
    "wait_screen title_screen\n+43 wake\n+45 confirm\n"
    "wait_screen main_screen\n+43 wake\n+45 confirm\n"
    "wait_screen choose_mode_screen\n+43 wake\n+45 confirm\n"
    "wait_screen song_select_screen\n+43 wake\n+45 down\n+105 confirm\n"
    "wait_screen multiuser_screen\n+38 wake\n+40 confirm\n+110 confirm\n+180 confirm\n";

struct Gap {
    double from, to; // song seconds; to < 0 = to the end
};

// A standing person (the first frame of the P-5 fixture) at 30 Hz over the
// whole song, blanked inside the gaps: what the socket sends when the
// detector sees nobody.
bool WriteFixture(const std::string &src, const std::string &dst, const std::vector<Gap> &gaps) {
    std::ifstream in(src);
    std::string line, pose;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream ss(line);
        double t;
        int tracked;
        ss >> t >> tracked;
        if (tracked) {
            std::getline(ss, pose);
            break;
        }
    }
    if (pose.empty())
        return false;
    std::ofstream out(dst);
    out << "# dc3-pose-fixture v1\n# test_native_pose_dropout.cpp: standing, with gaps\n";
    for (int i = -30 * 30; i <= 240 * 30; i++) {
        double t = i / 30.0;
        bool gone = false;
        for (const Gap &g : gaps)
            gone |= t >= g.from && (g.to < 0 || t < g.to);
        char head[64];
        snprintf(head, sizeof head, "%.4f %d", t, gone ? 0 : 1);
        out << head << (gone ? "" : pose) << "\n";
    }
    return out.good();
}

struct DropoutRun {
    std::string setupError;
    std::string output; // engine log
    std::string driver;
    bool Entered(const char *screen) const {
        return output.find(std::string("Screen '") + screen + "' Enter") != std::string::npos;
    }
    // Telemetry: the song second of the last gameplay frame before the pause
    // screen (or the end of the log), and whether one was unpaused.
    double LastPlayingSecs() const {
        size_t end = output.find("Screen 'perform_pause_screen' Enter");
        std::istringstream ss(output.substr(0, end));
        std::string line;
        double best = -1.0;
        while (std::getline(ss, line)) {
            if (line.compare(0, 8, "DC3_TEL:") || line.find("screen=game_screen ") == std::string::npos
                || line.find(" gamePaused=0 ") == std::string::npos)
                continue;
            size_t at = line.find(" realSecs=");
            if (at != std::string::npos)
                best = atof(line.c_str() + at + 10);
        }
        return best;
    }
};

// Plays until perform_pause_screen, or gameplay telemetry past `stopSecs`, or
// the bounded wait runs out.
DropoutRun RunDropout(const std::vector<Gap> &gaps, const char *holdMs, double stopSecs) {
    DropoutRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string src = dir + "/../tests/data/pose/taye01_stand_dance.dc3pose";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    char tmpl[] = "/tmp/dc3-dropout-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        r.setupError = "mkdtemp failed";
        return r;
    }
    std::string R = root;
    if (!SeedAllTutorialsSeen(R)) {
        r.setupError = "could not seed the global options cache under " + R;
        return r;
    }
    std::string fixture = R + "/standing.dc3pose";
    if (!WriteFixture(src, fixture, gaps)) {
        r.setupError = "could not build the replay fixture from " + src;
        return r;
    }
    std::string route = R + "/route.txt";
    {
        std::ofstream f(route);
        f << kRoute;
    }
    std::string log = R + "/engine.log";
    char stop[32];
    snprintf(stop, sizeof stop, "%.1f", stopSecs);

    std::ostringstream sh;
    sh << kShellTimeoutLookup << "cd " << dir << " || exit 1; "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " MILO_MAX_FRAMES=0 DC3_TEL=1 DC3_TEL_INTERVAL=15 DC3_CONTROLLER_MODE=forced"
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " MILO_INPUT_SCRIPT=" << route << " DC3_POSE=external"
       << " DC3_POSE_PERFORM=replay:" << fixture << " DC3_POSE_PERFORM_REPLAY_SYNC=song";
    if (holdMs)
        sh << " DC3_POSE_HOLD_MS=" << holdMs;
    sh << " exec ${t:+$t 280} ./dc3-native > " << log << " 2>&1 ) & epid=$!; "
       << "end=$((SECONDS+240)); "
       << "until grep -aq \"Screen 'perform_pause_screen' Enter\" " << log << " 2>/dev/null"
       << " || awk -v s=" << stop
       << " '/^DC3_TEL:.*screen=game_screen / { for (i = 1; i <= NF; i++) if ($i ~ /^realSecs=/)"
          " { split($i, a, \"=\"); if (a[2] + 0 >= s) found = 1 } } END { exit !found }' "
       << log << " 2>/dev/null"
       << " || [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do sleep 0.5; done; "
       << "echo \"DRIVER: stop after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
       << "kill $epid 2>/dev/null; sleep 1; kill -9 $epid 2>/dev/null; "
       << "cat " << log;
    FILE *p = popen(sh.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    char buf[8192];
    while (fgets(buf, sizeof buf, p)) {
        if (!strncmp(buf, "DRIVER:", 7))
            r.driver += buf;
        else
            r.output += buf;
    }
    pclose(p);
    return r;
}

class DropoutFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
    static std::string Dump(const DropoutRun &r) {
        std::string path = "/tmp/claude-1000/pose_dropout_" +
            std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "_" +
            std::to_string((int)getpid()) + ".log";
        std::ofstream f(path);
        if (f.is_open())
            f << r.driver << r.output;
        return path;
    }
};

// The np-dogfood3 dropout, twice.
const std::vector<Gap> kDropouts = { { 20.0, 21.43 }, { 28.0, 29.43 } };

class PoseDropoutTest : public DropoutFixture {};
class PoseDropoutHoldOffTest : public DropoutFixture {};
class PoseExitTest : public DropoutFixture {};

} // namespace

TEST_F(PoseDropoutTest, ADancerLostForUnderTwoSecondsKeepsPlaying) {
    DropoutRun r = RunDropout(kDropouts, nullptr, 33.0);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;
    EXPECT_FALSE(r.Entered("perform_pause_screen"))
        << "paused at song " << r.LastPlayingSecs()
        << " s for a 1.43 s detector dropout (the hold did not cover it); " << where;
    EXPECT_GE(r.LastPlayingSecs(), 32.0) << "the song did not run past both dropouts; "
                                         << r.driver << where;
}

TEST_F(PoseDropoutHoldOffTest, WithoutTheHoldTheImagesOneSecondPauses) {
    DropoutRun r = RunDropout(kDropouts, "0", 33.0);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;
    ASSERT_TRUE(r.Entered("perform_pause_screen"))
        << "no pause with the hold off: the dropouts did not reach a pause-eligible "
           "moment, so PoseDropoutTest proves nothing; " << where;
    double at = r.LastPlayingSecs();
    EXPECT_GT(at, 20.9) << where;
    EXPECT_LT(at, 22.5) << "paused, but not ~1 s into the first dropout; " << where;
}

TEST_F(PoseExitTest, APlayerWhoLeavesIsStillPaused) {
    DropoutRun r = RunDropout({ { 20.0, -1.0 } }, nullptr, 33.0);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;
    ASSERT_TRUE(r.Entered("perform_pause_screen"))
        << "the player left at song 20 s and the game never paused; " << where;
    double at = r.LastPlayingSecs();
    EXPECT_GT(at, 21.5) << "paused inside the hold; " << where;
    EXPECT_LT(at, 24.0) << "the hold delayed the pause too long; " << where;
}
