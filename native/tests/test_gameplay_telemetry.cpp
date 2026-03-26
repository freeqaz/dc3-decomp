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
    // WorldDir::mHUD. On Xbox this is always true. On native, our workaround
    // forces it via SetHUD. The convergence goal is to make this true without
    // the SetHUD hack.
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
