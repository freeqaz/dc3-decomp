// nativeui core: UTF-8, text fitting, the display list and its painter, the
// renderer seam, the theme, and animation. No GPU, no game data.

#include "nativeui_test_support.h"

#include "nativeui/Animation.h"
#include "nativeui/Clock.h"
#include "nativeui/Renderer.h"
#include "nativeui/Text.h"
#include "nativeui/Utf8.h"

#include <cmath>

using namespace nativeui;
using namespace nativeui_test;

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

TEST(UiCoreUtf8, DecodesEveryLength) {
    const std::string text = "A\xC3\xA9\xE6\x97\xA5\xF0\x9F\x8E\xB5"; // A é 日 🎵
    EXPECT_EQ(utf8::Decode(text, 0).codePoint, U'A');
    EXPECT_EQ(utf8::Decode(text, 0).length, 1u);
    EXPECT_EQ(utf8::Decode(text, 1).codePoint, char32_t(0xE9));
    EXPECT_EQ(utf8::Decode(text, 1).length, 2u);
    EXPECT_EQ(utf8::Decode(text, 3).codePoint, char32_t(0x65E5));
    EXPECT_EQ(utf8::Decode(text, 3).length, 3u);
    EXPECT_EQ(utf8::Decode(text, 6).codePoint, char32_t(0x1F3B5));
    EXPECT_EQ(utf8::Decode(text, 6).length, 4u);
    EXPECT_EQ(utf8::Count(text), 4u);
    EXPECT_EQ(utf8::Decode(text, text.size()).length, 0u);
}

TEST(UiCoreUtf8, MalformedBytesBecomeOneReplacementEach) {
    // A stray continuation, a truncated sequence, an overlong NUL, a surrogate.
    for (const std::string &bad : { std::string("\x80"),
                                    std::string("\xE6\x97"),
                                    std::string("\xC0\x80"),
                                    std::string("\xED\xA0\x80"),
                                    std::string("\xFF") }) {
        const utf8::Decoded d = utf8::Decode(bad, 0);
        EXPECT_EQ(d.codePoint, utf8::kReplacement) << bad.size();
        EXPECT_EQ(d.length, 1u);
        EXPECT_EQ(utf8::Count(bad), bad.size());
    }
}

TEST(UiCoreUtf8, BoundariesStepByWholeCharacters) {
    const std::string text = "a\xC3\xA9\xE6\x97\xA5z"; // a é 日 z
    EXPECT_EQ(utf8::Next(text, 0), 1u);
    EXPECT_EQ(utf8::Next(text, 1), 3u);
    EXPECT_EQ(utf8::Next(text, 3), 6u);
    EXPECT_EQ(utf8::Next(text, 7), 7u);
    EXPECT_EQ(utf8::Prev(text, 7), 6u);
    EXPECT_EQ(utf8::Prev(text, 6), 3u);
    EXPECT_EQ(utf8::Prev(text, 3), 1u);
    EXPECT_EQ(utf8::Prev(text, 1), 0u);
    EXPECT_EQ(utf8::Prev(text, 0), 0u);
    EXPECT_EQ(utf8::OffsetOf(text, 2), 3u);
    EXPECT_EQ(utf8::OffsetOf(text, 99), text.size());
    // Behind a broken sequence, step one byte rather than swallow good text.
    EXPECT_EQ(utf8::Prev("ab\x80", 3), 2u);
}

TEST(UiCoreUtf8, AppendRoundTrips) {
    for (const char32_t cp : { U'A',
                               char32_t(0xE9),
                               char32_t(0x7FF),
                               char32_t(0x800),
                               char32_t(0x65E5),
                               char32_t(0xFFFF),
                               char32_t(0x10000),
                               char32_t(0x10FFFF) }) {
        std::string out;
        utf8::Append(out, cp);
        const utf8::Decoded d = utf8::Decode(out, 0);
        EXPECT_EQ(d.codePoint, cp);
        EXPECT_EQ(d.length, out.size());
    }
    std::string bad;
    utf8::Append(bad, 0xD800);
    EXPECT_EQ(utf8::Decode(bad, 0).codePoint, utf8::kReplacement);
}

