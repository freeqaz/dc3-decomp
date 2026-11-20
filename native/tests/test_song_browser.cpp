// The song browser's data layer (docs/native/reference/SONG_BROWSER_DATA.md):
// the catalog, search, the arrangements, and GET /api/songs.
//
// Two kinds of test:
//
//   * SongBrowser{Text,Catalog,Search,Sections,Json}Test -- the pure library
//     in native/src/songbrowser/.  No engine.  Most of them search and
//     arrange the REAL song list: native/tests/data/song_browser/catalog.tsv
//     is what a dc3-native answered on GET /api/songs with the game's songs
//     and both Dance Central 2 packs installed (121 songs).  That list has no
//     accented letter in any title, artist or album, so the accent tests use
//     songs written here, and say so.
//   * SongBrowserFlowTest -- GET /api/songs of a child dc3-native against the
//     game's own song select: the same songs, the same order in each sort,
//     and a song that reached gameplay first in the recents.  Gated by
//     DC3_DTA_FLOW_TESTS=1 like every other flow test.  No GPU.

#include <gtest/gtest.h>

#include "engine_http_port.h"
#include "platform/ExecutablePath.h"
#include "seed_global_options.h"
#include "songbrowser/SongCatalog.h"
#include "songbrowser/SongCatalogJson.h"
#include "songbrowser/SongSearch.h"
#include "songbrowser/SongSections.h"
#include "songbrowser/SongText.h"
#include "test_tmp.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

using namespace songbrowser;

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

std::vector<std::string> SplitOn(const std::string &text, char separator) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (true) {
        size_t end = text.find(separator, at);
        if (end == std::string::npos) {
            parts.push_back(text.substr(at));
            return parts;
        }
        parts.push_back(text.substr(at, end - at));
        at = end + 1;
    }
}

// The real catalog.  `origin` empty: every song; else only that game_origin.
// `reversed` adds the songs last to first (the order must not matter).
SongCatalog RealCatalog(const std::string &origin = "", bool reversed = false) {
    SongCatalog catalog;
    std::ifstream in(BinaryDir() + "/../tests/data/song_browser/catalog.tsv");
    std::vector<SongEntry> songs;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f = SplitOn(line, '\t');
        if (f.size() != 13) {
            ADD_FAILURE() << "catalog.tsv: a line without 13 fields: " << line;
            continue;
        }
        SongEntry song;
        song.shortName = f[0];
        song.id = atoi(f[1].c_str());
        song.title = f[2];
        song.artist = f[3];
        song.album = f[4];
        song.year = atoi(f[5].c_str());
        song.bpm = atoi(f[6].c_str());
        song.lengthMs = atoi(f[7].c_str());
        song.rank = (float)atof(f[8].c_str());
        song.difficultyTier = atoi(f[9].c_str());
        song.origin = f[10];
        song.source = SourceFromOrigin(song.origin);
        song.onDisc = f[11] == "1";
        song.isCover = f[12] == "1";
        if (origin.empty() || song.origin == origin) songs.push_back(song);
    }
    if (reversed) std::reverse(songs.begin(), songs.end());
    for (size_t i = 0; i < songs.size(); i++) {
        catalog.Add(songs[i]);
    }
    return catalog;
}

SongEntry Song(const char *shortName, const char *title, const char *artist,
               const char *album = "") {
    static int nextId = 9000;
    SongEntry song;
    song.id = nextId++;
    song.shortName = shortName;
    song.title = title;
    song.artist = artist;
    song.album = album;
    return song;
}

// The short names of a result's rows, in order.
std::vector<std::string> Names(const SongCatalog &catalog, const SearchResult &result) {
    std::vector<std::string> names;
    for (size_t i = 0; i < result.rows.size(); i++) {
        names.push_back(catalog.At(result.rows[i].song).shortName);
    }
    return names;
}

std::vector<std::string> Names(const SongCatalog &catalog, const std::vector<int> &songs) {
    std::vector<std::string> names;
    for (size_t i = 0; i < songs.size(); i++) {
        names.push_back(catalog.At(songs[i]).shortName);
    }
    return names;
}

std::vector<std::string> List(std::initializer_list<const char *> names) {
    return std::vector<std::string>(names.begin(), names.end());
}

std::string Joined(const std::vector<std::string> &names) {
    std::string text;
    for (size_t i = 0; i < names.size(); i++) {
        text += (i ? " " : "") + names[i];
    }
    return text;
}

const SearchRow *RowOf(const SongCatalog &catalog, const SearchResult &result,
                       const std::string &shortName) {
    for (size_t i = 0; i < result.rows.size(); i++) {
        if (catalog.At(result.rows[i].song).shortName == shortName) return &result.rows[i];
    }
    return nullptr;
}

std::string FieldText(const SongEntry &song, MatchField field) {
    return field == kFieldTitle ? song.title : field == kFieldArtist ? song.artist : song.album;
}

// "field:begin+length=text" for every range of a row.
std::string Ranges(const SongCatalog &catalog, const SearchRow &row) {
    std::string text;
    for (size_t i = 0; i < row.ranges.size(); i++) {
        const MatchRange &range = row.ranges[i];
        if (i) text += " ";
        text += std::string(MatchFieldName(range.field)) + ":" + std::to_string(range.begin) + "+"
            + std::to_string(range.length) + "="
            + FieldText(catalog.At(row.song), range.field).substr(range.begin, range.length);
    }
    return text;
}

} // namespace

// ===========================================================================
// Text rules
// ===========================================================================

TEST(SongBrowserTextTest, FoldingIgnoresCaseAndAccents) {
    // Not from the real list (it has no accents): Latin-1 and Latin Extended-A.
    EXPECT_EQ(FoldForSearch("Beyonc\xC3\xA9", kFoldTight).text, "beyonce");
    EXPECT_EQ(FoldForSearch("D\xC3\xA9j\xC3\xA0 Vu", kFoldSpaced).text, "deja vu");
    EXPECT_EQ(FoldForSearch("Stra\xC3\x9F" "e", kFoldTight).text, "strasse");
    EXPECT_EQ(FoldForSearch("M\xC3\xB6tley Cr\xC3\xBC" "e", kFoldTight).text, "motley crue");
    EXPECT_EQ(FoldForSearch("\xC3\x86nima", kFoldTight).text, "aenima");
    EXPECT_EQ(FoldForSearch("\xC5\x81\xC3\xB3\x64\xC5\xBA", kFoldTight).text, "lodz"); // Łódź
    EXPECT_EQ(FoldForSearch("SIGUR R\xC3\x93S", kFoldTight).text, "sigur ros");
    EXPECT_EQ(FoldForSearch("OMG", kFoldTight).text, "omg");
}

TEST(SongBrowserTextTest, PunctuationFoldsBothWays) {
    struct Case {
        const char *text, *spaced, *tight;
    } cases[] = {
        // From the real list.
        { "will.i.am", "will i am", "william" },
        { "Y.M.C.A.", "y m c a", "ymca" },
        { "Ne-Yo", "ne yo", "neyo" },
        { "Ain't 2 Proud 2 Beg", "ain t 2 proud 2 beg", "aint 2 proud 2 beg" },
        { "1, 2 Step", "1 2 step", "1 2 step" },
        { "C+C Music Factory", "c c music factory", "cc music factory" },
        // Initials written apart are one word when squeezed.
        { "J. J. Fad", "j j fad", "jj fad" },
        { "Mary J. Blige", "mary j blige", "mary j blige" },
        { "B.o.B ft. Bruno Mars", "b o b ft bruno mars", "bob ft bruno mars" },
        { "Diddy-Dirty Money ft. T.I.", "diddy dirty money ft t i", "diddydirty money ft ti" },
        { "Dr. J. Smith", "dr j smith", "dr j smith" },
        { "(When You Gonna) Give It Up to Me", "when you gonna give it up to me",
          "when you gonna give it up to me" },
        { "You Make Me Feel...", "you make me feel", "you make me feel" },
        { "What's My Name?", "what s my name", "whats my name" },
        { "Yeah!", "yeah", "yeah" },
        // "&" is the word "and", with or without spaces around it.
        { "S & M", "s and m", "s and m" },
        { "Round & Round", "round and round", "round and round" },
        { "AT&T", "at and t", "at and t" },
        // Not from the real list: "$" beside a letter, "!" before one.
        { "Ke$ha", "kesha", "kesha" },
        { "A$AP Rocky", "asap rocky", "asap rocky" },
        { "P!nk", "pink", "pink" },
        { "$100 Bill", "100 bill", "100 bill" },
        { "  spaced   out  ", "spaced out", "spaced out" },
        { "...", "", "" },
    };
    for (const Case &c : cases) {
        EXPECT_EQ(FoldForSearch(c.text, kFoldSpaced).text, c.spaced) << c.text;
        EXPECT_EQ(FoldForSearch(c.text, kFoldTight).text, c.tight) << c.text;
    }
}

TEST(SongBrowserTextTest, FoldedCharactersRememberTheirBytes) {
    // "Déjà": D=0, é=1..2, j=3, à=4..5.
    FoldedText folded = FoldForSearch("D\xC3\xA9j\xC3\xA0", kFoldTight);
    ASSERT_EQ(folded.text, "deja");
    EXPECT_EQ(folded.begin, (std::vector<int> { 0, 1, 3, 4 }));
    EXPECT_EQ(folded.end, (std::vector<int> { 1, 3, 4, 6 }));
    // One letter that folds to two: both remember the same bytes.
    FoldedText sharp = FoldForSearch("\xC3\x9F", kFoldTight);
    ASSERT_EQ(sharp.text, "ss");
    EXPECT_EQ(sharp.begin, (std::vector<int> { 0, 0 }));
    EXPECT_EQ(sharp.end, (std::vector<int> { 2, 2 }));
    // The dots of "will.i.am" are inside the run its tight fold covers.
    FoldedText tight = FoldForSearch("will.i.am", kFoldTight);
    EXPECT_EQ(tight.begin.front(), 0);
    EXPECT_EQ(tight.end.back(), 9);
}

TEST(SongBrowserTextTest, BadBytesAreOneCharacterEach) {
    unsigned cp = 0;
    EXPECT_EQ(DecodeUtf8("\xC3", 0, cp), 1); // a lead byte and nothing after it
    EXPECT_EQ(cp, 0xFFFDu);
    EXPECT_EQ(DecodeUtf8("\xFF" "a", 0, cp), 1);
    EXPECT_EQ(cp, 0xFFFDu);
    EXPECT_EQ(DecodeUtf8("\xC3\x41", 0, cp), 1); // a lead byte and a letter
    EXPECT_EQ(DecodeUtf8("\xE2\x82\xAC", 0, cp), 3);
    EXPECT_EQ(cp, 0x20ACu);
    // A Latin-1 byte in a string that is otherwise ASCII still folds.
    EXPECT_EQ(FoldForSearch("Beyonc\xE9 Live", kFoldTight).text, "beyonc live");
}

