#pragma once
// Song browser data - incremental search
// (docs/native/reference/SONG_BROWSER_DATA.md).
//
//     SongSearch search(catalog);                  // once per catalog
//     SearchResult found = search.Search("will i am");
//     if (!found.searching)  ...show the browsable list...
//     else for (row : found.rows)  catalog.At(row.song) ...
//
// The rules, in short (the page above has them with examples):
//
//   * Case, accents and punctuation do not matter: "beyonce" finds "Beyoncé",
//     "william" and "will i am" both find "will.i.am", "ymca" finds
//     "Y.M.C.A.", "kesha" finds "Ke$ha".  "&" and the word "and" are the same.
//   * A leading "The" may be left out: "trammps" is the start of "The Trammps".
//   * Every word of the query has to be found, in the title, the artist or
//     the album; the words may be in different fields ("usher scream").
//   * Rows are ordered by how they matched (MatchClass), then songs played
//     recently first, then by title.
//   * A query with nothing to search for (empty, spaces, punctuation only) is
//     not a search: `searching` is false and there are no rows.
//
// Standard C++ only.

#include "SongCatalog.h"
#include "SongText.h"

#include <string>
#include <vector>

namespace songbrowser {

enum MatchField { kFieldTitle = 0, kFieldArtist = 1, kFieldAlbum = 2 };

// How a row matched, best first.  This is the first sort key of the result.
enum MatchClass {
    kMatchTitlePrefix = 0,  // the title starts with the query
    kMatchTitleWord = 1,    // a later word of the title starts with it
    kMatchArtistPrefix = 2, // the artist starts with it
    kMatchArtistWord = 3,   // a later word of the artist starts with it
    kMatchAlbum = 4,        // a word of the album starts with it
    kMatchSubstring = 5,    // found, but inside a word (or not at a word start)
    kMatchFuzzy = 6,        // only found by allowing a typing mistake
};

const char *MatchFieldName(MatchField field); // "title", "artist", "album"
const char *MatchClassName(MatchClass matchClass); // "title_prefix", ..., "fuzzy"

// A run of the ORIGINAL string of `field` to highlight, in BYTES of its UTF-8
// (not code points): title.substr(begin, length) is always whole characters.
struct MatchRange {
    MatchField field;
    int begin;
    int length;
};

struct SearchRow {
    int song; // catalog index
    MatchClass matchClass;
    // The field that decided matchClass.
    MatchField field;
    // What to highlight: one range when the whole query was found as one run,
    // else one per query word.  Ordered by field, then position; never
    // overlapping within a field.
    std::vector<MatchRange> ranges;
};

struct SearchResult {
    // False when the query had nothing to search for.  That is "show the
    // list", not "no song matches": rows is empty either way.
    bool searching = false;
    // The query as it was searched: folded words.
    std::vector<std::string> words;
    std::vector<SearchRow> rows;
};

// The words a query is searched as: folded (SongText.h, tight), split at
// white space and at "&".
std::vector<std::string> QueryWords(const std::string &query);

class SongSearch {
public:
    // A typing mistake is only forgiven in a query word of at least this many
    // characters.  Shorter words must be found exactly.
    static const int kMinFuzzyWord = 3;
    // ... and "one wrong, missing, extra or swapped character" needs a word
    // this long; a 3-character word is only matched with characters missing
    // ("rhn" for "Rihanna").
    static const int kMinEditWord = 4;

    SongSearch() {}
    explicit SongSearch(const SongCatalog &catalog) { Rebuild(catalog); }

    // Reads everything the search needs out of `catalog`.  Call again after
    // the catalog's songs or its play history change; the catalog itself is
    // not kept.
    void Rebuild(const SongCatalog &catalog);

    // At most `maxRows` rows (0 or less: every match).
    SearchResult Search(const std::string &query, int maxRows = 0) const;

    int Size() const { return (int)mSongs.size(); }

private:
    struct Song {
        // [field][fold mode]
        FoldedText text[3][2];
        int recentRank;
        int titleOrder; // position in title order: the last tie-break
    };
    struct Hit {
        bool found = false;
        MatchClass matchClass = kMatchFuzzy;
        MatchField field = kFieldTitle;
        int begin = 0; // bytes of the original
        int end = 0;
    };

    static Hit FindExact(const Song &song, const std::string &term);
    static Hit FindFuzzy(const Song &song, const std::string &term);
    static Hit Find(const Song &song, const std::string &term);

    std::vector<Song> mSongs;
};

} // namespace songbrowser
