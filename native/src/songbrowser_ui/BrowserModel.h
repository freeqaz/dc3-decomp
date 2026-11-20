// BrowserModel: what the song browser's list holds, with no widgets in it.
//
// It turns the catalog plus "which arrangement" plus "what is typed" into one
// flat run of rows, the shape nativeui::ListView wants:
//   browsing    a heading row before each section, then that section's songs
//   searching   one heading row saying how many songs match, then the ranked
//               results (songbrowser::SongSearch)
// and answers the questions the screen asks about them: which row is a song,
// where a song's row is, where a section jump lands, what a tier is called.
//
// Rows are indices; nothing here outlives the catalog it was given. After the
// catalog changes, call SetCatalog again.
#pragma once

#include "songbrowser/SongCatalog.h"
#include "songbrowser/SongSearch.h"
#include "songbrowser/SongSections.h"

#include <map>
#include <string>
#include <vector>

namespace songbrowser_ui {

    struct BrowserRow {
        bool heading = false;
        int song = -1; // catalog index; -1 for a heading
        int section = -1; // index into the arrangement's sections; -1 in a search
        int result = -1; // index into the search results; -1 when browsing
    };

    class BrowserModel {
    public:
        // Sections of the title and artist arrangements hold at least this many
        // songs ("A - C"), as the game's own artist sort does; a heading per
        // letter would be most of the list.
        static constexpr int kMinSectionSongs = 4;

        // `maxRecents`: the most Recently Played songs to offer (0: all).
        void SetCatalog(const songbrowser::SongCatalog &catalog, int maxRecents);
        void SetArrangement(songbrowser::Arrangement arrangement);
        void SetQuery(const std::string &query);

        bool HasCatalog() const { return catalog_ != nullptr; }
        const songbrowser::SongCatalog &Catalog() const { return *catalog_; }
        songbrowser::Arrangement GetArrangement() const { return arrangement_; }
        const std::string &Query() const { return query_; }
        // True when the query has something to search for; "   " does not.
        bool Searching() const { return found_.searching; }

        const std::vector<BrowserRow> &Rows() const { return rows_; }
        int SongCount() const { return songRows_; }
        // The play history that can be shown, newest first, as catalog indices.
        const std::vector<int> &Recents() const { return recents_; }

        // -1 for a heading or a row that does not exist.
        int SongAt(int row) const;
        // The row showing `song`; -1 when the list does not have it.
        int RowOfSong(int song) const;
        // Where a section jump from `row` lands: the first song of the next
        // (+1) or previous (-1) section. -1 when there is none that way, and
        // always in a search, which has no sections.
        int SectionJump(int row, int direction) const;

        std::string HeadingTitle(int row) const;
        std::string HeadingCount(int row) const;
        // What the search matched in this row; null when browsing.
        const songbrowser::SearchRow *ResultAt(int row) const;

        // The catalog's names for a difficulty tier and a source.
        std::string TierName(int tier) const;
        std::string SourceName(songbrowser::SongSource source) const;

    private:
        void Rebuild();

        const songbrowser::SongCatalog *catalog_ = nullptr;
        songbrowser::SongSearch search_;
        songbrowser::Arrangement arrangement_ = songbrowser::kByTitle;
        std::string query_;
        songbrowser::SongList list_;
        songbrowser::SearchResult found_;
        std::vector<BrowserRow> rows_;
        std::vector<int> rowOfSong_; // by catalog index
        std::vector<int> recents_;
        std::map<int, std::string> tierNames_;
        std::map<int, std::string> sourceNames_;
        int songRows_ = 0;
    };

}