TEST(SongBrowserTextTest, SortKeysAreTheGamesAlphaKeys) {
    // Sorting.cpp, AlphaKeySkip.
    EXPECT_EQ(AlphaKeySkip("The Trammps", true), 4u);
    EXPECT_EQ(AlphaKeySkip("The Trammps", false), 0u);
    EXPECT_EQ(AlphaKeySkip("the hustle", true), 4u);
    EXPECT_EQ(AlphaKeySkip("A Tribe Called Quest", true), 2u);
    EXPECT_EQ(AlphaKeySkip("An Honest Mistake", true), 3u);
    EXPECT_EQ(AlphaKeySkip("The ", true), 0u) << "only from a string longer than the article";
    EXPECT_EQ(AlphaKeySkip("Them", true), 0u);
    EXPECT_EQ(AlphaKeySkip("(When You Gonna) Give It Up to Me", false), 1u);
    EXPECT_EQ(AlphaKeySkip("The (Thing)", true), 5u);

    EXPECT_EQ(SortKey("The Trammps", true), "trammps");
    EXPECT_EQ(SortKey("The Hustle", false), "the hustle");
    EXPECT_EQ(SortKey("Y.M.C.A.", false), "y.m.c.a.") << "punctuation stays in a sort key";

    // ArtistCmp::SortCharRank / SortCharLabel.
    EXPECT_EQ(SortCharRank("2NE1", true), 0);
    EXPECT_EQ(SortCharRank("50 Cent", true), 0);
    EXPECT_EQ(SortCharRank("The Trammps", true), 20);
    EXPECT_EQ(SortCharRank("The Black Eyed Peas", true), 2);
    EXPECT_EQ(SortCharRank("will.i.am", true), 23);
    EXPECT_EQ(SortCharRank("", true), 0);
    EXPECT_STREQ(SortCharLabel(0), "123");
    EXPECT_STREQ(SortCharLabel(1), "A");
    EXPECT_STREQ(SortCharLabel(26), "Z");
    // Where this differs from the game: an accented first letter is its base
    // letter here, and "123" there.
    EXPECT_EQ(SortCharRank("\xC3\x89xito", false), 5);

    // strcasecmp order: case does not matter, digits before letters.
    EXPECT_LT(CompareSortKeys(SortKey("1, 2 Step", false), SortKey("Ain't 2 Proud 2 Beg", false)), 0);
    EXPECT_EQ(CompareSortKeys(SortKey("omg", false), SortKey("OMG", false)), 0);
    EXPECT_LT(CompareSortKeys(SortKey("Yeah 3X", false), SortKey("Yeah!", false)), 0);
    EXPECT_GT(CompareSortKeys(SortKey("Yeah!", false), SortKey("Yeah", false)), 0);
}

// ===========================================================================
// Catalog
// ===========================================================================

TEST(SongBrowserCatalogTest, TheFixtureIsTheRealList) {
    SongCatalog catalog = RealCatalog();
    ASSERT_EQ(catalog.Size(), 121);
    EXPECT_EQ(RealCatalog("ham3").Size(), 45);
    EXPECT_EQ(RealCatalog("ham2").Size(), 44);
    EXPECT_EQ(RealCatalog("ham2_dlc").Size(), 32);
    for (int i = 0; i < catalog.Size(); i++) {
        const SongEntry &song = catalog.At(i);
        for (const std::string *text : { &song.title, &song.artist, &song.album }) {
            for (size_t c = 0; c < text->size(); c++) {
                ASSERT_LT((unsigned char)(*text)[c], 0x80)
                    << song.shortName << ": the real list was pure ASCII when this was written";
            }
        }
    }
}

TEST(SongBrowserCatalogTest, LooksUpByShortNameAndId) {
    SongCatalog catalog = RealCatalog();
    const SongEntry *ymca = catalog.Find("ymca");
    ASSERT_NE(ymca, nullptr);
    EXPECT_EQ(ymca->title, "Y.M.C.A.");
    EXPECT_EQ(ymca->artist, "Village People");
    EXPECT_EQ(catalog.FindById(ymca->id), ymca);
    EXPECT_EQ(catalog.IndexOfId(ymca->id), catalog.IndexOf("ymca"));
    EXPECT_EQ(&catalog.At(catalog.IndexOf("ymca")), ymca);
    EXPECT_EQ(catalog.Find("no_such_song"), nullptr);
    EXPECT_EQ(catalog.IndexOf("no_such_song"), -1);
    EXPECT_EQ(catalog.FindById(-5), nullptr);
    EXPECT_EQ(catalog.IndexOfId(-5), -1);
}

TEST(SongBrowserCatalogTest, AShortNameAddedAgainReplacesTheFirst) {
    SongCatalog catalog;
    SongEntry first = Song("one", "First Title", "Artist");
    SongEntry other = Song("two", "Other", "Artist");
    EXPECT_EQ(catalog.Add(first), 0);
    EXPECT_EQ(catalog.Add(other), 1);
    SongEntry again = Song("one", "Second Title", "Artist");
    EXPECT_EQ(catalog.Add(again), 0);
    EXPECT_EQ(catalog.Size(), 2);
    EXPECT_EQ(catalog.Find("one")->title, "Second Title");
    EXPECT_EQ(catalog.FindById(first.id), nullptr) << "the replaced song's id is gone";
    EXPECT_EQ(catalog.FindById(again.id)->title, "Second Title");
}

TEST(SongBrowserCatalogTest, RecentsSkipSongsThatAreNotInstalled) {
    SongCatalog catalog = RealCatalog("ham3");
    // "closer" is in the Dance Central 2 DLC pack, which this catalog does not have.
    catalog.SetRecents(List({ "closer", "ymca", "omg", "ymca", "never_existed", "scream" }));
    EXPECT_EQ(catalog.History(), List({ "closer", "ymca", "omg", "never_existed", "scream" }))
        << "the history is kept whole, each name once";
    EXPECT_EQ(Names(catalog, catalog.Recents(6)), List({ "ymca", "omg", "scream" }));
    EXPECT_EQ(Names(catalog, catalog.Recents(2)), List({ "ymca", "omg" }));
    EXPECT_EQ(Names(catalog, catalog.Recents(0)), List({ "ymca", "omg", "scream" }));
    // recentRank is the place in the history, missing songs counted.
    EXPECT_EQ(catalog.Find("ymca")->recentRank, 1);
    EXPECT_EQ(catalog.Find("omg")->recentRank, 2);
    EXPECT_EQ(catalog.Find("scream")->recentRank, 4);
    EXPECT_EQ(catalog.Find("starships")->recentRank, -1);
    EXPECT_TRUE(catalog.Find("ymca")->played);
    EXPECT_FALSE(catalog.Find("starships")->played);

    // A new history replaces the old one.
    catalog.SetRecents(List({ "starships" }));
    EXPECT_EQ(catalog.Find("ymca")->recentRank, -1);
    EXPECT_EQ(catalog.Find("starships")->recentRank, 0);
    EXPECT_EQ(Names(catalog, catalog.Recents(6)), List({ "starships" }));

    // An empty history: no recents.
    catalog.SetRecents(std::vector<std::string>());
    EXPECT_TRUE(catalog.Recents(6).empty());
}

TEST(SongBrowserCatalogTest, ALockedSongIsNotARecent) {
    SongCatalog catalog;
    SongEntry locked = Song("locked", "Locked", "A");
    locked.locked = true;
    catalog.Add(locked);
    catalog.Add(Song("open", "Open", "A"));
    catalog.SetRecents(List({ "locked", "open" }));
    EXPECT_EQ(Names(catalog, catalog.Recents(6)), List({ "open" }));
}

TEST(SongBrowserCatalogTest, SourceIsTheGamesLocationRule) {
    EXPECT_EQ(SourceFromOrigin("ham1"), kSourceDC1);
    EXPECT_EQ(SourceFromOrigin("ham2"), kSourceDC2);
    EXPECT_EQ(SourceFromOrigin("ham3"), kSourceDC3);
    EXPECT_EQ(SourceFromOrigin("ham2_dlc"), kSourceDownload);
    EXPECT_EQ(SourceFromOrigin("ham3_dlc"), kSourceDownload);
    EXPECT_EQ(SourceFromOrigin(""), kSourceDownload);
    EXPECT_STREQ(SourceToken(kSourceDC2), "ham2");
    EXPECT_STREQ(SourceToken(kSourceDownload), "DLC");
}

TEST(SongBrowserCatalogTest, LabelsFallBack) {
    SongCatalog catalog;
    EXPECT_EQ(catalog.Label("song_tier_0", "Warmup"), "Warmup");
    catalog.SetLabel("song_tier_0", "Aufw\xC3\xA4rmen");
    EXPECT_EQ(catalog.Label("song_tier_0", "Warmup"), "Aufw\xC3\xA4rmen");
}

// ===========================================================================
// Search
// ===========================================================================

TEST(SongBrowserSearchTest, AnEmptyQueryIsNotASearch) {
    SongCatalog catalog = RealCatalog();
    SongSearch search(catalog);
    for (const char *query : { "", " ", "   \t ", "...", "!", "$", "(-)" }) {
        SearchResult result = search.Search(query);
        EXPECT_FALSE(result.searching) << "'" << query << "'";
        EXPECT_TRUE(result.rows.empty()) << "'" << query << "'";
        EXPECT_TRUE(result.words.empty()) << "'" << query << "'";
    }
    // Searching and finding nothing is another state.
    SearchResult none = search.Search("qqqq");
    EXPECT_TRUE(none.searching);
    EXPECT_TRUE(none.rows.empty());
    // And one letter is a search.
    SearchResult one = search.Search("y");
    EXPECT_TRUE(one.searching);
    EXPECT_FALSE(one.rows.empty());
    EXPECT_EQ(one.words, List({ "y" }));
}

TEST(SongBrowserSearchTest, QueriesAreFoldedLikeTheSongs) {
    EXPECT_EQ(QueryWords("  Will.I.Am  "), List({ "william" }));
    EXPECT_EQ(QueryWords("will i am"), List({ "will", "i", "am" }));
    EXPECT_EQ(QueryWords("S&M"), List({ "s", "and", "m" }));
    EXPECT_EQ(QueryWords("ne-yo"), List({ "neyo" }));
    EXPECT_EQ(QueryWords("BEYONC\xC3\x89"), List({ "beyonce" }));
    EXPECT_EQ(QueryWords("ke$ha"), List({ "kesha" }));
}

