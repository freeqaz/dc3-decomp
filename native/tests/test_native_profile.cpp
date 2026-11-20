// DC3_PROFILE: the native build's defaults in one table
// (native/src/platform/System_Native.cpp, docs/debugging/native.md "Profiles").
//
// dev (the default) setenv()s each row's value when the variable is unset;
// release sets nothing, so it is each variable's compiled default -- the
// player-facing build.  A variable already in the environment always wins.
//
// These run Dc3ProfileApply() in-process against a scrubbed environment and
// restore it afterwards.  The end-to-end half (a real dc3-native under each
// profile) is HeadlessBootTest.DevProfileIsTheDefault /
// ReleaseProfileIsThePlayerBuild in test_headless_boot.cpp; the Options rows
// are OptionsGameplayFlowTest / OptionsGameplayPlayerFlowTest in
// test_dta_flow.cpp.

#include <gtest/gtest.h>

// dc3's header by path: "platform/NativeSettings.h" can resolve to the
// engine's older copy, which does not carry the profile API.
#include "../src/platform/NativeSettings.h"
#include "../src/platform/DebugPanel.h"
#include "os/Debug.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// Saves and scrubs DC3_PROFILE plus every variable the dev table names, and
// puts them all back on destruction.
class ScopedProfileEnv {
public:
    ScopedProfileEnv() {
        mNames.push_back("DC3_PROFILE");
        int n = 0;
        const Dc3ProfileDefault *rows = Dc3ProfileDevDefaults(&n);
        for (int i = 0; i < n; i++) mNames.push_back(rows[i].name);
        for (const std::string &name : mNames) {
            const char *v = getenv(name.c_str());
            if (v) mSaved[name] = v;
            unsetenv(name.c_str());
        }
    }
    ~ScopedProfileEnv() {
        for (const std::string &name : mNames) {
            auto it = mSaved.find(name);
            if (it != mSaved.end())
                setenv(name.c_str(), it->second.c_str(), 1);
            else
                unsetenv(name.c_str());
        }
    }

private:
    std::vector<std::string> mNames;
    std::map<std::string, std::string> mSaved;
};

std::vector<Dc3ProfileDefault> Rows() {
    int n = 0;
    const Dc3ProfileDefault *rows = Dc3ProfileDevDefaults(&n);
    return std::vector<Dc3ProfileDefault>(rows, rows + n);
}

std::string Env(const char *name) {
    const char *v = getenv(name);
    return v ? std::string(v) : std::string("<unset>");
}

} // namespace

TEST(Dc3ProfileTest, DevIsTheDefaultAndAppliesEveryRow) {
    ScopedProfileEnv scope;
    EXPECT_EQ(Dc3ProfileResolve(), kDc3ProfileDev) << "DC3_PROFILE unset must be dev";
    Dc3ProfileApply();
    EXPECT_EQ(Env("DC3_PROFILE"), "dev") << "the resolved profile is exported";
    for (const Dc3ProfileDefault &row : Rows())
        EXPECT_EQ(Env(row.name), row.value) << row.name << " (" << row.what << ")";
}

// The rows the owner asked for by name; a table edit that drops one fails here,
// not silently in a playtest.
TEST(Dc3ProfileTest, DevTableCarriesTheDeveloperDefaults) {
    std::map<std::string, std::string> want = {
        {"DC3_DEV_OPTIONS", "1"}, {"DC3_DEV_MENU", "1"},  {"DC3_RND_OVERLAYS", "log"},
        {"DC3_HTTP", "1"},        {"DC3_TEL", "api"},     {"DC3_TAG_MODALS", "dev"},
        {"MILO_INPUT_LOG", "1"},  {"DC3_MOVE_RATING_LOG", "1"}, {"MILO_PERF", "1"},
        {"DC3_DLC", "1"},
    };
    std::map<std::string, std::string> have;
    for (const Dc3ProfileDefault &row : Rows()) have[row.name] = row.value;
    for (const auto &kv : want) {
        auto it = have.find(kv.first);
        ASSERT_NE(it, have.end()) << kv.first << " is not a dev default any more";
        EXPECT_EQ(it->second, kv.second) << kv.first;
    }
}

