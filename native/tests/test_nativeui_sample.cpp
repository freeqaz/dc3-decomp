// nativeui end to end: the sample song browser driven by events, checked on
// the display list it produces; and one exact display-list golden for a small
// tree.

#include "nativeui_raster.h"
#include "nativeui_test_support.h"

#include "nativeui/Clock.h"
#include "nativeui/HintBar.h"
#include "nativeui/Label.h"
#include "nativeui/Layout.h"
#include "nativeui/Panel.h"
#include "nativeui/SampleBrowser.h"
#include "nativeui/Screen.h"
#include "nativeui/Utf8.h"

#include <cstdlib>
#include <set>

using namespace nativeui;
using namespace nativeui_test;

namespace {

    // The sample browser with a fake measurer and a clock the test turns.
    struct SampleRig {
        FixedAdvanceMeasurer text;
        ManualClock clock;
        SampleBrowser browser;

        explicit SampleRig(Size viewport = { 1280.0f, 720.0f })
            : browser(viewport, text, clock) {}

        const Theme &theme() { return browser.GetScreen().GetTheme(); }

        DisplayList Frame(double seconds = 1.0 / 60.0) {
            clock.Advance(seconds);
            return browser.BuildFrame();
        }

        // Frames at 60 Hz until nothing moves.
        DisplayList Settle() {
            DisplayList list = browser.BuildFrame();
            for (int i = 0; i < 600 && !browser.GetScreen().AtRest(); ++i) {
                list = Frame();
            }
            EXPECT_TRUE(browser.GetScreen().AtRest())
                << "still animating after 10 seconds";
            return list;
        }

        // The songs the list shows, in list order.
        std::vector<int> ListedSongs() {
            std::vector<int> songs;
            for (const SampleBrowser::Entry &entry : browser.Entries()) {
                if (!entry.isHeader) {
                    songs.push_back(entry.song);
                }
            }
            return songs;
        }
    };

    // The filter rule, written again without the framework's helpers (ASCII
    // queries only).
    std::string LowerAscii(std::string text) {
        for (char &c : text) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return text;
    }

    std::set<int>
    SongsContaining(const std::vector<SampleSong> &songs, const std::string &query) {
        std::set<int> out;
        for (int i = 0; i < static_cast<int>(songs.size()); ++i) {
            if (LowerAscii(songs[i].title).find(query) != std::string::npos
                || LowerAscii(songs[i].artist).find(query) != std::string::npos) {
                out.insert(i);
            }
        }
        return out;
    }

    bool Inside(const Rect &inner, const Rect &outer) {
        return inner.Left() >= outer.Left() && inner.Right() <= outer.Right()
            && inner.Top() >= outer.Top() && inner.Bottom() <= outer.Bottom();
    }

}

