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
// And one in real time (np-player-identity): the image's recoverer and the
// chooser count UI seconds, which DC3_FAST_TIME does not compress, so who is
// unbound when is only faithful in real time.  The sensor is a replayed pose
// session written here (DC3_POSE_PERFORM=session:, song-synced) -- two people
// in a pose taken from the recorded two-player session:
//   LostPlayerStandInTest    the player (tracker id 5), and from song 12 s a
//                            second person (9) who never raises a hand.
//                            20-24 s the detector loses the player: the song
//                            goes on with the second person standing in, and
//                            when the player is seen again she is bound again
//                            and the stand-in is nobody again.  At 30 s the
//                            detector finds the player 0.55 m to the side as
//                            somebody new (tracker id 7): a new body near
//                            where she stood, bound to her within the
//                            image's recovery second.  Never
//                            paused, player 2 never bound.  Before the image
//                            was told of these bindings (NativeAutoAssignPlayers,
//                            GestureMgr::SetTrackedSkeletons) the chooser undid
//                            the second at once -- the song paused on that
//                            frame -- and after the first was left holding the
//                            stand-in's id as a player id, which it gave to
//                            player 2 with no raised hand.
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
#include "test_tmp.h"
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
    std::string R = MakeTestTempDir("dc3-secondperson"); // removed at exit on a pass
    if (R.empty()) {
        r.setupError = "MakeTestTempDir failed under " + TestTempBase();
        return r;
    }
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

// The pose of one dancer in the recorded two-player session (song 3, first
// frame: 1.4 m from the camera, arms out, feet out of the picture): camera
// space x y z and the confidence 0/1/2.
const float kRecordedPose[20][4] = {
    { -0.0430f, -0.2809f, 1.4338f, 2 }, { -0.0400f, -0.2243f, 1.4581f, 2 },
    { -0.1176f, 0.0427f, 1.3848f, 2 },  { -0.1159f, 0.2071f, 1.3289f, 2 },
    { -0.2616f, 0.0086f, 1.4390f, 2 },  { -0.2943f, -0.1953f, 1.4561f, 1 },
    { -0.4158f, -0.2773f, 1.3767f, 2 }, { -0.4565f, -0.3091f, 1.3512f, 1 },
    { 0.0339f, 0.0444f, 1.3354f, 2 },   { 0.2121f, -0.1325f, 1.3035f, 2 },
    { 0.4003f, -0.2466f, 1.2405f, 2 },  { 0.4453f, -0.2867f, 1.2254f, 2 },
    { -0.1219f, -0.3968f, 1.4987f, 2 }, { -0.3451f, -0.5287f, 1.3572f, 1 },
    { -0.0513f, -0.6138f, 1.5364f, 0 }, { 0.0806f, -0.3591f, 1.3982f, 2 },
    { 0.0588f, -0.4895f, 1.2814f, 2 },  { 0.0622f, -0.6739f, 1.4687f, 0 },
    { -0.0163f, -0.6534f, 1.5395f, 0 }, { 0.0519f, -0.7197f, 1.4710f, 0 },
};

void WritePerson(std::ofstream &out, int trackId, float dx, float dy) {
    out << "P " << trackId << " 1";
    char buf[96];
    for (int j = 0; j < 20; j++) {
        snprintf(buf, sizeof buf, " %.4f %.4f %.4f %d", kRecordedPose[j][0] + dx,
            kRecordedPose[j][1] + dy, kRecordedPose[j][2], (int)kRecordedPose[j][3]);
        out << buf;
    }
    out << "\n";
}

// A pose session (dc3-pose-session v1, PoseSession_Native.cpp), 30 frames a
// second from song -4 s to 50 s, levelled as the recording was: the player
// (tracker id 5, 0.35 m left of the axis) out of the picture from 20 to 24 s,
// and from 30 s under tracker id 7 another 0.55 m to the left (further than
// People takes for the same person at once, nearer than Players' reach half a
// second on); a second person (9, 0.45 m right) from 12 s.
bool WriteStandInSession(const std::string &path) {
    std::ofstream out(path);
    out << "# dc3-pose-session v1\n# test_native_second_person.cpp LostPlayerStandInTest\n";
    int seq = 0;
    for (int i = -4 * 30; i <= 50 * 30; i++, seq++) {
        double song = i / 30.0;
        bool player = song < 20.0 || song >= 24.0;
        bool second = song >= 12.0;
        char head[200];
        snprintf(head, sizeof head, "F %d %d %.6f %.6f %d %.6f 1 1 0.0116923293 0.988781333 -0.148912057 %d\n",
            seq, 2 * seq, 100.0 + song, song, 1000 + seq, 5000.0 + song, (player ? 1 : 0) + (second ? 1 : 0));
        out << head;
        if (player && song < 30.0)
            WritePerson(out, 5, -0.35f, 0.0f);
        else if (player)
            WritePerson(out, 7, -0.90f, 0.0f);
        if (second)
            WritePerson(out, 9, 0.45f, 0.0f);
    }
    return out.good();
}

