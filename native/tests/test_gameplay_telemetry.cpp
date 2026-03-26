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
    cmd << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_TEL=1"
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
        sResult = RunWithTelemetry(9050, script.c_str(), 180);
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

    std::vector<TelemetrySample> samplesOnScreen(const std::string &screen) const {
        std::vector<TelemetrySample> out;
        for (auto &s : sSamples) {
            if (s.getString("screen") == screen) out.push_back(s);
        }
        return out;
    }

    std::vector<TelemetrySample> gameplaySamples() const {
        std::vector<TelemetrySample> out;
        for (auto &s : sSamples) {
            if (s.getString("screen") == "game_screen"
                && s.getString("state") == "playing") {
                out.push_back(s);
            }
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

    const TelemetrySample *firstScreen(const std::string &screen) const {
        for (auto &s : sSamples) {
            if (s.getString("screen") == screen) return &s;
        }
        return nullptr;
    }

    std::string sampleSummary(const TelemetrySample &s) const {
        std::ostringstream oss;
        oss << "frame=" << s.frame
            << " screen=" << s.getString("screen", "<none>")
            << " state=" << s.getString("state", "<none>")
            << " transition=" << s.getString("transition", "<none>")
            << " gamePanelLoadState=" << s.getInt("gamePanelLoadState", -1)
            << " worldLoaded=" << s.getInt("worldLoaded", 0)
            << " mergerDir=" << s.getInt("mergerDir", 0)
            << " clipDir=" << s.getInt("clipDir", 0)
            << " songAnimFrame=" << s.getFloat("songAnimFrame", 0.0f)
            << " beat=" << s.getFloat("beat", 0.0f);
        return oss.str();
    }

    std::string lastSampleSummary() const {
        if (sSamples.empty()) return "no telemetry samples";
        return sampleSummary(sSamples.back());
    }

    std::string progressSummary() const {
        static const char *kOrderedScreens[] = {
            "attract_screen",
            "autosave_warning_screen",
            "title_screen",
            "wait_main_after_saveload_screen",
            "main_screen",
            "choose_mode_screen",
            "song_select_screen",
            "multiuser_screen",
            "loading_screen",
            "preloading_screen",
            "game_screen",
        };

        int bestIndex = -1;
        const TelemetrySample *bestSample = nullptr;
        for (auto &s : sSamples) {
            std::string cur = s.getString("screen");
            for (int i = 0; i < (int)(sizeof(kOrderedScreens) / sizeof(kOrderedScreens[0])); i++) {
                if (cur == kOrderedScreens[i] && i > bestIndex) {
                    bestIndex = i;
                    bestSample = &s;
                }
            }
        }

        std::ostringstream oss;
        if (bestSample) {
            oss << "furthest current screen="
                << kOrderedScreens[bestIndex]
                << " at " << sampleSummary(*bestSample);
        } else {
            oss << "no known screen progression observed";
        }
        oss << "; last sample: " << lastSampleSummary();
        return oss.str();
    }
};

TelRunResult GameplayTelemetryTest::sResult = {};
std::vector<TelemetrySample> GameplayTelemetryTest::sSamples = {};
bool GameplayTelemetryTest::sRanEngine = false;

TEST_F(GameplayTelemetryTest, NoCrashDuringRun) {
    EXPECT_EQ(sResult.signal, 0)
        << "Engine crashed with signal " << sResult.signal;
    EXPECT_EQ(sResult.exitCode, 0)
        << "Engine exited with code " << sResult.exitCode;
    EXPECT_FALSE(sResult.timedOut) << "Engine timed out (hung)";
}

TEST_F(GameplayTelemetryTest, TelemetryIncludesUIScreenAndLoadSignals) {
    ASSERT_FALSE(sSamples.empty()) << "No telemetry samples were captured";

    bool sawScreen = false;
    bool sawLoadState = false;
    for (auto &s : sSamples) {
        if (!s.getString("screen").empty()) sawScreen = true;
        if (s.getInt("gamePanelLoadState", -1) >= 0) sawLoadState = true;
    }

    EXPECT_TRUE(sawScreen) << "Telemetry never reported a current UI screen";
    EXPECT_TRUE(sawLoadState) << "Telemetry never reported GamePanel load state";
}

TEST_F(GameplayTelemetryTest, AutomationLeavesAttractScreen) {
    ASSERT_NE(firstScreen("attract_screen"), nullptr)
        << "Never observed attract_screen. " << progressSummary();

    bool leftAttract = false;
    for (auto &s : sSamples) {
        std::string screen = s.getString("screen");
        if (!screen.empty() && screen != "attract_screen") {
            leftAttract = true;
            break;
        }
    }

    EXPECT_TRUE(leftAttract)
        << "Automation never left attract_screen. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, AutomationReachesTitleScreen) {
    EXPECT_NE(firstScreen("title_screen"), nullptr)
        << "Never reached title_screen. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, AutomationReachesMainScreen) {
    EXPECT_NE(firstScreen("main_screen"), nullptr)
        << "Never reached main_screen. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, AutomationReachesChooseModeScreen) {
    EXPECT_NE(firstScreen("choose_mode_screen"), nullptr)
        << "Never reached choose_mode_screen. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, AutomationReachesSongSelectScreen) {
    EXPECT_NE(firstScreen("song_select_screen"), nullptr)
        << "Never reached song_select_screen. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, AutomationReachesMultiUserScreen) {
    EXPECT_NE(firstScreen("multiuser_screen"), nullptr)
        << "Never reached multiuser_screen. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, EngineReachesGameScreen) {
    EXPECT_NE(firstScreen("game_screen"), nullptr)
        << "Never reached game_screen. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, GamePanelLoadCompletesOnGameScreen) {
    auto gameScreen = samplesOnScreen("game_screen");
    ASSERT_FALSE(gameScreen.empty())
        << "Never reached game_screen. " << progressSummary();

    bool loaded = false;
    for (auto &s : gameScreen) {
        if (s.getInt("gamePanelLoadState", -1) == 4) {
            loaded = true;
            break;
        }
    }
    EXPECT_TRUE(loaded)
        << "game_screen became current, but GamePanel never reached load state 4. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, GameplayEntersIntroState) {
    bool found = false;
    for (auto &s : sSamples) {
        if (s.getString("screen") == "game_screen"
            && s.getString("state") == "intro") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "Never observed game_screen in intro state. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, GameplayEntersPlayingState) {
    auto playing = gameplaySamples();
    EXPECT_FALSE(playing.empty())
        << "Never observed game_screen in playing state. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, WorldPipelineConvergesDuringGameplay) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed game_screen in playing state. " << progressSummary();

    bool found = false;
    for (auto &s : playing) {
        if (s.getBool("worldLoaded")
            && s.getBool("worldPresent")
            && s.getBool("mergerDir")
            && s.getBool("clipDir")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "Gameplay began, but the world pipeline never converged to "
        << "worldLoaded=1, worldPresent=1, mergerDir=1, clipDir=1. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongAnimHasPropKeysDuringGameplay) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed game_screen in playing state. " << progressSummary();

    bool found = false;
    for (auto &s : playing) {
        if (s.getInt("songAnimKeys", -1) > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "During gameplay, song.anim never exposed any PropKeys tracks. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongAnimHasClipKeysDuringGameplay) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed game_screen in playing state. " << progressSummary();

    bool found = false;
    for (auto &s : playing) {
        if (s.getInt("clipKeyCount", -1) > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "During gameplay, clip keyframe count never became positive. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongAnimAdvances) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen, so song animation "
        << "advance cannot be evaluated yet. " << progressSummary();

    auto it = std::adjacent_find(playing.begin(), playing.end(),
        [](const TelemetrySample &a, const TelemetrySample &b) {
            return b.getFloat("songAnimFrame") > a.getFloat("songAnimFrame");
        });
    EXPECT_NE(it, playing.end())
        << "songAnimFrame never increased during actual gameplay samples. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongAnimFrameAdvancesPastInit) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    bool advanced = false;
    for (auto &s : playing) {
        float f = s.getFloat("songAnimFrame");
        if (f > 0.0f) { advanced = true; break; }
    }
    EXPECT_TRUE(advanced)
        << "songAnimFrame never became positive during actual gameplay. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongAnimationGateOpen) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    bool gateOpen = false;
    for (auto &s : playing) {
        if (s.getInt("doSongAnim", -1) == 1) { gateOpen = true; break; }
    }
    EXPECT_TRUE(gateOpen)
        << "doSongAnim was never 1 during actual gameplay. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, Player0SongAnimationReady) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    bool ready = false;
    for (auto &s : playing) {
        if (s.getInt("p0SongAnim", -99) > -1) { ready = true; break; }
    }
    EXPECT_TRUE(ready)
        << "p0SongAnim was never > -1 during actual gameplay. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongDriverLayersStayLiveAfterIntro) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    bool found = false;
    for (auto &s : playing) {
        if (s.getInt("charClipLayers", 0) > 0 && s.getInt("p0SongAnim", -99) > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "Player 0 never showed live song-driver layers during actual gameplay. "
        << progressSummary();
}
