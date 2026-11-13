// The venue crowd is drawn every frame and animates (PLAN H-6, branch np-crowd).
//
// What the image does (src/system/world/Crowd.cpp, all four bodies 100%
// name_check): WorldCrowd::Load hands the serialised force flag to
// Force3DCrowd, and every WorldCrowd in all five crowd venues serialises
// force=1 (34 objects: dci 10, houseparty 7, rollerrink 6, streetside 6,
// dclive 5).  So every crowd instance moves into m3DChars, and
// WorldCrowd::DrawShowing -> Draw3DChars draws the crowd member's Character at
// each instance's transform, every frame, with the pose its own Poll gave it.
// The impostor half of DrawShowing (render a Character into gImpostorTex[mLod]
// every frame, draw the billboard MultiMesh) has no instances left to draw.
//
// What native did before np-crowd: Load forced Force3DCrowd(false), so every
// instance stayed a billboard; Draw3DChars broke out before drawing; and
// DrawShowing rendered each Character ONCE into a private texture it then
// reused forever (and drew it with a blend that left nothing on screen).
//
// The check: one headless, no-GPU engine run of the YMCA flow (rollerrink).
// DC3_CROWD_STATS makes Crowd.cpp print, per sampled frame, how many 3D crowd
// draws and impostor renders happened, and a checksum of the local
// transforms of every transformable inside each Character drawn.  During
// gameplay every sample must draw crowd members in 3D, none as impostors, and
// the checksum must keep changing, i.e. what is drawn is the animated pose.
//
// Sabotage-checked (see the np-crowd commit): putting back the native
// Force3DCrowd(false), or the Draw3DChars break, leaves no 3D draws; skipping
// the crowd Characters' Poll freezes the checksum.  Gated by
// DC3_DTA_FLOW_TESTS=1; the engine is killed by PID; every wait is bounded.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "platform/ExecutablePath.h"

namespace {

// Samples wanted: one every kEvery render frames once the crowd draws.
const int kEvery = 25;
const int kSamples = 60;

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

std::string RunCapture(const std::string &cmd) {
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p)
        return out;
    char buf[8192];
    while (fgets(buf, sizeof buf, p))
        out += buf;
    pclose(p);
    return out;
}

struct Sample {
    unsigned frame = 0;
    int draws3d = 0;
    int impostors = 0;
    double pose = 0;
};

struct CrowdRun {
    std::string setupError;
    std::string driver;
    std::string output;
    std::vector<Sample> samples;
};

CrowdRun RunYmca() {
    CrowdRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string repo = dir + "/../..";
    std::string flow = repo + "/scripts/dc3-input-flows/ymca.txt";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(flow)) {
        r.setupError = "missing " + flow;
        return r;
    }
    char tmpl[] = "/tmp/dc3-crowd-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        r.setupError = "mkdtemp failed";
        return r;
    }
    std::string R = root;
    mkdir((R + "/cache").c_str(), 0755);
    mkdir((R + "/saves").c_str(), 0755);
    std::string log = R + "/engine.log";
    std::ostringstream sh;
    sh << "cd " << dir << " || exit 1; "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " MILO_MAX_FRAMES=0 DC3_CROWD_STATS=" << kEvery
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " MILO_INPUT_SCRIPT=" << flow
       << " exec timeout 240 ./dc3-native > " << log << " 2>&1 ) & epid=$!; "
       << "end=$((SECONDS+200)); "
       << "until [ \"$(grep -ac '^DC3_CROWD:' " << log << " 2>/dev/null)\" -ge " << kSamples
       << " ] || [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do sleep 0.5; done; "
       << "echo \"DRIVER: stop after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
       << "kill $epid 2>/dev/null; sleep 1; kill -9 $epid 2>/dev/null; "
       << "cat " << log;
    std::istringstream all(RunCapture(sh.str()));
    std::string line;
    while (std::getline(all, line)) {
        if (!line.compare(0, 7, "DRIVER:")) {
            r.driver += line + "\n";
            continue;
        }
        r.output += line + "\n";
        Sample s;
        if (sscanf(
                line.c_str(),
                "DC3_CROWD: frame=%u draws3d=%d impostors=%d pose=%lf",
                &s.frame,
                &s.draws3d,
                &s.impostors,
                &s.pose
            )
            == 4)
            r.samples.push_back(s);
    }
    return r;
}

std::string SaveLog(const CrowdRun &r) {
    std::string path = std::string("/tmp/dc3-crowd-")
        + ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".log";
    std::ofstream f(path);
    if (f.is_open())
        f << r.driver << r.output;
    return path;
}

class CrowdTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
};

} // namespace

TEST_F(CrowdTest, TheVenueCrowdIsDrawnIn3DEveryFrameAndAnimates) {
    CrowdRun r = RunYmca();
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string logPath = SaveLog(r);
    ASSERT_NE(r.output.find("Screen 'game_screen' Enter"), std::string::npos)
        << "the YMCA flow never reached game_screen; log: " << logPath << "\n"
        << r.driver;
    ASSERT_GE((int)r.samples.size(), kSamples)
        << "the venue crowd drew in only " << r.samples.size() << " sampled frames (want "
        << kSamples << "): the crowd is not being drawn in 3D. log: " << logPath << "\n"
        << r.driver;

    int frames3d = 0, impostorFrames = 0, changes = 0;
    std::set<double> poses;
    for (size_t i = 0; i < r.samples.size(); i++) {
        const Sample &s = r.samples[i];
        frames3d += s.draws3d > 0;
        impostorFrames += s.impostors > 0;
        poses.insert(s.pose);
        if (i > 0)
            changes += s.pose != r.samples[i - 1].pose;
    }
    const int n = (int)r.samples.size();
    // rollerrink has 57 crowd members; a camera shot can hide some of the
    // crowd objects, never all of them.
    EXPECT_EQ(frames3d, n) << "sampled frames without a single 3D crowd draw. log: " << logPath;
    EXPECT_GE(r.samples.front().draws3d, 20) << "log: " << logPath;
    EXPECT_EQ(impostorFrames, 0)
        << "a crowd member was rendered as an impostor; every rollerrink crowd loads force=1, "
           "so the image draws them all in 3D. log: "
        << logPath;
    // Animated: the drawn pose moves between almost every pair of samples
    // (25 frames apart).  A frozen crowd gives one value, or a value per
    // camera-shot crowd subset.
    EXPECT_GE(changes, n * 3 / 4) << "the crowd's drawn pose did not change between samples ("
                                  << poses.size() << " distinct of " << n << "). log: " << logPath;
}