TEST(UiCoreUtf8, UpperCasesAccentedLatin) {
    EXPECT_EQ(utf8::ToUpper("d\xC3\xA9j\xC3\xA0 vu"), "D\xC3\x89J\xC3\x80 VU");
    EXPECT_EQ(utf8::ToUpper("se\xC3\xB1orita"), "SE\xC3\x91ORITA");
    EXPECT_EQ(utf8::ToUpper("\xC3\xBF"), "\xC5\xB8"); // y-diaeresis
    EXPECT_EQ(
        utf8::ToUpper(
            "stra\xC3\x9F"
            "e"
        ),
        "STRASSE"
    );
    EXPECT_EQ(utf8::ToUpper("\xC3\xB7"), "\xC3\xB7"); // division sign stays
    EXPECT_EQ(utf8::ToUpper("\xC5\x82"), "\xC5\x81"); // l with stroke
    EXPECT_EQ(utf8::ToUpper("\xC5\xBE"), "\xC5\xBD"); // z with caron
    EXPECT_EQ(utf8::ToUpper("\xC4\x81"), "\xC4\x80"); // a with macron
    EXPECT_EQ(utf8::ToUpper("\xC4\xB1"), "I"); // dotless i
    EXPECT_EQ(utf8::ToUpper("\xE6\x97\xA5 ok"), "\xE6\x97\xA5 OK");
    EXPECT_EQ(utf8::ToUpper("ALREADY 123"), "ALREADY 123");
}

// ---------------------------------------------------------------------------
// Text measuring and fitting (FixedAdvanceMeasurer: half the size per character)
// ---------------------------------------------------------------------------

namespace {

    const TextStyle kPlain { FontId { 1 }, 20.0f, 0.0f, TextCase::AsWritten };

}

TEST(UiCoreText, WidthIsAdvancesPlusTrackingBetweenGlyphs) {
    const FixedAdvanceMeasurer m;
    EXPECT_FLOAT_EQ(MeasureWidth(m, kPlain, "ABCD"), 40.0f);
    TextStyle tracked = kPlain;
    tracked.tracking = 0.1f; // 2 units, between 4 glyphs: 3 gaps
    EXPECT_FLOAT_EQ(MeasureWidth(m, tracked, "ABCD"), 46.0f);
    EXPECT_FLOAT_EQ(MeasureWidth(m, tracked, ""), 0.0f);
    EXPECT_FLOAT_EQ(MeasureWidth(m, tracked, "A"), 10.0f);
    // Characters, not bytes.
    EXPECT_FLOAT_EQ(MeasureWidth(m, kPlain, "\xC3\x89\xC3\x89"), 20.0f);
}

TEST(UiCoreText, FitsWithoutCuttingWhenThereIsRoom) {
    const FixedAdvanceMeasurer m;
    const FittedLine line = FitLine(m, kPlain, "ABCD", 40.0f, "...");
    EXPECT_EQ(line.text, "ABCD");
    EXPECT_FLOAT_EQ(line.width, 40.0f);
    EXPECT_FALSE(line.truncated);
}

TEST(UiCoreText, CutsToTheLongestPrefixThatFitsWithTheEllipsis) {
    const FixedAdvanceMeasurer m;
    // 10 units per character; "..." is 30. In 65: three letters + ellipsis = 60.
    const FittedLine line = FitLine(m, kPlain, "ABCDEFGHIJ", 65.0f, "...");
    EXPECT_EQ(line.text, "ABC...");
    EXPECT_FLOAT_EQ(line.width, 60.0f);
    EXPECT_TRUE(line.truncated);
    EXPECT_LE(line.width, 65.0f);
    // One unit short of the next character still cuts at the same place.
    EXPECT_EQ(FitLine(m, kPlain, "ABCDEFGHIJ", 69.9f, "...").text, "ABC...");
    EXPECT_EQ(FitLine(m, kPlain, "ABCDEFGHIJ", 70.0f, "...").text, "ABCD...");
}

