// Two-player modes on the native sensor (branch np-twoplayer, PLAN G-1).
//
// The 2026-11-13 desktop survey saw Dance Battle go to dancebattle_pause_screen
// at about beat 4 and inferred that the native sensor feeds only slot 0.  That
// run had NO pose provider: the standing dummy filled slot 0, player 2 was
// never bound, and the image's Game::CheckForSkeletonLoss (requires_2_players
// -> two players needed) did what it does on a 360 when one person stands in
// front of the Kinect -- dance_battle.dta's pause_on_skeleton_loss pauses from
// beat 4.  The pose providers were never limited to one slot.
//
// These three runs pin the behaviour per head-count, each one engine run of
// Dance Battle on YMCA (scripts/dc3-input-flows/battle-duo.txt), headless with
// no GPU and DC3_FAST_TIME, with scripts/synthetic_kinect.py as the sensor:
//
//   TwoPlayerBattleTest       two performing people -> the whole song, results
//                             screen, both players bound to different tracking
//                             ids and both scored, never paused.
//   OnePersonBattleTest       one person -> dancebattle_pause_screen, as on the
//                             360.  The no-camera second stand-in must never
//                             supplement a running provider.
//   NoCameraBattleTest        no provider at all -> the two stand-in dummies
//                             (NativeDummyPlayerCount) are both bound and the
//                             song reaches results, so Battle is testable with
//                             a controller.
//
// Each fixture has ONE test (ctest runs every test in its own process, so a
// second TEST would re-run the engine).  Gated by DC3_DTA_FLOW_TESTS=1.  The
// engine and the sensor are killed by PID, never by pattern; every wait is
// bounded.

#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform/ExecutablePath.h"

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

// A loopback port for the engine's debug server, from 20000-29999: below the
// kernel's ephemeral range (32768+), because a port handed out by bind(0) was
// measured being taken back by an outgoing connection (the sensor of a
// parallel test talking to its engine) before this engine bound it -- the
// engine then aborts with "port ... already in use".  Starts at a pid-derived
// offset so parallel test processes spread out; each candidate is test-bound.
int FreePort() {
    for (int attempt = 0; attempt < 200; attempt++) {
        int port = 20000 + (int)(((unsigned)getpid() * 37u + (unsigned)attempt * 101u) % 10000u);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            return -1;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)port);
        bool ok = bind(fd, (sockaddr *)&addr, sizeof(addr)) == 0;
        close(fd);
        if (ok)
            return port;
    }
    return -1;
}

// The global-options cache of a console that has seen every tutorial, so no
// tutorial screen sits between multiuser_screen and loading.  Same 0x39 bytes
// and offset (mTutorialsSeen, int at 0x28) as test_dta_flow.cpp's
// SeedGlobalOptions, where the layout is measured.
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

struct BattleRun {
    std::string setupError;
    std::string output;  // engine log
    std::string driver;  // DRIVER: lines
    std::string players; // /api/pose/target at the stop screen
    bool Entered(const char *screen) const {
        std::string needle = std::string("Screen '") + screen + "' Enter";
        return output.find(needle) != std::string::npos;
    }
    // "score":N of player `p` in the /api/pose/target answer; -1 if absent.
    long Field(int p, const char *field) const {
        std::string key = "\"player\":" + std::to_string(p) + ",";
        size_t at = players.find(key);
        if (at == std::string::npos)
            return -1;
        std::string f = std::string("\"") + field + "\":";
        size_t v = players.find(f, at);
        size_t next = players.find("\"player\":", at + key.size());
        if (v == std::string::npos || (next != std::string::npos && v > next))
            return -1;
        return strtol(players.c_str() + v + f.size(), nullptr, 10);
    }
};

