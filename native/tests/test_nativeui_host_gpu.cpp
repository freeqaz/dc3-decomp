// nativeui_host with the real things: the game's fonts loaded the way a label
// loads them, the engine's canvas, a render target read back -- and the demo
// (DC3_NATIVEUI_DEMO=1) inside a running dc3-native, driven over the debug
// server.
//
// The pure halves (translation, layout, cache, input) are test_nativeui_host.cpp.

#include <gtest/gtest.h>
#include <zlib.h>

#include "engine_http_port.h"
#include "nativeui_raster.h"
#include "test_helpers.h"
#include "test_tmp.h"

#include "gfx/Canvas2D.h"
#include "nativeui/SampleBrowser.h"
#include "nativeui_host/GameFonts.h"
#include "nativeui_host/NativeUiDemo.h"
#include "nativeui_host/NativeUiHost.h"
#include "obj/Object.h"
#include "platform/ExecutablePath.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace nativeui_host;

namespace {

    struct Frame {
        std::vector<uint8_t> px; // RGBA
        int w = 0, h = 0;
        const uint8_t *At(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
    };

    // Pixels of the rectangle (logical 1280x720 units, scaled to the frame)
    // for which `pred(r, g, b)` holds.
    template <class Pred>
    int Count(const Frame &f, float x, float y, float w, float h, Pred pred) {
        const float s = (float)f.h / 720.0f;
        int n = 0;
        for (int py = std::max(0, (int)(y * s)); py < std::min(f.h, (int)((y + h) * s)); py++) {
            for (int px = std::max(0, (int)(x * s)); px < std::min(f.w, (int)((x + w) * s));
                 px++) {
                const uint8_t *p = f.At(px, py);
                if (pred(p[0], p[1], p[2])) n++;
            }
        }
        return n;
    }

    bool Bright(int r, int g, int b) { return r >= 170 && g >= 170 && b >= 170; }

    void SavePng(const Frame &f, const std::string &path) {
        std::vector<unsigned char> rgb;
        rgb.reserve((size_t)f.w * f.h * 3);
        for (int i = 0; i < f.w * f.h; i++) {
            rgb.insert(rgb.end(), &f.px[(size_t)i * 4], &f.px[(size_t)i * 4] + 3);
        }
        nativeui_test::WriteFile(path, nativeui_test::EncodePng(f.w, f.h, rgb));
    }

    constexpr Canvas2DColor kBackdrop = { 16 / 255.0f, 24 / 255.0f, 32 / 255.0f, 1.0f };

    class NativeUiHostGpuTest : public EngineTestFixture {
    protected:
        bool Ready() const { return gWgpuRnd && gWgpuRnd->Gpu().IsReady() && TheCanvas2D(); }