TEST(UiCoreText, DropsSpacesBeforeTheEllipsis) {
    const FixedAdvanceMeasurer m;
    EXPECT_EQ(FitLine(m, kPlain, "AB CDEFGH", 60.0f, "...").text, "AB...");
}

TEST(UiCoreText, CutsOnlyAtCharacterBoundaries) {
    const FixedAdvanceMeasurer m;
    const std::string accents =
        "\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89"; // 6 x E-acute
    const FittedLine line = FitLine(m, kPlain, accents, 50.0f, "...");
    EXPECT_EQ(line.text, "\xC3\x89\xC3\x89...");
    EXPECT_EQ(utf8::Count(line.text), 5u);
    // A one-character ellipsis is measured as one character.
    const FittedLine single = FitLine(m, kPlain, accents, 50.0f, "\xE2\x80\xA6");
    EXPECT_EQ(utf8::Count(single.text), 5u);
    EXPECT_FLOAT_EQ(single.width, 50.0f);
}

TEST(UiCoreText, EmptyWhenNotEvenTheEllipsisFits) {
    const FixedAdvanceMeasurer m;
    const FittedLine line = FitLine(m, kPlain, "ABCDEFGHIJ", 20.0f, "...");
    EXPECT_EQ(line.text, "");
    EXPECT_FLOAT_EQ(line.width, 0.0f);
    EXPECT_TRUE(line.truncated);
    EXPECT_EQ(FitLine(m, kPlain, "", 0.0f, "...").text, "");
    EXPECT_FALSE(FitLine(m, kPlain, "", 0.0f, "...").truncated);
}

TEST(UiCoreText, UpperCaseIsAStyleOption) {
    const FixedAdvanceMeasurer m;
    TextStyle upper = kPlain;
    upper.textCase = TextCase::Upper;
    EXPECT_EQ(
        FitLine(m, upper, "D\xC3\xA9j\xC3\xA0 Vu", 1000.0f, "...").text,
        "D\xC3\x89J\xC3\x80 VU"
    );
    EXPECT_EQ(
        FitLine(m, kPlain, "D\xC3\xA9j\xC3\xA0 Vu", 1000.0f, "...").text,
        "D\xC3\xA9j\xC3\xA0 Vu"
    );
    // The cut happens after casing: sharp s grows to two letters first.
    EXPECT_EQ(
        FitLine(
            m,
            upper,
            "stra\xC3\x9F"
            "enbahn",
            70.0f,
            "..."
        )
            .text,
        "STRA..."
    );
}

TEST(UiCoreText, AlignsAndCentresBaselines) {
    EXPECT_FLOAT_EQ(AlignOffset(TextAlign::Left, 100.0f, 40.0f), 0.0f);
    EXPECT_FLOAT_EQ(AlignOffset(TextAlign::Center, 100.0f, 40.0f), 30.0f);
    EXPECT_FLOAT_EQ(AlignOffset(TextAlign::Right, 100.0f, 40.0f), 60.0f);
    // Ascent 16 + descent 4 centred in a box 40 tall starting at 10.
    EXPECT_FLOAT_EQ(CenteredBaseline({ 16.0f, 4.0f, 24.0f }, 10.0f, 40.0f), 36.0f);
}

// ---------------------------------------------------------------------------
// Display list, painter, renderer
// ---------------------------------------------------------------------------

namespace {

    const Color kWhite { 1.0f, 1.0f, 1.0f, 1.0f };

}