// Every awkward name of the real list, typed the ways a person would.
TEST(SongBrowserSearchTest, RealNamesAreFoundTheWayPeopleTypeThem) {
    SongCatalog catalog = RealCatalog();
    SongSearch search(catalog);
    struct Case {
        const char *query;
        const char *first; // the first row
        MatchClass matchClass;
    } cases[] = {
        // dots
        { "ymca", "ymca", kMatchTitlePrefix },
        { "y.m.c.a.", "ymca", kMatchTitlePrefix },
        { "Y M C A", "ymca", kMatchTitlePrefix },
        { "opp", "opp", kMatchTitlePrefix },
        { "o.p.p", "opp", kMatchTitlePrefix },
        { "william", "omg", kMatchArtistWord },
        { "will i am", "omg", kMatchArtistWord },
        { "will.i.am", "omg", kMatchArtistWord },
        { "bob bruno", "nothinonyou", kMatchArtistWord }, // B.o.B ft. Bruno Mars
        { "b.o.b ft bruno mars", "nothinonyou", kMatchArtistPrefix },
        { "eu", "dabutt", kMatchArtistPrefix },         // E.U.
        { "mr saxobeat", "mrsaxobeat", kMatchTitlePrefix },
        { "mr. saxobeat", "mrsaxobeat", kMatchTitlePrefix },
        { "saxobeat", "mrsaxobeat", kMatchTitleWord },
        { "jj fad", "supersonic", kMatchArtistPrefix },  // J. J. Fad
        { "j j fad", "supersonic", kMatchArtistPrefix },
        { "j. j. fad", "supersonic", kMatchArtistPrefix },
        { "j.j. fad", "supersonic", kMatchArtistPrefix },
        { "mary j blige", "reallove", kMatchArtistPrefix },
        { "mary j. blige", "reallove", kMatchArtistPrefix },
        { "t.i.", "hellogoodmorning", kMatchArtistWord }, // Diddy-Dirty Money ft. T.I.
        // digits
        { "1 2 step", "12step", kMatchTitlePrefix },
        { "1, 2 step", "12step", kMatchTitlePrefix },
        { "2 step", "12step", kMatchTitleWord },
        { "2ne1", "iamthebest", kMatchArtistPrefix },
        { "50 cent", "indaclub", kMatchArtistPrefix },
        { "calabria 2008", "calabria", kMatchTitlePrefix },
        { "yeah 3x", "yeah3x", kMatchTitlePrefix },
        { "g6", "likeag6", kMatchTitleWord },
        { "maroon 5", "moveslikejagger", kMatchArtistPrefix },
        // apostrophes
        { "aint 2 proud", "aint2proud2beg", kMatchTitlePrefix },
        { "ain't 2 proud", "aint2proud2beg", kMatchTitlePrefix },
        { "dont touch me", "donttouchme", kMatchTitlePrefix },
        { "whats my name", "whatsmyname", kMatchTitlePrefix },
        { "what's my name?", "whatsmyname", kMatchTitlePrefix },
        { "da butt", "dabutt", kMatchTitlePrefix },
        { "fallin in love", "djgotusfallininlove", kMatchTitleWord },
        { "lil wayne", "letitrock", kMatchArtistWord },  // Kevin Rudolf ft. Lil' Wayne
        // hyphens and plus
        { "neyo", "closer", kMatchArtistPrefix },
        { "ne-yo", "closer", kMatchArtistPrefix },
        { "ne yo", "closer", kMatchArtistPrefix },
        { "ching a ling", "chingaling", kMatchTitlePrefix },
        { "chingaling", "chingaling", kMatchTitlePrefix },
        { "sir mix a lot", "babygotback", kMatchArtistPrefix },
        { "o-zone", "dragosteadintei", kMatchArtistPrefix },
        { "ozone", "dragosteadintei", kMatchArtistPrefix },
        { "c+c music factory", "gonnamakeyousweat", kMatchArtistPrefix },
        { "cc music", "gonnamakeyousweat", kMatchArtistPrefix },
        { "c c music", "gonnamakeyousweat", kMatchArtistPrefix },
        // "&" and "and"
        { "s&m", "sandm", kMatchTitlePrefix },
        { "s & m", "sandm", kMatchTitlePrefix },
        { "s and m", "sandm", kMatchTitlePrefix },
        { "round & round", "roundandround", kMatchTitlePrefix },
        { "round and round", "roundandround", kMatchTitlePrefix },
        { "heavy d & the boyz", "nowthatwefoundlove", kMatchArtistPrefix },
        { "heavy d and the boyz", "nowthatwefoundlove", kMatchArtistPrefix },
        { "sexy & i know it", "sexyandiknowit", kMatchTitlePrefix }, // the title says "And"
        // a leading "The", and a leading "("
        { "trammps", "discoinferno", kMatchArtistPrefix },
        { "the trammps", "discoinferno", kMatchArtistPrefix },
        { "humpty", "thehumptydance", kMatchTitlePrefix },
        { "the humpty dance", "thehumptydance", kMatchTitlePrefix },
        { "when you gonna", "giveituptome", kMatchTitlePrefix },
        { "give it up to me", "giveituptome", kMatchTitleWord },
        { "(when you gonna) give", "giveituptome", kMatchTitlePrefix },
        { "everybody dance now", "gonnamakeyousweat", kMatchTitleWord },
        // featuring credits: the names alone, or with the word typed any way
        { "usher pitbull", "djgotusfallininlove", kMatchArtistWord },
        { "usher ft pitbull", "djgotusfallininlove", kMatchArtistPrefix },
        { "usher ft. pitbull", "djgotusfallininlove", kMatchArtistPrefix },
        { "usher feat pitbull", "djgotusfallininlove", kMatchArtistWord },
        { "usher featuring pitbull", "djgotusfallininlove", kMatchArtistWord },
        { "usher feat. will.i.am", "omg", kMatchArtistWord },
        { "pitbull usher", "djgotusfallininlove", kMatchArtistWord },
        // case
        { "OMG", "omg", kMatchTitlePrefix },
        { "lmfao", "partyrockanthem", kMatchArtistPrefix },
        { "dcup", "wenospeakamericano", kMatchArtistWord },
    };
    for (const Case &c : cases) {
        SearchResult result = search.Search(c.query);
        ASSERT_FALSE(result.rows.empty()) << "'" << c.query << "' found nothing";
        EXPECT_EQ(catalog.At(result.rows[0].song).shortName, c.first)
            << "'" << c.query << "': " << Joined(Names(catalog, result));
        EXPECT_STREQ(MatchClassName(result.rows[0].matchClass), MatchClassName(c.matchClass))
            << "'" << c.query << "'";
    }
    // "bob" is the start of two artists, Bobby Brown and B.o.B: both, in
    // title order ("My Prerogative", "Nothin' On You"), before anything else.
    for (const char *query : { "bob", "b.o.b", "BOB" }) {
        std::vector<std::string> names = Names(catalog, search.Search(query));
        ASSERT_GE(names.size(), 2u) << query;
        EXPECT_EQ(names[0], "myprerogative") << query;
        EXPECT_EQ(names[1], "nothinonyou") << query;
        EXPECT_EQ(search.Search(query).rows[1].matchClass, kMatchArtistPrefix) << query;
    }
}

// One query, one song for each way of matching, added worst first.
TEST(SongBrowserSearchTest, RowsAreOrderedByHowTheyMatched) {
    SongCatalog catalog;
    catalog.Add(Song("none", "Hello", "World", "Greetings"));
    catalog.Add(Song("fuzzy", "Lvoe Shack", "Zed"));            // two letters swapped
    catalog.Add(Song("inside", "Glovebox", "Zed"));
    catalog.Add(Song("album", "Zzz", "Zed", "Greatest Love Songs"));
    catalog.Add(Song("artist_word", "Zzz", "Courtney Love"));
    catalog.Add(Song("artist_prefix", "Zzz", "Love Unlimited"));
    catalog.Add(Song("title_word", "Real Love", "Zed"));
    catalog.Add(Song("title_prefix", "Love Shack", "Zed"));
    SongSearch search(catalog);
    SearchResult result = search.Search("love");
    EXPECT_EQ(Names(catalog, result),
              List({ "title_prefix", "title_word", "artist_prefix", "artist_word", "album",
                     "inside", "fuzzy" }));
    ASSERT_EQ(result.rows.size(), 7u);
    for (int i = 0; i < 7; i++) {
        EXPECT_EQ((int)result.rows[i].matchClass, i) << MatchClassName(result.rows[i].matchClass);
    }
    EXPECT_EQ(result.rows[0].field, kFieldTitle);
    EXPECT_EQ(result.rows[2].field, kFieldArtist);
    EXPECT_EQ(result.rows[4].field, kFieldAlbum);
    EXPECT_EQ(result.rows[5].field, kFieldTitle);

    // A title match beats an artist match of the same song, and the artist
    // an album: the best one is reported.
    SongCatalog both;
    both.Add(Song("both", "Love Song", "Love", "Love"));
    SearchResult best = SongSearch(both).Search("love");
    ASSERT_EQ(best.rows.size(), 1u);
    EXPECT_EQ(best.rows[0].matchClass, kMatchTitlePrefix);
}

// The same on the real list: classes never go back up, and inside a class
// the rows are in title order.
TEST(SongBrowserSearchTest, RealResultsAreGroupedByClassThenTitle) {
    SongCatalog catalog = RealCatalog();
    SongSearch search(catalog);
    int checked = 0;
    for (const char *query : { "love", "the", "you", "s", "b", "da", "me", "rihanna", "ft" }) {
        SearchResult result = search.Search(query);
        ASSERT_GT(result.rows.size(), 2u) << query;
        for (size_t i = 1; i < result.rows.size(); i++) {
            const SearchRow &before = result.rows[i - 1];
            const SearchRow &row = result.rows[i];
            ASSERT_LE(before.matchClass, row.matchClass) << query << " row " << i;
            if (before.matchClass != row.matchClass) continue;
            ASSERT_LE(CompareSortKeys(SortKey(catalog.At(before.song).title, false),
                                      SortKey(catalog.At(row.song).title, false)),
                      0)
                << query << " row " << i << ": " << catalog.At(before.song).title << " / "
                << catalog.At(row.song).title;
            checked++;
        }
    }
    EXPECT_GT(checked, 50);
    // "love": Rihanna's "Only Girl" has no love in it; "Stereo Love" does, as
    // a later word; nothing in the list starts with it.
    SearchResult love = search.Search("love");
    EXPECT_EQ(RowOf(catalog, love, "onlygirlintheworld"), nullptr);
    ASSERT_NE(RowOf(catalog, love, "stereolove"), nullptr);
    EXPECT_EQ(RowOf(catalog, love, "stereolove")->matchClass, kMatchTitleWord);
    // "usher": his own songs (artist prefix) before any other match.
    SearchResult usher = search.Search("usher");
    ASSERT_GE(usher.rows.size(), 4u);
    EXPECT_EQ(usher.rows[0].matchClass, kMatchArtistPrefix);
    EXPECT_EQ(Names(catalog, usher), List({ "djgotusfallininlove", "omg", "scream", "yeah" }));
}

TEST(SongBrowserSearchTest, TiesGoToRecentlyPlayedThenTitle) {
    SongCatalog catalog = RealCatalog();
    // Three titles start with "you"; in title order:
    EXPECT_EQ(Names(catalog, SongSearch(catalog).Search("you")).front(), "yougotittherightstuff");
    std::vector<std::string> byTitle = Names(catalog, SongSearch(catalog).Search("you"));
    ASSERT_GE(byTitle.size(), 3u);
    EXPECT_EQ(std::vector<std::string>(byTitle.begin(), byTitle.begin() + 3),
              List({ "yougotittherightstuff", "youmakemefeeldc", "youreajerk" }));

    // Played: the newest first, then the older, then the unplayed by title.
    catalog.SetRecents(List({ "youreajerk", "ymca", "youmakemefeeldc" }));
    std::vector<std::string> played = Names(catalog, SongSearch(catalog).Search("you"));
    EXPECT_EQ(std::vector<std::string>(played.begin(), played.begin() + 3),
              List({ "youreajerk", "youmakemefeeldc", "yougotittherightstuff" }));

    // Recency never lifts a row over a better kind of match: "stereo love"
    // only has "you" nowhere, and a played artist match stays under titles.
    catalog.SetRecents(List({ "nothinonyou" })); // "Nothin' On You": a later word
    std::vector<std::string> mixed = Names(catalog, SongSearch(catalog).Search("you"));
    EXPECT_EQ(std::vector<std::string>(mixed.begin(), mixed.begin() + 4),
              List({ "yougotittherightstuff", "youmakemefeeldc", "youreajerk", "nothinonyou" }));
}

