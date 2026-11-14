// The native auto-assign must not fight the image's SkeletonChooser (branch
// np-tracking, PLAN W-10).
//
// np-dogfood3's web run played a second song with the camera and nothing was
// scored and no move cards showed.  Reproduced on desktop with the live pose
// server on Boyfriend (score 0, "New player detected!  Raise your hand to
// join." on screen, twice in two runs): the fake camera's cut from its held
// frame to the song frame gave the dancer a new tracking id while the old one
// was still held, so for a second there were two skeletons.  The image's
// chooser round-robins an unbound skeleton for a raised hand and, while it
// waits, drops that skeleton's id from the player ids, so
// SetPlayerSkeletonNavData unbinds whoever holds it.  NativeAutoAssignPlayers
// (GestureMgr_Native.cpp) bound the player to it again on every sensor frame:
// 634 re-binds in one song, each unbind an Overshell player_quit (perform.dta
// on_player_quit: start_score_move_index 1000, flash cards cleared) and each
// rebind a player_join restarting the drop-in grace.  Nothing was ever scored.
// It also bound player 2 to the extra skeleton in a one-player mode, and the
// chooser then switched its active player back and forth.
//
// Two engine runs, Perform on YMCA, headless with no GPU and DC3_FAST_TIME,
// scripts/synthetic_kinect.py as the sensor:
//   ReacquiredDancerTest     at song 20 s the dancer reappears 0.6 m away under
//                            a new tracking id (--jump-at): she is bound to the
//                            new id, scoring goes on, the auto-assign binds a
//                            handful of times, not every frame.
//   SecondPersonPerformTest  a second person walks in at song 20 s and stands
//                            there without raising a hand
//                            (--second-person-from): player 2 is never bound,
//                            player 1 keeps her id, the song is scored.  (Two
//                            people standing in the MENUS are both bound by the
//                            image itself, RoundRobinForStandingStill, so the
//                            second person has to arrive during the song.)
// Players are sampled from /api/pose/target every half second of the song;
// DC3_SCORING_DEBUG prints the auto-assign counter.
//
// Gated by DC3_DTA_FLOW_TESTS=1.  The engine and the sensor are killed by PID,
// never by pattern; every wait is bounded.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include "platform/ExecutablePath.h"
#include "engine_http_port.h"
#include "shell_timeout.h"