TEST(UiCoreDisplayList, CommandsPassThroughUnchangedAtTheRoot) {
    DisplayList list;
    Painter painter(list);
    painter.FillRoundRect(
        { 1, 2, 3, 4 }, 5.0f, CornerColors::Vertical(kWhite, { 0, 0, 0, 1 }), 6.0f
    );
    painter.StrokeRoundRect({ 1, 2, 3, 4 }, 5.0f, CornerColors::Solid(kWhite), 2.0f);
    painter.DrawImage({ 1, 2, 3, 4 }, ImageHandle { 7 }, { 0, 0, 1, 1 }, kWhite, 5.0f);
    ASSERT_EQ(list.commands.size(), 3u);
    const RoundRectFill fill = std::get<RoundRectFill>(list.commands[0]);
    EXPECT_EQ(fill.rect, (Rect { 1, 2, 3, 4 }));
    EXPECT_FLOAT_EQ(fill.radius, 5.0f);
    EXPECT_FLOAT_EQ(fill.softness, 6.0f);
    EXPECT_EQ(fill.colors.topLeft, kWhite);
    EXPECT_EQ(fill.colors.bottomRight, (Color { 0, 0, 0, 1 }));
    EXPECT_FLOAT_EQ(std::get<RoundRectStroke>(list.commands[1]).thickness, 2.0f);
    EXPECT_EQ(std::get<ImageQuad>(list.commands[2]).image, ImageHandle { 7 });
    EXPECT_TRUE(painter.IsBalanced());
}

TEST(UiCoreDisplayList, ALayerMovesScalesAndFadesWhatIsDrawnInIt) {
    DisplayList list;
    Painter painter(list);
    painter.PushLayer({ { 10.0f, 20.0f }, 2.0f, 0.5f });
    painter.FillRoundRect({ 1, 2, 3, 4 }, 1.0f, CornerColors::Solid(kWhite), 2.0f);
    painter.DrawText(
        { 5.0f, 6.0f }, { FontId { 3 }, 10.0f, 0.1f, TextCase::AsWritten }, kWhite, "HI"
    );
    painter.PopLayer();
    painter.FillRoundRect({ 1, 2, 3, 4 }, 1.0f, CornerColors::Solid(kWhite));

    const RoundRectFill inLayer = std::get<RoundRectFill>(list.commands[0]);
    EXPECT_EQ(inLayer.rect, (Rect { 12, 24, 6, 8 }));
    EXPECT_FLOAT_EQ(inLayer.radius, 2.0f);
    EXPECT_FLOAT_EQ(inLayer.softness, 4.0f);
    EXPECT_FLOAT_EQ(inLayer.colors.topLeft.a, 0.5f);

    const TextRun text = std::get<TextRun>(list.commands[1]);
    EXPECT_EQ(text.origin, (Vec2 { 20.0f, 32.0f }));
    EXPECT_FLOAT_EQ(text.size, 20.0f);
    EXPECT_FLOAT_EQ(text.letterSpacing, 2.0f);
    EXPECT_FLOAT_EQ(text.color.a, 0.5f);
    EXPECT_EQ(text.font, FontId { 3 });

    // After the pop, nothing of the layer remains.
    const RoundRectFill after = std::get<RoundRectFill>(list.commands[2]);
    EXPECT_EQ(after.rect, (Rect { 1, 2, 3, 4 }));
    EXPECT_FLOAT_EQ(after.colors.topLeft.a, 1.0f);
}

TEST(UiCoreDisplayList, NestedLayersCompose) {
    DisplayList list;
    Painter painter(list);
    painter.PushLayer({ { 10.0f, 0.0f }, 2.0f, 0.5f });
    painter.PushLayer({ { 5.0f, 5.0f }, 0.5f, 0.5f });
    painter.FillRoundRect({ 2, 2, 4, 4 }, 0.0f, CornerColors::Solid(kWhite));
    painter.PopLayer();
    EXPECT_FALSE(painter.IsBalanced());
    painter.PopLayer();
    EXPECT_TRUE(painter.IsBalanced());
    const RoundRectFill fill = std::get<RoundRectFill>(list.commands[0]);
    // Inner offset is scaled by the outer layer: 10 + 2*5 = 20; scales cancel.
    EXPECT_EQ(fill.rect, (Rect { 22, 12, 4, 4 }));
    EXPECT_FLOAT_EQ(fill.colors.topLeft.a, 0.25f);
}