TEST(SongBrowserSearchTest, EveryWordMustBeFoundAndWordsMaySpanFields) {
    SongCatalog catalog = RealCatalog();
    SongSearch search(catalog);
    // Title and artist.
    EXPECT_EQ(Names(catalog, search.Search("usher scream")), List({ "scream" }));
    EXPECT_EQ(Names(catalog, search.Search("scream usher")), List({ "scream" }));
    EXPECT_EQ(Names(catalog, search.Search("rihanna only girl")), List({ "onlygirlintheworld" }));
    // Artist and album ("Loud" is the album of three Rihanna songs).
    EXPECT_EQ(Names(catalog, search.Search("rihanna loud")),
              List({ "onlygirlintheworld", "sandm", "whatsmyname" }));
    // One word that is nowhere: no row, however well the others match.
    EXPECT_TRUE(search.Search("usher qqqq").rows.empty());
    EXPECT_TRUE(search.Search("scream qqqq usher").rows.empty());
    // More words never find more songs.
    size_t one = search.Search("lady").rows.size();
    size_t two = search.Search("lady gaga").rows.size();
    size_t three = search.Search("lady gaga glory").rows.size();
    EXPECT_GE(one, two);
    EXPECT_GE(two, three);
    EXPECT_EQ(three, 1u);
    // The whole query as one run ranks as that run: "the humpty" is the start
    // of a title, though "humpty" alone is its second word.
    SearchResult run = search.Search("black eyed");
    (void)run;
    SearchResult humpty = search.Search("the humpty");
    ASSERT_FALSE(humpty.rows.empty());
    EXPECT_EQ(humpty.rows[0].matchClass, kMatchTitlePrefix);
    // Words in different fields rank as the worst of them: an artist that
    // starts with "usher" and a title that starts with "omg".
    SearchResult split = search.Search("omg usher");
    ASSERT_EQ(Names(catalog, split), List({ "omg" }));
    EXPECT_EQ(split.rows[0].matchClass, kMatchArtistPrefix);
    EXPECT_EQ(split.rows[0].field, kFieldArtist);
}

TEST(SongBrowserSearchTest, TypingMistakesAreForgivenInLongerWords) {
    SongCatalog catalog = RealCatalog();
    SongSearch search(catalog);
    struct Case {
        const char *query, *finds;
    } cases[] = {
        { "rihana", "sandm" },        // a letter missing
        { "rihannna", "sandm" },      // a letter too many
        { "rihamna", "sandm" },       // a wrong letter
        { "rihnana", "sandm" },       // two letters swapped
        { "macarana", "macarena" },
        { "ushre", "scream" },
        { "starshps", "starships" },
        { "rhn", "sandm" },           // three letters of Rihanna, in order
        { "blackeyedpeas", "boomboompow" }, // no spaces
        { "pitbll usher", "djgotusfallininlove" },
    };
    for (const Case &c : cases) {
        SearchResult result = search.Search(c.query);
        const SearchRow *row = RowOf(catalog, result, c.finds);
        ASSERT_NE(row, nullptr) << "'" << c.query << "' did not find " << c.finds << ": "
                                << Joined(Names(catalog, result));
        EXPECT_EQ(row->matchClass, kMatchFuzzy) << c.query;
    }
    // A mistake never outranks the real thing: "rihanna" typed right finds
    // her songs exactly, and nothing is reported as fuzzy that was found.
    SearchResult exact = search.Search("rihanna");
    ASSERT_EQ(exact.rows.size(), 4u);
    for (size_t i = 0; i < exact.rows.size(); i++) {
        EXPECT_LT(exact.rows[i].matchClass, kMatchFuzzy);
    }
    // Two mistakes are not forgiven, nor is a wrong first letter.
    EXPECT_EQ(RowOf(catalog, search.Search("rihnnaa"), "sandm"), nullptr);
    EXPECT_EQ(RowOf(catalog, search.Search("eihanna"), "sandm"), nullptr);
}

// The bounds: a short query must not turn into "half the catalog".
TEST(SongBrowserSearchTest, TypoToleranceIsBounded) {
    SongCatalog catalog = RealCatalog();
    SongSearch search(catalog);
    // Every one- and two-letter query: no fuzzy row at all, and every row
    // really contains the letters.
    int rows = 0;
    for (char a = 'a'; a <= 'z'; a++) {
        for (char b = 'a' - 1; b <= 'z'; b++) {
            std::string query(1, a);
            if (b >= 'a') query += b;
            SearchResult result = search.Search(query);
            for (size_t i = 0; i < result.rows.size(); i++) {
                const SearchRow &row = result.rows[i];
                ASSERT_NE(row.matchClass, kMatchFuzzy) << query;
                const SongEntry &song = catalog.At(row.song);
                bool contains = false;
                for (const std::string *text : { &song.title, &song.artist, &song.album }) {
                    contains = contains
                        || FoldForSearch(*text, kFoldSpaced).text.find(query) != std::string::npos
                        || FoldForSearch(*text, kFoldTight).text.find(query) != std::string::npos;
                }
                ASSERT_TRUE(contains) << query << " -> " << song.title;
                rows++;
            }
        }
    }
    EXPECT_GT(rows, 1000);
    // Every three-letter query: the rows that are only there by forgiveness
    // stay a small part of the 121 songs.  (Measured maximum when written:
    // see the page; the bound here leaves room for a larger list.)
    int worst = 0;
    std::string worstQuery;
    long total = 0;
    for (char a = 'a'; a <= 'z'; a++) {
        for (char b = 'a'; b <= 'z'; b++) {
            for (char c = 'a'; c <= 'z'; c++) {
                std::string query;
                query += a;
                query += b;
                query += c;
                SearchResult result = search.Search(query);
                int fuzzy = 0;
                for (size_t i = 0; i < result.rows.size(); i++) {
                    if (result.rows[i].matchClass == kMatchFuzzy) fuzzy++;
                }
                total += fuzzy;
                if (fuzzy > worst) {
                    worst = fuzzy;
                    worstQuery = query;
                }
            }
        }
    }
    RecordProperty("worst_three_letter_fuzzy_rows", worst);
    printf("[ measured ] three-letter queries: at most %d fuzzy rows ('%s'), %.2f on average, of %d songs\n",
           worst, worstQuery.c_str(), total / 17576.0, catalog.Size());
    EXPECT_LE(worst, catalog.Size() / 8) << "'" << worstQuery << "'";
    EXPECT_LT(total / 17576.0, 1.0);
    // The constants the two rules above are.
    EXPECT_EQ(SongSearch::kMinFuzzyWord + 0, 3);
    EXPECT_EQ(SongSearch::kMinEditWord + 0, 4);
    // Three letters with one wrong is not enough to go on: "uxh" is not Usher.
    EXPECT_EQ(RowOf(catalog, search.Search("uxh"), "scream"), nullptr);
}

TEST(SongBrowserSearchTest, MatchRangesAreBytesOfTheOriginal) {
    // Not from the real list: accents.  "Déjà Vu" is 9 bytes, "Beyoncé" 8.
    SongCatalog catalog;
    catalog.Add(Song("dejavu", "D\xC3\xA9j\xC3\xA0 Vu", "Beyonc\xC3\xA9 ft. Jay-Z", "B'Day"));
    catalog.Add(Song("strasse", "Stra\xC3\x9F" "e 7", "Kraftwerk"));
    SongSearch search(catalog);
    struct Case {
        const char *query, *song, *ranges;
    } cases[] = {
        { "deja", "dejavu", "title:0+6=D\xC3\xA9j\xC3\xA0" },
        { "d\xC3\xA9j\xC3\xA0", "dejavu", "title:0+6=D\xC3\xA9j\xC3\xA0" },
        { "de", "dejavu", "title:0+3=D\xC3\xA9" },
        { "vu", "dejavu", "title:7+2=Vu" },
        { "deja vu", "dejavu", "title:0+9=D\xC3\xA9j\xC3\xA0 Vu" },
        { "beyonce", "dejavu", "artist:0+8=Beyonc\xC3\xA9" },
        { "jayz", "dejavu", "artist:13+5=Jay-Z" },
        { "jay z", "dejavu", "artist:13+5=Jay-Z" },
        { "bday", "dejavu", "album:0+5=B'Day" },
        // Two fields: by field, then position.
        { "jay deja", "dejavu", "title:0+6=D\xC3\xA9j\xC3\xA0 artist:13+3=Jay" },
        // "stras" ends inside the two letters of the sharp s: the whole letter.
        { "stras", "strasse", "title:0+6=Stra\xC3\x9F" },
        { "strasse", "strasse", "title:0+7=Stra\xC3\x9F" "e" },
    };
    for (const Case &c : cases) {
        SearchResult result = search.Search(c.query);
        const SearchRow *row = RowOf(catalog, result, c.song);
        ASSERT_NE(row, nullptr) << c.query;
        EXPECT_EQ(Ranges(catalog, *row), c.ranges) << c.query;
    }

    // On the real list.
    SongCatalog real = RealCatalog();
    SongSearch realSearch(real);
    struct Case realCases[] = {
        { "ymca", "ymca", "title:0+7=Y.M.C.A" },
        { "william", "omg", "artist:10+9=will.i.am" },
        { "will i am", "omg", "artist:10+9=will.i.am" },
        { "trammps", "discoinferno", "artist:4+7=Trammps" },
        { "give it up", "giveituptome", "title:17+10=Give It Up" },
        { "rihanna only", "onlygirlintheworld", "title:0+4=Only artist:0+7=Rihanna" },
        { "s&m", "sandm", "title:0+5=S & M" },
        // Both words land on the first "Round": one range, not two.
        { "round round", "roundandround", "title:0+5=Round" },
        { "aint", "aint2proud2beg", "title:0+5=Ain't" },
        { "rihana", "sandm", "artist:0+6=Rihann" }, // the forgiven run
    };
    for (const Case &c : realCases) {
        SearchResult result = realSearch.Search(c.query);
        const SearchRow *row = RowOf(real, result, c.song);
        ASSERT_NE(row, nullptr) << c.query;
        EXPECT_EQ(Ranges(real, *row), c.ranges) << c.query;
    }
    // Every range of every row of a spread of queries is inside its string
    // and never splits a character.
    for (const char *query : { "a", "the", "love", "usher ft", "rihana", "o", "2", "lil jon" }) {
        SearchResult result = realSearch.Search(query);
        for (size_t i = 0; i < result.rows.size(); i++) {
            const SearchRow &row = result.rows[i];
            ASSERT_FALSE(row.ranges.empty()) << query;
            for (size_t r = 0; r < row.ranges.size(); r++) {
                const MatchRange &range = row.ranges[r];
                const std::string text = FieldText(real.At(row.song), range.field);
                ASSERT_GE(range.begin, 0);
                ASSERT_GT(range.length, 0);
                ASSERT_LE(range.begin + range.length, (int)text.size());
                if (r > 0 && row.ranges[r - 1].field == range.field) {
                    ASSERT_GT(range.begin, row.ranges[r - 1].begin + row.ranges[r - 1].length);
                }
            }
        }
    }
}

