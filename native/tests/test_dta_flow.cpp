// DTA flow integration tests
//
// Verifies the full DTA-driven panel flow works end-to-end using the ymca.txt
// input script: boot → attract → title → main → choose_mode → song_select
// → multiuser (driven by controller input: difficulty, play, skip_waiting)
// → [first-run move tutorial, only while kTutorialGeneral is unseen]
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
#include "platform/ExecutablePath.h"
#include "shell_timeout.h"

// ---------------------------------------------------------------------------
// Helpers (duplicated from test_headless_boot.cpp to keep standalone)
// ---------------------------------------------------------------------------

static std::string GetBinaryDir() {
    char buf[4096];
    ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
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
    // The cache/save root the run used, so a second boot can reuse it.
    std::string persistenceRoot;
};

static bool FileExists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

// Wall-clock backstop for one engine run.  This is a HANG detector, not a
// performance budget: a hung engine emits no further telemetry and is caught
// only by this cap, so the number only has to be far enough above a healthy
// run that a busy machine cannot reach it.
//
// Measured 2026-11-05, 5000-frame `ymca.txt` run: 67 s on a quiet box (load
// ~25), and the same work exceeds 120 s with several lanes building at once.
// That was a real false red: two consecutive landing gates failed
// DtaFlowTest.NoCrashCleanExit on `Engine timed out (hung during DTA flow)`
// while the engine was running normally -- 4760 frames at the cap, no signal,
// no assertion -- and the identical binary passed in 67 s once the box went
// quiet.  A/B against the pre-change engine (3000 frames, alternating, three
// pairs) found no throughput difference at all: 40/31/28 s vs 32/31/29 s.
//
// 300 s is ~4.5x the idle cost, which covers the contention seen so far.  A
// genuinely hung run still fails, just after 300 s instead of 120.  Only the
// long flow uses it: the shorter flows already sit at 3.6x or better of their
// own cost and keep their tighter caps, so a hang there is still reported
// quickly.
static constexpr int kEngineWallClockSeconds = 300;

// Frame budget for the long flow, and it is NOT a game-time budget: several UI
// transitions are gated on real time, so the same flow costs more FRAMES on a
// faster machine.  Measured 2026-11-06: the flow finished at frame ~4205 on a
// box running it at ~60 fps, and on a box running it at ~555 fps it reached the
// transition into loading_screen at frame 2626 with the transition still
// unfinished 2374 frames (4.3 s) later -- so the old 5000 cut a healthy run
// short and reported it as "loading_screen was never entered".  The first-run
// move tutorial (see ymca.txt) is ~3000 of those frames.  8000 clears the worst
// case measured with margin and still leaves a hung flow reporting through the
// wall-clock cap above rather than running forever.
static constexpr int kDtaFlowMaxFrames = 8000;

// A private persistence root for one engine run: DC3_CACHE_DIR and DC3_SAVE_DIR
// both point inside it, and it starts empty.
//
// This is not hygiene, it is what the assertions mean.  The engine's screen
// chain depends on saved state: enter_gameplay's first clause is
// `{! {profile_mgr has_seen_tutorial kTutorialGeneral}}` (ui/global.dta:230) and
// ProfileMgr::HasSeenTutorial is
//     mTutorialsSeen & tutorial || mGlobalOptionsSaveState != kMetaProfileLoaded
// The second clause is the image's "persistence is broken, do not nag"
// fallback, so whether the first-run move tutorial appears between
// multiuser_screen and loading_screen hinges on whether the global-options
// cache loaded -- i.e. on what is in the engine's cache directory.  With the
// save/load chain wired, letting the child inherit the caller's $HOME made the
// SAME binary take two different paths depending on what a previous run had
// left in ~/.local/share/dc3/cache, and wrote there.  An empty root per run
// makes every run a fresh profile (the tutorial path, which
// scripts/dc3-input-flows/ymca.txt drives) on every machine, and keeps all of
// the engine's state inside the fixture.
//
// The directory is deliberately left behind, as test_cache_native.cpp's
// MakeTempRoot() does, so a failure can be re-run against the same state.
static std::string MakePersistenceRoot() {
    char tmpl[] = "/tmp/dc3-dta-flow-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) return std::string();
    std::string root(dir);
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    return root;
}

// Tutorial bits, config/macros.dta:1466-1489 (ProfileMgr::mTutorialsSeen).
static constexpr int kTutorialGeneral = 1;
static constexpr int kTutorialFitness = 128;
static constexpr int kTutorialPlaylists = 2048;

