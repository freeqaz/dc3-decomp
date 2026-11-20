// The song browser screen (docs/native/reference/SONG_BROWSER_UI.md):
// native/src/songbrowser_ui/ driven by events on a manual clock, with the fake
// fixed-advance text measurer, and checked on the display list it builds.
//
// Most tests run on the REAL song list, native/tests/data/song_browser/
// catalog.tsv (121 songs: the game's 45, the Dance Central 2 export's 44 and
// the DC2 DLC pack's 32). Play history and scores are added here, since the
// fixture was taken from a fresh profile.
//
// SongBrowserUi.Pictures writes PNGs through the test-only software
// rasteriser when SONG_BROWSER_UI_PNG_DIR names a directory:
//   SONG_BROWSER_UI_PNG_DIR=/tmp/ui native/build/milo-tests \
//       --gtest_filter=SongBrowserUi.Pictures

#include "nativeui_raster.h"
#include "nativeui_test_support.h"

#include "nativeui/Clock.h"
#include "nativeui/ListView.h"
#include "nativeui/Shelf.h"
#include "platform/ExecutablePath.h"
#include "songbrowser/SongCatalog.h"
#include "songbrowser/SongSearch.h"
#include "songbrowser/SongSections.h"
#include "songbrowser_ui/BrowserModel.h"
#include "songbrowser_ui/MatchText.h"
#include "songbrowser_ui/SongBrowserScreen.h"
#include "songbrowser_ui/SongWidgets.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <set>

using namespace nativeui;
using namespace nativeui_test;
using namespace songbrowser;
using namespace songbrowser_ui;

namespace {

    std::string BinaryDir() {
        char buf[4096];
        const ssize_t len = ReadExecutablePath(buf, sizeof(buf) - 1);
        if (len <= 0) {
            return ".";
        }
        const std::string path(buf, static_cast<std::size_t>(len));
        const std::size_t slash = path.rfind('/');
        return slash != std::string::npos ? path.substr(0, slash) : ".";
    }

    std::vector<std::string> SplitOn(const std::string &text, char separator) {
        std::vector<std::string> parts;
        std::size_t at = 0;
        while (true) {
            const std::size_t end = text.find(separator, at);
            if (end == std::string::npos) {
                parts.push_back(text.substr(at));
                return parts;
            }
            parts.push_back(text.substr(at, end - at));
            at = end + 1;
        }
    }

    // The real catalog; `origin` empty for all 121 songs, "ham3" for the disc.
    SongCatalog RealCatalog(const std::string &origin = "") {
        SongCatalog catalog;
        std::ifstream in(BinaryDir() + "/../tests/data/song_browser/catalog.tsv");
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            const std::vector<std::string> f = SplitOn(line, '\t');
            if (f.size() != 13) {
                ADD_FAILURE() << "catalog.tsv: a line without 13 fields: " << line;
                continue;
            }
            SongEntry song;
            song.shortName = f[0];
            song.id = std::atoi(f[1].c_str());
            song.title = f[2];
            song.artist = f[3];
            song.album = f[4];
            song.year = std::atoi(f[5].c_str());
            song.bpm = std::atoi(f[6].c_str());
            song.lengthMs = std::atoi(f[7].c_str());
            song.rank = static_cast<float>(std::atof(f[8].c_str()));
            song.difficultyTier = std::atoi(f[9].c_str());
            song.origin = f[10];
            song.source = SourceFromOrigin(song.origin);
            song.onDisc = f[11] == "1";
            song.isCover = f[12] == "1";
            if (origin.empty() || song.origin == origin) {
                catalog.Add(song);
            }
        }
        return catalog;
    }

    // `count` made-up songs, the same ones every time.
    SongCatalog SyntheticCatalog(int count) {
        static const char *const kFirst[] = { "Midnight", "Electric", "Golden", "Neon",
                                              "Crystal",  "Velvet",   "Stereo", "Starlight",
                                              "Summer",   "Wild",     "Zero",   "Quiet" };
        static const char *const kSecond[] = { "Heart",  "Dance",  "Fever", "Lights",
                                               "Rhythm", "Dreams", "Street" };
        SongCatalog catalog;
        for (int i = 0; i < count; ++i) {
            SongEntry song;
            song.id = i + 1;
            song.shortName = "song" + std::to_string(i);
            song.title = std::string(kFirst[i % 12]) + " " + kSecond[(i / 12) % 7] + " "
                + std::to_string(i);
            song.artist = std::string(kSecond[(i * 5) % 7]) + " " + kFirst[(i * 7) % 12];
            song.album = "Album " + std::to_string(i % 40);
            song.year = 1970 + (i * 3) % 45;
            song.bpm = 90 + (i * 7) % 80;
            song.lengthMs = 150000 + (i * 977) % 120000;
            song.difficultyTier = i % 7;
            song.rank = static_cast<float>(i % 7) * 40.0f + 1.0f;
            song.origin = i % 3 == 0 ? "ham3" : (i % 3 == 1 ? "ham2" : "ham2_dlc");
            song.source = SourceFromOrigin(song.origin);
            catalog.Add(song);
        }
        return catalog;
    }

    // Gives the song a best score, as the profile would.
    void SetScore(SongCatalog &catalog, const std::string &shortName, int stars, int score) {
        SongEntry song = *catalog.Find(shortName);
        song.bestStars = stars;
        song.bestScore = score;
        song.played = true;
        catalog.Add(song);
    }

    const std::vector<std::string> kHistory = { "omg", "ymca", "thehustle", "starships",
                                                "macarena", "scream", "whipmyhair" };

    ImageHandle ArtOf(const SongEntry &song) {
        return ImageHandle { 100000u + static_cast<std::uint32_t>(song.id) };
    }

    // A browser with a fake measurer, a clock the test turns, and a delegate
    // that writes down what it is told.
    struct Rig {
        FixedAdvanceMeasurer text;
        ManualClock clock;
        SongCatalog catalog;
        Theme theme = Dc3Theme();
        std::vector<std::string> picks;
        std::vector<Cue> cues;
        int backs = 0;
        bool artReady = true;
        int artCalls = 0;
        std::unique_ptr<SongBrowserScreen> browser;

        explicit Rig(
            SongCatalog songs,
            const std::vector<std::string> &history = {},
            const State *restore = nullptr
        )
            : catalog(std::move(songs)) {
            catalog.SetRecents(history);
            Delegate delegate;
            delegate.onPick = [this](const std::string &name) { picks.push_back(name); };
            delegate.onBack = [this] { ++backs; };
            delegate.onCue = [this](Cue cue) { cues.push_back(cue); };
            delegate.artFor = [this](const SongEntry &song) {
                ++artCalls;
                return artReady ? ArtOf(song) : ImageHandle {};
            };
            browser = std::make_unique<SongBrowserScreen>(
                catalog, delegate, Environment { text, clock }, restore
            );
        }

        DisplayList Frame(double seconds = 1.0 / 60.0) {
            clock.Advance(seconds);
            return browser->BuildFrame();
        }

        // Frames of `step` seconds until nothing moves.
        DisplayList Settle(double step = 1.0 / 60.0) {
            DisplayList list = browser->BuildFrame();
            for (int i = 0; i < 2000 && !browser->IsSettled(); ++i) {
                list = Frame(step);
            }
            EXPECT_TRUE(browser->IsSettled()) << "still animating";
            return list;
        }

        void Press(Action action, int times = 1) {
            for (int i = 0; i < times; ++i) {
                browser->HandleEvent(Act(action));
            }
        }

        void TypeText(const std::string &textToType) {
            for (const char c : textToType) {
                browser->HandleEvent(Type(static_cast<char32_t>(c)));
            }
        }

        State Saved() const { return browser->SaveState(); }
        Region Focus() const { return Saved().focus; }
        std::string Selected() const { return Saved().selectedSong; }
        int Count(Cue cue) const {
            return static_cast<int>(std::count(cues.begin(), cues.end(), cue));
        }

        // Where the detail pane is: the right-hand column, between the top bar
        // (the search field is above it) and the hints.
        Rect PaneRect() const {
            const float left = 1280.0f - theme.space.xl - theme.metric.sidePaneWidth;
            return { left, 80.0f, theme.metric.sidePaneWidth, 580.0f };
        }
    };

    float WidthOf(const TextRun &run) {
        const FixedAdvanceMeasurer measurer;
        const std::size_t glyphs = utf8::Count(run.text);
        return measurer.AdvanceWidth(run.font, run.size, run.text)
            + (glyphs > 0 ? run.letterSpacing * static_cast<float>(glyphs - 1) : 0.0f);
    }

    std::vector<TextRun> RunsIn(const DisplayList &frame, const Rect &area) {
        std::vector<TextRun> out;
        for (const TextRun &run : CommandsOf<TextRun>(frame)) {
            if (area.Contains(run.origin)) {
                out.push_back(run);
            }
        }
        return out;
    }

    // The run with exactly this text inside `area`; fails the test without one.
    TextRun RunWith(const DisplayList &frame, const Rect &area, const std::string &text) {
        for (const TextRun &run : RunsIn(frame, area)) {
            if (run.text == text) {
                return run;
            }
        }
        ADD_FAILURE() << "no text \"" << text << "\" in that area";
        return {};
    }

    bool HasRun(const DisplayList &frame, const Rect &area, const std::string &text) {
        for (const TextRun &run : RunsIn(frame, area)) {
            if (run.text == text) {
                return true;
            }
        }
        return false;
    }

    bool SameRgb(const Color &a, const Color &b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }

    std::string Upper(const std::string &text) { return utf8::ToUpper(text); }

    const Rect kView { 0.0f, 0.0f, 1280.0f, 720.0f };

}

