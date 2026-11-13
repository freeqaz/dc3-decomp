// Rehearse draws its demonstrator (PLAN G-11, branch np-rehearse, 2026-11-13).
//
// In Rehearse (gameplay_mode practice) the practice camshots hide player0
// through their hide_list, and the dancer the player copies is drawn a second
// way, as a projection: world/shared/chars_base.milo's projection.rndtex
// (TexRenderer: draw player0, cam projection.cam, output projection.tex,
// draw_pre_clear) renders the character into a texture, and
// projection_mesh.mesh shows that texture on a quad parented to the world
// camera, with projection.mat (blend SrcAlpha, alpha 0.5, then two NextPass
// SrcAlpha passes). A PropAnim reveals the quad by keying its vertex positions:
// the original's projection_mesh verts alternate between a thin strip
// (z 39..50) and the full quad (z -40..50) all through the song (measured on
// the original debug.xex under Xenia over the DTA channel, 2026-11-13).
//
// Native drew none of it, for two engine reasons, each enough on its own to
// hide the dancer (measured by building the engine with each fix alone: A only
// 0%, B only <= 2% of the dancer area; both 18-38%):
//   A. the DC3 rndobj shape had kRenderTargetForcesAlphaWrite = false, so
//      projection.tex's alpha stayed at its clear value 0 and the SrcAlpha quad
//      was invisible. The image's SetColorWriteMask (rndobj/Shader.cpp,
//      0x8269E770, 100% matched) writes alpha whenever a target is bound
//      (TheNgRnd.Offscreen()) unless the material pins it (force_alpha_write).
//   B. the engine's RndMesh::DrawShowing lacked the image's
//      `if (geom->Verts().unkc) geom->Sync(0x1f)` (DxMesh::DrawShowing,
//      rnddx9/Mesh.cpp), so the PropAnim's property-set verts never reached the
//      GPU, which kept the first upload: the strip, off the top of the screen.
//
// The test drives the real engine to the Rehearse game_screen (DTA over HTTP,
// the same calls the menus make, so no pad timing and no tutorial), takes
// screenshots across several reveal cycles and counts non-tunnel pixels where
// the dancer stands. The bid tunnel is purple/magenta/black (green channel low
// against red and blue); a character has skin, hair and clothing, which are
// not (kDancerShown below has the measurements).
//
// Route preconditions are asserted separately (game_screen reached,
// use_char_projection on, projection_mesh at the full-quad reveal at least
// once), so a failure says whether the route or the drawing broke.
//
// Gated by DC3_DTA_FLOW_TESTS=1 (drives the real engine as a subprocess, ~1-2
// minutes). Needs a GPU: the screenshot comes from the GPU frame.

#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "engine_http_port.h"
#include "platform/ExecutablePath.h"
#include "test_helpers.h"

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

// Minimal PNG reader for the engine's own screenshots (stb_image_write: 8-bit
// RGBA or RGB, non-interlaced, any row filter). Returns false on anything else.
struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb; // w*h*3
};

unsigned BE32(const unsigned char *p) {
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}

bool DecodePng(const std::string &path, Image &img, std::string &why) {
    std::ifstream f(path, std::ios::binary);
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), {});
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (d.size() < 8 || memcmp(d.data(), sig, 8)) {
        why = "not a PNG (" + std::to_string(d.size()) + " bytes)";
        return false;
    }
    int depth = 0, ctype = -1, interlace = 0;
    std::vector<unsigned char> z;
    for (size_t p = 8; p + 12 <= d.size();) {
        unsigned len = BE32(&d[p]);
        if (p + 12 + len > d.size())
            break;
        const unsigned char *type = &d[p + 4], *data = &d[p + 8];
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            img.w = (int)BE32(data);
            img.h = (int)BE32(data + 4);
            depth = data[8];
            ctype = data[9];
            interlace = data[12];
        } else if (!memcmp(type, "IDAT", 4)) {
            z.insert(z.end(), data, data + len);
        }
        p += 12 + len;
    }
    int bpp = ctype == 6 ? 4 : ctype == 2 ? 3 : 0;
    if (depth != 8 || !bpp || interlace || img.w <= 0 || img.h <= 0) {
        why = "unsupported PNG (depth " + std::to_string(depth) + ", colour type " +
            std::to_string(ctype) + ")";
        return false;
    }
    size_t stride = (size_t)img.w * bpp;
    std::vector<unsigned char> raw((stride + 1) * img.h);
    uLongf rawLen = raw.size();
    if (uncompress(raw.data(), &rawLen, z.data(), z.size()) != Z_OK || rawLen != raw.size()) {
        why = "PNG inflate failed";
        return false;
    }
    std::vector<unsigned char> prev(stride, 0), cur(stride);
    img.rgb.resize((size_t)img.w * img.h * 3);
    for (int y = 0; y < img.h; y++) {
        const unsigned char *row = &raw[y * (stride + 1)];
        int filter = row[0];
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i],
                c = i >= (size_t)bpp ? prev[i - bpp] : 0, x = row[1 + i];
            switch (filter) {
            case 0: break;
            case 1: x += a; break;
            case 2: x += b; break;
            case 3: x += (a + b) / 2; break;
            case 4: {
                int pp = a + b - c, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
                x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                break;
            }
            default: why = "bad PNG row filter"; return false;
            }
            cur[i] = (unsigned char)x;
        }
        for (int px = 0; px < img.w; px++)
            for (int ch = 0; ch < 3; ch++)
                img.rgb[((size_t)y * img.w + px) * 3 + ch] = cur[(size_t)px * bpp + ch];
        prev.swap(cur);
    }
    return true;
}

