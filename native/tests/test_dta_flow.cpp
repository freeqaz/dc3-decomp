// DTA flow integration tests
//
// Verifies the full DTA-driven panel flow works end-to-end using the ymca.txt
// input script: boot → attract → title → main → choose_mode → song_select
// → multiuser (driven by controller input: difficulty, play, skip_waiting)
// → loading → preloading → real_loading → game_screen.
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
#include <vector>
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

// `extraEnv` is prepended to the command as further VAR=value assignments
// (e.g. "DC3_CONTROLLER_MODE=faithful").
static DtaRunResult RunDtaFlow(int maxFrames, int timeout = 120,
                               const char *scriptName = "ymca.txt",
                               const char *extraEnv = "") {
    std::string binary = GetDc3NativePath();
    std::string script = GetScriptDir() + "/" + scriptName;

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
    // DC3_TEL at interval 1: GameplayReachesPlayingState reads the real
    // hamprovider game_stage off the per-frame telemetry line.
    cmd << extraEnv << (extraEnv[0] ? " " : "")
        << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
        << " DC3_TEL=1 DC3_TEL_INTERVAL=1"
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
    // The DTA flow navigates through multiuser_screen, whose start_game
    // (reached with the controller: seldiff_pane -> startgame_pane `play` ->
    // readywait_pane `skip_waiting`) fires enter_gameplay. This transitions
    // to loading_screen, proving the DTA function executed.  There is no
    // native auto-fire any more: the image's MultiUserGesturePanel::Poll
    // (82942EB8) only runs UpdateNavLists / UpdateProviderPlayerIndices.
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
    // hamprovider game_stage becomes `playing` for real, and only when the
    // intro is over: Game::Poll sends intro_over once the song clock crosses 0
    // ("Game::Poll: intro timer expired"), and the mode's DTA handler sets it
    // (game_modes.dta `intro_over`: {hamprovider set game_stage playing},
    // skipped only in rhythm_battle).  The image's GamePanel::StartGame
    // (HasIntro/Start, SetInGame, mState = kGamePlaying) sets no property.
    //
    // A native-only SetProperty(game_stage, playing) in StartGame -- which
    // runs ~25 ms of song time EARLIER, at TaskMgr seconds > -0.025 -- used to
    // be what this test looked for (its log line).  It clobbered the intro
    // stage for every mode (rhythm_battle's intro, holla_back's `title`), so
    // this now asserts the real state AND its order: no telemetry sample may
    // read gameStage=playing before intro_over was sent.  Both lines go to
    // stderr, so their order in the captured output is the order they ran.
    const std::string &out = sResult.output;
    size_t introOver = out.find("Game::Poll: intro timer expired");
    size_t firstPlaying = out.find("gameStage=playing");
    ASSERT_NE(firstPlaying, std::string::npos)
        << "no telemetry sample ever read gameStage=playing -- the intro_over "
           "DTA handler never ran, or loading stalled before gameplay";
    ASSERT_NE(introOver, std::string::npos)
        << "Game::Poll never sent intro_over (no 'intro timer expired')";
    EXPECT_GT(firstPlaying, introOver)
        << "game_stage read 'playing' before intro_over was sent: something "
           "other than the mode's intro_over handler forced it";
    size_t lineStart = out.rfind('\n', firstPlaying);
    std::string line = out.substr(lineStart + 1, firstPlaying - lineStart);
    EXPECT_NE(line.find("screen=game_screen"), std::string::npos)
        << "first gameStage=playing sample was not on game_screen:\n" << line;
}

TEST_F(DtaFlowTest, NoCrashCleanExit) {
    EXPECT_EQ(sResult.signal, 0)
        << "Engine crashed with signal " << sResult.signal;
    EXPECT_FALSE(sResult.timedOut)
        << "Engine timed out (hung during DTA flow)";
    EXPECT_EQ(sResult.exitCode, 0)
        << "Engine exited with code " << sResult.exitCode;
}

