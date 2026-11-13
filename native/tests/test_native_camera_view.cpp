// The player view from a camera stream (PLAN P-4, branch np-player-view).
//
// On a 360 the cursor panel's two StreamRenderers (cursor.milo
// depth_buffer_left/right.sr, display mode 4) draw the Kinect's depth|player
// stream into depth_buffer_left/right.tex, the box under the score that shows
// the player's silhouette in menus and in gameplay.  Natively the webcam
// stands in for the Kinect: the pose server (protocol v3) or the page sends a
// picture and a person mask, CameraStream_Native turns them into the Kinect's
// two image streams, a LiveCameraInput stand-in hands them to the decompiled
// LiveCameraInput / StreamRenderer code, and the native DrawToTexture composes
// what the shaders would and uploads it into the box's render target.
//
// Each fixture is ONE engine run (ctest runs each test in its own process),
// headless with no GPU -- StreamRenderer::DrawShowing runs without a device, and
// GET /api/camera answers from the CPU copy of each upload:
//
//   CameraViewTest   scripts/synthetic_kinect.py stands one person 0.6 m to
//                    their right of the camera axis and sends protocol v3
//                    with a 320x240 picture + mask (scripts/camera_fixture.py).
//                    At main_screen every depth_buffer_*.tex the game drew must
//                    contain THAT mask, sampled on a 32x24 grid -- mirrored,
//                    because a Kinect frame is a mirror view and the wire
//                    picture is not (CameraStream_Native.h, "Orientation") --
//                    and at least one box must show the player visibly.
//   NoCameraViewTest the same sensor without --frames (protocol v2, what an
//                    old pose server sends): no stream, no stand-in, nothing
//                    drawn -- the pre-P-4 behaviour.
//
// The expected mask comes from camera_fixture.py --grid, the same renderer the
// sensor uses, so the assertion is "the picture that went in is the picture
// the game drew", not a constant captured from a run.  Sabotage-checked (see
// the commit): dropping the player index in ConvertDepth, ignoring the player
// in NativeStreamCompose, removing the mirror, and not parsing the v3 block
// each fail CameraViewTest.  Gated by DC3_DTA_FLOW_TESTS=1; processes are
// killed by PID; every wait is bounded.

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
#include <vector>
#include "platform/ExecutablePath.h"

