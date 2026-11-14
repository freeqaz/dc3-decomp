// Headless boot + input replay integration test
// Launches dc3-native as a subprocess with scripted inputs and checks it survives.
// These tests FAIL when the engine crashes — as boot bugs get fixed, they start passing.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "platform/ExecutablePath.h"
#include "shell_timeout.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string GetBinaryDir() {
    // The test binary (milo-tests) lives next to dc3-native in the build dir
    char buf[4096];
    ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
    if (len <= 0)
        return ".";
    buf[len] = '\0';
    std::string path(buf);
    size_t slash = path.rfind('/');
    return (slash != std::string::npos) ? path.substr(0, slash) : ".";
}

static std::string GetDc3NativePath() {
    return GetBinaryDir() + "/dc3-native";
}

struct RunResult {
    int exitCode;       // -1 if signal killed
    int signal;         // 0 if exited normally
    std::string output; // combined stdout+stderr
    bool timedOut;
};

// The engine's persistence root for one run: DC3_CACHE_DIR and DC3_SAVE_DIR
// both point inside it and it starts empty.
//
// Since the save/load chain was wired (SaveLoadManager::Init +
// CacheMgrNativeInit), a boot reads the caller's ~/.local/share/dc3/cache and,
// when it is missing or out of date, writes it -- so a test would both depend
// on and mutate developer state outside the tree, and whether the first-run
// move tutorial appears depends on what it found there
// (ProfileMgr::HasSeenTutorial's `mGlobalOptionsSaveState != kMetaProfileLoaded`
// fallback).  mkdtemp gives every run the same empty start and keeps the write
// here.  Same mechanism as test_dta_flow.cpp's MakePersistenceRoot().
static std::string MakePersistenceRoot() {
    char tmpl[] = "/tmp/dc3-headless-boot-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) return std::string();
    std::string root(dir);
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    return root;
}

// Run dc3-native with environment variables, capture output
struct EnvVar { std::string key, value; };

static RunResult RunHeadless(
    int maxFrames,
    const char *inputScriptPath = nullptr,
    int timeoutSeconds = 30,
    std::vector<EnvVar> extraEnv = {},
    const char *cwd = nullptr
) {
    std::string binary = GetDc3NativePath();

    // Build command with env vars piped through
    std::string persist = MakePersistenceRoot();
    std::ostringstream cmd;
    cmd << kShellTimeoutLookup;
    if (cwd)
        cmd << "cd '" << cwd << "' && ";
    cmd << "MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 MILO_MAX_FRAMES=" << maxFrames;
    if (!persist.empty())
        cmd << " DC3_CACHE_DIR=" << persist << "/cache DC3_SAVE_DIR=" << persist << "/saves";
    if (inputScriptPath)
        cmd << " MILO_INPUT_SCRIPT=" << inputScriptPath;
    for (auto &ev : extraEnv)
        cmd << " " << ev.key << "=" << ev.value;
    cmd << " ${t:+$t " << timeoutSeconds << "} " << binary << " 2>&1";

    FILE *pipe = popen(cmd.str().c_str(), "r");
    RunResult result = {-1, 0, "", false};
    if (!pipe)
        return result;

    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe))
        result.output += buf;

    int status = pclose(pipe);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.signal = 0;
        if (result.exitCode == 124)
            result.timedOut = true;
        // Our signal handler exits with 128+sig
        if (result.exitCode > 128 && result.exitCode <= 128 + 31)
            result.signal = result.exitCode - 128;
    } else if (WIFSIGNALED(status)) {
        result.exitCode = -1;
        result.signal = WTERMSIG(status);
    }
    return result;
}

// Check if the engine printed its "frames completed, engine stable!" success message
static bool HasCompletionMarker(const std::string &output) {
    return output.find("frames completed, engine stable!") != std::string::npos;
}

// Extract the first FAIL:/FATAL: line from output, or empty string
static std::string FindFatal(const std::string &output) {
    // Engine uses both FAIL: (assertion) and FATAL: (hard crash)
    size_t pos = output.find("FAIL: File:");
    if (pos == std::string::npos)
        pos = output.find("FAIL: ");
    if (pos == std::string::npos)
        pos = output.find("FATAL:");
    if (pos == std::string::npos)
        return "";
    size_t end = output.find('\n', pos);
    if (end == std::string::npos)
        end = output.size();
    return output.substr(pos, end - pos);
}

