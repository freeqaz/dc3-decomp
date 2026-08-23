// DTA flow integration tests
//
// Verifies the full DTA-driven panel flow works end-to-end using the ymca.txt
// input script: boot → attract → title → main → choose_mode → song_select
// → multiuser → loading → preloading → real_loading → game_screen.
//
// Gated by DC3_DTA_FLOW_TESTS=1 (requires game assets).
// Pattern: subprocess-based, single engine run shared via SetUpTestSuite.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

// ---------------------------------------------------------------------------
// Helpers (duplicated from test_headless_boot.cpp to keep standalone)
// ---------------------------------------------------------------------------

static std::string GetBinaryDir() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    buf[len] = '\0';
    std::string path(buf);
    size_t slash = path.rfind('/');
    return (slash != std::string::npos) ? path.substr(0, slash) : ".";
}

static std::string GetDc3NativePath() {
    return GetBinaryDir() + "/dc3-native";
}

static std::string GetScriptDir() {
    // Scripts live at repo_root/scripts/dc3-input-flows/
    // Binary is at repo_root/native/build/dc3-native
    return GetBinaryDir() + "/../../scripts/dc3-input-flows";
}

struct DtaRunResult {
    int exitCode;
    int signal;
    std::string output;
    bool timedOut;
    // Non-empty when the run never happened because a prerequisite was absent.
    // Distinguishing "the flow did not reach game_screen" from "there was no
    // engine to run" matters: measured 2026-08-23, a lane that built only the
    // milo-tests target got all seven of these tests red with messages like
    // "multiuser_screen never transitioned to loading_screen -- enter_gameplay
    // DTA function didn't fire", and the actual cause was
    //     timeout: failed to run command '.../native/build/dc3-native':
    //     No such file or directory
    // Seven gameplay-shaped failure messages for one missing file. They were
    // reported up the chain as a pre-existing gameplay regression.
    std::string setupError;
};

static bool FileExists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

static DtaRunResult RunDtaFlow(int maxFrames, int timeout = 120) {
    std::string binary = GetDc3NativePath();
    std::string script = GetScriptDir() + "/ymca.txt";

    // Check the prerequisites BEFORE running, so a missing one is reported as
    // itself instead of as seven content assertions about gameplay.
    DtaRunResult pre = {-1, 0, "", false, ""};
    if (!FileExists(binary)) {
        pre.setupError =
            "dc3-native does not exist at:\n    " + binary +
            "\nThese tests drive the real engine as a subprocess, so without it "
            "every assertion below is about output that was never produced. "
            "Build it:\n    cmake --build <build-dir> --target dc3-native\n"
            "(scripts/native_test.sh builds both milo-tests and dc3-native.)";
        return pre;
    }
    if (!FileExists(script)) {
        pre.setupError = "input flow script missing:\n    " + script;
        return pre;
    }

    std::ostringstream cmd;
    cmd << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
        << " MILO_INPUT_SCRIPT=" << script
        << " MILO_MAX_FRAMES=" << maxFrames
        << " timeout " << timeout << " " << binary << " 2>&1";

    FILE *pipe = popen(cmd.str().c_str(), "r");
    DtaRunResult result = {-1, 0, "", false, ""};
    if (!pipe) {
        result.setupError = "popen() failed for:\n    " + cmd.str();
        return result;
    }

    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        result.output += buf;

    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.signal = 0;
        if (result.exitCode == 124) result.timedOut = true;
        if (result.exitCode > 128 && result.exitCode <= 128 + 31)
            result.signal = result.exitCode - 128;
    } else if (WIFSIGNALED(status)) {
        result.exitCode = -1;
        result.signal = WTERMSIG(status);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Fixture: single engine run shared across all DTA flow tests
// ---------------------------------------------------------------------------

class DtaFlowTest : public ::testing::Test {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;

    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            return;
        sResult = RunDtaFlow(5000, 120);
        sRanEngine = true;
    }

    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS")) {
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        }
        if (!sRanEngine) {
            GTEST_SKIP() << "Engine did not run (SetUpTestSuite failed)";
        }
        // A broken setup is a FAILURE, not a fake content assertion, and not a
        // skip either -- the gate said this suite should run. Abort the body so
        // the only message the operator sees is the real one.
        if (!sResult.setupError.empty()) {
            GTEST_FAIL() << "DtaFlowTest could not run the engine.\n"
                         << sResult.setupError
                         << "\nThe assertions in this suite are about engine "
                            "output; none of them is meaningful here, and all "
                            "seven would otherwise fail with gameplay-shaped "
                            "messages that name the wrong cause.";
        }
    }

    void TearDown() override {
        if (HasFailure() && sRanEngine) {
            std::string path = "/tmp/claude-1000/dta_flow_";
            auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            if (info) path += info->name();
            path += ".log";
            std::ofstream f(path);
            if (f.is_open()) {
                f << sResult.output;
                fprintf(stderr, "Full output dumped to: %s\n", path.c_str());
            }
        }
    }

    bool outputContains(const char *needle) const {
        return sResult.output.find(needle) != std::string::npos;
    }
};