namespace {

const int kGridW = 32, kGridH = 24;
const char *kOffset = "0.6";

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

// Same scheme as test_native_twoplayer.cpp: 20000-29999, below the ephemeral
// range, pid-spread, each candidate test-bound.
int FreePort() {
    for (int attempt = 0; attempt < 200; attempt++) {
        int port = 20000 + (int)(((unsigned)getpid() * 41u + (unsigned)attempt * 103u) % 10000u);
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

struct ViewRun {
    std::string setupError;
    std::string output; // engine log
    std::string driver; // DRIVER: lines
    std::string camera; // GET /api/camera?grid=32x24
    bool Entered(const char *screen) const {
        return output.find(std::string("Screen '") + screen + "' Enter") != std::string::npos;
    }
};

// One engine run to main_screen with the synthetic sensor; `frames` adds
// --frames 320x240 (protocol v3).
ViewRun RunToMainScreen(bool frames) {
    ViewRun r;
    std::string dir = BinaryDir();
    std::string bin = dir + "/dc3-native";
    std::string repo = dir + "/../..";
    std::string sensor = repo + "/scripts/synthetic_kinect.py";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(sensor)) {
        r.setupError = "missing " + sensor;
        return r;
    }
    if (system("python3 -c 'import json, socket, struct, numpy' >/dev/null 2>&1") != 0) {
        r.setupError = "python3 with numpy is not runnable; the camera frames need it";
        return r;
    }
    char tmpl[] = "/tmp/dc3-cameraview-XXXXXX";
    char *root = mkdtemp(tmpl);
    if (!root) {
        r.setupError = "mkdtemp failed";
        return r;
    }
    std::string R = root;
    {
        std::ofstream f(R + "/input.txt");
        f << "wait_screen title_screen\n+30 confirm\nwait_screen main_screen\n";
    }
    mkdir((R + "/cache").c_str(), 0755);
    mkdir((R + "/saves").c_str(), 0755);
    int port = FreePort();
    if (port <= 0) {
        r.setupError = "could not get a free loopback port";
        return r;
    }
    std::string api = "http://127.0.0.1:" + std::to_string(port) + "/api";
    std::string sock = R + "/kinect.sock";
    std::string log = R + "/engine.log";
    std::ostringstream sh;
    sh << "cd " << dir << " || exit 1; "
       << "python3 " << sensor << " --socket " << sock << " --people 1 --offset " << kOffset
       << (frames ? " --frames 320x240" : "") << " > " << R << "/kinect.log 2>&1 & kpid=$!; "
       << "end=$((SECONDS+20)); until [ -S " << sock << " ] || [ $SECONDS -ge $end ]; do sleep 0.2; done; "
       << "[ -S " << sock << " ] || echo 'DRIVER: sensor socket never appeared'; "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " DC3_HTTP=1 DC3_HTTP_PORT=" << port << " MILO_MAX_FRAMES=0"
       << " DC3_CACHE_DIR=" << R << "/cache DC3_SAVE_DIR=" << R << "/saves"
       << " MILO_INPUT_SCRIPT=" << R << "/input.txt"
       << " DC3_POSE=external DC3_POSE_NO_SPAWN=1 DC3_POSE_SOCKET=" << sock
       // the script's `confirm` navigates; with a sensor running the default
       // controller mode (`auto`) would want a gesture -- this test is about
       // the picture, not navigation
       << " DC3_CONTROLLER_MODE=forced"
       << " exec timeout 180 ./dc3-native > " << log << " 2>&1 ) & epid=$!; "
       << "end=$((SECONDS+120)); "
       << "until grep -aq \"Screen 'main_screen' Enter\" " << log
       << " 2>/dev/null || [ $SECONDS -ge $end ] || ! kill -0 $epid 2>/dev/null; do sleep 0.5; done; "
       // a few seconds of frames at main_screen, so the boxes have drawn
       << "sleep 4; "
       << "echo \"DRIVER: stop after ${SECONDS}s, engine alive=$(kill -0 $epid 2>/dev/null && echo 1 || echo 0)\"; "
       << "echo \"CAMERA: $(curl -s -m 10 '" << api << "/camera?grid=" << kGridW << "x" << kGridH
       << "')\"; "
       << "kill $epid 2>/dev/null; sleep 1; kill -9 $epid 2>/dev/null; "
       << "kill $kpid 2>/dev/null; sleep 1; kill -9 $kpid 2>/dev/null; "
       << "cat " << log;
    std::istringstream all(RunCapture(sh.str()));
    std::string line;
    while (std::getline(all, line)) {
        if (!line.compare(0, 7, "DRIVER:"))
            r.driver += line + "\n";
        else if (!line.compare(0, 7, "CAMERA:"))
            r.camera += line.substr(7);
        else
            r.output += line + "\n";
    }
    return r;
}

// The value after "key": in `json` (from `from`), as a raw token.
std::string Token(const std::string &json, const char *key, size_t from = 0) {
    std::string k = std::string("\"") + key + "\":";
    size_t at = json.find(k, from);
    if (at == std::string::npos)
        return "";
    at += k.size();
    size_t end = json.find_first_of(",}]", at);
    return json.substr(at, end - at);
}

struct Grid {
    std::string name;
    std::vector<std::string> cells; // kGridW*kGridH "rrggbbaa"
};

std::vector<Grid> Textures(const std::string &json) {
    std::vector<Grid> out;
    size_t at = 0;
    while ((at = json.find("{\"name\":\"", at)) != std::string::npos) {
        Grid g;
        size_t q = at + 9;
        g.name = json.substr(q, json.find('"', q) - q);
        size_t rows = json.find("\"rows\":[", at);
        if (rows == std::string::npos)
            break;
        size_t p = rows + 8;
        for (int y = 0; y < kGridH; y++) {
            size_t a = json.find('"', p), b = json.find('"', a + 1);
            std::string row = json.substr(a + 1, b - a - 1);
            for (int x = 0; x + 8 <= (int)row.size(); x += 8)
                g.cells.push_back(row.substr(x, 8));
            p = b + 1;
        }
        out.push_back(g);
        at = p;
    }
    return out;
}

// camera_fixture.py --grid: the WIRE mask ('.' / ordinal), one row per line.
std::vector<std::string> ExpectedWireMask() {
    std::string repo = BinaryDir() + "/../..";
    std::string out = RunCapture("python3 " + repo + "/scripts/camera_fixture.py --people 1 --offset "
                                 + kOffset + " --grid " + std::to_string(kGridW) + "x"
                                 + std::to_string(kGridH) + " 2>&1");
    std::vector<std::string> rows;
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line))
        if ((int)line.size() == kGridW)
            rows.push_back(line);
    return rows;
}

std::string Dump(const ViewRun &r) {
    std::string path = "/tmp/claude-1000/cameraview_" +
        std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".log";
    std::ofstream f(path);
    if (f.is_open())
        f << r.driver << "CAMERA: " << r.camera << "\n" << r.output;
    return path;
}

class ViewFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
};

class CameraViewTest : public ViewFixture {};
class NoCameraViewTest : public ViewFixture {};

} // namespace