TEST(SongBrowserSearchTest, TheSameQueryGivesTheSameRows) {
    SongCatalog catalog = RealCatalog();
    SongCatalog backwards = RealCatalog("", true);
    catalog.SetRecents(List({ "ymca", "omg", "scream" }));
    backwards.SetRecents(List({ "ymca", "omg", "scream" }));
    SongSearch search(catalog);
    SongSearch searchBackwards(backwards);
    for (const char *query : { "a", "the", "love", "you", "usher", "rihana", "ft", "s", "yeah",
                               "da", "o", "lil", "2", "feat" }) {
        std::vector<std::string> first = Names(catalog, search.Search(query));
        EXPECT_EQ(first, Names(catalog, search.Search(query))) << query;
        // The order the songs were added in does not show.
        EXPECT_EQ(first, Names(backwards, searchBackwards.Search(query))) << query;
        ASSERT_FALSE(first.empty()) << query;
    }
    // Two songs with the same title and artist: the short name decides.
    SongCatalog twins;
    twins.Add(Song("twin_b", "Same", "Same"));
    twins.Add(Song("twin_a", "Same", "Same"));
    EXPECT_EQ(Names(twins, SongSearch(twins).Search("same")), List({ "twin_a", "twin_b" }));
}

TEST(SongBrowserSearchTest, MaxRowsKeepsTheBestRows) {
    SongCatalog catalog = RealCatalog();
    SongSearch search(catalog);
    std::vector<std::string> all = Names(catalog, search.Search("s"));
    ASSERT_GT(all.size(), 10u);
    std::vector<std::string> five = Names(catalog, search.Search("s", 5));
    EXPECT_EQ(five, std::vector<std::string>(all.begin(), all.begin() + 5));
    EXPECT_EQ(search.Search("s", 0).rows.size(), all.size());
}

TEST(SongBrowserSearchTest, RebuildFollowsTheCatalog) {
    SongCatalog catalog = RealCatalog("ham3");
    SongSearch search(catalog);
    EXPECT_EQ(search.Size(), 45);
    EXPECT_TRUE(search.Search("closer").rows.empty()); // a DLC song
    SongCatalog all = RealCatalog();
    search.Rebuild(all);
    EXPECT_EQ(search.Size(), 121);
    EXPECT_EQ(Names(all, search.Search("closer")), List({ "closer" }));
}

// 500 songs: the real 121 a little over four times, each copy renamed.
TEST(SongBrowserSearchTest, FiveHundredSongsTakeWellUnderAMillisecond) {
    SongCatalog real = RealCatalog();
    SongCatalog catalog;
    for (int i = 0; i < 500; i++) {
        SongEntry song = real.At(i % real.Size());
        int copy = i / real.Size();
        if (copy > 0) {
            song.shortName += "_" + std::to_string(copy);
            song.id += 100000 * copy;
            song.title += " (Remix " + std::to_string(copy) + ")";
        }
        catalog.Add(song);
    }
    ASSERT_EQ(catalog.Size(), 500);
    SongSearch search(catalog);
    const char *queries[] = { "y",        "yo",           "you",        "love",  "usher ft pitbull",
                              "rihana",   "will i am",    "the",        "qqqq",  "macarana",
                              "s",        "blackeyedpeas", "lil jon",   "2",     "everybody dance now" };
    const int numQueries = sizeof(queries) / sizeof(queries[0]);
    // The best of several passes: what the code costs, not what else the
    // machine was doing.
    double bestMicros = 1e12, worstQueryMicros = 0;
    const char *worstQuery = "";
    size_t rows = 0;
    for (int pass = 0; pass < 7; pass++) {
        double passWorst = 0;
        const char *passWorstQuery = "";
        std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
        for (int q = 0; q < numQueries; q++) {
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            rows += search.Search(queries[q]).rows.size();
            double micros = std::chrono::duration<double, std::micro>(
                                std::chrono::steady_clock::now() - start
            )
                                .count();
            if (micros > passWorst) {
                passWorst = micros;
                passWorstQuery = queries[q];
            }
        }
        double micros = std::chrono::duration<double, std::micro>(
                            std::chrono::steady_clock::now() - before
        )
                            .count()
            / numQueries;
        if (micros < bestMicros) {
            bestMicros = micros;
            worstQueryMicros = passWorst;
            worstQuery = passWorstQuery;
        }
    }
    EXPECT_GT(rows, 0u);
    printf("[ measured ] 500 songs: %.1f us a search on average over %d queries; slowest '%s' %.1f us\n",
           bestMicros, numQueries, worstQuery, worstQueryMicros);
    EXPECT_LT(bestMicros, 1000.0);
    EXPECT_LT(worstQueryMicros, 1000.0) << worstQuery;
}

// ===========================================================================
// Arrangements
// ===========================================================================

namespace {

// Sections are in row order, start where the one before ended, and cover
// every row.
void ExpectSectionsCoverRows(const SongList &list) {
    int next = 0;
    for (size_t i = 0; i < list.sections.size(); i++) {
        EXPECT_EQ(list.sections[i].firstRow, next) << "section " << i;
        EXPECT_GT(list.sections[i].count, 0) << "section " << i;
        for (int row = next; row < next + list.sections[i].count; row++) {
            ASSERT_EQ(list.SectionForRow(row), (int)i);
        }
        next += list.sections[i].count;
    }
    EXPECT_EQ(next, (int)list.rows.size());
    EXPECT_EQ(list.rowLetter.size(), list.rows.size());
    EXPECT_EQ(list.SectionForRow(-1), -1);
    EXPECT_EQ(list.SectionForRow(next), -1);
}

std::vector<std::string> Tokens(const SongList &list) {
    std::vector<std::string> tokens;
    for (size_t i = 0; i < list.sections.size(); i++) {
        tokens.push_back(list.sections[i].token);
    }
    return tokens;
}

std::vector<std::string> ReadSongLines(const std::string &path) {
    std::vector<std::string> songs;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 5, "song ") == 0) songs.push_back(line.substr(5));
    }
    return songs;
}

std::vector<std::string> Without(std::vector<std::string> names, const std::string &name) {
    names.erase(std::remove(names.begin(), names.end(), name), names.end());
    return names;
}

} // namespace

TEST(SongBrowserSectionsTest, ByTitleIsOneSectionPerLetter) {
    SongCatalog catalog = RealCatalog();
    SongList list = Arrange(catalog, kByTitle);
    ASSERT_EQ(list.rows.size(), 121u);
    ExpectSectionsCoverRows(list);
    EXPECT_EQ(Tokens(list),
              List({ "123", "A", "B", "C", "D", "E", "F", "G", "H", "I", "L", "M", "N", "O", "P",
                     "R", "S", "T", "V", "W", "Y" }));
    // Digits and symbols are one section, first; here that is one song.
    EXPECT_EQ(list.sections[0].count, 1);
    EXPECT_EQ(catalog.At(list.rows[0]).shortName, "12step");
    EXPECT_EQ(list.sections[0].title, "123");
    // Every row is under its letter, and the rows are in title order.
    for (size_t row = 0; row < list.rows.size(); row++) {
        const SongEntry &song = catalog.At(list.rows[row]);
        const Section &section = list.sections[list.SectionForRow(row)];
        EXPECT_EQ(section.token, SortCharLabel(SortCharRank(song.title, false))) << song.title;
        EXPECT_EQ(list.rowLetter[row], SortCharRank(song.title, false));
        if (row > 0) {
            EXPECT_LE(CompareSortKeys(SortKey(catalog.At(list.rows[row - 1]).title, false),
                                      SortKey(song.title, false)),
                      0)
                << song.title;
        }
    }
    // Titles are not stripped of "The": the game's title sorts do not either.
    EXPECT_EQ(list.sections[list.SectionForRow(list.RowForSong(catalog, "thehustle"))].token, "T");
    // A leading "(" is skipped: "(When You Gonna) Give It Up to Me" is a W.
    EXPECT_EQ(list.sections[list.SectionForRow(list.RowForSong(catalog, "giveituptome"))].token, "W");
}

// The image's own by_song list (disc songs, dumped before any native song
// select work: native/tests/data/song_select/classic_rows_by_song.txt).
TEST(SongBrowserSectionsTest, ByTitleAgreesWithTheImagesBySongList) {
    std::vector<std::string> image =
        ReadSongLines(BinaryDir() + "/../tests/data/song_select/classic_rows_by_song.txt");
    ASSERT_EQ(image.size(), 45u);
    SongCatalog catalog = RealCatalog("ham3");
    SongList list = Arrange(catalog, kByTitle);
    std::vector<std::string> ours = Names(catalog, list.rows);
    // One song is somewhere else, and on purpose.  The image puts
    // "(When You Gonna) Give It Up to Me" in its first section ("( - A") --
    // its header rule reads the raw first character -- but orders it inside
    // that section as a W, which is what its item rule says.  Here it is a W.
    EXPECT_EQ(Without(ours, "giveituptome"), Without(image, "giveituptome"));
    EXPECT_NE(ours, image);
    EXPECT_EQ(image[3], "giveituptome");
}

TEST(SongBrowserSectionsTest, ByArtistFollowsTheNativeArtistSort) {
    SongCatalog catalog = RealCatalog();
    SongList list = Arrange(catalog, kByArtist);
    ExpectSectionsCoverRows(list);
    ASSERT_EQ(list.rows.size(), 121u);
    EXPECT_EQ(list.sections[0].token, "123");
    // "2NE1" and "50 Cent": not letters.
    EXPECT_EQ(Names(catalog, std::vector<int>(list.rows.begin(),
                                              list.rows.begin() + list.sections[0].count)),
              List({ "iamthebest", "indaclub" }));
    // "The Trammps" is a T: the artist sort skips the article.
    EXPECT_EQ(list.sections[list.SectionForRow(list.RowForSong(catalog, "discoinferno"))].token, "T");
    for (size_t row = 0; row < list.rows.size(); row++) {
        const SongEntry &song = catalog.At(list.rows[row]);
        EXPECT_EQ(list.sections[list.SectionForRow(row)].token,
                  SortCharLabel(SortCharRank(song.artist, true)))
            << song.artist;
        if (row == 0) continue;
        const SongEntry &before = catalog.At(list.rows[row - 1]);
        // Artist, then title.
        int byArtist = CompareSortKeys(SortKey(before.artist, true), SortKey(song.artist, true));
        EXPECT_LE(byArtist, 0) << before.artist << " / " << song.artist;
        if (byArtist == 0) {
            EXPECT_LE(CompareSortKeys(SortKey(before.title, false), SortKey(song.title, false)), 0)
                << before.title << " / " << song.title;
        }
    }
    // Rihanna's three own songs are together, by title; the one with a
    // featured artist is another artist string, after them.
    int first = list.RowForSong(catalog, "onlygirlintheworld");
    ASSERT_GE(first, 0);
    ASSERT_LE(first + 4, (int)list.rows.size());
    EXPECT_EQ(Names(catalog, std::vector<int>(list.rows.begin() + first,
                                              list.rows.begin() + first + 4)),
              List({ "onlygirlintheworld", "rudeboy", "sandm", "whatsmyname" }));
}

