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
//   MenuSelectFlowTest      hold to select (np-menu-gestures,
//                           DC3_MENU_SELECT=swipe|hold|both), end to end on the
//                           real lists: scripts/menu_gesture_bench.py check.
//                           Under the default (swipe) a hand held on a row
//                           selects nothing and a swipe selects; switched at
//                           run time ({native_settings set_menu_select hold})
//                           the held hand selects the row after ~1.5 s with
//                           the list's slide as progress, a hand left there
//                           selects nothing more, a swipe selects nothing,
//                           controller mode holds nothing; `both` takes
//                           either; the left hand's Back holds too (after its
//                           own, longer time); back under
//                           swipe it is the image again.  Its first check
//                           is DC3_POSE_FILL_MENU's default: outside a song
//                           the lists get the camera's own frames.  17
//                           checks, each printed.  The second test is the
//                           standing negative control: the same driver
//                           started under DC3_MENU_SELECT=hold must FAIL (its
//                           swipe checks do), or the first proves nothing.
//                           Sabotaged twice more by hand (the driver run
//                           directly, each exit 1): HamNavList::UpdateGestures
//                           not calling Dc3MenuGesture::UpdateHold fails the
//                           five hold checks ("hold: the hand kept on a row
//                           selects it", "the list slid", "a swipe selects
//                           nothing", "both: a hold selects", "the left
//                           hand's Back"); DC3_POSE_FILL_MENU=same fails
//                           "outside a song the pose fill is off [extrap]".
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
#include "test_tmp.h"
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
    r.workdir = MakeTestTempDir("dc3-gesture-menus"); // removed at exit on a pass
    if (r.workdir.empty()) {
        r.setupError = "MakeTestTempDir 'dc3-gesture-menus' failed under " + TestTempBase();
        return r;
    }
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
class MenuSelectFlowTest : public GestureFlowFixture {};
class MenuSelectFlowSabotageTest : public GestureFlowFixture {};

// scripts/menu_gesture_bench.py check: its exit code and its PASS/FAIL lines.
struct CheckRun {
    std::string setupError;
    int exitCode = -1;
    std::string output;
    std::string workdir;

    int Count(const char *prefix) const {
        int n = 0;
        std::istringstream in(output);
        std::string line;
        while (std::getline(in, line)) {
            if (line.compare(0, strlen(prefix), prefix) == 0)
                n++;
        }
        return n;
    }
    bool Has(const char *text) const { return output.find(text) != std::string::npos; }
};

CheckRun RunMenuSelectCheck(const char *extraEnv, int timeoutSeconds) {
    CheckRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string driver = dir + "/../../scripts/menu_gesture_bench.py";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(driver)) {
        r.setupError = "missing " + driver;
        return r;
    }
    r.workdir = MakeTestTempDir("dc3-menu-select"); // removed at exit on a pass
    if (r.workdir.empty()) {
        r.setupError = "MakeTestTempDir 'dc3-menu-select' failed under " + TestTempBase();
        return r;
    }
    std::ostringstream sh;
    sh << kShellTimeoutLookup
       << "cd " << dir << " || exit 2; "
       << "${t:+$t -k 10 " << timeoutSeconds << "} python3 " << driver
       << " check --engine " << bin << " --out " << r.workdir;
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
    return r;
}

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

TEST_F(MenuSelectFlowTest, SwipeIsTheDefaultAndHoldSelectsWhenSwitchedTo) {
    CheckRun r = RunMenuSelectCheck("", 600);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = "\nworkdir " + r.workdir + " (engine.log, bench.log)\n" + r.output;
    EXPECT_EQ(r.exitCode, 0) << where;
    EXPECT_TRUE(r.Has("MENU SELECT CHECK: ok")) << where;
    EXPECT_EQ(r.Count("FAIL  "), 0) << where;
    // All of them ran: a driver that stopped early prints fewer.
    EXPECT_EQ(r.Count("PASS  "), 17) << where;
    EXPECT_TRUE(r.Has("PASS  outside a song the pose fill is off")) << where;
    EXPECT_TRUE(r.Has("PASS  hold: the hand kept on a row selects it")) << where;
    EXPECT_TRUE(r.Has("PASS  swipe: a hand held 3.5 s on a row selects nothing")) << where;
    EXPECT_TRUE(r.Has("PASS  hold: the left hand's Back is selected by a hold, after about 2.1 s"))
        << where;
}

TEST_F(MenuSelectFlowSabotageTest, TheSameCheckFailsWhenTheDefaultIsNotSwipe) {
    CheckRun r = RunMenuSelectCheck("DC3_MENU_SELECT=hold", 600);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    EXPECT_EQ(r.exitCode, 1)
        << "the check passed with hold as the start mode -- it cannot tell the modes apart:\n"
        << r.output;
    EXPECT_TRUE(r.Has("FAIL  the default is swipe")) << r.output;
    EXPECT_TRUE(r.Has("FAIL  swipe: a hand held 3.5 s on a row selects nothing")) << r.output;
    EXPECT_TRUE(r.Has("FAIL  swipe: a swipe selects the row")) << r.output;
}
