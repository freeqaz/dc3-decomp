// Gameplay telemetry integration tests.
//
// Launches dc3-native with the scripted boot->menu->gameplay path, parses the
// structured DC3_TEL output, and asserts real gameplay invariants against the
// current UI screen plus GamePanel/HamDirector state. This suite is intended to
// be the north-star gameplay check for native/web parity work.
//
// See: docs/sessions/2026-03-17-gameplay-telemetry-tests.md

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
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
#include <sys/stat.h>
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

// Same defect class as test_dta_flow.cpp, measured 2026-08-23: a lane that
// built only the milo-tests target got every engine-driven assertion red with
// gameplay-shaped messages, and the real cause was
//     timeout: failed to run command '.../dc3-native': No such file or directory
// Report the missing prerequisite as itself.
static std::string sTelSetupError;

static bool Dc3NativeIsPresent() {
    const std::string binary = GetDc3NativePath();
    struct stat st;
    if (::stat(binary.c_str(), &st) == 0) return true;
    sTelSetupError =
        "dc3-native does not exist at:\n    " + binary +
        "\nThis suite drives the real engine as a subprocess; without it every "
        "assertion is about output that was never produced. Build it:\n"
        "    cmake --build <build-dir> --target dc3-native";
    return false;
}

static TelRunResult RunWithTelemetry(int maxFrames, const char *script, int timeout = 120) {
    if (!Dc3NativeIsPresent()) return TelRunResult{-1, 0, "", false};
    std::string binary = GetDc3NativePath();
    std::ostringstream cmd;
    cmd << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_TEL=1 DC3_FAST_BOOT=1"
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
        if (!sTelSetupError.empty()) {
            GTEST_FAIL() << "GameplayTelemetryTest could not run the engine.\n"
                         << sTelSetupError;
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

    // Phase helpers — derive milestones from telemetry rather than hardcoded
    // frame windows so the suite tracks real game state.
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

// ---------------------------------------------------------------------------
// Tier 3: SetFrame path — verify HamDirector::Poll() drives animation
//
// The SetFrame path in HamDirector::Poll() drives song anim prop key evaluation
// on native/web (replacing the Xbox select_camera → OnSelectCamera → SetFrame
// chain). These tests verify the engine works correctly via the SetFrame path.
// ---------------------------------------------------------------------------

TEST_F(GameplayTelemetryTest, NativeSetFrameDrivesAnimation) {
    // The SetFrame call in HamDirector::Poll() should fire repeatedly during
    // gameplay, driving prop key evaluation (move_interp, clip interp, etc.).
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int finalCount = playing.back().getInt("nativeSetFrameCount", 0);
    EXPECT_GT(finalCount, 0)
        << "Native SetFrame path never fired during gameplay. "
        << "This means prop key evaluation (move_interp, clip interp) "
        << "is not being driven by HamDirector::Poll(). "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongAnimReachesMeaningfulFrame) {
    // Beyond just advancing, the song anim frame should reach a meaningful value
    // during gameplay (at least 1 second = 30 frames at 30fps), proving sustained
    // animation advancement rather than a one-time tick.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    float maxFrame = 0.0f;
    for (auto &s : playing) {
        float f = s.getFloat("songAnimFrame");
        if (f > maxFrame) maxFrame = f;
    }
    EXPECT_GT(maxFrame, 30.0f)
        << "songAnimFrame never reached 30.0 (1 second at 30fps) during gameplay. "
        << "Max frame was " << maxFrame << ". " << progressSummary();
}

// ---------------------------------------------------------------------------
// Tier 4: Move/flashcard data validation
//
// Verify that the SetFrame → move_interp → flashcard update chain actually
// produces correct dance move data during gameplay. The earlier tiers prove
// that songAnim advances and prop keys exist; these tests prove the
// downstream effect — real move names reaching the game objects.
// ---------------------------------------------------------------------------

TEST_F(GameplayTelemetryTest, MoveInterpActiveDuringGameplay) {
    // The "move" prop key track on the Easy difficulty song.anim should exist
    // and have keys during gameplay. This proves the interp handler chain
    // (move_interp) CAN fire when SetFrame evaluates prop keys.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    bool found = false;
    int maxKeyCount = 0;
    for (auto &s : playing) {
        int keys = s.getInt("moveKeyCount", 0);
        if (keys > maxKeyCount) maxKeyCount = keys;
        if (s.getBool("moveInterpActive")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "moveInterpActive was never true during gameplay. "
        << "The move prop key track either does not exist or has zero keys. "
        << "Max moveKeyCount observed: " << maxKeyCount << ". "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, SongAnimFrameAdvancesSteadily) {
    // During gameplay, songAnimFrameRate (delta between samples) should be
    // positive in at least 50% of samples. This proves sustained animation
    // advancement, not just a one-time tick or stall.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int advancingCount = 0;
    for (auto &s : playing) {
        float rate = s.getFloat("songAnimFrameRate", 0.0f);
        if (rate > 0.0f) advancingCount++;
    }
    float ratio = (float)advancingCount / (float)playing.size();
    EXPECT_GE(ratio, 0.5f)
        << "songAnimFrameRate was positive in only " << advancingCount
        << " of " << playing.size() << " gameplay samples ("
        << (ratio * 100.0f) << "%). "
        << "Expected at least 50% to show forward progress. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, ActiveMovesAppearDuringGameplay) {
    // During gameplay, at least one player should have a non-null, non-Rest
    // current move in at least one sample. This is end-to-end proof that
    // SetFrame → move_interp → set_cur_move produces real dance moves
    // that reach the MoveDir, which the HUD flashcard system reads from.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    bool found = false;
    int maxMoves = 0;
    for (auto &s : playing) {
        int moves = s.getInt("activeMoveCount", 0);
        if (moves > maxMoves) maxMoves = moves;
        if (moves > 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "activeMoveCount was never > 0 during gameplay. "
        << "No player had a real (non-Rest) current move assigned. "
        << "This means the move_interp → set_cur_move chain is not producing "
        << "dance moves that reach MoveDir::CurrentMove(). "
        << "Max activeMoveCount observed: " << maxMoves << ". "
        << progressSummary();
}

// ---------------------------------------------------------------------------
// Tier 5: HUD merge convergence — verify the merge target invariants
//
// These tests verify that the FileMerger game_hud merge goes to the correct
// target (WorldDir::mHUD), and that downstream DTA state is correct.
// See: docs/sessions/2026-03-26-hud-merge-target-divergence.md
//
// T1: MergerDir() == world->GetHUD()
// T2: DataVariable("hud_panel") == world->GetHUD()
// T3: hud_left/hud_right findable as children of the merge target
//
// Note: These use game_screen + gameStage=playing as the gameplay indicator
// rather than the `state` field, because GetGameState() can return "loading"
// even when the game is actively playing (DTA message handler issue).
// ---------------------------------------------------------------------------

TEST_F(GameplayTelemetryTest, HudMergeTargetMatchesWorldHUD) {
    // T1: The game_hud merger's MergerDir() should be the same object as
    // WorldDir::mHUD. On Xbox this is always true. On native, the merger's
    // mDir ObjPtr resolves to the "hud" PanelDir (= WorldDir::mHUD) during
    // deserialization, so MergerDir() == mHUD naturally.
    auto gs = samplesOnScreen("game_screen");
    ASSERT_FALSE(gs.empty())
        << "Never reached game_screen. " << progressSummary();

    bool found = false;
    for (auto &s : gs) {
        if (s.getBool("hudMergeTargetIsHUD")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "hudMergeTargetIsHUD was never true on game_screen. "
        << "The game_hud merger's MergerDir() is not the same object as "
        << "WorldDir::mHUD. This means the HUD merge goes to the wrong target "
        << "and DTA handlers on mHUD won't find merged children. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, HudPanelVariableMatchesWorldHUD) {
    // T2: The DTA variable $hud_panel should point to the WorldDir's mHUD.
    // This is set by the DTA enter handler on the HUD PanelDir.
    auto gs = samplesOnScreen("game_screen");
    ASSERT_FALSE(gs.empty())
        << "Never reached game_screen. " << progressSummary();

    bool found = false;
    for (auto &s : gs) {
        if (s.getBool("hudPanelIsHUD")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "hudPanelIsHUD was never true on game_screen. "
        << "DataVariable(\"hud_panel\") does not point to WorldDir::mHUD. "
        << "This means the DTA enter handler fired on the wrong PanelDir "
        << "or $hud_panel was overwritten. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, HudChildrenFoundAfterMerge) {
    // T3: After the game_hud merge, hud_left and hud_right must be findable
    // as children of the merge target. These are loaded from _default_hud.milo
    // and DTA handlers use {$this find "hud_left" FALSE} to populate player_huds.
    auto gs = samplesOnScreen("game_screen");
    ASSERT_FALSE(gs.empty())
        << "Never reached game_screen. " << progressSummary();

    bool hasLeft = false, hasRight = false;
    for (auto &s : gs) {
        if (s.getBool("hudHasLeft")) hasLeft = true;
        if (s.getBool("hudHasRight")) hasRight = true;
        if (hasLeft && hasRight) break;
    }
    EXPECT_TRUE(hasLeft)
        << "hudHasLeft was never true on game_screen. "
        << "hud_left was not found as a child of the merge target. "
        << progressSummary();
    EXPECT_TRUE(hasRight)
        << "hudHasRight was never true on game_screen. "
        << "hud_right was not found as a child of the merge target. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, HudMDirResolvedDuringGameplay) {
    // Diagnostic: Check whether the game_hud merger's mDir ObjPtr resolved
    // to a non-null object during deserialization. If false, the merger uses
    // the fallback path (mDir.Owner()->Dir()). Understanding this is key to
    // the root cause investigation (Q1/Q2).
    auto gs = samplesOnScreen("game_screen");
    ASSERT_FALSE(gs.empty())
        << "Never reached game_screen. " << progressSummary();

    bool resolved = false;
    for (auto &s : gs) {
        if (s.getBool("hudMDirResolved")) {
            resolved = true;
            break;
        }
    }
    // This is diagnostic — we log either way to inform the root cause investigation.
    // On Xbox, mDir is expected to point to the "hud" PanelDir.
    if (!resolved) {
        ADD_FAILURE()
            << "hudMDirResolved was never true on game_screen. "
            << "The game_hud merger's mDir ObjPtr is null — the merger uses the "
            << "fallback path (mDir.Owner()->Dir()) to determine the merge target. "
            << "Root cause is likely in ObjPtr deserialization or object hierarchy. "
            << progressSummary();
    }
}

TEST_F(GameplayTelemetryTest, NoHudDtaErrors) {
    // T5 partial: Check that the engine output does not contain "$hud not
    // function or object" errors, which indicate that get_player_hud returned
    // null because player_huds was not populated.
    size_t pos = sResult.output.find("$hud not function or object");
    EXPECT_EQ(pos, std::string::npos)
        << "Found '$hud not function or object' in engine output. "
        << "This means get_player_hud returned null during gameplay, "
        << "indicating $hud_panel was empty or pointed to the wrong PanelDir. "
        << progressSummary();
}

// ---------------------------------------------------------------------------
// Tier 6: Foot orientation validation
//
// Detects inverted/flipped feet during gameplay by checking ankle and toe bone
// world positions. The invariant: toe Z should be BELOW ankle Z (toe is closer
// to the ground). If inverted, the IK mLocalXfm back-computation or dirty
// cascade has gone wrong and the foot mesh flips 180 degrees upward through
// the shin.
//
// See: docs/sessions/2026-03-25-feet-in-ground-fix.md (Phase 4: Root Cause)
// ---------------------------------------------------------------------------

TEST_F(GameplayTelemetryTest, FootBonesFoundDuringGameplay) {
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    bool found = false;
    for (auto &s : playing) {
        if (s.getBool("footDataValid")) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found)
        << "footDataValid was never true during gameplay. "
        << "Could not find ankle/toe bones on player 0's character. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, NoInvertedFeetDuringGameplay) {
    // Core invariant: toe bone Z should be BELOW ankle bone Z.
    // If the toe is ABOVE the ankle by >2 units, the foot is inverted
    // (flipped 180 degrees through the shin). This was the root cause
    // of the "feet in ground" visual bug — actually inverted feet.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int lInvertedCount = 0;
    int rInvertedCount = 0;
    float worstLDelta = 0.0f;
    float worstRDelta = 0.0f;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        if (s.getBool("lFootInverted")) {
            lInvertedCount++;
            float delta = s.getFloat("lToeZ") - s.getFloat("lAnkleZ");
            if (delta > worstLDelta) worstLDelta = delta;
        }
        if (s.getBool("rFootInverted")) {
            rInvertedCount++;
            float delta = s.getFloat("rToeZ") - s.getFloat("rAnkleZ");
            if (delta > worstRDelta) worstRDelta = delta;
        }
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(lInvertedCount, 0)
        << "Left foot was inverted in " << lInvertedCount << "/" << footSamples
        << " gameplay samples. Worst toe-above-ankle delta: " << worstLDelta
        << " units. This indicates the IK mLocalXfm back-computation is broken "
        << "or a dirty cascade is clobbering the ankle transform. "
        << progressSummary();

    EXPECT_EQ(rInvertedCount, 0)
        << "Right foot was inverted in " << rInvertedCount << "/" << footSamples
        << " gameplay samples. Worst toe-above-ankle delta: " << worstRDelta
        << " units. This indicates the IK mLocalXfm back-computation is broken "
        << "or a dirty cascade is clobbering the ankle transform. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, FootZAxisNotFlippedDuringGameplay) {
    // Secondary invariant: the ankle bone's local Z-axis (WorldXfm.m.z.z)
    // should not point strongly upward (positive Z). In a correct foot pose,
    // the Z-axis of the ankle rotation matrix should be roughly downward
    // or lateral. A value > 0.7 means the foot is rotated nearly 180 degrees
    // from its intended orientation.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int lFlippedCount = 0;
    int rFlippedCount = 0;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        if (s.getFloat("lFootZAxisZ") > 0.7f) lFlippedCount++;
        if (s.getFloat("rFootZAxisZ") > 0.7f) rFlippedCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(lFlippedCount, 0)
        << "Left ankle Z-axis pointed upward in " << lFlippedCount << "/"
        << footSamples << " gameplay samples. "
        << "This means the ankle rotation is flipped ~180 degrees. "
        << progressSummary();

    EXPECT_EQ(rFlippedCount, 0)
        << "Right ankle Z-axis pointed upward in " << rFlippedCount << "/"
        << footSamples << " gameplay samples. "
        << "This means the ankle rotation is flipped ~180 degrees. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, NoFootInversionWarningsInOutput) {
    // Check that the runtime MILO_NOTIFY_ONCE guard in HamIKEffector::Poll()
    // did not fire. This catches the inversion at the source, even for frames
    // that fall between telemetry sample intervals.
    size_t pos1 = sResult.output.find("FOOT INVERTED:");
    EXPECT_EQ(pos1, std::string::npos)
        << "HamIKEffector::Poll() detected an inverted foot during gameplay. "
        << "The toe bone was above the ankle bone after IK. "
        << progressSummary();

    size_t pos2 = sResult.output.find("FOOT FLIPPED:");
    EXPECT_EQ(pos2, std::string::npos)
        << "HamIKEffector::Poll() detected a flipped ankle rotation during gameplay. "
        << "The ankle Z-axis was pointing upward after IK. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, AnklesNotCollapsedDuringGameplay) {
    // Core invariant for "merged characters" bug: L and R ankles should be
    // separated in space. If they collapse to the same point, the pelvis
    // mLocalXfm was corrupted, pulling all bones to the skeleton root.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int collapsedCount = 0;
    float worstSeparation = 999.0f;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        float sep = s.getFloat("ankleSeparation");
        if (sep < worstSeparation) worstSeparation = sep;
        // Ankles closer than 3 units = collapsed
        if (sep < 3.0f) collapsedCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(collapsedCount, 0)
        << "L/R ankles were collapsed (< 3 units apart) in " << collapsedCount
        << "/" << footSamples << " gameplay samples. Worst separation: "
        << worstSeparation << " units. This is the 'merged characters' bug — "
        << "the pelvis mLocalXfm back-computation corrupted the skeleton root. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, LegsNotCollapsedDuringGameplay) {
    // Pelvis-to-ankle distance should stay reasonable during gameplay.
    // If it drops near zero, the leg bones have collapsed.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int collapsedCount = 0;
    float worstDist = 999.0f;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        float dist = s.getFloat("pelvisToLAnkle");
        if (dist < worstDist) worstDist = dist;
        if (dist < 10.0f) collapsedCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(collapsedCount, 0)
        << "Pelvis-to-ankle distance was < 10 units in " << collapsedCount
        << "/" << footSamples << " gameplay samples. Worst: " << worstDist
        << " units. Legs have collapsed — the IK mLocalXfm back-computation "
        << "is corrupting structural skeleton bones. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, NoBoneGarbageDuringGameplay) {
    // No ankle position should have garbage values (>10000 in any axis).
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int garbageCount = 0;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        float vals[] = {
            s.getFloat("lAnkleX"), s.getFloat("lAnkleY"), s.getFloat("lAnkleZ"),
            s.getFloat("rAnkleX"), s.getFloat("rAnkleY"), s.getFloat("rAnkleZ")
        };
        for (float v : vals) {
            if (std::fabs(v) > 10000.0f || std::isnan(v)) {
                garbageCount++;
                break;
            }
        }
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(garbageCount, 0)
        << "Ankle bone positions had garbage values (>10000 or NaN) in "
        << garbageCount << "/" << footSamples << " gameplay samples. "
        << "The Invert() in mLocalXfm back-computation likely hit a "
        << "degenerate matrix. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, FeetNotBelowFloorDuringGameplay) {
    // Real Xbox hardware does NOT exhibit feet-in-floor (verified by user
    // visual inspection of original game). Native port shows toes at Z ~ -3.3
    // due to an unresolved divergence in the IK pipeline. This test tracks
    // the bug — it should FAIL until we find and fix the divergence.
    //
    // Do NOT relax this threshold to make the test pass. The bug is real.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int lBelowCount = 0;
    int rBelowCount = 0;
    float worstLToeZ = 999.0f;
    float worstRToeZ = 999.0f;
    float worstLAnkleZ = 999.0f;
    float worstRAnkleZ = 999.0f;

    const float kFloorZ = 0.0f;
    // Strict threshold: any toe more than 2 units below floor is a real bug.
    const float kMaxPenetration = -2.0f;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        float lToe = s.getFloat("lToeZ");
        float rToe = s.getFloat("rToeZ");
        float lAnkle = s.getFloat("lAnkleZ");
        float rAnkle = s.getFloat("rAnkleZ");

        if (lToe < worstLToeZ) { worstLToeZ = lToe; worstLAnkleZ = lAnkle; }
        if (rToe < worstRToeZ) { worstRToeZ = rToe; worstRAnkleZ = rAnkle; }

        if (lToe < kFloorZ + kMaxPenetration) lBelowCount++;
        if (rToe < kFloorZ + kMaxPenetration) rBelowCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    printf("  Foot floor penetration check (%d samples):\n", footSamples);
    printf("    L-toe worst Z: %.2f (ankle Z: %.2f)\n", worstLToeZ, worstLAnkleZ);
    printf("    R-toe worst Z: %.2f (ankle Z: %.2f)\n", worstRToeZ, worstRAnkleZ);
    printf("    L below floor: %d/%d samples\n", lBelowCount, footSamples);
    printf("    R below floor: %d/%d samples\n", rBelowCount, footSamples);

    EXPECT_EQ(lBelowCount, 0)
        << "Left toe went below floor (Z < " << (kFloorZ + kMaxPenetration)
        << ") in " << lBelowCount << "/" << footSamples
        << " gameplay samples. Worst toe Z: " << worstLToeZ
        << " (ankle Z: " << worstLAnkleZ << "). "
        << "Toe is below floor — real Xbox does not exhibit this. "
        << progressSummary();

    EXPECT_EQ(rBelowCount, 0)
        << "Right toe went below floor (Z < " << (kFloorZ + kMaxPenetration)
        << ") in " << rBelowCount << "/" << footSamples
        << " gameplay samples. Worst toe Z: " << worstRToeZ
        << " (ankle Z: " << worstRAnkleZ << "). "
        << "Toe is below floor — real Xbox does not exhibit this. "
        << progressSummary();
}

// ---------------------------------------------------------------------------
// Tier 7: "Flying Feet" detection
//
// Catches the bug where character feet visually "fly around" during gameplay.
// The existing foot inversion tests (Tier 6) check structural invariants
// (toe-above-ankle, Z-axis flip) but miss the more common failure mode:
// sudden large position jumps, NaN/Inf propagation through the bone chain,
// and stale mLocalXfm from IKElbow's missing back-computation.
//
// Root cause hypothesis: IKElbow() at HamIKEffector.cpp:195-214 calls
// SetWorldXfm() on grandparent and parent bones WITHOUT back-computing
// mLocalXfm. When the dirty cascade later recomputes from stale mLocalXfm,
// the IK correction is discarded and bones snap to wrong positions.
// This is the same pattern as the ankle mLocalXfm fix (Phase 4), but
// IKElbow does it for the knee/upper-leg (ankle path) and forearm/upper-arm
// (hand path) without any guard.
//
// See: docs/sessions/2026-03-25-feet-in-ground-fix.md (Phase 4)
// ---------------------------------------------------------------------------

TEST_F(GameplayTelemetryTest, NoAnkleNaNDuringGameplay) {
    // No NaN or Inf should appear in ANY component of the ankle WorldXfm.
    // The existing NoBoneGarbageDuringGameplay test only checks position
    // magnitude. This checks the full 12-component rotation + translation
    // matrix, which catches NaN in rotation entries that produce visually
    // correct positions but wildly wrong orientations.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int nanCount = 0;
    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;
        if (s.getBool("ankleHasNaN")) nanCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(nanCount, 0)
        << "Ankle WorldXfm contained NaN/Inf in " << nanCount << "/"
        << footSamples << " gameplay samples. "
        << "NaN in the rotation matrix produces visually wild bone orientations. "
        << "Likely source: Invert() of a near-singular parent WorldXfm in the "
        << "mLocalXfm back-computation, or NeutralWorldXfm() returning "
        << "uninitialized data. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, NoAnkleLocalXfmNaNDuringGameplay) {
    // The mLocalXfm back-computation guard at HamIKEffector.cpp:473 uses
    // fabs(x) < 50 which correctly rejects NaN (fabs(NaN) < 50 is false).
    // But NaN could enter mLocalXfm through OTHER paths:
    // - Direct assignment from animation data
    // - IKElbow setting parent WorldXfm without back-computing local
    // - Dirty cascade recomputing from stale/corrupt parent chain
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int nanCount = 0;
    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;
        if (s.getBool("ankleLocalHasNaN")) nanCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(nanCount, 0)
        << "Ankle mLocalXfm contained NaN/Inf in " << nanCount << "/"
        << footSamples << " gameplay samples. "
        << "This means corrupt values entered the local transform through a "
        << "path that bypasses the sanity guard in HamIKEffector::Poll(). "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, NoAnkleSuddenJumpsDuringGameplay) {
    // "Flying feet" manifests as sudden large jumps in ankle world position
    // between samples. Normal dance choreography produces ankle deltas of
    // ~0.5-5 units per telemetry sample (10 frames at 30fps).
    //
    // 2026-08-19: this test was reporting a 130.6-unit L-ankle jump at frame
    // 1070 as flying feet. It is NOT flying feet, and the distinction matters
    // enough to encode. `lAnkleWorldDelta` is a WORLD-space delta, so on its
    // own it cannot tell "the foot moved relative to the body" (the IK bug)
    // from "the body moved and carried the foot with it" (character placement).
    // At frame 1070 of the 9050-frame ymca run, the evidence is unambiguous
    // that it was the latter:
    //
    //   lAnkle      (30.0, -21.6) -> ( 57.9, -149.1)   delta 130.55
    //   rAnkle      (-4.5, -27.5) -> ( 24.3, -152.1)   delta 127.95
    //   lHand       (18.0, -37.7) -> ( 40.8, -164.0)   same XY delta
    //   rHand       (-9.4, -28.7) -> ( 14.7, -154.0)   same XY delta
    //   ankleSeparation   35.0 -> 33.8   (its normal 33-35 range)
    //   pelvisToLAnkle    29.5 -> 30.2   (unchanged)
    //   lAnkleZ/rAnkleZ   4.2/3.9 -> 4.2/3.9  (height above floor unchanged)
    //   lAnkleLocal      18.48/0/0 -> 18.48/0/0  (bit-identical)
    //   charClipLayers       2 -> 1
    //
    // Every internal body relationship is preserved and both hands move by the
    // same vector: the whole character was rigidly translated in XY, coincident
    // with a clip layer being dropped. The causes this test names in its own
    // failure message -- stale mLocalXfm, a constraint target changing,
    // NeutralWorldXfm garbage -- would all DISTORT the body, not translate it.
    //
    // So classify instead of thresholding. A jump is only "flying feet" if the
    // body articulated: the two ankles moved by materially different amounts,
    // or the ankle-to-ankle / pelvis-to-ankle distances changed. A jump where
    // everything moved together is a reposition, reported separately (and still
    // bounded, so a regression that starts teleporting the dancer repeatedly
    // goes red rather than being explained away by this comment).
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    // Threshold: 40 units accommodates large dance moves (YMCA has
    // ~30-unit XY displacements).
    const float kMaxDelta = 40.0f;
    // Two ankles count as moving "together" if their deltas agree to within
    // 15% of the larger one. The observed reposition agreed to 2%.
    const float kRigidRatio = 0.15f;
    // ...and the body must not have deformed while doing it.
    const float kMaxShapeChange = 5.0f;

    int footSamples = 0;
    int articulatedJumps = 0;   // real flying feet
    int rigidRepositions = 0;   // whole-character moves
    float worstArticulated = 0.0f;
    int worstArticulatedFrame = 0;
    float worstRigid = 0.0f;
    int worstRigidFrame = 0;
    std::string rigidDetail;

    float prevSep = -1.0f, prevPelvis = -1.0f;
    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        // Skip state transitions — character placement and animation
        // rewinds produce legitimate large position changes.
        if (s.getString("state") != "playing") continue;
        footSamples++;

        float ld = s.getFloat("lAnkleWorldDelta");
        float rd = s.getFloat("rAnkleWorldDelta");
        float sep = s.getFloat("ankleSeparation");
        float pelvis = s.getFloat("pelvisToLAnkle");
        float sepChange = (prevSep < 0.0f) ? 0.0f : std::fabs(sep - prevSep);
        float pelvisChange = (prevPelvis < 0.0f) ? 0.0f : std::fabs(pelvis - prevPelvis);
        prevSep = sep;
        prevPelvis = pelvis;

        float big = std::max(ld, rd);
        if (big <= kMaxDelta) continue;

        bool movedTogether = big > 0.0f
            && std::fabs(ld - rd) / big <= kRigidRatio
            && sepChange <= kMaxShapeChange
            && pelvisChange <= kMaxShapeChange;

        if (movedTogether) {
            rigidRepositions++;
            if (big > worstRigid) {
                worstRigid = big;
                worstRigidFrame = s.frame;
                std::ostringstream d;
                d << "L=" << ld << " R=" << rd << " sepChange=" << sepChange
                  << " pelvisChange=" << pelvisChange
                  << " charClipLayers=" << s.getString("charClipLayers");
                rigidDetail = d.str();
            }
        } else {
            articulatedJumps++;
            if (big > worstArticulated) {
                worstArticulated = big;
                worstArticulatedFrame = s.frame;
            }
        }
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    printf("  Ankle jump classification (%d samples, threshold=%.0f units):\n",
           footSamples, kMaxDelta);
    printf("    articulated (flying feet): %d, worst %.1f at frame %d\n",
           articulatedJumps, worstArticulated, worstArticulatedFrame);
    printf("    rigid repositions        : %d, worst %.1f at frame %d  %s\n",
           rigidRepositions, worstRigid, worstRigidFrame, rigidDetail.c_str());

    EXPECT_EQ(articulatedJumps, 0)
        << "An ankle moved " << worstArticulated << " units at frame "
        << worstArticulatedFrame << " WITHOUT the rest of the body moving with "
        << "it (the other ankle disagreed by more than "
        << (kRigidRatio * 100.0f) << "%, or ankle separation / pelvis-to-ankle "
        << "distance changed by more than " << kMaxShapeChange << " units). "
        << "That is the 'flying feet' bug: the IK solve produced a wildly "
        << "different ankle position while the body stayed put. "
        << progressSummary();

    // A whole-character reposition is not an IK bug, but it is a visible
    // teleport and there should be at most the one known clip-layer handoff.
    // Bounded, not ignored -- if the dancer starts hopping around the stage
    // this still fails, it just fails with the right diagnosis.
    EXPECT_LE(rigidRepositions, 1)
        << "The character was rigidly repositioned " << rigidRepositions
        << " times mid-gameplay (worst " << worstRigid << " units at frame "
        << worstRigidFrame << "; " << rigidDetail << "). One handoff at the "
        << "start of the routine is known and tolerated; more than that is a "
        << "character-placement regression, NOT an IK/flying-feet bug. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, AnkleLocalXfmStaysSane) {
    // The mLocalXfm back-computation guard rejects values > 50 in any axis.
    // But the ORIGINAL mLocalXfm from animation should also be sane — ankle
    // local transforms represent the offset from the knee joint, which should
    // be roughly the shin length (~15-25 units in X, small in Y/Z).
    // Values > 100 in mLocalXfm indicate the back-computation wrote a bad
    // value that passed the guard, or animation data is corrupt.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    const float kMaxLocal = 100.0f;
    int footSamples = 0;
    int insaneCount = 0;
    float worstVal = 0.0f;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        float vals[] = {
            s.getFloat("lAnkleLocalX"), s.getFloat("lAnkleLocalY"),
            s.getFloat("lAnkleLocalZ"),
            s.getFloat("rAnkleLocalX"), s.getFloat("rAnkleLocalY"),
            s.getFloat("rAnkleLocalZ")
        };
        for (float v : vals) {
            float av = std::fabs(v);
            if (av > kMaxLocal) {
                insaneCount++;
                if (av > worstVal) worstVal = av;
                break;
            }
        }
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(insaneCount, 0)
        << "Ankle mLocalXfm had values > " << kMaxLocal << " in "
        << insaneCount << "/" << footSamples << " gameplay samples. "
        << "Worst value: " << worstVal << ". "
        << "The IK back-computation is writing oversized local transforms. "
        << "The sanity guard (< 50) should catch this — but the guard only "
        << "runs on ankle/hand effectors, not on parent bones modified by IKElbow. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, NoHandNaNDuringGameplay) {
    // IKElbow is also called for hand effectors (type == kEffectorTypeHand).
    // The same missing mLocalXfm back-computation bug applies to hands:
    // IKElbow::SetWorldXfm on forearm+upper-arm without updating mLocalXfm.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int nanCount = 0;
    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;
        if (s.getBool("handHasNaN")) nanCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(nanCount, 0)
        << "Hand bone WorldXfm contained NaN/Inf in " << nanCount << "/"
        << footSamples << " gameplay samples. "
        << "IKElbow modifies the forearm and upper-arm parent bones without "
        << "back-computing mLocalXfm, which can propagate NaN through the "
        << "dirty cascade. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, HandBonesNotFlyingDuringGameplay) {
    // Hand bones are also affected by IKElbow. "Flying hands" means a hand
    // detaches from the BODY -- so measure the hand relative to the body.
    //
    // 2026-08-19: this test used to assert |world coordinate| <= 200 on the
    // premise, stated in its own comment, that "character is at world origin".
    // The game violates that premise: the dancer is repositioned to roughly
    // (25..75, -150) once at the start of the routine (see the classification
    // in NoAnkleSuddenJumpsDuringGameplay). Measured over an audit run:
    //
    //   world-space max-abs coordinate : p50 160.8  p99 191.0  max 199.5
    //   body-relative hand distance    : p50  44.5  p99  74.4  max  79.6
    //
    // The old assertion was passing with 0.5 units of margin out of 200 -- a
    // 0.25% margin against a number that tracks where the dancer happens to
    // stand, not whether their hands are attached. It went red on a rerun where
    // the reposition landed slightly further out, which is how this was found.
    //
    // The ankle midpoint is used as the character-root proxy; the telemetry
    // does not carry a pelvis world position, and the ankles are already
    // validated as a rigid pair by the jump classifier.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    // ~2x headroom over the observed 79.6 max. A genuine flying hand is off by
    // hundreds of units (the IKElbow failure mode writes garbage transforms),
    // so this stays sensitive to the bug while being immune to where the
    // character is standing.
    const float kMaxFromBody = 150.0f;
    int footSamples = 0;
    int flyingCount = 0;
    int nanCount = 0;
    float worstVal = 0.0f;
    int worstFrame = 0;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        const float cx = (s.getFloat("lAnkleX") + s.getFloat("rAnkleX")) * 0.5f;
        const float cy = (s.getFloat("lAnkleY") + s.getFloat("rAnkleY")) * 0.5f;
        const float cz = (s.getFloat("lAnkleZ") + s.getFloat("rAnkleZ")) * 0.5f;

        const char *hands[] = {"lHand", "rHand"};
        for (const char *h : hands) {
            float x = s.getFloat(std::string(h) + "X");
            float y = s.getFloat(std::string(h) + "Y");
            float z = s.getFloat(std::string(h) + "Z");
            if (std::isnan(x) || std::isnan(y) || std::isnan(z)) {
                nanCount++;
                break;
            }
            float dx = x - cx, dy = y - cy, dz = z - cz;
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > worstVal) {
                worstVal = dist;
                worstFrame = s.frame;
            }
            if (dist > kMaxFromBody) {
                flyingCount++;
                break;
            }
        }
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    printf("  Hand-to-body distance: worst %.1f at frame %d (limit %.0f), "
           "%d/%d samples over limit\n",
           worstVal, worstFrame, kMaxFromBody, flyingCount, footSamples);

    EXPECT_EQ(nanCount, 0)
        << "Hand bone position was NaN in " << nanCount << "/" << footSamples
        << " gameplay samples. " << progressSummary();

    EXPECT_EQ(flyingCount, 0)
        << "A hand bone was more than " << kMaxFromBody << " units from the "
        << "character's own body (ankle midpoint) in " << flyingCount << "/"
        << footSamples << " gameplay samples. Worst: " << worstVal
        << " units at frame " << worstFrame << ". "
        << "This is the 'flying hands' variant of the IKElbow bug -- and unlike "
        << "the old world-space check, it cannot be triggered merely by the "
        << "dancer standing somewhere other than the origin. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, KneeLocalXfmNotCorruptDuringGameplay) {
    // The knee bone is the parent of the ankle. IKElbow() calls
    // SetWorldXfm() on the knee (parent) WITHOUT back-computing its
    // mLocalXfm. When another pollable triggers SetDirty() on the knee's
    // parent (upper-leg), the dirty cascade recomputes the knee's WorldXfm
    // from its STALE mLocalXfm, discarding the IK correction.
    //
    // This test checks that the knee's mLocalXfm.v.x (bone length along
    // the limb axis) stays reasonable. In a correct skeleton, the knee
    // local X should be roughly constant (shin length ~15-25 units).
    // If IKElbow's missing back-computation causes frame-to-frame
    // instability, the knee local X will oscillate or take extreme values.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int nanCount = 0;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;
        if (s.getBool("kneeLocalHasNaN")) nanCount++;
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(nanCount, 0)
        << "Knee mLocalXfm contained NaN/Inf in " << nanCount << "/"
        << footSamples << " gameplay samples. "
        << "The knee is the ankle's parent — IKElbow modifies it via "
        << "SetWorldXfm without back-computing mLocalXfm. This is the "
        << "root cause of the 'flying feet' bug. " << progressSummary();
}

TEST_F(GameplayTelemetryTest, AnkleRotationMatrixValidDuringGameplay) {
    // The ankle rotation matrix should have a determinant close to 1.0
    // (orthonormal rotation). Degenerate rotations (det near 0 or negative)
    // indicate the Invert() or MakeRotMatrix() produced a non-rotation
    // matrix, which causes the mesh to scale/shear/flip visually.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    int footSamples = 0;
    int badRotCount = 0;
    float worstDet = 1.0f;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        float det = s.getFloat("ankleRotDeterminant");
        if (std::isnan(det) || std::fabs(det - 1.0f) > 0.1f) {
            badRotCount++;
            if (std::fabs(det - 1.0f) > std::fabs(worstDet - 1.0f)) {
                worstDet = det;
            }
        }
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(badRotCount, 0)
        << "Ankle rotation matrix had non-unit determinant in " << badRotCount
        << "/" << footSamples << " gameplay samples. Worst determinant: "
        << worstDet << " (should be ~1.0). "
        << "A degenerate rotation causes mesh shearing/flipping. "
        << "Likely source: Invert() of a near-singular parent WorldXfm or "
        << "MakeRotMatrix() from a non-normalized quaternion. "
        << progressSummary();
}

TEST_F(GameplayTelemetryTest, AnklePositionSmoothnessDuringGameplay) {
    // Beyond detecting single-frame teleports (> 20 units), this test
    // measures the overall smoothness of ankle motion. In correct dance
    // choreography, the MEDIAN frame-to-frame delta should be small
    // (< 5 units per 10-frame sample). If the median is high, feet are
    // jittering even if no single frame exceeds the teleport threshold.
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    std::vector<float> lDeltas, rDeltas;
    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        float ld = s.getFloat("lAnkleWorldDelta");
        float rd = s.getFloat("rAnkleWorldDelta");
        // Skip the first sample (delta = 0 from initialization)
        if (ld > 0.0f || rd > 0.0f) {
            lDeltas.push_back(ld);
            rDeltas.push_back(rd);
        }
    }

    if (lDeltas.size() < 5) {
        GTEST_SKIP() << "Not enough ankle delta samples for smoothness check";
    }

    // Compute median
    std::sort(lDeltas.begin(), lDeltas.end());
    std::sort(rDeltas.begin(), rDeltas.end());
    float lMedian = lDeltas[lDeltas.size() / 2];
    float rMedian = rDeltas[rDeltas.size() / 2];

    // Compute 90th percentile
    float lP90 = lDeltas[(size_t)(lDeltas.size() * 0.9)];
    float rP90 = rDeltas[(size_t)(rDeltas.size() * 0.9)];

    printf("  Ankle smoothness (%zu samples):\n", lDeltas.size());
    printf("    L-ankle median delta: %.2f, P90: %.2f\n", lMedian, lP90);
    printf("    R-ankle median delta: %.2f, P90: %.2f\n", rMedian, rP90);

    // Median delta should be < 5 units (normal choreography)
    const float kMaxMedian = 5.0f;
    // 90th percentile should be < 15 units (allows occasional fast moves)
    const float kMaxP90 = 15.0f;

    EXPECT_LT(lMedian, kMaxMedian)
        << "Left ankle median frame-to-frame delta was " << lMedian
        << " units (threshold: " << kMaxMedian << "). "
        << "The ankle is jittering — feet are visually unstable even without "
        << "single-frame teleports. This suggests the IK system is oscillating "
        << "due to stale mLocalXfm from IKElbow's missing back-computation. "
        << progressSummary();

    EXPECT_LT(rMedian, kMaxMedian)
        << "Right ankle median frame-to-frame delta was " << rMedian
        << " units (threshold: " << kMaxMedian << "). " << progressSummary();

    EXPECT_LT(lP90, kMaxP90)
        << "Left ankle 90th percentile delta was " << lP90
        << " units (threshold: " << kMaxP90 << "). "
        << "Frequent large ankle jumps indicate systemic IK instability. "
        << progressSummary();

    EXPECT_LT(rP90, kMaxP90)
        << "Right ankle 90th percentile delta was " << rP90
        << " units (threshold: " << kMaxP90 << "). " << progressSummary();
}

TEST_F(GameplayTelemetryTest, AnkleSeparationNotExplodingDuringGameplay) {
    // Complement to AnklesNotCollapsedDuringGameplay: ankles should also
    // not be impossibly far apart. L/R ankles > 100 units apart means one
    // or both ankles have flown to a garbage position (the character's
    // stance can be at most ~30 units wide during extreme dance moves).
    auto playing = gameplaySamples();
    ASSERT_FALSE(playing.empty())
        << "Never observed real gameplay on game_screen. " << progressSummary();

    const float kMaxSeparation = 100.0f;
    int footSamples = 0;
    int explodedCount = 0;
    float worstSep = 0.0f;

    for (auto &s : playing) {
        if (!s.getBool("footDataValid")) continue;
        footSamples++;

        float sep = s.getFloat("ankleSeparation");
        if (sep > kMaxSeparation) {
            explodedCount++;
            if (sep > worstSep) worstSep = sep;
        }
    }

    if (footSamples == 0) {
        GTEST_SKIP() << "No foot data samples during gameplay";
    }

    EXPECT_EQ(explodedCount, 0)
        << "L/R ankles were > " << kMaxSeparation << " units apart in "
        << explodedCount << "/" << footSamples << " gameplay samples. "
        << "Worst separation: " << worstSep << " units. "
        << "One or both ankles have flown to a garbage position. "
        << progressSummary();
}