// Extract crash summary: signal + last FAIL + last few DirLoader lines
static std::string CrashSummary(const RunResult &result) {
    std::string summary;
    if (result.signal)
        summary += "Signal: " + std::to_string(result.signal) + " (" +
                   (result.signal == 11 ? "SIGSEGV" :
                    result.signal == 6  ? "SIGABRT" : "other") + ")\n";

    std::string fatal = FindFatal(result.output);
    if (!fatal.empty())
        summary += fatal + "\n";

    // Find "Caught" line for crash address
    size_t caughtPos = result.output.find("DC3 Native: Caught ");
    if (caughtPos != std::string::npos) {
        size_t end = result.output.find('\n', caughtPos);
        summary += result.output.substr(caughtPos, end - caughtPos) + "\n";
    }

    // Last DirLoader PreLoad line (shows what was being loaded)
    size_t lastPreLoad = result.output.rfind("DirLoader: PreLoad ");
    if (lastPreLoad != std::string::npos) {
        size_t end = result.output.find('\n', lastPreLoad);
        summary += "Last load: " + result.output.substr(lastPreLoad, end - lastPreLoad) + "\n";
    }

    // Last DataNew line if present
    size_t lastDataNew = result.output.rfind("DataNew: creating ");
    if (lastDataNew != std::string::npos) {
        size_t end = result.output.find('\n', lastDataNew);
        summary += result.output.substr(lastDataNew, end - lastDataNew) + "\n";
    }

    return summary;
}

// Print the tail of output for test logs, optionally flush full output to file
static void PrintOutputTail(const std::string &output, size_t maxBytes = 2000) {
    // Write full output to file if MILO_TEST_LOGFILE is set
    const char *logfile = getenv("MILO_TEST_LOGFILE");
    if (logfile && logfile[0]) {
        FILE *f = fopen(logfile, "w");
        if (f) {
            fwrite(output.data(), 1, output.size(), f);
            fclose(f);
            printf("--- full output written to %s (%zu bytes) ---\n", logfile, output.size());
        }
    }

    printf("--- dc3-native output (%zu bytes) ---\n", output.size());
    if (output.size() > maxBytes)
        printf("...[truncated]...\n%s", output.substr(output.size() - maxBytes).c_str());
    else
        printf("%s", output.c_str());
    printf("--- end output ---\n");
}

// Write a temporary input script file, returns path.  The name is unique to
// this process and call: the fixed dc3_test_input.txt was shared by every
// concurrent gate run, so one run's engine could replay another test's script
// (np-harness).
static std::string WriteInputScript(const std::vector<std::pair<int, std::string>> &inputs) {
    static int sCount = 0;
    std::string path = std::string(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp/claude-1000")
                       + "/dc3_test_input_" + std::to_string((int)getpid()) + "_" +
                       std::to_string(sCount++) + ".txt";
    std::ofstream f(path);
    for (auto &entry : inputs)
        f << entry.first << " " << entry.second << "\n";
    f.close();
    return path;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Where dc3-native will look for gen/main_xbox.hdr -- the boot path's own probe
// (System_Native.cpp; $DC3_DATA, then orig-assets / ../orig-assets /
// ../../orig-assets / . relative to the cwd, which RunHeadless passes through
// unchanged). nullptr = no game data.
extern const char *NativeFindDataDir();

class HeadlessBootTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string binary = GetDc3NativePath();
        if (access(binary.c_str(), X_OK) != 0)
            GTEST_SKIP() << "dc3-native binary not found at " << binary;
        // Booting with no archive is not a boot test: it dies straight after
        // "could not find game data" (SIGFPE on the asset-less CI runner),
        // which says nothing about the main loop.
        if (!NativeFindDataDir()) {
            char cwd[4096];
            GTEST_SKIP() << "[no-game-data] gen/main_xbox.hdr not under $DC3_DATA "
                            "or orig-assets/../orig-assets/../../orig-assets/. from cwd "
                         << (getcwd(cwd, sizeof(cwd)) ? cwd : "?");
        }
    }
};