TEST_F(DtaFlowTest, SongLoadChainRunsOncePerSong) {
    // Game::IsLoaded's load chain (SongDB/Game PostLoad -- which deletes and
    // recreates mOvershell -- MoveMgr::LoadMoveData, LoadAllVariants, the
    // MoveMerger wait) runs once per song: the image's Game::Restart
    // (82864F08..82864F98) stores mRestartCount and mWaitState and never
    // touches mLoadState (+0xa0), so the Restart(true) that GamePanel::Reset
    // runs at song start leaves the game loaded.  A native-only
    // `mLoadState = 0` in Restart re-ran the whole chain on every song start.
    size_t n = 0;
    const std::string needle = "Game::IsLoaded() - Done waiting for MoveGraph";
    for (size_t p = sResult.output.find(needle); p != std::string::npos;
         p = sResult.output.find(needle, p + 1))
        n++;
    EXPECT_EQ(n, 1u) << "the song load chain ran " << n
                     << " times in one song (Restart reset mLoadState)";
}

TEST_F(DtaFlowTest, SongSelectPicksYmca) {
    // ymca.txt must actually play YMCA.  Perform-mode song select enters on
    // index 2 (song_select.dta: scroll_to_index 2 2 -- the song_tier_0 header)
    // and one down reaches ymca at index 3.  Until 2026-09-30 this flow pressed
    // four downs and quietly played `starships`: the fourth press reached the
    // song_tier_1 header at index 6, and an invented `- 1` in HamNavList::
    // OnMsg's ScrollDown edge fired one row early and hopped the cursor over
    // it.  With the image's edge restored, four downs select the header.
    EXPECT_TRUE(outputContains(
        "HamNavList: select name='right_hand.hnl' selected=3 first=2 sym='ymca'"))
        << "song select did not select ymca (index 3, first showing 2)";
}

// ===========================================================================
// multiuser_screen waits for input
// ===========================================================================
//
// Image: ?Poll@MultiUserGesturePanel@@UAAXXZ (82942EB8) is UpdateNavLists x2,
// UpdateProviderPlayerIndices, TexLoadPanel::Poll -- nothing that leaves the
// screen.  It leaves through its DTA panes' start_game, i.e. through input.
// A native-only mNativeEnterPending executed enter_gameplay on the first
// non-transition frame, skipping difficulty / character / crew select, the
// readywait pane and start_game itself (enter_game.flow, the campaign state
// step).  idle-multiuser.txt drives to multiuser_screen and presses nothing.

class DtaFlowIdleMultiuserTest : public ::testing::Test {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;

    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            return;
        sResult = RunDtaFlow(2500, 120, "idle-multiuser.txt");
        sRanEngine = true;
    }

    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        if (!sRanEngine)
            GTEST_SKIP() << "Engine did not run (SetUpTestSuite failed)";
        if (!sResult.setupError.empty())
            GTEST_FAIL() << "DtaFlowIdleMultiuserTest could not run the engine.\n"
                         << sResult.setupError;
    }

    bool outputContains(const char *needle) const {
        return sResult.output.find(needle) != std::string::npos;
    }
};

DtaRunResult DtaFlowIdleMultiuserTest::sResult = {};
bool DtaFlowIdleMultiuserTest::sRanEngine = false;