// Write the global-options cache entry a previous session would have left:
// <root>/cache/global/global, the 0x39-byte block ProfileMgr::SaveGlobalOptions
// fills and SaveLoadManager writes through the cache (states 0x32/0x33,
// kStrGlobalCacheName).  The bytes are what the native engine itself wrote at
// the end of a 2026-11-13 Perform run, with mTutorialsSeen (an int at 0x28,
// after version/mono/sync/volumes/the nine option bools/sync preset/overscan)
// replaced by `tutorialsSeen`.  The offset is measured, not derived: zeroing
// byte 0x28 alone turns {profile_mgr has_seen_tutorial kTutorialGeneral} from
// 1 to 0 on the next boot.  Tutorials-seen is per CONSOLE -- it lives here,
// not in DC3_SAVE_DIR/profile_0_save.dat -- so a save dir without its cache
// dir is a console that has never seen a tutorial.
static bool SeedGlobalOptions(const std::string &root, int tutorialsSeen) {
    static const unsigned char kGlobalOptions[0x39] = {
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xc1, 0x00, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    unsigned char bytes[sizeof(kGlobalOptions)];
    memcpy(bytes, kGlobalOptions, sizeof(bytes));
    for (int i = 0; i < 4; i++)  // little-endian int, as the native stream writes it
        bytes[0x28 + i] = (unsigned char)((unsigned)tutorialsSeen >> (8 * i));
    std::string dir = root + "/cache/global";
    mkdir(dir.c_str(), 0755);
    std::ofstream f(dir + "/global", std::ios::binary);
    if (!f.is_open()) return false;
    f.write((const char *)bytes, sizeof(bytes));
    return f.good();
}

// `extraEnv` is prepended to the command as further VAR=value assignments
// (e.g. "DC3_CONTROLLER_MODE=faithful").  `fastBoot` false leaves
// DC3_FAST_BOOT out, so boot screens advance as the image's do -- title_screen
// in particular then waits for a confirm instead of auto-advancing.
// `seedTutorialsSeen` >= 0 starts the run from a console whose global options
// record those tutorials as seen (SeedGlobalOptions); -1 keeps the empty root.
// `reuseRoot` non-empty boots against a previous run's persistence root
// (DtaRunResult::persistenceRoot) instead of a fresh one, without seeding.
static DtaRunResult RunDtaFlow(int maxFrames, int timeout = kEngineWallClockSeconds,
                               const char *scriptName = "ymca.txt",
                               const char *extraEnv = "", bool fastBoot = true,
                               int seedTutorialsSeen = -1,
                               const std::string &reuseRoot = std::string()) {
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
    std::string persistence = reuseRoot.empty() ? MakePersistenceRoot() : reuseRoot;
    if (persistence.empty()) {
        pre.setupError =
            "could not create a temporary persistence root (mkdtemp "
            "'/tmp/dc3-dta-flow-XXXXXX').  The engine's cache and save "
            "directories have to be owned by the fixture: without them the "
            "flow's screen chain depends on whatever ~/.local/share/dc3/cache "
            "holds on this machine, and the run would write there.";
        return pre;
    }
    if (reuseRoot.empty() && seedTutorialsSeen >= 0 &&
        !SeedGlobalOptions(persistence, seedTutorialsSeen)) {
        pre.setupError = "could not write the seeded global options under " + persistence;
        return pre;
    }

    std::ostringstream cmd;
    // DC3_TEL at interval 1: GameplayReachesPlayingState reads the real
    // hamprovider game_stage off the per-frame telemetry line.
    cmd << kShellTimeoutLookup << extraEnv << (extraEnv[0] ? " " : "")
        << (fastBoot ? "" : "env -u DC3_FAST_BOOT ")
        << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0"
        << (fastBoot ? " DC3_FAST_BOOT=1" : "")
        << " DC3_TEL=1 DC3_TEL_INTERVAL=1"
        << " DC3_CACHE_DIR=" << persistence << "/cache"
        << " DC3_SAVE_DIR=" << persistence << "/saves"
        << " MILO_INPUT_SCRIPT=" << script
        << " MILO_MAX_FRAMES=" << maxFrames
        << " ${t:+$t " << timeout << "} " << binary << " 2>&1";

    FILE *pipe = popen(cmd.str().c_str(), "r");
    DtaRunResult result = {-1, 0, "", false, "", persistence};
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
        sResult = RunDtaFlow(kDtaFlowMaxFrames, kEngineWallClockSeconds);
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
            path += "_" + std::to_string((int)getpid()) + ".log"; // per run: shared by concurrent gates
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
    //
    // TWO exits are correct, because enter_gameplay's first clause is a
    // tutorial gate (ui/global.dta:230):
    //     {! {profile_mgr has_seen_tutorial kTutorialGeneral}}
    // and ProfileMgr::HasSeenTutorial is
    //     mTutorialsSeen & tutorial || mGlobalOptionsSaveState != kMetaProfileLoaded
    // That second clause is the image's "persistence is broken, do not nag"
    // fallback, and native used to take it on EVERY boot because nothing ever
    // loaded the global-options cache — so this test asserted the shortcut.
    // Now that the cache is wired, the answer depends on what is on disk:
    //   * a cache whose global options exist and have kTutorialGeneral set
    //     (any second run against the same DC3_CACHE_DIR) -> straight to
    //     loading_screen, the old expected string;
    //   * a FRESH cache (~/.local/share/dc3/cache absent, i.e. a new machine
    //     or a first run) -> the global options are created with the tutorials
    //     unset, and the first-run move tutorial is inserted first
    //     (ui/tutorial/tutorial.dta:1014-1109).  scripts/dc3-input-flows/
    //     ymca.txt drives its five screens; screen_4 marks kTutorialGeneral
    //     seen and re-runs enter_gameplay, which then reaches loading_screen.
    // Asserting only `loading_screen` is reached would not distinguish the two,
    // so both shapes are accepted — but one of them is required, and the
    // loading_screen Enter is required in the tutorial case.
    bool direct = outputContains("Screen 'multiuser_screen' Exit (to 'loading_screen')");
    bool viaTutorial =
        outputContains("Screen 'multiuser_screen' Exit (to 'tutorial_perform_it_screen_0')")
        && outputContains("Screen 'loading_screen' Enter");
    EXPECT_TRUE(direct || viaTutorial)
        << "multiuser_screen never reached loading_screen (directly or through the "
           "first-run move tutorial) — enter_gameplay DTA function didn't fire "
           "from the menu flow";
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

TEST_F(DtaFlowTest, HelpbarHiddenDuringGameplay) {
    // G-7.  On the 360 the "EXIT CONTROLLER MODE / SELECT" helpbar is gone for
    // the whole song: ShellInput::Poll's TheGamePanel arm (0x82902CC0) calls
    // ExitControllerMode(true) whenever the gameplay panel has focus, and
    // HelpBarPanel::Draw shows all.grp only while
    // `(!hide_helpbar && !disabled) || InControllerMode()` -- and the game
    // panel sets hide_helpbar TRUE (ui/gameplay/game_modes.dta).  The default
    // DC3_CONTROLLER_MODE=forced policy pins GestureMgr in controller mode, so
    // the bar used to stay up through gameplay.  HelpBarPanel::Draw logs every
    // change of its decision ("DC3 HelpBar: all.grp showing=N"); the LAST
    // decision once the song is playing must be 0.
    const std::string &out = sResult.output;
    size_t playing = out.find("gameStage=playing");
    ASSERT_NE(playing, std::string::npos)
        << "the flow never reached gameplay (no gameStage=playing sample); "
           "nothing below is meaningful";
    EXPECT_NE(out.find("DC3 ControllerMode: forced: helpbar leaves controller "
                       "mode for gameplay"),
              std::string::npos)
        << "the gameplay-panel exit never took the helpbar out of controller mode";
    const std::string needle = "DC3 HelpBar: all.grp showing=";
    size_t last = out.rfind(needle);
    ASSERT_NE(last, std::string::npos)
        << "HelpBarPanel::Draw never logged a decision -- is the helpbar drawn?";
    size_t gameEnter = out.find("Screen 'game_screen' Enter");
    ASSERT_NE(gameEnter, std::string::npos);
    EXPECT_GT(last, gameEnter)
        << "the helpbar made no draw decision after game_screen was entered, "
           "so whatever it decided before gameplay (shown, in controller mode) "
           "stood for the whole song";
    size_t eol = out.find('\n', last);
    std::string line = out.substr(last, eol == std::string::npos ? std::string::npos
                                                                  : eol - last);
    EXPECT_EQ(line.compare(0, needle.size() + 1, needle + "0"), 0)
        << "the helpbar's last draw decision during gameplay was to SHOW it:\n"
        << line;
}

TEST_F(DtaFlowTest, NoCrashCleanExit) {
    EXPECT_EQ(sResult.signal, 0)
        << "Engine crashed with signal " << sResult.signal;
    EXPECT_FALSE(sResult.timedOut)
        << "Engine did not finish inside its wall-clock cap. This is a hang "
           "OR a machine too loaded to complete a healthy run -- the "
           "long flow costs ~67 s at load 25 and exceeded 120 s with "
           "several lanes building. Re-run on a quiet box before treating it "
           "as a regression; the cap is kEngineWallClockSeconds.";
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
        path += "_" + std::to_string((int)getpid()) + ".log"; // per run: shared by concurrent gates
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

// The native default with no camera: the default policy is `auto`, and with
// no pose provider (headless, no DC3_POSE: the dummy skeleton) auto stays
// forced -- `wake` never presses, controller mode never runs the image's
// bodies (they are the only source of "DC3 ControllerMode:" lines), and the
// same script still navigates.
TEST_F(ControllerModeForcedTest, WakeIsANoOpUnderForcedDefault) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Native: controller mode policy = auto"), std::string::npos)
        << "the default policy is not `auto`";
    EXPECT_EQ(out.find("controller mode policy auto -> faithful"), std::string::npos)
        << "auto went faithful with no pose provider running";
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

// ymca.txt under `faithful` WITHOUT fast boot: its title wake must stay.
//
// The DtaFlowTest run cannot see a lost title wake.  Under `faithful` the game
// boots out of controller mode, so without the wake title_screen's confirm is
// swallowed (it only enters controller mode) -- but DC3_FAST_BOOT auto-advances
// title_screen after 60 frames, which carries the flow on regardless.  Without
// fast boot nothing advances title_screen but a confirm, so a flow that lost
// its title wake strands there (measured: the main_screen wait times out after
// 1801 frames).  choose_mode_screen is reached by frame ~270 with the wakes in
// place; the run stops at 450 frames, since nothing after it is in question.
// The flow is shared with the original game under Xenia
// (tools/fork-regress/flows/dc3-ymca.txt), where every wake is needed.
class YmcaFaithfulNoFastBootTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS")) return;
        sResult = RunDtaFlow(450, 60, "ymca.txt", "DC3_CONTROLLER_MODE=faithful",
                             /*fastBoot=*/false);
        sRanEngine = true;
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult YmcaFaithfulNoFastBootTest::sResult = {};
bool YmcaFaithfulNoFastBootTest::sRanEngine = false;

TEST_F(YmcaFaithfulNoFastBootTest, TitleWakeReachesChooseMode) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Native: controller mode policy = faithful"), std::string::npos)
        << "precondition: DC3_CONTROLLER_MODE=faithful did not reach the engine";
    ASSERT_EQ(out.find("DC3 UI: Fast boot enabled"), std::string::npos)
        << "precondition: fast boot is on, so title_screen auto-advances and this "
           "test cannot see a swallowed title confirm";
    EXPECT_NE(out.find("DC3 Input: wait_screen 'choose_mode_screen' satisfied"),
              std::string::npos)
        << "ymca.txt did not reach choose_mode_screen under faithful without fast "
           "boot.  If it stranded on title_screen, the flow's title `wake` is "
           "missing or no longer lands before the confirm: the confirm was "
           "swallowed into entering controller mode.";
    EXPECT_FALSE(sResult.timedOut) << "the engine hit the 60 s timeout";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// ===========================================================================
// Story: the Perform-tutorial gate reads the persisted global options (G-2)
// ===========================================================================
//
// docs/native/PLAN.md G-2 was filed as "Story re-shows the Perform tutorial on a
// profile that has seen it".  Measured 2026-11-13: the surveyed run (n8-story)
// used a save dir created empty by its own driver, so the console had never
// seen the tutorial and showing it was correct.  The gate is enter_gameplay's
// first clause (ui/global.dta:211-230) -- campaign_intro's gameplay_mode is
// `perform` (campaign's parent_mode) -- and kTutorialGeneral is read from the
// GLOBAL options cache, not from the profile save.  These two runs pin both
// branches of that gate on the story path, so a break in persisting or
// loading mTutorialsSeen (or a gate that always/never fires) turns one red.

class StoryTutorialGateTest : public ControllerModeFlowTest {
protected:
    static void RunOnce(DtaRunResult &result, bool &ran, int seed) {
        if (!getenv("DC3_DTA_FLOW_TESTS") || ran) return;
        result = RunDtaFlow(5000, 180, "story-tutorial-gate.txt", "", true, seed);
        ran = true;
    }
    static constexpr const char *kToLoading =
        "Screen 'campaign_diffselect_screen' Exit (to 'loading_screen')";
    static constexpr const char *kToTutorial =
        "Screen 'campaign_diffselect_screen' Exit (to 'tutorial_perform_it_screen_0')";
};

class StoryTutorialGateSeenTest : public StoryTutorialGateTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() { RunOnce(sResult, sRanEngine, kTutorialGeneral); }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult StoryTutorialGateSeenTest::sResult = {};
bool StoryTutorialGateSeenTest::sRanEngine = false;

class StoryTutorialGateFreshTest : public StoryTutorialGateTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() { RunOnce(sResult, sRanEngine, -1); }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult StoryTutorialGateFreshTest::sResult = {};
bool StoryTutorialGateFreshTest::sRanEngine = false;

TEST_F(StoryTutorialGateSeenTest, SavedConsoleSkipsPerformTutorial) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Input: wait_screen 'campaign_diffselect_screen' satisfied"),
              std::string::npos)
        << "precondition: story-tutorial-gate.txt never reached campaign_diffselect_screen";
    EXPECT_NE(out.find(kToLoading), std::string::npos)
        << "with kTutorialGeneral recorded in the global options cache, confirming "
           "a difficulty must go straight to loading_screen";
    EXPECT_EQ(out.find("Screen 'tutorial_perform_it_screen_0' Enter"), std::string::npos)
        << "the Perform tutorial was shown on a console whose global options say it "
           "was seen: mTutorialsSeen was not loaded from DC3_CACHE_DIR/global/global "
           "(ProfileMgr::LoadGlobalOptions / SaveLoadManager state 0x32)";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// PLAN G-22: the story profile chooser listed "Sign Into a Gamer Profile"
// under the local profile.  On a 360 that row opens the guide's sign-in blade
// (ChooseProfilePanel show_signin -> PlatformMgr::SignInUsers ->
// XShowSigninUI); a PC has none, so selecting it did nothing.  Natively the
// row is offered only when no profile is signed in, so with the local
// profile signed in the list is that one row.
TEST_F(StoryTutorialGateSeenTest, ChooseProfileOffersNoDeadSignInRow) {
    std::vector<std::string> lines =
        LinesFrom(sResult.output, "Screen 'campaign_choose_profile_screen' Enter");
    ASSERT_FALSE(lines.empty())
        << "precondition: story-tutorial-gate.txt never reached campaign_choose_profile_screen";
    int numData = -1;
    for (const std::string &line : lines) {
        if (!StartsWith(line, "DC3 HamNavList: select name='right_hand.hnl'")) continue;
        size_t at = line.find("numData=");
        if (at != std::string::npos) numData = atoi(line.c_str() + at + strlen("numData="));
        break;
    }
    ASSERT_NE(numData, -1) << "no right_hand.hnl select on campaign_choose_profile_screen";
    EXPECT_EQ(numData, 1)
        << "the profile list should be the local profile alone; 2 = the "
           "'Sign Into a Gamer Profile' row is back, and on a PC it does nothing";
    EXPECT_NE(sResult.output.find("Screen 'campaign_choose_profile_screen' Exit (to "
                                  "'campaign_diffselect_screen')"),
              std::string::npos)
        << "selecting the local profile did not proceed to difficulty select";
}

TEST_F(StoryTutorialGateFreshTest, FreshConsoleShowsPerformTutorial) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Input: wait_screen 'campaign_diffselect_screen' satisfied"),
              std::string::npos)
        << "precondition: story-tutorial-gate.txt never reached campaign_diffselect_screen";
    EXPECT_NE(out.find(kToTutorial), std::string::npos)
        << "on a console that has never seen it, story's first song must insert "
           "tutorial_perform_it_screen_0 (ui/global.dta:230)";
    EXPECT_EQ(out.find(kToLoading), std::string::npos)
        << "the tutorial gate was skipped on a fresh console";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// ===========================================================================
// Fitness: a fitness song starts from the `playlists` row (G-4)
// ===========================================================================
//
// docs/native/PLAN.md G-4: "confirm on the first fitness_screen item only
// toggles fitness_option".  That is the image's design, not a bug:
// FitnessProvider::UpdateList (100% match) builds fitness_option /
// fitness_set_fitness_goals / fitness_body_profile / playlists, and only
// `playlists` leads to songs (ui/fitness/fitness.dta NAV_SELECT_MSG ->
// song_select_playlist_tutorial_check_screen).  fitness-playlist.txt toggles
// fitness mode on, takes row 3, picks a playlist and `playlist_play`s it into
// multiuser_screen under gamemode playlist_perform.  Seeded with the fitness
// and playlist tutorials seen so neither is inserted.

class FitnessPlaylistFlowTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS") || sRanEngine) return;
        sResult = RunDtaFlow(5000, 180, "fitness-playlist.txt", "", true,
                             kTutorialGeneral | kTutorialFitness | kTutorialPlaylists);
        sRanEngine = true;
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult FitnessPlaylistFlowTest::sResult = {};
bool FitnessPlaylistFlowTest::sRanEngine = false;

TEST_F(FitnessPlaylistFlowTest, PlaylistsRowStartsAPlaylist) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Input: wait_screen 'fitness_screen' satisfied"), std::string::npos)
        << "precondition: fitness-playlist.txt never reached fitness_screen";
    EXPECT_NE(out.find("sym='fitness_option'"), std::string::npos)
        << "row 0 of fitness_screen was not fitness_option";
    EXPECT_NE(out.find("Screen 'fitness_screen' Exit (to "
                       "'song_select_playlist_tutorial_check_screen')"),
              std::string::npos)
        << "fitness_screen row 3 did not lead to the playlist screens: the "
           "`playlists` row is FitnessProvider's fourth entry";
    EXPECT_NE(out.find("sym='playlist_play'"), std::string::npos)
        << "the playlist options list never offered playlist_play";
    EXPECT_NE(out.find("Screen 'song_select_playlist_screen' Exit (to 'multiuser_screen')"),
              std::string::npos)
        << "playlist_play did not reach the playlist_perform seldiff screen";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// ===========================================================================
// The forced help bar survives a song (PLAN G-19)
// ===========================================================================
//
// Under DC3_CONTROLLER_MODE=forced (the default with no camera) the helpbar
// leaves controller mode for the song and comes back when focus leaves the
// gameplay panel (G-7, HelpBarPanel.cpp NativeForcedHelpbar*).  Leaving the
// results, perform_complete_screen hands focus back to game_screen for the
// frames of its transition to meta_loading_continue_screen; the helpbar used
// to leave controller mode again there and be put straight back while its
// exit_controller_mode_immediate.flow was still running, so the exit won and
// song select came back gesture-styled (LEFT HAND / MODE SELECT) after every
// song.  The forced exit now waits out a transition.

// song_select_screen came back at frame ~42,300 (MILO_NORENDER, measured);
// part of the route is wall-clock transitions, so the frame count moves with
// the box's speed -- leave a wide margin (~25 s at NORENDER rates).
static constexpr int kSongToSongSelectFrames = 100000;

class ForcedHelpbarAfterSongTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS") || sRanEngine) return;
        sResult = RunDtaFlow(kSongToSongSelectFrames, 180, "song-to-song-select.txt",
                             "MILO_NORENDER=1 DC3_FAST_TIME=1 DC3_CONTROLLER_MODE=forced",
                             true, kTutorialGeneral);
        sRanEngine = true;
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult ForcedHelpbarAfterSongTest::sResult = {};
bool ForcedHelpbarAfterSongTest::sRanEngine = false;

TEST_F(ForcedHelpbarAfterSongTest, HelpbarLeavesControllerModeOncePerSong) {
    const std::string &out = sResult.output;
    const char *kLeave = "DC3 ControllerMode: forced: helpbar leaves controller mode for gameplay";
    size_t complete = out.find("Screen 'perform_complete_screen' Enter");
    size_t songSelect = out.find("Screen 'song_select_screen' Enter", complete);
    ASSERT_NE(complete, std::string::npos)
        << "precondition: song-to-song-select.txt never reached perform_complete_screen";
    ASSERT_NE(songSelect, std::string::npos)
        << "precondition: the flow never returned to song_select_screen after the song";
    size_t leaves = 0;
    for (size_t at = out.find(kLeave); at != std::string::npos; at = out.find(kLeave, at + 1))
        leaves++;
    EXPECT_EQ(leaves, 1u) << "the helpbar should leave controller mode once, for the song";
    size_t spurious = out.find(kLeave, complete);
    EXPECT_TRUE(spurious == std::string::npos || spurious > songSelect)
        << "the helpbar left controller mode again on the way from the results to "
           "song select; put straight back while its exit flow is still running, it "
           "comes back gesture-styled";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// ===========================================================================
// Memory is flat song over song (PLAN H-24)
// ===========================================================================
//
// three-songs.txt plays three different Perform songs in one process and
// returns to song select after each.  DC3_MEM_LOG prints one line per screen
// Enter (src/system/obj/Object.cpp NativeMemReport):
//     DC3 MEM song_select_screen rss=..MB inuse=..MB heldfree=..MB ... objects=N
// The song_select_screen lines are the samples: index 0 is before the first
// song, 1..3 after each song.  Song select is the same screen with the same
// content each time, so whatever is live there and was not before is what a
// song left behind.
//
// MEASURED 2026-11-14, MILO_NORENDER + DC3_FAST_TIME, seven distinct songs,
// box load ~105 on 32 threads (memory numbers do not depend on load):
//
//   song select after song   0      1      2      3      4      5      6
//   fixed:   rss MB         354    310    312    312    313    314    313
//            in use MB    281.2  255.6  255.9  255.9  256.0  256.0  259.1
//            objects      27346  26102  26150  26153  26154  26154  26154
//   no trim (DC3_MEM_TRIM=0):
//            rss MB         354    481    506    506    526    526    523
//
// Two causes, two assertions:
//  * A leak: ~HamNavList's native cascade branch dropped its list widgets
//    (dir-less copies, UIListDir::CreateElements) without deleting them: +~450
//    Hmx::Objects and ~1 MB per song select visit, linear.  After the fix the
//    count settles (+48 after song 2, then +3, +1, 0, 0).  Bound: 150 objects
//    over songs 2-3, three times the one-off step seen, a third of one visit's
//    leak.
//  * glibc keeps freed heap resident.  A song's working set (in use ~420-460
//    MB at the results screen) is freed when the song unloads -- in use comes
//    back to within ~1 MB -- but glibc gives almost none of it back, so rss
//    stays at the high-water of the biggest song so far: +127 MB after song 1,
//    +25 more after song 2, plateauing near +170 MB.  That is the "~90 MB a
//    song" of the soak (H-24).  NativeReleaseFreeHeap (malloc_trim at a
//    screen Enter) returns it.  Bounds: rss at every post-song song select
//    within 25 MB of the pre-song one (fixed: -44 to -40; no trim: +127 and
//    up), and rss growth over songs 2-3 under 10 MB (fixed: +2; no trim: +25).
//    In use over songs 2-3 under 4 MB (fixed: +0.3).
//
// Wall clock: ~70 s a song plus boot at load ~105 (MEASURED); 600 s is a hang
// guard.  The frame cap is a runaway guard too (~42k frames a song).
static constexpr int kThreeSongsFrames = 300000;

struct MemSample {
    double rss, inUse;
    long objects;
};

static std::vector<MemSample> MemSamplesAt(const std::string &out, const char *screen) {
    std::vector<MemSample> samples;
    std::string prefix = std::string("DC3 MEM ") + screen + " rss=";
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        if (!StartsWith(line, prefix.c_str())) continue;
        MemSample s = {-1, -1, -1};
        size_t at;
        if ((at = line.find(" rss=")) != std::string::npos) s.rss = atof(line.c_str() + at + 5);
        if ((at = line.find(" inuse=")) != std::string::npos) s.inUse = atof(line.c_str() + at + 7);
        if ((at = line.find(" objects=")) != std::string::npos)
            s.objects = atol(line.c_str() + at + 9);
        samples.push_back(s);
    }
    return samples;
}

class MemoryFlatSongOverSongTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS") || sRanEngine) return;
        sResult = RunDtaFlow(kThreeSongsFrames, 600, "three-songs.txt",
                             "MILO_NORENDER=1 DC3_FAST_TIME=1 DC3_CONTROLLER_MODE=forced "
                             "DC3_MEM_LOG=1",
                             true, kTutorialGeneral);
        sRanEngine = true;
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
    static std::string Table(const std::vector<MemSample> &s) {
        std::ostringstream t;
        for (size_t i = 0; i < s.size(); i++)
            t << "\n  song select " << i << ": rss=" << s[i].rss << "MB inuse=" << s[i].inUse
              << "MB objects=" << s[i].objects;
        return t.str();
    }
};
DtaRunResult MemoryFlatSongOverSongTest::sResult = {};
bool MemoryFlatSongOverSongTest::sRanEngine = false;

