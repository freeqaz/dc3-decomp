// The player in freestyle: DepthBuffer3D ("boxyman") in the running game
// (PLAN P-4, branch np-boxyman).
//
// The freestyle section of a Perform song swaps the venue for the visualizer
// and shows the player as a figure of small cubes, takes 16 photos of them and
// shows the photos at the end.  The figure is four DepthBuffer3D objects
// (ui/boxyman/boxyman.milo, two layers per player); the same four draw on the
// loading screen before the song.  Natively DepthBuffer3D's draw was an empty
// stub, so freestyle was the visualizer with nobody in it.
//
// test_depthbuffer3d.cpp pins what the shader port computes.  This file pins
// that the game reaches it: one engine run each (ctest runs each test in its
// own process), headless with no GPU -- the DepthBuffer3D code and the CPU
// vertex shader run without a device, and GET /api/camera's "boxyman" member
// counts what they produced -- on DC3_FAST_TIME, through Better Off Alone
// (scripts/dc3-input-flows/betteroffalone.txt), whose freestyle is beats
// 260..292.
//
//   BoxymanTest          scripts/synthetic_kinect.py with --frames 320x240
//                        (protocol v3: a picture and a person mask).  Cubes
//                        must be drawn on the loading screen, many more draws
//                        with cubes must happen during the song (only
//                        freestyle draws them), the draw must be of game
//                        player 1, and the 16 photos must have been taken.
//   NoCameraBoxymanTest  the same sensor without --frames: no depth stream,
//                        so DepthBuffer3D has nothing to draw -- and must get
//                        through the loading screen without one.
//
// Sabotage-checked (see the commit).  Gated by DC3_DTA_FLOW_TESTS=1;
// processes are killed by PID; every wait is bounded.

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include "platform/ExecutablePath.h"
#include "engine_http_port.h"
#include "shell_timeout.h"

namespace {

const int kEndBeat = 296; // freestyle and its slideshow are over at 292

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

std::string RunCapture(const std::string &cmd) {
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p)
        return out;
    char buf[8192];
    while (fgets(buf, sizeof buf, p))
        out += buf;
    pclose(p);
    return out;
}

struct SongRun {
    std::string setupError;
    std::string output;  // engine log
    std::string driver;  // DRIVER: lines
    std::string loaded;  // GET /api/camera when game_screen was entered
    std::string camera;  // GET /api/camera after the freestyle section
    std::string photos;  // {ui num_snapshots} after the freestyle section
    std::string endBeat; // {taskmgr beat} when the run was stopped
    bool Entered(const char *screen) const {
        return output.find(std::string("Screen '") + screen + "' Enter") != std::string::npos;
    }
};

