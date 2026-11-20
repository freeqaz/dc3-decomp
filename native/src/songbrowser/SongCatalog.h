#pragma once
// Song browser data - the catalog (docs/native/reference/SONG_BROWSER_DATA.md).
//
// What the song browser knows about every song it can list, as plain data:
// no engine types, so the unit tests (native/tests/test_song_browser.cpp)
// build and search catalogs without a game.  The running game fills one
// through SongCatalogSource_Native.h.
//
// Songs are referred to by their index in the catalog (0 .. Size()-1), which
// is stable until the next Clear or Add.  Strings are UTF-8.

#include <map>
#include <string>
#include <vector>

namespace songbrowser {

// Where a song comes from, as the game's "by location" sort distinguishes it
// (SongSortByLocation.cpp, ConvertGameOriginSymbolToEnum): the metadata's
// game_origin is ham1, ham2 or ham3, and anything else is a download.  The
// values are that function's, so they order the same way.
enum SongSource {
    kSourceDC1 = 0,      // game_origin ham1: imported from Dance Central
    kSourceDC2 = 1,      // game_origin ham2: imported from Dance Central 2
    kSourceDC3 = 2,      // game_origin ham3: this game's own songs
    kSourceDownload = 3, // anything else (ham1_dlc, ham2_dlc, ham3_dlc, ...)
};

SongSource SourceFromOrigin(const std::string &origin);
// The token the game's own section header uses: "ham1", "ham2", "ham3", "DLC".
const char *SourceToken(SongSource source);

struct SongEntry {
    int id = 0;            // the song id (songs.dta song_id)
    std::string shortName; // "ymca": what the game calls the song everywhere
    std::string title;     // "Y.M.C.A."
    std::string artist;    // "Village People"
    std::string album;
    int year = 0;          // year released; 0 when the metadata has none
    int bpm = 0;
    int lengthMs = 0;
    float rank = 0;          // the metadata's difficulty number (higher is harder)
    int difficultyTier = -1; // the tier that rank falls in (0 = easiest)
    std::string origin;      // game_origin as written: "ham3", "ham2_dlc", ...
    SongSource source = kSourceDC3; // SourceFromOrigin(origin)
    bool onDisc = false;     // mounted from the game's own files, not a pack
    bool isCover = false;    // shown as "as made famous by <artist>"
    std::string albumArtPath;
    int bestStars = 0;   // the active profile's best, 0..5 (6 = gold)
    int bestScore = 0;   // ... and that run's score
    bool played = false; // the profile has a score, or the song is in the history
    bool locked = false; // not yet unlocked (never true in song select's own set)
    int recentRank = -1; // position in the play history, 0 = newest; -1 = not in it
};

class SongCatalog {
public:
    void Clear();
    // Adds a song and returns its index.  A second song with a short name
    // already present replaces the first and keeps its index.
    int Add(const SongEntry &entry);

    int Size() const { return (int)mEntries.size(); }
    const SongEntry &At(int index) const { return mEntries[index]; }
    const std::vector<SongEntry> &Entries() const { return mEntries; }

    // -1 / nullptr when there is no such song.
    int IndexOf(const std::string &shortName) const;
    int IndexOfId(int id) const;
    const SongEntry *Find(const std::string &shortName) const;
    const SongEntry *FindById(int id) const;

    // The play history, newest first (NativeSongSelectState::History()).
    // Names of songs that are not in the catalog are kept -- a pack that is
    // not installed today may be tomorrow -- and every entry's recentRank and
    // played flag are brought up to date.  Call it after the last Add.
    void SetRecents(const std::vector<std::string> &newestFirst);
    const std::vector<std::string> &History() const { return mHistory; }
    // The songs of the history that are in the catalog and not locked, newest
    // first, at most `maxRows` (0 or less: all of them), as catalog indices.
    std::vector<int> Recents(int maxRows) const;

    // Display text for a section token ("song_tier_3", "ham2", "DLC"), set by
    // whoever fills the catalog from localized strings.  Label returns
    // `fallback` for a token nobody set.
    void SetLabel(const std::string &token, const std::string &text);
    std::string Label(const std::string &token, const std::string &fallback) const;
    const std::map<std::string, std::string> &Labels() const { return mLabels; }

private:
    std::vector<SongEntry> mEntries;
    std::map<std::string, int> mByShortName;
    std::map<int, int> mById;
    std::vector<std::string> mHistory;
    std::map<std::string, std::string> mLabels;
};

} // namespace songbrowser