        template <class F>
        bool Render(int w, int h, Frame &out, F &&draw) {
            RndTex *tex = Hmx::Object::New<RndTex>();
            tex->SetBitmap(w, h, 32, RndTex::kRendered, false, nullptr);
            RndCam *cam = Hmx::Object::New<RndCam>();
            cam->SetTargetTex(tex);
            TheRnd.BeginDrawing();
            RndCam *prev = RndCam::Current();
            cam->Select();
            Canvas2D *canvas = TheCanvas2D();
            if (canvas->Begin(1280.0f, 720.0f)) {
                canvas->FillRect({ 0, 0, 1280, 720 }, kBackdrop);
                canvas->End();
            }
            draw();
            cam->SetTargetTex(nullptr);
            if (prev) prev->Select();
            TheRnd.EndDrawing();
            bool ok = ReadbackGpuRenderTarget(tex, out.px, out.w, out.h) && out.w == w
                && out.h == h;
            delete cam;
            delete tex;
            return ok;
        }
    };

} // namespace

// With the switch off the demo is not there at all: nothing installed, no
// font or texture loaded (DemoInstall is what main() calls).
TEST(NativeUiHostDemoSwitch, OffUnlessTheEnvironmentAsks) {
    unsetenv("DC3_NATIVEUI_DEMO");
    DemoInstall();
    EXPECT_FALSE(DemoActive());
    EXPECT_NE(DemoStateJson().find("\"active\":false"), std::string::npos) << DemoStateJson();
    setenv("DC3_NATIVEUI_DEMO", "0", 1);
    DemoInstall();
    EXPECT_FALSE(DemoActive()) << "0 is off";
    unsetenv("DC3_NATIVEUI_DEMO");
}

// The fonts song select draws with, loaded through the game's loader, and
// what was measured from their atlases.  Prints the table the docs quote.
TEST_F(NativeUiHostGpuTest, GameFontsLoadWithMeasuredCells) {
    if (IsSkipped()) return;
    GameFonts fonts;
    ASSERT_TRUE(fonts.Load()) << "ui/resource/fonts/eagle-light(47).milo did not load";
    const GameFont *menu = fonts.Find(nativeui::kDc3MenuFont);
    ASSERT_NE(menu, nullptr);
    for (const GameFontInfo &info : fonts.Table()) {
        printf(
            "FONT %-20s %-36s cell %gx%g texels, %d page(s) %dx%d, %d glyphs, %d kerning "
            "pairs, base kerning %g, cap top %.3f baseline %.3f descender %.3f%s%s\n",
            info.resource.c_str(), info.fontName.c_str(), info.cellWidth, info.cellHeight,
            info.pages, info.pageWidth, info.pageHeight, info.glyphs, info.kerningPairs,
            info.baseKerning, info.capTop, info.baseline, info.descenderBottom,
            info.carriesColour ? " (coloured)" : "",
            info.measuredFromAtlas ? "" : " (ink not measured: defaults)"
        );
    }
    const GameFontInfo &info = menu->Info();
    EXPECT_GT(info.cellWidth, 16.0f);
    EXPECT_GT(info.cellHeight, 16.0f);
    EXPECT_GE(info.pages, 1);
    EXPECT_GT(info.glyphs, 90) << "at least printable ASCII";
    EXPECT_TRUE(info.measuredFromAtlas) << "the atlas texels were not readable";
    EXPECT_LT(info.capTop, info.baseline);
    EXPECT_GT(info.baseline, 0.5f);
    EXPECT_LE(info.baseline, info.descenderBottom);
    const GlyphSource &source = menu->Source();
    for (char32_t c : { U'A', U'Z', U'a', U'z', U'0', U'9', U' ', U'É', U'ö' }) {
        EXPECT_TRUE(source.HasGlyph(c)) << "U+" << std::hex << (unsigned)c;
    }
    // Every glyph's rectangle is inside the atlas and has an area.
    std::vector<GlyphQuad> quads;
    source.Layout(100.0f, 0.0f, "ABCXYZabcxyz0189!?&", quads);
    EXPECT_EQ(quads.size(), 19u);
    for (const GlyphQuad &q : quads) {
        EXPECT_GE(q.page, 0);
        EXPECT_GE(q.u0, 0.0f);
        EXPECT_LE(q.u1, 1.0f);
        EXPECT_GE(q.v0, 0.0f);
        EXPECT_LE(q.v1, 1.0f);
        EXPECT_LT(q.u0, q.u1);
        EXPECT_LT(q.v0, q.v1);
        EXPECT_NE(source.PageTexture(q.page), nullptr);
    }
    // Proportional: an I is narrower than a W.
    EXPECT_LT(source.Advance(100.0f, "I"), source.Advance(100.0f, "W") * 0.6f);
}

// One line of real text through the renderer and the canvas, inside a clip
// narrower than the text: ink inside the clip, none outside it, and the ink
// between the cap line and the baseline the metrics promise.
TEST_F(NativeUiHostGpuTest, RealGlyphsLandOnTheBaselineInsideTheirClip) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    GameFonts fonts;
    ASSERT_TRUE(fonts.Load());
    const float size = 80.0f, baseline = 300.0f, left = 200.0f;
    const nativeui::FontMetrics m = fonts.Fonts().Metrics(nativeui::kDc3MenuFont, size);
    const float wide = fonts.Fonts().AdvanceWidth(nativeui::kDc3MenuFont, size, "HHHHHHHHHH");
    ASSERT_GT(wide, 300.0f);
    Frame f;
    ASSERT_TRUE(Render(1280, 720, f, [&] {
        Canvas2D *canvas = TheCanvas2D();
        ASSERT_TRUE(canvas->Begin(1280.0f, 720.0f));
        Canvas2DSink sink(*canvas);
        CanvasRenderer renderer(sink, fonts.Fonts(), nullptr);
        renderer.SetDeviceScale(1.0f);
        renderer.PushClip({ left, 200, 200, 140 });
        nativeui::TextRun run;
        run.origin = { left, baseline };
        run.font = nativeui::kDc3MenuFont;
        run.size = size;
        run.color = { 1, 1, 1, 1 };
        run.text = "HHHHHHHHHH";
        renderer.Draw(run);
        renderer.PopClip();
        canvas->End();
    }));
    if (const char *dir = getenv("NATIVEUI_HOST_PNG_DIR")) SavePng(f, std::string(dir) + "/glyphs.png");
    const int inside = Count(f, left, 200, 200, 140, Bright);
    EXPECT_GT(inside, 600) << "white glyph ink inside the clip";
    auto touched = [](int r, int g, int b) { return r != 16 || g != 24 || b != 32; };
    EXPECT_EQ(Count(f, left + 201, 180, 600, 180, touched), 0) << "ink right of the clip";
    EXPECT_EQ(Count(f, 0, 180, left - 1, 180, touched), 0) << "ink left of the run's origin";
    // Capitals stand on the baseline and reach the cap line (2 px of slack
    // for the atlas's own antialiasing).
    EXPECT_EQ(Count(f, left, baseline + 3, 200, 30, Bright), 0) << "ink below the baseline";
    EXPECT_EQ(Count(f, left, 200, 200, baseline - m.ascent - 3 - 200, Bright), 0)
        << "ink above the cap line (ascent " << m.ascent << ")";
    EXPECT_GT(Count(f, left, baseline - 6, 200, 6, Bright), 20) << "feet on the baseline";
    EXPECT_GT(Count(f, left, baseline - m.ascent, 200, 6, Bright), 20) << "tops at the cap line";
}