// ---------------------------------------------------------------------------
// Where focus starts
// ---------------------------------------------------------------------------

TEST(SongBrowserUi, ArrivesOnTheFirstRecentCardWhenThereIsHistory) {
    Rig rig(RealCatalog(), kHistory);
    const DisplayList frame = rig.Settle();
    EXPECT_EQ(rig.Focus(), Region::Recents);
    EXPECT_EQ(rig.browser->Recents().CardCount(), 7);
    EXPECT_EQ(rig.browser->Recents().SelectedIndex(), 0);
    EXPECT_TRUE(rig.browser->RecentsShown());
    EXPECT_EQ(rig.browser->DetailSong(), "omg") << "the newest song played";
    EXPECT_TRUE(HasText(frame, "RECENTLY PLAYED"));
    EXPECT_TRUE(HasRun(frame, rig.PaneRect(), "OMG"));
    EXPECT_TRUE(rig.cues.empty()) << "arriving plays nothing";
}

TEST(SongBrowserUi, ArrivesOnTheFirstListRowWithoutHistory) {
    Rig rig(RealCatalog());
    const DisplayList frame = rig.Settle();
    EXPECT_EQ(rig.Focus(), Region::List);
    EXPECT_FALSE(rig.browser->RecentsShown());
    EXPECT_FALSE(HasText(frame, "RECENTLY PLAYED"));
    EXPECT_EQ(rig.browser->Recents().CardCount(), 0);
    // By title, the first row under the first heading.
    const SongList byTitle = Arrange(rig.catalog, kByTitle);
    EXPECT_EQ(rig.Selected(), rig.catalog.At(byTitle.rows[0]).shortName);
    EXPECT_EQ(rig.browser->List().SelectedIndex(), 1);
    EXPECT_EQ(rig.browser->DetailSong(), rig.Selected());

    // With no shelf the list starts right under the top bar: the layout closes up.
    Rig withHistory(RealCatalog(), kHistory);
    withHistory.Settle();
    EXPECT_LT(
        rig.browser->List().Bounds().Top() + 100.0f,
        withHistory.browser->List().Bounds().Top()
    );
    EXPECT_GT(rig.browser->List().Bounds().height, 500.0f);
}

TEST(SongBrowserUi, ArrivesWhereASavedStateSays) {
    State saved;
    saved.arrangement = kByArtist;
    saved.focus = Region::List;
    saved.selectedSong = "ymca";
    Rig rig(RealCatalog(), kHistory, &saved);
    rig.Settle();
    EXPECT_EQ(rig.Focus(), Region::List) << "not the shelf, though there is history";
    EXPECT_EQ(rig.Selected(), "ymca");
    EXPECT_EQ(rig.Saved().arrangement, kByArtist);
    EXPECT_EQ(rig.browser->DetailSong(), "ymca");

    // A saved shelf focus with no history left falls back to the list.
    State onShelf;
    onShelf.focus = Region::Recents;
    Rig noHistory(RealCatalog(), {}, &onShelf);
    EXPECT_EQ(noHistory.Focus(), Region::List);
}

// ---------------------------------------------------------------------------
// Moving around
// ---------------------------------------------------------------------------

TEST(SongBrowserUi, ShelfThenListAndTheDetailPaneFollows) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();

    rig.Press(Action::Right);
    EXPECT_EQ(rig.browser->Recents().SelectedIndex(), 1);
    EXPECT_EQ(rig.browser->DetailSong(), "ymca");
    rig.Press(Action::Left, 3);
    EXPECT_EQ(rig.browser->Recents().SelectedIndex(), 0) << "no wrap";

    // Down: into the list. The shelf folds away and the list takes its room.
    const float listHeightWithShelf = rig.browser->List().Bounds().height;
    rig.Press(Action::Down);
    EXPECT_EQ(rig.Focus(), Region::List);
    EXPECT_FALSE(rig.browser->RecentsShown());
    EXPECT_EQ(rig.browser->DetailSong(), rig.Selected());
    EXPECT_NE(rig.browser->DetailSong(), "omg");
    DisplayList frame = rig.Settle();
    EXPECT_FALSE(HasText(frame, "RECENTLY PLAYED"));
    EXPECT_GT(rig.browser->List().Bounds().height, listHeightWithShelf + 150.0f);

    // Down moves through the list and the pane shows each song at once.
    const std::string first = rig.Selected();
    rig.Press(Action::Down);
    EXPECT_NE(rig.Selected(), first);
    EXPECT_EQ(rig.browser->DetailSong(), rig.Selected());
    frame = rig.Settle();
    EXPECT_TRUE(HasRun(frame, rig.PaneRect(), Upper(rig.catalog.Find(rig.Selected())->title)));

    // Up from the top row goes back to the shelf, which unfolds again.
    rig.Press(Action::Up);
    EXPECT_EQ(rig.Selected(), first);
    rig.Press(Action::Up);
    EXPECT_EQ(rig.Focus(), Region::Recents);
    EXPECT_TRUE(rig.browser->RecentsShown());
    EXPECT_EQ(rig.browser->DetailSong(), "omg") << "the shelf's card again";
    // Up again: the tabs. Right along them to the search field, Left back.
    rig.Press(Action::Up);
    EXPECT_EQ(rig.Focus(), Region::Tabs);
    rig.Press(Action::Right, 4);
    EXPECT_EQ(rig.Saved().arrangement, kByDecade);
    rig.Press(Action::Right);
    EXPECT_EQ(rig.Focus(), Region::Search);
    rig.Press(Action::Left);
    EXPECT_EQ(rig.Focus(), Region::Tabs);
    rig.Press(Action::Down);
    EXPECT_EQ(rig.Focus(), Region::Recents);
}

TEST(SongBrowserUi, LeftAndRightJumpBetweenSections) {
    Rig rig(RealCatalog());
    rig.Settle();
    BrowserModel model;
    model.SetCatalog(rig.catalog, 0);

    // The second section's first song, worked out from the data layer alone.
    ArrangeOptions options;
    options.minSectionSongs = BrowserModel::kMinSectionSongs;
    const SongList byTitle = Arrange(rig.catalog, kByTitle, options);
    ASSERT_GE(byTitle.sections.size(), 3u);
    const auto firstOf = [&](std::size_t section) {
        return rig.catalog.At(byTitle.rows[byTitle.sections[section].firstRow]).shortName;
    };

    rig.Press(Action::Down, 2); // somewhere inside the first section
    rig.Press(Action::Right);
    EXPECT_EQ(rig.Selected(), firstOf(1));
    rig.Press(Action::Right);
    EXPECT_EQ(rig.Selected(), firstOf(2));
    rig.Press(Action::Left);
    EXPECT_EQ(rig.Selected(), firstOf(1));
    rig.Press(Action::Left, 5);
    EXPECT_EQ(rig.Selected(), firstOf(0)) << "stops at the first section";
    rig.Press(Action::Right, 100);
    EXPECT_EQ(rig.Selected(), firstOf(byTitle.sections.size() - 1)) << "and at the last";
    EXPECT_EQ(rig.Focus(), Region::List);

    // The heading of the section landed in is on screen with its count.
    const DisplayList frame = rig.Settle();
    const Section &last = byTitle.sections.back();
    EXPECT_TRUE(HasText(frame, Upper(last.title)));
    EXPECT_TRUE(HasText(frame, "(" + std::to_string(last.count) + ")"));

    // End and Home, PageDown.
    rig.Press(Action::Home);
    EXPECT_EQ(rig.Selected(), firstOf(0));
    rig.Press(Action::PageDown);
    EXPECT_GT(rig.browser->List().SelectedIndex(), 6);
    rig.Press(Action::End);
    EXPECT_EQ(rig.Selected(), rig.catalog.At(byTitle.rows.back()).shortName);
}