TEST(UiCoreDisplayList, AFullyTransparentLayerEmitsNoDrawing) {
    DisplayList list;
    Painter painter(list);
    painter.PushLayer({ {}, 1.0f, 0.0f });
    painter.FillRoundRect({ 0, 0, 1, 1 }, 0.0f, CornerColors::Solid(kWhite));
    painter.DrawText({}, kPlain, kWhite, "X");
    painter.DrawImage({ 0, 0, 1, 1 }, ImageHandle { 1 }, { 0, 0, 1, 1 }, kWhite, 0.0f);
    painter.PopLayer();
    EXPECT_TRUE(list.commands.empty());
}

TEST(UiCoreDisplayList, ClipsNestIntersectAndFollowTheLayer) {
    DisplayList list;
    Painter painter(list);
    painter.PushClip({ 0, 0, 100, 100 });
    painter.PushLayer({ { 50.0f, 50.0f }, 1.0f, 1.0f });
    painter.PushClip({ 0, 0, 100, 100 }); // 50..150 in root space, cut to 50..100
    EXPECT_FALSE(painter.IsBalanced());
    painter.PopClip();
    painter.PopLayer();
    painter.PopClip();
    EXPECT_TRUE(painter.IsBalanced());

    const std::vector<ClipPush> pushes = CommandsOf<ClipPush>(list);
    ASSERT_EQ(pushes.size(), 2u);
    EXPECT_EQ(pushes[0].rect, (Rect { 0, 0, 100, 100 }));
    EXPECT_EQ(pushes[1].rect, (Rect { 50, 50, 50, 50 }));
    EXPECT_TRUE(ClipsBalanced(list));
    EXPECT_EQ(CommandsOf<ClipPop>(list).size(), 2u);
}

TEST(UiCoreDisplayList, ReplayReproducesTheListThroughTheRendererSeam) {
    DisplayList list;
    Painter painter(list);
    painter.PushClip({ 0, 0, 50, 50 });
    painter.FillRoundRect(
        { 1, 2, 3, 4 }, 5.0f, CornerColors::Horizontal(kWhite, { 1, 0, 0, 1 }), 1.0f
    );
    painter.StrokeRoundRect({ 1, 2, 3, 4 }, 5.0f, CornerColors::Solid(kWhite), 2.0f, 0.5f);
    painter.DrawImage(
        { 5, 6, 7, 8 }, ImageHandle { 9 }, { 0.25f, 0, 0.5f, 1 }, kWhite, 3.0f
    );
    painter.DrawText({ 4.0f, 5.0f }, kPlain, kWhite, "HELLO");
    painter.PopClip();

    RecordingRenderer renderer;
    Replay(list, renderer);
    ASSERT_EQ(renderer.recorded.commands.size(), list.commands.size());
    EXPECT_TRUE(renderer.recorded.commands == list.commands);
    EXPECT_EQ(Describe(renderer.recorded), Describe(list));
}

TEST(UiCoreDisplayList, DescribeIsAReadableGolden) {
    DisplayList list;
    Painter painter(list);
    painter.PushClip({ 0, 0, 50, 50 });
    painter.FillRoundRect(
        { 16, 8, 200, 40 }, 6.0f, CornerColors::Solid(Color::FromRgb8(0x37A7E3))
    );
    painter.FillRoundRect(
        { 0, 0, 2, 2 }, 0.0f, CornerColors::Vertical(kWhite, { 0, 0, 0, 0.5f }), 1.5f
    );
    painter.StrokeRoundRect({ 1, 2, 3, 4 }, 5.0f, CornerColors::Solid(kWhite), 2.0f);
    painter.DrawImage({ 5, 6, 7, 8 }, ImageHandle { 9 }, { 0, 0, 1, 1 }, kWhite, 3.0f);
    painter.DrawText(
        { 24.0f, 36.0f }, { FontId { 1 }, 26.0f, 0.04f, TextCase::Upper }, kWhite, "WARMUP"
    );
    painter.PopClip();
    EXPECT_EQ(
        Describe(list),
        "clip 0,0 50x50\n"
        "fill 16,8 200x40 r=6 soft=0 #37A7E3FF\n"
        "fill 0,0 2x2 r=0 soft=1.5 #FFFFFFFF #FFFFFFFF #00000080 #00000080\n"
        "stroke 1,2 3x4 r=5 t=2 soft=0 #FFFFFFFF\n"
        "image id=9 5,6 7x8 uv=0,0 1x1 r=3 tint=#FFFFFFFF\n"
        "text 24,36 font=1 size=26 ls=1.04 #FFFFFFFF \"WARMUP\"\n"
        "unclip\n"
    );
}