// people < 0: no sensor at all (the engine's no-provider dummies).
// Runs until one of `stopScreens` ("a|b") is entered (or the bounded wait runs
// out), then
// records /api/pose/target and kills both processes by PID.
BattleRun RunBattle(int people, const char *stopScreens, int waitSeconds) {
    BattleRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string repo = dir + "/../..";
    std::string script = repo + "/scripts/dc3-input-flows/battle-duo.txt";
    std::string sensor = repo + "/scripts/synthetic_kinect.py";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin +
            " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(script) || !Exists(sensor)) {
        r.setupError = "missing " + script + " or " + sensor;
        return r;
    }
    if (people >= 0 && system("python3 -c 'import json, socket, struct' >/dev/null 2>&1") != 0) {
        r.setupError = "python3 is not runnable; the synthetic sensor needs it";
        return r;
    }
    char tmpl[] = "/tmp/dc3-twoplayer-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        r.setupError = "mkdtemp '/tmp/dc3-twoplayer-XXXXXX' failed";
        return r;
    }
    std::string R = root;
    if (!SeedAllTutorialsSeen(R)) {
        r.setupError = "could not seed the global options cache under " + R;
        return r;
    }
    int port = FreePort();
    if (port <= 0) {
        r.setupError = "could not get a free loopback port";
        return r;
    }
    std::string api = "http://127.0.0.1:" + std::to_string(port) + "/api";
    std::string sock = R + "/kinect.sock";
    std::string log = R + "/engine.log";

    std::ostringstream sh;
    sh << "cd " << dir << " || exit 1; ";
    if (people >= 0) {
        sh << "python3 " << sensor << " --socket " << sock << " --people " << people
           << " --perform " << api << " --record " << R << "/kinect.json"
           << " > " << R << "/kinect.log 2>&1 & kpid=$!; "
           << "end=$((SECONDS+20)); until [ -S " << sock << " ] || [ $SECONDS -ge $end ]; do sleep 0.2; done; "
           << "[ -S " << sock << " ] || echo 'DRIVER: sensor socket never appeared'; ";
    }
    sh << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " DC3_FAST_TIME=1 DC3_HTTP=1 DC3_HTTP_PORT=" << port << " MILO_MAX_FRAMES=0"
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " MILO_INPUT_SCRIPT=" << script;
    if (people >= 0)
        sh << " DC3_POSE=external DC3_POSE_NO_SPAWN=1 DC3_POSE_SOCKET=" << sock;
    sh << " exec timeout " << (waitSeconds + 60) << " ./dc3-native > " << log << " 2>&1 ) & epid=$!; "
       << "end=$((SECONDS+" << waitSeconds << ")); "
       << "until grep -aqE \"Screen '(" << stopScreens << ")' Enter\" " << log
       << " 2>/dev/null || [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do sleep 0.5; done; "
       << "echo \"DRIVER: stop after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
       << "echo \"PLAYERS: $(curl -s -m 5 " << api << "/pose/target)\"; "
       << "kill $epid 2>/dev/null; sleep 1; kill -9 $epid 2>/dev/null; ";
    if (people >= 0)
        sh << "kill $kpid 2>/dev/null; sleep 1; kill -9 $kpid 2>/dev/null; ";
    sh << "cat " << log;
    FILE *p = popen(sh.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    char buf[8192];
    while (fgets(buf, sizeof buf, p)) {
        if (!strncmp(buf, "DRIVER:", 7))
            r.driver += buf;
        else if (!strncmp(buf, "PLAYERS:", 8))
            r.players += buf;
        else
            r.output += buf;
    }
    pclose(p);
    return r;
}

// The image's own lines for "the song is over and the results are up":
// dance_battle's win_screen (config/modes.dta).
const char *kResults = "dancebattle_perform_endgame_screen";
const char *kPause = "dancebattle_pause_screen";
// Stop at whichever comes first, so a wrong outcome fails in seconds rather
// than after the whole bounded wait.
const char *kResultsOrPause = "dancebattle_perform_endgame_screen|dancebattle_pause_screen";

class BattleFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
    static std::string Dump(const BattleRun &r) {
        std::string path = "/tmp/claude-1000/twoplayer_" +
            std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".log";
        std::ofstream f(path);
        if (f.is_open())
            f << r.driver << r.players << r.output;
        return path;
    }
};

class TwoPlayerBattleTest : public BattleFixture {};
class OnePersonBattleTest : public BattleFixture {};
class NoCameraBattleTest : public BattleFixture {};

} // namespace

TEST_F(TwoPlayerBattleTest, BothPerformersPlayTheWholeSongToResults) {
    BattleRun r = RunBattle(2, kResultsOrPause, 240);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;
    EXPECT_FALSE(r.Entered(kPause))
        << "paused with two people in front of the sensor -- player 2 was not reachable; "
        << where;
    ASSERT_TRUE(r.Entered(kResults)) << "the song never reached the results screen; "
                                     << r.driver << where;
    long id0 = r.Field(0, "trackingId"), id1 = r.Field(1, "trackingId");
    EXPECT_GT(id0, 0) << r.players;
    EXPECT_GT(id1, 0) << r.players;
    EXPECT_NE(id0, id1) << "both players bound to one skeleton: " << r.players;
    EXPECT_GT(r.Field(0, "score"), 0) << "player 1 not scored: " << r.players;
    EXPECT_GT(r.Field(1, "score"), 0) << "player 2 not scored: " << r.players;
}

TEST_F(OnePersonBattleTest, PausesAndAsksForTheSecondPlayer) {
    BattleRun r = RunBattle(1, kResultsOrPause, 150);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;
    EXPECT_TRUE(r.Entered(kPause))
        << "one person in a two-player mode must pause, as on the 360 "
           "(Game::CheckForSkeletonLoss); something supplied a second player; " << where;
    EXPECT_FALSE(r.Entered(kResults)) << where;
    EXPECT_GT(r.Field(0, "trackingId"), 0) << r.players;
    EXPECT_LE(r.Field(1, "trackingId"), 0)
        << "player 2 bound with nobody there to bind: " << r.players;
}

TEST_F(NoCameraBattleTest, TwoStandInsReachResults) {
    BattleRun r = RunBattle(-1, kResultsOrPause, 240);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;
    EXPECT_FALSE(r.Entered(kPause))
        << "no camera: the second stand-in (NativeDummyPlayerCount) did not keep "
           "player 2 playing; " << where;
    EXPECT_TRUE(r.Entered(kResults)) << r.driver << where;
    EXPECT_EQ(r.Field(0, "trackingId"), 1) << r.players;
    EXPECT_EQ(r.Field(1, "trackingId"), 2) << r.players;
}