// Fraction of the dancer's screen area (x 52..64%, y 25..75% of the frame:
// between the move cards and the boombox, above the floor ring) holding pixels
// the tunnel does not have.
double DancerFraction(const Image &img) {
    int x0 = img.w * 52 / 100, x1 = img.w * 64 / 100;
    int y0 = img.h * 25 / 100, y1 = img.h * 75 / 100;
    long hit = 0, total = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const unsigned char *p = &img.rgb[((size_t)y * img.w + x) * 3];
            int r = p[0], g = p[1], b = p[2];
            // Not tunnel: green holds up against red and blue. Not a flare:
            // the venue's lens flares are white-to-pink (red and blue both
            // high and close), and one crossing this area read 16.8% before
            // they were excluded.
            bool flare = r >= 170 && b >= 170 && abs(r - b) < 40;
            if (g >= 80 && g * 10 >= 6 * std::max(r, b) && !flare)
                hit++;
            total++;
        }
    return total ? (double)hit / total : 0.0;
}

constexpr int kShots = 8;
// A shot shows the dancer when this much of the area is character-coloured.
// Measured: fixed engine 17.8..39.2% in 4..6 of 8 shots; stock engine and
// either fix alone 0.0%, except one shot with a lens flare in the area (3.2%).
// The test asks for two shots, so one stray effect cannot pass it.
constexpr double kDancerShown = 0.10;

struct EngineRun {
    std::string setupError;
    std::string output;   // engine log + driver lines
    std::string driver;   // DRIVER: lines only
    std::string shotDir;
};

