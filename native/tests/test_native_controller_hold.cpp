// The press hold (branch np-gesture-cooldown, PLAN H-157).
//
// With a camera running the controller-mode policy is faithful: a keyboard or
// pad press takes the menus, and the image hands them back to the hand cursor
// 5 s after the last press.  On a webcam at a desk that hand is the arm of the
// person at the keyboard, cut by the edge of the picture and in and out of it
// from frame to frame (playtest 2026-11-21).  Natively a press now keeps the
// menus for the hold (DC3_CONTROLLER_HOLD_MS, default 5 minutes) and raising
// both hands -- the image's HandsUpGestureFilter -- gives them to the hands at
// once (System_Native.cpp "The press hold", ShellInput::Poll).
//
// scripts/controller_hold_check.py launches dc3-native with
// scripts/synthetic_kinect.py as the sensor, faithful, a 20 s hold, paced at
// 30 frames a second, and prints one PASS/FAIL line per check:
//
//   ControllerHoldTest          all twelve pass: the menus are still the
//                               keyboard's 9 s after the last press, both
//                               hands raised take them back within 4 s without
//                               also opening the correct-identity overlay, and
//                               without a press the hold ends them, the exit
//                               naming the hold as its timeout.
//   ControllerHoldSabotageTest  the standing negative control: the same
//                               driver with DC3_CONTROLLER_HOLD_MS=0 (the
//                               hold off is the image) must FAIL the hold
//                               checks, or the first test proves nothing.
//
// One TEST per fixture (ctest runs each in its own process).  Gated by
// DC3_DTA_FLOW_TESTS=1.  The driver kills its engine by process group; every
// wait in it is bounded, and the whole run is bounded by `timeout` here.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// scripts/controller_hold_check.py: its exit code and its PASS/FAIL lines.
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

CheckRun RunHoldCheck(const char *extraEnv, int timeoutSeconds) {
    CheckRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string driver = dir + "/../../scripts/controller_hold_check.py";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(driver)) {
        r.setupError = "missing " + driver;
        return r;
    }
    r.workdir = MakeTestTempDir("dc3-controller-hold"); // removed at exit on a pass
    if (r.workdir.empty()) {
        r.setupError = "MakeTestTempDir 'dc3-controller-hold' failed under " + TestTempBase();
        return r;
    }
    std::ostringstream sh;
    sh << kShellTimeoutLookup
       << "cd " << dir << " || exit 2; "
       << "${t:+$t -k 10 " << timeoutSeconds << "} python3 " << driver
       << " --engine " << bin << " --workdir " << r.workdir;
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

class ControllerHoldFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
};

class ControllerHoldTest : public ControllerHoldFixture {};
class ControllerHoldSabotageTest : public ControllerHoldFixture {};

} // namespace

TEST_F(ControllerHoldTest, APressKeepsTheMenusAndBothHandsRaisedGiveThemBack) {
    CheckRun r = RunHoldCheck("", 300);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = "\nworkdir " + r.workdir + " (engine.log)\n" + r.output;
    EXPECT_EQ(r.exitCode, 0) << where;
    EXPECT_EQ(r.Count("FAIL: "), 0) << where;
    // All of them ran: a driver that stopped early prints fewer.
    EXPECT_EQ(r.Count("PASS: "), 12) << where;
    EXPECT_TRUE(r.Has("PASS: still in controller mode 9 s after the last press")) << where;
    EXPECT_TRUE(r.Has("PASS: both hands raised: out of controller mode within 4 s")) << where;
    EXPECT_TRUE(r.Has("PASS: it was the raised hands that took the menus")) << where;
    EXPECT_TRUE(r.Has("PASS: the same raise does not open the correct-identity overlay")) << where;
    EXPECT_TRUE(r.Has("PASS: the idle exit names the hold as its timeout")) << where;
}

TEST_F(ControllerHoldSabotageTest, TheSameCheckFailsWithTheHoldOff) {
    CheckRun r = RunHoldCheck("DC3_CONTROLLER_HOLD_MS=0", 300);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    EXPECT_EQ(r.exitCode, 1)
        << "the check passed with the hold off -- it cannot tell the hold from the image:\n"
        << r.output;
    EXPECT_TRUE(r.Has("FAIL: still in controller mode 9 s after the last press")) << r.output;
    EXPECT_TRUE(r.Has("FAIL: it was the raised hands that took the menus")) << r.output;
    // With the hold off the raise is the image's: it opens the overlay.
    EXPECT_TRUE(r.Has("FAIL: the same raise does not open the correct-identity overlay"))
        << r.output;
    EXPECT_TRUE(r.Has("FAIL: the idle exit names the hold as its timeout")) << r.output;
}