TEST(UiCoreSample, FirstFrameShowsEveryPart) {
    SampleRig rig;
    const DisplayList frame = rig.Settle();
    const Theme &theme = rig.theme();

    for (const char *expected : { "SONGS",
                                  "TITLE",
                                  "ARTIST",
                                  "SKILL LEVEL",
                                  "SEARCH",
                                  "RECENTLY PLAYED",
                                  "A",
                                  "SELECT",
                                  "B",
                                  "BACK",
                                  "LB",
                                  "RB" }) {
        EXPECT_TRUE(HasText(frame, expected)) << expected;
    }
    EXPECT_EQ(CountText(frame, "SORT"), 2);
    EXPECT_TRUE(ClipsBalanced(frame));

    // The backdrop covers the viewport; the page sits inside its margins.
    const RoundRectFill backdrop = std::get<RoundRectFill>(frame.commands.front());
    EXPECT_EQ(backdrop.rect, (Rect { 0, 0, 1280, 720 }));
    EXPECT_EQ(backdrop.colors.topLeft, theme.color.background);
    EXPECT_FLOAT_EQ(rig.browser.Search().Bounds().Right(), 1280.0f - theme.space.xl);
    EXPECT_FLOAT_EQ(rig.browser.Search().Bounds().width, 320.0f);
    EXPECT_FLOAT_EQ(rig.browser.Recent().Bounds().Left(), theme.space.xl);
    EXPECT_FLOAT_EQ(rig.browser.SongList().Bounds().Left(), theme.space.xl);
    EXPECT_FLOAT_EQ(
        rig.browser.SongList().Bounds().Right(),
        1280.0f - theme.space.xl - 340.0f - theme.space.lg
    );
    for (const TextRun &run : CommandsOf<TextRun>(frame)) {
        EXPECT_LT(run.origin.y, 720.0f - theme.space.lg + 0.01f) << run.text;
        EXPECT_GE(run.origin.x, theme.space.xl - 0.01f) << run.text;
    }

    // 120 songs under letter headings, by title; the list starts focused on
    // its first song and the detail card shows it.
    EXPECT_EQ(rig.browser.Songs().size(), 120u);
    EXPECT_EQ(rig.ListedSongs().size(), 120u);
    EXPECT_EQ(
        rig.browser.SongList().RowCount(), static_cast<int>(rig.browser.Entries().size())
    );
    EXPECT_EQ(rig.browser.GetScreen().Focused(), &rig.browser.SongList());
    ASSERT_TRUE(rig.browser.Entries().front().isHeader);
    EXPECT_EQ(rig.browser.SongList().SelectedIndex(), 1);
    ASSERT_NE(rig.browser.DetailSong(), nullptr);
    EXPECT_EQ(
        rig.browser.DetailSong(), &rig.browser.Songs()[rig.browser.Entries()[1].song]
    );
    EXPECT_TRUE(HasText(frame, rig.browser.Entries().front().heading));
    EXPECT_GE(CountText(frame, utf8::ToUpper(rig.browser.DetailSong()->title)), 2)
        << "once in its row, once on the detail card";

    const std::vector<int> listed = rig.ListedSongs();
    for (std::size_t i = 1; i < listed.size(); ++i) {
        EXPECT_LE(
            utf8::ToUpper(rig.browser.Songs()[listed[i - 1]].title),
            utf8::ToUpper(rig.browser.Songs()[listed[i]].title)
        );
    }

    // 120 songs, but only the rows near the viewport exist or are drawn.
    EXPECT_LT(rig.browser.SongList().BuiltRowCount(), 20);
    EXPECT_LT(CommandsOf<TextRun>(frame).size(), 60u);
    EXPECT_LT(
        CommandsOf<ImageQuad>(frame).size() + CommandsOf<RoundRectFill>(frame).size(), 60u
    );
}