// ---------------------------------------------------------------------------
// Theme
// ---------------------------------------------------------------------------

TEST(UiCoreTheme, Dc3ThemeHasTheGamesLook) {
    const Theme theme = Dc3Theme();
    // Black behind, a cyan accent (more blue than red), white text.
    EXPECT_EQ(theme.color.background, Color::FromRgb8(0x000000));
    EXPECT_GT(theme.color.accent.b, theme.color.accent.r + 0.4f);
    EXPECT_EQ(theme.color.accent, Color::FromRgb8(0x37A7E3));
    EXPECT_EQ(theme.color.textPrimary, Color::FromRgb8(0xFFFFFF));
    EXPECT_GT(theme.color.panelTop.r, theme.color.panelBottom.r); // light to dark
    // Every style is upper case with a real size. Reading sizes are in the
    // menu face; the two small styles are in the heavy face, whose capitals
    // are taller for the same size.
    for (const TextStyleToken token : { &Typography::display,
                                        &Typography::title,
                                        &Typography::body,
                                        &Typography::caption,
                                        &Typography::label,
                                        &Typography::hint }) {
        const TextStyle &style = theme.type.*token;
        EXPECT_EQ(style.textCase, TextCase::Upper);
        const bool small = token == &Typography::label || token == &Typography::hint;
        EXPECT_EQ(style.font, small ? kDc3HeavyFont : kDc3MenuFont);
        EXPECT_GT(style.size, 0.0f);
    }
    EXPECT_GT(theme.type.display.size, theme.type.body.size);
    EXPECT_EQ(theme.type.ellipsis, "...");
}

// The readability floor: at 720 rows of output one logical unit is one pixel,
// and no style's capitals may be under 10 of them. (Bigger outputs scale up.)
TEST(UiCoreTheme, NoStyleHasCapitalsUnderTenPixelsAt720Rows) {
    const Theme theme = Dc3Theme();
    float smallest = 1e9f;
    for (const TextStyleToken token : { &Typography::display,
                                        &Typography::title,
                                        &Typography::body,
                                        &Typography::caption,
                                        &Typography::label,
                                        &Typography::hint }) {
        const TextStyle &style = theme.type.*token;
        const float capRatio = style.font == kDc3HeavyFont ? kDc3HeavyCapRatio : kDc3MenuCapRatio;
        EXPECT_GE(style.size * capRatio, 10.0f) << "size " << style.size;
        smallest = std::min(smallest, style.size * capRatio);
    }
    std::cout << "[ measured ] smallest capital height at 720 rows: " << smallest << " px\n";
}