struct SessionRun {
    std::string setupError;
    std::string output; // engine log
    std::string driver;
    bool Entered(const char *screen) const {
        return output.find(std::string("Screen '") + screen + "' Enter") != std::string::npos;
    }
    // Every "DC3 SCORING: player->slot binding p0=A p1=B" line from `from` on.
    std::vector<std::pair<int, int>> Bindings(size_t from) const {
        std::vector<std::pair<int, int>> v;
        const char *key = "DC3 SCORING: player->slot binding p0=";
        for (size_t at = output.find(key, from); at != std::string::npos; at = output.find(key, at + 1)) {
            int p0 = -9, p1 = -9;
            if (sscanf(output.c_str() + at + strlen(key), "%d p1=%d", &p0, &p1) == 2)
                v.push_back(std::make_pair(p0, p1));
        }
        return v;
    }
    // The song second of the last unpaused gameplay telemetry line.
    double LastPlayingSecs() const {
        std::istringstream ss(output);
        std::string line;
        double best = -1.0;
        while (std::getline(ss, line)) {
            if (line.compare(0, 8, "DC3_TEL:") || line.find("screen=game_screen ") == std::string::npos
                || line.find(" gamePaused=0 ") == std::string::npos)
                continue;
            size_t at = line.find(" realSecs=");
            if (at != std::string::npos)
                best = atof(line.c_str() + at + 10);
        }
        return best;
    }
};