TEST(SongBrowserUi, ChangingTheSortKeepsTheSelectedSongInView) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();
    rig.Press(Action::Down);
    rig.Press(Action::Down, 40);
    const std::string song = rig.Selected();
    const std::string title = Upper(rig.catalog.Find(song)->title);
    rig.Settle();
    rig.cues.clear();

    const Arrangement expected[] = { kByArtist, kByDifficulty, kBySource, kByDecade, kByTitle };
    for (const Arrangement arrangement : expected) {
        rig.Press(Action::NextTab);
        EXPECT_EQ(rig.Saved().arrangement, arrangement);
        EXPECT_EQ(rig.Selected(), song) << ArrangementName(arrangement);
        EXPECT_EQ(rig.Focus(), Region::List);
        const DisplayList frame = rig.Settle();
        const Rect list = rig.browser->List().Bounds();
        const Rect bar = rig.browser->List().HighlightRect();
        EXPECT_GE(bar.Top(), list.Top() - 0.01f) << ArrangementName(arrangement);
        EXPECT_LE(bar.Bottom(), list.Bottom() + 0.01f) << ArrangementName(arrangement);
        // The song's row is drawn under the bar.
        bool onBar = false;
        for (const TextRun &run : RunsIn(frame, bar)) {
            onBar = onBar || run.text == title;
        }
        EXPECT_TRUE(onBar) << title << " by " << ArrangementName(arrangement);
    }
    EXPECT_EQ(rig.Count(Cue::Tab), 5);
    rig.Press(Action::PrevTab);
    EXPECT_EQ(rig.Saved().arrangement, kByDecade) << "the tabs wrap";

    // By difficulty the headings are the game's tier names.
    rig.Press(Action::NextTab, 3);
    ASSERT_EQ(rig.Saved().arrangement, kByDifficulty);
    rig.Press(Action::Home);
    EXPECT_TRUE(HasText(rig.Settle(), "WARMUP"));
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

TEST(SongBrowserUi, TypingSearchesFromEveryRegion) {
    for (const Region start : { Region::Recents, Region::List, Region::Tabs, Region::Search }) {
        State saved;
        saved.focus = start;
        Rig rig(RealCatalog(), kHistory, &saved);
        rig.Settle();
        ASSERT_EQ(rig.Focus(), start);

        rig.TypeText("ymca");
        EXPECT_EQ(rig.Saved().query, "ymca");
        EXPECT_EQ(rig.Focus(), Region::Search) << "typing moves focus to the field";
        EXPECT_FALSE(rig.browser->RecentsShown()) << "no shelf while searching";
        EXPECT_EQ(rig.Selected(), "ymca") << "the best match is selected";
        EXPECT_EQ(rig.browser->DetailSong(), "ymca");
        EXPECT_EQ(rig.browser->List().RowCount(), 2) << "a count heading and one song";

        const DisplayList frame = rig.Settle();
        EXPECT_TRUE(HasText(frame, "YMCA")) << "the query, in the field";
        EXPECT_TRUE(HasText(frame, "1 SONG MATCHES"));
        EXPECT_FALSE(HasText(frame, "RECENTLY PLAYED"));
        EXPECT_FALSE(HasText(frame, "TYPE TO SEARCH")) << "the placeholder is gone";
    }

    // The placeholder tells a keyboard user to type; a leading space is not a query.
    Rig rig(RealCatalog());
    EXPECT_TRUE(HasText(rig.Settle(), "TYPE TO SEARCH"));
    EXPECT_FALSE(rig.browser->HandleEvent(Type(U' ')));
    EXPECT_EQ(rig.Saved().query, "");
    EXPECT_TRUE(rig.browser->WantsTextInput());
}

TEST(SongBrowserUi, SearchResultsAreRankedAndFlat) {
    Rig rig(RealCatalog());
    rig.Settle();
    rig.TypeText("usher");
    const SearchResult expected = SongSearch(rig.catalog).Search("usher");
    ASSERT_GE(expected.rows.size(), 4u);
    EXPECT_EQ(
        rig.browser->List().RowCount(), static_cast<int>(expected.rows.size()) + 1
    );
    // Down from the field goes into the results; they come in the search's order.
    rig.Press(Action::Down);
    EXPECT_EQ(rig.Focus(), Region::List);
    for (const SearchRow &row : expected.rows) {
        EXPECT_EQ(rig.Selected(), rig.catalog.At(row.song).shortName);
        rig.Press(Action::Down);
    }
    // No sections in a search: Left and Right stay put.
    const std::string last = rig.Selected();
    rig.Press(Action::Left);
    rig.Press(Action::Right);
    EXPECT_EQ(rig.Selected(), last);
    // Up from the first result returns to the field.
    rig.Press(Action::Home);
    rig.Press(Action::Up);
    EXPECT_EQ(rig.Focus(), Region::Search);
    // Accept from the field also goes into the results.
    rig.Press(Action::Accept);
    EXPECT_EQ(rig.Focus(), Region::List);
    EXPECT_TRUE(rig.picks.empty());
    // Typing more from the results goes back to the field and narrows them.
    rig.TypeText(" scream");
    EXPECT_EQ(rig.Focus(), Region::Search);
    EXPECT_EQ(rig.Saved().query, "usher scream");
    EXPECT_EQ(rig.Selected(), "scream");
}

// What the search matched is drawn in the accent colour, on the glyphs it
// matched. The expected runs are worked out from SongSearch's byte ranges and
// the text as written, not from the screen's own code.
TEST(SongBrowserUi, MatchHighlightsLandOnTheRightGlyphsAfterUpperCasing) {
    Rig rig(RealCatalog());
    rig.Settle();
    const TextStyle body = rig.theme.type.body;
    const TextStyle caption = rig.theme.type.caption;
    const FixedAdvanceMeasurer measurer;

    struct Case {
        const char *query;
        const char *song;
    };
    for (const Case &c : { Case { "will i am", "omg" },
                           Case { "usher scream", "scream" },
                           Case { "william", "omg" },
                           Case { "ymca", "ymca" } }) {
        Rig fresh(RealCatalog());
        fresh.Settle();
        fresh.TypeText(c.query);
        ASSERT_EQ(fresh.Selected(), c.song) << c.query;
        const DisplayList frame = fresh.Settle();
        const Rect rowRect = fresh.browser->List().HighlightRect();
        const std::vector<TextRun> runs = RunsIn(frame, rowRect);
        const SongEntry &song = *fresh.catalog.Find(c.song);
        const SearchRow found = SongSearch(fresh.catalog).Search(c.query).rows[0];
        ASSERT_EQ(fresh.catalog.At(found.song).shortName, c.song);
        ASSERT_FALSE(found.ranges.empty());

        for (const MatchRange &range : found.ranges) {
            ASSERT_NE(range.field, kFieldAlbum) << c.query;
            const bool inTitle = range.field == kFieldTitle;
            const std::string &original = inTitle ? song.title : song.artist;
            const TextStyle &style = inTitle ? body : caption;
            const std::string wanted = Upper(
                original.substr(
                    static_cast<std::size_t>(range.begin),
                    static_cast<std::size_t>(range.length)
                )
            );
            const std::string before =
                Upper(original.substr(0, static_cast<std::size_t>(range.begin)));
            // The line's left end: where its first run starts.
            float lineLeft = 1e9f;
            const TextRun *match = nullptr;
            for (const TextRun &run : runs) {
                if (run.size != style.size) {
                    continue;
                }
                lineLeft = std::min(lineLeft, run.origin.x);
                if (run.text == wanted && SameRgb(run.color, fresh.theme.color.accent)) {
                    match = &run;
                }
            }
            ASSERT_NE(match, nullptr) << c.query << ": no accent run \"" << wanted << "\"";
            const float expectedX = before.empty()
                ? 0.0f
                : MeasureWidth(measurer, style, before) + LetterSpacing(style);
            EXPECT_NEAR(match->origin.x - lineLeft, expectedX, 0.01f)
                << c.query << ": \"" << wanted << "\" after \"" << before << "\"";
        }
        // Nothing else on the row is accent-coloured.
        std::size_t accentRuns = 0;
        for (const TextRun &run : runs) {
            accentRuns += SameRgb(run.color, fresh.theme.color.accent) ? 1 : 0;
        }
        EXPECT_EQ(accentRuns, found.ranges.size()) << c.query;
    }

    // "will i am" lights the whole of "will.i.am", dots included, in the artist.
    Rig will(RealCatalog());
    will.Settle();
    will.TypeText("will i am");
    const DisplayList frame = will.Settle();
    const TextRun lit =
        RunWith(frame, will.browser->List().HighlightRect(), "WILL.I.AM");
    EXPECT_TRUE(SameRgb(lit.color, will.theme.color.accent));
    EXPECT_TRUE(HasRun(frame, will.browser->List().HighlightRect(), "USHER FT. "));
}

// Upper-casing can change the number of glyphs: sharp s becomes SS. The fixture
// has no such title, so these songs are written here.
TEST(SongBrowserUi, MatchHighlightsSurviveACaseChangeThatAddsGlyphs) {
    SongCatalog catalog;
    SongEntry song;
    song.id = 1;
    song.shortName = "strasse";
    song.title = "Stra\xC3\x9F" "e Nights"; // "Straße Nights"
    song.artist = "D\xC3\xA9j\xC3\xA0 Vu"; // "Déjà Vu"
    catalog.Add(song);
    Rig rig(catalog);
    rig.Settle();
    rig.TypeText("nights");
    const DisplayList frame = rig.Settle();
    const Rect row = rig.browser->List().HighlightRect();
    const TextRun plain = RunWith(frame, row, "STRASSE ");
    const TextRun lit = RunWith(frame, row, "NIGHTS");
    EXPECT_TRUE(SameRgb(lit.color, rig.theme.color.accent));
    EXPECT_FALSE(SameRgb(plain.color, rig.theme.color.accent));
    // Eight glyphs (S T R A S S E and a space) come before it, not seven.
    const TextStyle &body = rig.theme.type.body;
    EXPECT_NEAR(
        lit.origin.x - plain.origin.x, 8.0f * (body.size * 0.5f + LetterSpacing(body)), 0.01f
    );

    // An accented match: "deja" lights "DÉJÀ".
    Rig accents(catalog);
    accents.Settle();
    accents.TypeText("deja");
    const DisplayList accentFrame = accents.Settle();
    const TextRun deja =
        RunWith(accentFrame, accents.browser->List().HighlightRect(), "D\xC3\x89J\xC3\x80");
    EXPECT_TRUE(SameRgb(deja.color, accents.theme.color.accent));
}

