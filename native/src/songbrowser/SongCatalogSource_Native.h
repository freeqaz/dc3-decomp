#pragma once
// Song browser data - the catalog of the running game
// (docs/native/reference/SONG_BROWSER_DATA.md).
//
// The one place of the song browser's data layer that knows the game: it
// fills a SongCatalog from TheHamSongMgr, the active profile and the play
// history.  Native only; the Xbox build does not see this file.  Everything
// here is for the main thread.

#include "SongCatalog.h"

#include <string>

namespace songbrowser {

// Whether the game is far enough along to answer (the song manager's content
// is in and the unlock table exists).  False during boot.
bool GameCatalogReady();

// Clears `catalog` and fills it with the songs song select lists, the play
// history and the localized section labels.
//
// The songs are exactly the ones SongSortMgr::RebuildSongRecordMap offers
// today's song select: every ranked song (TheHamSongMgr.GetRankedSongs) that
// is not fake and whose content is unlocked.  With `includeLocked` the songs
// that only fail the last test are added too, with SongEntry::locked set.
// Titles, artists and albums are the metadata's own strings, as song select
// shows them (they are not locale tokens).
//
// Scores are the active profile's (TheProfileMgr.GetActiveProfile(true));
// without a profile every song has no stars and no score.
void FillCatalogFromGame(SongCatalog &catalog, bool includeLocked = false);

// The body of GET /api/songs, for HttpServer.  `options` is the query
// string's other parameters as "key=value;key=value":
//   q=1          `query` was given: answer the search result
//   sort=NAME    answer that arrangement (title, artist, difficulty, source,
//                decade, bpm) instead of the catalog
//   limit=N      at most N search rows
//   recents=N    rows of Recently Played (in the catalog's "recents" and at
//                the top of an arrangement); default 6
//   min=N        ArrangeOptions::minSectionSongs
//   locked=1     include locked songs
// False, with `error` set, when the game is not ready or `sort` is unknown.
bool SongsEndpointJson(
    const std::string &query, const std::string &options, std::string &json, std::string &error
);

} // namespace songbrowser
