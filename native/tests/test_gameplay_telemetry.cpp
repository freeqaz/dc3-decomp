// Gameplay telemetry integration tests
//
// Launches dc3-native with YMCA input script + DC3_TEL=1, parses structured
// telemetry output, and asserts gameplay state machine invariants.
//
// Tier 1: should pass today (guards working behavior)
// Tier 2: expected to fail today (each maps to engine work + hack audit phase)
//
// See: docs/sessions/2026-03-17-gameplay-telemetry-tests.md

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

#include "telemetry_parser.h"

// ---------------------------------------------------------------------------
// Helpers (shared with test_headless_boot.cpp — duplicated to keep standalone)
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

struct TelRunResult {
    int exitCode;
    int signal;
    std::string output;
    bool timedOut;
};

static TelRunResult RunWithTelemetry(int maxFrames, const char *script, int timeout = 120) {
    std::string binary = GetDc3NativePath();
    std::ostringstream cmd;
    cmd << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_TEL=1"
        << " MILO_MAX_FRAMES=" << maxFrames;
    if (script)
        cmd << " MILO_INPUT_SCRIPT=" << script;
    cmd << " timeout " << timeout << " " << binary << " 2>&1";

    FILE *pipe = popen(cmd.str().c_str(), "r");
    TelRunResult result = {-1, 0, "", false};
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
// Fixture: single engine run shared across all tests
// ---------------------------------------------------------------------------

class GameplayTelemetryTest : public ::testing::Test {
protected:
    static TelRunResult sResult;
    static std::vector<TelemetrySample> sSamples;
    static bool sRanEngine;

    static void SetUpTestSuite() {
        if (!getenv("DC3_GAMEPLAY_TESTS")) {
            return;
        }
        std::string script = GetScriptDir() + "/ymca.txt";
        sResult = RunWithTelemetry(3000, script.c_str(), 180);
        sSamples = ParseTelemetry(sResult.output);
        sRanEngine = true;
    }

    void SetUp() override {
        if (!getenv("DC3_GAMEPLAY_TESTS")) {
            GTEST_SKIP() << "Set DC3_GAMEPLAY_TESTS=1 to enable (requires game assets)";
        }
        if (!sRanEngine) {
            GTEST_SKIP() << "Engine did not run (SetUpTestSuite failed)";
        }
    }

    void TearDown() override {
        if (HasFailure() && sRanEngine) {
            std::string path = "/tmp/claude-1000/gameplay_tel_";
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

    // Phase helpers — detect from data, not hardcoded frame numbers
    std::vector<TelemetrySample> samplesInPhase(const std::string &state) const {
        std::vector<TelemetrySample> out;
        for (auto &s : sSamples) {
            if (s.getString("state") == state) out.push_back(s);
        }
        return out;
    }

    // Find first sample where a key has a non-default value
    const TelemetrySample *firstWhere(const std::string &key, const std::string &val) const {
        for (auto &s : sSamples) {
            if (s.getString(key) == val) return &s;
        }
        return nullptr;
    }
};

TelRunResult GameplayTelemetryTest::sResult = {};
std::vector<TelemetrySample> GameplayTelemetryTest::sSamples = {};
bool GameplayTelemetryTest::sRanEngine = false;

// ===========================================================================
// Tier 1: Should pass today
// ===========================================================================

TEST_F(GameplayTelemetryTest, EngineReachesGameScreen) {
    // At least one telemetry sample exists with a non-empty state
    bool hasState = false;
    for (auto &s : sSamples) {
        std::string st = s.getString("state");
        if (!st.empty() && st != "boot") {
            hasState = true;
            break;
        }
    }
    EXPECT_TRUE(hasState) << "No telemetry samples with gameplay state found. "
        << "Total samples: " << sSamples.size();
}

TEST_F(GameplayTelemetryTest, NoCrashDuringGameplay) {
    EXPECT_EQ(sResult.signal, 0)
        << "Engine crashed with signal " << sResult.signal;
    EXPECT_EQ(sResult.exitCode, 0)
        << "Engine exited with code " << sResult.exitCode;
    EXPECT_FALSE(sResult.timedOut) << "Engine timed out (hung)";
}

TEST_F(GameplayTelemetryTest, SongAnimAdvances) {
    // Property: songAnimFrame is not constant — at least one adjacent pair differs
    auto it = std::adjacent_find(sSamples.begin(), sSamples.end(),
        [](const TelemetrySample &a, const TelemetrySample &b) {
            return b.getFloat("songAnimFrame") > a.getFloat("songAnimFrame");
        });
    EXPECT_NE(it, sSamples.end())
        << "songAnimFrame never increased across " << sSamples.size() << " samples";
}

TEST_F(GameplayTelemetryTest, SongAnimMonotonicallyIncreases) {
    // Property: songAnimFrame never decreases between consecutive samples
    for (size_t i = 1; i < sSamples.size(); i++) {
        float prev = sSamples[i - 1].getFloat("songAnimFrame");
        float curr = sSamples[i].getFloat("songAnimFrame");
        if (curr < prev && prev - curr > 1.0f) {
            // Allow small jitter but not major regression
            FAIL() << "songAnimFrame decreased from " << prev << " to " << curr
                   << " between frame " << sSamples[i - 1].frame
                   << " and " << sSamples[i].frame;
        }
    }
}

// ===========================================================================
// Tier 2: Expected to fail today — each maps to hack audit phase
// ===========================================================================

TEST_F(GameplayTelemetryTest, VenueTypeDefSet) {
    // Hack audit: Phase A1 (set_type "world")
    // Note: reclassified as T1 — hack is permanent, test validates it works
    // TypeDef name may be "world" or "venue" depending on DTA handler ordering
    bool found = false;
    for (auto &s : sSamples) {
        std::string td = s.getString("typeDef");
        if (!td.empty()) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "Venue WorldDir never had a TypeDef set. "
        << "Either SetType() hack failed or VenueEnter didn't run.";
}

TEST_F(GameplayTelemetryTest, HamProviderInitialized) {
    // Hack audit: Phase A4 — likely passes today (init is early)
    auto *sample = firstWhere("hamProvider", "1");
    EXPECT_NE(sample, nullptr)
        << "TheHamProvider was never non-null. HamInit() may not have run.";
}

TEST_F(GameplayTelemetryTest, BeatStartsDuringGameplay) {
    // Hack audit: Phase D2 (intro timing)
    auto playing = samplesInPhase("playing");
    if (playing.empty()) {
        GTEST_SKIP() << "No gameplay-phase samples (game never reached playing state)";
    }
    bool beatStarted = false;
    for (auto &s : playing) {
        if (s.getFloat("beat") > 0.0f) {
            beatStarted = true;
            break;
        }
    }
    EXPECT_TRUE(beatStarted)
        << "Beat never became positive during gameplay phase. "
        << "Game::Poll() may not be calling SetSecondsAndBeat().";
}

TEST_F(GameplayTelemetryTest, BeatDrivenAnimation) {
    // Hack audit: Phase D2 — beat-driven timing works (not just wall-clock)
    // Property: after beat > 0, songAnimFrame still advances
    bool foundBeat = false;
    float frameAtBeatStart = 0.0f;
    float lastFrame = 0.0f;
    for (auto &s : sSamples) {
        if (!foundBeat && s.getFloat("beat") > 0.0f) {
            foundBeat = true;
            frameAtBeatStart = s.getFloat("songAnimFrame");
        }
        if (foundBeat) {
            lastFrame = s.getFloat("songAnimFrame");
        }
    }
    if (!foundBeat) {
        GTEST_SKIP() << "Beat never started — can't test beat-driven animation";
    }
    EXPECT_GT(lastFrame, frameAtBeatStart)
        << "songAnimFrame didn't advance after beat started "
        << "(beat-driven path may not be working). "
        << "Frame at beat start: " << frameAtBeatStart
        << ", last frame: " << lastFrame;
}

TEST_F(GameplayTelemetryTest, MergerDirAvailable) {
    // Hack audit: Phase B2 — mMerger->Dir() returns non-null WorldDir
    auto playing = samplesInPhase("playing");
    if (playing.empty()) {
        GTEST_SKIP() << "No gameplay-phase samples";
    }
    bool found = false;
    for (auto &s : playing) {
        if (s.getBool("mergerDir")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "mergerDir was never 1 during gameplay. "
        << "FileMerger pipeline may not have wired mMerger.";
}

TEST_F(GameplayTelemetryTest, PollEnabledDuringGameplay) {
    // Sanity: HamDirector should have mPollEnabled=true during gameplay
    auto playing = samplesInPhase("playing");
    if (playing.empty()) {
        GTEST_SKIP() << "No gameplay-phase samples";
    }
    bool found = false;
    for (auto &s : playing) {
        if (s.getBool("pollEnabled")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "mPollEnabled was never true during gameplay. "
        << "Game::StartGame() may not be calling SetPollEnabled(true).";
}