// The sample browser through NativeUiHost::Draw(): real fonts, the engine's
// canvas, a 1280x720 render target.  Regions, not a whole-image hash.
TEST_F(NativeUiHostGpuTest, SampleBrowserThroughTheHostAndCanvas) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    GameFonts fonts;
    ASSERT_TRUE(fonts.Load());
    NativeUiHost host(fonts.Fonts(), nullptr);
    nativeui::SampleBrowser browser({ kLogicalWidth, kLogicalHeight }, fonts.Fonts(), host.Clock());
    host.SetScreen(&browser.GetScreen());
    host.SetOutputSize(1280, 720);
    double now = 10.0;
    host.Enter(now);
    for (int i = 0; i < 3; i++) {
        host.OnButtonDown(kPadDDown, kPadActionDown);
        host.OnButtonUp(kPadDDown, kPadActionDown);
    }
    for (int i = 0; i < 600 && (i < 5 || !browser.GetScreen().AtRest()); i++) {
        host.Tick(now += 1.0 / 60.0);
        browser.BuildFrame();
    }
    ASSERT_TRUE(browser.GetScreen().AtRest());

    Frame f;
    NativeUiHost::FrameStats stats;
    ASSERT_TRUE(Render(1280, 720, f, [&] {
        host.Draw();
        stats = host.Stats();
    }));
    if (const char *dir = getenv("NATIVEUI_HOST_PNG_DIR")) SavePng(f, std::string(dir) + "/browser.png");
    printf(
        "HOST FRAME %u commands, %u quads, %u draw calls, %u glyphs, build %u us, replay %u us\n",
        stats.commands, stats.quads, stats.drawCalls, stats.glyphs, stats.buildMicros,
        stats.replayMicros
    );
    ASSERT_TRUE(stats.drew);
    EXPECT_GT(stats.quads, 200u);
    EXPECT_GT(stats.drawCalls, 0u);
    EXPECT_LT(stats.drawCalls, 120u);
    EXPECT_EQ(stats.placeholderGlyphs, 0u) << "the menu font has every character of the sample";

    // The highlight bar: the accent colour (blue) on the selected row, and
    // not on the row under it.
    const nativeui::Rect bar = browser.SongList().HighlightRect();
    const nativeui::Color accent = browser.GetScreen().GetTheme().color.accent;
    ASSERT_GT(accent.b, accent.r + 0.2f);
    const int bx = (int)bar.x + 60, by = (int)(bar.y + bar.height * 0.5f);
    auto accented = [&](int r, int g, int b) { return b > r + 40 && b > 90; };
    EXPECT_GT(Count(f, bar.x + 300, bar.y + 4, 100, bar.height - 8, accented), 3000)
        << "bar at " << bar.x << "," << bar.y << " " << bar.width << "x" << bar.height
        << "; pixel " << (int)f.At(bx, by)[0] << "," << (int)f.At(bx, by)[1] << ","
        << (int)f.At(bx, by)[2];
    EXPECT_EQ(Count(f, bar.x + 300, bar.Bottom() + 30, 100, 10, accented), 0)
        << "the row under the bar is not highlighted";

    // The screen's title: ink inside the box the measurer gives it, none in
    // the gap after it (the drawn run is as wide as it was measured).
    const nativeui::DisplayList &list = browser.BuildFrame();
    const nativeui::TextRun *title = nullptr;
    const nativeui::RoundRectFill *rounded = nullptr;
    for (const nativeui::Command &command : list.commands) {
        if (const auto *run = std::get_if<nativeui::TextRun>(&command)) {
            if (!title && run->size >= 40.0f) title = run;
        }
        if (const auto *fill = std::get_if<nativeui::RoundRectFill>(&command)) {
            const nativeui::Color &c = fill->colors.topLeft;
            const nativeui::Color &p = browser.GetScreen().GetTheme().color.imagePlaceholder;
            if (!rounded && fill->radius >= 6.0f && fill->softness == 0.0f && c.r == p.r
                && c.g == p.g && c.b == p.b && fill->rect.width >= 100.0f
                && fill->rect.x >= 0.0f && fill->rect.Right() <= 1280.0f) {
                rounded = fill;
            }
        }
    }
    ASSERT_NE(title, nullptr) << "no display-size text run";
    const nativeui::FontMetrics tm = fonts.Fonts().Metrics(title->font, title->size);
    const float titleWidth = fonts.Fonts().AdvanceWidth(title->font, title->size, title->text)
        + title->letterSpacing * (float)(title->text.size() - 1);
    EXPECT_GT(Count(f, title->origin.x, title->origin.y - tm.ascent, titleWidth, tm.ascent, Bright),
              (int)(titleWidth * tm.ascent * 0.04f))
        << "title \"" << title->text << "\" at " << title->origin.x << "," << title->origin.y;
    EXPECT_EQ(Count(f, title->origin.x + titleWidth + 2, title->origin.y - tm.ascent, 12,
                    tm.ascent, Bright), 0)
        << "ink past the measured width";
    EXPECT_EQ(Count(f, title->origin.x - 14, title->origin.y - tm.ascent, 12, tm.ascent, Bright), 0)
        << "ink before the origin";

    // A rounded corner: an image placeholder tile (every third sample song
    // has no art).  Its top edge is the tile's colour; its very corner is not.
    ASSERT_NE(rounded, nullptr) << "no rounded placeholder tile in the frame";
    const int cx = (int)rounded->rect.x, cy = (int)rounded->rect.y;
    const uint8_t *edge = f.At(cx + (int)rounded->rect.width / 2, cy + 2);
    const uint8_t *corner = f.At(cx, cy);
    const uint8_t *inset = f.At(cx + (int)rounded->radius, cy + (int)rounded->radius);
    const int want[3] = { (int)(rounded->colors.topLeft.r * 255.0f + 0.5f),
                          (int)(rounded->colors.topLeft.g * 255.0f + 0.5f),
                          (int)(rounded->colors.topLeft.b * 255.0f + 0.5f) };
    for (int i = 0; i < 3; i++) {
        EXPECT_NEAR(edge[i], want[i], 3) << "top edge, channel " << i;
        EXPECT_NEAR(inset[i], want[i], 3) << "inside the corner's arc, channel " << i;
    }
    EXPECT_GT(std::abs(corner[0] - want[0]) + std::abs(corner[1] - want[1])
                  + std::abs(corner[2] - want[2]),
              60)
        << "the corner pixel is the tile's colour: the corner is square (radius "
        << rounded->radius << " at " << cx << "," << cy << ")";
}