// Today's artist sort joins letters until a section has four songs.
TEST(SongBrowserSectionsTest, SmallSectionsCanBeJoinedLikeTodaysArtistSort) {
    SongCatalog catalog = RealCatalog("ham3");
    ArrangeOptions options;
    options.minSectionSongs = 4;
    SongList list = Arrange(catalog, kByArtist, options);
    ExpectSectionsCoverRows(list);
    SongList single = Arrange(catalog, kByArtist);
    EXPECT_EQ(list.rows, single.rows) << "joining sections does not move a row";
    EXPECT_LT(list.sections.size(), single.sections.size());
    for (size_t i = 0; i < list.sections.size(); i++) {
        const Section &section = list.sections[i];
        EXPECT_GE(section.count, 4) << section.token;
        // The label is the first and last sort character of its rows.
        std::string first = SortCharLabel(
            SortCharRank(catalog.At(list.rows[section.firstRow]).artist, true));
        std::string last = SortCharLabel(SortCharRank(
            catalog.At(list.rows[section.firstRow + section.count - 1]).artist, true));
        EXPECT_EQ(section.token, first == last ? first : first + " - " + last);
        // A section is whole letters: the next one starts with another.
        if (i + 1 < list.sections.size()) {
            EXPECT_NE(SortCharRank(catalog.At(list.rows[section.firstRow + section.count]).artist, true),
                      SortCharRank(catalog.At(list.rows[section.firstRow + section.count - 1]).artist, true));
        }
    }
    EXPECT_EQ(list.sections[0].token, "123 - A");
    // By hand: 3 songs under A, 1 under B, 5 under C -> "A - B" (4), "C" (5);
    // then a lone D at the end joins the section before it.
    SongCatalog small;
    const char *artists[] = { "Abba", "Aqua", "Adele", "Blur", "Cher", "Cream", "Coolio",
                              "Ciara", "Chic", "Dido" };
    for (int i = 0; i < 10; i++) {
        small.Add(Song(("s" + std::to_string(i)).c_str(), "Title", artists[i]));
    }
    SongList joined = Arrange(small, kByArtist, options);
    EXPECT_EQ(Tokens(joined), List({ "A - B", "C - D" }));
    EXPECT_EQ(joined.sections[0].count, 4);
    EXPECT_EQ(joined.sections[1].count, 6);
    // The other arrangements have no letters to join.
    EXPECT_EQ(Tokens(Arrange(catalog, kByDifficulty, options)),
              Tokens(Arrange(catalog, kByDifficulty)));
}

TEST(SongBrowserSectionsTest, ByDifficultyFollowsTheTiers) {
    SongCatalog catalog = RealCatalog();
    SongList list = Arrange(catalog, kByDifficulty);
    ExpectSectionsCoverRows(list);
    EXPECT_EQ(Tokens(list), List({ "song_tier_0", "song_tier_1", "song_tier_2", "song_tier_3",
                                   "song_tier_4", "song_tier_5", "song_tier_6" }));
    int counts[] = { 7, 19, 24, 19, 30, 13, 9 };
    const char *titles[] = { "Warmup", "Simple", "Moderate", "Tough", "Legit", "Hardcore",
                             "Off The Hook" };
    for (int tier = 0; tier < 7; tier++) {
        EXPECT_EQ(list.sections[tier].count, counts[tier]) << tier;
        EXPECT_EQ(list.sections[tier].title, titles[tier]);
    }
    // Inside a tier: by rank, then title (DifficultyCmp).
    for (size_t row = 1; row < list.rows.size(); row++) {
        const SongEntry &before = catalog.At(list.rows[row - 1]);
        const SongEntry &song = catalog.At(list.rows[row]);
        ASSERT_LE(before.difficultyTier, song.difficultyTier);
        if (before.difficultyTier != song.difficultyTier) continue;
        ASSERT_LE(before.rank, song.rank) << before.title << " / " << song.title;
        if (before.rank == song.rank) {
            ASSERT_LE(CompareSortKeys(SortKey(before.title, false), SortKey(song.title, false)), 0);
        }
    }
    // The catalog's labels win over the English defaults.
    catalog.SetLabel("song_tier_0", "Aufw\xC3\xA4rmen");
    EXPECT_EQ(Arrange(catalog, kByDifficulty).sections[0].title, "Aufw\xC3\xA4rmen");
    // A song without a tier, and one without a rank: last.
    SongEntry untiered = Song("untiered", "Aaa", "Aaa");
    untiered.difficultyTier = -1;
    catalog.Add(untiered);
    SongEntry unranked = Song("unranked", "Aaa", "Aaa");
    unranked.difficultyTier = 0;
    unranked.rank = 0;
    catalog.Add(unranked);
    SongList more = Arrange(catalog, kByDifficulty);
    EXPECT_EQ(more.sections.back().token, "no_part");
    EXPECT_EQ(catalog.At(more.rows.back()).shortName, "untiered");
    EXPECT_EQ(catalog.At(more.rows[more.sections[0].count - 1]).shortName, "unranked");
}

TEST(SongBrowserSectionsTest, BySourceIsTheGamesLocationOrder) {
    SongCatalog catalog = RealCatalog();
    SongList list = Arrange(catalog, kBySource);
    ExpectSectionsCoverRows(list);
    EXPECT_EQ(Tokens(list), List({ "ham2", "ham3", "DLC" }));
    EXPECT_EQ(list.sections[0].title, "Dance Central 2");
    EXPECT_EQ(list.sections[1].title, "Dance Central 3");
    EXPECT_EQ(list.sections[2].title, "DLC");
    EXPECT_EQ(list.sections[0].count, 44);
    EXPECT_EQ(list.sections[1].count, 45);
    EXPECT_EQ(list.sections[2].count, 32);
    for (size_t row = 1; row < list.rows.size(); row++) {
        const SongEntry &before = catalog.At(list.rows[row - 1]);
        const SongEntry &song = catalog.At(list.rows[row]);
        ASSERT_LE(before.source, song.source);
        if (before.source == song.source) {
            ASSERT_LE(CompareSortKeys(SortKey(before.title, false), SortKey(song.title, false)), 0);
        }
    }
    // A Dance Central song would be first.
    SongEntry first = Song("dc1song", "Zzz", "Zzz");
    first.origin = "ham1";
    first.source = SourceFromOrigin(first.origin);
    catalog.Add(first);
    EXPECT_EQ(Tokens(Arrange(catalog, kBySource)), List({ "ham1", "ham2", "ham3", "DLC" }));
}

TEST(SongBrowserSectionsTest, ByDecadeAndByBpm) {
    SongCatalog catalog = RealCatalog();
    SongList decades = Arrange(catalog, kByDecade);
    ExpectSectionsCoverRows(decades);
    EXPECT_EQ(Tokens(decades), List({ "decade_1970", "decade_1980", "decade_1990", "decade_2000",
                                      "decade_2010" }));
    int counts[] = { 6, 9, 18, 42, 46 };
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(decades.sections[i].count, counts[i]);
    }
    EXPECT_EQ(decades.sections[0].title, "1970s");
    for (size_t row = 1; row < decades.rows.size(); row++) {
        ASSERT_LE(catalog.At(decades.rows[row - 1]).year, catalog.At(decades.rows[row]).year);
    }

    SongList bpm = Arrange(catalog, kByBpm);
    ExpectSectionsCoverRows(bpm);
    EXPECT_EQ(bpm.sections.front().token, "bpm_70"); // "Oops (Oh My)", 79
    EXPECT_EQ(bpm.sections.front().title, "70-79 BPM");
    for (size_t row = 0; row < bpm.rows.size(); row++) {
        const SongEntry &song = catalog.At(bpm.rows[row]);
        const Section &section = bpm.sections[bpm.SectionForRow(row)];
        EXPECT_EQ(section.token, "bpm_" + std::to_string(song.bpm / 10 * 10)) << song.title;
        if (row > 0) ASSERT_LE(catalog.At(bpm.rows[row - 1]).bpm, song.bpm);
    }
    // No year, no BPM: a section of their own, last.
    catalog.Add(Song("nodata", "Aaa", "Aaa"));
    SongList decades2 = Arrange(catalog, kByDecade);
    EXPECT_EQ(decades2.sections.back().token, "decade_unknown");
    EXPECT_EQ(decades2.sections.back().title, "Unknown year");
    EXPECT_EQ(catalog.At(decades2.rows.back()).shortName, "nodata");
    SongList bpm2 = Arrange(catalog, kByBpm);
    EXPECT_EQ(bpm2.sections.back().token, "bpm_unknown");
    EXPECT_EQ(catalog.At(bpm2.rows.back()).shortName, "nodata");
}

TEST(SongBrowserSectionsTest, RecentlyPlayedRowsAreSecondRows) {
    SongCatalog catalog = RealCatalog("ham3");
    catalog.SetRecents(List({ "ymca", "closer", "omg", "scream" })); // "closer" is not installed
    ArrangeOptions options;
    options.recentRows = 6;
    for (int a = 0; a < kNumArrangements; a++) {
        SongList list = Arrange(catalog, (Arrangement)a, options);
        ExpectSectionsCoverRows(list);
        ASSERT_EQ(list.rows.size(), 48u) << ArrangementName((Arrangement)a);
        EXPECT_TRUE(list.sections[0].recents);
        EXPECT_EQ(list.sections[0].token, "recently_played");
        EXPECT_EQ(list.sections[0].title, "Recently Played");
        EXPECT_EQ(list.sections[0].count, 3);
        EXPECT_FALSE(list.sections[1].recents);
        EXPECT_EQ(Names(catalog, std::vector<int>(list.rows.begin(), list.rows.begin() + 3)),
                  List({ "ymca", "omg", "scream" }));
        // Looking a song up answers with its own row, further down.
        int row = list.RowForSong(catalog, "ymca");
        EXPECT_GE(row, 3);
        EXPECT_EQ(catalog.At(list.rows[row]).shortName, "ymca");
        EXPECT_EQ(list.rowLetter[0], -1);
        // Below the section the list is the one without it.
        SongList plain = Arrange(catalog, (Arrangement)a);
        EXPECT_EQ(std::vector<int>(list.rows.begin() + 3, list.rows.end()), plain.rows);
    }
    options.recentRows = 2;
    EXPECT_EQ(Arrange(catalog, kByTitle, options).sections[0].count, 2);
    // Nothing played, or nothing asked for: no such section.
    EXPECT_FALSE(Arrange(catalog, kByTitle).sections[0].recents);
    catalog.SetRecents(List({ "closer" }));
    options.recentRows = 6;
    EXPECT_FALSE(Arrange(catalog, kByTitle, options).sections[0].recents);
    EXPECT_EQ(Arrange(catalog, kByTitle, options).rows.size(), 45u);
}

