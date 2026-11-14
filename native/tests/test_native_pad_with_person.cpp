// PLAN H-20: a pad player's presses land with someone moving on the sensor.
//
// Found by np-playtest-kit: with a replayed dancer in view and controller mode
// `forced`, three Down presses on choose_mode_screen moved the highlight
// 0->1->1->1. Traced (np-window): the presses never reached the list. The
// dancer's arms went up (macarena), ShellInput::Poll's hands-up arm -- the
// image's, unchanged -- opened correct_identity_panel, the Kinect "who is
// this?" overlay, and that overlay's BUTTON_DOWN_MSG answers every press but B
// itself (ui/correct_identity/correct_identity.dta). On the 360 the overlay's
// enter also leaves controller mode, so the hands drive it; `forced` pins
// controller mode, so nothing could drive it and it only ate the pad. Under
// `forced` the overlay no longer opens (ShellInput.cpp, HX_NATIVE).
//
// The person: native/tests/data/pose/hands_up_held.dc3pose, one standing
// frame with both arms raised and held (the in-process replay performer).
// The route wakes title and main, then on choose_mode_screen presses Down,
// Down, A: the A must select item 2. Before the fix the overlay opened on the
// title screen and on main_screen and the route never got past main.
//
// One engine run, headless, no audio. Gated by DC3_DTA_FLOW_TESTS=1. Bounded
// by MILO_MAX_FRAMES and `timeout`.

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

std::string Tail(const std::string &s, size_t n = 6000) {
    return s.size() > n ? "...\n" + s.substr(s.size() - n) : s;
}

const char *const kRoute = "wait_screen title_screen\n+43 wake\n+45 confirm\n"
                           "wait_screen main_screen\n+43 wake\n+45 confirm\n"
                           "wait_screen choose_mode_screen\n+200 down\n+260 down\n+320 confirm\n";

class PadWithAPersonTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
};

} // namespace

TEST_F(PadWithAPersonTest, RaisedHandsDoNotTakeThePadUnderForcedControllerMode) {
    const std::string dir = BinaryDir();
    const std::string bin = dir + "/dc3-native";
    const std::string fixture = dir + "/../tests/data/pose/hands_up_held.dc3pose";
    ASSERT_TRUE(Exists(bin)) << bin << " (scripts/native_test.sh builds it)";
    ASSERT_TRUE(Exists(fixture)) << fixture;
    char tmpl[] = "/tmp/dc3-pad-person-XXXXXX";
    char *root = mkdtemp(tmpl);
    ASSERT_NE(root, nullptr);
    const std::string R = root;
    const std::string route = R + "/route.txt";
    std::ofstream(route) << kRoute;
    const std::string log = R + "/engine.log";

    std::ostringstream sh;
    sh << kShellTimeoutLookup << "cd " << dir << " || exit 2; "
       << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1 DC3_AUDIO_GAIN=0"
       << " DC3_HEARTBEAT_S=0 MILO_MAX_FRAMES=3000 DC3_CONTROLLER_MODE=forced"
       << " DC3_POSE_PERFORM=replay:" << fixture << " MILO_INPUT_SCRIPT=" << route
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " ${t:+$t -k 10 240} ./dc3-native > " << log << " 2>&1";
    int status = system(sh.str().c_str());
    const std::string out = ReadFile(log);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) != 124)
        << "the run hit its 240 s bound\n" << Tail(out);

    ASSERT_NE(out.find("controller mode policy = forced"), std::string::npos) << Tail(out);
    ASSERT_NE(out.find("DC3 PERFORM: replay"), std::string::npos)
        << "the hands-up performer did not run\n" << Tail(out);
    const size_t chooseMode = out.find("Screen 'choose_mode_screen' Enter");
    ASSERT_NE(chooseMode, std::string::npos)
        << "never reached choose_mode_screen: a raised hand took the pad on title/main\n"
        << Tail(out);
    // Down, Down, A: the A selects item 2 of the mode list.
    const size_t select = out.find("DC3 HamNavList: select name='right_hand.hnl' ", chooseMode);
    ASSERT_NE(select, std::string::npos)
        << "the A on choose_mode_screen selected nothing\n" << Tail(out);
    const std::string line = out.substr(select, out.find('\n', select) - select);
    EXPECT_NE(line.find(" selected=2 "), std::string::npos)
        << "the Down presses did not all land: " << line << "\n" << Tail(out);
    EXPECT_EQ(out.find("(ui/correct_identity/correct_identity.milo)"), std::string::npos)
        << "the correct-identity overlay opened under forced controller mode\n" << Tail(out);
}
