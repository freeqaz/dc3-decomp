// nativeui end to end, without a screen of songs: one exact display-list
// golden for a small tree, and the test-only software rasteriser.
//
// The framework's worked example is the song browser itself
// (native/src/songbrowser_ui/); its tests, test_song_browser_ui.cpp, drive a
// whole screen with events and write the pictures.

#include "nativeui_raster.h"
#include "nativeui_test_support.h"

#include "nativeui/Clock.h"
#include "nativeui/HintBar.h"
#include "nativeui/Label.h"
#include "nativeui/Layout.h"
#include "nativeui/Panel.h"
#include "nativeui/Screen.h"

using namespace nativeui;
using namespace nativeui_test;

// One small tree, every command checked exactly. FixedAdvanceMeasurer: a
// glyph is half its size wide, ascent 0.8, descent 0.2, line height 1.2.
TEST(UiCoreGolden, DisplayListGoldenForASmallCard) {
    const FixedAdvanceMeasurer text;
    const ManualClock clock;
    Screen screen({ 200.0f, 100.0f }, Dc3Theme(), text, clock);

    auto column = std::make_unique<Column>(4.0f, CrossAlign::Start);
    column->Add(std::make_unique<Label>("Hi", &Typography::caption));
    column->Add(std::make_unique<HintBar>(std::vector<Hint> { { "A", "Ok" } }));
    PanelStyle style;
    style.border = &Palette::panelBorder;
    screen.SetRoot(
        std::make_unique<Panel>(
            style, std::make_unique<Padding>(Insets::All(10.0f), std::move(column))
        )
    );

    const std::string expected =
        "fill 0,0 200x100 r=18 soft=0 #9594A0EB #9594A0EB #3B3843EB #3B3843EB\n"
        "stroke 0,0 200x100 r=18 t=1.5 soft=0 #423F5CFF\n"
        // The label: 10 in, baseline centred in its 21.6 line (10 + 1.8 + 14.4).
        "text 10,26.2 font=1 size=18 ls=1.08 #FFFFFFFF \"HI\"\n"
        // The hint bar, 4 below the label: a 26 pill, its glyph centred in it
        // (10 + 8, baseline 35.6 + 3 + 16), then the text 8 after the pill.
        "stroke 10,35.6 26x26 r=13 t=1.5 soft=0 #FFFFFFB8\n"
        "text 18,54.6 font=1 size=20 ls=0.8 #FFFFFFFF \"A\"\n"
        "text 44,54.6 font=1 size=20 ls=0.8 #FFFFFFB8 \"OK\"\n";
    EXPECT_EQ(Describe(screen.BuildFrame()), expected);
}

// ---------------------------------------------------------------------------
// The software rasteriser (a test tool)
// ---------------------------------------------------------------------------

TEST(UiCoreRaster, DrawsShapesRoundCornersClipsAndText) {
    const Color red = Color::FromRgb8(0xFF0000);
    Raster raster(40, 40);
    raster.Draw(RoundRectFill { { 4, 4, 20, 20 }, 10.0f, CornerColors::Solid(red), 0.0f });
    EXPECT_FLOAT_EQ(raster.At(14, 14).r, 1.0f) << "the middle is filled";
    EXPECT_FLOAT_EQ(raster.At(4, 4).r, 0.0f) << "the corner is rounded away";
    EXPECT_FLOAT_EQ(raster.At(30, 30).r, 0.0f);

    raster.PushClip({ 0, 30, 5, 5 });
    raster.Draw(RoundRectFill { { 0, 26, 40, 14 }, 0.0f, CornerColors::Solid(red), 0.0f });
    raster.PopClip();
    EXPECT_FLOAT_EQ(raster.At(2, 32).r, 1.0f);
    EXPECT_FLOAT_EQ(raster.At(10, 32).r, 0.0f) << "right of the clip";
    EXPECT_FLOAT_EQ(raster.At(2, 28).r, 0.0f) << "above the clip";

    Raster ring(40, 40);
    ring.Draw(
        RoundRectStroke { { 4, 4, 30, 30 }, 0.0f, CornerColors::Solid(red), 3.0f, 0.0f }
    );
    EXPECT_FLOAT_EQ(ring.At(5, 20).r, 1.0f) << "on the band, just inside the edge";
    EXPECT_FLOAT_EQ(ring.At(19, 19).r, 0.0f) << "hollow in the middle";
    EXPECT_FLOAT_EQ(ring.At(1, 20).r, 0.0f) << "nothing outside";

    Raster text(40, 40);
    text.Draw(TextRun { { 4, 30 }, FontId { 1 }, 20.0f, red, 0.0f, "I" });
    float ink = 0.0f;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            ink += text.At(x, y).r;
            if (text.At(x, y).r > 0.0f) {
                EXPECT_LT(y, 30) << "glyphs sit on the baseline";
                EXPECT_GE(y, 30 - 14) << "and are 0.7 of the size tall";
            }
        }
    }
    EXPECT_GT(ink, 20.0f);

    // One stored block: signature, IHDR, IDAT (zlib header, block header,
    // 40 rows of a filter byte and 120 colour bytes, Adler-32), IEND.
    const std::vector<unsigned char> png = EncodePng(40, 40, raster.Rgb8());
    EXPECT_EQ(png.size(), 8u + 25u + (12u + 2u + 5u + 40u * 121u + 4u) + 12u);
    EXPECT_EQ(std::string(png.begin() + 1, png.begin() + 4), "PNG");
    EXPECT_EQ(std::string(png.end() - 8, png.end() - 4), "IEND");
}