TEST(SongBrowserSectionsTest, RowForSongAndJumpToLetter) {
    SongCatalog catalog = RealCatalog();
    SongList byTitle = Arrange(catalog, kByTitle);
    for (int i = 0; i < catalog.Size(); i++) {
        int row = byTitle.RowForSong(i);
        ASSERT_GE(row, 0);
        ASSERT_EQ(byTitle.rows[row], i);
        ASSERT_EQ(byTitle.RowForSong(catalog, catalog.At(i).shortName), row);
    }
    EXPECT_EQ(byTitle.RowForSong(catalog, "no_such_song"), -1);
    EXPECT_EQ(byTitle.RowForSong(-1), -1);
    EXPECT_EQ(byTitle.RowForSong(121), -1);

    // The first M title is the first row of the M section.
    int m = byTitle.RowForLetter('M');
    ASSERT_GE(m, 0);
    EXPECT_EQ(byTitle.sections[byTitle.SectionForRow(m)].token, "M");
    EXPECT_EQ(byTitle.sections[byTitle.SectionForRow(m)].firstRow, m);
    EXPECT_EQ(catalog.At(byTitle.rows[m]).shortName, "macarena");
    EXPECT_EQ(byTitle.RowForLetter('m'), m);
    EXPECT_EQ(byTitle.RowForLetter('#'), 0);
    EXPECT_EQ(byTitle.RowForLetter('7'), 0);
    EXPECT_EQ(byTitle.RowForLetter('X'), -1) << "no title starts with X";
    EXPECT_EQ(byTitle.RowForLetter('Z'), -1);

    // By artist the letter is the artist's.
    SongList byArtist = Arrange(catalog, kByArtist);
    int t = byArtist.RowForLetter('T');
    ASSERT_GE(t, 0);
    EXPECT_EQ(SortCharRank(catalog.At(byArtist.rows[t]).artist, true), 20);
    EXPECT_EQ(SortCharRank(catalog.At(byArtist.rows[t - 1]).artist, true), 19);
    EXPECT_EQ(byArtist.RowForLetter('#'), 0);

    // By difficulty the rows are not in letter order: the first M title,
    // wherever it is.
    SongList byDifficulty = Arrange(catalog, kByDifficulty);
    int first = byDifficulty.RowForLetter('M');
    ASSERT_GE(first, 0);
    EXPECT_EQ(SortCharRank(catalog.At(byDifficulty.rows[first]).title, false), 13);
    for (int row = 0; row < first; row++) {
        ASSERT_NE(SortCharRank(catalog.At(byDifficulty.rows[row]).title, false), 13);
    }

    // Under a Recently Played section the jump lands on the song's own row.
    catalog.SetRecents(List({ "macarena", "moveslikejagger" }));
    ArrangeOptions options;
    options.recentRows = 6;
    SongList withRecents = Arrange(catalog, kByTitle, options);
    EXPECT_EQ(withRecents.RowForLetter('M'), m + 2);
    EXPECT_EQ(withRecents.RowForLetter('#'), 2);
}

TEST(SongBrowserSectionsTest, ArrangementsHaveNamesAndAreStable) {
    for (int a = 0; a < kNumArrangements; a++) {
        Arrangement back = kNumArrangements;
        ASSERT_TRUE(ArrangementFromName(ArrangementName((Arrangement)a), back));
        EXPECT_EQ(back, (Arrangement)a);
    }
    Arrangement none = kByTitle;
    EXPECT_FALSE(ArrangementFromName("genre", none));
    // The order the songs were added in does not show.
    SongCatalog catalog = RealCatalog();
    SongCatalog backwards = RealCatalog("", true);
    for (int a = 0; a < kNumArrangements; a++) {
        EXPECT_EQ(Names(catalog, Arrange(catalog, (Arrangement)a).rows),
                  Names(backwards, Arrange(backwards, (Arrangement)a).rows))
            << ArrangementName((Arrangement)a);
    }
    // An empty catalog: no rows, no sections.
    SongCatalog empty;
    SongList nothing = Arrange(empty, kByTitle);
    EXPECT_TRUE(nothing.rows.empty());
    EXPECT_TRUE(nothing.sections.empty());
    EXPECT_EQ(nothing.RowForLetter('A'), -1);
}

// ===========================================================================
// JSON
// ===========================================================================

TEST(SongBrowserJsonTest, EscapesAndCarriesEveryField) {
    EXPECT_EQ(JsonEscape("a\"b\\c\nd\te"), "a\\\"b\\\\c\\nd\\te");
    EXPECT_EQ(JsonEscape("Beyonc\xC3\xA9"), "Beyonc\xC3\xA9");
    EXPECT_EQ(JsonEscape(std::string("a\x01z", 3)), "a\\u0001z");
    EXPECT_EQ(JsonEscape("bad\xFF" "byte"), "bad\xEF\xBF\xBD" "byte");

    SongCatalog catalog;
    SongEntry song = Song("quote", "Say \"Hey\"", "D\xC3\xA9j\xC3\xA0", "Al\\bum");
    song.id = 42;
    song.year = 1999;
    song.bpm = 120;
    song.lengthMs = 180000;
    song.rank = 12.5f;
    song.difficultyTier = 3;
    song.origin = "ham2_dlc";
    song.source = SourceFromOrigin(song.origin);
    song.albumArtPath = "songs/quote/quote_keep.png";
    song.bestStars = 4;
    song.bestScore = 123456;
    catalog.Add(song);
    catalog.SetRecents(List({ "gone", "quote" }));
    catalog.SetLabel("song_tier_3", "Tough");
    EXPECT_EQ(CatalogJson(catalog),
              "{\"count\":1,\"songs\":[{\"id\":42,\"shortName\":\"quote\","
              "\"title\":\"Say \\\"Hey\\\"\",\"artist\":\"D\xC3\xA9j\xC3\xA0\","
              "\"album\":\"Al\\\\bum\",\"year\":1999,\"bpm\":120,\"lengthMs\":180000,"
              "\"rank\":12.5,\"difficultyTier\":3,\"origin\":\"ham2_dlc\",\"source\":\"DLC\","
              "\"onDisc\":false,\"isCover\":false,"
              "\"albumArtPath\":\"songs/quote/quote_keep.png\",\"bestStars\":4,"
              "\"bestScore\":123456,\"played\":true,\"locked\":false,\"recentRank\":1}],"
              "\"history\":[\"gone\",\"quote\"],\"recents\":[\"quote\"],"
              "\"labels\":{\"song_tier_3\":\"Tough\"}}");

    SongSearch search(catalog);
    EXPECT_EQ(SearchJson(catalog, search.Search("deja"), "deja"),
              "{\"query\":\"deja\",\"searching\":true,\"words\":[\"deja\"],\"count\":1,"
              "\"rows\":[{\"shortName\":\"quote\",\"title\":\"Say \\\"Hey\\\"\","
              "\"artist\":\"D\xC3\xA9j\xC3\xA0\",\"album\":\"Al\\\\bum\","
              "\"match\":\"artist_prefix\",\"field\":\"artist\","
              "\"ranges\":[{\"field\":\"artist\",\"begin\":0,\"length\":6,"
              "\"text\":\"D\xC3\xA9j\xC3\xA0\"}]}]}");
    EXPECT_EQ(SearchJson(catalog, search.Search(" "), " "),
              "{\"query\":\" \",\"searching\":false,\"words\":[],\"count\":0,\"rows\":[]}");

    ArrangeOptions options;
    options.recentRows = 6;
    EXPECT_EQ(ArrangementJson(catalog, Arrange(catalog, kByDifficulty, options)),
              "{\"arrangement\":\"difficulty\",\"count\":2,\"sections\":["
              "{\"token\":\"recently_played\",\"title\":\"Recently Played\",\"firstRow\":0,"
              "\"count\":1,\"recents\":true},"
              "{\"token\":\"song_tier_3\",\"title\":\"Tough\",\"firstRow\":1,\"count\":1,"
              "\"recents\":false}],\"rows\":[\"quote\",\"quote\"]}");
}

// ===========================================================================
// Flow test: GET /api/songs against the game's own song select
// ===========================================================================

namespace {

bool Exists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

struct EngineRun {
    std::string setupError;
    std::string output;
    std::string driver; // the DRIVER: lines
};

// The shell functions of the driver, as in test_native_song_select.cpp
// (every wait bounded; the wait after a button counted in engine frames).
const char *const kDriverFunctions = R"SH(
L='{song_select_panel find right_hand.hnl}';
scr() { curl -s -m 5 $api/screen | sed -n 's/.*"screen":"\([^"]*\)".*/\1/p'; };
ev() { curl -s -m 30 -X POST --data-binary "$1" $api/dta/eval; };
frame() { curl -s -m 5 $api/frame | sed -n 's/.*"frame":\([0-9]*\).*/\1/p'; };
settle() { f=$(frame); [ -n "$f" ] && curl -s -m 70 "$api/frame/wait/$((f+$1))" > /dev/null; };
pace() { curl -s -m 5 -X PUT "$api/pace?fps=$1" > /dev/null; };
press() { curl -s -m 10 -X POST --data-binary "{\"button\":\"$1\"}" $api/input/press > /dev/null; settle ${2:-45}; };
waitfor() { end=$((SECONDS+$1)); shift; while [ $SECONDS -lt $end ] && kill -0 $pid 2>/dev/null; do
  s=$(scr); for w in "$@"; do [ "$s" = "$w" ] && return 0; done; sleep 0.2; done; return 1; };
nav() { from=$(scr); n=0; while [ $n -lt 4 ]; do press confirm 60; waitfor ${2:-6} "$1" && return 0;
  [ "$(scr)" = "$from" ] || break; n=$((n+1)); done; waitfor 90 "$1"; };
expand() { if ev '{song_offer_provider get_header_mode}' | grep -q '"value":1}'; then press cancel 150; fi; };
sortname() { ev '{song_offer_provider get_current_sort_name}' | sed -n 's/.*"value":"\([^"]*\)".*/\1/p'; };
rows() { echo "DRIVER: $1 $(ev '{do ($s "") {foreach_int $i 0 {song_offer_provider num_data} {set $s {sprint $s {cond ({song_offer_provider data_is $i song} "song") ({song_offer_provider data_is $i header} "header") (TRUE "function")} " " {song_offer_provider get_token $i} ";"}}} $s}')"; };
api() { echo "DRIVER: $1 $(curl -s -m 30 "$api/songs$2")"; };
)SH";

// One dc3-native, headless and without a renderer, disc songs only, against
// a fresh persistence root seeded so a Perform song starts without the
// tutorial.  Boots to main_screen, then runs `drive`.  Killed by PID.
EngineRun RunEngine(const std::string &drive) {
    EngineRun run;
    std::string bin = BinaryDir() + "/dc3-native";
    std::string script = BinaryDir() + "/../../scripts/dc3-input-flows/idle-main.txt";
    if (!Exists(bin)) {
        run.setupError = "dc3-native does not exist at " + bin;
        return run;
    }
    if (!Exists(script)) {
        run.setupError = "input flow script missing: " + script;
        return run;
    }
    std::string root = MakeTestTempDir("dc3-songbrowser");
    if (root.empty()) {
        run.setupError = "could not create a temporary persistence root";
        return run;
    }
    mkdir((root + "/cache").c_str(), 0755);
    mkdir((root + "/saves").c_str(), 0755);
    if (!SeedGlobalOptions(root, kTutorialGeneral)) {
        run.setupError = "could not write the seeded global options under " + root;
        return run;
    }
    std::string dir = root + "/run";
    mkdir(dir.c_str(), 0755);
    std::string log = dir + "/engine.log";
    std::ostringstream sh;
    sh << "t=$(command -v timeout || command -v gtimeout); cd " << BinaryDir() << " && "
       << "( MILO_HEADLESS=1 MILO_NORENDER=1 MILO_FATAL_FAILS=0 DC3_SHOW_SPLASH=0 DC3_FAST_BOOT=1"
       << " DC3_DLC=0" << EngineHttpPortShellEnv(dir) << " MILO_MAX_FRAMES=0"
       << " DC3_CACHE_DIR=" << root << "/cache DC3_SAVE_DIR=" << root << "/saves"
       << " MILO_INPUT_SCRIPT=" << script << " DC3_FAST_TIME=1"
       << " ${t:+$t 600} ./dc3-native > " << log << " 2>&1 & echo $! > " << log << ".pid ); "
       << "pid=$(cat " << log << ".pid); " << EngineHttpApiShell(dir, "pid", 180)
       << kDriverFunctions
       << "waitfor 180 main_screen; echo \"DRIVER: main=$(scr)\"; settle 120; pace 240; "
       << drive
       << "kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null; "
       << "tail -c 6000 " << log << "; rm -f " << log << " " << log << ".pid";
    FILE *pipe = popen(sh.str().c_str(), "r");
    if (!pipe) {
        run.setupError = "popen failed";
        return run;
    }
    std::string line;
    char buf[4096];
    while (fgets(buf, sizeof buf, pipe)) {
        line += buf;
        if (line.empty() || line[line.size() - 1] != '\n') continue;
        run.output += line;
        if (line.compare(0, 7, "DRIVER:") == 0) run.driver += line;
        line.clear();
    }
    run.output += line;
    pclose(pipe);
    if (getenv("DC3_TEST_SHOW_DRIVER")) fputs(run.driver.c_str(), stderr);
    return run;
}

