// DTA flow integration tests
//
// Verifies the DTA-driven panel flow works end-to-end: enter_gameplay fires,
// loading_screen transitions, game_screen enters, GamePanel gates pass,
// HamDirector enters, and OnFileLoaded callbacks fire for song/venue.
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
};

static DtaRunResult RunDtaFlow(int maxFrames, int timeout = 120) {
    std::string binary = GetDc3NativePath();
    std::string script = GetScriptDir() + "/boot-to-main.txt";
    std::ostringstream cmd;
    cmd << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0"
        << " DC3_SCREEN=game_screen"
        << " DC3_SONG=boyfriend"
        << " MILO_INPUT_SCRIPT=" << script
        << " MILO_MAX_FRAMES=" << maxFrames
        << " timeout " << timeout << " " << binary << " 2>&1";

    FILE *pipe = popen(cmd.str().c_str(), "r");
    DtaRunResult result = {-1, 0, "", false};
    if (!pipe) return result;

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
    // App.cpp fires enter_gameplay after setting up game data.
    // The "game data initialized" log proves we reached the call site.
    EXPECT_TRUE(outputContains("game data initialized"))
        << "enter_gameplay was never called — DC3_SCREEN=game_screen "
        << "code path in App.cpp didn't fire";
}

TEST_F(DtaFlowTest, LoadingChainTransitions) {
    // enter_gameplay triggers a transition to loading_screen
    EXPECT_TRUE(outputContains("Screen 'loading_screen' Enter"))
        << "loading_screen was never entered — enter_gameplay DTA function "
        << "didn't trigger the expected screen transition";
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

TEST_F(DtaFlowTest, HamDirectorEnterFires) {
    // HamDirector::Enter() fires from the meta_game panel cascade
    EXPECT_TRUE(outputContains("HamDirector::Enter()"))
        << "HamDirector::Enter() never fired — meta_game panel cascade "
        << "didn't reach HamDirector (world_panel may not have entered)";
}

TEST_F(DtaFlowTest, OnFileLoadedCallbacks) {
    // HamDirector::OnFileLoaded fires for both song and venue
    EXPECT_TRUE(outputContains("OnFileLoaded('song')"))
        << "OnFileLoaded('song') never fired — FileMerger didn't complete "
        << "song loading or callback wasn't dispatched";
    EXPECT_TRUE(outputContains("OnFileLoaded('venue')"))
        << "OnFileLoaded('venue') never fired — venue .milo didn't load "
        << "or callback wasn't dispatched";
}

TEST_F(DtaFlowTest, NoCrashCleanExit) {
    EXPECT_EQ(sResult.signal, 0)
        << "Engine crashed with signal " << sResult.signal;
    EXPECT_FALSE(sResult.timedOut)
        << "Engine timed out (hung during DTA flow)";
    EXPECT_EQ(sResult.exitCode, 0)
        << "Engine exited with code " << sResult.exitCode;
}