// ---------------------------------------------------------------------------
// The demo in a running engine
// ---------------------------------------------------------------------------

namespace {

    std::string BinaryDir() {
        char buf[4096];
        ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
        if (len <= 0) return ".";
        buf[len] = '\0';
        std::string path(buf);
        size_t slash = path.rfind('/');
        return slash != std::string::npos ? path.substr(0, slash) : ".";
    }

    bool Exists(const std::string &path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    }

    unsigned BE32(const unsigned char *p) {
        return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
    }

    // The engine's own screenshots: 8-bit RGB or RGBA, not interlaced.
    bool DecodePng(const std::string &path, Frame &img, std::string &why) {
        std::ifstream file(path, std::ios::binary);
        std::vector<unsigned char> d((std::istreambuf_iterator<char>(file)), {});
        static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
        if (d.size() < 8 || memcmp(d.data(), sig, 8)) {
            why = "not a PNG (" + std::to_string(d.size()) + " bytes)";
            return false;
        }
        int depth = 0, ctype = -1, interlace = 0;
        std::vector<unsigned char> z;
        for (size_t p = 8; p + 12 <= d.size();) {
            unsigned len = BE32(&d[p]);
            if (p + 12 + len > d.size()) break;
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
        const int bpp = ctype == 6 ? 4 : ctype == 2 ? 3 : 0;
        if (depth != 8 || !bpp || interlace || img.w <= 0 || img.h <= 0) {
            why = "unsupported PNG";
            return false;
        }
        const size_t stride = (size_t)img.w * bpp;
        std::vector<unsigned char> raw((stride + 1) * img.h);
        uLongf rawLen = raw.size();
        if (uncompress(raw.data(), &rawLen, z.data(), z.size()) != Z_OK || rawLen != raw.size()) {
            why = "PNG inflate failed";
            return false;
        }
        std::vector<unsigned char> prev(stride, 0), cur(stride);
        img.px.assign((size_t)img.w * img.h * 4, 255);
        for (int y = 0; y < img.h; y++) {
            const unsigned char *row = &raw[y * (stride + 1)];
            for (size_t i = 0; i < stride; i++) {
                int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i],
                    c = i >= (size_t)bpp ? prev[i - bpp] : 0, x = row[1 + i];
                switch (row[0]) {
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
            for (int x = 0; x < img.w; x++) {
                memcpy(&img.px[((size_t)y * img.w + x) * 4], &cur[(size_t)x * bpp], 3);
            }
            prev = cur;
        }
        return true;
    }

    // The value of "key": in a flat JSON answer: a string without its quotes,
    // a number or an array as written.  Empty when the key is not there.
    std::string JsonField(const std::string &json, const std::string &key) {
        size_t at = json.find("\"" + key + "\":");
        if (at == std::string::npos) return "";
        at += key.size() + 3;
        if (at >= json.size()) return "";
        if (json[at] == '"') {
            size_t end = json.find('"', at + 1);
            return end == std::string::npos ? "" : json.substr(at + 1, end - at - 1);
        }
        if (json[at] == '[') {
            size_t end = json.find(']', at);
            return end == std::string::npos ? "" : json.substr(at + 1, end - at - 1);
        }
        size_t end = json.find_first_of(",}", at);
        return json.substr(at, end == std::string::npos ? std::string::npos : end - at);
    }

    nativeui::Rect RectField(const std::string &json, const std::string &key) {
        nativeui::Rect r;
        sscanf(JsonField(json, key).c_str(), "%f,%f,%f,%f", &r.x, &r.y, &r.width, &r.height);
        return r;
    }

    std::string DriverLine(const std::string &driver, const std::string &tag) {
        const std::string prefix = "DRIVER: " + tag + " ";
        size_t at = driver.find(prefix);
        if (at == std::string::npos) return "";
        size_t end = driver.find('\n', at);
        return driver.substr(at + prefix.size(), end - at - prefix.size());
    }

} // namespace

// dc3-native, headless but rendering, with DC3_NATIVEUI_DEMO=1: the demo is
// what /api/screenshot shows, text injected over the debug server lands in the
// search field (asked of the screen, and seen in the pixels), and Down moves
// the selection.  One boot; the three are separate assertion groups.
TEST_F(NativeUiHostGpuTest, DemoRunsInTheEngine) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    const std::string bin = BinaryDir() + "/dc3-native";
    const std::string script = BinaryDir() + "/../../scripts/dc3-input-flows/idle-main.txt";
    ASSERT_TRUE(Exists(bin)) << bin << " (scripts/native_test.sh builds it)";
    ASSERT_TRUE(Exists(script)) << script;
    const std::string root = MakeTestTempDir("dc3-nativeui-demo");
    ASSERT_FALSE(root.empty());
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    const std::string log = root + "/engine.log";

    std::ostringstream sh;
    sh << "t=$(command -v timeout || command -v gtimeout); cd " << BinaryDir() << " && "
       << "( MILO_HEADLESS=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1 DC3_DLC=0"
       << EngineHttpPortShellEnv(root) << " MILO_MAX_FRAMES=0 MILO_WIDTH=1280 MILO_HEIGHT=720"
       << " DC3_CACHE_DIR=" << root << "/cache DC3_SAVE_DIR=" << root << "/saves"
       << " MILO_INPUT_SCRIPT=" << script << " DC3_NATIVEUI_DEMO=1"
       << " ${t:+$t 300} ./dc3-native > " << log << " 2>&1 & echo $! > " << log << ".pid ); "
       << "pid=$(cat " << log << ".pid); " << EngineHttpApiShell(root, "pid", 180)
       << R"SH(
scr() { curl -s -m 5 $api/screen | sed -n 's/.*"screen":"\([^"]*\)".*/\1/p'; };
frame() { curl -s -m 5 $api/frame | sed -n 's/.*"frame":\([0-9]*\).*/\1/p'; };
settle() { f=$(frame); [ -n "$f" ] && curl -s -m 70 "$api/frame/wait/$((f+$1))" > /dev/null; };
press() { curl -s -m 10 -X POST --data-binary "{\"button\":\"$1\"}" $api/input/press > /dev/null; settle ${2:-30}; };
state() { curl -s -m 5 $api/nativeui/state; };
end=$((SECONDS+180)); while [ $SECONDS -lt $end ] && kill -0 $pid 2>/dev/null && [ "$(scr)" != main_screen ]; do sleep 0.3; done;
echo "DRIVER: main $(scr)"; settle 240;
echo "DRIVER: s0 $(state)"; curl -s -m 30 -o )SH" << root << R"SH(/shot0.png $api/screenshot;
press down 30; echo "DRIVER: s1 $(state)";
echo "DRIVER: typed $(curl -s -m 5 -X POST --data-binary '{"text":"zz"}' $api/input/text)"; settle 30;
echo "DRIVER: s2 $(state)"; curl -s -m 30 -o )SH" << root << R"SH(/shot2.png $api/screenshot;
echo "DRIVER: bad $(curl -s -m 5 -o /dev/null -w '%{http_code}' -X POST --data-binary '{"key":"f13"}' $api/input/text)";
curl -s -m 5 -X POST --data-binary '{"key":"backspace"}' $api/input/text > /dev/null; settle 30;
echo "DRIVER: s3 $(state)";
kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; grep -a "NativeUI demo" )SH" << log << "; true";

