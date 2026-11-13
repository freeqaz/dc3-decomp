// Menus by gesture alone (branch np-gesture-menus, PLAN P-3).
//
// A webcam-only player has no controller: DC3's shell has to be driven the way
// a 360 with a Kinect and no pad is driven -- the right arm out to the side,
// hand height picks the item (HandHeightGestureFilter), a forearm swipe across
// the body selects (DirectionGestureFilterSingleUser / ArcDetector), the left
// arm does the same on the helpbar's back button.  The image runs all of that
// whenever !GestureMgr::InControllerMode(); the native controller-mode policy
// `auto` (System_Native.cpp) is faithful exactly when a pose provider runs.
//
// scripts/gesture_menu_flow.py launches dc3-native with scripts/synthetic_kinect.py
// as the sensor (no MILO_INPUT_SCRIPT, no DC3_FAST_BOOT, no /api/input) and
// walks title -> main -> perform -> YMCA -> easy -> start -> the song ->
// results -> complete (new song) -> song select -> back -> back -> main,
// reading only what a player sees (screen, highlighted item).  It counts every
// HamNavList select by its source: `selecting=0` is a gesture
// (UpdateGestures), `selecting=1` a controller confirm (OnMsg(ButtonDownMsg)).
//
//   GestureMenusTest        default policy: the route completes with ZERO
//                           button selects and controller mode never entered.
//   GestureMenusForcedTest  the sabotage, kept as a standing negative control:
//                           DC3_CONTROLLER_MODE=forced pins controller mode,
//                           HamNavList ignores the hands, and the same performer
//                           must not get past the title screen.  If this ever
//                           passes the route, the first test proves nothing.
//
// One TEST per fixture (ctest runs each in its own process).  Gated by
// DC3_DTA_FLOW_TESTS=1.  The driver kills its engine by process group; every
// wait in it is bounded, and the whole run is bounded by `timeout` here.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
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

std::string ReadFile(const std::string &p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

struct FlowRun {
    std::string setupError;
    int exitCode = -1;
    std::string report; // the driver's JSON report
    std::string output; // the driver's stdout (one line per step)
    std::string workdir;

    // `"key": <int>` from the report; -1 when absent.
    long Int(const char *key) const {
        std::string k = std::string("\"") + key + "\": ";
        size_t at = report.find(k);
        if (at == std::string::npos)
            return -1;
        return strtol(report.c_str() + at + k.size(), nullptr, 10);
    }
    bool Ok() const { return report.find("\"ok\": true") != std::string::npos; }
    bool Entered(const char *screen) const {
        return report.find(std::string("\"") + screen + "\"") != std::string::npos;
    }
};

FlowRun RunFlow(const char *extraEnv, int timeoutSeconds) {
    FlowRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string repo = dir + "/../..";
    std::string driver = repo + "/scripts/gesture_menu_flow.py";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin +
            " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(driver)) {
        r.setupError = "missing " + driver;
        return r;
    }
    if (system("python3 -c 'import json, socket, struct, http.client' >/dev/null 2>&1") != 0) {
        r.setupError = "python3 is not runnable; the synthetic sensor needs it";
        return r;
    }
    char tmpl[] = "/tmp/dc3-gesture-menus-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        r.setupError = "mkdtemp '/tmp/dc3-gesture-menus-XXXXXX' failed";
        return r;
    }
    r.workdir = root;
    std::string reportPath = r.workdir + "/report.json";
    std::ostringstream sh;
    sh << kShellTimeoutLookup
       << "cd " << dir << " || exit 2; "
       << "${t:+$t -k 10 " << timeoutSeconds << "} python3 " << driver
       << " --engine " << bin << " --workdir " << r.workdir << " --report " << reportPath
       << " --all-tutorials-seen";
    if (extraEnv && *extraEnv)
        sh << " --env " << extraEnv;
    sh << " 2>&1";
    FILE *p = popen(sh.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    char buf[8192];
    while (fgets(buf, sizeof buf, p))
        r.output += buf;
    int status = pclose(p);
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    r.report = ReadFile(reportPath);
    return r;
}

class GestureFlowFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
};

class GestureMenusTest : public GestureFlowFixture {};
class GestureMenusForcedTest : public GestureFlowFixture {};

} // namespace

TEST_F(GestureMenusTest, TitleToSongToResultsToMainWithZeroButtons) {
    FlowRun r = RunFlow("", 900);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    ASSERT_FALSE(r.report.empty())
        << "the driver wrote no report (exit " << r.exitCode << "):\n" << r.output;
    std::string where = "\nworkdir " + r.workdir + " (engine.log, flow.log, report.json)\n" +
        r.output;
    EXPECT_NE(r.report.find("policy = auto"), std::string::npos)
        << "the default controller-mode policy is not auto:\n" << r.report;
    EXPECT_NE(r.report.find("auto -> faithful"), std::string::npos)
        << "auto never went faithful with a pose provider running:\n" << r.report;
    ASSERT_TRUE(r.Ok()) << "route failed (exit " << r.exitCode << "):\n" << r.report << where;
    EXPECT_EQ(r.exitCode, 0) << where;
    EXPECT_TRUE(r.Entered("game_screen")) << r.report;
    EXPECT_TRUE(r.Entered("perform_final_results_screen")) << r.report;
    // Every select on the route is a gesture: title, gameplay, perform, ymca,
    // easy, play, continue, new_song = 8 on the right-hand lists, plus two
    // helpbar backs (left_hand.hnl selects are not HamNavList right-list
    // selects but still log through the same line).
    EXPECT_EQ(r.Int("selects_by_button"), 0) << "a select came from a button:\n" << r.report;
    EXPECT_GE(r.Int("selects_by_gesture"), 8) << r.report;
    EXPECT_EQ(r.Int("controller_mode_enters"), 0)
        << "something entered controller mode on a gesture-only run:\n" << r.report;
}

TEST_F(GestureMenusForcedTest, ForcedPolicyIgnoresTheSamePerformer) {
    FlowRun r = RunFlow("DC3_CONTROLLER_MODE=forced", 300);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    ASSERT_FALSE(r.report.empty())
        << "the driver wrote no report (exit " << r.exitCode << "):\n" << r.output;
    EXPECT_NE(r.report.find("policy = forced"), std::string::npos) << r.report;
    EXPECT_FALSE(r.Ok())
        << "under forced controller mode the gesture route completed -- the "
           "zero-button test cannot tell gestures from anything else:\n" << r.report;
    EXPECT_EQ(r.exitCode, 1) << r.output;
    EXPECT_EQ(r.Int("selects_by_gesture"), 0) << r.report;
    EXPECT_FALSE(r.Entered("main_screen") && r.Entered("choose_mode_screen"))
        << "got past the title screen with controller mode pinned:\n" << r.report;
}