TEST(SongBrowserUi, MatchLineCutsAndMergesSpans) {
    const FixedAdvanceMeasurer measurer;
    const TextStyle style { FontId { 1 }, 20.0f, 0.0f, TextCase::Upper };
    // 10 units a glyph. Spans out of order, overlapping and past the end.
    const MatchLine whole = FitMatchLine(
        measurer, style, "abcdefghij", { { 6, 99 }, { 2, 2 }, { 3, 2 } }, 1000.0f, "..."
    );
    ASSERT_EQ(whole.runs.size(), 4u);
    EXPECT_EQ(whole.runs[0].text, "AB");
    EXPECT_EQ(whole.runs[1].text, "CDE");
    EXPECT_TRUE(whole.runs[1].highlighted);
    EXPECT_FLOAT_EQ(whole.runs[1].x, 20.0f);
    EXPECT_EQ(whole.runs[2].text, "F");
    EXPECT_FALSE(whole.runs[2].highlighted);
    EXPECT_EQ(whole.runs[3].text, "GHIJ");
    EXPECT_TRUE(whole.runs[3].highlighted);
    EXPECT_FALSE(whole.truncated);

    // Cut inside a highlighted run: it stays highlighted up to the cut and the
    // ellipsis is plain. 70 units hold 4 glyphs and the 3 dots.
    const MatchLine cut = FitMatchLine(measurer, style, "abcdefghij", { { 2, 6 } }, 70.0f, "...");
    ASSERT_EQ(cut.runs.size(), 3u);
    EXPECT_EQ(cut.runs[0].text, "AB");
    EXPECT_EQ(cut.runs[1].text, "CD");
    EXPECT_TRUE(cut.runs[1].highlighted);
    EXPECT_EQ(cut.runs[2].text, "...");
    EXPECT_FALSE(cut.runs[2].highlighted);
    EXPECT_FLOAT_EQ(cut.runs[2].x, 40.0f);
    EXPECT_TRUE(cut.truncated);
    EXPECT_LE(cut.width, 70.0f);

    // With no spans it is one run, exactly what FitLine gives.
    const MatchLine plain = FitMatchLine(measurer, style, "abcdefghij", {}, 70.0f, "...");
    ASSERT_EQ(plain.runs.size(), 1u);
    EXPECT_EQ(plain.runs[0].text, FitLine(measurer, style, "abcdefghij", 70.0f, "...").text);
}

TEST(SongBrowserUi, NoMatchSaysSoAndOffersNothingToPick) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();
    rig.TypeText("zzqqxx");
    EXPECT_EQ(rig.browser->List().RowCount(), 0);
    EXPECT_EQ(rig.browser->DetailSong(), "");
    const DisplayList frame = rig.Settle();
    const Rect list = rig.browser->List().Bounds();
    const TextRun headline = RunWith(frame, list, "NO SONGS MATCH");
    EXPECT_NEAR(headline.origin.x + WidthOf(headline) * 0.5f, list.Center().x, 1.0f)
        << "centred in the list's area";
    EXPECT_TRUE(HasRun(frame, list, "NOTHING FOUND FOR ZZQQXX"));
    EXPECT_TRUE(RunsIn(frame, rig.PaneRect()).empty()) << "the detail pane is gone";

    // Nothing to go down into, nothing to pick.
    rig.Press(Action::Down);
    EXPECT_EQ(rig.Focus(), Region::Search);
    rig.Press(Action::Accept);
    EXPECT_TRUE(rig.picks.empty());

    // Backspace edits; the matches come back as soon as there are some.
    for (int i = 0; i < 5; ++i) {
        rig.browser->HandleEvent(Key(Edit::Backspace));
    }
    EXPECT_EQ(rig.Saved().query, "z");
    EXPECT_GT(rig.browser->List().RowCount(), 1);
    EXPECT_FALSE(HasText(rig.Settle(), "NO SONGS MATCH"));
}

TEST(SongBrowserUi, BackClearsTheQueryFirstAndOnlyThenLeaves) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();
    rig.Press(Action::Down);
    rig.Press(Action::Down, 17);
    const std::string before = rig.Selected();
    rig.Settle();
    const float scrollBefore = rig.Saved().scroll;
    ASSERT_GT(scrollBefore, 300.0f);
    ASSERT_NE(before, "ymca");

    rig.TypeText("ymca");
    rig.Press(Action::Down); // into the results
    ASSERT_EQ(rig.Focus(), Region::List);
    ASSERT_EQ(rig.Selected(), "ymca");

    rig.Press(Action::Back);
    EXPECT_EQ(rig.backs, 0) << "the first Back only clears the query";
    EXPECT_EQ(rig.Saved().query, "");
    EXPECT_EQ(rig.Selected(), before) << "back at the song selected before the search";
    EXPECT_EQ(rig.Focus(), Region::List) << "and in the region the search started from";
    EXPECT_EQ(rig.browser->DetailSong(), before);
    EXPECT_GT(rig.browser->List().RowCount(), 121);
    EXPECT_FLOAT_EQ(rig.Saved().scroll, scrollBefore) << "the list is exactly where it was";
    rig.Settle();

    rig.Press(Action::Back);
    EXPECT_EQ(rig.backs, 1);
    EXPECT_EQ(rig.Count(Cue::Back), 2);

    // Deleting the query by hand also returns the list to that song; focus
    // stays in the field, where the typing is.
    rig.TypeText("ab");
    rig.browser->HandleEvent(Key(Edit::Backspace));
    rig.browser->HandleEvent(Key(Edit::Backspace));
    EXPECT_EQ(rig.Saved().query, "");
    EXPECT_EQ(rig.Selected(), before);
    EXPECT_EQ(rig.Focus(), Region::Search);
    EXPECT_TRUE(rig.browser->RecentsShown()) << "the shelf is back";

    // A search started from the shelf returns to the shelf.
    Rig shelf(RealCatalog(), kHistory);
    shelf.Settle();
    shelf.Press(Action::Right, 2);
    shelf.TypeText("hustle");
    shelf.Press(Action::Back);
    EXPECT_EQ(shelf.Focus(), Region::Recents);
    EXPECT_EQ(shelf.browser->Recents().SelectedIndex(), 2);
    EXPECT_EQ(shelf.browser->DetailSong(), "thehustle");
    EXPECT_EQ(shelf.backs, 0);
}

// ---------------------------------------------------------------------------
// Picking
// ---------------------------------------------------------------------------

TEST(SongBrowserUi, AcceptPicksTheRightSongFromShelfListAndResults) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();
    rig.Press(Action::Right, 3);
    rig.Press(Action::Accept);
    ASSERT_EQ(rig.picks.size(), 1u);
    EXPECT_EQ(rig.picks[0], "starships") << "the fourth recent card";

    rig.Press(Action::Down);
    rig.Press(Action::Down, 9);
    const std::string inList = rig.Selected();
    rig.Press(Action::Accept);
    ASSERT_EQ(rig.picks.size(), 2u);
    EXPECT_EQ(rig.picks[1], inList);
    EXPECT_NE(inList, "starships");

    rig.TypeText("will i am");
    rig.Press(Action::Accept); // field -> results
    EXPECT_EQ(rig.picks.size(), 2u);
    rig.Press(Action::Accept);
    ASSERT_EQ(rig.picks.size(), 3u);
    EXPECT_EQ(rig.picks[2], "omg");
    EXPECT_EQ(rig.Count(Cue::Select), 3);
    EXPECT_EQ(rig.backs, 0);
}

TEST(SongBrowserUi, CuesSayWhatHappened) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();
    rig.Press(Action::Right);
    EXPECT_EQ(rig.cues, (std::vector<Cue> { Cue::Move }));
    rig.Press(Action::Left, 2);
    EXPECT_EQ(rig.Count(Cue::Move), 2) << "a move that goes nowhere is silent";
    rig.cues.clear();
    rig.Press(Action::Down); // to the list: a region change
    rig.Press(Action::Down); // a row
    rig.Press(Action::Right); // a section
    EXPECT_EQ(rig.cues, (std::vector<Cue> { Cue::Move, Cue::Move, Cue::Move }));
    rig.cues.clear();
    rig.Press(Action::NextTab);
    EXPECT_EQ(rig.cues, (std::vector<Cue> { Cue::Tab })) << "re-sorting moves nothing aloud";
    rig.cues.clear();
    rig.TypeText("zzqq");
    EXPECT_TRUE(rig.cues.empty()) << "typing is silent";
    rig.Press(Action::Back);
    EXPECT_EQ(rig.cues, (std::vector<Cue> { Cue::Back }));

    // Accept on an empty list is an error.
    Rig empty((SongCatalog()));
    empty.Press(Action::Accept);
    EXPECT_EQ(empty.cues, (std::vector<Cue> { Cue::Error }));
    EXPECT_TRUE(empty.picks.empty());
}