// One engine run of Better Off Alone with the synthetic sensor; `frames` adds
// --frames 320x240 (protocol v3).  With `throughFreestyle` the song runs to
// kEndBeat, otherwise the run stops when game_screen is entered.
SongRun RunSong(bool frames, bool throughFreestyle) {
    SongRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string repo = dir + "/../..";
    std::string sensor = repo + "/scripts/synthetic_kinect.py";
    std::string flow = repo + "/scripts/dc3-input-flows/betteroffalone.txt";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(sensor) || !Exists(flow)) {
        r.setupError = "missing " + sensor + " or " + flow;
        return r;
    }
    if (system("python3 -c 'import json, socket, struct, numpy' >/dev/null 2>&1") != 0) {
        r.setupError = "python3 with numpy is not runnable; the camera frames need it";
        return r;
    }
    char tmpl[] = "/tmp/dc3-boxyman-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        r.setupError = "mkdtemp failed";
        return r;
    }
    std::string R = root;
    mkdir((R + "/cache").c_str(), 0755);
    mkdir((R + "/saves").c_str(), 0755);
    std::string sock = R + "/kinect.sock";
    std::string log = R + "/engine.log";
    const char *value = "sed 's/.*\"value\":\\([0-9.-]*\\).*/\\1/'";
    std::ostringstream sh;
    sh << kShellTimeoutLookup << "cd " << dir << " || exit 1; "
       << "python3 " << sensor << " --socket " << sock << " --people 1"
       << (frames ? " --frames 320x240" : "") << " --perform-port-file " << EngineHttpPortFile(R)
       << " > " << R << "/kinect.log 2>&1 & kpid=$!; "
       << "end=$((SECONDS+20)); until [ -S " << sock << " ] || [ $SECONDS -ge $end ]; do sleep 0.2; done; "
       << "[ -S " << sock << " ] || echo 'DRIVER: sensor socket never appeared'; "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " DC3_FAST_TIME=1" << EngineHttpPortShellEnv(R) << " MILO_MAX_FRAMES=0"
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " MILO_INPUT_SCRIPT=" << flow
       << " DC3_POSE=external DC3_POSE_NO_SPAWN=1 DC3_POSE_SOCKET=" << sock
       // the flow's presses navigate; with a sensor running the default
       // controller mode (`auto`) would want a gesture
       << " DC3_CONTROLLER_MODE=forced"
       << " exec ${t:+$t 300} ./dc3-native > " << log << " 2>&1 ) & epid=$!; "
       << EngineHttpApiShell(R, "epid", 120)
       // To game_screen.  The run's cache dir is empty, so the first-run
       // Perform tutorial comes up between multiuser_screen and the loading
       // screens; the flow does not know about it -- press through it.
       << "end=$((SECONDS+150)); "
       << "until grep -aq \"Screen 'game_screen' Enter\" " << log
       << " 2>/dev/null || [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do "
       << "case \"$(curl -s -m 5 \"$api/screen\")\" in *tutorial_*) "
       << "curl -s -m 5 -X POST \"$api/input/press\" -d '{\"button\":\"confirm\"}' >/dev/null; "
       << "sleep 1;; *) sleep 0.5;; esac; done; "
       << "echo \"DRIVER: game_screen wait over after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
       << "echo \"LOADED: $(curl -s -m 10 \"$api/camera?grid=2x2\")\"; ";
    if (throughFreestyle) {
        sh << "end=$((SECONDS+150)); b=-1; "
           << "until [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do "
           << "b=$(curl -s -m 5 -X POST \"$api/dta/eval\" -d '{taskmgr beat}' | " << value << "); "
           << "[ \"${b%.*}\" -ge " << kEndBeat << " ] 2>/dev/null && break; sleep 0.3; done; "
           << "echo \"DRIVER: song wait over after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
           << "echo \"BEAT: $b\"; "
           << "echo \"CAMERA: $(curl -s -m 10 \"$api/camera?grid=2x2\")\"; "
           << "echo \"PHOTOS: $(curl -s -m 5 -X POST \"$api/dta/eval\" -d '{ui num_snapshots}' | "
           << value << ")\"; ";
    }
    sh << "kill $epid 2>/dev/null; sleep 1; kill -9 $epid 2>/dev/null; "
       << "kill $kpid 2>/dev/null; sleep 1; kill -9 $kpid 2>/dev/null; "
       << "cat " << log;
    std::istringstream all(RunCapture(sh.str()));
    std::string line;
    while (std::getline(all, line)) {
        if (!line.compare(0, 7, "DRIVER:"))
            r.driver += line + "\n";
        else if (!line.compare(0, 8, "LOADED: "))
            r.loaded += line.substr(8);
        else if (!line.compare(0, 8, "CAMERA: "))
            r.camera += line.substr(8);
        else if (!line.compare(0, 8, "PHOTOS: "))
            r.photos += line.substr(8);
        else if (!line.compare(0, 6, "BEAT: "))
            r.endBeat += line.substr(6);
        else
            r.output += line + "\n";
    }
    return r;
}

// The "boxyman" object of a /api/camera answer, or "".
std::string Boxyman(const std::string &camera) {
    size_t at = camera.find("\"boxyman\":{");
    if (at == std::string::npos)
        return "";
    size_t end = camera.find('}', at);
    return camera.substr(at + 10, end == std::string::npos ? end : end - at - 9);
}

// The number after "key": in `json`, or `missing`.
double Number(const std::string &json, const char *key, double missing = -1.0) {
    std::string k = std::string("\"") + key + "\":";
    size_t at = json.find(k);
    if (at == std::string::npos)
        return missing;
    at += k.size();
    while (at < json.size() && json[at] == '[')
        at++;
    return strtod(json.c_str() + at, nullptr);
}

// pid in the name: a fixed name would be shared by every concurrent run of the gate
std::string Dump(const SongRun &r) {
    std::string path = "/tmp/claude-1000/boxyman_" +
        std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "_" +
        std::to_string((int)getpid()) + ".log";
    std::ofstream f(path);
    if (f.is_open())
        f << r.driver << "LOADED: " << r.loaded << "\nCAMERA: " << r.camera << "\nPHOTOS: "
          << r.photos << "\nBEAT: " << r.endBeat << "\n" << r.output;
    return path;
}

class BoxymanFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
};

class BoxymanTest : public BoxymanFixture {};
class NoCameraBoxymanTest : public BoxymanFixture {};

} // namespace