// Boot to main_screen (idle-main.txt: title confirm, then nothing), then run
// `drive`, a shell fragment with these helpers: scr (current screen), ev DTA
// (POST /api/dta/eval), waitfor SECONDS SCREEN..., shot NAME (screenshot to
// $shots/NAME.png). Each run has its own empty persistence root and the
// port the engine bound. Bounded throughout; the engine is killed by PID, never by pattern.
EngineRun RunEngine(const std::string &drive) {
    EngineRun r;
    std::string bin = BinaryDir() + "/dc3-native";
    std::string script = BinaryDir() + "/../../scripts/dc3-input-flows/idle-main.txt";
    if (!Exists(bin)) {
        r.setupError = "dc3-native does not exist at " + bin + " (scripts/native_test.sh builds it)";
        return r;
    }
    if (!Exists(script)) {
        r.setupError = "input flow script missing: " + script;
        return r;
    }
    // Inside this process's scratch dir (removed when it exits); one dir per
    // engine run.
    const std::string &scratch = TestScratchDir();
    if (scratch.empty()) {
        r.setupError = "no per-process scratch dir";
        return r;
    }
    std::string tmpl = scratch + "/engine-XXXXXX";
    std::vector<char> dirBuf(tmpl.begin(), tmpl.end());
    dirBuf.push_back('\0');
    if (!mkdtemp(dirBuf.data())) {
        r.setupError = "mkdtemp failed under " + scratch;
        return r;
    }
    std::string dir(dirBuf.data());
    mkdir((dir + "/cache").c_str(), 0755);
    mkdir((dir + "/saves").c_str(), 0755);
    mkdir((dir + "/shots").c_str(), 0755);
    r.shotDir = dir + "/shots";
    std::string log = dir + "/engine.log";
    std::ostringstream sh;
    sh << "cd " << BinaryDir() << " && "
       << "( MILO_HEADLESS=1 MILO_RENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << EngineHttpPortShellEnv(dir) << " MILO_MAX_FRAMES=0"
       << " DC3_CACHE_DIR=" << dir << "/cache DC3_SAVE_DIR=" << dir << "/saves"
       << " MILO_INPUT_SCRIPT=" << script
       << " timeout 420 ./dc3-native > " << log << " 2>&1 & echo $! > " << log << ".pid ); "
       // The engine binds its own port and reports it (engine_http_port.h).
       << "pid=$(cat " << log << ".pid); " << EngineHttpApiShell(dir, "pid", 180)
       << "shots=" << r.shotDir << "; "
       << "scr() { curl -s -m 5 $api/screen | sed -n 's/.*\"screen\":\"\\([^\"]*\\)\".*/\\1/p'; }; "
       << "ev() { curl -s -m 30 -X POST --data-binary \"$1\" $api/dta/eval; }; "
       << "shot() { curl -s -m 60 $api/screenshot -o $shots/$1.png; }; "
       << "waitfor() { end=$((SECONDS+$1)); shift; while [ $SECONDS -lt $end ] && kill -0 $pid 2>/dev/null; do"
       << "  s=$(scr); for w in \"$@\"; do [ \"$s\" = \"$w\" ] && return 0; done; sleep 1; done; return 1; }; "
       << "waitfor 180 main_screen; echo \"DRIVER: main=$(scr)\"; sleep 2; "
       << drive
       << "kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; "
       << "cat " << log << "; rm -f " << log << " " << log << ".pid";
    FILE *p = popen(sh.str().c_str(), "r");
    if (!p) {
        r.setupError = "popen failed";
        return r;
    }
    char buf[4096];
    while (fgets(buf, sizeof buf, p)) {
        r.output += buf;
        if (!strncmp(buf, "DRIVER:", 7))
            r.driver += buf;
    }
    pclose(p);
    return r;
}

// The calls choose_mode's item and enter_gameplay make, minus the tutorial
// branch (a fresh persistence root has seen no tutorial): setup_game_mode,
// then straight to loading_screen. A DTA '' inside the shell's single quotes
// is spelled '\'''\''.
std::string EnterGameplay(const char *song, const char *mode) {
    std::ostringstream d;
    d << "echo \"DRIVER: setup $(ev '{setup_game_mode " << song
      << " '\\'''\\'' ((kDifficultyEasy)) " << mode << "}')\"; "
      << "echo \"DRIVER: load $(ev '{do {hamprovider set ui_nav_mode loading} {initialize_gameplay_data}"
         " {ui force_letterbox_off_immediate} {ui goto_screen loading_screen}}')\"; ";
    return d.str();
}

// Rehearse: Ice Ice Baby, then practice_welcome's `practice_all` (game_screen).
EngineRun RunRehearse() {
    std::ostringstream d;
    d << EnterGameplay("iceicebaby", "practice")
      << "waitfor 180 practice_welcome_screen tutorial_practice_screen_0 game_screen;"
         " echo \"DRIVER: shell=$(scr)\"; sleep 2; "
      << "echo \"DRIVER: start $(ev '{do {hamprovider set byo_bid FALSE}"
         " {practice_welcome_panel set show_perform_shortcut TRUE} {ui force_letterbox_off}"
         " {ui goto_screen game_screen}}')\"; "
      << "waitfor 120 game_screen; echo \"DRIVER: game=$(scr)\"; "
      << "W='{set $w {$hamdirector get cur_world}}{set $m {$w find projection_mesh.mesh}}'; "
      << "for i in $(seq 1 " << kShots << "); do sleep 2.5; shot s$i; "
      << "  echo \"DRIVER: state $i $(ev \"{do $W\"' {sprint \"proj=\" {hamprovider get use_char_projection}"
         " \" grp=\" {{$w find projection.grp} showing} \" v4z=\" {$m get (verts 4 pos z)}}}')\"; "
      << "done; ";
    return RunEngine(d.str());
}

constexpr int kCrowdPairs = 4;