// The web build lists DLC songs only when DC3_DLC reaches it (main_web.cpp
// WebDlcEnabled, PLAN W-23): a desktop-only row is never applied there, and the
// web song select then silently lacks every package (measured: 54 rows, no
// milkshake, before this row was made web-wide).
TEST(Dc3ProfileTest, DlcIsADevDefaultOnWebToo) {
    for (const Dc3ProfileDefault &row : Rows()) {
        if (std::string(row.name) != "DC3_DLC") continue;
        EXPECT_FALSE(row.desktopOnly) << "DC3_DLC must apply on web as well as desktop";
        return;
    }
    FAIL() << "DC3_DLC is not a dev default";
}

TEST(Dc3ProfileTest, AnExplicitVariableAlwaysWins) {
    ScopedProfileEnv scope;
    setenv("DC3_HTTP", "0", 1);
    setenv("DC3_DEV_OPTIONS", "0", 1);
    setenv("DC3_TAG_MODALS", "1", 1);
    Dc3ProfileApply();
    EXPECT_EQ(Env("DC3_HTTP"), "0");
    EXPECT_EQ(Env("DC3_DEV_OPTIONS"), "0");
    EXPECT_EQ(Env("DC3_TAG_MODALS"), "1") << "the harvest tap must survive the dev default";
    EXPECT_EQ(Env("MILO_INPUT_LOG"), "1") << "the other rows still apply";
}

// Empty counts as unset (as in Dc3EnvFlag): a harness that can only add
// variables blanks one to get the profile's default.
TEST(Dc3ProfileTest, AnEmptyVariableTakesTheDefault) {
    ScopedProfileEnv scope;
    setenv("DC3_HTTP", "", 1);
    setenv("DC3_PROFILE", "", 1);
    Dc3ProfileApply();
    EXPECT_EQ(Env("DC3_HTTP"), "1");
    EXPECT_EQ(Env("DC3_PROFILE"), "dev");
}

// MILO_PERF is tested for presence by the engine (Rnd_Wgpu.cpp), so a literal
// "0" would leave it on; the profile removes an explicit off spelling.
TEST(Dc3ProfileTest, AnExplicitOffReachesAPresenceReader) {
    ScopedProfileEnv scope;
    bool sawPresenceRow = false;
    for (const Dc3ProfileDefault &row : Rows()) {
        if (!row.presence) continue;
        sawPresenceRow = true;
        setenv(row.name, "0", 1);
    }
    ASSERT_TRUE(sawPresenceRow) << "MILO_PERF is presence-tested; its row must say so";
    Dc3ProfileApply();
    for (const Dc3ProfileDefault &row : Rows())
        if (row.presence)
            EXPECT_EQ(Env(row.name), "<unset>") << row.name << "=0 would read as on";
    EXPECT_EQ(Env("MILO_INPUT_LOG"), "1") << "the other rows still apply";
}

TEST(Dc3ProfileTest, ReleaseSetsNothing) {
    ScopedProfileEnv scope;
    setenv("DC3_PROFILE", "release", 1);
    EXPECT_EQ(Dc3ProfileResolve(), kDc3ProfileRelease);
    Dc3ProfileApply();
    for (const Dc3ProfileDefault &row : Rows())
        EXPECT_EQ(Env(row.name), "<unset>") << row.name << " leaked into release";
    EXPECT_EQ(Env("DC3_PROFILE"), "release");
}

TEST(Dc3ProfileTest, ReleaseStillHonoursAnExplicitVariable) {
    ScopedProfileEnv scope;
    setenv("DC3_PROFILE", "RELEASE", 1); // case-insensitive
    setenv("DC3_DEV_OPTIONS", "1", 1);
    Dc3ProfileApply();
    EXPECT_EQ(Env("DC3_DEV_OPTIONS"), "1");
    EXPECT_EQ(Env("DC3_HTTP"), "<unset>");
}

// The boot banner prints every label; harnesses find the HTTP port by
// searching the output for "DC3_HTTP_PORT=", so no label may contain it.
TEST(Dc3ProfileTest, NoBannerLabelLooksLikeThePortLine) {
    for (const Dc3ProfileDefault &row : Rows()) {
        ASSERT_NE(row.label, nullptr) << row.name;
        EXPECT_EQ(std::string(row.label).find("DC3_HTTP_PORT="), std::string::npos)
            << row.name << "'s banner label would be read as the port line";
    }
}