TEST_F(BoxymanTest, TheLoadingScreenAndFreestyleDrawThePlayerAndTakeSixteenPhotos) {
    SongRun r = RunSong(true, true);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("real_loading_screen")) << "never reached the loading screen; "
                                                  << r.driver << where;
    ASSERT_TRUE(r.Entered("game_screen")) << "never reached game_screen; " << r.driver << where;
    EXPECT_NE(r.output.find("camera stream live -> LiveCameraInput stand-in attached"),
              std::string::npos) << where;

    // The loading screen: the four DepthBuffer3D reach the shader, and the
    // two layers of player 1 have cubes.
    std::string loaded = Boxyman(r.loaded);
    ASSERT_FALSE(loaded.empty()) << "/api/camera has no boxyman member: "
                                 << r.loaded.substr(0, 300) << " " << where;
    const double loadedCubes = Number(loaded, "drawsWithCubes");
    EXPECT_GT(Number(loaded, "draws"), 0) << "DepthBuffer3D never reached its mesh draw; " << where;
    EXPECT_GT(loadedCubes, 10) << "no cubes on the loading screen: " << loaded << " " << where;

    // The song, through freestyle.
    ASSERT_FALSE(r.endBeat.empty()) << where;
    ASSERT_GE(atof(r.endBeat.c_str()), (double)kEndBeat)
        << "the song did not get past freestyle; " << r.driver << where;
    std::string box = Boxyman(r.camera);
    ASSERT_FALSE(box.empty()) << r.camera.substr(0, 300) << " " << where;
    const double cubes = Number(box, "drawsWithCubes");
    // Freestyle shows the figure for ~20 beats: ~1,000 frames on DC3_FAST_TIME
    // (1/120 s a frame), two layers each.
    EXPECT_GT(cubes - loadedCubes, 500)
        << "freestyle drew no player (" << loadedCubes << " draws with cubes before the song, "
        << cubes << " after): " << box << " " << where;
    // What the last of those draws was: a person-sized share of the 64 x 48
    // cells boxyman's 0.8 tile shows, each cube three quads ...
    const double cells = Number(box, "cells");
    EXPECT_GT(cells, 60) << box;
    EXPECT_LT(cells, 64 * 48 / 2) << box;
    EXPECT_EQ(Number(box, "triangles"), cells * 6) << box;
    EXPECT_EQ(Number(box, "vertices"), cells * 12) << box;
    // ... of game player 1 (NUI player 1 -- the only body), nobody as player 2
    EXPECT_EQ(Number(box, "slots"), 1.0) << box;
    size_t dz = box.find("\"depthZoom\":[");
    ASSERT_NE(dz, std::string::npos) << box;
    float nearZ = 0, farZ = 0, wanted = -1;
    ASSERT_EQ(sscanf(box.c_str() + dz + 13, "%f,%f,%f", &nearZ, &farZ, &wanted), 3) << box;
    EXPECT_EQ(wanted, 1.f) << box;
    EXPECT_LT(nearZ, farZ) << box;
    // ... standing out of the grid plane (-y), not collapsed onto it
    size_t mn = box.find("\"min\":[");
    ASSERT_NE(mn, std::string::npos) << box;
    float minX = 0, minY = 0, minZ = 0;
    ASSERT_EQ(sscanf(box.c_str() + mn + 7, "%f,%f,%f", &minX, &minY, &minZ), 3) << box;
    EXPECT_LT(minY, -2.5f) << "the cubes are in the grid plane: " << box;

    // The photos: photo_taker's 16 take_snapshot events during freestyle.
    EXPECT_EQ(r.photos, "16") << "photos taken by the end of freestyle; " << where;
    printf("[boxyman] loading screen: %s\n[boxyman] after freestyle (beat %s): %s photos=%s\n",
           loaded.c_str(), r.endBeat.c_str(), box.c_str(), r.photos.c_str());
}

TEST_F(NoCameraBoxymanTest, WithoutADepthStreamNothingIsDrawn) {
    SongRun r = RunSong(false, false);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("real_loading_screen")) << "never reached the loading screen; "
                                                  << r.driver << where;
    ASSERT_TRUE(r.Entered("game_screen"))
        << "never got through the loading screen without a camera; " << r.driver << where;
    ASSERT_FALSE(r.loaded.empty()) << "no /api/camera answer; " << where;
    EXPECT_EQ(r.output.find("stand-in attached"), std::string::npos) << where;
    std::string box = Boxyman(r.loaded);
    ASSERT_FALSE(box.empty()) << r.loaded.substr(0, 300) << " " << where;
    EXPECT_EQ(Number(box, "draws"), 0) << box;
    EXPECT_EQ(Number(box, "drawsWithCubes"), 0) << box;
}