TEST(UiCoreSample, TypingSearchesFromAnywhere) {
    SampleRig rig;
    rig.Settle();
    ASSERT_EQ(rig.browser.GetScreen().Focused(), &rig.browser.SongList());

    EXPECT_TRUE(rig.browser.HandleEvent(Type(U's')));
    EXPECT_TRUE(rig.browser.HandleEvent(Type(U't')));
    EXPECT_EQ(rig.browser.Search().Text(), "st");
    EXPECT_EQ(rig.browser.GetScreen().Focused(), &rig.browser.SongList())
        << "focus stays put";

    const std::set<int> expected = SongsContaining(rig.browser.Songs(), "st");
    ASSERT_GT(expected.size(), 20u);
    ASSERT_LT(expected.size(), 120u);
    const std::vector<int> listed = rig.ListedSongs();
    EXPECT_EQ(std::set<int>(listed.begin(), listed.end()), expected);
    EXPECT_EQ(listed.size(), expected.size()) << "no song listed twice";

    const DisplayList frame = rig.Settle();
    EXPECT_TRUE(HasText(frame, "ST")) << "the search field shows the query";
    EXPECT_FALSE(HasText(frame, "SEARCH")) << "and no longer its placeholder";
    // The selection went back to the first match and the card follows it.
    EXPECT_EQ(rig.browser.SongList().SelectedIndex(), 1);
    EXPECT_FLOAT_EQ(rig.browser.SongList().ScrollOffset(), 0.0f);
    ASSERT_NE(rig.browser.DetailSong(), nullptr);
    EXPECT_EQ(rig.browser.DetailSong(), &rig.browser.Songs()[listed.front()]);
    EXPECT_GE(CountText(frame, utf8::ToUpper(rig.browser.DetailSong()->title)), 2);
    EXPECT_TRUE(
        HasText(frame, utf8::ToUpper(SampleSkillName(rig.browser.DetailSong()->skill)))
    );

    // Deleting the query brings every song back.
    EXPECT_TRUE(rig.browser.HandleEvent(Key(Edit::Backspace)));
    EXPECT_TRUE(rig.browser.HandleEvent(Key(Edit::Backspace)));
    EXPECT_EQ(rig.ListedSongs().size(), 120u);
    EXPECT_TRUE(HasText(rig.Settle(), "SEARCH"));

    // A query nothing matches: an empty list and a card that says so.
    for (const char32_t c : { U'z', U'z', U'z' }) {
        rig.browser.HandleEvent(Type(c));
    }
    EXPECT_TRUE(rig.browser.Entries().empty());
    EXPECT_EQ(rig.browser.DetailSong(), nullptr);
    EXPECT_EQ(rig.browser.SongList().SelectedIndex(), -1);
    const DisplayList none = rig.Settle();
    EXPECT_TRUE(HasText(none, "NO SONGS FOUND"));
    EXPECT_TRUE(ClipsBalanced(none));
}

TEST(UiCoreSample, MovingThroughTheListAndChangingTheSort) {
    SampleRig rig;
    rig.Settle();
    rig.browser.HandleEvent(Type(U's'));
    rig.browser.HandleEvent(Type(U't'));
    rig.Settle();
    ListView &list = rig.browser.SongList();
    const std::vector<SampleBrowser::Entry> &entries = rig.browser.Entries();

    // Down: the next song, and the card changes with it.
    const SampleSong *before = rig.browser.DetailSong();
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::Down)));
    const int afterDown = list.SelectedIndex();
    EXPECT_GT(afterDown, 1);
    ASSERT_FALSE(entries[afterDown].isHeader);
    EXPECT_NE(rig.browser.DetailSong(), before);
    EXPECT_EQ(rig.browser.DetailSong(), &rig.browser.Songs()[entries[afterDown].song]);

    // Page down: several rows at once; the list scrolls there and the
    // highlight ends up inside it, on the selected row.
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::PageDown)));
    const int afterPage = list.SelectedIndex();
    EXPECT_GE(afterPage, afterDown + 4);
    ASSERT_FALSE(entries[afterPage].isHeader);
    EXPECT_FLOAT_EQ(list.ScrollOffset(), 0.0f);
    rig.Frame();
    EXPECT_GT(list.ScrollOffset(), 0.0f);
    EXPECT_FALSE(rig.browser.GetScreen().AtRest());
    const DisplayList paged = rig.Settle();
    EXPECT_GT(list.ScrollOffset(), 0.0f);
    EXPECT_TRUE(Inside(list.HighlightRect(), list.Bounds()));
    const SampleSong &pagedSong = rig.browser.Songs()[entries[afterPage].song];
    EXPECT_EQ(rig.browser.DetailSong(), &pagedSong);
    EXPECT_GE(CountText(paged, utf8::ToUpper(pagedSong.title)), 2);
    // The highlight bar is drawn in the accent colour across the list's width.
    int bars = 0;
    for (const RoundRectFill &fill : CommandsOf<RoundRectFill>(paged)) {
        if (fill.colors.topLeft == rig.theme().color.accent && fill.softness == 0.0f) {
            ++bars;
            EXPECT_NEAR(fill.rect.y, list.HighlightRect().y, 0.01f);
            EXPECT_FLOAT_EQ(fill.rect.x, list.Bounds().x);
            EXPECT_FLOAT_EQ(fill.rect.width, list.Bounds().width);
            EXPECT_FLOAT_EQ(fill.rect.height, rig.theme().metric.listRowHeight);
        }
    }
    EXPECT_EQ(bars, 1);
    EXPECT_LT(list.BuiltRowCount(), 25);

    // Next tab: sorted and sectioned by artist, same search, back at the top.
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::NextTab)));
    EXPECT_EQ(rig.browser.Tabs().SelectedIndex(), SampleBrowser::kByArtist);
    EXPECT_EQ(rig.browser.Search().Text(), "st");
    const std::set<int> expected = SongsContaining(rig.browser.Songs(), "st");
    const std::vector<int> listed = rig.ListedSongs();
    EXPECT_EQ(std::set<int>(listed.begin(), listed.end()), expected);
    std::string heading;
    std::vector<std::string> headings;
    for (const SampleBrowser::Entry &entry : entries) {
        if (entry.isHeader) {
            heading = entry.heading;
            headings.push_back(heading);
        } else {
            EXPECT_EQ(rig.browser.Songs()[entry.song].artist, heading);
        }
    }
    EXPECT_GT(headings.size(), 3u);
    EXPECT_EQ(
        std::set<std::string>(headings.begin(), headings.end()).size(), headings.size()
    ) << "each artist has one section";
    EXPECT_EQ(list.SelectedIndex(), 1);
    const DisplayList byArtist = rig.Settle();
    EXPECT_FLOAT_EQ(list.ScrollOffset(), 0.0f);
    EXPECT_TRUE(HasText(byArtist, utf8::ToUpper(headings.front())));

    // Previous tab twice wraps round to skill level.
    rig.browser.HandleEvent(Act(Action::PrevTab));
    rig.browser.HandleEvent(Act(Action::PrevTab));
    EXPECT_EQ(rig.browser.Tabs().SelectedIndex(), SampleBrowser::kBySkill);
    int lastSkill = -1;
    for (const int song : rig.ListedSongs()) {
        EXPECT_GE(rig.browser.Songs()[song].skill, lastSkill);
        lastSkill = rig.browser.Songs()[song].skill;
    }
}