DtaRunResult DtaFlowTest::sResult = {};
bool DtaFlowTest::sRanEngine = false;

// ===========================================================================
// DTA flow milestone tests
// ===========================================================================

TEST_F(DtaFlowTest, EnterGameplayFired) {
    // The DTA flow navigates through multiuser_screen which fires
    // enter_gameplay. This transitions to loading_screen, proving
    // the DTA function executed.
    EXPECT_TRUE(outputContains("Screen 'multiuser_screen' Exit (to 'loading_screen')"))
        << "multiuser_screen never transitioned to loading_screen — "
        << "enter_gameplay DTA function didn't fire from the menu flow";
}

TEST_F(DtaFlowTest, LoadingChainTransitions) {
    // enter_gameplay triggers loading → preloading → real_loading chain
    EXPECT_TRUE(outputContains("Screen 'loading_screen' Enter"))
        << "loading_screen was never entered";
    EXPECT_TRUE(outputContains("Screen 'preloading_screen' Enter"))
        << "preloading_screen was never entered";
    EXPECT_TRUE(outputContains("Screen 'real_loading_screen' Enter"))
        << "real_loading_screen was never entered";
}

TEST_F(DtaFlowTest, ScreenChainReachesGameScreen) {
    // After loading completes, the chain transitions to game_screen
    EXPECT_TRUE(outputContains("Screen 'game_screen' Enter"))
        << "game_screen was never entered — loading chain didn't complete "
        << "or transition to game_screen failed";
}

TEST_F(DtaFlowTest, GamePanelGatesPass) {
    // GamePanel::PollForLoading reaches state 4 (DONE)
    EXPECT_TRUE(outputContains("DONE (state 4)!"))
        << "GamePanel never reached state 4 (DONE) — loading gates "
        << "didn't pass (song/venue/hud async loads may have stalled)";
}

TEST_F(DtaFlowTest, HamDirectorActivates) {
    // HamDirector becomes active during gameplay (sets dircut categories)
    EXPECT_TRUE(outputContains("HamDirector::SetDircut"))
        << "HamDirector never set a dircut — world_panel may not have "
        << "entered or HamDirector didn't activate";
}

TEST_F(DtaFlowTest, GameplayReachesPlayingState) {
    // StartGame() sets game_stage to 'playing' after all loading completes
    EXPECT_TRUE(outputContains("game_stage set to 'playing'"))
        << "game_stage never reached 'playing' — StartGame() didn't fire "
        << "or loading stalled before gameplay could begin";
}

TEST_F(DtaFlowTest, NoCrashCleanExit) {
    EXPECT_EQ(sResult.signal, 0)
        << "Engine crashed with signal " << sResult.signal;
    EXPECT_FALSE(sResult.timedOut)
        << "Engine timed out (hung during DTA flow)";
    EXPECT_EQ(sResult.exitCode, 0)
        << "Engine exited with code " << sResult.exitCode;
}