TEST_F(MemoryFlatSongOverSongTest, ObjectsAndHeapDoNotGrowAfterTheFirstSong) {
    std::vector<MemSample> s = MemSamplesAt(sResult.output, "song_select_screen");
    ASSERT_GE(s.size(), 4u) << "precondition: three-songs.txt should reach song select "
                               "four times (before, and after each of three songs)"
                            << Table(s);
    for (const MemSample &m : s)
        ASSERT_TRUE(m.rss > 0 && m.inUse > 0 && m.objects > 0)
            << "unparseable DC3 MEM line" << Table(s);
    printf("song select samples:%s\n", Table(s).c_str());
    EXPECT_LE(s[3].objects - s[1].objects, 150)
        << "Hmx::Objects left behind by songs 2 and 3: something a song or song "
           "select creates is not destroyed (the ~HamNavList widget leak was ~450 a "
           "visit).  DC3_MEM_CENSUS=1 prints the classes that moved."
        << Table(s);
    EXPECT_LE(s[3].inUse - s[1].inUse, 4.0)
        << "heap in use grew over songs 2 and 3" << Table(s);
    EXPECT_LE(s[3].rss - s[1].rss, 10.0)
        << "resident memory grew over songs 2 and 3" << Table(s);
    for (size_t i = 1; i < 4; i++)
        EXPECT_LE(s[i].rss - s[0].rss, 25.0)
            << "song select after song " << i << " is not back near its pre-song "
            << "resident size: the freed song is still resident (malloc_trim at "
               "screen Enter, NativeReleaseFreeHeap, not running?)"
            << Table(s);
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// ===========================================================================
// Options > Gameplay and the Kinect Tuner (PLAN G-12, G-16)
// ===========================================================================
//
// options-gameplay.txt enters options_gameplay_screen twice, selecting one
// checkbox row each time, then selects A/V > Kinect Tuner.  native's
// {speech_mgr is_speech_supportable} is 0, so the panel shows
// provider_no_voice.hnp: its four rows, plus -- in the dev profile, the
// default (DC3_PROFILE unset; it sets DC3_DEV_OPTIONS=1) -- the native
// overlay's two (option_camera_blend, option_debug_panel) = 6.  This suite runs
// with nothing set, so it pins the DEFAULT and keeps exercising the overlay's
// append path; OptionsGameplayPlayerFlowTest below runs the same flow under
// DC3_PROFILE=release and requires the retail four (G-20).  The developer rows
// are appended after the retail four, so options-gameplay.txt's row walk is the
// same under both.
//
// G-12: the overlay counted rows with `num_data`, a message HamNavProvider has
// never handled (Handle is 100% matched; the image has no such handler), so
// every append ran against kDataUnhandled: 14 MILO_FAILs per visit.  The two
// rows must be there on both visits.  (options_gameplay.milo is reloaded per
// visit -- the providers are new objects each time -- so an unconditional
// append cannot double them; the second visit pins that.)
// G-16: the Kinect Tuner row did nothing; it must now say why.

class OptionsGameplayFlowTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS")) return;
        sResult = RunDtaFlow(1500, 180, "options-gameplay.txt",
                             "env -u DC3_DEV_OPTIONS -u DC3_PROFILE");
        sRanEngine = true;
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }

public:
    // The lines from the first options_gameplay_screen Enter up to (not
    // including) the options_av_screen Enter.
    static std::vector<std::string> GameplayLines(const DtaRunResult &result = sResult) {
        std::vector<std::string> out;
        for (const std::string &line :
             LinesFrom(result.output, "Screen 'options_gameplay_screen' Enter")) {
            if (line.find("Screen 'options_av_screen' Enter") != std::string::npos) break;
            out.push_back(line);
        }
        return out;
    }

    // The right-hand list's row count at each checkbox select (one per visit):
    // HamNavList logs numData with every select.
    static std::vector<int> RowCountsPerVisit(const std::vector<std::string> &lines) {
        std::vector<int> counts;
        for (const std::string &line : lines) {
            if (!StartsWith(line, "DC3 HamNavList: select name='right_hand.hnl'")) continue;
            if (line.find("sym='option_photo'") == std::string::npos &&
                line.find("sym='option_freestyle'") == std::string::npos)
                continue;
            size_t at = line.find("numData=");
            if (at == std::string::npos) continue;
            counts.push_back(atoi(line.c_str() + at + strlen("numData=")));
        }
        return counts;
    }
};
DtaRunResult OptionsGameplayFlowTest::sResult = {};
bool OptionsGameplayFlowTest::sRanEngine = false;