TEST(UiCoreSample, FocusMovesBetweenTheParts) {
    SampleRig rig;
    rig.Settle();
    Screen &screen = rig.browser.GetScreen();

    // Up from the top of the list leaves it for the shelf above.
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::Up)));
    EXPECT_EQ(screen.Focused(), &rig.browser.Recent());
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::Right)));
    EXPECT_EQ(rig.browser.Recent().SelectedIndex(), 1);
    EXPECT_EQ(rig.browser.DetailSong(), &rig.browser.Songs()[(1 * 13 + 5) % 120]);
    const DisplayList onShelf = rig.Settle();
    EXPECT_EQ(CommandsOf<RoundRectStroke>(onShelf).size(), 4u + 1u + 1u + 1u)
        << "four hint pills, the field, the card, and now the shelf's focus ring";

    // Up again reaches the top bar; text typed there goes to the field directly.
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::Up)));
    EXPECT_TRUE(
        screen.Focused() == &rig.browser.Tabs()
        || screen.Focused() == &rig.browser.Search()
    );
    screen.SetFocus(&rig.browser.Search());
    EXPECT_TRUE(rig.browser.HandleEvent(Type(U'n')));
    EXPECT_EQ(rig.browser.Search().Text(), "n");
    const DisplayList typing = rig.Settle();
    EXPECT_TRUE(HasText(typing, "N"));

    // Down from the field goes back to the shelf, then to the list.
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::Down)));
    EXPECT_EQ(screen.Focused(), &rig.browser.Recent());
    EXPECT_TRUE(rig.browser.HandleEvent(Act(Action::Down)));
    EXPECT_EQ(screen.Focused(), &rig.browser.SongList());
    // Nothing below the list can take focus: the event is nobody's.
    rig.browser.HandleEvent(Act(Action::End));
    EXPECT_FALSE(rig.browser.HandleEvent(Act(Action::Down)));
    EXPECT_EQ(screen.Focused(), &rig.browser.SongList());
    EXPECT_FALSE(rig.browser.HandleEvent(Act(Action::Back)));
}