// Motion is short and eases out: tweens of 120 to 320 ms, and every spring
// within 1% of its target in 120 to 220 ms without ever passing it.
TEST(UiCoreTheme, MotionIsShortAndEasesOut) {
    const Motion motion = Dc3Theme().motion;
    EXPECT_GE(motion.fast, 0.12f);
    EXPECT_LE(motion.normal, 0.22f);
    EXPECT_LE(motion.slow, 0.35f);
    for (const SpringParams *params :
         { &motion.scrollSpring, &motion.focusSpring, &motion.fadeSpring }) {
        Spring spring(0.0f);
        spring.SetTarget(1.0f);
        const float step = 1.0f / 480.0f;
        float seconds = 0.0f;
        float previous = 0.0f;
        while (spring.Value() < 0.99f && seconds < 1.0f) {
            spring.Advance(step, *params);
            seconds += step;
            EXPECT_GE(spring.Value(), previous) << "ease-out: it only ever approaches";
            EXPECT_LE(spring.Value(), 1.0f) << "no overshoot";
            previous = spring.Value();
        }
        EXPECT_GE(seconds, 0.12f) << "omega " << params->angularFrequency;
        EXPECT_LE(seconds, 0.22f) << "omega " << params->angularFrequency;
        std::cout << "[ measured ] spring omega " << params->angularFrequency << ": within 1% after "
                  << seconds * 1000.0f << " ms\n";
    }
}

TEST(UiCoreTheme, ScalesAscendAndAThemeIsAValue) {
    Theme theme = Dc3Theme();
    EXPECT_LT(theme.space.xs, theme.space.sm);
    EXPECT_LT(theme.space.sm, theme.space.md);
    EXPECT_LT(theme.space.md, theme.space.lg);
    EXPECT_LT(theme.space.lg, theme.space.xl);
    EXPECT_LT(theme.radius.none, theme.radius.small);
    EXPECT_LT(theme.radius.small, theme.radius.medium);
    EXPECT_LT(theme.radius.medium, theme.radius.large);
    EXPECT_LT(theme.motion.fast, theme.motion.normal);
    EXPECT_LT(theme.motion.normal, theme.motion.slow);
    EXPECT_GT(theme.metric.shelfFocusScale, 1.0f);

    const Theme original = theme;
    theme.color.accent = Color::FromRgb8(0xFF0000);
    EXPECT_EQ(original.color.accent, Dc3Theme().color.accent);
    const ColorToken token = &Palette::accent;
    EXPECT_EQ(theme.color.*token, Color::FromRgb8(0xFF0000));
}

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

TEST(UiCoreAnimation, EasingKeepsItsEndsAndNeverGoesBackwards) {
    for (const Easing easing : { Easing::Linear, Easing::EaseOut, Easing::EaseInOut }) {
        EXPECT_EQ(Ease(easing, 0.0f), 0.0f);
        EXPECT_EQ(Ease(easing, 1.0f), 1.0f);
        EXPECT_EQ(Ease(easing, -1.0f), 0.0f);
        EXPECT_EQ(Ease(easing, 2.0f), 1.0f);
        float previous = 0.0f;
        for (int i = 1; i <= 100; ++i) {
            const float value = Ease(easing, static_cast<float>(i) / 100.0f);
            EXPECT_GE(value, previous);
            previous = value;
        }
    }
    EXPECT_GT(Ease(Easing::EaseOut, 0.25f), 0.25f); // fast start
    EXPECT_LT(Ease(Easing::EaseInOut, 0.25f), 0.25f); // gentle start
}

TEST(UiCoreAnimation, TweenArrivesExactlyAndThenRests) {
    Tween tween(10.0f);
    EXPECT_TRUE(tween.AtRest());
    tween.Start(20.0f, 0.5f, Easing::Linear);
    EXPECT_FALSE(tween.AtRest());
    tween.Advance(0.25f);
    EXPECT_FLOAT_EQ(tween.Value(), 15.0f);
    EXPECT_FALSE(tween.AtRest());
    tween.Advance(0.1f);
    tween.Advance(0.1f);
    tween.Advance(0.1f); // 0.55 s in total, past the end
    EXPECT_EQ(tween.Value(), 20.0f);
    EXPECT_TRUE(tween.AtRest());
    tween.Advance(5.0f);
    EXPECT_EQ(tween.Value(), 20.0f);
}

