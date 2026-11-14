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

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

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
        {"DC3_DEV_OPTIONS", "1"}, {"DC3_DEV_MENU", "1"},  {"DC3_RND_OVERLAYS", "1"},
        {"DC3_HTTP", "1"},        {"DC3_TEL", "api"},     {"DC3_TAG_MODALS", "dev"},
        {"MILO_INPUT_LOG", "1"},  {"DC3_MOVE_RATING_LOG", "1"}, {"MILO_PERF", "1"},
    };
    std::map<std::string, std::string> have;
    for (const Dc3ProfileDefault &row : Rows()) have[row.name] = row.value;
    for (const auto &kv : want) {
        auto it = have.find(kv.first);
        ASSERT_NE(it, have.end()) << kv.first << " is not a dev default any more";
        EXPECT_EQ(it->second, kv.second) << kv.first;
    }
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
    };
    for (const Dc3ProfileDefault &row : Rows())
        EXPECT_EQ(forbidden.count(row.name), 0u)
            << row.name << " changes the game or costs time; it must stay opt-in";
}