namespace {

std::string BinaryDir() {
    char buf[4096];
    ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
    if (len <= 0)
        return ".";
    buf[len] = '\0';
    std::string p(buf);
    size_t slash = p.rfind('/');
    return slash != std::string::npos ? p.substr(0, slash) : ".";
}

bool Exists(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

// Every tutorial seen (test_native_twoplayer.cpp's SeedAllTutorialsSeen; the
// layout is measured in test_dta_flow.cpp).
bool SeedAllTutorialsSeen(const std::string &root) {
    static const unsigned char kGlobalOptions[0x39] = {
        0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0, 0xc1, 0x00, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x09, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    unsigned char bytes[sizeof(kGlobalOptions)];
    memcpy(bytes, kGlobalOptions, sizeof(bytes));
    const unsigned seen = 0x7fffffffu;
    for (int i = 0; i < 4; i++)
        bytes[0x28 + i] = (unsigned char)(seen >> (8 * i));
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    mkdir((root + "/cache/global").c_str(), 0755);
    std::ofstream f(root + "/cache/global/global", std::ios::binary);
    if (!f.is_open())
        return false;
    f.write((const char *)bytes, sizeof(bytes));
    return f.good();
}

// title -> main -> Perform -> YMCA -> multiuser (test_native_pose_dropout.cpp).
const char *kRoute =
    "wait_screen title_screen\n+43 wake\n+45 confirm\n"
    "wait_screen main_screen\n+43 wake\n+45 confirm\n"
    "wait_screen choose_mode_screen\n+43 wake\n+45 confirm\n"
    "wait_screen song_select_screen\n+43 wake\n+45 down\n+105 confirm\n"
    "wait_screen multiuser_screen\n+38 wake\n+40 confirm\n+110 confirm\n+180 confirm\n";

// A field of player `p` in one /api/pose/target answer; -1 if absent.
long Field(const std::string &json, int p, const char *field) {
    std::string key = "\"player\":" + std::to_string(p) + ",";
    size_t at = json.find(key);
    if (at == std::string::npos)
        return -1;
    std::string f = std::string("\"") + field + "\":";
    size_t v = json.find(f, at);
    size_t next = json.find("\"player\":", at + key.size());
    if (v == std::string::npos || (next != std::string::npos && v > next))
        return -1;
    return strtol(json.c_str() + v + f.size(), nullptr, 10);
}

struct PerformRun {
    std::string setupError;
    std::string output; // engine log
    std::string driver;
    std::vector<std::string> samples; // /api/pose/target during the song
    std::string last;                 // /api/pose/target at the stop screen
    bool Entered(const char *screen) const {
        return output.find(std::string("Screen '") + screen + "' Enter") != std::string::npos;
    }
    // The last "auto-assign rebinds p0=A p1=B (total)" line; -1 if none.
    void Rebinds(int &p0, int &p1) const {
        p0 = p1 = -1;
        const char *key = "DC3 SCORING: auto-assign rebinds p0=";
        size_t at = output.rfind(key);
        if (at != std::string::npos)
            sscanf(output.c_str() + at + strlen(key), "%d p1=%d", &p0, &p1);
    }
};

PerformRun RunPerform(const char *sensorArgs, int waitSeconds) {
    PerformRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string repo = dir + "/../..";
    std::string sensor = repo + "/scripts/synthetic_kinect.py";
    if (!Exists(bin) || !Exists(sensor)) {
        r.setupError = "missing " + bin + " or " + sensor + " (scripts/native_test.sh builds it)";
        return r;
    }
    if (system("python3 -c 'import json, socket, struct' >/dev/null 2>&1") != 0) {
        r.setupError = "python3 is not runnable; the synthetic sensor needs it";
        return r;
    }
    char tmpl[] = "/tmp/dc3-secondperson-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        r.setupError = "mkdtemp failed";
        return r;
    }
    std::string R = root;
    if (!SeedAllTutorialsSeen(R)) {
        r.setupError = "could not seed the global options cache under " + R;
        return r;
    }
    std::string route = R + "/route.txt";
    {
        std::ofstream f(route);
        f << kRoute;
    }
    std::string sock = R + "/kinect.sock";
    std::string log = R + "/engine.log";

    std::ostringstream sh;
    sh << kShellTimeoutLookup << "cd " << dir << " || exit 1; "
       << "python3 " << sensor << " --socket " << sock << " " << sensorArgs
       << " --perform-port-file " << EngineHttpPortFile(R) << " > " << R << "/kinect.log 2>&1 & kpid=$!; "
       << "end=$((SECONDS+20)); until [ -S " << sock << " ] || [ $SECONDS -ge $end ]; do sleep 0.2; done; "
       << "[ -S " << sock << " ] || echo 'DRIVER: sensor socket never appeared'; "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " DC3_FAST_TIME=1" << EngineHttpPortShellEnv(R) << " MILO_MAX_FRAMES=0"
       << " DC3_SCORING_DEBUG=1 DC3_CONTROLLER_MODE=forced"
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " MILO_INPUT_SCRIPT=" << route
       << " DC3_POSE=external DC3_POSE_NO_SPAWN=1 DC3_POSE_SOCKET=" << sock
       << " exec ${t:+$t " << (waitSeconds + 60) << "} ./dc3-native > " << log << " 2>&1 ) & epid=$!; "
       << EngineHttpApiShell(R, "epid", 120)
       << "end=$((SECONDS+" << waitSeconds << ")); "
       << "until grep -aqE \"Screen '(perform_endgame_screen|perform_pause_screen)' Enter\" " << log
       << " 2>/dev/null || [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do "
       << "if [ -n \"$api\" ] && grep -aq \"Screen 'game_screen' Enter\" " << log << " 2>/dev/null; then "
       << "echo \"SAMPLE: $(curl -s -m 2 \"$api/pose/target\")\"; fi; sleep 0.5; done; "
       << "echo \"DRIVER: stop after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
       << "echo \"LAST: $(curl -s -m 5 \"$api/pose/target\")\"; "
       << "kill $epid 2>/dev/null; kill $kpid 2>/dev/null; sleep 1; "
       << "kill -9 $epid 2>/dev/null; kill -9 $kpid 2>/dev/null; "
       << "cat " << log;
    FILE *p = popen(sh.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    std::string line;
    char buf[8192];
    while (fgets(buf, sizeof buf, p)) {
        line += buf;
        if (line.empty() || line.back() != '\n')
            continue; // a long JSON line arrives in pieces
        if (!strncmp(line.c_str(), "DRIVER:", 7))
            r.driver += line;
        else if (!strncmp(line.c_str(), "SAMPLE:", 7)) {
            if (line.find("\"moveDir\":true") != std::string::npos)
                r.samples.push_back(line);
        } else if (!strncmp(line.c_str(), "LAST:", 5))
            r.last = line;
        else
            r.output += line;
        line.clear();
    }
    r.output += line;
    pclose(p);
    return r;
}

class PerformFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
    static std::string Dump(const PerformRun &r) {
        std::string path = "/tmp/claude-1000/second_person_" +
            std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "_" +
            std::to_string((int)getpid()) + ".log";
        std::ofstream f(path);
        if (f.is_open()) {
            f << r.driver << r.last;
            for (const std::string &s : r.samples)
                f << s;
            f << r.output;
        }
        return path;
    }
};

double SongSecs(const std::string &sample) {
    size_t at = sample.find("\"songSeconds\":");
    return at != std::string::npos ? atof(sample.c_str() + at + 14) : -1.0;
}

class ReacquiredDancerTest : public PerformFixture {};
class SecondPersonPerformTest : public PerformFixture {};

} // namespace

TEST_F(ReacquiredDancerTest, ADancerWithANewIdIsBoundAndScored) {
    PerformRun r = RunPerform("--people 1 --jump-at 20", 240);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;

    // Score just before the jump, and whether she is on her new id (101)
    // afterwards.  Judged 30..140 s: YMCA's last move ends before 140.
    long before = -1;
    int after = 0, onNewId = 0, p2Bound = 0;
    for (const std::string &s : r.samples) {
        double song = SongSecs(s);
        if (song > 0.0 && song < 20.0)
            before = Field(s, 0, "score");
        if (song < 30.0 || song > 140.0)
            continue;
        after++;
        if (Field(s, 0, "trackingId") == 101)
            onNewId++;
        if (Field(s, 1, "trackingId") > 0)
            p2Bound++;
    }
    ASSERT_GE(before, 0) << "no sample before the jump; " << where;
    ASSERT_GE(after, 5) << "the song was not sampled after the jump; " << r.driver << where;
    EXPECT_EQ(onNewId, after) << "player 1 bound to her new tracking id in only " << onNewId
                              << " of " << after << " samples; " << where;
    EXPECT_EQ(p2Bound, 0) << "player 2 bound in a one-player mode; " << where;

    int rebind0, rebind1;
    r.Rebinds(rebind0, rebind1);
    EXPECT_GE(rebind0, 2) << "the auto-assign never re-bound her (no counter, or no jump); "
                          << where;
    EXPECT_LE(rebind0, 6) << "player 1 was re-bound " << rebind0
                          << " times: the chooser unbinds her every frame; " << where;
    EXPECT_EQ(rebind1, 0) << where;

    EXPECT_FALSE(r.Entered("perform_pause_screen")) << where;
    ASSERT_TRUE(r.Entered("perform_endgame_screen")) << r.driver << where;
    long final = Field(r.last, 0, "score");
    EXPECT_GT(final, before + 500000) << "scoring stopped at the jump: " << before << " -> "
                                      << final << "; " << where;
}

TEST_F(SecondPersonPerformTest, AnUnjoinedSecondPersonDoesNotTakeTheSong) {
    PerformRun r = RunPerform("--people 2 --second-person-from 20", 240);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;

    // Judged from her arrival (20 s, +5 s for the sensor and the filters) to
    // 140 s.  YMCA's last move ends before that; after it the image itself may
    // bind a person standing still, as it does in the menus (measured: at
    // ~150 s, by the chooser, with the auto-assign counter still 0 for player 2).
    int p2Bound = 0, afterArrival = 0;
    std::set<long> p1Ids;
    for (const std::string &s : r.samples) {
        double song = SongSecs(s);
        if (song < 25.0 || song > 140.0)
            continue;
        afterArrival++;
        if (Field(s, 1, "trackingId") > 0)
            p2Bound++;
        p1Ids.insert(Field(s, 0, "trackingId"));
    }
    ASSERT_GE(afterArrival, 5) << "the song was not sampled after the second person "
                                  "arrived; " << r.driver << where;
    EXPECT_EQ(p2Bound, 0) << "player 2 bound in " << p2Bound << " of " << afterArrival
                          << " song samples with nobody having joined; " << where;
    EXPECT_EQ(p1Ids.size(), 1u) << "player 1's tracking id changed during the song "
                                   "(active-player swaps); " << where;
    EXPECT_GT(*p1Ids.begin(), 0) << where;

    int rebind0, rebind1;
    r.Rebinds(rebind0, rebind1);
    EXPECT_GE(rebind0, 1) << "no auto-assign counter in the log; " << where;
    EXPECT_LE(rebind0, 3) << "player 1 was re-bound " << rebind0 << " times; " << where;
    EXPECT_EQ(rebind1, 0) << "player 2 auto-assigned in a one-player mode; " << where;

    EXPECT_FALSE(r.Entered("perform_pause_screen")) << where;
    EXPECT_TRUE(r.Entered("perform_endgame_screen")) << r.driver << where;
    EXPECT_GT(Field(r.last, 0, "score"), 0) << "player 1 not scored: " << r.last << where;
}