TEST(Dc3ProfileTest, AnUnknownProfileIsDev) {
    ScopedProfileEnv scope;
    setenv("DC3_PROFILE", "shipping", 1);
    EXPECT_EQ(Dc3ProfileResolve(), kDc3ProfileDev);
}

// The policy the table must keep (System_Native.cpp's header comment): no row
// may change the game away from the original in a way that invalidates testing
// it, or cost real time.  Each of these is a tool, not a default.
TEST(Dc3ProfileTest, NoRowChangesTheGameOrCostsTime) {
    const std::set<std::string> forbidden = {
        "DC3_FAST_TIME",           "DC3_FAST_BOOT",       "DC3_SHOW_SPLASH",
        "DC3_POSE_SELFTEST",       "DC3_POSE_PERFORM",    "DC3_POSE_LATENCY_MS",
        "DC3_NATIVE_SCORING",      "DC3_REAL_MOVE_PASSED", "DC3_DUMMY_PLAYERS",
        "DC3_CONTROLLER_MODE",     "MILO_FATAL_FAILS",    "MILO_MAX_FRAMES",
        "DC3_REFRING_AUDIT",       "DC3_POISON_FREED_OBJECTS", "DC3_STUB_TRACE",
        "DC3_DRAW_CRASH_GUARD",    "MILO_INPUT_SCRIPT",   "DC3_POSE",
        "MILO_NORENDER",           "MILO_HEADLESS",       "DC3_NO_AUDIO",
        // The boot sequence follows the console's state, not the profile
        // (System_Native.cpp "The boot sequence").
        "DC3_BOOT_INTRO",
    };
    for (const Dc3ProfileDefault &row : Rows())
        EXPECT_EQ(forbidden.count(row.name), 0u)
            << row.name << " changes the game or costs time; it must stay opt-in";
}

// DC3_DLC / DC3_DLC_DIR -> the DLC roots ContentMgr_Native mounts
// (NativeDlcRoots, ContentMgr_Native.cpp).  The dev profile's DC3_DLC=1 must
// find <data>/dlc and nothing else; DC3_DLC=0 (what ctest's MILO_TEST_ENV
// sets) must mount nothing; a named DC3_DLC_DIR (the DLC flow tests) wins.
TEST(Dc3ProfileTest, DlcRootsFollowDc3DlcAndDc3DlcDir) {
    ScopedProfileEnv scope;
    const char *savedData = getenv("DC3_DATA");
    std::string oldData = savedData ? savedData : "";
    const char *savedDir = getenv("DC3_DLC_DIR");
    std::string oldDir = savedDir ? savedDir : "";
    char tmpl[] = "/tmp/dc3-dlc-roots-XXXXXX";
    ASSERT_NE(mkdtemp(tmpl), nullptr);
    std::string data = tmpl;
    ASSERT_EQ(mkdir((data + "/gen").c_str(), 0755), 0);
    FILE *hdr = fopen((data + "/gen/main_xbox.hdr").c_str(), "wb");
    ASSERT_NE(hdr, nullptr);
    fclose(hdr);
    setenv("DC3_DATA", data.c_str(), 1);
    unsetenv("DC3_DLC_DIR");

    setenv("DC3_DLC", "1", 1);
    EXPECT_EQ(NativeDlcRoots(), "") << "no <data>/dlc directory: nothing to mount";
    ASSERT_EQ(mkdir((data + "/dlc").c_str(), 0755), 0);
    EXPECT_EQ(NativeDlcRoots(), data + "/dlc");
    setenv("DC3_DLC", "0", 1);
    EXPECT_EQ(NativeDlcRoots(), "") << "DC3_DLC=0 must mount nothing";
    unsetenv("DC3_DLC");
    EXPECT_EQ(NativeDlcRoots(), "") << "unset (release, milo-tests) must mount nothing";
    setenv("DC3_DLC", "0", 1);
    setenv("DC3_DLC_DIR", "/a:/b", 1);
    EXPECT_EQ(NativeDlcRoots(), "/a:/b") << "a named DC3_DLC_DIR wins";
    setenv("DC3_DLC", "1", 1);
    setenv("DC3_DLC_DIR", "", 1);
    EXPECT_EQ(NativeDlcRoots(), "") << "an empty DC3_DLC_DIR names no roots (play.sh --no-dlc)";

    // The dev profile turns it on.
    unsetenv("DC3_DLC");
    unsetenv("DC3_DLC_DIR");
    Dc3ProfileApply();
    EXPECT_EQ(NativeDlcRoots(), data + "/dlc");

    rmdir((data + "/dlc").c_str());
    remove((data + "/gen/main_xbox.hdr").c_str());
    rmdir((data + "/gen").c_str());
    rmdir(data.c_str());
    if (savedData) setenv("DC3_DATA", oldData.c_str(), 1); else unsetenv("DC3_DATA");
    if (savedDir) setenv("DC3_DLC_DIR", oldDir.c_str(), 1); else unsetenv("DC3_DLC_DIR");
}