TEST_F(OptionsGameplayFlowTest, GameplayOptionsRaiseNoFails) {
    std::vector<std::string> lines = GameplayLines();
    ASSERT_FALSE(lines.empty())
        << "precondition: options-gameplay.txt never reached options_gameplay_screen";
    int enters = 0, fails = 0;
    std::string firstBad;
    for (const std::string &line : lines) {
        if (line.find("Screen 'options_gameplay_screen' Enter") != std::string::npos) enters++;
        bool bad = StartsWith(line, "FAIL:") ||
                   line.find("unhandled msg: num_data") != std::string::npos ||
                   line.find("null array for key nav_items") != std::string::npos;
        if (bad) {
            fails++;
            if (firstBad.empty()) firstBad = line;
        }
    }
    EXPECT_EQ(enters, 2) << "the flow should enter options_gameplay_screen twice";
    EXPECT_EQ(fails, 0) << "options_gameplay raised " << fails
                        << " failures; first: " << firstBad;
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

TEST_F(OptionsGameplayFlowTest, OverlayAddsItsTwoRowsEachVisit) {
    std::vector<std::string> lines = GameplayLines();
    ASSERT_FALSE(lines.empty())
        << "precondition: options-gameplay.txt never reached options_gameplay_screen";
    std::vector<int> counts = RowCountsPerVisit(lines);
    ASSERT_EQ(counts.size(), 2u)
        << "expected one checkbox select per visit (photo or freestyle)";
    EXPECT_EQ(counts[0], 6)
        << "first visit: provider_no_voice.hnp's 4 rows + camera blend + debug overlay "
           "(4 = the dev profile, the default, no longer shows the developer rows)";
    EXPECT_EQ(counts[1], 6)
        << "second visit: 6 rows again (fewer: an append was lost; more: the "
           "provider outlived the first visit and the rows doubled)";
}

// G-20: a player (DC3_PROFILE=release, the future player build) sees the
// retail rows only, as on a 360: provider_no_voice.hnp's option_photo,
// option_freestyle, autosave, option_choosestorage.  Before G-20 every run
// showed the two developer rows (camera blend, debug overlay) too; since the
// dev profile became the default only release hides them.
class OptionsGameplayPlayerFlowTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS") || sRanEngine) return;
        sResult = RunDtaFlow(1500, 180, "options-gameplay.txt",
                             "env -u DC3_DEV_OPTIONS DC3_PROFILE=release");
        sRanEngine = true;
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult OptionsGameplayPlayerFlowTest::sResult = {};
bool OptionsGameplayPlayerFlowTest::sRanEngine = false;

TEST_F(OptionsGameplayPlayerFlowTest, PlayerSeesTheRetailRowsOnly) {
    std::vector<std::string> lines = OptionsGameplayFlowTest::GameplayLines(sResult);
    ASSERT_FALSE(lines.empty())
        << "precondition: options-gameplay.txt never reached options_gameplay_screen";
    std::vector<int> counts = OptionsGameplayFlowTest::RowCountsPerVisit(lines);
    ASSERT_EQ(counts.size(), 2u)
        << "expected one checkbox select per visit (photo or freestyle)";
    EXPECT_EQ(counts[0], 4) << "first visit: provider_no_voice.hnp's 4 retail rows only "
                               "(6 = the developer rows are showing to a player)";
    EXPECT_EQ(counts[1], 4) << "second visit: the retail 4 again";
    int fails = 0;
    for (const std::string &line : lines)
        if (StartsWith(line, "FAIL:")) fails++;
    EXPECT_EQ(fails, 0) << "Options > Gameplay raised MILO_FAILs without the developer rows";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

TEST_F(OptionsGameplayFlowTest, KinectTunerGivesFeedback) {
    const std::string &out = sResult.output;
    size_t sel = out.find("sym='natal_troubleshooter'");
    ASSERT_NE(sel, std::string::npos)
        << "precondition: the flow never selected A/V > Kinect Tuner";
    EXPECT_NE(out.find("DC3 NOTICE [kinect_tuner]", sel), std::string::npos)
        << "selecting the Kinect Tuner produced no notice: either "
           "show_controller_required never answered controller_req_op_complete, "
           "or XShowNuiTroubleshooterUI is silent";
}

// ===========================================================================
// The largest song preloads and reaches gameplay (song preload budget)
// ===========================================================================
//
// Every other flow here plays ymca or Better Off Alone, whose preload sets
// (barks + song milo + midi) are the smallest.  omg has the LARGEST set:
// 25.20 MiB as the web build reads it (the inflated extraction), which is why
// loading.dta's 16 MiB `max_cache_size` sent it -- and 33 other songs -- back
// to the title screen on web until PreloadPanel::SetTypeDef scaled it
// (185b6f50e).  omg.txt picks it (51 downs: the last row of song select) on a
// console that has seen the move tutorial, so the run goes straight from
// multiuser_screen to loading.
//
// This is the end-to-end half; native/tests/test_preload_budget.cpp is the
// per-song half.  What it can and cannot catch, measured 2026-11-13: desktop
// reads the ark, where omg's set is ~10 MiB (compressed, the 360's own bytes),
// so with the x4 removed this flow still PASSES on desktop -- the bounce is
// web-only, and PreloadBudgetTest.WebReadsInflatedFiles is the guard that
// fails for it.  This flow guards the rest of the chain on the biggest song:
// it is selected, preloads, loads the biggest world+song set, and plays.

class LargestSongFlowTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static bool sRanEngine;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS") || sRanEngine) return;
        // game_screen at ~frame 1700, gameStage=playing by ~2100 on an idle
        // box (headless, no GPU).  The load and the song intro wait on the
        // wall clock, not on frames, so the frame count they take grows with
        // box load AND with how cheap a frame is: after the pipeline cache
        // (np-cpu-perf) a loaded gate reached game_screen at 2049 and was
        // still in the intro at 3000, in three trees.  The run always goes to
        // the frame cap (omg.txt does not quit), so 8000 costs an idle box a
        // few more seconds; 300 s is the wall-clock bound.
        sResult = RunDtaFlow(8000, 300, "omg.txt", "", true, kTutorialGeneral);
        sRanEngine = true;
    }
    void SetUp() override { CheckRan(sResult, sRanEngine); }
    void TearDown() override {
        if (HasFailure() && sRanEngine) Dump(sResult);
    }
};
DtaRunResult LargestSongFlowTest::sResult = {};
bool LargestSongFlowTest::sRanEngine = false;