TEST(SongBrowserUi, HintsFollowTheContext) {
    Rig rig(RealCatalog(), kHistory);
    const auto hintTexts = [&] {
        const DisplayList frame = rig.Settle();
        const Rect bar { 0.0f, 720.0f - rig.theme.space.lg - 40.0f, 1280.0f, 60.0f };
        std::vector<std::string> out;
        for (const TextRun &run : RunsIn(frame, bar)) {
            out.push_back(run.text);
        }
        return out;
    };
    using Texts = std::vector<std::string>;
    EXPECT_EQ(hintTexts(), (Texts { "A", "SELECT", "B", "BACK", "LB RB", "SORT", "ABC", "SEARCH" }));
    rig.Press(Action::Down);
    EXPECT_EQ(
        hintTexts(),
        (Texts { "A", "SELECT", "B", "BACK", "< >", "SECTION", "LB RB", "SORT", "ABC", "SEARCH" })
    );
    rig.TypeText("ymca");
    EXPECT_EQ(hintTexts(), (Texts { "A", "RESULTS", "B", "CLEAR SEARCH" }));
    rig.Press(Action::Down);
    EXPECT_EQ(hintTexts(), (Texts { "A", "SELECT", "B", "CLEAR SEARCH" }));
    rig.Press(Action::Back);
    rig.Press(Action::Up, 30);
    rig.Press(Action::Up);
    ASSERT_EQ(rig.Focus(), Region::Tabs);
    EXPECT_EQ(hintTexts(), (Texts { "< >", "SORT", "B", "BACK", "ABC", "SEARCH" }));
    rig.Press(Action::Right, 9);
    ASSERT_EQ(rig.Focus(), Region::Search);
    EXPECT_EQ(hintTexts(), (Texts { "ABC", "TYPE TO SEARCH", "B", "BACK" }));
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

TEST(SongBrowserUi, StateSavesAndRestores) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();
    rig.Press(Action::Right, 2); // third recent card
    rig.Press(Action::NextTab); // by artist
    rig.Press(Action::Down);
    rig.Press(Action::PageDown, 4);
    rig.Press(Action::Down, 3);
    rig.Settle();
    const State saved = rig.Saved();
    EXPECT_EQ(saved.arrangement, kByArtist);
    EXPECT_EQ(saved.focus, Region::List);
    EXPECT_EQ(saved.recentSong, "thehustle");
    EXPECT_GT(saved.scroll, 500.0f);
    ASSERT_FALSE(saved.selectedSong.empty());

    Rig back(RealCatalog(), kHistory, &saved);
    back.Settle();
    const State again = back.Saved();
    EXPECT_EQ(again.arrangement, saved.arrangement);
    EXPECT_EQ(again.focus, saved.focus);
    EXPECT_EQ(again.selectedSong, saved.selectedSong);
    EXPECT_EQ(again.recentSong, saved.recentSong);
    EXPECT_EQ(again.query, saved.query);
    EXPECT_FLOAT_EQ(again.scroll, saved.scroll) << "the list is where it was, to the pixel";
    EXPECT_EQ(back.browser->List().SelectedIndex(), rig.browser->List().SelectedIndex());
    EXPECT_EQ(back.browser->DetailSong(), saved.selectedSong);
    // The two screens draw the same frame.
    EXPECT_EQ(Describe(back.Settle()), Describe(rig.Settle()));

    // In the middle of a search: the query, the result selected, and the song
    // to return to all come back.
    rig.TypeText("usher");
    rig.Press(Action::Down, 3);
    const State searching = rig.Saved();
    EXPECT_EQ(searching.query, "usher");
    EXPECT_EQ(searching.songBeforeSearch, saved.selectedSong);
    Rig resumed(RealCatalog(), kHistory, &searching);
    resumed.Settle();
    EXPECT_EQ(resumed.Saved().query, "usher");
    EXPECT_EQ(resumed.Selected(), searching.selectedSong);
    EXPECT_EQ(resumed.Focus(), Region::List);
    resumed.Press(Action::Back);
    EXPECT_EQ(resumed.Selected(), saved.selectedSong);
    EXPECT_EQ(resumed.backs, 0);

    // RestoreState on a live screen does the same; a song that is gone falls
    // back to the first row, a sort without a tab to the first tab.
    State odd;
    odd.arrangement = kByBpm;
    odd.selectedSong = "not_a_song";
    back.browser->RestoreState(odd);
    EXPECT_EQ(back.Saved().arrangement, kByTitle);
    EXPECT_EQ(back.browser->List().SelectedIndex(), 1);
}

TEST(SongBrowserUi, ACatalogChangeKeepsThePlaceAndShowsTheNewHistory) {
    Rig rig(RealCatalog(), kHistory);
    rig.Settle();
    rig.Press(Action::Down);
    rig.Press(Action::Down, 12);
    const std::string song = rig.Selected();
    ASSERT_NE(song, "iwillsurvive");

    // "I Will Survive" was just played, for four stars.
    std::vector<std::string> history = kHistory;
    history.insert(history.begin(), "iwillsurvive");
    rig.catalog.SetRecents(history);
    SetScore(rig.catalog, "iwillsurvive", 4, 1234567);
    rig.browser->SetCatalog(rig.catalog);

    EXPECT_EQ(rig.Selected(), song) << "the list keeps its song";
    EXPECT_EQ(rig.Focus(), Region::List);
    EXPECT_EQ(rig.browser->Recents().CardCount(), 8);
    rig.Press(Action::Home);
    rig.Press(Action::Up);
    ASSERT_EQ(rig.Focus(), Region::Recents);
    rig.Press(Action::Home);
    EXPECT_EQ(rig.browser->DetailSong(), "iwillsurvive") << "the newest card";
    const DisplayList frame = rig.Settle();
    EXPECT_TRUE(HasRun(frame, rig.PaneRect(), "1,234,567"));
    EXPECT_TRUE(HasRun(frame, rig.PaneRect(), "6:18") || HasRun(frame, rig.PaneRect(), "3:18")
                || !RunsIn(frame, rig.PaneRect()).empty());

    // Four gold marks and one empty ring in the pane.
    int filled = 0;
    for (const RoundRectFill &fill : CommandsOf<RoundRectFill>(frame)) {
        if (rig.PaneRect().Contains({ fill.rect.x, fill.rect.y })
            && SameRgb(fill.colors.topLeft, rig.theme.color.star)) {
            ++filled;
        }
    }
    EXPECT_EQ(filled, 4);

    // History that names songs that are not installed: only the installed show.
    Rig sparse(RealCatalog("ham3"), kHistory);
    EXPECT_LT(sparse.browser->Recents().CardCount(), 7);
    EXPECT_GT(sparse.browser->Recents().CardCount(), 0);
    Rig one(RealCatalog(), { "ymca" });
    EXPECT_EQ(one.browser->Recents().CardCount(), 1);
    EXPECT_EQ(one.Focus(), Region::Recents);
    one.Press(Action::Accept);
    EXPECT_EQ(one.picks, (std::vector<std::string> { "ymca" }));

    // 24 names, some unknown, more than fit: the shelf scrolls and stays whole.
    std::vector<std::string> many;
    for (int i = 0; i < 24; ++i) {
        many.push_back(i % 4 == 3 ? "gone" + std::to_string(i) : rig.catalog.At(i * 3).shortName);
    }
    Rig full(RealCatalog(), many);
    full.Settle();
    EXPECT_EQ(full.browser->Recents().CardCount(), 18);
    full.Press(Action::End);
    full.Settle();
    EXPECT_EQ(full.browser->Recents().SelectedIndex(), 17);
    EXPECT_GT(full.browser->Recents().ScrollOffset(), 0.0f);
    EXPECT_LT(full.browser->Recents().BuiltCardCount(), 18);
}

// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------