    std::string out;
    FILE *pipe = popen(sh.str().c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    char buf[8192];
    while (fgets(buf, sizeof buf, pipe)) out += buf;
    pclose(pipe);
    if (getenv("DC3_TEST_SHOW_DRIVER")) fputs(out.c_str(), stderr);

    ASSERT_EQ(DriverLine(out, "main"), "main_screen") << out;
    const std::string s0 = DriverLine(out, "s0"), s1 = DriverLine(out, "s1"),
                      s2 = DriverLine(out, "s2"), s3 = DriverLine(out, "s3");
    ASSERT_EQ(JsonField(s0, "active"), "true") << "state: " << s0 << "\n" << out;
    EXPECT_NE(out.find("NativeUI demo: font eagle-light(47)"), std::string::npos)
        << "the demo did not report the menu font";

    // 1. The screenshot is the demo: the highlight bar is where the screen
    // says it is, in the accent colour, and the title's ink is there.
    Frame shot0, shot2;
    std::string why;
    ASSERT_TRUE(DecodePng(root + "/shot0.png", shot0, why)) << why;
    EXPECT_EQ(shot0.w, 1280);
    EXPECT_EQ(shot0.h, 720);
    const nativeui::Rect bar = RectField(s0, "highlight_rect");
    ASSERT_GT(bar.width, 300.0f) << s0;
    auto accented = [](int r, int g, int b) { return b > r + 40 && b > 90; };
    EXPECT_GT(Count(shot0, bar.x + 300, bar.y + 4, 100, bar.height - 8, accented), 2000)
        << "no highlight bar at " << bar.x << "," << bar.y << " in the screenshot";
    EXPECT_GT(Count(shot0, 40, 20, 140, 50, Bright), 150) << "no title ink top left";
    EXPECT_GT(atoi(JsonField(s0, "glyphs").c_str()), 100) << s0;
    EXPECT_EQ(JsonField(s0, "placeholder_glyphs"), "0") << s0;
    EXPECT_GT(atoi(JsonField(s0, "draw_calls").c_str()), 0) << s0;

    // 2. Down moves the selection.
    EXPECT_EQ(atoi(JsonField(s1, "selected_row").c_str()),
              atoi(JsonField(s0, "selected_row").c_str()) + 1)
        << "before: " << s0 << "\nafter: " << s1;
    EXPECT_NE(JsonField(s1, "detail_title"), JsonField(s0, "detail_title"));

    // 3. Injected text is in the search field: by the screen's own account,
    // and in the pixels of the field (white text where the dim prompt was).
    EXPECT_NE(DriverLine(out, "typed").find("\"typed\":2"), std::string::npos) << out;
    EXPECT_EQ(JsonField(s0, "search"), "");
    EXPECT_EQ(JsonField(s2, "search"), "zz") << s2;
    EXPECT_EQ(JsonField(s3, "search"), "z") << "backspace over the endpoint: " << s3;
    EXPECT_EQ(DriverLine(out, "bad"), "400") << "an unknown key name must be refused";
    ASSERT_TRUE(DecodePng(root + "/shot2.png", shot2, why)) << why;
    const nativeui::Rect field = RectField(s0, "search_rect");
    ASSERT_GT(field.width, 100.0f) << s0;
    // The left part of the field only: the game's own player box overlaps
    // its right end in the demo.
    const int before = Count(shot0, field.x + 8, field.y + 4, 150, field.height - 8, Bright);
    const int after = Count(shot2, field.x + 8, field.y + 4, 150, field.height - 8, Bright);
    EXPECT_EQ(before, 0) << "the empty field's prompt is dim";
    EXPECT_GT(after, 40) << "no white text in the search field after typing";
}