// ---------------------------------------------------------------------------
// np-notifyband: the debug build's notices (cheat_display -- every MILO_NOTIFY
// at notify_level 1, e.g. "P1_area1_far01.shot ... creating target cache for
// boombox.trans, targets changed while playing camera" across gameplay) are
// not drawn over the picture by default; the console game never drew them.
// They still reach the log (DC3 OVERLAY lines; MILO_NOTIFY: from the dev tap),
// DC3_RND_OVERLAYS=1 draws them again, and a FAIL -- which never went through
// an overlay -- is printed exactly as before.  The end-to-end half (a real
// dc3-native: "DC3 OVERLAY: drawn on screen" absent by default, present with
// the switch) is HeadlessBootTest.DevProfileIsTheDefault /
// OverlaysSwitchDrawsThem.
//
// Sabotages (each turns a test here red):
//  - the dev row back to "1"            -> DebugNoticesAreLoggedNotDrawnByDefault
//  - RndOverlaysModeFor("1") -> log     -> TheSwitchDrawsThem
//  - Debug::Fail's "FAIL: " print skipped unless DC3_RND_OVERLAYS draws
//                                        -> AFailIsStillPrintedUnderTheDefault
//    (it survived a first version that matched "FAIL: <msg>" as a substring:
//    the dev tap's "MILO_FAIL: <msg>" line carried it)
// ---------------------------------------------------------------------------

TEST(Dc3ProfileTest, DebugNoticesAreLoggedNotDrawnByDefault) {
    ScopedProfileEnv scope;
    Dc3ProfileApply();
    EXPECT_EQ(DebugPanel::RndOverlaysModeFor(getenv("DC3_RND_OVERLAYS")),
              DebugPanel::kRndOverlaysLog)
        << "the dev profile (the default, desktop and web) draws the debug notices over the "
           "picture; DC3_RND_OVERLAYS=" << Env("DC3_RND_OVERLAYS");
}

TEST(Dc3ProfileTest, TheSwitchDrawsThem) {
    ScopedProfileEnv scope;
    setenv("DC3_RND_OVERLAYS", "1", 1);
    Dc3ProfileApply();
    EXPECT_EQ(Env("DC3_RND_OVERLAYS"), "1") << "the profile overrode an explicit switch";
    EXPECT_EQ(DebugPanel::RndOverlaysModeFor(getenv("DC3_RND_OVERLAYS")),
              DebugPanel::kRndOverlaysDraw)
        << "DC3_RND_OVERLAYS=1 no longer draws the notices";
}

TEST(Dc3ProfileTest, ReleaseNeitherDrawsNorCollectsThem) {
    ScopedProfileEnv scope;
    setenv("DC3_PROFILE", "release", 1);
    Dc3ProfileApply();
    EXPECT_EQ(DebugPanel::RndOverlaysModeFor(getenv("DC3_RND_OVERLAYS")),
              DebugPanel::kRndOverlaysOff);
}

TEST(Dc3ProfileTest, AFailIsStillPrintedUnderTheDefault) {
    ScopedProfileEnv scope;
    Dc3ProfileApply();
    const char *fatal = getenv("MILO_FATAL_FAILS");
    ASSERT_TRUE(!fatal || !*fatal || strcmp(fatal, "0") == 0)
        << "MILO_FATAL_FAILS is set: a FAIL would abort the test binary";
    testing::internal::CaptureStderr();
    TheDebug.Fail("np-notifyband: a FAIL under the default overlay mode", nullptr);
    const std::string err = "\n" + testing::internal::GetCapturedStderr();
    // A line of its own that STARTS with "FAIL: " -- Debug::Fail's print, what
    // a terminal, engine.log and the browser console (console.error) show.  The
    // dev tap's "MILO_FAIL: <msg>" line contains the same text, so a substring
    // match would pass on the tap alone (it did: the FAIL sabotage in the
    // header comment survived until this was anchored).
    EXPECT_NE(err.find("\nFAIL: np-notifyband: a FAIL under the default overlay mode\n"),
              std::string::npos)
        << "a FAIL is no longer printed when the debug notices are not drawn:" << err;
}