std::string DriverLine(const EngineRun &run, const std::string &tag) {
    std::string prefix = "DRIVER: " + tag + " ";
    size_t at = 0;
    while (at < run.driver.size()) {
        size_t end = run.driver.find('\n', at);
        if (end == std::string::npos) end = run.driver.size();
        if (run.driver.compare(at, prefix.size(), prefix) == 0) {
            return run.driver.substr(at + prefix.size(), end - at - prefix.size());
        }
        at = end + 1;
    }
    return "";
}

// Every value of "key":"value" in `json`, in order.  (Short names and tokens
// have nothing to unescape.)
std::vector<std::string> JsonValues(const std::string &json, const std::string &key) {
    std::vector<std::string> values;
    std::string wanted = "\"" + key + "\":\"";
    size_t at = json.find(wanted);
    while (at != std::string::npos) {
        size_t begin = at + wanted.size();
        size_t end = json.find('"', begin);
        if (end == std::string::npos) break;
        values.push_back(json.substr(begin, end - begin));
        at = json.find(wanted, end);
    }
    return values;
}

// The strings of the array "key":["a","b"] in `json`.
std::vector<std::string> JsonArray(const std::string &json, const std::string &key) {
    std::vector<std::string> values;
    std::string wanted = "\"" + key + "\":[";
    size_t at = json.find(wanted);
    if (at == std::string::npos) return values;
    at += wanted.size();
    while (at < json.size() && json[at] != ']') {
        if (json[at] == '"') {
            size_t end = json.find('"', at + 1);
            if (end == std::string::npos) break;
            values.push_back(json.substr(at + 1, end - at - 1));
            at = end + 1;
        } else {
            at++;
        }
    }
    return values;
}

// The game's list as `rows TAG` wrote it: "song NAME" and "header TOKEN"
// rows; a symbol with a space in it comes between single quotes.
void GameRows(const EngineRun &run, const std::string &tag, std::vector<std::string> &songs,
              std::vector<std::string> &headers) {
    std::string json = DriverLine(run, tag);
    std::vector<std::string> value = JsonValues(json, "value");
    if (value.empty()) return;
    std::vector<std::string> rows = SplitOn(value[0], ';');
    for (size_t i = 0; i < rows.size(); i++) {
        size_t space = rows[i].find(' ');
        if (space == std::string::npos) continue;
        std::string kind = rows[i].substr(0, space);
        std::string token = rows[i].substr(space + 1);
        if (token.size() >= 2 && token[0] == '\'' && token[token.size() - 1] == '\'') {
            token = token.substr(1, token.size() - 2);
        }
        if (kind == "song") songs.push_back(token);
        if (kind == "header") headers.push_back(token);
    }
}

std::vector<std::string> Sorted(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

// GET /api/songs lists exactly the songs song select offers, each
// arrangement is the game's own sort row for row, and a song that reached
// gameplay is the first recent.  One engine run.
TEST(SongBrowserFlowTest, ApiSongsIsSongSelectsListAndAPlayedSongIsTheFirstRecent) {
    if (!getenv("DC3_DTA_FLOW_TESTS")) {
        GTEST_SKIP() << "Set DC3_DTA_FLOW_TESTS=1 to enable (requires game assets)";
    }
    std::ostringstream d;
    d << "api boot ''; "
      << "nav choose_mode_screen; settle 90; nav song_select_screen; "
      << "echo \"DRIVER: at=$(scr)\"; settle 240; "
      // The four sorts, each opened: difficulty, title, artist, source.
      << "expand; echo \"DRIVER: sort0 $(sortname)\"; rows rows0; "
      << "press y 150; expand; echo \"DRIVER: sort1 $(sortname)\"; rows rows1; "
      << "press y 150; expand; echo \"DRIVER: sort2 $(sortname)\"; rows rows2; "
      << "press y 150; expand; echo \"DRIVER: sort3 $(sortname)\"; rows rows3; "
      << "api songs ''; api difficulty '?sort=difficulty&recents=0'; "
      << "api title '?sort=title&recents=0'; api artist '?sort=artist&min=4&recents=0'; "
      << "api source '?sort=source&recents=0'; api search '?q=will+i+am'; "
      << "api notsearching '?q=+'; api badsort '?sort=genre'; "
      // To a song row, then into the song: gameplay is what counts as played.
      << "for i in 1 2 3 4; do ev \"{song_offer_provider data_is {$L get_selected} song}\" "
      << "| grep -q '\"value\":1}' && break; press down; done; "
      << "echo \"DRIVER: pick $(ev \"{song_offer_provider get_token {$L get_selected}}\")\"; "
      << "press confirm 60; waitfor 30 multiuser_screen; settle 120; nav game_screen 3; "
      << "echo \"DRIVER: game=$(scr)\"; api after ''; api after_title '?sort=title'; ";
    EngineRun run = RunEngine(d.str());
    ASSERT_TRUE(run.setupError.empty()) << run.setupError;
    std::string tail = run.output.substr(run.output.size() > 3000 ? run.output.size() - 3000 : 0);
    ASSERT_NE(run.driver.find("DRIVER: at=song_select_screen"), std::string::npos)
        << "precondition: the driver never reached song select\n"
        << run.driver.substr(0, 2000) << "\n--- tail ---\n"
        << tail;

    // --- The same songs -----------------------------------------------------
    std::string songsJson = DriverLine(run, "songs");
    ASSERT_NE(songsJson.find("\"ok\":true"), std::string::npos) << songsJson.substr(0, 400);
    std::vector<std::string> apiSongs = JsonValues(songsJson, "shortName");
    std::vector<std::string> gameSongs[4], gameHeaders[4];
    for (int i = 0; i < 4; i++) {
        GameRows(run, "rows" + std::to_string(i), gameSongs[i], gameHeaders[i]);
    }
    ASSERT_FALSE(gameSongs[0].empty()) << DriverLine(run, "rows0").substr(0, 400);
    EXPECT_EQ(apiSongs.size(), 45u) << "the disc's songs";
    EXPECT_EQ(Sorted(apiSongs), Sorted(gameSongs[0]))
        << "GET /api/songs and song select's list are not the same songs";
    EXPECT_EQ(std::set<std::string>(apiSongs.begin(), apiSongs.end()).size(), apiSongs.size());
    EXPECT_NE(songsJson.find("\"count\":" + std::to_string(gameSongs[0].size()) + ","),
              std::string::npos);
    // Before song select was ever entered the answer was already that list.
    EXPECT_EQ(JsonValues(DriverLine(run, "boot"), "shortName"), apiSongs);
    // A fresh save: nothing played.
    EXPECT_NE(songsJson.find("\"history\":[],\"recents\":[]"), std::string::npos);
    // The section names are the game's own strings.
    EXPECT_NE(songsJson.find("\"song_tier_0\":\"Warmup\""), std::string::npos);
    EXPECT_NE(songsJson.find("\"ham3\":\"Dance Central 3\""), std::string::npos);

    // --- The same order in every sort ---------------------------------------
    ASSERT_EQ(DriverLine(run, "sort0"), "by_difficulty");
    ASSERT_EQ(DriverLine(run, "sort1"), "by_song");
    ASSERT_EQ(DriverLine(run, "sort2"), "by_artist");
    ASSERT_EQ(DriverLine(run, "sort3"), "by_location");
    std::string difficulty = DriverLine(run, "difficulty");
    EXPECT_EQ(JsonArray(difficulty, "rows"), gameSongs[0]);
    EXPECT_EQ(JsonValues(difficulty, "token"), gameHeaders[0]);
    // By title: one song apart, on purpose (see
    // SongBrowserSectionsTest.ByTitleAgreesWithTheImagesBySongList).
    EXPECT_EQ(Without(JsonArray(DriverLine(run, "title"), "rows"), "giveituptome"),
              Without(gameSongs[1], "giveituptome"));
    // By artist, with today's section joining: the same sections too.
    std::string artist = DriverLine(run, "artist");
    EXPECT_EQ(JsonArray(artist, "rows"), gameSongs[2]);
    EXPECT_EQ(JsonValues(artist, "token"), gameHeaders[2]);
    std::string source = DriverLine(run, "source");
    EXPECT_EQ(JsonArray(source, "rows"), gameSongs[3]);
    EXPECT_EQ(JsonValues(source, "token"), gameHeaders[3]);

    // --- Search, over HTTP --------------------------------------------------
    std::string found = DriverLine(run, "search");
    EXPECT_NE(found.find("\"searching\":true"), std::string::npos) << found;
    EXPECT_EQ(JsonValues(found, "shortName"), List({ "omg" }));
    EXPECT_NE(found.find("\"match\":\"artist_word\""), std::string::npos) << found;
    EXPECT_NE(found.find("\"begin\":10,\"length\":9,\"text\":\"will.i.am\""), std::string::npos)
        << found;
    EXPECT_NE(DriverLine(run, "notsearching").find("\"searching\":false"), std::string::npos);
    EXPECT_NE(DriverLine(run, "badsort").find("\"ok\":false"), std::string::npos);

    // --- A played song is the first recent ----------------------------------
    std::vector<std::string> pick = JsonValues(DriverLine(run, "pick"), "value");
    ASSERT_EQ(pick.size(), 1u) << DriverLine(run, "pick");
    ASSERT_NE(std::find(apiSongs.begin(), apiSongs.end(), pick[0]), apiSongs.end())
        << "the row picked is not a song: " << pick[0];
    ASSERT_NE(run.driver.find("DRIVER: game=game_screen"), std::string::npos)
        << "precondition: the song never reached gameplay\n"
        << run.driver.substr(run.driver.size() > 1500 ? run.driver.size() - 1500 : 0);
    std::string after = DriverLine(run, "after");
    EXPECT_EQ(JsonArray(after, "history"), List({ pick[0].c_str() }));
    EXPECT_EQ(JsonArray(after, "recents"), List({ pick[0].c_str() }));
    EXPECT_NE(after.find("\"shortName\":\"" + pick[0] + "\""), std::string::npos);
    // Its entry says so, and it is the only one that does.
    size_t entry = after.find("\"shortName\":\"" + pick[0] + "\"");
    size_t entryEnd = after.find('}', entry);
    std::string entryJson = after.substr(entry, entryEnd - entry);
    EXPECT_NE(entryJson.find("\"played\":true"), std::string::npos) << entryJson;
    EXPECT_NE(entryJson.find("\"recentRank\":0"), std::string::npos) << entryJson;
    EXPECT_EQ(JsonValues(after, "shortName"), apiSongs) << "the songs did not change";
    // ... and the arrangements start with it.
    std::string afterTitle = DriverLine(run, "after_title");
    std::vector<std::string> tokens = JsonValues(afterTitle, "token");
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens[0], "recently_played");
    std::vector<std::string> rows = JsonArray(afterTitle, "rows");
    ASSERT_EQ(rows.size(), 46u);
    EXPECT_EQ(rows[0], pick[0]);
}