// Perform: each pair is a screenshot with the venue's crowds as they are, then
// one with every WorldCrowd hidden, then the crowds restored.
EngineRun RunPerformCrowd(const char *song) {
    std::ostringstream d;
    d << EnterGameplay(song, "perform")
      << "waitfor 180 game_screen; echo \"DRIVER: game=$(scr)\"; sleep 4; "
      << "echo \"DRIVER: crowds $(ev '{do ($n 0) {{$hamdirector get cur_world} iterate WorldCrowd $c"
         " {set $n {+ $n 1}}} $n}')\"; "
      << "for i in $(seq 1 " << kCrowdPairs << "); do sleep 2; shot a$i; "
      << "  ev '{{$hamdirector get cur_world} iterate WorldCrowd $c {$c set_showing FALSE}}' > /dev/null; "
      << "  shot b$i; "
      << "  ev '{{$hamdirector get cur_world} iterate WorldCrowd $c {$c set_showing TRUE}}' > /dev/null; "
      << "done; ";
    return RunEngine(d.str());
}

} // namespace

class RehearseProjectionTest : public ::testing::Test {
protected:
    static EngineRun sRun;
    static bool sRan;
    static void SetUpTestSuite() {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            return;
        sRun = RunRehearse();
        sRan = true;
    }
    void SetUp() override {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        if (!sRan)
            GTEST_SKIP() << "engine did not run";
        if (!sRun.setupError.empty())
            GTEST_FAIL() << sRun.setupError;
    }
    bool Driver(const char *s) const { return sRun.driver.find(s) != std::string::npos; }
};
EngineRun RehearseProjectionTest::sRun;
bool RehearseProjectionTest::sRan = false;

// The route: game_screen in practice mode, the projection switched on by
// practice.dta, and the reveal PropAnim moving the quad to its full extent.
TEST_F(RehearseProjectionTest, RouteReachesTheProjectionReveal) {
    ASSERT_TRUE(Driver("DRIVER: game=game_screen")) << sRun.driver << "\n--- tail ---\n"
        << sRun.output.substr(sRun.output.size() > 4000 ? sRun.output.size() - 4000 : 0);
    EXPECT_TRUE(Driver("proj=1")) << "use_char_projection never on\n" << sRun.driver;
    EXPECT_TRUE(Driver("grp=1")) << "projection.grp never showing\n" << sRun.driver;
    EXPECT_TRUE(Driver("v4z=-40.00"))
        << "projection_mesh never at the full-quad reveal (verts 4 z -40)\n" << sRun.driver;
}

// The drawing: the dancer is on screen in at least two of the shots (the
// reveal is off for part of every cycle, in the original too).
TEST_F(RehearseProjectionTest, DancerIsDrawnThroughTheProjection) {
    if (!Driver("DRIVER: game=game_screen"))
        GTEST_FAIL() << "route did not reach game_screen\n" << sRun.driver;
    std::string fractions;
    int decoded = 0, showing = 0;
    for (int i = 1; i <= kShots; i++) {
        std::string path = sRun.shotDir + "/s" + std::to_string(i) + ".png";
        Image img;
        std::string why;
        if (!DecodePng(path, img, why)) {
            fractions += " s" + std::to_string(i) + "=(" + why + ")";
            continue;
        }
        decoded++;
        double f = DancerFraction(img);
        if (f >= kDancerShown)
            showing++;
        char b[64];
        snprintf(b, sizeof b, " s%d=%.1f%%", i, f * 100.0);
        fractions += b;
    }
    if (decoded == 0) {
        std::string tail = sRun.output.substr(sRun.output.size() > 3000 ? sRun.output.size() - 3000 : 0);
        if (tail.find("Null backend") != std::string::npos ||
            sRun.output.find("GPU initialization failed") != std::string::npos)
            GTEST_SKIP() << "GPU unavailable; no screenshot to inspect";
        GTEST_FAIL() << "no screenshot decoded:" << fractions << "\n" << tail;
    }
    EXPECT_GE(showing, 2) << "the Rehearse dancer is not on screen (dancer-area fraction per"
                             " shot:" << fractions << ")\n" << sRun.driver;
    printf("Rehearse dancer-area fraction per shot:%s\n", fractions.c_str());
}