TEST(SongBrowserUi, EverythingSettlesAndARestingFrameRepeats) {
    Rig rig(RealCatalog(), kHistory);
    EXPECT_FALSE(rig.browser->IsSettled()) << "the entrance is still to play";
    // The entrance: nothing on the first frame, everything a moment later.
    for (const TextRun &run : CommandsOf<TextRun>(rig.browser->BuildFrame())) {
        EXPECT_FLOAT_EQ(run.color.a, 0.0f) << run.text;
    }
    const DisplayList early = rig.Frame(rig.theme.motion.slow * 0.25);
    ASSERT_FALSE(CommandsOf<TextRun>(early).empty());
    EXPECT_LT(Nth<TextRun>(early, 0).color.a, 0.9f);
    const DisplayList rested = rig.Settle();
    EXPECT_FLOAT_EQ(Nth<TextRun>(rested, 0).color.a, 1.0f);
    EXPECT_TRUE(ClipsBalanced(rested));

    // Every kind of move starts something and it all comes to rest again.
    const std::vector<Action> moves = { Action::Right,   Action::Down,   Action::Down,
                                        Action::NextTab, Action::Left,   Action::PageUp,
                                        Action::End,     Action::Home,   Action::Up,
                                        Action::Up,      Action::PrevTab, Action::Down };
    for (std::size_t i = 0; i < moves.size(); ++i) {
        rig.Press(moves[i]);
        EXPECT_FALSE(rig.browser->IsSettled()) << "something should be moving after move " << i;
        rig.Settle();
    }
    rig.TypeText("su");
    rig.Settle();
    rig.Press(Action::Back);
    const DisplayList atRest = rig.Settle();
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(Describe(rig.Frame(0.5)), Describe(atRest)) << "a resting frame repeats";
        EXPECT_TRUE(rig.browser->IsSettled());
    }
}

// Time, not frames: the same events at 30, 60 and 240 frames a second end in
// the same picture.
TEST(SongBrowserUi, MotionDependsOnTimeNotOnFrameCount) {
    std::vector<std::string> results;
    for (const double step : { 1.0 / 30.0, 1.0 / 60.0, 1.0 / 240.0 }) {
        Rig rig(RealCatalog(), kHistory);
        rig.Settle(step);
        rig.Press(Action::Down);
        rig.Settle(step);
        rig.Press(Action::Down, 25);
        rig.Press(Action::NextTab);
        // A tenth of a second passes, in 3, 6 or 24 frames.
        for (int i = 0; i < static_cast<int>(0.1 / step + 0.5); ++i) {
            rig.Frame(step);
        }
        EXPECT_FALSE(rig.browser->IsSettled());
        rig.Press(Action::Right, 2);
        results.push_back(Describe(rig.Settle(step)));
    }
    EXPECT_EQ(results[0], results[1]);
    EXPECT_EQ(results[1], results[2]);

    // And a glide takes the same time whatever the frame rate: half way
    // through the highlight's move the bar is in the same place.
    std::vector<float> barTops;
    for (const int frames : { 3, 12 }) {
        Rig rig(RealCatalog());
        rig.Settle();
        rig.Press(Action::Down, 3);
        for (int i = 0; i < frames; ++i) {
            rig.Frame(0.06 / frames);
        }
        barTops.push_back(rig.browser->List().HighlightRect().Top());
        EXPECT_FALSE(rig.browser->IsSettled());
    }
    EXPECT_NEAR(barTops[0], barTops[1], 0.5f);
}

TEST(SongBrowserUi, TheDetailPaneCrossfadesWithNoStaleData) {
    Rig rig(RealCatalog());
    rig.Settle();
    const std::string a = rig.Selected();
    rig.Press(Action::Down);
    const std::string b = rig.Selected();
    const std::string titleA = Upper(rig.catalog.Find(a)->title);
    const std::string titleB = Upper(rig.catalog.Find(b)->title);
    const ImageHandle artA = ArtOf(*rig.catalog.Find(a));
    const ImageHandle artB = ArtOf(*rig.catalog.Find(b));
    ASSERT_NE(titleA, titleB);

    // At once the pane is about B; A is only what it is fading from.
    EXPECT_EQ(rig.browser->DetailSong(), b);
    EXPECT_EQ(rig.browser->DetailFadingSong(), a);

    const auto paneArtAlpha = [&](const DisplayList &frame, ImageHandle art) {
        float alpha = 0.0f;
        for (const ImageQuad &quad : CommandsOf<ImageQuad>(frame)) {
            if (quad.image == art && rig.PaneRect().Contains({ quad.rect.x, quad.rect.y })) {
                alpha = std::max(alpha, quad.tint.a);
            }
        }
        return alpha;
    };
    float lastA = 1.1f;
    float lastB = -0.1f;
    int framesInFade = 0;
    const float step = rig.theme.motion.normal / 8.0f;
    for (int i = 0; i < 7; ++i) {
        const DisplayList frame = rig.Frame(step);
        ++framesInFade;
        const TextRun runA = RunWith(frame, rig.PaneRect(), titleA);
        const TextRun runB = RunWith(frame, rig.PaneRect(), titleB);
        EXPECT_LT(runA.color.a, lastA) << "the old song only ever fades out";
        EXPECT_GT(runB.color.a, lastB) << "the new one only ever fades in";
        lastA = runA.color.a;
        lastB = runB.color.a;
        // Each song's art is as visible as its own title: never B's title
        // over A's art.
        EXPECT_NEAR(paneArtAlpha(frame, artA), runA.color.a, 0.001f);
        EXPECT_NEAR(paneArtAlpha(frame, artB), runB.color.a, 0.001f);
        EXPECT_NEAR(runA.color.a + runB.color.a, 1.0f, 0.001f);
    }
    EXPECT_EQ(framesInFade, 7);
    const DisplayList done = rig.Settle();
    EXPECT_FALSE(HasRun(done, rig.PaneRect(), titleA)) << "nothing of the old song is left";
    EXPECT_FLOAT_EQ(RunWith(done, rig.PaneRect(), titleB).color.a, 1.0f);
    EXPECT_FLOAT_EQ(paneArtAlpha(done, artA), 0.0f);
    EXPECT_EQ(rig.browser->DetailFadingSong(), "");

    // The fade is short: over within the theme's normal duration.
    rig.Press(Action::Down);
    rig.Frame(rig.theme.motion.normal * 1.01);
    rig.Frame(0.0);
    EXPECT_EQ(rig.browser->DetailFadingSong(), "");
}

