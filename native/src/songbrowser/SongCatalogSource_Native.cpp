// Song browser data - the catalog of the running game.
// See SongCatalogSource_Native.h.

#include "SongCatalogSource_Native.h"

#include "SongCatalogJson.h"
#include "SongSearch.h"
#include "SongSections.h"

#include "meta_ham/AccomplishmentManager.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/HamSongMetadata.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/SongStatusMgr.h"
#include "platform/PgSongs_Native.h"
#include "platform/SongSelectState_Native.h"
#include "utl/Locale.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

std::string NativeContentCoverRoot(const char *contentName); // platform/ContentMgr_Native.cpp

namespace songbrowser {

namespace {

// The path the browser reads a song's cover from.  A disc song's is the
// game's own (HamSongMgr::GetAlbumArtPath).  A downloaded song's cover is
// inside its package, and the game's path reaches it only through the
// package's drive, which is there only while the package is mounted: the
// game mounts the package of the one song it is about to show or play, and
// SongAudioData puts the drive on the path then.  The browser shows a
// screenful of covers at once, so for those it goes through the drive the
// content manager keeps on the DLC root, mounted or not.
std::string CoverPath(Symbol shortName) {
    const char *art = TheHamSongMgr.GetAlbumArtPath(shortName);
    if (!art || !*art) return std::string();
    const char *content = TheHamSongMgr.ContentName(shortName, false);
    if (!content || !*content) return art;
    const std::string root = NativeContentCoverRoot(content);
    if (root.empty()) return art; // no drive on its root: the game's path
    // "cnt00000003:/songs/x/x_keep.png" while mounted, "songs/x/x_keep.png"
    // otherwise: the part inside the package either way.
    const char *colon = strchr(art, ':');
    const char *inside = colon ? colon + 1 : art;
    while (*inside == '/' || *inside == '\\') inside++;
    return root + "/" + inside;
}

// The locale's text for `token`, into the catalog's labels; nothing when the
// locale has none (the pure code then uses its English default).
void AddLabel(SongCatalog &catalog, const char *token) {
    bool found = false;
    const char *text = Localize(Symbol(token), &found, TheLocale);
    if (found && text && *text) catalog.SetLabel(token, text);
}

// "key=value;key=value" -> the value of `key`, or `fallback`.
std::string Option(const std::string &options, const char *key, const char *fallback) {
    std::string wanted = std::string(key) + "=";
    size_t at = 0;
    while (at < options.size()) {
        size_t end = options.find(';', at);
        if (end == std::string::npos) end = options.size();
        if (options.compare(at, wanted.size(), wanted) == 0) {
            return options.substr(at + wanted.size(), end - at - wanted.size());
        }
        at = end + 1;
    }
    return fallback;
}

} // namespace

bool GameCatalogReady() {
    // IsContentUnlocked asks TheAccomplishmentMgr, which does not exist yet
    // while the game is still starting up.
    return TheAccomplishmentMgr != nullptr;
}

void FillCatalogFromGame(SongCatalog &catalog, bool includeLocked) {
    catalog.Clear();
    if (!GameCatalogReady()) return;

    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    SongStatusMgr *statuses = profile ? profile->GetSongStatusMgr() : nullptr;

    // SongSortMgr::RebuildSongRecordMap's song eligibility rules. The
    // manager's own map cannot be asked instead: it is only rebuilt when song
    // select is entered, and the sorts hold pointers into it.
    std::vector<int> rankedSongs;
    TheHamSongMgr.GetRankedSongs(rankedSongs);
    std::set<int> tiers;
    for (size_t i = 0; i < rankedSongs.size(); i++) {
        const HamSongMetadata *metadata = TheHamSongMgr.Data(rankedSongs[i]);
        if (!metadata || metadata->IsFake()
            || !NativePgSongs::AllowsSong(metadata->ShortName())) {
            continue;
        }
        bool unlocked = TheProfileMgr.IsContentUnlocked(metadata->ShortName());
        if (!unlocked && !includeLocked) continue;

        SongEntry entry;
        entry.id = metadata->ID();
        // SongRecord's short name: the song manager's for this id.
        Symbol shortName = TheHamSongMgr.GetShortNameFromSongID(metadata->ID(), true);
        entry.shortName = shortName.Str();
        entry.title = metadata->Title();
        entry.artist = metadata->Artist();
        entry.album = metadata->Album();
        entry.year = metadata->YearReleased();
        entry.bpm = metadata->Bpm();
        entry.lengthMs = metadata->LengthMs();
        entry.rank = metadata->Rank();
        entry.difficultyTier = TheHamSongMgr.RankTier(metadata->Rank()); // SongRecord's tier
        entry.origin = metadata->GameOrigin().Str();
        entry.source = SourceFromOrigin(entry.origin);
        entry.onDisc = metadata->IsOnDisc();
        entry.isCover = metadata->IsCover();
        entry.albumArtPath = CoverPath(shortName);
        entry.locked = !unlocked;
        // HamStarsDisplay::SetSongImpl, mode 0: the best run over every
        // difficulty, counted once it has a score.
        if (statuses && statuses->HasSongStatus(entry.id)) {
            const SongStatusData &best =
                statuses->GetSongStatus(entry.id).GetBestSongStatusData();
            if (best.mScore > 0) {
                entry.bestScore = best.mScore;
                entry.bestStars = best.mStars;
                entry.played = true;
            }
        }
        catalog.Add(entry);
        tiers.insert(entry.difficultyTier);
    }

    catalog.SetRecents(TheNativeSongSelectState().History());

    // Section names, in the game's language.
    for (std::set<int>::iterator it = tiers.begin(); it != tiers.end(); ++it) {
        if (*it >= 0) AddLabel(catalog, TheHamSongMgr.RankTierToken(*it).Str());
    }
    AddLabel(catalog, "no_part");
    AddLabel(catalog, "ham1");
    AddLabel(catalog, "ham2");
    AddLabel(catalog, "ham3");
    AddLabel(catalog, "DLC");
}

bool SongsEndpointJson(
    const std::string &query, const std::string &options, std::string &json, std::string &error
) {
    if (!GameCatalogReady()) {
        error = "the game is still starting up (no unlock table yet)";
        return false;
    }
    SongCatalog catalog;
    FillCatalogFromGame(catalog, Option(options, "locked", "0") != "0");
    int recentRows = atoi(Option(options, "recents", "6").c_str());

    if (Option(options, "q", "0") != "0") {
        SongSearch search(catalog);
        std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
        SearchResult result = search.Search(query, atoi(Option(options, "limit", "0").c_str()));
        long long micros = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - before
        )
                               .count();
        json = SearchJson(catalog, result, query);
        // How long the search itself took, for whoever is measuring.
        json.insert(1, "\"searchMicros\":" + std::to_string(micros) + ",");
        return true;
    }

    std::string sort = Option(options, "sort", "");
    if (!sort.empty()) {
        Arrangement arrangement;
        if (!ArrangementFromName(sort, arrangement)) {
            error = "unknown sort '" + sort
                + "' (title, artist, difficulty, source, decade, bpm)";
            return false;
        }
        ArrangeOptions arrange;
        arrange.recentRows = recentRows;
        arrange.minSectionSongs = atoi(Option(options, "min", "1").c_str());
        json = ArrangementJson(catalog, Arrange(catalog, arrangement, arrange));
        return true;
    }

    json = CatalogJson(catalog, recentRows);
    return true;
}

} // namespace songbrowser