// Real time, headless with no GPU; plays until the pause screen, or gameplay
// telemetry past `stopSecs`, or the bounded wait runs out.
// `logEnv`: " DC3_SCORING_DEBUG=1" for every binding line and the per-second
// counters; "" for the dev profile's own (DC3_BINDING_LOG); " DC3_BINDING_LOG=0"
// for neither.
SessionRun RunStandInSession(double stopSecs, const char *logEnv = " DC3_SCORING_DEBUG=1") {
    SessionRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    std::string R = MakeTestTempDir("dc3-standin"); // removed at exit on a pass
    if (R.empty()) {
        r.setupError = "MakeTestTempDir failed under " + TestTempBase();
        return r;
    }
    if (!SeedAllTutorialsSeen(R)) {
        r.setupError = "could not seed the global options cache under " + R;
        return r;
    }
    std::string session = R + "/two.dc3session";
    if (!WriteStandInSession(session)) {
        r.setupError = "could not write " + session;
        return r;
    }
    std::string route = R + "/route.txt";
    {
        std::ofstream f(route);
        f << kRoute;
    }
    std::string log = R + "/engine.log";
    char stop[32];
    snprintf(stop, sizeof stop, "%.1f", stopSecs);

    std::ostringstream sh;
    sh << kShellTimeoutLookup << "cd " << dir << " || exit 1; "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " MILO_MAX_FRAMES=0 DC3_TEL=1 DC3_TEL_INTERVAL=15 DC3_CONTROLLER_MODE=forced"
       << logEnv
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " MILO_INPUT_SCRIPT=" << route << " DC3_POSE=external"
       << " DC3_POSE_PERFORM=session:" << session << " DC3_POSE_SESSION_SYNC=song"
       << " exec ${t:+$t 200} ./dc3-native > " << log << " 2>&1 ) & epid=$!; "
       << "end=$((SECONDS+170)); "
       << "until grep -aqF \"Screen 'perform_pause_screen' Enter\" " << log << " 2>/dev/null"
       << " || awk -v s=" << stop
       << " '/^DC3_TEL:.*screen=game_screen / { for (i = 1; i <= NF; i++) if ($i ~ /^realSecs=/)"
          " { split($i, a, \"=\"); if (a[2] + 0 >= s) found = 1 } } END { exit !found }' "
       << log << " 2>/dev/null"
       << " || [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do sleep 0.5; done; "
       << "echo \"DRIVER: stop after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
       << "kill $epid 2>/dev/null; sleep 1; kill -9 $epid 2>/dev/null; "
       << "cat " << log;
    FILE *p = popen(sh.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    char buf[8192];
    while (fgets(buf, sizeof buf, p)) {
        if (!strncmp(buf, "DRIVER:", 7))
            r.driver += buf;
        else
            r.output += buf;
    }
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
        std::string path = TestLogPath("second_person"); // removed at exit on a pass
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
class LostPlayerStandInTest : public PerformFixture {};
class BindingLogTest : public PerformFixture {};
class BindingLogSabotageTest : public PerformFixture {};

} // namespace

TEST_F(ReacquiredDancerTest, ADancerWithANewIdIsBoundAndScored) {
    PerformRun r = RunPerform("--people 1 --jump-at 20", 240);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached gameplay; " << r.driver << where;

    // Score just before the jump, and whether she is on her new id (101)
    // afterwards.  Judged 30..140 s: YMCA's last move ends before 140.
    // The tracking id is a person's (Dc3PoseIdentity::People), not the
    // tracker's: the body under tracker id 101 is somebody new at the jump
    // (out of reach, and a new tracker id), nobody else goes by that number,
    // and it is that one person from then on -- through the four times the
    // synthetic sensor stands its one body up 0.5 m from where it danced, or
    // puts it back, in one frame (PoseIdentity_Native.h, ONE BODY ALONE).
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

TEST_F(LostPlayerStandInTest, TheStandInIsNobodyAgainAndTheSongNeverPauses) {
    SessionRun r = RunStandInSession(42.0);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = TestTempFile("second_person_standin"); // removed at exit on a pass
    {
        std::ofstream f(where);
        if (f.is_open())
            f << r.driver << r.output;
    }
    size_t game = r.output.find("Screen 'game_screen' Enter");
    ASSERT_NE(game, std::string::npos) << "never reached gameplay; " << r.driver << where;

    // The run has to contain what it is about, in this order: the second
    // person (tracking id 9) bound for the lost player; the player bound again
    // by her own id and the stand-in released; the player bound to the new
    // body where she stood.
    size_t standIn = r.output.find("DC3 SCORING: bind p0 -> tracking id 9 ", game);
    ASSERT_NE(standIn, std::string::npos)
        << "nobody stood in for the lost player (paused instead?); " << where;
    EXPECT_NE(r.output.find("rule=auto", standIn), std::string::npos) << where;
    size_t back = r.output.find("DC3 SCORING: bind p0 -> tracking id 5 ", standIn);
    ASSERT_NE(back, std::string::npos) << "the player was not bound again when she returned; " << where;
    size_t released = r.output.find("DC3 SCORING: stand-in tracking id 9 released by p0", standIn);
    ASSERT_NE(released, std::string::npos) << where;
    size_t near = r.output.find("DC3 SCORING: bind p0 -> tracking id 7 ", released);
    ASSERT_NE(near, std::string::npos)
        << "the player was not bound to the new body where she stood; " << where;
    EXPECT_NE(r.output.find("rule=near", near), std::string::npos) << where;

    EXPECT_FALSE(r.Entered("perform_pause_screen"))
        << "the song paused with the player or a stand-in in view; " << where;
    EXPECT_GE(r.LastPlayingSecs(), 41.0) << "the song did not play on to the stop; " << r.driver << where;

    // Nobody joined: player 2 is never on a skeleton, and after the stand-in
    // is released the second person's id is never bound again.
    std::vector<std::pair<int, int>> all = r.Bindings(game);
    ASSERT_FALSE(all.empty()) << where;
    for (const std::pair<int, int> &b : all)
        EXPECT_EQ(b.second, -1) << "player 2 on a skeleton (slot " << b.second
                                << ") with nobody having joined; " << where;
    EXPECT_GE(all.back().first, 0) << "player 1 not on a skeleton at the stop; " << where;
    EXPECT_EQ(r.output.find("-> tracking id 9 ", released), std::string::npos)
        << "the stand-in was bound again after she was released; " << where;
}

// The who-is-who lines are in a playtest's log without DC3_SCORING_DEBUG
// (DC3_BINDING_LOG, a dev-profile default; PLAN H-158).  Playtest 2026-11-20:
// somebody walked through a two-player song, and the log held neither the
// bindings nor which body a rating was taken on.
//
// The same recorded session as LostPlayerStandInTest, with nothing set: the
// stand-in, the player's return and her new body are each a line, every
// MOVE_RATING names the body it was taken on, and the per-second counters of
// DC3_SCORING_DEBUG are not there.
TEST_F(BindingLogTest, APlaytestLogSaysWhichBodyEachPlayerWasScoredOn) {
    SessionRun r = RunStandInSession(36.0, "");
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = TestTempFile("binding_log"); // removed at exit on a pass
    {
        std::ofstream f(where);
        if (f.is_open())
            f << r.driver << r.output;
    }
    size_t game = r.output.find("Screen 'game_screen' Enter");
    ASSERT_NE(game, std::string::npos) << "never reached gameplay; " << r.driver << where;
    ASSERT_GE(r.LastPlayingSecs(), 35.0) << "the song did not play on to the stop; " << r.driver << where;

    size_t standIn = r.output.find("DC3 SCORING: bind p0 -> tracking id 9 ", game);
    ASSERT_NE(standIn, std::string::npos) << "the stand-in's binding is not in the log; " << where;
    size_t back = r.output.find("DC3 SCORING: bind p0 -> tracking id 5 ", standIn);
    EXPECT_NE(back, std::string::npos) << "the player's return is not in the log; " << where;
    EXPECT_NE(r.output.find("DC3 SCORING: stand-in tracking id 9 released by p0", standIn),
        std::string::npos)
        << where;
    size_t near = r.output.find("DC3 SCORING: bind p0 -> tracking id 7 ", standIn);
    EXPECT_NE(near, std::string::npos) << "the player's new body is not in the log; " << where;
    EXPECT_NE(r.output.find("DC3 SCORING: slot "), std::string::npos) << where;
    EXPECT_NE(r.output.find("DC3 SCORING: player->slot binding p0="), std::string::npos) << where;

    // Every rating names a body, and they are the player's own two: 5 until
    // she left the picture, 7 where she stood afterwards.
    int ratings = 0, unnamed = 0, onFirst = 0, onNew = 0;
    {
        std::istringstream ss(r.output);
        std::string line;
        while (std::getline(ss, line)) {
            if (line.compare(0, 20, "DC3 MOVE_RATING p=0 "))
                continue;
            ratings++;
            size_t at = line.find(" body=");
            if (at == std::string::npos) {
                unnamed++;
                continue;
            }
            int body = atoi(line.c_str() + at + 6);
            if (body == 5)
                onFirst++;
            if (body == 7)
                onNew++;
        }
    }
    EXPECT_GT(ratings, 0) << "no DC3 MOVE_RATING line for player 1; " << where;
    EXPECT_EQ(unnamed, 0) << "a DC3 MOVE_RATING line without body=; " << where;
    EXPECT_GT(onFirst, 0) << "no rating on tracking id 5, the player until 20 s; " << where;
    EXPECT_GT(onNew, 0) << "no rating on tracking id 7, the player from 30 s; " << where;

    // The developer's floods stay the developer's.
    EXPECT_EQ(r.output.find("DC3 SCORING: newFrames="), std::string::npos) << where;
    EXPECT_EQ(r.output.find("DC3 SCORING: auto-assign rebinds"), std::string::npos) << where;
    EXPECT_EQ(r.output.find("DC3 SCORING: move_passed"), std::string::npos) << where;
}

// The standing negative control: with the log switched off the same run, past
// the stand-in at 20 s, prints none of it -- so the test above is reading the
// default, not something every run prints.
TEST_F(BindingLogSabotageTest, TheSameRunWithTheLogOffSaysNothing) {
    SessionRun r = RunStandInSession(26.0, " DC3_BINDING_LOG=0");
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    size_t game = r.output.find("Screen 'game_screen' Enter");
    ASSERT_NE(game, std::string::npos) << "never reached gameplay; " << r.driver;
    ASSERT_GE(r.LastPlayingSecs(), 25.0) << "the song did not play past the stand-in; " << r.driver;
    EXPECT_EQ(r.output.find("DC3 SCORING:"), std::string::npos)
        << "a DC3 SCORING: line with DC3_BINDING_LOG=0 and no DC3_SCORING_DEBUG";
}