TEST_F(LargestSongFlowTest, OmgPreloadsAndReachesGameplay) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("DC3 Input: wait_screen 'song_select_screen' satisfied"),
              std::string::npos)
        << "precondition: omg.txt never reached song_select_screen";
    ASSERT_NE(out.find("sym='omg'"), std::string::npos)
        << "the confirm on song select did not pick omg -- the song list changed; "
           "recount the downs in scripts/dc3-input-flows/omg.txt";
    EXPECT_EQ(out.find("Could not cache"), std::string::npos)
        << "PreloadPanel::CheckFileCached failed: the preload cache evicted part of "
           "omg's set (loading.dta max_cache_size vs the bytes this build reads)";
    EXPECT_NE(out.find("Screen 'real_loading_screen' Enter (from 'preloading_screen')"),
              std::string::npos)
        << "preloading_screen did not go on to real_loading_screen (on_preload_ok)";
    EXPECT_EQ(out.find("Screen 'title_screen' Enter (from 'meta_loading"), std::string::npos)
        << "the preload failed and data_error sent the game back to the title screen";
    EXPECT_NE(out.find("Screen 'game_screen' Enter (from 'real_loading_screen')"),
              std::string::npos)
        << "omg never reached game_screen";
    EXPECT_NE(out.find("gameStage=playing"), std::string::npos)
        << "omg reached game_screen but never started playing";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// DLC: Milkshake from the Dance Central 2 DLC megapack, mounted from a package