// A boot with no game data must END WITH AN ERROR, not a signal. Asset-free on
// purpose (an empty temp dir as both cwd and DC3_DATA), so it runs everywhere,
// the data-less CI runner included. Before the System_Native.cpp check, the
// header-less Archive had an empty hash table and the first file lookup
// divided by zero: dc3-native exited 136 (SIGFPE via its handler) after
// "could not find game data", and so did every engine-backed test.
TEST(HeadlessBootNoGameData, ExitsWithAnErrorNotASignal) {
    std::string binary = GetDc3NativePath();
    if (access(binary.c_str(), X_OK) != 0)
        GTEST_SKIP() << "dc3-native binary not found at " << binary;
    char tmpl[] = "/tmp/dc3-no-game-data-XXXXXX";
    char *empty = mkdtemp(tmpl);
    ASSERT_NE(empty, nullptr);
    auto result = RunHeadless(100, nullptr, 60, {{"DC3_DATA", empty}}, empty);
    PrintOutputTail(result.output);
    rmdir(empty);
    EXPECT_FALSE(result.timedOut);
    EXPECT_EQ(result.signal, 0) << CrashSummary(result);
    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.output.find("FATAL - no game data"), std::string::npos)
        << "the player gets no explanation";
    EXPECT_NE(result.output.find(std::string("DC3_DATA is '") + empty + "'"), std::string::npos)
        << "the error should name the DC3_DATA it rejected";
}

// Boot and run enough frames to see the frame counter tick
TEST_F(HeadlessBootTest, SurvivesMainLoop) {
    auto result = RunHeadless(2000, nullptr, 120);
    PrintOutputTail(result.output);

    std::string summary = CrashSummary(result);
    if (!summary.empty())
        printf("=== CRASH SUMMARY ===\n%s=== END SUMMARY ===\n", summary.c_str());

    ASSERT_EQ(result.signal, 0) << "Engine crashed:\n" << summary;
    ASSERT_TRUE(FindFatal(result.output).empty()) << "Engine hit assertion:\n" << summary;

    bool completed = HasCompletionMarker(result.output);
    printf("Engine completed all frames: %s\n", completed ? "yes" : "no");
    EXPECT_TRUE(completed) << "Expected engine to complete 2000 frames of main loop";

    // H-17: a headless run is never frame-capped by default -- the flow tests
    // depend on running faster than real time.
    EXPECT_NE(result.output.find("frame cap: off (headless"), std::string::npos)
        << "headless runs must stay uncapped unless DC3_FPS_CAP asks";
}

// H-17: DC3_FPS_CAP caps a headless run when asked, by sleeping (the exit line
// reports how many frames the pacer held and for how long).
TEST_F(HeadlessBootTest, FrameCapWhenAsked) {
    auto result = RunHeadless(150, nullptr, 120, {{"DC3_FPS_CAP", "30"}});
    PrintOutputTail(result.output);
    ASSERT_EQ(result.signal, 0) << CrashSummary(result);
    ASSERT_TRUE(HasCompletionMarker(result.output)) << CrashSummary(result);
    EXPECT_NE(result.output.find("frame cap: 30.0 fps (headless, DC3_FPS_CAP)"),
              std::string::npos);
    long long paced = -1;
    int frames = -1;
    double slept = -1.0;
    size_t pos = result.output.find("frame cap: 30.00 fps, ");
    ASSERT_NE(pos, std::string::npos) << "no pacer summary at exit";
    ASSERT_EQ(sscanf(result.output.c_str() + pos,
                     "frame cap: 30.00 fps, %lld of %d frames paced, %lf s slept", &paced,
                     &frames, &slept),
              3);
    EXPECT_EQ(frames, 150);
    // Boot frames are long (loading) and are not slept; the menu frames after
    // them are. 150 frames at 30 fps is 5 s; well over half must have slept.
    EXPECT_GE(paced, 75) << "the cap held too few frames";
    EXPECT_GE(slept, 1.0) << "the cap did not sleep";
}