// ---------------------------------------------------------------------------
// The boot sequence (PLAN H-123; System_Native.cpp "The boot sequence",
// docs/debugging/native.md "Boot sequence").  A first launch on a console is
// the original's sequence; a console whose global options are on disk passes
// the splash screens, attract and the autosave warning and waits on the title
// screen.  Every variable that is set wins over the disk, in both directions.
// This is the decision table; what a real dc3-native does with each plan is
// HeadlessBootTest.{AFirstLaunch..., AReturningConsole..., ...} in
// test_headless_boot.cpp.
// ---------------------------------------------------------------------------
namespace {
const bool kPlayed = true, kFresh = false;
Dc3BootPlan Plan(bool onDisk, const char *splash = nullptr, const char *fast = nullptr,
                 const char *intro = nullptr, bool webFast = false) {
    return Dc3BootPlanResolve(onDisk, splash, fast, intro, webFast);
}
} // namespace

TEST(Dc3BootPlanTest, AFirstLaunchIsTheOriginalSequence) {
    const Dc3BootPlan p = Plan(kFresh);
    EXPECT_FALSE(p.returning);
    EXPECT_TRUE(p.showSplash) << "a first launch must show the splash screens";
    EXPECT_FALSE(p.skipIntro) << "a first launch must show attract and the autosave warning";
    EXPECT_FALSE(p.fastBoot);
}

TEST(Dc3BootPlanTest, AReturningConsoleSkipsTheIntroButNotTheTitle) {
    const Dc3BootPlan p = Plan(kPlayed);
    EXPECT_TRUE(p.returning);
    EXPECT_FALSE(p.showSplash);
    EXPECT_TRUE(p.skipIntro);
    EXPECT_FALSE(p.fastBoot) << "the title confirm sets the initial profiles and "
                                "$post_load_dest_screen: it stays the player's";
}

TEST(Dc3BootPlanTest, BootIntroIsTheOneSwitchBackAndForth) {
    const Dc3BootPlan again = Plan(kPlayed, nullptr, nullptr, "1");
    EXPECT_TRUE(again.showSplash);
    EXPECT_FALSE(again.skipIntro);
    EXPECT_FALSE(again.fastBoot);
    const Dc3BootPlan never = Plan(kFresh, nullptr, nullptr, "0");
    EXPECT_FALSE(never.showSplash);
    EXPECT_TRUE(never.skipIntro);
    EXPECT_FALSE(never.fastBoot);
    for (const char *off : {"false", "OFF", "no"})
        EXPECT_TRUE(Plan(kFresh, nullptr, nullptr, off).skipIntro) << off;
}

TEST(Dc3BootPlanTest, AnExplicitSplashWinsBothWays) {
    EXPECT_TRUE(Plan(kPlayed, "1").showSplash);
    EXPECT_TRUE(Plan(kPlayed, "1").skipIntro) << "DC3_SHOW_SPLASH is the splash only";
    EXPECT_FALSE(Plan(kFresh, "0").showSplash);
    EXPECT_FALSE(Plan(kFresh, "0").skipIntro) << "DC3_SHOW_SPLASH is the splash only";
    EXPECT_FALSE(Plan(kPlayed, "0", nullptr, "1").showSplash) << "it wins over DC3_BOOT_INTRO too";
    EXPECT_TRUE(Plan(kFresh, "1", nullptr, "0").showSplash);
}