// directory (scripts/dlc/import.sh) through DC3_DLC_DIR.  It is selected,
// preloaded from the package's cntNNNNNNNN: drive, and played.  The crash this
// guards: a song milo's `../../world/shared/director.milo` subdir must resolve
// to the disc's director (world_objects.dta `change_subdir`, which keys on a
// drive in the path) -- resolved inside the package it fails to load,
// song.anim's keys lose their HamDirector target, and game_screen dies in
// HamDirector::SetupRoutineBuilderAnims.
//
// The package is game content and never committed, so these tests exist only
// in a build whose configure FOUND it: the DC3_DLC_TESTS coverage gate
// (native/CMakeLists.txt) compiles them in and hands the package root over as
// DC3_TEST_DLC_DIR.  A box without the package does not register them at all,
// so its skip count is the same as a box with it.  Registered but absent is a
// stale configure, and FAILS (TestGates catches the other direction).
#if DC3_DLC_TESTS
static std::string GetDlcDir() {
    const char *env = getenv("DC3_TEST_DLC_DIR");
    if (env && *env) return env;
    return GetBinaryDir() + "/../../orig-assets/dlc";
}
static constexpr const char *kMilkshakePackage = "dancecentral2dlcmegapack";
static constexpr int kDlcFlowFrames = 5000;

class DlcSongFlowTest : public ControllerModeFlowTest {
protected:
    static DtaRunResult sResult;
    static DtaRunResult sWarm; // second boot on the same cache and save dirs
    static bool sRanEngine;
    static bool sRanWarm;
    static std::string sEnv;
    static bool HavePackage() {
        return FileExists(GetDlcDir() + "/" + kMilkshakePackage +
                          "/songs/milkshake/milkshake.mogg");
    }
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS") || sRanEngine || !HavePackage()) return;
        // DC3_TAG_MODALS prints the "Could not load:" notify a subdir that
        // resolved into the package raises.  game_screen ~frame 2000 and
        // gameStage=playing ~2500 (headless, no GPU) on an idle box -- but the
        // song intro waits on asynchronous loading, so under gate load it costs
        // ~3x the frames: a 3600-frame cap was reached at frame 3598, one frame
        // short of playing.  kDlcFlowFrames leaves room; the wall clock bounds it.
        // A root holding only this package, so other packages imported next
        // to it cannot move Milkshake in the song list (the flow counts downs).
        char tmpl[] = "/tmp/dc3-dlc-root-XXXXXX";
        const char *root = mkdtemp(tmpl);
        std::string pkg = GetDlcDir() + "/" + kMilkshakePackage;
        if (!root || symlink(pkg.c_str(), (std::string(root) + "/" + kMilkshakePackage).c_str())) {
            sResult.setupError = "could not make a one-package DLC root for " + pkg;
            sRanEngine = true;
            return;
        }
        sEnv = std::string("DC3_TAG_MODALS=1 DC3_DLC_DIR=") + root;
        sResult = RunDtaFlow(kDlcFlowFrames, 240, "milkshake.txt", sEnv.c_str(), true, kTutorialGeneral);
        sRanEngine = true;
    }
    // The first boot writes the song cache (SongMgr's "songcache" container,
    // which only content songs enter); a second boot on the same root reads
    // it.  Run only by the test that needs it: ctest runs each test in its
    // own process, so a suite-level second boot would be paid twice.
    static void EnsureWarmBoot() {
        if (sRanWarm || !sResult.setupError.empty()) return;
        sWarm = RunDtaFlow(kDlcFlowFrames, 240, "milkshake.txt", sEnv.c_str(), true, kTutorialGeneral,
                           sResult.persistenceRoot);
        sRanWarm = true;
    }
    void SetUp() override {
        ASSERT_TRUE(HavePackage())
            << "DC3_DLC_TESTS is compiled in but the package is gone: " << GetDlcDir() << "/"
            << kMilkshakePackage << " -- reconfigure the build (the gate is decided at "
            << "configure time), or re-import it with scripts/dlc/import.sh <package>";
        CheckRan(sResult, sRanEngine);
    }
    void TearDown() override {
        if (HasFailure() && sRanEngine) {
            Dump(sResult);
            if (sRanWarm) Dump(sWarm);
        }
    }
};
DtaRunResult DlcSongFlowTest::sResult = {};
DtaRunResult DlcSongFlowTest::sWarm = {};
bool DlcSongFlowTest::sRanEngine = false;
bool DlcSongFlowTest::sRanWarm = false;
std::string DlcSongFlowTest::sEnv;