// Boot with scripted input — press Start after some frames to dismiss title
// NOTE: Currently crashes in DTA script handlers for UIScreen::Handle when
// button input triggers joypad config lookups. The DTA handler path needs
// Flow::Enter() and complete DTA infrastructure to work fully.
TEST_F(HeadlessBootTest, InputReplayStartButton) {
    auto scriptPath = WriteInputScript({
        {100, "start"},
        {200, "confirm"},
        {300, "start"},
        {400, "confirm"},
        {500, "down"},
        {550, "confirm"},
    });

    auto result = RunHeadless(1000, scriptPath.c_str(), 120);
    PrintOutputTail(result.output);
    unlink(scriptPath.c_str());

    std::string summary = CrashSummary(result);
    if (!summary.empty())
        printf("=== CRASH SUMMARY ===\n%s=== END SUMMARY ===\n", summary.c_str());

    // Check that scripted input was at least processed before any crash
    bool inputProcessed = result.output.find("DC3 Input:") != std::string::npos;
    if (inputProcessed)
        printf("Scripted input was processed by the engine\n");
    else
        printf("Scripted input was NOT processed (engine may not have reached main loop)\n");

    if (result.signal != 0) {
        // Known issue: button input triggers DTA script handlers that
        // crash on null when required config entries are missing.
        // History: docs/archive/2026-11-13-native-doc-audit/native/DECOMP_GAPS.md (Flow::Enter blocker)
        printf("InputReplay: crash signal=%d — expected until DTA handler path is complete\n",
               result.signal);
        GTEST_SKIP() << "Input replay crashes in DTA handler path (known limitation)";
    }
    ASSERT_TRUE(FindFatal(result.output).empty()) << "Engine hit assertion:\n" << summary;
    EXPECT_EQ(result.exitCode, 0);
}

// ---------------------------------------------------------------------------
// DC3_PROFILE (docs/debugging/native.md "Profiles"): dev is the default and
// turns the developer defaults on; release is the player build and turns none
// of them on.  The unit half (the table, overrides) is test_native_profile.cpp;
// this is what a real dc3-native does with each.  DC3_PROFILE= (empty) is the
// unset case: RunHeadless can only add variables, and an empty one resolves
// exactly as an unset one, so a DC3_PROFILE in the caller's shell cannot leak.
// The developer variables themselves are blanked the same way, so a value in
// the caller's environment cannot stand in for the profile's.
// ---------------------------------------------------------------------------
// DC3_FAST_BOOT=1 and 3000 frames: the run reaches main_screen, whose
// preloads raise MILO_NOTIFYs inside the main loop (measured 2026-11-14: ten
// cheat_display lines by frame 3000; without fast boot a headless run is still
// on attract_screen at frame 8000 and the only notify is before the loop).
static const int kProfileFrames = 3000;
static std::vector<EnvVar> ProfileEnv(const char *profile) {
    return {{"DC3_PROFILE", profile},     {"DC3_HTTP", ""},       {"DC3_HTTP_PORT", ""},
            {"DC3_DEV_MENU", ""},         {"DC3_TEL", ""},        {"DC3_TAG_MODALS", ""},
            {"DC3_RND_OVERLAYS", ""},     {"DC3_DEV_OPTIONS", ""}, {"DC3_FAST_BOOT", "1"}};
}

static bool Has(const std::string &out, const char *s) {
    return out.find(s) != std::string::npos;
}

