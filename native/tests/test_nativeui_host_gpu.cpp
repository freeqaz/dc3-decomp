// nativeui_host with the real things: the game's fonts loaded the way a label
// loads them, the engine's canvas, a render target read back.
//
// The pure halves (translation, layout, cache, input) are test_nativeui_host.cpp;
// the host inside a running dc3-native is the song browser's own test
// (test_native_song_browser.cpp).

#include <gtest/gtest.h>
#include <zlib.h>

#include "nativeui_raster.h"
#include "test_helpers.h"
#include "test_tmp.h"

#include "gfx/Canvas2D.h"
#include "nativeui/ListView.h"
#include "nativeui_host/GameFonts.h"
#include "nativeui_host/NativeUiHost.h"
#include "obj/Object.h"
#include "songbrowser/SongCatalog.h"
#include "songbrowser_ui/SongBrowserScreen.h"
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

namespace {

    // Thirty made-up songs, none with a play history (so the list has the
    // focus on arrival) and none with art (so every tile is a placeholder).
    songbrowser::SongCatalog ThirtySongs() {
        static const char *const kWords[] = { "Midnight", "Electric", "Golden",
                                              "Neon",     "Crystal",  "Velvet" };
        songbrowser::SongCatalog catalog;
        for (int i = 0; i < 30; i++) {
            songbrowser::SongEntry song;
            song.id = i + 1;
            song.shortName = "song" + std::to_string(i);
            song.title = std::string(kWords[i % 6]) + " Heart " + std::to_string(i);
            song.artist = std::string("The ") + kWords[(i * 5) % 6] + " Band";
            song.album = "Album";
            song.year = 1980 + i;
            song.bpm = 100 + i;
            song.lengthMs = 180000;
            song.difficultyTier = i % 7;
            song.origin = "ham3";
            catalog.Add(song);
        }
        return catalog;
    }

}

// The song browser through NativeUiHost::Draw(): real fonts, the engine's
// canvas, a 1280x720 render target.  Regions, not a whole-image hash.
TEST_F(NativeUiHostGpuTest, SongBrowserThroughTheHostAndCanvas) {
    if (IsSkipped()) return;
    if (!Ready()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    GameFonts fonts;
    ASSERT_TRUE(fonts.Load());
    NativeUiHost host(fonts.Fonts(), nullptr);
    const songbrowser::SongCatalog catalog = ThirtySongs();
    const nativeui::Theme theme = nativeui::Dc3Theme();
    songbrowser_ui::SongBrowserScreen browser(
        catalog, songbrowser_ui::Delegate {},
        songbrowser_ui::Environment { fonts.Fonts(), host.Clock() }
    );
    HostedScreenOf<songbrowser_ui::SongBrowserScreen> hosted(browser);
    host.SetScreen(&hosted);
    host.SetOutputSize(1280, 720);
    double now = 10.0;
    host.Enter(now);
    for (int i = 0; i < 3; i++) {
        host.OnButtonDown(kPadDDown, kPadActionDown);
        host.OnButtonUp(kPadDDown, kPadActionDown);
    }
    for (int i = 0; i < 600 && (i < 5 || !browser.IsSettled()); i++) {
        host.Tick(now += 1.0 / 60.0);
        browser.BuildFrame();
    }
    ASSERT_TRUE(browser.IsSettled());

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
    EXPECT_EQ(stats.placeholderGlyphs, 0u) << "the menu font has every character of these songs";

    // The highlight bar: the accent colour (blue) on the selected row, and
    // not on the row under it.
    const nativeui::Rect bar = browser.List().HighlightRect();
    const nativeui::Color accent = theme.color.accent;
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
            const nativeui::Color &p = theme.color.imagePlaceholder;
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

    // A rounded corner: an image placeholder tile (no song here has art).  Its top edge is the tile's colour; its very corner is not.
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