TEST_F(DlcSongFlowTest, MilkshakeMountsAndReachesGameplay) {
    const std::string &out = sResult.output;
    EXPECT_NE(out.find(std::string("DLC package '") + kMilkshakePackage + "' at cnt"),
              std::string::npos)
        << "ContentMgr_Native did not mount the package on a cntNNNNNNNN: drive";
    ASSERT_NE(out.find("DC3 Input: wait_screen 'song_select_screen' satisfied"),
              std::string::npos)
        << "precondition: milkshake.txt never reached song_select_screen";
    ASSERT_NE(out.find("sym='milkshake'"), std::string::npos)
        << "the confirm on song select did not pick milkshake -- the song list changed; "
           "recount the downs in scripts/dc3-input-flows/milkshake.txt";
    EXPECT_NE(out.find("cnt00000000:/songs/milkshake/gen/milkshake.milo_xbox"),
              std::string::npos)
        << "the song milo was not read from the package's drive";
    for (const char *subdir : {"director.milo", "audio_mixer.milo"}) {
        size_t at = 0;
        bool failed = false;
        while ((at = out.find("Could not load: ", at)) != std::string::npos) {
            size_t eol = out.find('\n', at);
            if (out.substr(at, eol - at).find(subdir) != std::string::npos) failed = true;
            at = eol;
        }
        EXPECT_FALSE(failed) << subdir
                             << " was resolved inside the package instead of on the disc "
                                "(world_objects.dta change_subdir needs a drive in the path)";
    }
    EXPECT_NE(out.find("Screen 'game_screen' Enter (from 'real_loading_screen')"),
              std::string::npos)
        << "milkshake never reached game_screen";
    EXPECT_NE(out.find("gameStage=playing"), std::string::npos)
        << "milkshake reached game_screen but never started playing";
    EXPECT_EQ(sResult.signal, 0) << "Engine crashed with signal " << sResult.signal;
}

// The second boot finds the package in the song cache and plays it again.
// DataArraySongInfo::sSaveVer was 1 where the image has 2, so Save wrote a
// rev-1 record that Load then read with the rev < 2 fields Save never writes:
// the next boot overran a 512-byte Symbol buffer and crashed at
// wait_main_after_saveload_screen.
TEST_F(DlcSongFlowTest, MilkshakePlaysAgainFromTheSongCache) {
    ASSERT_NE(sResult.output.find("gameStage=playing"), std::string::npos)
        << "precondition: the first boot did not play Milkshake";
    EnsureWarmBoot();
    const std::string &out = sWarm.output;
    ASSERT_TRUE(sWarm.setupError.empty()) << sWarm.setupError;
    size_t mount = out.find(std::string("DLC package '") + kMilkshakePackage + "' at cnt");
    ASSERT_NE(mount, std::string::npos);
    EXPECT_NE(out.substr(mount, out.find('\n', mount) - mount).find("(cached)"),
              std::string::npos)
        << "the second boot did not take the package from the song cache";
    EXPECT_EQ(out.find("String chars"), std::string::npos)
        << "the song cache did not read back what it wrote";
    EXPECT_NE(out.find("sym='milkshake'"), std::string::npos);
    EXPECT_NE(out.find("Screen 'game_screen' Enter (from 'real_loading_screen')"),
              std::string::npos);
    EXPECT_NE(out.find("gameStage=playing"), std::string::npos);
    EXPECT_EQ(sWarm.signal, 0) << "Engine crashed with signal " << sWarm.signal;
}
#endif // DC3_DLC_TESTS

// PLAN H-11: the loading screen plays its loading music.  The image finds the
// era's .mid only in the persistent file cache App::App builds at boot
// (config/persistent_filecache.dta); without it native loaded every song in
// silence ("loading music MIDI not found", 45 of 45 songs).  The proof it
// played is the stream's own clock at LoadingPanel::Unload (HX_NATIVE trace):
// a headless run has no audio device to listen to.
TEST_F(LargestSongFlowTest, LoadingMusicStreams) {
    const std::string &out = sResult.output;
    ASSERT_NE(out.find("Screen 'loading_screen' Enter"), std::string::npos)
        << "precondition: the run never reached loading_screen";
    EXPECT_EQ(out.find("MIDI not found"), std::string::npos);
    EXPECT_EQ(out.find("LoadingPanel.cpp Line:"), std::string::npos)
        << "a LoadingPanel MILO_ASSERT fired (the .mid lookup, IsReady at Enter, or "
           "the stream not playing in Poll)";
    EXPECT_NE(out.find("making file from cache file sfx/samples/shell/loadingmusic_"),
              std::string::npos)
        << "the loading-music .mid was not served from the persistent file cache";
    const std::string tag = "LoadingPanel: loading music sfx/samples/shell/loadingmusic_";
    size_t at = out.find(tag);
    ASSERT_NE(at, std::string::npos) << "no LoadingPanel::Unload loading-music trace";
    size_t streamed = out.find(" streamed ", at);
    ASSERT_NE(streamed, std::string::npos);
    const double ms = atof(out.c_str() + streamed + strlen(" streamed "));
    printf("[loading-music] %s\n", out.substr(at, out.find('\n', at) - at).c_str());
    EXPECT_GT(ms, 100.0) << "the loading music stream did not advance while loading";
}