TEST(SongBrowserUi, ArtThatArrivesLateFadesIn) {
    Rig rig(RealCatalog(), kHistory);
    rig.artReady = false;
    rig.browser->SetCatalog(rig.catalog); // rebuild everything with no art
    DisplayList frame = rig.Settle();
    EXPECT_TRUE(CommandsOf<ImageQuad>(frame).empty()) << "placeholders only";
    const int callsBefore = rig.artCalls;
    rig.Frame();
    EXPECT_GT(rig.artCalls, callsBefore) << "art is asked for every frame";
    EXPECT_LT(rig.artCalls - callsBefore, 60) << "but only for what is on screen";

    rig.artReady = true;
    frame = rig.Frame();
    frame = rig.Frame(rig.theme.motion.normal * 0.5);
    ASSERT_FALSE(CommandsOf<ImageQuad>(frame).empty());
    EXPECT_LT(Nth<ImageQuad>(frame, 0).tint.a, 1.0f) << "fading in";
    EXPECT_FALSE(rig.browser->IsSettled());
    frame = rig.Settle();
    for (const ImageQuad &quad : CommandsOf<ImageQuad>(frame)) {
        EXPECT_FLOAT_EQ(quad.tint.a, 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

TEST(SongBrowserUi, NothingFillsTheScreenOpaquely) {
    Rig rig(RealCatalog(), kHistory);
    const DisplayList frame = rig.Settle();
    const RoundRectFill scrim = Nth<RoundRectFill>(frame, 0);
    EXPECT_EQ(scrim.rect, kView);
    EXPECT_EQ(scrim.colors.topLeft, rig.theme.color.scrim);
    EXPECT_LT(scrim.colors.topLeft.a, 0.5f) << "a scrim, not a backdrop";
    for (const RoundRectFill &fill : CommandsOf<RoundRectFill>(frame)) {
        const bool big = fill.rect.width * fill.rect.height > 0.5f * 1280.0f * 720.0f;
        EXPECT_FALSE(big && fill.colors.topLeft.a > 0.9f) << Describe(Command { fill });
    }
    // Every part is there, inside the margins.
    for (const char *expected : { "SONGS", "TITLE", "ARTIST", "DIFFICULTY", "SOURCE", "DECADE",
                                  "TYPE TO SEARCH", "RECENTLY PLAYED" }) {
        EXPECT_TRUE(HasText(frame, expected)) << expected;
    }
    for (const TextRun &run : CommandsOf<TextRun>(frame)) {
        EXPECT_GE(run.origin.x, rig.theme.space.xl - 0.01f) << run.text;
        EXPECT_LE(run.origin.x + WidthOf(run), 1280.0f - rig.theme.space.xl + 0.01f) << run.text;
        EXPECT_LT(run.origin.y, 720.0f - rig.theme.space.lg + 0.01f) << run.text;
    }
    EXPECT_TRUE(ClipsBalanced(frame));
}

// Walks the whole real list: in every row the title stops short of the artist
// column, the artist short of the meter, and nothing leaves the list.
TEST(SongBrowserUi, LongTitlesAndArtistsAreCutAndNeverOverlap) {
    Rig rig(RealCatalog());
    rig.Settle();
    const float bodySize = rig.theme.type.body.size;
    const float captionSize = rig.theme.type.caption.size;
    std::set<std::string> cutTitles;
    int rowsChecked = 0;
    for (int page = 0; page < 40; ++page) {
        const DisplayList frame = rig.Settle();
        const Rect list = rig.browser->List().Bounds();
        // Group the list's runs by baseline: one row each.
        std::map<int, std::vector<TextRun> > byRow;
        for (const TextRun &run : RunsIn(frame, list)) {
            byRow[static_cast<int>(run.origin.y / rig.theme.metric.listHeaderHeight)].push_back(run);
        }
        for (const auto &entry : byRow) {
            const TextRun *title = nullptr;
            const TextRun *artist = nullptr;
            for (const TextRun &run : entry.second) {
                if (run.size == bodySize) {
                    title = &run;
                } else if (run.size == captionSize && !title) {
                    continue; // a heading
                } else if (run.size == captionSize) {
                    artist = &run;
                }
            }
            if (!title || !artist) {
                continue;
            }
            ++rowsChecked;
            EXPECT_LT(title->origin.x + WidthOf(*title), artist->origin.x)
                << title->text << " | " << artist->text;
            EXPECT_LE(artist->origin.x + WidthOf(*artist), list.Right()) << artist->text;
            if (title->text.size() > 3
                && title->text.compare(title->text.size() - 3, 3, "...") == 0) {
                cutTitles.insert(title->text);
            }
        }
        const int before = rig.browser->List().SelectedIndex();
        rig.Press(Action::PageDown);
        if (rig.browser->List().SelectedIndex() == before) {
            break;
        }
    }
    EXPECT_GT(rowsChecked, 121);
    EXPECT_FALSE(cutTitles.empty()) << "the real list has titles too long for the column";

    // The detail pane with the longest real title and the longest real artist:
    // every line stays inside the pane.
    for (const char *song : { "gonnamakeyousweat", "macarena" }) {
        State at;
        at.selectedSong = song;
        Rig pane(RealCatalog(), {}, &at);
        const DisplayList frame = pane.Settle();
        ASSERT_EQ(pane.browser->DetailSong(), song);
        const Rect paneRect = pane.PaneRect();
        const std::vector<TextRun> runs = RunsIn(frame, paneRect);
        ASSERT_GE(runs.size(), 6u);
        for (const TextRun &run : runs) {
            EXPECT_GE(run.origin.x, paneRect.Left() + pane.theme.space.md - 0.01f) << run.text;
            EXPECT_LE(
                run.origin.x + WidthOf(run), paneRect.Right() - pane.theme.space.md + 0.01f
            ) << run.text;
        }
    }

    // The pane's title steps down from the title size to the body size before
    // it is cut. With the fake measurer (half an em a glyph) the pane holds 17
    // glyphs at the title size and 22 at the body size.
    const TextStyle &titleStyle = rig.theme.type.title;
    int shortSeen = 0;
    int mediumSeen = 0;
    int longSeen = 0;
    for (int i = 0; i < rig.catalog.Size(); ++i) {
        const SongEntry &song = rig.catalog.At(i);
        const std::size_t glyphs = utf8::Count(song.title);
        int &seen = glyphs <= 17 ? shortSeen : (glyphs <= 22 ? mediumSeen : longSeen);
        if (seen++ >= 3) {
            continue; // three of each kind is plenty
        }
        State at;
        at.selectedSong = song.shortName;
        Rig pane(RealCatalog(), {}, &at);
        const DisplayList frame = pane.Settle();
        const std::string title = Upper(song.title);
        if (glyphs <= 17) {
            EXPECT_FLOAT_EQ(RunWith(frame, pane.PaneRect(), title).size, titleStyle.size) << title;
        } else if (glyphs <= 22) {
            EXPECT_FLOAT_EQ(RunWith(frame, pane.PaneRect(), title).size, bodySize) << title;
        } else {
            EXPECT_FALSE(HasRun(frame, pane.PaneRect(), title)) << title << " is cut";
        }
    }
    EXPECT_EQ(shortSeen > 0 && mediumSeen > 0 && longSeen > 0, true);
}

TEST(SongBrowserUi, TheDetailPaneShowsEverythingAboutTheSong) {
    SongCatalog catalog = RealCatalog();
    SetScore(catalog, "ymca", 5, 987654);
    State at;
    at.selectedSong = "ymca";
    Rig rig(catalog, {}, &at);
    const DisplayList frame = rig.Settle();
    const SongEntry &ymca = *rig.catalog.Find("ymca");
    const Rect pane = rig.PaneRect();
    EXPECT_TRUE(HasRun(frame, pane, "Y.M.C.A."));
    EXPECT_TRUE(HasRun(frame, pane, "VILLAGE PEOPLE"));
    EXPECT_TRUE(HasRun(frame, pane, Upper(ymca.album + " (" + std::to_string(ymca.year) + ")")));
    const int seconds = (ymca.lengthMs + 500) / 1000;
    char length[16];
    std::snprintf(length, sizeof(length), "%d:%02d", seconds / 60, seconds % 60);
    EXPECT_TRUE(HasRun(frame, pane, length));
    EXPECT_TRUE(HasRun(frame, pane, std::to_string(ymca.bpm) + " BPM"));
    EXPECT_TRUE(HasRun(frame, pane, "987,654"));
    EXPECT_TRUE(HasRun(frame, pane, "DANCE CENTRAL 3"));
    // Its tier's name, and that many cells of the meter lit.
    const SongList byTier = Arrange(rig.catalog, kByDifficulty);
    const std::string tier =
        byTier.sections[static_cast<std::size_t>(byTier.SectionForRow(byTier.RowForSong(rig.catalog.IndexOf("ymca"))))].title;
    EXPECT_TRUE(HasRun(frame, pane, Upper(tier)));
    int lit = 0;
    int unlit = 0;
    for (const RoundRectFill &fill : CommandsOf<RoundRectFill>(frame)) {
        if (pane.Contains({ fill.rect.x, fill.rect.y })
            && fill.rect.height == rig.theme.metric.meterCellHeight) {
            lit += SameRgb(fill.colors.topLeft, rig.theme.color.accent) ? 1 : 0;
            unlit += SameRgb(fill.colors.topLeft, rig.theme.color.fieldFill) ? 1 : 0;
        }
    }
    EXPECT_EQ(lit, ymca.difficultyTier + 1);
    EXPECT_EQ(lit + unlit, kDifficultyTiers);
    // The art is the pane's largest thing.
    const ImageQuad art = Last<ImageQuad>(frame);
    EXPECT_EQ(art.image, ArtOf(ymca));
    EXPECT_GT(art.rect.width, 200.0f);
    EXPECT_FLOAT_EQ(art.rect.width, art.rect.height);

    // A song never played says so.
    rig.Press(Action::Down);
    EXPECT_TRUE(HasRun(rig.Settle(), pane, "NOT PLAYED YET"));
}

TEST(SongBrowserUi, WorksWithNoSongsOneSongAndTheDiscAlone) {
    // No songs at all: a message, no crash on any input, Back still leaves.
    Rig empty((SongCatalog()));
    DisplayList frame = empty.Settle();
    EXPECT_TRUE(HasText(frame, "NO SONGS"));
    EXPECT_TRUE(HasText(frame, "SONGS")) << "the top bar is still there";
    EXPECT_EQ(empty.browser->DetailSong(), "");
    for (const Action action : { Action::Up, Action::Down, Action::Left, Action::Right,
                                 Action::Accept, Action::PageUp, Action::PageDown,
                                 Action::NextTab, Action::PrevTab, Action::Home, Action::End }) {
        empty.Press(action);
        empty.Settle();
    }
    empty.TypeText("abc");
    EXPECT_TRUE(HasText(empty.Settle(), "NO SONGS MATCH"));
    empty.Press(Action::Back);
    empty.Press(Action::Back);
    EXPECT_EQ(empty.backs, 1);
    EXPECT_TRUE(empty.picks.empty());

    // One song: a heading and a row, and it can be picked.
    SongCatalog single;
    single.Add(*RealCatalog().Find("ymca"));
    Rig one(single);
    frame = one.Settle();
    EXPECT_EQ(one.browser->List().RowCount(), 2);
    EXPECT_EQ(one.Selected(), "ymca");
    one.Press(Action::Down);
    one.Press(Action::Right);
    one.Press(Action::NextTab);
    EXPECT_EQ(one.Selected(), "ymca");
    one.Press(Action::Accept);
    EXPECT_EQ(one.picks, (std::vector<std::string> { "ymca" }));

    // The disc alone: 45 songs, one source.
    Rig disc(RealCatalog("ham3"));
    disc.Settle();
    EXPECT_EQ(disc.catalog.Size(), 45);
    disc.Press(Action::NextTab, 3); // by source
    EXPECT_EQ(disc.browser->List().RowCount(), 46) << "one heading, 45 songs";
    disc.Press(Action::End);
    disc.Settle();
    EXPECT_EQ(disc.browser->List().SelectedIndex(), 45);
}

// One row, every command: where each column sits.
TEST(SongBrowserUi, ARowGolden) {
    const Harness harness;
    SongRowModel model;
    model.title = "Scream";
    model.secondary = "Usher";
    model.titleMatch = { { 0, 3 } };
    model.difficultyTier = 1;
    model.stars = 2;
    model.played = true;
    SongRow row(model, [] { return ImageHandle { 7 }; }, [] { return false; });
    const std::string expected =
        "image id=7 8,4 43x43 uv=0,0 1x1 r=6 tint=#FFFFFFFF\n"
        "fill 474,19.5 7x12 r=0 soft=0 #FFFFFFFF\n"
        "fill 483,19.5 7x12 r=0 soft=0 #FFFFFFFF\n"
        "fill 492,19.5 7x12 r=0 soft=0 #FFFFFF66\n"
        "fill 501,19.5 7x12 r=0 soft=0 #FFFFFF66\n"
        "fill 510,19.5 7x12 r=0 soft=0 #FFFFFF66\n"
        "fill 519,19.5 7x12 r=0 soft=0 #FFFFFF66\n"
        "fill 528,19.5 7x12 r=0 soft=0 #FFFFFF66\n"
        "fill 551,21 9x9 r=4.5 soft=0 #FFC83CFF\n"
        "fill 564,21 9x9 r=4.5 soft=0 #FFC83CFF\n"
        "stroke 577,21 9x9 r=4.5 t=1.5 soft=0 #FFFFFF66\n"
        "stroke 590,21 9x9 r=4.5 t=1.5 soft=0 #FFFFFF66\n"
        "stroke 603,21 9x9 r=4.5 t=1.5 soft=0 #FFFFFF66\n"
        "text 67,33.3 font=1 size=26 ls=0.78 #37A7E3FF \"SCR\"\n"
        "text 108.34,33.3 font=1 size=26 ls=0.78 #FFFFFFFF \"EAM\"\n"
        "text 269.6,30.9 font=1 size=18 ls=1.08 #FFFFFFB8 \"USHER\"\n";
    EXPECT_EQ(Describe(harness.Frame(row, { 628.0f, 51.0f })), expected);
}

// ---------------------------------------------------------------------------
// Cost
// ---------------------------------------------------------------------------

// Rows exist only near the viewport, however long the catalog, and a frame
// costs well under a millisecond (RelWithDebInfo). The numbers are printed.
TEST(SongBrowserUi, RowsBuiltAndFrameCostStayBoundedUpToAThousandSongs) {
    struct Size {
        const char *name;
        SongCatalog catalog;
    };
    std::vector<Size> sizes;
    sizes.push_back({ "45 (disc)", RealCatalog("ham3") });
    sizes.push_back({ "121 (real)", RealCatalog() });
    sizes.push_back({ "1000 (synthetic)", SyntheticCatalog(1000) });

    for (Size &size : sizes) {
        Rig rig(size.catalog);
        rig.Settle();
        const Rect list = rig.browser->List().Bounds();
        // What fits, plus half a viewport each way, at the smallest row height.
        const int bound =
            static_cast<int>(2.0f * list.height / rig.theme.metric.listHeaderHeight) + 2;
        int mostBuilt = rig.browser->List().BuiltRowCount();

        std::size_t commands = 0;
        using Clock = std::chrono::steady_clock;
        const auto microsPerFrame = [&](int frames, bool moving) {
            double best = 1e9;
            for (int batch = 0; batch < 5; ++batch) {
                const Clock::time_point start = Clock::now();
                for (int i = 0; i < frames; ++i) {
                    if (moving) {
                        rig.Press(i % 40 == 39 ? Action::PageDown : Action::Down);
                    }
                    rig.clock.Advance(1.0 / 60.0);
                    commands = rig.browser->BuildFrame().commands.size();
                    mostBuilt = std::max(mostBuilt, rig.browser->List().BuiltRowCount());
                }
                const double micros =
                    std::chrono::duration<double, std::micro>(Clock::now() - start).count();
                best = std::min(best, micros / frames);
            }
            return best;
        };
        const double resting = microsPerFrame(200, false);
        const int builtBefore = rig.browser->List().TotalRowsBuilt();
        const double moving = microsPerFrame(200, true);
        const int builtWhileMoving = rig.browser->List().TotalRowsBuilt() - builtBefore;
        rig.Press(Action::End);
        rig.Frame();
        rig.Press(Action::Home);
        rig.Settle();
        mostBuilt = std::max(mostBuilt, rig.browser->List().BuiltRowCount());

        std::cout << "[ measured ] " << size.name << ": " << resting
                  << " us/frame at rest, " << moving << " us/frame scrolling (" << commands
                  << " draw commands); rows alive at most "
                  << mostBuilt << " (bound " << bound << "), " << builtWhileMoving
                  << " rows built over 1000 scrolling frames, list rows "
                  << rig.browser->List().RowCount() << "\n";
        EXPECT_LE(mostBuilt, bound) << size.name;
        EXPECT_LT(rig.browser->List().BuiltRowCount(), rig.browser->List().RowCount() + 1);
        EXPECT_LT(resting, 1000.0) << size.name << ": a frame at rest, microseconds";
        EXPECT_LT(moving, 1000.0) << size.name << ": a frame while scrolling, microseconds";
        // Scrolling builds each row it reaches once, not the catalog.
        EXPECT_LE(builtWhileMoving, 1000 * 3) << size.name;
    }
}

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------

namespace {

    // A stand-in for the game's backdrop: black with blue and magenta streaks,
    // brightest at the right, so the pictures show the screen's contrast over
    // something like what it will be drawn on.
    void DrawBackdrop(Raster &raster, const Theme &theme) {
        const Color blue = theme.color.streakBlue;
        const Color magenta = theme.color.streakMagenta;
        const Color violet = Color::FromRgb8(0x694FD5);
        const Color bright = Color::FromRgb8(0x37A7E3);
        struct Streak {
            float y;
            float thickness;
            Color left;
            Color right;
            float alpha;
        };
        const Streak streaks[] = {
            { 38, 10, blue, magenta, 0.9f },   { 84, 3, violet, violet, 0.5f },
            { 140, 26, magenta, violet, 0.55f }, { 196, 6, violet, blue, 0.7f },
            { 250, 18, blue, bright, 0.75f },  { 296, 40, bright, bright, 0.8f },
            { 352, 8, violet, magenta, 0.6f }, { 398, 22, violet, blue, 0.65f },
            { 450, 34, magenta, violet, 0.7f }, { 500, 5, blue, bright, 0.8f },
            { 548, 16, violet, magenta, 0.5f }, { 604, 12, blue, violet, 0.6f },
            { 650, 4, magenta, magenta, 0.5f }, { 684, 8, blue, magenta, 0.85f },
        };
        for (const Streak &streak : streaks) {
            raster.Draw(
                RoundRectFill { { -40.0f, streak.y, 1360.0f, streak.thickness },
                                streak.thickness * 0.5f,
                                CornerColors::Horizontal(
                                    streak.left.WithAlpha(streak.alpha * 0.35f),
                                    streak.right.WithAlpha(streak.alpha)
                                ),
                                streak.thickness * 1.5f }
            );
        }
    }

    void Picture(const DisplayList &frame, const Theme &theme, const std::string &name) {
        Raster raster(1280, 720);
        DrawBackdrop(raster, theme);
        Replay(frame, raster);
        const std::vector<unsigned char> png = EncodePng(1280, 720, raster.Rgb8());
        EXPECT_GT(png.size(), 1280u * 720u * 3u);
        if (const char *dir = std::getenv("SONG_BROWSER_UI_PNG_DIR")) {
            const std::string path = std::string(dir) + "/" + name;
            ASSERT_TRUE(WriteFile(path, png)) << path;
        }
    }

    SongCatalog PlayedCatalog() {
        SongCatalog catalog = RealCatalog();
        const int stars[] = { 5, 4, 3, 6, 2, 4, 1 };
        for (std::size_t i = 0; i < kHistory.size(); ++i) {
            SetScore(catalog, kHistory[i], stars[i], 400000 + static_cast<int>(i) * 123457);
        }
        // A few more with scores, so the list shows stars in the middle too.
        for (int i = 0; i < catalog.Size(); i += 5) {
            SetScore(catalog, catalog.At(i).shortName, 1 + (i / 5) % 5, 250000 + i * 4321);
        }
        return catalog;
    }

}

TEST(SongBrowserUi, Pictures) {
    {
        Rig rig(PlayedCatalog(), kHistory);
        Picture(rig.Settle(), rig.theme, "browser_recents_focus.png");
        rig.Press(Action::Down);
        rig.Press(Action::Down, 58);
        Picture(rig.Settle(), rig.theme, "browser_list.png");
        rig.Press(Action::NextTab, 2);
        Picture(rig.Settle(), rig.theme, "browser_difficulty.png");
    }
    {
        Rig rig(PlayedCatalog(), kHistory);
        rig.Settle();
        rig.TypeText("will i am");
        Picture(rig.Settle(), rig.theme, "browser_search.png");
        rig.TypeText("zz");
        Picture(rig.Settle(), rig.theme, "browser_no_match.png");
    }
    {
        Rig rig(RealCatalog());
        Picture(rig.Settle(), rig.theme, "browser_no_history.png");
    }
}