TEST_F(DtaFlowIdleMultiuserTest, MultiuserScreenWaitsForInput) {
    ASSERT_TRUE(outputContains("Screen 'multiuser_screen' Enter"))
        << "precondition: the route never reached multiuser_screen";
    EXPECT_FALSE(outputContains("Screen 'multiuser_screen' Exit"))
        << "multiuser_screen left with no input at all -- something executed "
           "enter_gameplay / goto_screen on its own";
    EXPECT_FALSE(outputContains("Screen 'loading_screen' Enter"))
        << "loading_screen entered with no input on multiuser_screen";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// ===========================================================================
// Song select d-pad scroll edge
// ===========================================================================
//
// Image: ?OnMsg@HamNavList@@AAA?AVDataNode@@ABVButtonDownMsg@@@Z, 0x8244A11C:
//   lwz r10, sNumListSelectable / add r11, r11, r10 / cmpw cr6, r31, r11 / blt
// -- ScrollDown once the new selection reaches firstShowing +
// sNumListSelectable (5), with no `- 1`; otherwise SetHighlight.  Tier headers
// are active rows, so none is skipped.  song-select-scroll.txt presses six
// downs from the entry focus (index 2, first 2): four highlight moves to the
// song_tier_1 header (6), then two ScrollDowns (7 >= 2+5, 8 >= 3+5), so the
// confirm selects index 8 with first showing 4.  The `- 1` edge scrolled one
// press early, skipped index 6, and ended on index 9 / first 5.

class DtaFlowSongSelectScrollTest : public ::testing::Test {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;

    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            return;
        sResult = RunDtaFlow(700, 120, "song-select-scroll.txt");
        sRanEngine = true;
    }

    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        if (!sRanEngine)
            GTEST_SKIP() << "Engine did not run (SetUpTestSuite failed)";
        if (!sResult.setupError.empty())
            GTEST_FAIL() << "DtaFlowSongSelectScrollTest could not run the engine.\n"
                         << sResult.setupError;
    }

    bool outputContains(const char *needle) const {
        return sResult.output.find(needle) != std::string::npos;
    }
};

DtaRunResult DtaFlowSongSelectScrollTest::sResult = {};
bool DtaFlowSongSelectScrollTest::sRanEngine = false;