TEST_F(CameraViewTest, TheCursorBoxesDrawThePlayersMaskMirrored) {
    std::vector<std::string> wire = ExpectedWireMask();
    ASSERT_EQ((int)wire.size(), kGridH) << "camera_fixture.py --grid gave no mask";
    int expectedPerson = 0;
    for (auto &row : wire)
        for (char c : row)
            expectedPerson += c != '.';
    // The fixture must be able to tell a mirrored picture from an unmirrored one.
    int symmetric = 0;
    for (auto &row : wire)
        for (int x = 0; x < kGridW; x++)
            symmetric += row[x] != '.' && row[kGridW - 1 - x] != '.';
    ASSERT_GT(expectedPerson, 40);
    ASSERT_LT(symmetric, expectedPerson / 4) << "the fixture person is not off-centre";

    ViewRun r = RunToMainScreen(true);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("main_screen")) << "never reached main_screen; " << r.driver << where;
    EXPECT_NE(r.output.find("camera stream live -> LiveCameraInput stand-in attached"),
              std::string::npos) << where;
    ASSERT_EQ(Token(r.camera, "active"), "true") << r.camera.substr(0, 300) << " " << where;
    EXPECT_EQ(Token(r.camera, "standIn"), "true");
    EXPECT_GT(atoi(Token(r.camera, "latched").c_str()), 10) << "frames are not being latched";

    std::vector<Grid> grids = Textures(r.camera);
    int boxes = 0, visible = 0;
    for (const Grid &g : grids) {
        if (g.name.compare(0, 13, "depth_buffer_"))
            continue;
        boxes++;
        ASSERT_EQ((int)g.cells.size(), kGridW * kGridH) << g.name;
        // The "nobody" colour is what a corner cell holds (no person there).
        const std::string &nobody = g.cells[0];
        int agree = 0, person = 0, personAgree = 0, unmirroredAgree = 0;
        for (int y = 0; y < kGridH; y++) {
            for (int x = 0; x < kGridW; x++) {
                const std::string &c = g.cells[y * kGridW + x];
                // compare RGB only: a box can draw its player with alpha 0 (the
                // other side's box keeps that player's colour transparent)
                bool drawn = c.compare(0, 6, nobody, 0, 6) != 0;
                bool expected = wire[y][kGridW - 1 - x] != '.'; // mirrored
                agree += drawn == expected;
                unmirroredAgree += drawn == (wire[y][x] != '.');
                if (drawn) {
                    person++;
                    // the cell's centre may sit a pixel off the fixture's (the
                    // mirror maps camera column w-1-c) and the box's blur
                    // spreads a pixel: a drawn cell on the mask's edge counts
                    bool nearMask = false;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int yy = y + dy, xx = kGridW - 1 - x + dx;
                            if (yy >= 0 && yy < kGridH && xx >= 0 && xx < kGridW)
                                nearMask |= wire[yy][xx] != '.';
                        }
                    personAgree += nearMask;
                    if (strtoul(c.substr(6, 2).c_str(), nullptr, 16) > 0)
                        visible++;
                }
            }
        }
        float frac = (float)agree / (kGridW * kGridH);
        EXPECT_GE(frac, 0.96f) << g.name << ": the drawn silhouette is not the mask that was "
                               << "sent (" << agree << "/" << kGridW * kGridH << " cells agree, "
                               << "unmirrored would agree on " << unmirroredAgree << ")";
        EXPECT_GE(person, expectedPerson * 8 / 10) << g.name << ": too little of the player drawn";
        EXPECT_EQ(personAgree, person) << g.name << ": player colour away from the mask";
        printf("[camera-view] %s: %d/%d cells agree (unmirrored: %d), %d drawn, %d expected\n",
               g.name.c_str(), agree, kGridW * kGridH, unmirroredAgree, person, expectedPerson);
    }
    EXPECT_EQ(boxes, 2) << "expected depth_buffer_left.tex and depth_buffer_right.tex: "
                        << r.camera.substr(0, 400);
    EXPECT_GT(visible, 0) << "neither box shows the player with any opacity";
}

TEST_F(NoCameraViewTest, AV2SensorDrawsNoPlayerView) {
    ViewRun r = RunToMainScreen(false);
    ASSERT_TRUE(r.setupError.empty()) << r.setupError;
    std::string where = Dump(r);
    ASSERT_TRUE(r.Entered("main_screen")) << "never reached main_screen; " << r.driver << where;
    ASSERT_FALSE(r.camera.empty()) << "no /api/camera answer; " << where;
    EXPECT_EQ(Token(r.camera, "active"), "false") << r.camera;
    EXPECT_EQ(Token(r.camera, "standIn"), "false") << r.camera;
    EXPECT_EQ(Token(r.camera, "uploads"), "0") << r.camera;
    EXPECT_EQ(r.output.find("stand-in attached"), std::string::npos) << where;
}