TEST(UiCoreAnimation, TweenIsInSecondsNotSteps) {
    Tween coarse(0.0f);
    Tween fine(0.0f);
    coarse.Start(1.0f, 1.0f, Easing::EaseOut);
    fine.Start(1.0f, 1.0f, Easing::EaseOut);
    coarse.Advance(0.5f);
    for (int i = 0; i < 50; ++i) {
        fine.Advance(0.01f);
    }
    EXPECT_NEAR(coarse.Value(), fine.Value(), 1e-4f);
    // A zero-length tween is already there; a restart continues from here.
    Tween instant(3.0f);
    instant.Start(7.0f, 0.0f, Easing::Linear);
    EXPECT_EQ(instant.Value(), 7.0f);
    EXPECT_TRUE(instant.AtRest());
    coarse.Start(0.0f, 1.0f, Easing::Linear);
    const float before = coarse.Value();
    coarse.Advance(0.0f);
    EXPECT_EQ(coarse.Value(), before);
}

TEST(UiCoreAnimation, SpringConvergesLandsExactlyAndRests) {
    const SpringParams params { 16.0f, 0.05f };
    Spring spring(0.0f);
    EXPECT_TRUE(spring.AtRest());
    spring.SetTarget(100.0f);
    EXPECT_FALSE(spring.AtRest());

    float previous = 0.0f;
    int frames = 0;
    while (!spring.AtRest() && frames < 600) {
        spring.Advance(1.0f / 60.0f, params);
        EXPECT_GE(spring.Value(), previous) << "critically damped: no way back";
        EXPECT_LE(spring.Value(), 100.0f) << "and no overshoot";
        previous = spring.Value();
        ++frames;
    }
    EXPECT_TRUE(spring.AtRest());
    EXPECT_EQ(spring.Value(), 100.0f);
    EXPECT_EQ(spring.Velocity(), 0.0f);
    EXPECT_LT(frames, 60) << "settles within a second";
    EXPECT_GT(frames, 10) << "but is a motion, not a jump";
}

TEST(UiCoreAnimation, SpringDoesNotDependOnTheFrameRate) {
    const SpringParams params { 16.0f, 0.0001f };
    Spring oneStep(0.0f);
    Spring manySteps(0.0f);
    oneStep.SetTarget(100.0f);
    manySteps.SetTarget(100.0f);
    oneStep.Advance(0.1f, params);
    for (int i = 0; i < 100; ++i) {
        manySteps.Advance(0.001f, params);
    }
    EXPECT_NEAR(oneStep.Value(), manySteps.Value(), 0.01f);
    EXPECT_NEAR(oneStep.Velocity(), manySteps.Velocity(), 0.5f);
    EXPECT_GT(oneStep.Value(), 40.0f);
    EXPECT_LT(oneStep.Value(), 60.0f); // (1 - 2.6 e^-1.6) = 0.475 of the way
}

TEST(UiCoreAnimation, SpringCanBeRetargetedMidFlightAndSnapped) {
    const SpringParams params { 16.0f, 0.05f };
    Spring spring(0.0f);
    spring.SetTarget(100.0f);
    spring.Advance(0.05f, params);
    const float velocity = spring.Velocity();
    EXPECT_GT(velocity, 0.0f);
    spring.SetTarget(-50.0f); // turn around: velocity is kept, not reset
    EXPECT_EQ(spring.Velocity(), velocity);
    for (int i = 0; i < 600 && !spring.AtRest(); ++i) {
        spring.Advance(1.0f / 60.0f, params);
    }
    EXPECT_EQ(spring.Value(), -50.0f);
    spring.SetTarget(10.0f);
    spring.Advance(0.0f, params); // no time, no movement
    EXPECT_EQ(spring.Value(), -50.0f);
    spring.Snap(3.0f);
    EXPECT_EQ(spring.Value(), 3.0f);
    EXPECT_TRUE(spring.AtRest());
}

TEST(UiCoreAnimation, ManualClockMovesOnlyWhenTold) {
    ManualClock clock;
    EXPECT_EQ(clock.NowSeconds(), 0.0);
    clock.Advance(0.25);
    clock.Advance(0.25);
    EXPECT_EQ(clock.NowSeconds(), 0.5);
}