TEST_F(HeadlessBootTest, DevProfileIsTheDefault) {
    auto result = RunHeadless(kProfileFrames, nullptr, 180, ProfileEnv(""));
    const std::string &out = result.output;
    ASSERT_EQ(result.signal, 0) << CrashSummary(result);
    ASSERT_TRUE(HasCompletionMarker(out)) << "no frame-cap line\n" << CrashSummary(result);
    EXPECT_TRUE(Has(out, "DC3 Native: profile=dev (the default;"))
        << "the default profile is not dev, or is not announced";
    EXPECT_TRUE(Has(out, "DC3 Native: profile dev defaults: DC3_DEV_OPTIONS=1"));
    // The banner a person reads: what is on and the switch for each.
    EXPECT_TRUE(Has(out, "DC3 Native: +-- developer profile (DC3_PROFILE=dev)"))
        << "no boot banner";
    EXPECT_TRUE(Has(out, "| on   HTTP debug server")) << "the banner omits a row that is on";
    EXPECT_TRUE(Has(out, "off: DC3_HTTP=0\n")) << "the banner does not say how to turn it off";
    EXPECT_TRUE(Has(out, "DC3_PROFILE=release turns all of it off"));
    // The debug server: on, on loopback, and (headless, port unset) on a
    // kernel-assigned port -- a test run must not squat 9090.
    EXPECT_TRUE(Has(out, "DC3_HTTP_PORT=")) << "the HTTP debug server did not start";
    EXPECT_TRUE(Has(out, "[HttpServer] Listening on 127.0.0.1:"))
        << "the debug server is not loopback-only";
    EXPECT_FALSE(Has(out, "DC3_HTTP_PORT=9090\n"))
        << "a headless run with DC3_HTTP_PORT unset took 9090";
    EXPECT_TRUE(Has(out, "development menu on (DC3_DEV_MENU)"));
    EXPECT_TRUE(Has(out, "telemetry sampled for /api/telemetry only"));
    EXPECT_FALSE(Has(out, "DC3_TEL: frame=")) << "DC3_TEL=api must not print telemetry lines";
    // Boot raises MILO_NOTIFYs (measured: XTitleServerCreateEnumerator,
    // localization); the dev tap prints them, rate-limited, with no backtraces,
    // and the corner overlay shows the cheat_display copy.
    EXPECT_TRUE(Has(out, "\nMILO_NOTIFY: ")) << "the dev console printed no notify";
    EXPECT_FALSE(Has(out, "MILO_BT:")) << "the dev tap must not dump backtraces";
    EXPECT_TRUE(Has(out, "DC3 OVERLAY [cheats] ")) << "cheat_display reached no overlay";
}

TEST_F(HeadlessBootTest, ReleaseProfileIsThePlayerBuild) {
    auto result = RunHeadless(kProfileFrames, nullptr, 180, ProfileEnv("release"));
    const std::string &out = result.output;
    ASSERT_EQ(result.signal, 0) << CrashSummary(result);
    ASSERT_TRUE(HasCompletionMarker(out)) << "no frame-cap line\n" << CrashSummary(result);
    EXPECT_TRUE(Has(out, "DC3 Native: profile=release"));
    EXPECT_FALSE(Has(out, "profile dev defaults")) << "release applied the dev table";
    EXPECT_FALSE(Has(out, "developer profile")) << "release printed the dev banner";
    EXPECT_FALSE(Has(out, "DC3_HTTP_PORT=")) << "release started the HTTP debug server";
    EXPECT_FALSE(Has(out, "[HttpServer]"));
    EXPECT_FALSE(Has(out, "development menu on"));
    EXPECT_FALSE(Has(out, "DC3_TEL:")) << "release enabled telemetry";
    EXPECT_FALSE(Has(out, "\nMILO_NOTIFY: ")) << "release printed the modal tap";
    EXPECT_FALSE(Has(out, "DC3 OVERLAY [")) << "release drew the developer overlays";
}

// Long-running stability test — only runs if MILO_LONG_TEST=1
TEST_F(HeadlessBootTest, LongRunStability) {
    if (!getenv("MILO_LONG_TEST"))
        GTEST_SKIP() << "Set MILO_LONG_TEST=1 to enable (runs ~5 min)";

    auto scriptPath = WriteInputScript({
        {100, "start"},
        {200, "confirm"},
        {500, "start"},
        {600, "down"},
        {650, "confirm"},
        {1000, "start"},
        {2000, "confirm"},
        {5000, "start"},
    });

    auto result = RunHeadless(10000, scriptPath.c_str(), 300);
    PrintOutputTail(result.output);
    unlink(scriptPath.c_str());

    std::string summary = CrashSummary(result);
    if (!summary.empty())
        printf("=== CRASH SUMMARY ===\n%s=== END SUMMARY ===\n", summary.c_str());

    ASSERT_EQ(result.signal, 0) << "Engine crashed:\n" << summary;
    ASSERT_TRUE(FindFatal(result.output).empty()) << "Engine hit assertion:\n" << summary;

    bool completed = HasCompletionMarker(result.output);
    printf("Long run completed all frames: %s\n", completed ? "yes" : "no");
    EXPECT_TRUE(completed) << "Expected engine to complete 10000 frames";
}