TEST(UiCoreSample, AFrameAtRestRepeatsExactly) {
    SampleRig rig;
    rig.browser.HandleEvent(Act(Action::PageDown));
    rig.browser.HandleEvent(Act(Action::NextTab));
    const std::string settled = Describe(rig.Settle());
    EXPECT_EQ(Describe(rig.Frame()), settled);
    EXPECT_EQ(Describe(rig.Frame(5.0)), settled) << "however long the gap";
}

TEST(UiCoreSample, FillsWhateverViewportItIsGiven) {
    SampleRig small({ 1280.0f, 720.0f });
    SampleRig big({ 1920.0f, 1080.0f });
    small.Settle();
    const DisplayList frame = big.Settle();
    const Rect smallList = small.browser.SongList().Bounds();
    const Rect bigList = big.browser.SongList().Bounds();
    // The list takes all the extra room; everything else keeps its size.
    EXPECT_FLOAT_EQ(bigList.width - smallList.width, 640.0f);
    EXPECT_FLOAT_EQ(bigList.height - smallList.height, 360.0f);
    EXPECT_FLOAT_EQ(bigList.y, smallList.y);
    EXPECT_FLOAT_EQ(big.browser.Search().Bounds().Right(), 1920.0f - big.theme().space.xl);
    EXPECT_EQ(
        std::get<RoundRectFill>(frame.commands.front()).rect, (Rect { 0, 0, 1920, 1080 })
    );
    EXPECT_GT(
        big.browser.SongList().BuiltRowCount(), small.browser.SongList().BuiltRowCount()
    );
}

// One small tree, every command checked exactly. FixedAdvanceMeasurer: a
// glyph is half its size wide, ascent 0.8, descent 0.2, line height 1.2.
TEST(UiCoreSample, DisplayListGoldenForASmallCard) {
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
// The software rasteriser (a test tool) and a picture of the sample
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

// The sample screen through the software rasteriser. With NATIVEUI_SAMPLE_PNG
// set to a path the picture is also written there:
//   NATIVEUI_SAMPLE_PNG=/tmp/sample_layout.png native/build/milo-tests \
//       --gtest_filter=UiCoreSample.TheLayoutCanBeLookedAtAsAPicture
TEST(UiCoreSample, TheLayoutCanBeLookedAtAsAPicture) {
    SampleRig rig;
    rig.Settle();
    for (int i = 0; i < 3; ++i) {
        rig.browser.HandleEvent(Act(Action::Down));
    }
    const DisplayList frame = rig.Settle();
    Raster raster(1280, 720);
    Replay(frame, raster);

    // The backdrop is the theme's black, and the highlight bar is blue where
    // the row below it is not.
    const Color corner = raster.At(2, 2);
    EXPECT_LT(corner.r + corner.g + corner.b, 0.05f);
    const Rect bar = rig.browser.SongList().HighlightRect();
    const int nearRightEnd = static_cast<int>(bar.Right()) - 4;
    const Color onBar = raster.At(nearRightEnd, static_cast<int>(bar.Center().y));
    const Color belowBar = raster.At(nearRightEnd, static_cast<int>(bar.Bottom()) + 40);
    EXPECT_GT(onBar.b, onBar.r + 0.1f);
    EXPECT_LT(belowBar.b, onBar.b * 0.5f);

    const std::vector<unsigned char> png = EncodePng(1280, 720, raster.Rgb8());
    EXPECT_GT(png.size(), 1280u * 720u * 3u);
    if (const char *path = std::getenv("NATIVEUI_SAMPLE_PNG")) {
        ASSERT_TRUE(WriteFile(path, png)) << path;
    }
}