TEST_F(DtaFlowSongSelectScrollTest, ScrollDownEdgeMatchesImage) {
    ASSERT_TRUE(outputContains("DC3 Input: wait_screen 'song_select_screen' satisfied"))
        << "precondition: the route never reached song_select_screen";
    EXPECT_TRUE(outputContains(
        "HamNavList: select name='right_hand.hnl' selected=8 first=4 "))
        << "six downs from index 2 / first 2 should select index 8 with first "
           "showing 4 (four highlight moves, then two ScrollDowns)";
    EXPECT_TRUE(outputContains("Screen 'song_select_screen' Exit (to 'multiuser_screen')"))
        << "the selected row was not a song (a header does not leave song select)";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// ===========================================================================
// Controller mode policy (DC3_CONTROLLER_MODE) and the `wake` directive
// ===========================================================================
//
// docs/debugging/native.md "Controller mode policy".  On the 360 a pad press
// outside controller mode only enters it (ShellInput::OnMsg(ButtonDownMsg)),
// and ShellInput::Poll exits it once unk_0x68.SplitMs() >= unk_0x98 (the
// helpbar's controller_mode_timeout) with no pad input.  Native defaults to
// `forced` (controller mode pinned on); `faithful` runs the image's bodies.
// controller-mode-wake.txt idles on main_screen for ~1900 frames with a pair
// of `wake`s (10 frames apart) every 120 frames, then wake + confirm (twice)
// to choose_mode_screen.  Both
// runs set DC3_CONTROLLER_MODE_TIMEOUT_MS=750 so the idle window spans
// several timeouts at any plausible headless frame rate; under `forced` the
// override is not even read.

static const int kWakeTimeoutMs = 750;

// The engine's lines, in output order, from the first line containing `from`.
static std::vector<std::string> LinesFrom(const std::string &out, const char *from) {
    std::vector<std::string> lines;
    size_t start = out.find(from);
    if (start == std::string::npos) return lines;
    start = out.rfind('\n', start);
    start = (start == std::string::npos) ? 0 : start + 1;
    std::istringstream in(out.substr(start));
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

static bool StartsWith(const std::string &line, const char *prefix) {
    return line.compare(0, strlen(prefix), prefix) == 0;
}

static const char *kWakeLine = "DC3 Input: wake at frame ";
static const char *kModeLine = "DC3 ControllerMode: ";
static const char *kMainSatisfied = "DC3 Input: wait_screen 'main_screen' satisfied";

class ControllerModeFlowTest : public ::testing::Test {
protected:
    static void RunOnce(DtaRunResult &result, bool &ran, const char *policyEnv) {
        if (!getenv("DC3_DTA_FLOW_TESTS") || ran) return;
        std::string env = std::string(policyEnv) + " DC3_CONTROLLER_MODE_TIMEOUT_MS=" +
                          std::to_string(kWakeTimeoutMs);
        result = RunDtaFlow(2400, 180, "controller-mode-wake.txt", env.c_str());
        ran = true;
    }

    void CheckRan(const DtaRunResult &result, bool ran) {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        if (!ran) GTEST_SKIP() << "Engine did not run (SetUpTestSuite failed)";
        if (!result.setupError.empty())
            GTEST_FAIL() << "ControllerModeFlowTest could not run the engine.\n"
                         << result.setupError;
    }

    static void Dump(const DtaRunResult &result) {
        auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string path = "/tmp/claude-1000/dta_flow_";
        if (info) path += info->name();
        path += ".log";
        std::ofstream f(path);
        if (f.is_open()) {
            f << result.output;
            fprintf(stderr, "Full output dumped to: %s\n", path.c_str());
        }
    }
};

class ControllerModeFaithfulTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        RunOnce(sResult, sRanEngine, "DC3_CONTROLLER_MODE=faithful");
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult ControllerModeFaithfulTest::sResult = {};
bool ControllerModeFaithfulTest::sRanEngine = false;

class ControllerModeForcedTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        // No DC3_CONTROLLER_MODE at all (even if the caller's environment
        // has one): the default must be `forced`.
        RunOnce(sResult, sRanEngine, "env -u DC3_CONTROLLER_MODE");
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult ControllerModeForcedTest::sResult = {};
bool ControllerModeForcedTest::sRanEngine = false;

// An exit while in controller mode on main_screen is the idle timeout: nothing
// else on that screen exits (the DTA's own exit_controller_mode calls fire on
// screen changes).  Each one must have waited the configured timeout.
TEST_F(ControllerModeFaithfulTest, IdleTimeoutExitsAfterConfiguredTimeout) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Native: controller mode policy = faithful"), std::string::npos)
        << "precondition: DC3_CONTROLLER_MODE=faithful did not reach the engine";
    std::vector<std::string> lines = LinesFrom(out, kMainSatisfied);
    ASSERT_FALSE(lines.empty()) << "precondition: the route never reached main_screen";

    int timeoutExits = 0;
    for (const std::string &line : lines) {
        if (StartsWith(line, "DC3 UI: Screen 'main_screen' Exit")) break;
        int immediate, wasIn, idleMs, timeoutMs;
        if (sscanf(line.c_str(),
                   "DC3 ControllerMode: exit (immediate=%d, was_in=%d, idle_ms=%d, "
                   "timeout_ms=%d)", &immediate, &wasIn, &idleMs, &timeoutMs) != 4)
            continue;
        if (!wasIn) continue;
        timeoutExits++;
        EXPECT_EQ(timeoutMs, kWakeTimeoutMs)
            << "DC3_CONTROLLER_MODE_TIMEOUT_MS did not reach ShellInput: " << line;
        EXPECT_GE(idleMs, timeoutMs)
            << "controller mode exited before the idle timeout elapsed: " << line;
    }
    EXPECT_GE(timeoutExits, 1)
        << "controller mode never timed out on main_screen in ~1900 idle frames "
           "(ExitControllerMode still pinned, or the Poll timeout never fires)";
}

// After each timeout exit the next `wake` must press (L3) and the next
// controller-mode event must be an enter; the wake 10 frames after a press
// lands inside controller mode and is a no-op (the 120-frame spacing between
// pairs is NOT used for that: whether 120 frames outlast 750 ms depends on
// the frame rate); and the closing wake + confirm reaches choose_mode_screen.
TEST_F(ControllerModeFaithfulTest, WakeReentersControllerMode) {
    std::vector<std::string> lines = LinesFrom(sResult.output, kMainSatisfied);
    ASSERT_FALSE(lines.empty()) << "precondition: the route never reached main_screen";

    int reentries = 0, noOps = 0;
    const std::string *prevWake = nullptr;
    int prevWakeFrame = -1000;
    for (size_t i = 0; i < lines.size(); i++) {
        if (StartsWith(lines[i], "DC3 UI: Screen 'main_screen' Exit")) break;
        int wakeFrame = -1;
        if (sscanf(lines[i].c_str(), "DC3 Input: wake at frame %d", &wakeFrame) == 1) {
            if (prevWake && wakeFrame - prevWakeFrame <= 10
                && prevWake->find("pressed button") != std::string::npos) {
                EXPECT_NE(lines[i].find("no-op (already awake)"), std::string::npos)
                    << "a wake right after a wake press was not a no-op:\n  "
                    << *prevWake << "\n  " << lines[i];
                noOps++;
            }
            prevWake = &lines[i];
            prevWakeFrame = wakeFrame;
        }
        if (!StartsWith(lines[i], "DC3 ControllerMode: exit (immediate=1, was_in=1"))
            continue;
        // the first wake after this exit, unless something else (a real press
        // swallowed into an enter) re-entered controller mode first
        size_t w = i + 1;
        while (w < lines.size() && !StartsWith(lines[w], kWakeLine)
               && !StartsWith(lines[w], kModeLine)
               && !StartsWith(lines[w], "DC3 UI: Screen 'main_screen' Exit"))
            w++;
        if (w >= lines.size() || !StartsWith(lines[w], kWakeLine)) continue;
        EXPECT_NE(lines[w].find("pressed button"), std::string::npos)
            << "the first wake after a timeout exit did not press:\n  "
            << lines[i] << "\n  " << lines[w];
        // the next controller-mode event after that wake
        size_t e = w + 1;
        while (e < lines.size() && !StartsWith(lines[e], kModeLine)) e++;
        ASSERT_LT(e, lines.size()) << "no controller-mode event after " << lines[w];
        EXPECT_TRUE(StartsWith(lines[e], "DC3 ControllerMode: enter"))
            << "the wake press did not re-enter controller mode:\n  " << lines[w]
            << "\n  " << lines[e];
        reentries++;
    }
    EXPECT_GE(reentries, 1) << "no timeout exit was followed by a wake";
    EXPECT_GE(noOps, 1) << "no wake followed a wake press";
    EXPECT_NE(sResult.output.find("Screen 'main_screen' Exit (to 'choose_mode_screen')"),
              std::string::npos)
        << "wake + confirm did not leave main_screen for choose_mode_screen";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// The native default: `wake` never presses, controller mode never runs the
// image's bodies (they are the only source of "DC3 ControllerMode:" lines),
// and the same script still navigates.
TEST_F(ControllerModeForcedTest, WakeIsANoOpUnderForcedDefault) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Native: controller mode policy = forced"), std::string::npos)
        << "the default policy is not `forced`";
    int wakes = 0, noOps = 0;
    for (const std::string &line : LinesFrom(out, "DC3 Native: controller mode policy")) {
        if (!StartsWith(line, kWakeLine)) continue;
        wakes++;
        if (line.find("no-op (already awake)") != std::string::npos) noOps++;
        else ADD_FAILURE() << "wake pressed under forced: " << line;
    }
    EXPECT_EQ(wakes, 33) << "controller-mode-wake.txt has 33 wake directives "
                            "(1 on title, 15 pairs on main, 2 closing)";
    EXPECT_EQ(noOps, wakes);
    EXPECT_EQ(out.find(kModeLine), std::string::npos)
        << "the faithful Enter/ExitControllerMode body ran under forced";
    EXPECT_NE(out.find("Screen 'main_screen' Exit (to 'choose_mode_screen')"),
              std::string::npos)
        << "confirm did not leave main_screen for choose_mode_screen";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}