// ---------------------------------------------------------------------------
// Perform: the venue crowd adds no white figures (G-11 follow-up, G-17).
//
// The alpha-write half of the G-11 engine fix made every crowd impostor a
// solid white human figure in Perform (alley venue over the street, roller
// rink over the dancers' bodies): the native crowd draw relied on the impostor
// target's alpha staying 0, and the engine has no DC3 colour modulation for
// the palette-packed impostor. The Rehearse test never looks at Perform, so it
// could not see this. Whole-frame white counts cannot tell the figures from
// the alley's steam and lens flares (stock engine 4.5% vs a bad rink frame
// 2.2%), so each pair of shots differs only by whether the crowds are shown:
// whatever white the crowd adds is the crowd's.
//
// White is min channel >= 250; the limits and their measurements are at
// kAlleyWhiteMax.
namespace {

double WhiteFraction(const Image &img) {
    long hit = 0;
    size_t n = (size_t)img.w * img.h;
    for (size_t i = 0; i < n; i++) {
        const unsigned char *p = &img.rgb[i * 3];
        if (p[0] >= 250 && p[1] >= 250 && p[2] >= 250)
            hit++;
    }
    return n ? (double)hit / n : 0.0;
}

// Fraction of the frame the crowds may turn white, per venue, judged on the
// SECOND-largest of the 4 pairs: the HUD and the dancers change between the
// two shots of a pair (a "Ready?" card becomes a count), so one pair can catch
// a transition. Measured 2026-11-13 (largest / second-largest pair):
//                                    alley (iceicebaby)      rink (ymca)
//   fixed, engine cb124a7 or 4e738fe,
//   stock or patched (9 runs)        <= 0.476 / <= 0.397%    <= 0.519 / <= 0.105%
//   impostors drawn, no DC3 colour
//   mod                              4.260 / 3.965%          0.899 / 0.741%
//   additive workaround + alpha
//   written (the reported regression) 6.627 / 5.702%         1.099 / 0.890%
// The 0.519% rink pair is the one transition seen; the next pair of that run
// read 0.049%.
constexpr double kAlleyWhiteMax = 0.015;
constexpr double kRinkWhiteMax = 0.004;

} // namespace

class PerformCrowdTest : public ::testing::Test {
protected:
    void Check(const char *song, double whiteMax) {
        if (!getenv("DC3_DTA_FLOW_TESTS"))
            GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
        EngineRun run = RunPerformCrowd(song);
        if (!run.setupError.empty())
            GTEST_FAIL() << run.setupError;
        ASSERT_NE(run.driver.find("DRIVER: game=game_screen"), std::string::npos)
            << run.driver << "\n--- tail ---\n"
            << run.output.substr(run.output.size() > 4000 ? run.output.size() - 4000 : 0);
        // A venue with no crowd would pass without testing anything.
        int crowds = -1;
        size_t at = run.driver.find("DRIVER: crowds ");
        if (at != std::string::npos) {
            size_t v = run.driver.find("\"value\":", at);
            size_t eol = run.driver.find('\n', at);
            if (v != std::string::npos && v < eol)
                crowds = atoi(run.driver.c_str() + v + 8);
        }
        printf("%s: %d WorldCrowd in the venue\n", song, crowds);
        ASSERT_GE(crowds, 1) << song << ": no WorldCrowd in the venue, nothing to test\n"
                             << run.driver;
        std::string pairs;
        std::vector<double> added;
        int decoded = 0;
        for (int i = 1; i <= kCrowdPairs; i++) {
            Image a, b;
            std::string why;
            if (!DecodePng(run.shotDir + "/a" + std::to_string(i) + ".png", a, why) ||
                !DecodePng(run.shotDir + "/b" + std::to_string(i) + ".png", b, why)) {
                pairs += " " + std::to_string(i) + "=(" + why + ")";
                continue;
            }
            decoded++;
            double add = WhiteFraction(a) - WhiteFraction(b);
            added.push_back(add);
            char buf[64];
            snprintf(buf, sizeof buf, " %d=%+.3f%%", i, add * 100.0);
            pairs += buf;
        }
        if (decoded == 0) {
            if (run.output.find("Null backend") != std::string::npos ||
                run.output.find("GPU initialization failed") != std::string::npos)
                GTEST_SKIP() << "GPU unavailable; no screenshot to inspect";
            GTEST_FAIL() << "no screenshot pair decoded:" << pairs;
        }
        printf("%s: white the crowds add, per pair:%s\n", song, pairs.c_str());
        ASSERT_GE(added.size(), 2u) << "fewer than 2 screenshot pairs decoded:" << pairs;
        std::sort(added.begin(), added.end(), std::greater<double>());
        EXPECT_LT(added[1], whiteMax)
            << song << ": the venue crowd draws white figures (white fraction the crowds add,"
                       " judged on the second-largest pair; per pair:" << pairs << ")\n" << run.driver;
    }
};

// Alley (crowd on the street behind the dancers).
TEST_F(PerformCrowdTest, AlleyCrowdAddsNoWhiteFigures) { Check("iceicebaby", kAlleyWhiteMax); }
// Roller rink (the crowd's figures landed on the dancers' bodies).
TEST_F(PerformCrowdTest, RollerRinkCrowdAddsNoWhiteFigures) { Check("ymca", kRinkWhiteMax); }