TEST(Dc3BootPlanTest, AnExplicitFastBootWinsBothWays) {
    // On: every boot screen passes, whatever the disk or DC3_BOOT_INTRO says
    // (the test suites' boot).  It never decided the splash and still does not.
    for (bool onDisk : {kFresh, kPlayed}) {
        const Dc3BootPlan on = Plan(onDisk, nullptr, "1");
        EXPECT_TRUE(on.fastBoot);
        EXPECT_TRUE(on.skipIntro);
        EXPECT_EQ(on.showSplash, !onDisk);
        EXPECT_TRUE(Plan(onDisk, nullptr, "1", "1").fastBoot);
    }
    // Off: what it always gave, even on a played console -- the original
    // sequence, splash included, and no screen passed for the player.
    const Dc3BootPlan off = Plan(kPlayed, nullptr, "0");
    EXPECT_FALSE(off.fastBoot);
    EXPECT_FALSE(off.skipIntro) << "DC3_FAST_BOOT=0 must leave every boot screen to the player";
    EXPECT_TRUE(off.showSplash);
    EXPECT_FALSE(Plan(kPlayed, "0", "0").showSplash) << "DC3_SHOW_SPLASH still decides the splash";
    EXPECT_FALSE(Plan(kPlayed, nullptr, "0", "0").skipIntro)
        << "DC3_FAST_BOOT=0 wins over DC3_BOOT_INTRO=0 on passing screens";
    // The web page's ?fast_boot=1 is the same switch.
    EXPECT_TRUE(Plan(kFresh, nullptr, nullptr, nullptr, true).fastBoot);
    EXPECT_TRUE(Plan(kPlayed, nullptr, "0", nullptr, true).fastBoot);
}

// RunHeadless and the flow tests blank a variable to mean "unset" (they can
// only add variables), so an empty value must decide nothing.
TEST(Dc3BootPlanTest, AnEmptyVariableDecidesNothing) {
    for (bool onDisk : {kFresh, kPlayed}) {
        const Dc3BootPlan unset = Plan(onDisk), empty = Plan(onDisk, "", "", "");
        EXPECT_EQ(empty.showSplash, unset.showSplash);
        EXPECT_EQ(empty.skipIntro, unset.skipIntro);
        EXPECT_EQ(empty.fastBoot, unset.fastBoot);
    }
}

// "Played before" is the global options file the game itself writes
// (<cache root>/global/global, Cache_Stub.cpp): present, regular, not empty.
TEST(Dc3BootPlanTest, PlayedBeforeIsTheGlobalOptionsFile) {
    const char *names[] = {"DC3_CACHE_DIR", "HOME"};
    std::string saved[2];
    bool had[2];
    for (int i = 0; i < 2; i++) {
        const char *v = getenv(names[i]);
        had[i] = v != nullptr;
        if (v) saved[i] = v;
    }
    char tmpl[] = "/tmp/dc3-bootplan-XXXXXX";
    ASSERT_NE(mkdtemp(tmpl), nullptr);
    const std::string root = tmpl;

    setenv("DC3_CACHE_DIR", (root + "/cache").c_str(), 1);
    EXPECT_EQ(Dc3GlobalOptionsPath(), root + "/cache/global/global");
    unsetenv("DC3_CACHE_DIR");
    setenv("HOME", root.c_str(), 1);
    EXPECT_EQ(Dc3GlobalOptionsPath(), root + "/.local/share/dc3/cache/global/global");

    setenv("DC3_CACHE_DIR", (root + "/cache").c_str(), 1);
    const std::string file = root + "/cache/global/global";
    EXPECT_FALSE(Dc3GlobalOptionsOnDisk()) << "an empty cache root is a first launch";
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/cache/global").c_str(), 0755);
    EXPECT_FALSE(Dc3GlobalOptionsOnDisk()) << "the cache directory alone is not the file";
    mkdir(file.c_str(), 0755);
    EXPECT_FALSE(Dc3GlobalOptionsOnDisk()) << "a directory of that name is not the file";
    rmdir(file.c_str());
    FILE *f = fopen(file.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    fclose(f);
    EXPECT_FALSE(Dc3GlobalOptionsOnDisk()) << "an empty file is a failed write, not a played console";
    f = fopen(file.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    const char bytes[0x39] = {0};
    fwrite(bytes, 1, sizeof(bytes), f);
    fclose(f);
    EXPECT_TRUE(Dc3GlobalOptionsOnDisk());

    unlink(file.c_str());
    rmdir((root + "/cache/global").c_str());
    rmdir((root + "/cache").c_str());
    rmdir(root.c_str());
    for (int i = 0; i < 2; i++) {
        if (had[i]) setenv(names[i], saved[i].c_str(), 1);
        else unsetenv(names[i]);
    }
}
