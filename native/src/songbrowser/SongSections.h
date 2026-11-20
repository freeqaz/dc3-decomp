#pragma once
// Song browser data - the browsable arrangements
// (docs/native/reference/SONG_BROWSER_DATA.md).
//
//     SongList list = Arrange(catalog, kByArtist);
//     list.rows[r]            catalog index of row r
//     list.sections[s]        title, first row, count: the headers
//     list.RowForLetter('M')  where a jump to M lands
//
// One flat row list plus the sections that cut it up, which is what a
// virtualised list wants.  Headers are not rows.  Standard C++ only.
//
// The orders are the game's (and the native artist sort's), so this list and
// today's song select agree; the differences are listed on the page above.

#include "SongCatalog.h"

#include <string>
#include <vector>

namespace songbrowser {

enum Arrangement {
    kByTitle,      // A, B, C ...; digits and symbols together under "123"
    kByArtist,     // the same, on the artist with a leading The/A/An skipped
    kByDifficulty, // the game's difficulty tiers, easiest first
    kBySource,     // Dance Central, Dance Central 2, Dance Central 3, DLC
    kByDecade,     // 1970s, 1980s ...; songs without a year last
    kByBpm,        // 10 BPM bands, slowest first; songs without a BPM last
    kNumArrangements
};

// "title", "artist", "difficulty", "source", "decade", "bpm".
const char *ArrangementName(Arrangement arrangement);
bool ArrangementFromName(const std::string &name, Arrangement &arrangement);

struct ArrangeOptions {
    // More than 0: the list starts with a "Recently Played" section of at
    // most this many rows (SongCatalog::Recents).  Those rows are second rows
    // of their songs: every song still has its own row further down.
    int recentRows = 0;
    // kByTitle and kByArtist: a letter with fewer songs than this takes the
    // next letter in with it ("A - C"), and a short tail joins the section
    // before it.  1 is one section per letter; 4 is what today's artist sort
    // does (SongSortByArtist::kMinSectionSongs).
    int minSectionSongs = 1;
};

struct Section {
    // A stable name: "A", "123", "A - C", "song_tier_3", "no_part", "ham2",
    // "DLC", "decade_1990", "bpm_120", "recently_played".
    std::string token;
    // What to show: the catalog's label for the token when it has one
    // (SongCatalog::SetLabel), else an English default.
    std::string title;
    int firstRow = 0;
    int count = 0;
    bool recents = false; // the Recently Played section
};

struct SongList {
    Arrangement arrangement = kByTitle;
    std::vector<int> rows; // catalog index of every row, top to bottom
    std::vector<Section> sections; // in row order; together they cover every row

    // The song's own row, never its Recently Played row.  -1 when the song is
    // not in the list.
    int RowForSong(int catalogIndex) const;
    int RowForSong(const SongCatalog &catalog, const std::string &shortName) const;

    // The section row `row` is in; -1 for a row that does not exist.
    int SectionForRow(int row) const;

    // Jump to letter: the first row (below Recently Played) whose sort key
    // starts with `letter`, either case.  Any other character -- a digit, '#'
    // -- asks for the first row whose key does not start with a letter.  The
    // sort key is the artist's in kByArtist and the title's everywhere else,
    // so in kByDifficulty this is "the first song starting with M", which is
    // the easiest one.  -1 when no row does.
    int RowForLetter(char letter) const;

    // 0 ("123") or 1..26 (A..Z) for every row, as RowForLetter sees it; -1 for
    // a Recently Played row.
    std::vector<int> rowLetter;

private:
    friend SongList Arrange(const SongCatalog &, Arrangement, const ArrangeOptions &);
    std::vector<int> mRowOfSong; // by catalog index
};

SongList Arrange(
    const SongCatalog &catalog,
    Arrangement arrangement,
    const ArrangeOptions &options = ArrangeOptions()
);

} // namespace songbrowser
